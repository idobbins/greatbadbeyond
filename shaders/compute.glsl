#version 450

#include "bindings.inc.glsl"

layout(constant_id = 0) const uint WG_X = 16u;
layout(constant_id = 1) const uint WG_Y = 16u;
layout(local_size_x_id = 0, local_size_y_id = 1, local_size_z = 1) in;

layout(binding = B_TARGET, rgba8) uniform writeonly image2D uTarget;

layout(std430, binding = B_SPHERE_CR) buffer SphereCR  { vec4 sphereCR[];  };
layout(std430, binding = B_SPHERE_ALB) buffer SphereAlb { vec4 sphereAlb[]; };
layout(std430, binding = B_HIT_T) buffer HitTBuf { float hitT[]; };
layout(std430, binding = B_HIT_N) buffer HitNBuf { vec4 hitN[]; };

layout(push_constant) uniform PC {
    uvec2 size;
    uint frame;
    uint sphereCount;
    uint accumulationEpoch;

    float tanHalfFovY;
    float aspect;
    float _pad0;
    vec3 camPos;   float _pad1;
    vec3 camFwd;   float _pad2;
    vec3 camRight; float _pad3;
    vec3 camUp;    float _pad4;

    vec2 worldMin;
    vec2 worldMax;
    float groundY;
    float _pad5[3];
} pc;

const float kHuge = 1e30;
const float kSurfaceBias = 1e-3;

vec3 computeRayDir(uvec2 pix)
{
    vec2 res = vec2(max(pc.size.x, 1u), max(pc.size.y, 1u));
    vec2 uv = (vec2(pix) + vec2(0.5)) / res * 2.0 - 1.0;
    uv.x *= pc.aspect;

    float t = pc.tanHalfFovY;
    return normalize(uv.x * t * pc.camRight + uv.y * t * pc.camUp + pc.camFwd);
}

bool intersectSphere(vec3 ro, vec3 rd, vec4 cr, out float t, out vec3 n)
{
    vec3 oc = ro - cr.xyz;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - cr.w * cr.w;
    float h = b * b - c;
    if (h < 0.0)
    {
        return false;
    }
    h = sqrt(h);
    float t0 = -b - h;
    float t1 = -b + h;
    t = (t0 > 0.0) ? t0 : ((t1 > 0.0) ? t1 : -1.0);
    if (t <= 0.0)
    {
        return false;
    }
    vec3 p = ro + t * rd;
    n = normalize(p - cr.xyz);
    return true;
}

bool intersectPlaneY(vec3 ro, vec3 rd, float y, out float t, out vec3 n)
{
    float denom = rd.y;
    if (abs(denom) < 1e-6)
    {
        return false;
    }
    t = (y - ro.y) / denom;
    if (t <= 0.0)
    {
        return false;
    }
    n = vec3(0.0, (denom < 0.0) ? 1.0 : -1.0, 0.0);
    return true;
}

#if defined(KERNEL_PRIMARY_INTERSECT) || defined(KERNEL_SHADE_SHADOW)
bool findClosestSphere(vec3 ro, vec3 rd, inout float bestT, inout vec3 bestN, inout float bestId)
{
    bool hit = false;
    for (uint i = 0u; i < pc.sphereCount; ++i)
    {
        float t; vec3 n;
        if (intersectSphere(ro, rd, sphereCR[i], t, n) && (t < bestT))
        {
            bestT = t;
            bestN = n;
            bestId = float(i + 1u);
            hit = true;
        }
    }
    return hit;
}
#endif

#ifdef KERNEL_PRIMARY_INTERSECT
void main()
{
    uvec2 id = gl_GlobalInvocationID.xy;
    if (id.x >= pc.size.x || id.y >= pc.size.y)
    {
        return;
    }
    uint pix = id.y * pc.size.x + id.x;

    vec3 ro = pc.camPos;
    vec3 rd = computeRayDir(id);

    float bestT = kHuge;
    vec3 bestN = vec3(0.0);
    float bestId = -1.0;

    findClosestSphere(ro, rd, bestT, bestN, bestId);

    float tPlane; vec3 nPlane;
    if (intersectPlaneY(ro, rd, pc.groundY, tPlane, nPlane) && (tPlane < bestT))
    {
        bestT = tPlane;
        bestN = nPlane;
        bestId = 0.0;
    }

    if (bestT == kHuge)
    {
        hitT[pix] = -1.0;
        hitN[pix] = vec4(0.0, 1.0, 0.0, -1.0);
    }
    else
    {
        hitT[pix] = bestT;
        hitN[pix] = vec4(bestN, bestId);
    }
}
#endif

#ifdef KERNEL_SHADE_SHADOW
uint wangHash(uint s)
{
    s = (s ^ 61u) ^ (s >> 16);
    s *= 9u;
    s ^= (s >> 4);
    s *= 0x27d4eb2du;
    s ^= (s >> 15);
    return s;
}

float rnd(inout uint s)
{
    s = wangHash(s);
    return float(s) * (1.0 / 4294967296.0);
}

layout(std430, binding = B_ACCUM) buffer AccumBuf { vec4 accum[]; };
layout(std430, binding = B_SPP) buffer SppBuf { uint spp[]; };
layout(std430, binding = B_EPOCH) buffer EpochBuf { uint epochBuf[]; };

