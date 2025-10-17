#version 450

#include "bindings.inc.glsl"

#if defined(KERNEL_SPHERES_INIT) || defined(KERNEL_GRID_COUNT) || defined(KERNEL_GRID_SCATTER)
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#elif defined(KERNEL_GRID_CLASSIFY)
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
#endif

layout(binding = B_TARGET, rgba8) uniform writeonly image2D uTarget;

layout(std430, binding = B_SPHERE_CR)  buffer SphereCR  { vec4 sphereCR[];  };
layout(std430, binding = B_SPHERE_ALB) buffer SphereAlb { vec4 sphereAlb[]; };
layout(std430, binding = B_HIT_T) buffer HitTBuf { float hitT[]; };
layout(std430, binding = B_HIT_N) buffer HitNBuf { vec4 hitN[]; };

layout(std430, binding = B_GRID_L0_META) buffer GridLevel0Meta { uvec4 gridL0Meta[]; };
layout(std430, binding = B_GRID_L0_COUNTER) buffer GridLevel0Counter { uint gridL0Counter[]; };
layout(std430, binding = B_GRID_L0_INDICES) buffer GridLevel0Indices { uint gridL0Indices[]; };
layout(std430, binding = B_GRID_L1_META) buffer GridLevel1Meta { uvec4 gridL1Meta[]; };
layout(std430, binding = B_GRID_L1_COUNTER) buffer GridLevel1Counter { uint gridL1Counter[]; };
layout(std430, binding = B_GRID_L1_INDICES) buffer GridLevel1Indices { uint gridL1Indices[]; };
layout(std430, binding = B_GRID_STATE) buffer GridStateBuf { uint gridStateBuf[]; };

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
    float sphereRadius;
    float groundY;
    uint rngSeed;
    uint flags;
    uint gridDimX;
    uint gridDimZ;
    uint gridFineDim;
    uint gridRefineThreshold;
} pc;

const float kHuge = 1e30;
const float kSurfaceBias = 1e-3;
const uint CELL_EMPTY = 0u;
const uint CELL_LEAF = 1u;
const uint CELL_SUBGRID = 2u;

uint wang_hash(uint s)
{
    s = (s ^ 61u) ^ (s >> 16);
    s *= 9u;
    s = s ^ (s >> 4);
    s *= 0x27d4eb2du;
    s = s ^ (s >> 15);
    return s;
}

float rand01(inout uint state)
{
    state = wang_hash(state);
    return float(state) * (1.0 / 4294967296.0);
}

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

vec2 gridExtent()
{
    vec2 e = pc.worldMax - pc.worldMin;
    return max(e, vec2(1e-3));
}

vec2 gridCellSize()
{
    vec2 ext = gridExtent();
    vec2 dims = vec2(max(float(pc.gridDimX), 1.0), max(float(pc.gridDimZ), 1.0));
    return ext / dims;
}

uint gridFineDimension()
{
    return max(pc.gridFineDim, 1u);
}

uint gridFineCountPerCoarse()
{
    uint f = gridFineDimension();
    return f * f;
}

uvec2 gridCoarseCoord(vec2 position)
{
    vec2 ext = gridExtent();
    vec2 dims = vec2(max(float(pc.gridDimX), 1.0), max(float(pc.gridDimZ), 1.0));
    vec2 coord = (position - pc.worldMin) * dims / ext;
    vec2 maxCoord = vec2(float(pc.gridDimX) - 1.0, float(pc.gridDimZ) - 1.0);
    return uvec2(clamp(floor(coord), vec2(0.0), maxCoord));
}

uint gridCoarseIndex(uvec2 coord)
{
    return coord.y * pc.gridDimX + coord.x;
}

uint gridFineBase(uint coarseIndex)
{
    return coarseIndex * gridFineCountPerCoarse();
}

uvec2 gridFineCoord(uvec2 coarseCoord, vec2 position)
{
    vec2 coarseSize = gridCellSize();
    vec2 coarseMin = pc.worldMin + vec2(float(coarseCoord.x), float(coarseCoord.y)) * coarseSize;
    float fdim = float(gridFineDimension());
    vec2 fine = (position - coarseMin) * vec2(fdim) / coarseSize;
    float fineMax = fdim - 1.0;
    return uvec2(clamp(floor(fine), vec2(0.0), vec2(fineMax)));
}

uint gridFineIndex(uint coarseIndex, uvec2 fineCoord)
{
    uint fdim = gridFineDimension();
    return gridFineBase(coarseIndex) + fineCoord.y * fdim + fineCoord.x;
}

