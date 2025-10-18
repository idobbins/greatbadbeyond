#include "runtime.h"
#include "rt_resources.h"
#include "vk_descriptors.h"

static void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VmaAllocation *alloc)
{
    Assert(buf != NULL, "Buffer handle pointer is null");
    Assert(alloc != NULL, "Buffer allocation pointer is null");
    Assert(GLOBAL.Vulkan.vma != NULL, "VMA allocator is not ready");

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkResult result = vmaCreateBuffer(GLOBAL.Vulkan.vma, &bufferInfo, &allocInfo, buf, alloc, NULL);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan buffer via VMA");
}

static void DestroyBuffer(VkBuffer *buf, VmaAllocation *alloc)
{
    if ((buf == NULL) || (alloc == NULL))
    {
        return;
    }

    if ((*buf != VK_NULL_HANDLE) && (GLOBAL.Vulkan.vma != NULL))
    {
        vmaDestroyBuffer(GLOBAL.Vulkan.vma, *buf, *alloc);
    }

    *buf = VK_NULL_HANDLE;
    *alloc = NULL;
}

static void CreateGradientResources(void)
{
    if (GLOBAL.Vulkan.gradientImage != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");
    Assert(GLOBAL.Vulkan.swapchain != VK_NULL_HANDLE, "Vulkan swapchain is not ready");
    Assert(GLOBAL.Vulkan.descriptorSet != VK_NULL_HANDLE, "Vulkan descriptor set is not ready");
    Assert(GLOBAL.Vulkan.vma != NULL, "VMA allocator is not ready");

    const VkExtent2D extent = GLOBAL.Vulkan.swapchainExtent;
    Assert(extent.width > 0 && extent.height > 0, "Vulkan swapchain extent is invalid");

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = extent.width,
            .height = extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkResult imageResult = vmaCreateImage(
        GLOBAL.Vulkan.vma,
        &imageInfo,
        &allocInfo,
        &GLOBAL.Vulkan.gradientImage,
        &GLOBAL.Vulkan.gradientAlloc,
        NULL);
    Assert(imageResult == VK_SUCCESS, "Failed to create Vulkan gradient image via VMA");

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = GLOBAL.Vulkan.gradientImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkResult viewResult = vkCreateImageView(GLOBAL.Vulkan.device, &viewInfo, NULL, &GLOBAL.Vulkan.gradientImageView);
    Assert(viewResult == VK_SUCCESS, "Failed to create Vulkan gradient image view");

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkResult samplerResult = vkCreateSampler(GLOBAL.Vulkan.device, &samplerInfo, NULL, &GLOBAL.Vulkan.gradientSampler);
    Assert(samplerResult == VK_SUCCESS, "Failed to create Vulkan gradient sampler");

    GLOBAL.Vulkan.gradientInitialized = false;

    LogInfo("Vulkan gradient image ready");
}

static void DestroyGradientResources(void)
{
    if (GLOBAL.Vulkan.gradientSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gradientSampler, NULL);
        GLOBAL.Vulkan.gradientSampler = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gradientImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gradientImageView, NULL);
        GLOBAL.Vulkan.gradientImageView = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gradientImage != VK_NULL_HANDLE)
    {
        if (GLOBAL.Vulkan.vma != NULL)
        {
            vmaDestroyImage(GLOBAL.Vulkan.vma, GLOBAL.Vulkan.gradientImage, GLOBAL.Vulkan.gradientAlloc);
        }
        GLOBAL.Vulkan.gradientImage = VK_NULL_HANDLE;
        GLOBAL.Vulkan.gradientAlloc = NULL;
    }

    GLOBAL.Vulkan.gradientInitialized = false;
}

