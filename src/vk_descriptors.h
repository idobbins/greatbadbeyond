#pragma once

#include <vulkan/vulkan.h>

typedef struct ComputeDS {
    VkImageView targetView;
    VkSampler targetSampler;
    VkBuffer sphereCR;
    VkBuffer sphereAlb;
    VkBuffer hitT;
    VkBuffer hitN;
    VkBuffer gridLevel0Meta;
    VkBuffer gridLevel0Counter;
    VkBuffer gridLevel0Indices;
    VkBuffer gridLevel1Meta;
    VkBuffer gridLevel1Counter;
    VkBuffer gridLevel1Indices;
    VkBuffer gridState;
} ComputeDS;

void VulkanCreateDescriptorInfra(void);
void VulkanDestroyDescriptorInfra(void);
void UpdateComputeDescriptorSet(const ComputeDS *resources);
