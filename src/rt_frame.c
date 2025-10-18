#include "runtime.h"
#include "rt_frame.h"
#include "vk_descriptors.h"
#include "vk_mem_alloc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void CreateOrResizeBuffer(VkDeviceSize size, VkBuffer *buf, VmaAllocation *alloc)
{
    Assert(buf != NULL, "Grid buffer handle pointer is null");
    Assert(alloc != NULL, "Grid buffer allocation pointer is null");
    Assert(GLOBAL.Vulkan.vma != NULL, "VMA allocator is not ready");
    Assert(size > 0, "Grid buffer size must be greater than zero");

    if (*buf != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(GLOBAL.Vulkan.vma, *buf, *alloc);
        *buf = VK_NULL_HANDLE;
        *alloc = NULL;
    }

    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkResult result = vmaCreateBuffer(GLOBAL.Vulkan.vma, &info, &allocInfo, buf, alloc, NULL);
    Assert(result == VK_SUCCESS, "Failed to create uniform grid buffer");
}

static void BuildUniformGrid(VkCommandBuffer cmd, uint32_t sphereCount)
{
    Assert(cmd != VK_NULL_HANDLE, "Command buffer is not ready");

    if (sphereCount == 0u)
    {
        GLOBAL.Vulkan.gridDimX = 1u;
        GLOBAL.Vulkan.gridDimY = 1u;
        GLOBAL.Vulkan.gridDimZ = 1u;
        GLOBAL.Vulkan.gridMinX = GLOBAL.Vulkan.worldMinX;
        GLOBAL.Vulkan.gridMinY = GLOBAL.Vulkan.groundY;
        GLOBAL.Vulkan.gridMinZ = GLOBAL.Vulkan.worldMinZ;
        GLOBAL.Vulkan.gridInvCellX = 1.0f;
        GLOBAL.Vulkan.gridInvCellY = 1.0f;
        GLOBAL.Vulkan.gridInvCellZ = 1.0f;
        GLOBAL.Vulkan.coarseDimX = 1u;
        GLOBAL.Vulkan.coarseDimY = 1u;
        GLOBAL.Vulkan.coarseDimZ = 1u;
        GLOBAL.Vulkan.coarseInvCellX = 1.0f;
        GLOBAL.Vulkan.coarseInvCellY = 1.0f;
        GLOBAL.Vulkan.coarseInvCellZ = 1.0f;
        GLOBAL.Vulkan.coarseFactor = 1u;
        return;
    }

    const float cell = fmaxf(GLOBAL.Vulkan.sphereMaxRadius * 2.5f, 0.05f);
    const uint32_t coarseFactor = 4u;
    const float3 minW = { GLOBAL.Vulkan.worldMinX, GLOBAL.Vulkan.groundY, GLOBAL.Vulkan.worldMinZ };

    const float spanX = GLOBAL.Vulkan.worldMaxX - GLOBAL.Vulkan.worldMinX;
    const float spanZ = GLOBAL.Vulkan.worldMaxZ - GLOBAL.Vulkan.worldMinZ;

    const uint32_t dimX = (uint32_t)fmaxf(1.0f, ceilf(spanX / cell));
    const uint32_t dimY = 1u;
    const uint32_t dimZ = (uint32_t)fmaxf(1.0f, ceilf(spanZ / cell));
    const uint32_t cellCount = dimX * dimY * dimZ;

    const uint32_t coarseDimX = (dimX + coarseFactor - 1u) / coarseFactor;
    const uint32_t coarseDimY = (dimY + coarseFactor - 1u) / coarseFactor;
    const uint32_t coarseDimZ = (dimZ + coarseFactor - 1u) / coarseFactor;
    const uint32_t coarseCount = coarseDimX * coarseDimY * coarseDimZ;

    const float invCell = 1.0f / cell;
    GLOBAL.Vulkan.gridDimX = dimX;
    GLOBAL.Vulkan.gridDimY = dimY;
    GLOBAL.Vulkan.gridDimZ = dimZ;
    GLOBAL.Vulkan.gridMinX = minW.x;
    GLOBAL.Vulkan.gridMinY = minW.y;
    GLOBAL.Vulkan.gridMinZ = minW.z;
    GLOBAL.Vulkan.gridInvCellX = invCell;
    GLOBAL.Vulkan.gridInvCellY = invCell;
    GLOBAL.Vulkan.gridInvCellZ = invCell;
    const float coarseCell = cell * (float)coarseFactor;
    const float invCoarseCell = 1.0f / coarseCell;
    GLOBAL.Vulkan.coarseDimX = coarseDimX;
    GLOBAL.Vulkan.coarseDimY = coarseDimY;
    GLOBAL.Vulkan.coarseDimZ = coarseDimZ;
    GLOBAL.Vulkan.coarseInvCellX = invCoarseCell;
    GLOBAL.Vulkan.coarseInvCellY = (dimY > 0u) ? invCoarseCell : 0.0f;
    GLOBAL.Vulkan.coarseInvCellZ = invCoarseCell;
    GLOBAL.Vulkan.coarseFactor = coarseFactor;

    uint32_t *counts = (uint32_t *)calloc(cellCount, sizeof(uint32_t));
    Assert(counts != NULL, "Failed to allocate grid cell counts");
    uint32_t *coarseCounts = (uint32_t *)calloc(coarseCount, sizeof(uint32_t));
    Assert(coarseCounts != NULL, "Failed to allocate coarse grid counts");

    for (uint32_t i = 0; i < sphereCount; ++i)
    {
        const float cx = GLOBAL.Vulkan.sphereCRHost[i * 4u + 0u];
        const float cy = GLOBAL.Vulkan.sphereCRHost[i * 4u + 1u];
        const float cz = GLOBAL.Vulkan.sphereCRHost[i * 4u + 2u];
        const float r = GLOBAL.Vulkan.sphereCRHost[i * 4u + 3u];
        const float minx = cx - r;
        const float maxx = cx + r;
        const float miny = fmaxf(minW.y, cy - r);
        const float maxy = cy + r;
        const float minz = cz - r;
        const float maxz = cz + r;

        int ix0 = (int)floorf((minx - minW.x) * invCell);
        int ix1 = (int)floorf((maxx - minW.x) * invCell);
        int iy0 = (int)floorf((miny - minW.y) * invCell);
        int iy1 = (int)floorf((maxy - minW.y) * invCell);
        int iz0 = (int)floorf((minz - minW.z) * invCell);
        int iz1 = (int)floorf((maxz - minW.z) * invCell);

        if (ix0 < 0) { ix0 = 0; }
        if (iy0 < 0) { iy0 = 0; }
        if (iz0 < 0) { iz0 = 0; }
        if (ix1 >= (int)dimX) { ix1 = (int)dimX - 1; }
        if (iy1 >= (int)dimY) { iy1 = (int)dimY - 1; }
        if (iz1 >= (int)dimZ) { iz1 = (int)dimZ - 1; }

        for (int z = iz0; z <= iz1; ++z)
        {
            for (int y = iy0; y <= iy1; ++y)
            {
                for (int x = ix0; x <= ix1; ++x)
                {
                    const uint32_t idx = (uint32_t)((z * (int)dimY + y) * (int)dimX + x);
                    counts[idx]++;
                }
            }
        }
    }

    uint32_t *starts = (uint32_t *)malloc(sizeof(uint32_t) * cellCount);
    Assert(starts != NULL, "Failed to allocate grid cell starts");

    uint32_t total = 0u;
    for (uint32_t c = 0u; c < cellCount; ++c)
    {
        starts[c] = total;
        total += counts[c];
    }

    uint32_t indicesCapacity = (total > 0u) ? total : 1u;
    uint32_t *indices = (uint32_t *)malloc(sizeof(uint32_t) * indicesCapacity);
    Assert(indices != NULL, "Failed to allocate grid indices");

    uint32_t *cursor = (uint32_t *)malloc(sizeof(uint32_t) * cellCount);
    Assert(cursor != NULL, "Failed to allocate grid index cursors");

    memcpy(cursor, starts, sizeof(uint32_t) * cellCount);

    for (uint32_t z = 0u; z < dimZ; ++z)
    {
        for (uint32_t y = 0u; y < dimY; ++y)
        {
            for (uint32_t x = 0u; x < dimX; ++x)
            {
                const uint32_t cellIndex = (z * dimY + y) * dimX + x;
                const uint32_t cellCountValue = counts[cellIndex];
                if (cellCountValue == 0u)
                {
                    continue;
                }
                const uint32_t coarseX = x / coarseFactor;
                const uint32_t coarseY = y / coarseFactor;
                const uint32_t coarseZ = z / coarseFactor;
                const uint32_t coarseIndex = (coarseZ * coarseDimY + coarseY) * coarseDimX + coarseX;
                coarseCounts[coarseIndex] += cellCountValue;
            }
        }
    }

    for (uint32_t i = 0; i < sphereCount; ++i)
    {
        const float cx = GLOBAL.Vulkan.sphereCRHost[i * 4u + 0u];
        const float cy = GLOBAL.Vulkan.sphereCRHost[i * 4u + 1u];
        const float cz = GLOBAL.Vulkan.sphereCRHost[i * 4u + 2u];
        const float r = GLOBAL.Vulkan.sphereCRHost[i * 4u + 3u];
        const float minx = cx - r;
        const float maxx = cx + r;
        const float miny = fmaxf(minW.y, cy - r);
        const float maxy = cy + r;
        const float minz = cz - r;
        const float maxz = cz + r;

        int ix0 = (int)floorf((minx - minW.x) * invCell);
        int ix1 = (int)floorf((maxx - minW.x) * invCell);
        int iy0 = (int)floorf((miny - minW.y) * invCell);
        int iy1 = (int)floorf((maxy - minW.y) * invCell);
        int iz0 = (int)floorf((minz - minW.z) * invCell);
        int iz1 = (int)floorf((maxz - minW.z) * invCell);

        if (ix0 < 0) { ix0 = 0; }
        if (iy0 < 0) { iy0 = 0; }
        if (iz0 < 0) { iz0 = 0; }
        if (ix1 >= (int)dimX) { ix1 = (int)dimX - 1; }
        if (iy1 >= (int)dimY) { iy1 = (int)dimY - 1; }
        if (iz1 >= (int)dimZ) { iz1 = (int)dimZ - 1; }

        for (int z = iz0; z <= iz1; ++z)
        {
            for (int y = iy0; y <= iy1; ++y)
            {
                for (int x = ix0; x <= ix1; ++x)
                {
                    const uint32_t cellIndex = (uint32_t)((z * (int)dimY + y) * (int)dimX + x);
                    indices[cursor[cellIndex]++] = i;
                }
            }
        }
    }

    const VkDeviceSize rangesBytes = sizeof(uint32_t) * 2u * cellCount;
    const VkDeviceSize indicesBytes = sizeof(uint32_t) * (VkDeviceSize)indicesCapacity;
    const VkDeviceSize coarseBytes = sizeof(uint32_t) * coarseCount;

    uint32_t *ranges = (uint32_t *)malloc((size_t)rangesBytes);
    Assert(ranges != NULL, "Failed to allocate grid ranges");

    for (uint32_t c = 0u; c < cellCount; ++c)
    {
        ranges[c * 2u + 0u] = starts[c];
        ranges[c * 2u + 1u] = counts[c];
    }

    CreateOrResizeBuffer(rangesBytes, &GLOBAL.Vulkan.rt.gridRanges, &GLOBAL.Vulkan.rt.gridRangesAlloc);
    CreateOrResizeBuffer(indicesBytes, &GLOBAL.Vulkan.rt.gridIndices, &GLOBAL.Vulkan.rt.gridIndicesAlloc);
    CreateOrResizeBuffer(coarseBytes, &GLOBAL.Vulkan.rt.gridCoarseCounts, &GLOBAL.Vulkan.rt.gridCoarseCountsAlloc);

    ComputeDS ds = {
        .targetView = GLOBAL.Vulkan.gradientImageView,
        .targetSampler = GLOBAL.Vulkan.gradientSampler,
        .sphereCR = GLOBAL.Vulkan.rt.sphereCR,
        .sphereAlb = GLOBAL.Vulkan.rt.sphereAlb,
        .hitT = GLOBAL.Vulkan.rt.hitT,
        .hitN = GLOBAL.Vulkan.rt.hitN,
        .accum = GLOBAL.Vulkan.rt.accum,
        .spp = GLOBAL.Vulkan.rt.spp,
        .epoch = GLOBAL.Vulkan.rt.epoch,
        .gridRanges = GLOBAL.Vulkan.rt.gridRanges,
        .gridIndices = GLOBAL.Vulkan.rt.gridIndices,
        .gridCoarseCounts = GLOBAL.Vulkan.rt.gridCoarseCounts,
    };

    UpdateComputeDescriptorSet(&ds);

    if (rangesBytes > 0)
    {
        vkCmdUpdateBuffer(cmd, GLOBAL.Vulkan.rt.gridRanges, 0, rangesBytes, ranges);
    }

    if (indicesBytes > 0)
    {
        vkCmdUpdateBuffer(cmd, GLOBAL.Vulkan.rt.gridIndices, 0, indicesBytes, indices);
    }
    if (coarseBytes > 0)
    {
        vkCmdUpdateBuffer(cmd, GLOBAL.Vulkan.rt.gridCoarseCounts, 0, coarseBytes, coarseCounts);
    }

    VkBufferMemoryBarrier2 barriers[3] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridRanges,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridIndices,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridCoarseCounts,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
    };

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = ARRAY_SIZE(barriers),
        .pBufferMemoryBarriers = barriers,
    };

    vkCmdPipelineBarrier2(cmd, &dep);

    free(counts);
    free(coarseCounts);
    free(starts);
    free(cursor);
    free(indices);
    free(ranges);
}

