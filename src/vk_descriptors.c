#include "runtime.h"
#include "vk_descriptors.h"
#include "shader_bindings.h"

#include <string.h>

void VulkanCreateDescriptorInfra(void)
{
    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");

    if (GLOBAL.Vulkan.descriptorSetLayout == VK_NULL_HANDLE)
    {
        VkDescriptorSetLayoutBinding bindings[9] = {
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
                .binding = B_ACCUM,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_SPP,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = B_EPOCH,
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
        VkDescriptorPoolSize poolSizes[9] = {
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1 },
            { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
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
    Assert(resources->accum != VK_NULL_HANDLE, "Accumulation buffer is not ready");
    Assert(resources->spp != VK_NULL_HANDLE, "Sample count buffer is not ready");
    Assert(resources->epoch != VK_NULL_HANDLE, "Accumulation epoch buffer is not ready");

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

    VkDescriptorBufferInfo accum = {
        .buffer = resources->accum,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo spp = {
        .buffer = resources->spp,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkDescriptorBufferInfo epoch = {
        .buffer = resources->epoch,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };

    VkWriteDescriptorSet writes[9] = {
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
            .dstBinding = B_ACCUM,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &accum,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_SPP,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &spp,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = B_EPOCH,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &epoch,
        },
    };

    vkUpdateDescriptorSets(GLOBAL.Vulkan.device, (uint32_t)ARRAY_SIZE(writes), writes, 0, NULL);
}