bool intersectCoarseLeaf(uvec4 meta, vec3 ro, vec3 rd, inout float bestT, inout vec3 bestN, inout float bestId, bool anyHit)
{
    uint first = meta.x;
    uint count = meta.y;
    bool hit = false;
    for (uint i = 0u; i < count; ++i)
    {
        uint sphereIndex = gridL0Indices[first + i];
        float t; vec3 n;
        if (intersectSphere(ro, rd, sphereCR[sphereIndex], t, n) && (t < bestT))
        {
            bestT = t;
            bestN = n;
            bestId = float(sphereIndex + 1u);
            hit = true;
            if (anyHit)
            {
                return true;
            }
        }
    }
    return hit;
}

bool intersectFineLeaf(uint fineIndex, vec3 ro, vec3 rd, inout float bestT, inout vec3 bestN, inout float bestId, bool anyHit)
{
    uvec4 meta = gridL1Meta[fineIndex];
    uint count = meta.y;
    if (count == 0u)
    {
        return false;
    }

    uint first = meta.x;
    bool hit = false;
    for (uint i = 0u; i < count; ++i)
    {
        uint sphereIndex = gridL1Indices[first + i];
        float t; vec3 n;
        if (intersectSphere(ro, rd, sphereCR[sphereIndex], t, n) && (t < bestT))
        {
            bestT = t;
            bestN = n;
            bestId = float(sphereIndex + 1u);
            hit = true;
            if (anyHit)
            {
                return true;
            }
        }
    }
    return hit;
}

bool intersectGridBounds(vec3 ro, vec3 rd, out float tEnter, out float tExit)
{
    tEnter = 0.0;
    tExit = kHuge;

    vec2 minB = pc.worldMin;
    vec2 maxB = pc.worldMax;

    if (abs(rd.x) < 1e-6)
    {
        if ((ro.x < minB.x) || (ro.x > maxB.x))
        {
            return false;
        }
    }
    else
    {
        float inv = 1.0 / rd.x;
        float t0 = (minB.x - ro.x) * inv;
        float t1 = (maxB.x - ro.x) * inv;
        tEnter = max(tEnter, min(t0, t1));
        tExit = min(tExit, max(t0, t1));
    }

    if (abs(rd.z) < 1e-6)
    {
        if ((ro.z < minB.y) || (ro.z > maxB.y))
        {
            return false;
        }
    }
    else
    {
        float inv = 1.0 / rd.z;
        float t0 = (minB.y - ro.z) * inv;
        float t1 = (maxB.y - ro.z) * inv;
        tEnter = max(tEnter, min(t0, t1));
        tExit = min(tExit, max(t0, t1));
    }

    return (tExit >= tEnter);
}

bool traverseFine(ivec2 coarseCoord, uint fineBase, vec3 ro, vec3 rd, float tStart, float tEnd, inout float bestT, inout vec3 bestN, inout float bestId, bool anyHit)
{
    uint fdim = gridFineDimension();
    if (fdim == 0u)
    {
        return false;
    }

    vec2 coarseSize = gridCellSize();
    vec2 fineSize = coarseSize / vec2(float(fdim));
    vec2 coarseMin = pc.worldMin + vec2(float(coarseCoord.x), float(coarseCoord.y)) * coarseSize;

    vec3 startPos = ro + rd * tStart;
    vec2 local = (startPos.xz - coarseMin) / fineSize;
    ivec2 cell = ivec2(clamp(floor(local), vec2(0.0), vec2(float(fdim) - 1.0)));

    ivec2 step;
    vec2 next;
    vec2 delta;

    if (abs(rd.x) < 1e-6)
    {
        step.x = 0;
        next.x = kHuge;
        delta.x = kHuge;
    }
    else if (rd.x > 0.0)
    {
        step.x = 1;
        float boundary = coarseMin.x + (float(cell.x) + 1.0) * fineSize.x;
        next.x = (boundary - ro.x) / rd.x;
        delta.x = fineSize.x / rd.x;
    }
    else
    {
        step.x = -1;
        float boundary = coarseMin.x + float(cell.x) * fineSize.x;
        next.x = (boundary - ro.x) / rd.x;
        delta.x = -fineSize.x / rd.x;
    }

    if (abs(rd.z) < 1e-6)
    {
        step.y = 0;
        next.y = kHuge;
        delta.y = kHuge;
    }
    else if (rd.z > 0.0)
    {
        step.y = 1;
        float boundary = coarseMin.y + (float(cell.y) + 1.0) * fineSize.y;
        next.y = (boundary - ro.z) / rd.z;
        delta.y = fineSize.y / rd.z;
    }
    else
    {
        step.y = -1;
        float boundary = coarseMin.y + float(cell.y) * fineSize.y;
        next.y = (boundary - ro.z) / rd.z;
        delta.y = -fineSize.y / rd.z;
    }

    uint maxSteps = fdim + fdim + 2u;
    bool hit = false;
    float limit = min(tEnd, anyHit ? tEnd : bestT);

    for (uint iter = 0u; iter < maxSteps; ++iter)
    {
        if ((cell.x < 0) || (cell.x >= int(fdim)) || (cell.y < 0) || (cell.y >= int(fdim)))
        {
            break;
        }

        uint fineIndex = fineBase + uint(cell.y) * fdim + uint(cell.x);
        if (intersectFineLeaf(fineIndex, ro, rd, bestT, bestN, bestId, anyHit))
        {
            hit = true;
            if (anyHit && (bestT < tEnd))
            {
                return true;
            }
            limit = min(tEnd, anyHit ? tEnd : bestT);
        }

        float tNext = min(next.x, next.y);
        if ((tNext >= limit) || (tNext >= tEnd))
        {
            break;
        }

        if ((step.x == 0) && (step.y == 0))
        {
            break;
        }

        if (next.x < next.y)
        {
            cell.x += step.x;
            next.x += delta.x;
        }
        else
        {
            cell.y += step.y;
            next.y += delta.y;
        }
    }

    return hit;
}

