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

    vec3 camPos;   float fovY;
    vec3 camFwd;   float _pad0;
    vec3 camRight; float _pad1;
    vec3 camUp;    float _pad2;

    vec2 worldMin;
    vec2 worldMax;
    float groundY;
    float _pad3[3];
} pc;

const float kHuge = 1e30;
const float kSurfaceBias = 1e-3;

vec3 computeRayDir(uvec2 pix)
{
    vec2 res = vec2(max(pc.size.x, 1u), max(pc.size.y, 1u));
    vec2 uv = (vec2(pix) + vec2(0.5)) / res * 2.0 - 1.0;
    uv.x *= res.x / res.y;

    float t = tan(0.5 * pc.fovY);
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
bool occludedBySpheres(vec3 ro, vec3 rd, float maxDistance)
{
    if (pc.sphereCount == 0u)
    {
        return false;
    }

    for (uint i = 0u; i < pc.sphereCount; ++i)
    {
        float t; vec3 n;
        if (intersectSphere(ro, rd, sphereCR[i], t, n) && (t > 0.0) && (t < maxDistance))
        {
            return true;
        }
    }
    return false;
}

const vec3 kLightDir = normalize(vec3(0.4, 1.0, 0.2));
const vec3 kPlaneAlb = vec3(0.8, 0.8, 0.8);

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

    float t = hitT[pix];
    if (t < 0.0)
    {
        float v = clamp(0.5 * rd.y + 0.5, 0.0, 1.0);
        vec3 sky = mix(vec3(0.7, 0.8, 1.0), vec3(0.2, 0.3, 0.6), 1.0 - v);
        imageStore(uTarget, ivec2(id), vec4(sky, 1.0));
        return;
    }

    vec4 n_id = hitN[pix];
    vec3 n = normalize(n_id.xyz);
    int mid = int(n_id.w);
    vec3 pos = ro + t * rd;
    pos += n * kSurfaceBias;

    vec3 albedo = (mid == 0) ? kPlaneAlb : sphereAlb[mid - 1].rgb;

    bool shadowed = occludedBySpheres(pos, kLightDir, kHuge);
    float ndl = max(0.0, dot(n, kLightDir));
    float shadow = shadowed ? 0.0 : 1.0;

    vec3 col = albedo * (0.05 + 0.95 * ndl * shadow);
    imageStore(uTarget, ivec2(id), vec4(col, 1.0));
}
#endif
