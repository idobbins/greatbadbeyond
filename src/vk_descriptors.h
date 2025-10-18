#pragma once

#include <vulkan/vulkan.h>

typedef struct ComputeDS {
    VkImageView targetView;
    VkSampler targetSampler;
    VkBuffer sphereCR;
    VkBuffer sphereAlb;
    VkBuffer hitT;
    VkBuffer hitN;
    VkBuffer accum;
    VkBuffer spp;
    VkBuffer epoch;
} ComputeDS;

void VulkanCreateDescriptorInfra(void);
void VulkanDestroyDescriptorInfra(void);
void UpdateComputeDescriptorSet(const ComputeDS *resources);