bool traverseGrid(vec3 ro, vec3 rd, float maxDistance, inout float bestT, inout vec3 bestN, inout float bestId, bool anyHit)
{
    if ((pc.gridDimX == 0u) || (pc.gridDimZ == 0u))
    {
        return false;
    }

    float tEnter;
    float tExit;
    if (!intersectGridBounds(ro, rd, tEnter, tExit))
    {
        return false;
    }

    if (tExit <= 0.0)
    {
        return false;
    }

    float tStart = max(tEnter, 0.0);
    float tEnd = min(tExit, maxDistance);
    if (tStart > tEnd)
    {
        return false;
    }

    vec2 cellSize = gridCellSize();
    vec2 invCell = vec2(1.0) / cellSize;

    vec3 startPos = ro + rd * tStart;
    vec2 rel = (startPos.xz - pc.worldMin) * invCell;
    ivec2 cell = ivec2(clamp(floor(rel), vec2(0.0), vec2(float(pc.gridDimX) - 1.0, float(pc.gridDimZ) - 1.0)));

    ivec2 step;
    vec2 next;
    vec2 delta;

    if (abs(rd.x) < 1e-6)
    {
        step.x = 0;
        next.x = kHuge;
        delta.x = kHuge;
    }
    else if (rd.x > 0.0)
    {
        step.x = 1;
        float boundary = pc.worldMin.x + (float(cell.x) + 1.0) * cellSize.x;
        next.x = (boundary - ro.x) / rd.x;
        delta.x = cellSize.x / rd.x;
    }
    else
    {
        step.x = -1;
        float boundary = pc.worldMin.x + float(cell.x) * cellSize.x;
        next.x = (boundary - ro.x) / rd.x;
        delta.x = -cellSize.x / rd.x;
    }

    if (abs(rd.z) < 1e-6)
    {
        step.y = 0;
        next.y = kHuge;
        delta.y = kHuge;
    }
    else if (rd.z > 0.0)
    {
        step.y = 1;
        float boundary = pc.worldMin.y + (float(cell.y) + 1.0) * cellSize.y;
        next.y = (boundary - ro.z) / rd.z;
        delta.y = cellSize.y / rd.z;
    }
    else
    {
        step.y = -1;
        float boundary = pc.worldMin.y + float(cell.y) * cellSize.y;
        next.y = (boundary - ro.z) / rd.z;
        delta.y = -cellSize.y / rd.z;
    }

    uint maxSteps = pc.gridDimX + pc.gridDimZ + 4u;
    bool hit = false;
    float limit = min(tEnd, anyHit ? tEnd : bestT);
    float currentT = tStart;

    for (uint iter = 0u; iter < maxSteps; ++iter)
    {
        if ((cell.x < 0) || (cell.x >= int(pc.gridDimX)) || (cell.y < 0) || (cell.y >= int(pc.gridDimZ)))
        {
            break;
        }

        uint coarseIndex = uint(cell.y) * pc.gridDimX + uint(cell.x);
        uvec4 meta = gridL0Meta[coarseIndex];
        uint cellType = meta.w;

        if (cellType == CELL_LEAF)
        {
            if (intersectCoarseLeaf(meta, ro, rd, bestT, bestN, bestId, anyHit))
            {
                hit = true;
                if (anyHit && (bestT < maxDistance))
                {
                    return true;
                }
                limit = min(tEnd, anyHit ? tEnd : bestT);
            }
        }
        else if (cellType == CELL_SUBGRID)
        {
            uint fallbackCount = gridL0Counter[coarseIndex];
            if (fallbackCount > 0u)
            {
                uvec4 fallbackMeta = uvec4(meta.x, fallbackCount, 0u, CELL_LEAF);
                if (intersectCoarseLeaf(fallbackMeta, ro, rd, bestT, bestN, bestId, anyHit))
                {
                    hit = true;
                    if (anyHit && (bestT < maxDistance))
                    {
                        return true;
                    }
                    limit = min(tEnd, anyHit ? tEnd : bestT);
                }
            }

            float tCellExit = min(min(next.x, next.y), limit);
            if (traverseFine(cell, meta.z, ro, rd, currentT, tCellExit, bestT, bestN, bestId, anyHit))
            {
                hit = true;
                if (anyHit && (bestT < maxDistance))
                {
                    return true;
                }
                limit = min(tEnd, anyHit ? tEnd : bestT);
            }
        }

        float tNext = min(next.x, next.y);
        if ((tNext >= limit) || (tNext >= tEnd))
        {
            break;
        }

        if ((step.x == 0) && (step.y == 0))
        {
            break;
        }

        if (next.x < next.y)
        {
            cell.x += step.x;
            next.x += delta.x;
        }
        else
        {
            cell.y += step.y;
            next.y += delta.y;
        }

        currentT = tNext;
    }

    return hit;
}

