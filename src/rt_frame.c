#include "runtime.h"
#include "rt_frame.h"

#include <math.h>

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

    const uint32_t maxAttempts = 256u;
    uint32_t rng = seed ^ 0x9e3779b9u;
    uint32_t placed = 0u;

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

            bool overlaps = false;
            for (uint32_t j = 0; j < placed; ++j)
            {
                float ox = GLOBAL.Vulkan.sphereCRHost[j * 4u + 0u];
                float oz = GLOBAL.Vulkan.sphereCRHost[j * 4u + 2u];
                float oradius = GLOBAL.Vulkan.sphereCRHost[j * 4u + 3u];
                float dx = x - ox;
                float dz = z - oz;
                float minDist = radius + oradius;
                if ((dx * dx + dz * dz) < (minDist * minDist))
                {
                    overlaps = true;
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

    uint32_t count = GLOBAL.Vulkan.sphereTargetCount;
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
    Assert(GLOBAL.Vulkan.primaryIntersectPipe != VK_NULL_HANDLE, "Primary intersect pipeline is not ready");
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

    bool needAccumReset = (!GLOBAL.Vulkan.gradientInitialized) ||
        GLOBAL.Vulkan.resetAccumulation ||
        (!GLOBAL.Vulkan.sceneInitialized);
    if (needAccumReset)
    {
        // Clear progressive accumulation buffers after creation
        vkCmdFillBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.accum, 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.spp, 0, VK_WHOLE_SIZE, 0);

        VkBufferMemoryBarrier2 clearBarriers[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .buffer = GLOBAL.Vulkan.rt.accum,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
            {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .buffer = GLOBAL.Vulkan.rt.spp,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
        };

        VkDependencyInfo clearDep = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = ARRAY_SIZE(clearBarriers),
            .pBufferMemoryBarriers = clearBarriers,
        };

        vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &clearDep);
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
                vkCmdUpdateBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.sphereCR, 0, sphereBytes, GLOBAL.Vulkan.sphereCRHost);
                vkCmdUpdateBuffer(GLOBAL.Vulkan.commandBuffer, GLOBAL.Vulkan.rt.sphereAlb, 0, sphereBytes, GLOBAL.Vulkan.sphereAlbHost);

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

                vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &readyDependency);
            }

            GLOBAL.Vulkan.sceneInitialized = true;
        }
        else
        {
            LogWarn("Sphere placement failed, will retry next frame");
        }
    }

    PCPush pc = {
        .width = extent.width,
        .height = extent.height,
        .frame = frame,
        .sphereCount = GLOBAL.Vulkan.sphereCount,
        .camPos = { GLOBAL.Vulkan.cam.pos.x, GLOBAL.Vulkan.cam.pos.y, GLOBAL.Vulkan.cam.pos.z },
        .fovY = GLOBAL.Vulkan.cam.fovY,
        .camFwd = { GLOBAL.Vulkan.cam.fwd.x, GLOBAL.Vulkan.cam.fwd.y, GLOBAL.Vulkan.cam.fwd.z },
        .camRight = { GLOBAL.Vulkan.cam.right.x, GLOBAL.Vulkan.cam.right.y, GLOBAL.Vulkan.cam.right.z },
        .camUp = { GLOBAL.Vulkan.cam.up.x, GLOBAL.Vulkan.cam.up.y, GLOBAL.Vulkan.cam.up.z },
        .worldMin = { GLOBAL.Vulkan.worldMinX, GLOBAL.Vulkan.worldMinZ },
        .worldMax = { GLOBAL.Vulkan.worldMaxX, GLOBAL.Vulkan.worldMaxZ },
        .groundY = GLOBAL.Vulkan.groundY,
        ._pad3 = { 0.0f, 0.0f, 0.0f },
    };

    const uint32_t localSizeX = (GLOBAL.Vulkan.computeLocalSizeX > 0u) ? GLOBAL.Vulkan.computeLocalSizeX : VULKAN_COMPUTE_LOCAL_SIZE;
    const uint32_t localSizeY = (GLOBAL.Vulkan.computeLocalSizeY > 0u) ? GLOBAL.Vulkan.computeLocalSizeY : VULKAN_COMPUTE_LOCAL_SIZE;

    Assert(localSizeX > 0u, "Compute local size X is zero");
    Assert(localSizeY > 0u, "Compute local size Y is zero");

    const uint32_t groupCountX = (pc.width + localSizeX - 1u) / localSizeX;
    const uint32_t groupCountY = (pc.height + localSizeY - 1u) / localSizeY;

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
