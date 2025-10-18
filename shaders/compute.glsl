#version 450

#include "bindings.inc.glsl"

layout(constant_id = 0) const uint WG_X = 16u;
layout(constant_id = 1) const uint WG_Y = 16u;
layout(local_size_x_id = 0, local_size_y_id = 1, local_size_z = 1) in;

layout(binding = B_TARGET, rgba8) uniform writeonly image2D uTarget;

layout(std430, binding = B_SPHERE_CR)  buffer SphereCR  { vec4 sphereCR[];  };
layout(std430, binding = B_SPHERE_ALB) buffer SphereAlb { vec4 sphereAlb[]; };
layout(std430, binding = B_HIT_T)      buffer HitTBuf   { float hitT[];     };
layout(std430, binding = B_HIT_N)      buffer HitNBuf   { vec4  hitN[];     };

layout(std430, binding = B_GRID_RANGES)  buffer GridRanges  { uvec2 gridRanges[];  };
layout(std430, binding = B_GRID_INDICES) buffer GridIndices { uint  gridIndices[]; };
layout(std430, binding = B_GRID_COARSE_COUNTS) buffer GridCoarseCounts { uint gridCoarseCounts[]; };

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

    uvec3 gridDim; uint showGrid;
    vec3  gridMin; float _pad6;
    vec3  gridInvCell; float _pad7;
    uvec3 coarseDim; uint coarseFactor;
    vec3  coarseInvCell; float _pad8;
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

bool intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out float t0, out float t1)
{
    vec3 inv = 1.0 / rd;
    vec3 tlo = (bmin - ro) * inv;
    vec3 thi = (bmax - ro) * inv;
    vec3 tminv = min(tlo, thi);
    vec3 tmaxv = max(tlo, thi);
    t0 = max(max(tminv.x, tminv.y), tminv.z);
    t1 = min(min(tmaxv.x, tmaxv.y), tmaxv.z);
    return t1 >= max(t0, 0.0);
}

uvec3 clampCell(ivec3 c)
{
    ivec3 maxC = ivec3(pc.gridDim) - ivec3(1);
    ivec3 clamped = clamp(c, ivec3(0), maxC);
    return uvec3(clamped);
}

uint cellIndex(uvec3 c)
{
    return (c.z * pc.gridDim.y + c.y) * pc.gridDim.x + c.x;
}

uint coarseCellIndex(uvec3 c)
{
    return (c.z * pc.coarseDim.y + c.y) * pc.coarseDim.x + c.x;
}

bool traverseFineCells(vec3 ro, vec3 rd, float tStart, float tEnd, uvec3 coarseCell,
                       inout float bestT, inout vec3 bestN, inout float bestId, bool anyHit)
{
    vec3 cellSize = vec3(1.0) / pc.gridInvCell;
    float t = max(tStart, 0.0);
    vec3 p = ro + rd * t;
    ivec3 cell = ivec3(floor((p - pc.gridMin) * pc.gridInvCell));

    int fxMin = int(coarseCell.x) * int(pc.coarseFactor);
    int fyMin = int(coarseCell.y) * int(pc.coarseFactor);
    int fzMin = int(coarseCell.z) * int(pc.coarseFactor);

    int fxMax = min(fxMin + int(pc.coarseFactor) - 1, int(pc.gridDim.x) - 1);
    int fyMax = min(fyMin + int(pc.coarseFactor) - 1, int(pc.gridDim.y) - 1);
    int fzMax = min(fzMin + int(pc.coarseFactor) - 1, int(pc.gridDim.z) - 1);

    cell = clamp(cell, ivec3(fxMin, fyMin, fzMin), ivec3(fxMax, fyMax, fzMax));

    ivec3 step = ivec3(sign(rd));
    step = ivec3(step.x != 0 ? step.x : 1, step.y != 0 ? step.y : 1, step.z != 0 ? step.z : 1);
    vec3 stepPositive = vec3(step.x > 0 ? 1.0 : 0.0, step.y > 0 ? 1.0 : 0.0, step.z > 0 ? 1.0 : 0.0);

    vec3 nextBoundary = pc.gridMin + (vec3(cell) + stepPositive) * cellSize;
    vec3 tMax = vec3(
        (rd.x == 0.0) ? kHuge : (nextBoundary.x - ro.x) / rd.x,
        (rd.y == 0.0) ? kHuge : (nextBoundary.y - ro.y) / rd.y,
        (rd.z == 0.0) ? kHuge : (nextBoundary.z - ro.z) / rd.z);

    vec3 tDelta = vec3(
        (rd.x == 0.0) ? kHuge : cellSize.x / abs(rd.x),
        (rd.y == 0.0) ? kHuge : cellSize.y / abs(rd.y),
        (rd.z == 0.0) ? kHuge : cellSize.z / abs(rd.z));

    while (t <= tEnd)
    {
        uint idx = cellIndex(uvec3(cell));
        uvec2 range = gridRanges[idx];
        for (uint k = 0u; k < range.y; ++k)
        {
            uint si = gridIndices[range.x + k];
            float ts;
            vec3 ns;
            if (intersectSphere(ro, rd, sphereCR[si], ts, ns) && ts > 0.0 && ts < bestT)
            {
                bestT = ts;
                bestN = ns;
                bestId = float(si + 1u);
                if (anyHit && ts < tEnd)
                {
                    return true;
                }
            }
        }

        float nextT = min(tMax.x, min(tMax.y, tMax.z));
        if (bestT < nextT)
        {
            return true;
        }

        if (tMax.x <= tMax.y && tMax.x <= tMax.z)
        {
            t = tMax.x;
            tMax.x += tDelta.x;
            if (step.x > 0)
            {
                cell.x++;
                if (cell.x > fxMax)
                {
                    break;
                }
            }
            else
            {
                if (cell.x <= fxMin)
                {
                    break;
                }
                cell.x -= 1;
            }
        }
        else if (tMax.y <= tMax.z)
        {
            t = tMax.y;
            tMax.y += tDelta.y;
            if (step.y > 0)
            {
                cell.y++;
                if (cell.y > fyMax)
                {
                    break;
                }
            }
            else
            {
                if (cell.y <= fyMin)
                {
                    break;
                }
                cell.y -= 1;
            }
        }
        else
        {
            t = tMax.z;
            tMax.z += tDelta.z;
            if (step.z > 0)
            {
                cell.z++;
                if (cell.z > fzMax)
                {
                    break;
                }
            }
            else
            {
                if (cell.z <= fzMin)
                {
                    break;
                }
                cell.z -= 1;
            }
        }

        if (t > tEnd)
        {
            break;
        }
    }

    return (bestT < kHuge);
}