bool traceGridAny(vec3 ro, vec3 rd, float maxDistance)
{
    float t = maxDistance;
    vec3 n = vec3(0.0);
    float id = -1.0;
    return traverseGrid(ro, rd, maxDistance, t, n, id, true);
}

#ifdef KERNEL_SPHERES_INIT
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.sphereCount)
    {
        return;
    }

    uint cellsX = uint(ceil(sqrt(float(pc.sphereCount))));
    uint cellsZ = (pc.sphereCount + cellsX - 1u) / cellsX;

    float wx = pc.worldMax.x - pc.worldMin.x;
    float wz = pc.worldMax.y - pc.worldMin.y;
    float cellX = wx / float(cellsX);
    float cellZ = wz / float(cellsZ);

    float jx = max(0.0, 0.5 * cellX - pc.sphereRadius);
    float jz = max(0.0, 0.5 * cellZ - pc.sphereRadius);

    uint ix = i % cellsX;
    uint iz = i / cellsX;

    float cx = pc.worldMin.x + (float(ix) + 0.5) * cellX;
    float cz = pc.worldMin.y + (float(iz) + 0.5) * cellZ;

    uint rng = pc.rngSeed ^ i;
    float dx = (rand01(rng) * 2.0 - 1.0) * jx * 0.8;
    float dz = (rand01(rng) * 2.0 - 1.0) * jz * 0.8;

    float y = pc.groundY + pc.sphereRadius;
    sphereCR[i] = vec4(cx + dx, y, cz + dz, pc.sphereRadius);

    vec3 h = vec3(rand01(rng), rand01(rng) * 0.25 + 0.65, rand01(rng) * 0.4 + 0.4);
    sphereAlb[i] = vec4(h, 1.0);
}
#endif

#ifdef KERNEL_GRID_COUNT
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= pc.sphereCount)
    {
        return;
    }

    vec4 sphere = sphereCR[index];
    vec2 extent = gridExtent();
    vec2 dims = vec2(max(float(pc.gridDimX), 1.0), max(float(pc.gridDimZ), 1.0));

    vec2 coarseCoordF = (sphere.xz - pc.worldMin) * dims / extent;
    vec2 coarseMax = vec2(float(pc.gridDimX) - 1.0, float(pc.gridDimZ) - 1.0);
    uvec2 coarseCoord = uvec2(clamp(floor(coarseCoordF), vec2(0.0), coarseMax));
    uint coarseIndex = gridCoarseIndex(coarseCoord);
    atomicAdd(gridL0Meta[coarseIndex].y, 1u);

    uint fdim = gridFineDimension();
    vec2 coarseSize = gridCellSize();
    vec2 coarseMin = pc.worldMin + vec2(float(coarseCoord.x), float(coarseCoord.y)) * coarseSize;
    vec2 fineCoordF = (sphere.xz - coarseMin) * vec2(float(fdim)) / coarseSize;
    vec2 fineMax = vec2(float(fdim) - 1.0);
    uvec2 fineCoord = uvec2(clamp(floor(fineCoordF), vec2(0.0), fineMax));
    uint fineIndex = gridFineIndex(coarseIndex, fineCoord);
    atomicAdd(gridL1Meta[fineIndex].y, 1u);
}
#endif