static uint32_t WangHash(uint32_t value)
{
    value = (value ^ 61u) ^ (value >> 16);
    value *= 9u;
    value = value ^ (value >> 4);
    value *= 0x27d4eb2du;
    value = value ^ (value >> 15);
    return value;
}

static float Rand01(uint32_t *state)
{
    Assert(state != NULL, "Random state pointer is null");
    *state = WangHash(*state);
    return (float)(*state) * (1.0f / 4294967296.0f);
}

static float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static uint32_t GenerateSphereData(uint32_t seed)
{
    const uint32_t desiredCount = GLOBAL.Vulkan.sphereTargetCount;
    if (desiredCount == 0u)
    {
        GLOBAL.Vulkan.sphereCount = 0u;
        return 0u;
    }

    float minRadius = GLOBAL.Vulkan.sphereMinRadius;
    float maxRadius = GLOBAL.Vulkan.sphereMaxRadius;
    if (minRadius <= 0.0f)
    {
        minRadius = 0.05f;
    }

    if (maxRadius < minRadius)
    {
        maxRadius = minRadius;
    }

    const float baseMinX = GLOBAL.Vulkan.worldMinX;
    const float baseMaxX = GLOBAL.Vulkan.worldMaxX;
    const float baseMinZ = GLOBAL.Vulkan.worldMinZ;
    const float baseMaxZ = GLOBAL.Vulkan.worldMaxZ;
    const float groundY = GLOBAL.Vulkan.groundY;

    Assert(baseMaxX > baseMinX, "Sphere spawn range X is invalid");
    Assert(baseMaxZ > baseMinZ, "Sphere spawn range Z is invalid");

    const uint32_t maxAttempts = 1024u;
    uint32_t rng = seed ^ 0x9e3779b9u;
    uint32_t placed = 0u;

    float spanX = baseMaxX - baseMinX;
    float spanZ = baseMaxZ - baseMinZ;
    float cellSize = fmaxf(maxRadius * 2.0f, 0.05f);
    Assert(spanX > 0.0f && spanZ > 0.0f, "Sphere spawn area is invalid");

    uint32_t gridDimX = (uint32_t)fmaxf(1.0f, ceilf(spanX / cellSize));
    uint32_t gridDimZ = (uint32_t)fmaxf(1.0f, ceilf(spanZ / cellSize));
    uint32_t gridCellCount = gridDimX * gridDimZ;

    uint32_t *cellHeads = (uint32_t *)malloc(sizeof(uint32_t) * gridCellCount);
    Assert(cellHeads != NULL, "Failed to allocate sphere occupancy grid");
    uint32_t *cellNext = (uint32_t *)malloc(sizeof(uint32_t) * desiredCount);
    Assert(cellNext != NULL, "Failed to allocate sphere occupancy links");

    memset(cellHeads, 0xFF, sizeof(uint32_t) * gridCellCount);
    memset(cellNext, 0xFF, sizeof(uint32_t) * desiredCount);

    for (uint32_t i = 0; i < desiredCount; ++i)
    {
        bool success = false;
        for (uint32_t attempt = 0; attempt < maxAttempts; ++attempt)
        {
            const float radiusSpan = maxRadius - minRadius;
            float radius = (radiusSpan > 0.0f) ? (minRadius + radiusSpan * Rand01(&rng)) : maxRadius;
            radius = fmaxf(radius, minRadius);

            float minX = baseMinX + radius;
            float maxX = baseMaxX - radius;
            float minZ = baseMinZ + radius;
            float maxZ = baseMaxZ - radius;

            if ((maxX <= minX) || (maxZ <= minZ))
            {
                continue;
            }

            float x = Lerp(minX, maxX, Rand01(&rng));
            float z = Lerp(minZ, maxZ, Rand01(&rng));

            int cellX = (int)floorf((x - baseMinX) / cellSize);
            int cellZ = (int)floorf((z - baseMinZ) / cellSize);
            if (cellX < 0) { cellX = 0; }
            if (cellZ < 0) { cellZ = 0; }
            if (cellX >= (int)gridDimX) { cellX = (int)gridDimX - 1; }
            if (cellZ >= (int)gridDimZ) { cellZ = (int)gridDimZ - 1; }

            bool overlaps = false;
            for (int nz = cellZ - 1; nz <= cellZ + 1; ++nz)
            {
                if ((nz < 0) || (nz >= (int)gridDimZ))
                {
                    continue;
                }
                for (int nx = cellX - 1; nx <= cellX + 1; ++nx)
                {
                    if ((nx < 0) || (nx >= (int)gridDimX))
                    {
                        continue;
                    }
                    uint32_t cellIndex = (uint32_t)nz * gridDimX + (uint32_t)nx;
                    uint32_t head = cellHeads[cellIndex];
                    while (head != UINT32_MAX)
                    {
                        float ox = GLOBAL.Vulkan.sphereCRHost[head * 4u + 0u];
                        float oz = GLOBAL.Vulkan.sphereCRHost[head * 4u + 2u];
                        float oradius = GLOBAL.Vulkan.sphereCRHost[head * 4u + 3u];
                        float dx = x - ox;
                        float dz = z - oz;
                        float minDist = radius + oradius;
                        if ((dx * dx + dz * dz) < (minDist * minDist))
                        {
                            overlaps = true;
                            break;
                        }
                        head = cellNext[head];
                    }
                    if (overlaps)
                    {
                        break;
                    }
                }
                if (overlaps)
                {
                    break;
                }
            }

            if (overlaps)
            {
                continue;
            }

            GLOBAL.Vulkan.sphereCRHost[i * 4u + 0u] = x;
            GLOBAL.Vulkan.sphereCRHost[i * 4u + 1u] = groundY + radius;
            GLOBAL.Vulkan.sphereCRHost[i * 4u + 2u] = z;
            GLOBAL.Vulkan.sphereCRHost[i * 4u + 3u] = radius;

            float hue = Rand01(&rng);
            float green = Rand01(&rng) * 0.25f + 0.65f;
            float blue = Rand01(&rng) * 0.4f + 0.4f;

            GLOBAL.Vulkan.sphereAlbHost[i * 4u + 0u] = hue;
            GLOBAL.Vulkan.sphereAlbHost[i * 4u + 1u] = green;
            GLOBAL.Vulkan.sphereAlbHost[i * 4u + 2u] = blue;
            GLOBAL.Vulkan.sphereAlbHost[i * 4u + 3u] = 1.0f;

            uint32_t targetCell = (uint32_t)cellZ * gridDimX + (uint32_t)cellX;
            cellNext[i] = cellHeads[targetCell];
            cellHeads[targetCell] = i;

            placed++;
            success = true;
            break;
        }

        if (!success)
        {
            LogWarn("Unable to place sphere %u without overlap after %u attempts", i, maxAttempts);
            break;
        }
    }

    if (placed < desiredCount)
    {
        LogWarn("Placed %u spheres out of %u requested", placed, desiredCount);
    }

    free(cellHeads);
    free(cellNext);

    GLOBAL.Vulkan.sphereCount = placed;
    return placed;
}