void RtCreateSwapchainResources(void)
{
    Assert(GLOBAL.Vulkan.swapchain != VK_NULL_HANDLE, "Vulkan swapchain is not ready");

    VkExtent2D extent = GLOBAL.Vulkan.swapchainExtent;
    Assert(extent.width > 0 && extent.height > 0, "Vulkan swapchain extent is invalid");
    Assert(GLOBAL.Vulkan.vma != NULL, "VMA allocator is not ready");
    Assert(GLOBAL.Vulkan.descriptorSet != VK_NULL_HANDLE, "Vulkan descriptor set is not allocated");

    VkDeviceSize pixels = (VkDeviceSize)extent.width * (VkDeviceSize)extent.height;
    VkDeviceSize hitTSize = sizeof(float) * pixels;
    VkDeviceSize hitNSize = sizeof(float) * 4 * pixels;
    VkDeviceSize sphereSize = sizeof(float) * 4 * (VkDeviceSize)RT_MAX_SPHERES;
    VkDeviceSize accumSize = sizeof(float) * 4 * pixels;
    VkDeviceSize sppSize = sizeof(uint32_t) * pixels;
    VkDeviceSize epochSize = sizeof(uint32_t) * pixels;
    if (GLOBAL.Vulkan.rt.hitT == VK_NULL_HANDLE)
    {
        CreateBuffer(hitTSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &GLOBAL.Vulkan.rt.hitT, &GLOBAL.Vulkan.rt.hitTAlloc);
    }

    if (GLOBAL.Vulkan.rt.hitN == VK_NULL_HANDLE)
    {
        CreateBuffer(hitNSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &GLOBAL.Vulkan.rt.hitN, &GLOBAL.Vulkan.rt.hitNAlloc);
    }

    const VkBufferUsageFlags sphereUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (GLOBAL.Vulkan.rt.sphereCR == VK_NULL_HANDLE)
    {
        CreateBuffer(sphereSize, sphereUsage, &GLOBAL.Vulkan.rt.sphereCR, &GLOBAL.Vulkan.rt.sphereCRAlloc);
    }

    if (GLOBAL.Vulkan.rt.sphereAlb == VK_NULL_HANDLE)
    {
        CreateBuffer(sphereSize, sphereUsage, &GLOBAL.Vulkan.rt.sphereAlb, &GLOBAL.Vulkan.rt.sphereAlbAlloc);
    }

    if (GLOBAL.Vulkan.rt.accum == VK_NULL_HANDLE)
    {
        CreateBuffer(
            accumSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            &GLOBAL.Vulkan.rt.accum,
            &GLOBAL.Vulkan.rt.accumAlloc);
    }

    if (GLOBAL.Vulkan.rt.spp == VK_NULL_HANDLE)
    {
        CreateBuffer(
            sppSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            &GLOBAL.Vulkan.rt.spp,
            &GLOBAL.Vulkan.rt.sppAlloc);
    }

    if (GLOBAL.Vulkan.rt.epoch == VK_NULL_HANDLE)
    {
        CreateBuffer(
            epochSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            &GLOBAL.Vulkan.rt.epoch,
            &GLOBAL.Vulkan.rt.epochAlloc);
    }

    CreateGradientResources();

    ComputeDS resources = {
        .targetView = GLOBAL.Vulkan.gradientImageView,
        .targetSampler = GLOBAL.Vulkan.gradientSampler,
        .sphereCR = GLOBAL.Vulkan.rt.sphereCR,
        .sphereAlb = GLOBAL.Vulkan.rt.sphereAlb,
        .hitT = GLOBAL.Vulkan.rt.hitT,
        .hitN = GLOBAL.Vulkan.rt.hitN,
        .accum = GLOBAL.Vulkan.rt.accum,
        .spp = GLOBAL.Vulkan.rt.spp,
        .epoch = GLOBAL.Vulkan.rt.epoch,
    };

    UpdateComputeDescriptorSet(&resources);

    GLOBAL.Vulkan.sceneInitialized = false;
    GLOBAL.Vulkan.resetAccumulation = true;
    GLOBAL.Vulkan.accumulationEpoch = 0u;
}

void RtDestroySwapchainResources(void)
{
    DestroyGradientResources();
    DestroyBuffer(&GLOBAL.Vulkan.rt.hitT, &GLOBAL.Vulkan.rt.hitTAlloc);
    DestroyBuffer(&GLOBAL.Vulkan.rt.hitN, &GLOBAL.Vulkan.rt.hitNAlloc);
    DestroyBuffer(&GLOBAL.Vulkan.rt.sphereCR, &GLOBAL.Vulkan.rt.sphereCRAlloc);
    DestroyBuffer(&GLOBAL.Vulkan.rt.sphereAlb, &GLOBAL.Vulkan.rt.sphereAlbAlloc);
    DestroyBuffer(&GLOBAL.Vulkan.rt.accum, &GLOBAL.Vulkan.rt.accumAlloc);
    DestroyBuffer(&GLOBAL.Vulkan.rt.spp, &GLOBAL.Vulkan.rt.sppAlloc);
    DestroyBuffer(&GLOBAL.Vulkan.rt.epoch, &GLOBAL.Vulkan.rt.epochAlloc);
    GLOBAL.Vulkan.sceneInitialized = false;
    GLOBAL.Vulkan.accumulationEpoch = 0u;
}
