#include "runtime.h"
#include "vk_descriptors.h"
#include "shader_bindings.h"

#include <string.h>

void VulkanCreateDescriptorInfra(void)
{
    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");

    if (GLOBAL.Vulkan.descriptorSetLayout == VK_NULL_HANDLE)
    {
        VkDescriptorSetLayoutBinding bindings[13] = {
            {
                .binding = B_TARGET,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_SAMPLER,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = B_SPHERE_CR,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_SPHERE_ALB,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_HIT_T,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_HIT_N,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_GRID_L0_META,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_GRID_L0_COUNTER,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_GRID_L0_INDICES,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_GRID_L1_META,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_GRID_L1_COUNTER,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_GRID_L1_INDICES,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_GRID_STATE,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = ARRAY_SIZE(bindings),
            .pBindings = bindings,
        };

        VkResult layoutResult = vkCreateDescriptorSetLayout(GLOBAL.Vulkan.device, &layoutInfo, NULL, &GLOBAL.Vulkan.descriptorSetLayout);
        Assert(layoutResult == VK_SUCCESS, "Failed to create Vulkan descriptor set layout");

        LogInfo("Vulkan descriptor set layout ready");
    }

    if (GLOBAL.Vulkan.descriptorPool == VK_NULL_HANDLE)
    {
        VkDescriptorPoolSize poolSizes[13] = {
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 },
        };

        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1,
            .poolSizeCount = ARRAY_SIZE(poolSizes),
            .pPoolSizes = poolSizes,
        };

        VkResult poolResult = vkCreateDescriptorPool(GLOBAL.Vulkan.device, &poolInfo, NULL, &GLOBAL.Vulkan.descriptorPool);
        Assert(poolResult == VK_SUCCESS, "Failed to create Vulkan descriptor pool");

        LogInfo("Vulkan descriptor pool ready");
    }

    if (GLOBAL.Vulkan.descriptorSet == VK_NULL_HANDLE)
    {
        Assert(GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE, "Vulkan descriptor set layout is not ready");

        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = GLOBAL.Vulkan.descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
        };

        VkResult allocResult = vkAllocateDescriptorSets(GLOBAL.Vulkan.device, &allocInfo, &GLOBAL.Vulkan.descriptorSet);
        Assert(allocResult == VK_SUCCESS, "Failed to allocate Vulkan descriptor set");

        LogInfo("Vulkan descriptor set ready");
    }
}

void VulkanDestroyDescriptorInfra(void)
{
    if (GLOBAL.Vulkan.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(GLOBAL.Vulkan.device, GLOBAL.Vulkan.descriptorPool, NULL);
        GLOBAL.Vulkan.descriptorPool = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.descriptorSet = VK_NULL_HANDLE;

    if (GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(GLOBAL.Vulkan.device, GLOBAL.Vulkan.descriptorSetLayout, NULL);
        GLOBAL.Vulkan.descriptorSetLayout = VK_NULL_HANDLE;
    }
}

void UpdateComputeDescriptorSet(const ComputeDS *resources)
{
    Assert(resources != NULL, "Descriptor resource data is null");
    Assert(GLOBAL.Vulkan.descriptorSet != VK_NULL_HANDLE, "Vulkan descriptor set is not allocated");
    Assert(resources->targetView != VK_NULL_HANDLE, "Target image view is not ready");
    Assert(resources->targetSampler != VK_NULL_HANDLE, "Target sampler is not ready");
    Assert(resources->sphereCR != VK_NULL_HANDLE, "Sphere center-radius buffer is not ready");
    Assert(resources->sphereAlb != VK_NULL_HANDLE, "Sphere albedo buffer is not ready");
    Assert(resources->hitT != VK_NULL_HANDLE, "Hit distance buffer is not ready");
    Assert(resources->hitN != VK_NULL_HANDLE, "Hit normal buffer is not ready");
    Assert(resources->gridLevel0Meta != VK_NULL_HANDLE, "Grid level0 meta buffer is not ready");
    Assert(resources->gridLevel0Counter != VK_NULL_HANDLE, "Grid level0 counter buffer is not ready");
    Assert(resources->gridLevel0Indices != VK_NULL_HANDLE, "Grid level0 index buffer is not ready");
    Assert(resources->gridLevel1Meta != VK_NULL_HANDLE, "Grid level1 meta buffer is not ready");
    Assert(resources->gridLevel1Counter != VK_NULL_HANDLE, "Grid level1 counter buffer is not ready");
    Assert(resources->gridLevel1Indices != VK_NULL_HANDLE, "Grid level1 index buffer is not ready");
    Assert(resources->gridState != VK_NULL_HANDLE, "Grid state buffer is not ready");

    VkDescriptorImageInfo storage = {
        .imageView = resources->targetView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    VkDescriptorImageInfo sampler = {
        .sampler = resources->targetSampler,
        .imageView = resources->targetView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkDescriptorBufferInfo centers = {
        .buffer = resources->sphereCR,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo albedo = {
        .buffer = resources->sphereAlb,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo hitT = {
        .buffer = resources->hitT,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo hitN = {
        .buffer = resources->hitN,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo gridL0Meta = {
        .buffer = resources->gridLevel0Meta,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo gridL0Counter = {
        .buffer = resources->gridLevel0Counter,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo gridL0Indices = {
        .buffer = resources->gridLevel0Indices,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo gridL1Meta = {
        .buffer = resources->gridLevel1Meta,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo gridL1Counter = {
        .buffer = resources->gridLevel1Counter,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo gridL1Indices = {
        .buffer = resources->gridLevel1Indices,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo gridState = {
        .buffer = resources->gridState,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet writes[13] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_TARGET,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &storage,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_SAMPLER,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &sampler,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_SPHERE_CR,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &centers,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_SPHERE_ALB,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &albedo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_HIT_T,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &hitT,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_HIT_N,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &hitN,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_GRID_L0_META,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &gridL0Meta,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_GRID_L0_COUNTER,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &gridL0Counter,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_GRID_L0_INDICES,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &gridL0Indices,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_GRID_L1_META,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &gridL1Meta,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_GRID_L1_COUNTER,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &gridL1Counter,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_GRID_L1_INDICES,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &gridL1Indices,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_GRID_STATE,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &gridState,
        },
    };

    vkUpdateDescriptorSets(GLOBAL.Vulkan.device, (uint32_t)ARRAY_SIZE(writes), writes, 0, NULL);
}