bool gridTraverseNearest(vec3 ro, vec3 rd, float tLimit, out float bestT, out vec3 bestN, out float bestId, bool anyHit)
{
    bestT = kHuge;
    bestN = vec3(0.0);
    bestId = -1.0;

    if (pc.gridDim.x == 0u || pc.gridDim.y == 0u || pc.gridDim.z == 0u ||
        pc.coarseDim.x == 0u || pc.coarseDim.y == 0u || pc.coarseDim.z == 0u ||
        pc.coarseFactor == 0u)
    {
        return false;
    }

    vec3 cellSize = vec3(1.0) / pc.coarseInvCell;
    vec3 gmin = pc.gridMin;
    vec3 gmax = pc.gridMin + vec3(pc.coarseDim) * cellSize;

    float t0;
    float t1;
    if (!intersectAABB(ro, rd, gmin, gmax, t0, t1))
    {
        return false;
    }

    t1 = min(t1, tLimit);
    float t = max(t0, 0.0);
    vec3 p = ro + rd * t;
    ivec3 startCell = ivec3(floor((p - gmin) * pc.coarseInvCell));
    startCell = clamp(startCell, ivec3(0), ivec3(pc.coarseDim) - ivec3(1));
    uvec3 cell = uvec3(startCell);

    ivec3 step = ivec3(sign(rd));
    step = ivec3(step.x != 0 ? step.x : 1, step.y != 0 ? step.y : 1, step.z != 0 ? step.z : 1);
    vec3 stepPositive = vec3(step.x > 0 ? 1.0 : 0.0, step.y > 0 ? 1.0 : 0.0, step.z > 0 ? 1.0 : 0.0);

    vec3 nextBoundary = gmin + (vec3(cell) + stepPositive) * cellSize;
    vec3 tMax = vec3(
        (rd.x == 0.0) ? kHuge : (nextBoundary.x - ro.x) / rd.x,
        (rd.y == 0.0) ? kHuge : (nextBoundary.y - ro.y) / rd.y,
        (rd.z == 0.0) ? kHuge : (nextBoundary.z - ro.z) / rd.z);

    vec3 tDelta = vec3(
        (rd.x == 0.0) ? kHuge : cellSize.x / abs(rd.x),
        (rd.y == 0.0) ? kHuge : cellSize.y / abs(rd.y),
        (rd.z == 0.0) ? kHuge : cellSize.z / abs(rd.z));

    while (t <= t1)
    {
        uint idx = coarseCellIndex(cell);
        if (gridCoarseCounts[idx] > 0u)
        {
            float cellExit = min(tMax.x, min(tMax.y, tMax.z));
            float segmentEnd = min(cellExit, t1);
            if (traverseFineCells(ro, rd, t, segmentEnd, cell, bestT, bestN, bestId, anyHit))
            {
                if (anyHit && bestT < segmentEnd)
                {
                    return true;
                }
            }
            if (bestT < segmentEnd)
            {
                return (bestT < kHuge);
            }
        }

        if (tMax.x <= tMax.y && tMax.x <= tMax.z)
        {
            t = tMax.x;
            tMax.x += tDelta.x;
            if (step.x > 0)
            {
                cell.x++;
                if (cell.x >= pc.coarseDim.x)
                {
                    break;
                }
            }
            else
            {
                if (cell.x == 0u)
                {
                    break;
                }
                cell.x -= 1u;
            }
        }
        else if (tMax.y <= tMax.z)
        {
            t = tMax.y;
            tMax.y += tDelta.y;
            if (step.y > 0)
            {
                cell.y++;
                if (cell.y >= pc.coarseDim.y)
                {
                    break;
                }
            }
            else
            {
                if (cell.y == 0u)
                {
                    break;
                }
                cell.y -= 1u;
            }
        }
        else
        {
            t = tMax.z;
            tMax.z += tDelta.z;
            if (step.z > 0)
            {
                cell.z++;
                if (cell.z >= pc.coarseDim.z)
                {
                    break;
                }
            }
            else
            {
                if (cell.z == 0u)
                {
                    break;
                }
                cell.z -= 1u;
            }
        }
    }

    return (bestT < kHuge);
}

