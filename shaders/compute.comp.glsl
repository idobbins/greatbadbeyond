#version 450

// --------------------- config ---------------------
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

const int   MAX_BOUNCES = 4;
const float T_MIN       = 1e-3;
const float T_MAX       = 1e30;
const uint  MISS_ID     = 0xFFFFFFFFu;
const uint  TILE        = 64u;

// ------------------ outputs -----------------------
layout(set = 0, binding = 0, rgba8) writeonly uniform image2D out_image;

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

// ---------------- RNG (PCG hash) ------------------
// Based on Jarzynski & Olano "hash functions for GPU" / Nathan Reed notes.
uint pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    uint word = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (word >> 22u) ^ word;
}
float rng(inout uint state) {
    state = pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

// cosine-weighted hemisphere sample around N
vec3 sample_cos_hemisphere(vec3 N, inout uint seed) {
    float u1 = rng(seed);
    float u2 = rng(seed);
    float r  = sqrt(u1);
    float t  = 6.28318530718 * u2;
    vec3 d   = vec3(r * cos(t), r * sin(t), sqrt(max(0.0, 1.0 - u1)));

    // Orthonormal basis (branchless choose-up)
    vec3 up = mix(vec3(0,0,1), vec3(1,0,0), step(0.999, abs(N.z)));
    vec3 T  = normalize(cross(up, N));
    vec3 B  = cross(N, T);
    return normalize(T * d.x + B * d.y + N * d.z);
}

shared vec4 tile_center_radius[TILE];
shared vec4 tile_albedo[TILE];

// Find only tmin and sphere index using shared-memory tiling.
void hit_spheres_min_only(vec3 ro, vec3 rd, out float tmin, out uint idmin) {
    tmin = T_MAX;
    idmin = MISS_ID;

    for (uint base = 0u; base < u.sphere_count; base += TILE) {
        uint lane = gl_LocalInvocationIndex;
        uint idx  = base + lane;
        if (lane < TILE && idx < u.sphere_count) {
            tile_center_radius[lane] = sphere_center_radius[idx];
            tile_albedo[lane]        = sphere_albedo[idx];
        }

        barrier();

        uint count = min(TILE, u.sphere_count - base);
        for (uint j = 0u; j < count; ++j) {
            vec3 center = tile_center_radius[j].xyz;
            float radius = tile_center_radius[j].w;
            vec3 oc = ro - center;
            float b = dot(oc, rd);
            float c = dot(oc, oc) - radius * radius;
            float disc = b * b - c;
            if (disc < 0.0) {
                continue;
            }

            float sd = sqrt(disc);
            float t0 = -b - sd;
            float t1 = -b + sd;
            float t = (t0 > T_MIN) ? t0 : ((t1 > T_MIN) ? t1 : T_MAX);
            if (t < tmin) {
                tmin = t;
                idmin = base + j;
            }
        }

        barrier();
    }
}

vec3 sky(vec3 rd) {
    float t = 0.5 * (normalize(rd).y + 1.0);
    return mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
}

void main() {
    ivec2 gid  = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dims = imageSize(out_image);
    if (gid.x >= dims.x || gid.y >= dims.y) return;

    // Jittered primary ray (no accumulation buffer here; one sample / frame)
    uint seed = uint(gid.x) * 1973u ^ uint(gid.y) * 9277u ^ u.frame_index * 26699u ^ 0x9E3779B9u;

    // NDC in [-1,1]
    vec2 uv = (vec2(gid) + vec2(rng(seed), rng(seed))) / vec2(dims);
    vec2 ndc = vec2(2.0*uv - 1.0);
    vec2 lens = vec2(ndc.x * u.aspect * u.tan_half_fovy, ndc.y * u.tan_half_fovy);

    vec3 ro = u.cam_pos;
    vec3 rd = normalize(u.cam_fwd + lens.x * u.cam_right + lens.y * u.cam_up);

    vec3 throughput = vec3(1.0);
    vec3 radiance   = vec3(0.0);
    bool path_alive = true;

    for (int bounce = 0; bounce < MAX_BOUNCES; ++bounce) {
        if (!path_alive) {
            break;
        }

        float t;
        uint sid;
        hit_spheres_min_only(ro, rd, t, sid);

        if (sid == MISS_ID) {
            radiance += throughput * sky(rd);
            break;
        }

        vec4 sphere = sphere_center_radius[sid];
        vec4 alb    = sphere_albedo[sid];
        vec3 center = sphere.xyz;
        vec3 hit_pos = ro + t * rd;
        vec3 normal = normalize(hit_pos - center);
        vec3 albedo = alb.rgb;

        throughput *= albedo;

        ro = hit_pos + normal * 1e-3;
        rd = sample_cos_hemisphere(normal, seed);

        // Russian roulette after 3 bounces
        if (bounce >= 3) {
            const float rrP = 0.9;
            if (rng(seed) > rrP) {
                path_alive = false;
            } else {
                throughput /= rrP;
            }
        }
    }

    // simple tonemap (linear space)
    if (path_alive) {
        radiance += throughput * sky(rd);
    }
    vec3 color = radiance;
    color = color / (color + 1.0);                 // Reinhard (linear). No gamma here.
    imageStore(out_image, gid, vec4(color, 1.0));
}