#ifdef KERNEL_GRID_CLASSIFY
void main()
{
    uint cellIndex = gl_GlobalInvocationID.x;
    uint coarseCount = pc.gridDimX * pc.gridDimZ;
    if (cellIndex >= coarseCount)
    {
        return;
    }

    uvec4 meta = gridL0Meta[cellIndex];
    uint count = meta.y;
    if (count == 0u)
    {
        gridL0Meta[cellIndex] = uvec4(0u);
        return;
    }

    if (count <= pc.gridRefineThreshold)
    {
        uint start = atomicAdd(gridStateBuf[0], count);
        gridL0Meta[cellIndex] = uvec4(start, count, 0u, CELL_LEAF);
    }
    else
    {
        uint fineBase = gridFineBase(cellIndex);
        uint fineCells = gridFineCountPerCoarse();
        uint fallbackStart = atomicAdd(gridStateBuf[0], count);
        gridL0Meta[cellIndex] = uvec4(fallbackStart, count, fineBase, CELL_SUBGRID);

        for (uint s = 0u; s < fineCells; ++s)
        {
            uint fineIndex = fineBase + s;
            uint subCount = gridL1Meta[fineIndex].y;
            if (subCount > 0u)
            {
                uint start = atomicAdd(gridStateBuf[1], subCount);
                gridL1Meta[fineIndex].x = start;
                gridL1Meta[fineIndex].w = CELL_LEAF;
            }
            else
            {
                gridL1Meta[fineIndex].x = 0u;
                gridL1Meta[fineIndex].w = CELL_EMPTY;
            }
        }
    }
}
#endif

#ifdef KERNEL_GRID_SCATTER
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= pc.sphereCount)
    {
        return;
    }

    vec4 sphere = sphereCR[index];
    vec2 extent = gridExtent();
    vec2 dims = vec2(max(float(pc.gridDimX), 1.0), max(float(pc.gridDimZ), 1.0));

    vec2 coarseCoordF = (sphere.xz - pc.worldMin) * dims / extent;
    vec2 coarseMax = vec2(float(pc.gridDimX) - 1.0, float(pc.gridDimZ) - 1.0);
    uvec2 coarseCoord = uvec2(clamp(floor(coarseCoordF), vec2(0.0), coarseMax));
    uint coarseIndex = gridCoarseIndex(coarseCoord);
    uvec4 meta = gridL0Meta[coarseIndex];

    if (meta.w == CELL_SUBGRID)
    {
        uint fdim = gridFineDimension();
        vec2 coarseSize = gridCellSize();
        vec2 coarseMin = pc.worldMin + vec2(float(coarseCoord.x), float(coarseCoord.y)) * coarseSize;
        vec2 fineCoordF = (sphere.xz - coarseMin) * vec2(float(fdim)) / coarseSize;
        vec2 fineMax = vec2(float(fdim) - 1.0);
        uvec2 fineCoord = uvec2(clamp(floor(fineCoordF), vec2(0.0), fineMax));
        uint fineIndex = meta.z + fineCoord.y * fdim + fineCoord.x;
        uvec4 fineMeta = gridL1Meta[fineIndex];
        if (fineMeta.y > 0u)
        {
            uint offset = atomicAdd(gridL1Counter[fineIndex], 1u);
            gridL1Indices[fineMeta.x + offset] = index;
        }
        else
        {
            uint offset = atomicAdd(gridL0Counter[coarseIndex], 1u);
            gridL0Indices[meta.x + offset] = index;
        }
    }
    else
    {
        uint offset = atomicAdd(gridL0Counter[coarseIndex], 1u);
        gridL0Indices[meta.x + offset] = index;
    }
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

    traverseGrid(ro, rd, kHuge, bestT, bestN, bestId, false);

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
bool occludedBySpheres(vec3 p, vec3 ldir)
{
    return traceGridAny(p, ldir, kHuge);
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

    bool ocl = occludedBySpheres(pos, kLightDir);
    float ndl = max(0.0, dot(n, kLightDir));
    float shadow = ocl ? 0.0 : 1.0;

    vec3 col = albedo * (0.05 + 0.95 * ndl * shadow);
    imageStore(uTarget, ivec2(id), vec4(col, 1.0));
}
#endif