#if defined(KERNEL_PRIMARY_INTERSECT) || defined(KERNEL_SHADE_SHADOW)
bool findClosestSphere(vec3 ro, vec3 rd, float tLimit,
                       inout float bestT, inout vec3 bestN, inout float bestId)
{
    if (pc.gridDim.x > 0u && pc.coarseDim.x > 0u)
    {
        float t;
        vec3 n;
        float id;
        if (gridTraverseNearest(ro, rd, tLimit, t, n, id, false))
        {
            bestT = t;
            bestN = n;
            bestId = id;
            return true;
        }
        return false;
    }

    bool hit = false;
    for (uint i = 0u; i < pc.sphereCount; ++i)
    {
        float t;
        vec3 n;
        if (intersectSphere(ro, rd, sphereCR[i], t, n) && t > 0.0 && t < tLimit && t < bestT)
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

    vec3 viewOrigin = pc.camPos;
    vec3 ro = viewOrigin;
    vec3 rd = computeRayDir(id);

    float bestT = kHuge;
    vec3 bestN = vec3(0.0);
    float bestId = -1.0;

    float tPlane;
    vec3 nPlane;
    bool planeHit = intersectPlaneY(ro, rd, pc.groundY, tPlane, nPlane);

    float tLimit = planeHit ? tPlane : kHuge;
    findClosestSphere(ro, rd, tLimit, bestT, bestN, bestId);

    if (planeHit && (tPlane < bestT))
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

    float tPlane;
    vec3 nPlane;
    bool planeHit = intersectPlaneY(ro, rd, pc.groundY, tPlane, nPlane);

    float tLimit = planeHit ? tPlane : kHuge;
    bool sphereHit = findClosestSphere(ro, rd, tLimit, bestT, bestN, bestId);

    if (sphereHit)
    {
        matId = int(bestId);
    }

    if (planeHit && (tPlane < bestT))
    {
        bestT = tPlane;
        bestN = nPlane;
        matId = 0;
    }

    return sphereHit || planeHit;
}

bool occludedWorld(vec3 ro, vec3 rd, float maxT)
{
    float tPlane;
    vec3 nPlane;
    if (intersectPlaneY(ro, rd, pc.groundY, tPlane, nPlane) && tPlane > 0.0 && tPlane < maxT)
    {
        return true;
    }

    if (pc.gridDim.x > 0u && pc.coarseDim.x > 0u)
    {
        float t;
        vec3 n;
        float id;
        return gridTraverseNearest(ro, rd, maxT, t, n, id, true);
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

    vec3 viewOrigin = pc.camPos;
    vec3 ro = viewOrigin;
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

    if (pc.showGrid != 0u && pc.gridDim.x > 0u)
    {
        float denom = rd0.y;
        if (abs(denom) > 1e-6)
        {
            float tg = (pc.groundY - viewOrigin.y) / denom;
            if (tg > 0.0)
            {
                vec3 p = viewOrigin + rd0 * tg;
                vec2 fineCoord = (p.xz - pc.gridMin.xz) * pc.gridInvCell.xz;
                vec2 fineFrac = fract(fineCoord);
                float fineEdgeX = min(fineFrac.x, 1.0 - fineFrac.x);
                float fineEdgeZ = min(fineFrac.y, 1.0 - fineFrac.y);
                float fineMask = ((fineEdgeX < 0.01) || (fineEdgeZ < 0.01)) ? 1.0 : 0.0;

                float coarseMask = 0.0;
                if (pc.coarseDim.x > 0u && pc.coarseDim.z > 0u)
                {
                    vec2 coarseCoord = (p.xz - pc.gridMin.xz) * pc.coarseInvCell.xz;
                    vec2 coarseFrac = fract(coarseCoord);
                    float coarseEdgeX = min(coarseFrac.x, 1.0 - coarseFrac.x);
                    float coarseEdgeZ = min(coarseFrac.y, 1.0 - coarseFrac.y);
                    if ((coarseEdgeX < 0.01) || (coarseEdgeZ < 0.01))
                    {
                        coarseMask = 1.0;
                    }
                }

                if (coarseMask > 0.0)
                {
                    mapped = mix(mapped, vec3(1.0, 0.1, 0.1), 0.8);
                }
                else if (fineMask > 0.0)
                {
                    mapped = mix(mapped, vec3(0.0, 1.0, 0.0), 0.65);
                }
            }
        }
    }

    imageStore(uTarget, ivec2(id), vec4(mapped, 1.0));
}
#endif
