#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

const float EPS   = 1e-8;
const float FMAX  = 1e30;

const int   MAX_BOUNCES = 4;
const float T_MIN       = 1e-3;
const float T_MAX       = 1e30;
const uint  MISS_ID     = 0xFFFFFFFFu;

// ------------------ outputs -----------------------
layout(set = 0, binding = 0, rgba32f) writeonly uniform image2D out_image;

// --------------- sphere buffers (vec4) -------------
layout(std430, set = 0, binding = 1) readonly buffer SphereCenterRadius {
    vec4 sphere_center_radius[];
};
layout(std430, set = 0, binding = 2) readonly buffer SphereAlbedo {
    vec4 sphere_albedo[];
};

// ---------------- camera & params -----------------
layout(std140, set = 0, binding = 3) uniform Camera
{
    vec3  cam_pos;       float _pad0;
    vec3  cam_fwd;       float _pad1;
    vec3  cam_right;     float _pad2;
    vec3  cam_up;        float _pad3;
    float tan_half_fovy; float aspect;
    uint  frame_index;   uint  sphere_count;
} u;

// ----------------- grid (two-level) ----------------
layout(std140, set = 0, binding = 4) uniform GridParams {
    vec3 g_bmin; float _gp0;
    vec3 g_bmax; float _gp1;
    uvec3 g_dims; uint _gp2;
    vec3 g_inv_cell; uint _gp3; // 1 / cell_size
};

layout(std430, set = 0, binding = 5) readonly buffer L0Cells {
    // x=start (into indices), y=count, z=child_base (index into L1 array; 0xFFFFFFFF if none), w=0:leaf,1:has child
    uvec4 l0_cells[];
};
layout(std430, set = 0, binding = 6) readonly buffer L1Cells {
    // x=start (into indices), y=count
    uvec2 l1_cells[];
};
layout(std430, set = 0, binding = 7) readonly buffer CellIndices {
    uint cell_indices[];
};