const vec3 kLightDir = normalize(vec3(0.4, 1.0, 0.2));
const vec3 kPlaneAlb = vec3(0.8, 0.8, 0.8);
const int MAX_DEPTH = 2;
const int RR_DEPTH = 2;

bool traceNearest(vec3 ro, vec3 rd, out float bestT, out vec3 bestN, out int matId)
{
    bestT = kHuge;
    bestN = vec3(0.0);
    matId = -1;
    float bestId = -1.0;
    bool hit = findClosestSphere(ro, rd, bestT, bestN, bestId);

    float tPlane;
    vec3 nPlane;
    if (intersectPlaneY(ro, rd, pc.groundY, tPlane, nPlane) && (tPlane < bestT))
    {
        bestT = tPlane;
        bestN = nPlane;
        bestId = 0.0;
        hit = true;
    }

    if (!hit)
    {
        return false;
    }

    matId = int(bestId);
    return true;
}

bool occludedWorld(vec3 ro, vec3 rd, float maxT)
{
    float tPlane;
    vec3 nPlane;
    if (intersectPlaneY(ro, rd, pc.groundY, tPlane, nPlane) && tPlane > 0.0 && tPlane < maxT)
    {
        return true;
    }

    for (uint i = 0u; i < pc.sphereCount; ++i)
    {
        float t;
        vec3 n;
        if (intersectSphere(ro, rd, sphereCR[i], t, n) && (t > 0.0) && (t < maxT))
        {
            return true;
        }
    }
    return false;
}

vec3 skyColor(vec3 rd)
{
    float v = clamp(0.5 * rd.y + 0.5, 0.0, 1.0);
    return mix(vec3(0.7, 0.8, 1.0), vec3(0.2, 0.3, 0.6), 1.0 - v);
}

void onb(in vec3 n, out vec3 t, out vec3 b)
{
    if (abs(n.z) < 0.999)
    {
        t = normalize(cross(vec3(0.0, 0.0, 1.0), n));
    }
    else
    {
        t = normalize(cross(vec3(0.0, 1.0, 0.0), n));
    }
    b = cross(n, t);
}

vec3 sampleCosineHemisphere(inout uint s)
{
    float u1 = rnd(s);
    float u2 = rnd(s);
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - u1));
    return vec3(x, y, z);
}

vec3 materialAlbedo(int matId)
{
    return (matId == 0) ? kPlaneAlb : sphereAlb[matId - 1].rgb;
}

void main()
{
    uvec2 id = gl_GlobalInvocationID.xy;
    if (id.x >= pc.size.x || id.y >= pc.size.y)
    {
        return;
    }
    uint pix = id.y * pc.size.x + id.x;

    uint seed = uint(pc.frame) ^ (pix * 0x9e3779b9u) ^ 0xA511E9B3u;

    vec2 res = vec2(max(pc.size.x, 1u), max(pc.size.y, 1u));
    vec2 jitter = vec2(rnd(seed), rnd(seed)) - 0.5;
    vec2 uv = ((vec2(id) + 0.5 + jitter) / res) * 2.0 - 1.0;
    uv.x *= pc.aspect;
    float tcam = pc.tanHalfFovY;
    vec3 rd0 = normalize(uv.x * tcam * pc.camRight + uv.y * tcam * pc.camUp + pc.camFwd);

    vec3 ro = pc.camPos;
    vec3 rd = rd0;
    vec3 L = vec3(0.0);
    vec3 beta = vec3(1.0);

    for (int depth = 0; depth < MAX_DEPTH; ++depth)
    {
        float t;
        vec3 n;
        int matId;
        if (!traceNearest(ro, rd, t, n, matId))
        {
            L += beta * skyColor(rd);
            break;
        }

        vec3 pos = ro + t * rd + n * kSurfaceBias;
        vec3 alb = materialAlbedo(matId);

        float ndl = max(0.0, dot(n, kLightDir));
        if (ndl > 0.0)
        {
            bool blocked = occludedWorld(pos, kLightDir, kHuge);
            if (!blocked)
            {
                L += beta * (alb * ndl);
            }
        }

        vec3 T;
        vec3 B;
        onb(n, T, B);
        vec3 wiL = sampleCosineHemisphere(seed);
        vec3 wi = normalize(wiL.x * T + wiL.y * B + wiL.z * n);
        beta *= alb;

        if (depth >= RR_DEPTH)
        {
            float p = clamp(max(max(beta.r, beta.g), beta.b), 0.05, 0.99);
            if (rnd(seed) > p)
            {
                break;
            }
            beta /= p;
        }

        ro = pos;
        rd = wi;
    }

    if (epochBuf[pix] != pc.accumulationEpoch)
    {
        accum[pix] = vec4(0.0);
        spp[pix] = 0u;
        epochBuf[pix] = pc.accumulationEpoch;
    }

    uint oldCount = spp[pix];
    vec3 sum = accum[pix].rgb + L;
    uint newCount = oldCount + 1u;
    accum[pix] = vec4(sum, 1.0);
    spp[pix] = newCount;

    vec3 avg = sum / float(newCount);
    vec3 mapped = sqrt(clamp(avg, 0.0, 1.0));
    imageStore(uTarget, ivec2(id), vec4(mapped, 1.0));
}
#endif