static void UpdateSpawnArea(void)
{
    float radius = GLOBAL.Vulkan.sphereMaxRadius;
    if (radius <= 0.0f)
    {
        radius = 0.25f;
    }

    float baseCellSize = radius * 3.0f;
    if (baseCellSize < (radius * 2.05f))
    {
        baseCellSize = radius * 2.05f;
    }

    float density = GLOBAL.Vulkan.sphereTargetDensity;
    if (density <= 0.0f)
    {
        float cellArea = baseCellSize * baseCellSize;
        if (cellArea <= 0.0f)
        {
            cellArea = 1.0f;
        }
        density = 1.0f / cellArea;
    }
    density = fmaxf(density, 1e-6f);

    uint32_t count = GLOBAL.Vulkan.sphereTargetCount;
    if (count == 0u)
    {
        count = 1u;
    }

    float area = (float)count / density;
    if (area <= 0.0f)
    {
        area = baseCellSize * baseCellSize;
    }

    float half = 0.5f * sqrtf(area);

    GLOBAL.Vulkan.worldMinX = -half;
    GLOBAL.Vulkan.worldMaxX = half;
    GLOBAL.Vulkan.worldMinZ = -half;
    GLOBAL.Vulkan.worldMaxZ = half;
}

void RtUpdateSpawnArea(void)
{
    UpdateSpawnArea();
}

void RtRecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkExtent2D extent)
{
    Assert(commandBuffer != VK_NULL_HANDLE, "Vulkan command buffer is not available");
    Assert(GLOBAL.Vulkan.shadeShadowPipe != VK_NULL_HANDLE, "Shade shadow pipeline is not ready");
    Assert(GLOBAL.Vulkan.blitPipeline != VK_NULL_HANDLE, "Vulkan blit pipeline is not ready");
    Assert(GLOBAL.Vulkan.descriptorSet != VK_NULL_HANDLE, "Vulkan descriptor set is not ready");
    Assert(GLOBAL.Vulkan.gradientImage != VK_NULL_HANDLE, "Vulkan gradient image is not ready");
    Assert(GLOBAL.Vulkan.gradientImageView != VK_NULL_HANDLE, "Vulkan gradient image view is not ready");
    Assert(GLOBAL.Vulkan.computePipelineLayout != VK_NULL_HANDLE, "Vulkan compute pipeline layout is not ready");
    Assert(GLOBAL.Vulkan.blitPipelineLayout != VK_NULL_HANDLE, "Vulkan blit pipeline layout is not ready");
    Assert(GLOBAL.Vulkan.swapchainImageViews[imageIndex] != VK_NULL_HANDLE, "Vulkan swapchain image view is not ready");
    Assert(imageIndex < GLOBAL.Vulkan.swapchainImageCount, "Vulkan swapchain image index out of range");
    Assert(GLOBAL.Vulkan.rt.sphereCR != VK_NULL_HANDLE, "Sphere center-radius buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.sphereAlb != VK_NULL_HANDLE, "Sphere albedo buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.hitT != VK_NULL_HANDLE, "Hit distance buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.hitN != VK_NULL_HANDLE, "Hit normal buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.accum != VK_NULL_HANDLE, "Accum buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.spp != VK_NULL_HANDLE, "Sample count buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.epoch != VK_NULL_HANDLE, "Accumulation epoch buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridRanges != VK_NULL_HANDLE, "Grid range buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridIndices != VK_NULL_HANDLE, "Grid index buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridCoarseCounts != VK_NULL_HANDLE, "Grid coarse count buffer is not ready");

    VkResult resetResult = vkResetCommandBuffer(commandBuffer, 0);
    Assert(resetResult == VK_SUCCESS, "Failed to reset Vulkan command buffer");

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult beginResult = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    Assert(beginResult == VK_SUCCESS, "Failed to begin Vulkan command buffer");

    VkImageMemoryBarrier2 toGeneral = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = GLOBAL.Vulkan.gradientInitialized ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = GLOBAL.Vulkan.gradientInitialized ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .oldLayout = GLOBAL.Vulkan.gradientInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = GLOBAL.Vulkan.gradientImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo toGeneralDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toGeneral,
    };

    vkCmdPipelineBarrier2(commandBuffer, &toGeneralDependency);

    bool needAccumReset = (!GLOBAL.Vulkan.gradientInitialized) ||
        GLOBAL.Vulkan.resetAccumulation ||
        (!GLOBAL.Vulkan.sceneInitialized);
    if (needAccumReset)
    {
        GLOBAL.Vulkan.accumulationEpoch++;
        if (GLOBAL.Vulkan.accumulationEpoch == 0u)
        {
            GLOBAL.Vulkan.accumulationEpoch = 1u;
        }
        GLOBAL.Vulkan.resetAccumulation = false;
    }

    Assert(GLOBAL.Vulkan.sphereTargetCount <= RT_MAX_SPHERES, "Sphere target count exceeds capacity");
    Assert(GLOBAL.Vulkan.sphereCount <= RT_MAX_SPHERES, "Sphere count exceeds capacity");

    UpdateSpawnArea();

    uint32_t frame = GLOBAL.Vulkan.frameIndex++;

    if (!GLOBAL.Vulkan.sceneInitialized)
    {
        uint32_t placed = GenerateSphereData(frame);

        VkDeviceSize sphereBytes = (VkDeviceSize)placed * sizeof(float) * 4u;
        if (GLOBAL.Vulkan.sphereTargetCount == 0u)
        {
            GLOBAL.Vulkan.sceneInitialized = true;
            GLOBAL.Vulkan.sphereCount = 0u;
        }
        else if (placed > 0u)
        {
            if (sphereBytes > 0)
            {
                vkCmdUpdateBuffer(commandBuffer, GLOBAL.Vulkan.rt.sphereCR, 0, sphereBytes, GLOBAL.Vulkan.sphereCRHost);
                vkCmdUpdateBuffer(commandBuffer, GLOBAL.Vulkan.rt.sphereAlb, 0, sphereBytes, GLOBAL.Vulkan.sphereAlbHost);

                VkBufferMemoryBarrier2 readyBarriers[2] = {
                    {
                        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                        .buffer = GLOBAL.Vulkan.rt.sphereCR,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE,
                    },
                    {
                        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                        .buffer = GLOBAL.Vulkan.rt.sphereAlb,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE,
                    },
                };

                VkDependencyInfo readyDependency = {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .bufferMemoryBarrierCount = ARRAY_SIZE(readyBarriers),
                    .pBufferMemoryBarriers = readyBarriers,
                };

                vkCmdPipelineBarrier2(commandBuffer, &readyDependency);
            }

            BuildUniformGrid(commandBuffer, placed);

            GLOBAL.Vulkan.sceneInitialized = true;
        }
        else
        {
            LogWarn("Sphere placement failed, will retry next frame");
        }
    }

    const float aspect = (extent.height > 0u) ? ((float)extent.width / (float)extent.height) : 1.0f;

    PCPush pc = {
        .width = extent.width,
        .height = extent.height,
        .frame = frame,
        .sphereCount = GLOBAL.Vulkan.sphereCount,
        .accumulationEpoch = GLOBAL.Vulkan.accumulationEpoch,
        .tanHalfFovY = tanf(0.5f * GLOBAL.Vulkan.cam.fovY),
        .aspect = aspect,
        ._pad0 = 0.0f,
        .camPos = { GLOBAL.Vulkan.cam.pos.x, GLOBAL.Vulkan.cam.pos.y, GLOBAL.Vulkan.cam.pos.z },
        ._pad1 = 0.0f,
        .camFwd = { GLOBAL.Vulkan.cam.fwd.x, GLOBAL.Vulkan.cam.fwd.y, GLOBAL.Vulkan.cam.fwd.z },
        ._pad2 = 0.0f,
        .camRight = { GLOBAL.Vulkan.cam.right.x, GLOBAL.Vulkan.cam.right.y, GLOBAL.Vulkan.cam.right.z },
        ._pad3 = 0.0f,
        .camUp = { GLOBAL.Vulkan.cam.up.x, GLOBAL.Vulkan.cam.up.y, GLOBAL.Vulkan.cam.up.z },
        ._pad4 = 0.0f,
        .worldMin = { GLOBAL.Vulkan.worldMinX, GLOBAL.Vulkan.worldMinZ },
        .worldMax = { GLOBAL.Vulkan.worldMaxX, GLOBAL.Vulkan.worldMaxZ },
        .groundY = GLOBAL.Vulkan.groundY,
        ._pad5 = { 0.0f, 0.0f, 0.0f },
        .gridDim = { GLOBAL.Vulkan.gridDimX, GLOBAL.Vulkan.gridDimY, GLOBAL.Vulkan.gridDimZ },
        .showGrid = GLOBAL.Vulkan.showGrid ? 1u : 0u,
        .gridMin = { GLOBAL.Vulkan.gridMinX, GLOBAL.Vulkan.gridMinY, GLOBAL.Vulkan.gridMinZ },
        ._pad6 = 0.0f,
        .gridInvCell = { GLOBAL.Vulkan.gridInvCellX, GLOBAL.Vulkan.gridInvCellY, GLOBAL.Vulkan.gridInvCellZ },
        ._pad7 = 0.0f,
        .coarseDim = { GLOBAL.Vulkan.coarseDimX, GLOBAL.Vulkan.coarseDimY, GLOBAL.Vulkan.coarseDimZ },
        .coarseFactor = GLOBAL.Vulkan.coarseFactor,
        .coarseInvCell = { GLOBAL.Vulkan.coarseInvCellX, GLOBAL.Vulkan.coarseInvCellY, GLOBAL.Vulkan.coarseInvCellZ },
        ._pad8 = 0.0f,
    };

    const uint32_t localSizeX = (GLOBAL.Vulkan.computeLocalSizeX > 0u) ? GLOBAL.Vulkan.computeLocalSizeX : VULKAN_COMPUTE_LOCAL_SIZE;
    const uint32_t localSizeY = (GLOBAL.Vulkan.computeLocalSizeY > 0u) ? GLOBAL.Vulkan.computeLocalSizeY : VULKAN_COMPUTE_LOCAL_SIZE;

    Assert(localSizeX > 0u, "Compute local size X is zero");
    Assert(localSizeY > 0u, "Compute local size Y is zero");

    const uint32_t groupCountX = (pc.width + localSizeX - 1u) / localSizeX;
    const uint32_t groupCountY = (pc.height + localSizeY - 1u) / localSizeY;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.shadeShadowPipe);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        GLOBAL.Vulkan.computePipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);
    vkCmdPushConstants(
        commandBuffer,
        GLOBAL.Vulkan.computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pc),
        &pc);
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

    VkImageMemoryBarrier2 toRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = GLOBAL.Vulkan.gradientImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo toReadDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toRead,
    };

    vkCmdPipelineBarrier2(commandBuffer, &toReadDependency);

    VkImageMemoryBarrier2 swapchainPre = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image = GLOBAL.Vulkan.swapchainImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo swapchainPreDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &swapchainPre,
    };

    vkCmdPipelineBarrier2(commandBuffer, &swapchainPreDependency);

    VkClearValue clearColor = {
        .color = { { 0.0f, 0.0f, 0.0f, 1.0f } },
    };

    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = GLOBAL.Vulkan.swapchainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = extent,
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GLOBAL.Vulkan.blitPipeline);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        GLOBAL.Vulkan.blitPipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 swapchainPost = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = GLOBAL.Vulkan.swapchainImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo swapchainPostDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &swapchainPost,
    };

    vkCmdPipelineBarrier2(commandBuffer, &swapchainPostDependency);

    VkResult endResult = vkEndCommandBuffer(commandBuffer);
    Assert(endResult == VK_SUCCESS, "Failed to record Vulkan frame command buffer");

    GLOBAL.Vulkan.gradientInitialized = true;
}