// ---------------- RNG ------------------
uint pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    uint word = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (word >> 22u) ^ word;
}
float rng(inout uint state) {
    state = pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

// cosine-weighted hemisphere
vec3 sample_cos_hemisphere(vec3 N, inout uint seed) {
    float u1 = rng(seed);
    float u2 = rng(seed);
    float r  = sqrt(u1);
    float t  = 6.28318530718 * u2;
    vec3 d   = vec3(r * cos(t), r * sin(t), sqrt(max(0.0, 1.0 - u1)));

    vec3 up = mix(vec3(0,0,1), vec3(1,0,0), step(0.999, abs(N.z)));
    vec3 T  = normalize(cross(up, N));
    vec3 B  = cross(N, T);
    return normalize(T * d.x + B * d.y + N * d.z);
}

vec3 random_in_unit_sphere(inout uint seed) {
    for (int i = 0; i < 8; ++i) {
        vec3 p = vec3(rng(seed), rng(seed), rng(seed)) * 2.0 - vec3(1.0);
        if (dot(p, p) < 1.0) {
            return p;
        }
    }
    vec3 p = vec3(rng(seed), rng(seed), rng(seed)) * 2.0 - vec3(1.0);
    return normalize(p);
}

float schlick(float cosine, float ref_idx) {
    float r0 = (1.0 - ref_idx) / (1.0 + ref_idx);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

vec3 sky(vec3 rd) {
    float t = 0.5 * (normalize(rd).y + 1.0);
    return mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
}

bool intersect_ground_plane(vec3 ro, vec3 rd, out float t) {
    const float ground_y = 0.0;
    if (abs(rd.y) < 1e-5) return false;
    float hit_t = (ground_y - ro.y) / rd.y;
    if (hit_t <= T_MIN) return false;
    vec3 p = ro + hit_t * rd;
    if (p.x < g_bmin.x || p.x > g_bmax.x || p.z < g_bmin.z || p.z > g_bmax.z) return false;
    t = hit_t;
    return true;
}

// ----------------- intersections -------------------
bool intersect_sphere_id(vec3 ro, vec3 rd, uint sid, out float t) {
    vec4 s = sphere_center_radius[sid];
    vec3 oc = ro - s.xyz;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - s.w * s.w;
    float disc = b*b - c;
    if (disc < 0.0) return false;
    float sd = sqrt(disc);
    float t0 = -b - sd;
    float t1 = -b + sd;
    if (t0 > T_MIN) { t = t0; return true; }
    if (t1 > T_MIN) { t = t1; return true; }
    return false;
}

bool intersect_aabb(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out float t0, out float t1) {
    vec3 inv = 1.0 / rd;
    vec3 tmin = (bmin - ro) * inv;
    vec3 tmax = (bmax - ro) * inv;
    vec3 tsm = min(tmin, tmax);
    vec3 tbg = max(tmin, tmax);
    t0 = max(max(tsm.x, tsm.y), tsm.z);
    t1 = min(min(tbg.x, tbg.y), tbg.z);
    return t1 >= max(t0, 0.0);
}

uint l0_index(ivec3 c, ivec3 dims) {
    return uint(c.x + dims.x * (c.y + dims.y * c.z));
}
uint l1_index(ivec3 c, int dim) {
    return uint(c.x + dim * (c.y + dim * c.z));
}

// returns nearest hit (id & t) across grid with early-out per cell
void hit_grid_min(vec3 ro, vec3 rd, out float tmin, out uint idmin) {
    tmin = T_MAX;
    idmin = MISS_ID;

    float tg0, tg1;
    if (!intersect_aabb(ro, rd, g_bmin, g_bmax, tg0, tg1)) {
        return;
    }

    vec3 cell_size = 1.0 / g_inv_cell;
    ivec3 dims = ivec3(g_dims);

    float t = max(tg0, 0.0);
    vec3 p = ro + t * rd;

    ivec3 c = clamp(ivec3(floor((p - g_bmin) * g_inv_cell)), ivec3(0), dims - ivec3(1));
    ivec3 stepv = ivec3(rd.x >= 0.0 ? 1 : -1,
                        rd.y >= 0.0 ? 1 : -1,
                        rd.z >= 0.0 ? 1 : -1);

    vec3 off = step(vec3(0.0), rd);
    vec3 inv_rd = 1.0 / rd;

    vec3 next_b = g_bmin + (vec3(c) + off) * cell_size;
    vec3 t_max = vec3(
        mix(FMAX, (next_b.x - ro.x) * inv_rd.x, step(EPS, abs(rd.x))),
        mix(FMAX, (next_b.y - ro.y) * inv_rd.y, step(EPS, abs(rd.y))),
        mix(FMAX, (next_b.z - ro.z) * inv_rd.z, step(EPS, abs(rd.z)))
    );
    vec3 t_delta = vec3(
        mix(FMAX, cell_size.x * abs(inv_rd.x), step(EPS, abs(rd.x))),
        mix(FMAX, cell_size.y * abs(inv_rd.y), step(EPS, abs(rd.y))),
        mix(FMAX, cell_size.z * abs(inv_rd.z), step(EPS, abs(rd.z)))
    );

    const int CHILD_DIM = 4;
    vec3 child_cell_size = cell_size / float(CHILD_DIM);

    for (int guard = 0; guard < 8192; ++guard) {
        if (t > tg1) break;
        float t_cell_exit = min(t_max.x, min(t_max.y, t_max.z));
        uint ci = l0_index(c, dims);
        uvec4 h = l0_cells[ci];

        // Leaf: test the list
        if (h.w == 0u) {
            float best_cell_t = T_MAX;
            uint best_cell_id = MISS_ID;
            for (uint k = 0u; k < h.y; ++k) {
                uint sid = cell_indices[h.x + k]; // > 0 (ground excluded when building grid)
                float th;
                if (intersect_sphere_id(ro, rd, sid, th) && th <= t_cell_exit && th < best_cell_t) {
                    best_cell_t = th;
                    best_cell_id = sid;
                }
            }
            if (best_cell_id != MISS_ID) {
                tmin = best_cell_t;
                idmin = best_cell_id;
                return;
            }
        } else {
            // Child grid inside this macro-cell
            vec3 macro_min = g_bmin + vec3(c) * cell_size;

            // start at current t (inside macro cell), run inner DDA until t_cell_exit
            vec3 pch = ro + t * rd;
            ivec3 ch_dims = ivec3(CHILD_DIM);
            vec3 rel = (pch - macro_min) / child_cell_size;
            ivec3 cc = clamp(ivec3(floor(rel)), ivec3(0), ch_dims - ivec3(1));

            vec3 ch_off = off;
            vec3 ch_next_b = macro_min + (vec3(cc) + ch_off) * child_cell_size;
            vec3 ch_t_max = vec3(
                mix(FMAX, (ch_next_b.x - ro.x) * inv_rd.x, step(EPS, abs(rd.x))),
                mix(FMAX, (ch_next_b.y - ro.y) * inv_rd.y, step(EPS, abs(rd.y))),
                mix(FMAX, (ch_next_b.z - ro.z) * inv_rd.z, step(EPS, abs(rd.z)))
            );
            vec3 ch_t_delta = vec3(
                mix(FMAX, child_cell_size.x * abs(inv_rd.x), step(EPS, abs(rd.x))),
                mix(FMAX, child_cell_size.y * abs(inv_rd.y), step(EPS, abs(rd.y))),
                mix(FMAX, child_cell_size.z * abs(inv_rd.z), step(EPS, abs(rd.z)))
            );

            for (int g2 = 0; g2 < 512; ++g2) {
                float t_ch_exit = min(ch_t_max.x, min(ch_t_max.y, ch_t_max.z));
                if (t > t_cell_exit || t > tg1) break;

                uint cci = h.z + l1_index(cc, CHILD_DIM);
                uvec2 h1 = l1_cells[cci];

                float limit = min(t_ch_exit, t_cell_exit);
                float best_child_t = T_MAX;
                uint best_child_id = MISS_ID;

                for (uint k = 0u; k < h1.y; ++k) {
                    uint sid = cell_indices[h1.x + k];
                    float th;
                    if (intersect_sphere_id(ro, rd, sid, th) && th <= limit && th < best_child_t) {
                        best_child_t = th;
                        best_child_id = sid;
                    }
                }

                if (best_child_id != MISS_ID) {
                    tmin = best_child_t;
                    idmin = best_child_id;
                    return;
                }

                float mch = min(ch_t_max.x, min(ch_t_max.y, ch_t_max.z));
                float cselx = step(ch_t_max.x, ch_t_max.y) * step(ch_t_max.x, ch_t_max.z);
                float csely = (1.0 - cselx) * step(ch_t_max.y, ch_t_max.z);
                float cselz = 1.0 - max(cselx, csely);

                cc += ivec3(int(cselx), int(csely), int(cselz)) * stepv;
                t = mch;
                ch_t_max += vec3(cselx, csely, cselz) * ch_t_delta;

                if (any(lessThan(cc, ivec3(0))) || any(greaterThanEqual(cc, ch_dims))) break;
            }
        }

        float m = min(t_max.x, min(t_max.y, t_max.z));
        float selx = step(t_max.x, t_max.y) * step(t_max.x, t_max.z);
        float sely = (1.0 - selx) * step(t_max.y, t_max.z);
        float selz = 1.0 - max(selx, sely);

        c += ivec3(int(selx), int(sely), int(selz)) * stepv;
        t = m;
        t_max += vec3(selx, sely, selz) * t_delta;

        if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, dims))) break;
    }
}

