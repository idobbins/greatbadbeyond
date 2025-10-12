#version 450

// --------------------- config ---------------------
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

const int   MAX_BOUNCES = 2;
const float T_MIN       = 1e-3;
const float T_MAX       = 1e30;

// ------------------ outputs -----------------------
layout(set = 0, binding = 0, rgba8) writeonly uniform image2D out_image;

// ---------------- SoA sphere buffers --------------
layout(std430, set = 0, binding = 1) readonly buffer SphereX { float s_cx[]; };
layout(std430, set = 0, binding = 2) readonly buffer SphereY { float s_cy[]; };
layout(std430, set = 0, binding = 3) readonly buffer SphereZ { float s_cz[]; };
layout(std430, set = 0, binding = 4) readonly buffer SphereR { float s_r[];  };
layout(std430, set = 0, binding = 5) readonly buffer ColR    { float s_cr[]; };
layout(std430, set = 0, binding = 6) readonly buffer ColG    { float s_cg[]; };
layout(std430, set = 0, binding = 7) readonly buffer ColB    { float s_cb[]; };

// ---------------- camera & params -----------------
layout(std140, set = 0, binding = 8) uniform Camera
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

// Branchless ray-sphere. Returns (t, normal, albedo) via refs.
void hit_spheres(vec3 ro, vec3 rd, out float tmin, out vec3 nmin, out vec3 alb)
{
    tmin = T_MAX;
    nmin = vec3(0);
    alb  = vec3(0);

    for (uint i = 0u; i < u.sphere_count; ++i) {
        vec3  c  = vec3(s_cx[i], s_cy[i], s_cz[i]);
        float r  = s_r[i];

        vec3  oc = ro - c;
        float b  = dot(oc, rd);
        float c2 = dot(oc, oc) - r * r;
        float disc = b*b - c2;
        float hit  = step(0.0, disc);                  // 1 if disc >= 0
        float sd   = sqrt(max(disc, 0.0));
        float t0   = -b - sd;
        float t1   = -b + sd;

        // choose frontmost positive root; INF otherwise
        float t    = mix(T_MAX, t0, step(T_MIN, t0));
        t          = mix(t, t1, (1.0 - step(T_MIN, t0)) * step(T_MIN, t1));
        t          = mix(T_MAX, t, hit);

        float sel  = step(t, tmin);                    // 1 if this t < current best
        tmin = mix(tmin, t, sel);

        // Compute normal & albedo for this candidate and branchlessly keep if selected
        vec3  p    = ro + t * rd;
        vec3  n    = normalize(p - c);
        vec3  k    = vec3(s_cr[i], s_cg[i], s_cb[i]);

        nmin = mix(nmin, n, sel);
        alb  = mix(alb,  k, sel);
    }
}

vec3 sky(vec3 rd) {
    float t = 0.5 * (rd.y + 1.0);
    vec3 horizon = vec3(0.82, 0.82, 0.86);
    vec3 zenith = vec3(0.97, 0.99, 1.02);
    vec3 base = mix(horizon, zenith, t);

    vec3 sun_dir = normalize(vec3(0.3, 0.6, 0.7));
    float sun = pow(max(dot(rd, sun_dir), 0.0), 64.0);
    vec3 sun_color = vec3(1.35, 1.25, 1.05);

    return base + sun_color * sun;     // soft sun highlight for extra light
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
    float alive     = 1.0;

    for (int bounce = 0; bounce < MAX_BOUNCES; ++bounce) {
        float t; vec3 n; vec3 albedo;
        hit_spheres(ro, rd, t, n, albedo);

        float hit = step(T_MIN, t) * step(t, T_MAX*0.5);  // 1 if valid hit
        vec3  hitP = ro + t * rd;

        // Miss: add sky and kill path (branchless)
        vec3 Lsky = sky(rd);
        radiance += (1.0 - hit) * alive * throughput * Lsky;
        alive     = alive * hit;

        // Next bounce (Lambert)
        vec3 new_ro = hitP + n * 1e-3;
        vec3 new_rd = sample_cos_hemisphere(n, seed);
        ro = mix(ro, new_ro, hit);
        rd = normalize(mix(rd, new_rd, hit));

        throughput *= mix(vec3(1.0), albedo, hit); // only on hit

        // Russian roulette after 2 bounces (branchless)
        float rrP = 0.9;
        float rrMask = step(2.0, float(bounce));   // 1 for bounce >= 2
        float survive = step(rng(seed), rrP);
        float rr = mix(1.0, survive, rrMask);      // if rrMask==1, maybe zero
        throughput *= mix(vec3(1.0), vec3(1.0/rrP), rrMask * survive);
        alive *= rr;                                // if killed, alive=0, path continues but contributes 0
    }

    // simple tonemap + gamma
    radiance += alive * throughput * sky(rd);
    vec3 color = radiance;
    color = color / (color + 1.0);                 // reinhard
    color = pow(color, vec3(1.0/2.2));
    imageStore(out_image, gid, vec4(color, 1.0));
}
