#include "runtime.h"
#include "rt_frame.h"

#include <math.h>

static void UpdateSpawnArea(void)
{
    float radius = GLOBAL.Vulkan.sphereRadius;
    if (radius <= 0.0f)
    {
        radius = 0.25f;
    }

    float baseCellSize = radius * 3.0f;
    if (baseCellSize < (radius * 2.05f))
    {
        baseCellSize = radius * 2.05f;
    }

    uint32_t count = GLOBAL.Vulkan.sphereCount;
    if (count < 16)
    {
        count = 16;
    }

    float area = (float)count * baseCellSize * baseCellSize;
    float side = sqrtf(area);
    float half = side * 0.5f;

    if (GLOBAL.Vulkan.worldMinX >= GLOBAL.Vulkan.worldMaxX)
    {
        GLOBAL.Vulkan.worldMinX = -half;
        GLOBAL.Vulkan.worldMaxX = half;
    }

    if (GLOBAL.Vulkan.worldMinZ >= GLOBAL.Vulkan.worldMaxZ)
    {
        GLOBAL.Vulkan.worldMinZ = -half;
        GLOBAL.Vulkan.worldMaxZ = half;
    }
}

void RtUpdateSpawnArea(void)
{
    UpdateSpawnArea();
}

void RtRecordFrame(uint32_t imageIndex, VkExtent2D extent)
{
    Assert(GLOBAL.Vulkan.commandBuffer != VK_NULL_HANDLE, "Vulkan command buffer is not available");
    Assert(GLOBAL.Vulkan.spheresInitPipe != VK_NULL_HANDLE, "Spheres init pipeline is not ready");
    Assert(GLOBAL.Vulkan.primaryIntersectPipe != VK_NULL_HANDLE, "Primary intersect pipeline is not ready");
    Assert(GLOBAL.Vulkan.shadeShadowPipe != VK_NULL_HANDLE, "Shade shadow pipeline is not ready");
    Assert(GLOBAL.Vulkan.gridCountPipe != VK_NULL_HANDLE, "Grid count pipeline is not ready");
    Assert(GLOBAL.Vulkan.gridClassifyPipe != VK_NULL_HANDLE, "Grid classify pipeline is not ready");
    Assert(GLOBAL.Vulkan.gridScatterPipe != VK_NULL_HANDLE, "Grid scatter pipeline is not ready");
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
    Assert(GLOBAL.Vulkan.rt.gridLevel0Meta != VK_NULL_HANDLE, "Grid level0 meta buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridLevel0Counter != VK_NULL_HANDLE, "Grid level0 counter buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridLevel0Indices != VK_NULL_HANDLE, "Grid level0 index buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridLevel1Meta != VK_NULL_HANDLE, "Grid level1 meta buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridLevel1Counter != VK_NULL_HANDLE, "Grid level1 counter buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridLevel1Indices != VK_NULL_HANDLE, "Grid level1 index buffer is not ready");
    Assert(GLOBAL.Vulkan.rt.gridState != VK_NULL_HANDLE, "Grid state buffer is not ready");

    VkResult resetResult = vkResetCommandBuffer(GLOBAL.Vulkan.commandBuffer, 0);
    Assert(resetResult == VK_SUCCESS, "Failed to reset Vulkan command buffer");

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult beginResult = vkBeginCommandBuffer(GLOBAL.Vulkan.commandBuffer, &beginInfo);
    Assert(beginResult == VK_SUCCESS, "Failed to begin Vulkan command buffer");

    VkImageMemoryBarrier2 toGeneral = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = GLOBAL.Vulkan.gradientInitialized ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
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

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &toGeneralDependency);

    Assert(GLOBAL.Vulkan.sphereCount <= RT_MAX_SPHERES, "Sphere count exceeds capacity");

    UpdateSpawnArea();

    const uint32_t sphereCount = GLOBAL.Vulkan.sphereCount;
    uint32_t gridDimBase = 1u;
    if (sphereCount > 0u)
    {
        float root = sqrtf((float)sphereCount);
        uint32_t approx = (uint32_t)ceilf(root * 0.5f);
        if (approx < 1u)
        {
            approx = 1u;
        }
        if (approx > (uint32_t)GRID_MAX_LEVEL0_DIM)
        {
            approx = (uint32_t)GRID_MAX_LEVEL0_DIM;
        }
        gridDimBase = approx;
    }

    uint32_t gridDimX = gridDimBase;
    uint32_t gridDimZ = gridDimBase;
    if (gridDimX == 0u)
    {
        gridDimX = 1u;
    }
    if (gridDimZ == 0u)
    {
        gridDimZ = 1u;
    }

    const uint32_t gridFineDim = (uint32_t)GRID_FINE_DIM;
    uint32_t gridRefineThreshold = gridFineDim * gridFineDim / 2u;
    if (gridRefineThreshold < 1u)
    {
        gridRefineThreshold = 1u;
    }

    PCPush pc = {
        .width = extent.width,
        .height = extent.height,
        .frame = GLOBAL.Vulkan.frameIndex++,
        .sphereCount = GLOBAL.Vulkan.sphereCount,
        .camPos = { GLOBAL.Vulkan.cam.pos.x, GLOBAL.Vulkan.cam.pos.y, GLOBAL.Vulkan.cam.pos.z },
        .fovY = GLOBAL.Vulkan.cam.fovY,
        .camFwd = { GLOBAL.Vulkan.cam.fwd.x, GLOBAL.Vulkan.cam.fwd.y, GLOBAL.Vulkan.cam.fwd.z },
        .camRight = { GLOBAL.Vulkan.cam.right.x, GLOBAL.Vulkan.cam.right.y, GLOBAL.Vulkan.cam.right.z },
        .camUp = { GLOBAL.Vulkan.cam.up.x, GLOBAL.Vulkan.cam.up.y, GLOBAL.Vulkan.cam.up.z },
        .worldMin = { GLOBAL.Vulkan.worldMinX, GLOBAL.Vulkan.worldMinZ },
        .worldMax = { GLOBAL.Vulkan.worldMaxX, GLOBAL.Vulkan.worldMaxZ },
        .sphereRadius = GLOBAL.Vulkan.sphereRadius,
        .groundY = GLOBAL.Vulkan.groundY,
        .rngSeed = 1337u,
        .flags = 0u,
        .gridDimX = gridDimX,
        .gridDimZ = gridDimZ,
        .gridFineDim = gridFineDim,
        .gridRefineThreshold = gridRefineThreshold,
    };

    const uint32_t groupCountX = (pc.width + VULKAN_COMPUTE_LOCAL_SIZE - 1u) / VULKAN_COMPUTE_LOCAL_SIZE;
    const uint32_t groupCountY = (pc.height + VULKAN_COMPUTE_LOCAL_SIZE - 1u) / VULKAN_COMPUTE_LOCAL_SIZE;
    const uint32_t gridCountGroups = (pc.sphereCount + 64u - 1u) / 64u;
    const uint32_t gridClassifyGroups = (gridDimX * gridDimZ + 64u - 1u) / 64u;

    if (!GLOBAL.Vulkan.sceneInitialized)
    {
        vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.spheresInitPipe);
        vkCmdBindDescriptorSets(
            GLOBAL.Vulkan.commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            GLOBAL.Vulkan.computePipelineLayout,
            0,
            1,
            &GLOBAL.Vulkan.descriptorSet,
            0,
            NULL);
        vkCmdPushConstants(
            GLOBAL.Vulkan.commandBuffer,
            GLOBAL.Vulkan.computePipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(pc),
            &pc);

        if (pc.sphereCount > 0u)
        {
            const uint32_t sphereGroups = (pc.sphereCount + 64u - 1u) / 64u;
            vkCmdDispatch(GLOBAL.Vulkan.commandBuffer, sphereGroups, 1, 1);
        }

        VkBufferMemoryBarrier2 sphereBarriers[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .buffer = GLOBAL.Vulkan.rt.sphereCR,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .buffer = GLOBAL.Vulkan.rt.sphereAlb,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
        };

        VkDependencyInfo sphereDependency = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = ARRAY_SIZE(sphereBarriers),
            .pBufferMemoryBarriers = sphereBarriers,
        };

        vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &sphereDependency);
        GLOBAL.Vulkan.sceneInitialized = true;
    }

    const VkDeviceSize gridL0MetaSize = sizeof(uint32_t) * 4 * (VkDeviceSize)GRID_LEVEL0_CELLS;
    const VkDeviceSize gridL0CounterSize = sizeof(uint32_t) * (VkDeviceSize)GRID_LEVEL0_CELLS;
    const VkDeviceSize gridL1MetaSize = sizeof(uint32_t) * 4 * (VkDeviceSize)GRID_LEVEL1_CELLS;
    const VkDeviceSize gridL1CounterSize = sizeof(uint32_t) * (VkDeviceSize)GRID_LEVEL1_CELLS;
    const VkDeviceSize gridStateSize = sizeof(uint32_t) * 4;

    vkCmdFillBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.gridLevel0Meta, 0, gridL0MetaSize, 0);
    vkCmdFillBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.gridLevel0Counter, 0, gridL0CounterSize, 0);
    vkCmdFillBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.gridLevel1Meta, 0, gridL1MetaSize, 0);
    vkCmdFillBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.gridLevel1Counter, 0, gridL1CounterSize, 0);
    vkCmdFillBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.gridState, 0, gridStateSize, 0);

    VkBufferMemoryBarrier2 clearBarriers[5] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel0Meta,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel0Counter,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel1Meta,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel1Counter,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridState,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
    };

    VkDependencyInfo clearDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = ARRAY_SIZE(clearBarriers),
        .pBufferMemoryBarriers = clearBarriers,
    };

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &clearDependency);

    if (pc.sphereCount > 0u)
    {
        vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.gridCountPipe);
        vkCmdBindDescriptorSets(
            GLOBAL.Vulkan.commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            GLOBAL.Vulkan.computePipelineLayout,
            0,
            1,
            &GLOBAL.Vulkan.descriptorSet,
            0,
            NULL);
        vkCmdPushConstants(
            GLOBAL.Vulkan.commandBuffer,
            GLOBAL.Vulkan.computePipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(pc),
            &pc);
        vkCmdDispatch(GLOBAL.Vulkan.commandBuffer, gridCountGroups, 1, 1);
    }

    VkBufferMemoryBarrier2 countBarriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel0Meta,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel1Meta,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
    };

    VkDependencyInfo countDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = ARRAY_SIZE(countBarriers),
        .pBufferMemoryBarriers = countBarriers,
    };

    if (pc.sphereCount > 0u)
    {
        vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &countDependency);
    }

    vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.gridClassifyPipe);
    vkCmdBindDescriptorSets(
        GLOBAL.Vulkan.commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        GLOBAL.Vulkan.computePipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);
    vkCmdPushConstants(
        GLOBAL.Vulkan.commandBuffer,
        GLOBAL.Vulkan.computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pc),
        &pc);
    vkCmdDispatch(GLOBAL.Vulkan.commandBuffer, gridClassifyGroups, 1, 1);

    VkBufferMemoryBarrier2 classifyBarriers[3] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel0Meta,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel1Meta,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel1Counter,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
    };

    VkDependencyInfo classifyDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = ARRAY_SIZE(classifyBarriers),
        .pBufferMemoryBarriers = classifyBarriers,
    };

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &classifyDependency);

    if (pc.sphereCount > 0u)
    {
        vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.gridScatterPipe);
        vkCmdBindDescriptorSets(
            GLOBAL.Vulkan.commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            GLOBAL.Vulkan.computePipelineLayout,
            0,
            1,
            &GLOBAL.Vulkan.descriptorSet,
            0,
            NULL);
        vkCmdPushConstants(
            GLOBAL.Vulkan.commandBuffer,
            GLOBAL.Vulkan.computePipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(pc),
            &pc);
        vkCmdDispatch(GLOBAL.Vulkan.commandBuffer, gridCountGroups, 1, 1);
    }

    VkBufferMemoryBarrier2 scatterBarriers[5] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel0Indices,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel1Indices,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel0Meta,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel1Meta,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.gridLevel0Counter,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
    };

    VkDependencyInfo scatterDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = ARRAY_SIZE(scatterBarriers),
        .pBufferMemoryBarriers = scatterBarriers,
    };

    if (pc.sphereCount > 0u)
    {
        vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &scatterDependency);
    }

    vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.primaryIntersectPipe);
    vkCmdBindDescriptorSets(
        GLOBAL.Vulkan.commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        GLOBAL.Vulkan.computePipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);
    vkCmdPushConstants(
        GLOBAL.Vulkan.commandBuffer,
        GLOBAL.Vulkan.computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pc),
        &pc);
    vkCmdDispatch(GLOBAL.Vulkan.commandBuffer, groupCountX, groupCountY, 1);

    VkBufferMemoryBarrier2 hitBarriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.hitT,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = GLOBAL.Vulkan.rt.hitN,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        },
    };

    VkDependencyInfo hitDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = ARRAY_SIZE(hitBarriers),
        .pBufferMemoryBarriers = hitBarriers,
    };

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &hitDependency);

    vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.shadeShadowPipe);
    vkCmdBindDescriptorSets(
        GLOBAL.Vulkan.commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        GLOBAL.Vulkan.computePipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);
    vkCmdPushConstants(
        GLOBAL.Vulkan.commandBuffer,
        GLOBAL.Vulkan.computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pc),
        &pc);
    vkCmdDispatch(GLOBAL.Vulkan.commandBuffer, groupCountX, groupCountY, 1);

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

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &toReadDependency);

    VkImageMemoryBarrier2 swapchainPre = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
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

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &swapchainPreDependency);

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

    vkCmdBeginRendering(GLOBAL.Vulkan.commandBuffer, &renderingInfo);
    vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GLOBAL.Vulkan.blitPipeline);
    vkCmdBindDescriptorSets(
        GLOBAL.Vulkan.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        GLOBAL.Vulkan.blitPipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);

    vkCmdDraw(GLOBAL.Vulkan.commandBuffer, 3, 1, 0, 0);
    vkCmdEndRendering(GLOBAL.Vulkan.commandBuffer);

    VkImageMemoryBarrier2 swapchainPost = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
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

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &swapchainPostDependency);

    VkResult endResult = vkEndCommandBuffer(GLOBAL.Vulkan.commandBuffer);
    Assert(endResult == VK_SUCCESS, "Failed to record Vulkan frame command buffer");

    GLOBAL.Vulkan.gradientInitialized = true;
}