// -------------------------- main --------------------------
void main() {
    ivec2 gid  = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dims_img = imageSize(out_image);
    if (gid.x >= dims_img.x || gid.y >= dims_img.y) return;

    uint seed = uint(gid.x) * 1973u ^ uint(gid.y) * 9277u ^ u.frame_index * 26699u ^ 0x9E3779B9u;

    vec2 uv = (vec2(gid) + vec2(rng(seed), rng(seed))) / vec2(dims_img);
    vec2 ndc = vec2(2.0*uv - 1.0);
    vec2 lens = vec2(ndc.x * u.aspect * u.tan_half_fovy, ndc.y * u.tan_half_fovy);

    vec3 ro = u.cam_pos;
    vec3 rd = normalize(u.cam_fwd + lens.x * u.cam_right + lens.y * u.cam_up);

    vec3 throughput = vec3(1.0);
    vec3 radiance   = vec3(0.0);
    bool path_alive = true;

    for (int bounce = 0; bounce < MAX_BOUNCES; ++bounce) {
        if (!path_alive) break;

        float t_plane = T_MAX;
        bool have_plane = intersect_ground_plane(ro, rd, t_plane);

        float t_grid = T_MAX;
        uint id_grid;
        hit_grid_min(ro, rd, t_grid, id_grid);
        bool have_grid = (id_grid != MISS_ID);

        bool choose_plane = have_plane && (!have_grid || t_plane <= t_grid);
        float take_plane = choose_plane ? 1.0 : 0.0;

        float t = mix(t_grid, t_plane, take_plane);
        bool miss = !(have_plane || have_grid);
        if (miss) { radiance += throughput * sky(rd); break; }

        bool hit_plane = (take_plane > 0.5);
        uint sid = hit_plane ? MISS_ID : id_grid;

        vec3 hit_pos = ro + t * rd;
        vec3 normal;
        vec3 albedo;
        float mat_param = 0.0;

        if (hit_plane) {
            normal = vec3(0.0, 1.0, 0.0);
            albedo = vec3(0.9);
        } else {
            vec4 sphere = sphere_center_radius[sid];
            vec4 alb    = sphere_albedo[sid];
            vec3 center = sphere.xyz;
            normal = (hit_pos - center) * (1.0 / sphere.w);
            albedo = alb.rgb;
            mat_param = alb.w;
        }

        if (mat_param < -1e-4) {
            float ior = -mat_param;
            bool entering = dot(rd, normal) < 0.0;
            vec3 n = entering ? normal : -normal;
            float refraction_ratio = entering ? (1.0 / ior) : ior;
            float cos_theta = min(dot(-rd, n), 1.0);
            float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
            bool cannot_refract = refraction_ratio * sin_theta > 1.0;
            float reflect_prob = schlick(cos_theta, refraction_ratio);
            vec3 direction;
            if (cannot_refract || rng(seed) < reflect_prob) {
                direction = reflect(rd, n);
            } else {
                direction = refract(rd, n, refraction_ratio);
            }
            direction = normalize(direction);
            throughput *= albedo;
            ro = hit_pos + direction * 1e-3;
            rd = direction;
        } else if (mat_param > 1e-4) {
            vec3 reflected = reflect(rd, normal);
            vec3 fuzz_dir = mat_param * random_in_unit_sphere(seed);
            vec3 new_dir = normalize(reflected + fuzz_dir);
            if (dot(new_dir, normal) <= 0.0) {
                path_alive = false;
                throughput = vec3(0.0);
                break;
            }
            throughput *= albedo;
            ro = hit_pos + normal * 1e-3;
            rd = new_dir;
        } else {
            throughput *= albedo;
            ro = hit_pos + normal * 1e-3;
            rd = sample_cos_hemisphere(normal, seed);
        }

        if (bounce >= 3) {
            const float rrP = 0.9;
            if (rng(seed) > rrP) { path_alive = false; }
            else { throughput /= rrP; }
        }
    }

    if (path_alive) { radiance += throughput * sky(rd); }
    vec3 color = max(radiance, vec3(0.0));
    imageStore(out_image, gid, vec4(color, 1.0));
}
