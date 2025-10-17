#include "runtime.h"
#include "vk_swapchain.h"
#include "rt_resources.h"
#include "rt_frame.h"
#include "vk_pipelines.h"

#include <string.h>

typedef struct VulkanSwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR formats[VULKAN_MAX_SURFACE_FORMATS];
    VkPresentModeKHR presentModes[VULKAN_MAX_PRESENT_MODES];
    uint32_t formatCount;
    uint32_t presentModeCount;
} VulkanSwapchainSupport;

static void VulkanRefreshReadyState(void)
{
    GLOBAL.Vulkan.ready =
        (GLOBAL.Vulkan.instance != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.surface != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.device != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.swapchain != VK_NULL_HANDLE);
}

static void VulkanQuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface, VulkanSwapchainSupport *support)
{
    Assert(support != NULL, "Vulkan swapchain support pointer is null");

    memset(support, 0, sizeof(*support));

    VkResult capabilitiesResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support->capabilities);
    Assert(capabilitiesResult == VK_SUCCESS, "Failed to query Vulkan surface capabilities");

    uint32_t formatCount = 0;
    VkResult formatResult = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, NULL);
    Assert(formatResult == VK_SUCCESS, "Failed to query Vulkan surface formats");
    if (formatCount > ARRAY_SIZE(support->formats))
    {
        LogWarn("Truncating Vulkan surface formats (%u > %u)", formatCount, (uint32_t)ARRAY_SIZE(support->formats));
        formatCount = (uint32_t)ARRAY_SIZE(support->formats);
    }
    if (formatCount > 0)
    {
        support->formatCount = formatCount;
        formatResult = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &support->formatCount, support->formats);
        Assert(formatResult == VK_SUCCESS, "Failed to enumerate Vulkan surface formats");
    }

    uint32_t presentModeCount = 0;
    VkResult presentModeResult = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, NULL);
    Assert(presentModeResult == VK_SUCCESS, "Failed to query Vulkan surface present modes");
    if (presentModeCount > ARRAY_SIZE(support->presentModes))
    {
        LogWarn("Truncating Vulkan present modes (%u > %u)", presentModeCount, (uint32_t)ARRAY_SIZE(support->presentModes));
        presentModeCount = (uint32_t)ARRAY_SIZE(support->presentModes);
    }
    if (presentModeCount > 0)
    {
        support->presentModeCount = presentModeCount;
        presentModeResult = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &support->presentModeCount, support->presentModes);
        Assert(presentModeResult == VK_SUCCESS, "Failed to enumerate Vulkan present modes");
    }
}

static VkSurfaceFormatKHR VulkanChooseSurfaceFormat(const VkSurfaceFormatKHR *formats, uint32_t count)
{
    Assert(count > 0, "No Vulkan surface formats available");

    for (uint32_t index = 0; index < count; index++)
    {
        if (formats[index].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return formats[index];
        }
    }

    return formats[0];
}

static VkPresentModeKHR VulkanChoosePresentMode(const VkPresentModeKHR *presentModes, uint32_t count)
{
    Assert(count > 0, "No Vulkan present modes available");

    bool hasFifo = false;
    bool hasMailbox = false;
    bool hasImmediate = false;

    for (uint32_t index = 0; index < count; index++)
    {
        if (presentModes[index] == VK_PRESENT_MODE_FIFO_KHR)
        {
            hasFifo = true;
        }
        else if (presentModes[index] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            hasMailbox = true;
        }
        else if (presentModes[index] == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            hasImmediate = true;
        }
    }

#if defined(__APPLE__)
    if (hasFifo)
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
#else
    if (hasFifo)
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    if (hasMailbox)
    {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (hasImmediate)
    {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
#endif

    return presentModes[0];
}

static VkExtent2D VulkanChooseExtent(const VkSurfaceCapabilitiesKHR *capabilities)
{
    if (capabilities->currentExtent.width != UINT32_MAX)
    {
        return capabilities->currentExtent;
    }

    int32_t width = 0;
    int32_t height = 0;
    glfwGetFramebufferSize(GLOBAL.Window.window, &width, &height);
    Assert(width > 0 && height > 0, "Vulkan framebuffer has invalid size");

    VkExtent2D extent = {
        .width = (uint32_t)width,
        .height = (uint32_t)height,
    };

    if (extent.width < capabilities->minImageExtent.width)
    {
        extent.width = capabilities->minImageExtent.width;
    }
    else if (extent.width > capabilities->maxImageExtent.width)
    {
        extent.width = capabilities->maxImageExtent.width;
    }

    if (extent.height < capabilities->minImageExtent.height)
    {
        extent.height = capabilities->minImageExtent.height;
    }
    else if (extent.height > capabilities->maxImageExtent.height)
    {
        extent.height = capabilities->maxImageExtent.height;
    }

    return extent;
}

static VkCompositeAlphaFlagBitsKHR VulkanChooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
{
    const VkCompositeAlphaFlagBitsKHR preferred[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };

    for (uint32_t index = 0; index < ARRAY_SIZE(preferred); index++)
    {
        if ((supported & preferred[index]) != 0)
        {
            return preferred[index];
        }
    }

    for (uint32_t bit = 0; bit < 32; bit++)
    {
        VkCompositeAlphaFlagBitsKHR alpha = (VkCompositeAlphaFlagBitsKHR)(1u << bit);
        if ((supported & alpha) != 0)
        {
            return alpha;
        }
    }

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static void VulkanDestroySwapchainSemaphores(void)
{
    for (uint32_t index = 0; index < VULKAN_MAX_SWAPCHAIN_IMAGES; index++)
    {
        if (GLOBAL.Vulkan.renderFinishedSemaphores[index] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(GLOBAL.Vulkan.device, GLOBAL.Vulkan.renderFinishedSemaphores[index], NULL);
            GLOBAL.Vulkan.renderFinishedSemaphores[index] = VK_NULL_HANDLE;
        }
    }
}

static void VulkanCreateSwapchainSemaphores(void)
{
    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");
    Assert(GLOBAL.Vulkan.swapchainImageCount <= VULKAN_MAX_SWAPCHAIN_IMAGES, "Vulkan swapchain image count out of range");

    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    for (uint32_t index = 0; index < GLOBAL.Vulkan.swapchainImageCount; index++)
    {
        if (GLOBAL.Vulkan.renderFinishedSemaphores[index] == VK_NULL_HANDLE)
        {
            VkResult result = vkCreateSemaphore(GLOBAL.Vulkan.device, &semaphoreInfo, NULL, &GLOBAL.Vulkan.renderFinishedSemaphores[index]);
            Assert(result == VK_SUCCESS, "Failed to create Vulkan render-finished semaphore");
        }
    }

    for (uint32_t index = GLOBAL.Vulkan.swapchainImageCount; index < VULKAN_MAX_SWAPCHAIN_IMAGES; index++)
    {
        if (GLOBAL.Vulkan.renderFinishedSemaphores[index] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(GLOBAL.Vulkan.device, GLOBAL.Vulkan.renderFinishedSemaphores[index], NULL);
            GLOBAL.Vulkan.renderFinishedSemaphores[index] = VK_NULL_HANDLE;
        }
    }
}

void CreateSwapchain(void)
{
    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");
    Assert(GLOBAL.Vulkan.surface != VK_NULL_HANDLE, "Vulkan surface is not created");
    Assert(GLOBAL.Window.ready, "Window is not created");

    VulkanSwapchainSupport support;
    VulkanQuerySwapchainSupport(GLOBAL.Vulkan.physicalDevice, GLOBAL.Vulkan.surface, &support);
    Assert(support.formatCount > 0, "No Vulkan surface formats available");
    Assert(support.presentModeCount > 0, "No Vulkan present modes available");

    VkSurfaceFormatKHR surfaceFormat = VulkanChooseSurfaceFormat(support.formats, support.formatCount);
    VkPresentModeKHR presentMode = VulkanChoosePresentMode(support.presentModes, support.presentModeCount);
    VkExtent2D extent = VulkanChooseExtent(&support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if ((support.capabilities.maxImageCount > 0) && (imageCount > support.capabilities.maxImageCount))
    {
        imageCount = support.capabilities.maxImageCount;
    }
    Assert(imageCount <= VULKAN_MAX_SWAPCHAIN_IMAGES, "Vulkan swapchain image count exceeds capacity");

    VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if ((support.capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) == 0)
    {
        transform = support.capabilities.currentTransform;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VulkanChooseCompositeAlpha(support.capabilities.supportedCompositeAlpha);

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = GLOBAL.Vulkan.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = transform,
        .compositeAlpha = compositeAlpha,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    VkResult swapchainResult = vkCreateSwapchainKHR(GLOBAL.Vulkan.device, &createInfo, NULL, &GLOBAL.Vulkan.swapchain);
    Assert(swapchainResult == VK_SUCCESS, "Failed to create Vulkan swapchain");

    uint32_t retrievedImageCount = 0;
    VkResult imageCountResult = vkGetSwapchainImagesKHR(GLOBAL.Vulkan.device, GLOBAL.Vulkan.swapchain, &retrievedImageCount, NULL);
    Assert(imageCountResult == VK_SUCCESS, "Failed to query Vulkan swapchain images");
    Assert(retrievedImageCount <= VULKAN_MAX_SWAPCHAIN_IMAGES, "Vulkan swapchain images exceed capacity");
    Assert(retrievedImageCount > 0, "Vulkan swapchain returned no images");
    VkResult imagesResult = vkGetSwapchainImagesKHR(GLOBAL.Vulkan.device, GLOBAL.Vulkan.swapchain, &retrievedImageCount, GLOBAL.Vulkan.swapchainImages);
    Assert(imagesResult == VK_SUCCESS, "Failed to enumerate Vulkan swapchain images");
    GLOBAL.Vulkan.swapchainImageCount = retrievedImageCount;

    memset(GLOBAL.Vulkan.swapchainImageViews, 0, sizeof(GLOBAL.Vulkan.swapchainImageViews));

    for (uint32_t index = 0; index < GLOBAL.Vulkan.swapchainImageCount; index++)
    {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = GLOBAL.Vulkan.swapchainImages[index],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surfaceFormat.format,
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

        VkResult viewResult = vkCreateImageView(GLOBAL.Vulkan.device, &viewInfo, NULL, &GLOBAL.Vulkan.swapchainImageViews[index]);
        Assert(viewResult == VK_SUCCESS, "Failed to create Vulkan swapchain image view");
    }

    GLOBAL.Vulkan.swapchainImageFormat = surfaceFormat.format;
    GLOBAL.Vulkan.swapchainExtent = extent;

    VulkanCreateSwapchainSemaphores();
    RtCreateSwapchainResources();
    CreateBlitPipeline();
    VulkanRefreshReadyState();

    LogInfo("Vulkan swapchain ready: %u images (%ux%u)", GLOBAL.Vulkan.swapchainImageCount, extent.width, extent.height);
}

void DestroySwapchain(void)
{
    RtDestroySwapchainResources();
    DestroyBlitPipeline();

    if (GLOBAL.Vulkan.swapchain == VK_NULL_HANDLE)
    {
        return;
    }

    for (uint32_t index = 0; index < GLOBAL.Vulkan.swapchainImageCount; index++)
    {
        if (GLOBAL.Vulkan.swapchainImageViews[index] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(GLOBAL.Vulkan.device, GLOBAL.Vulkan.swapchainImageViews[index], NULL);
            GLOBAL.Vulkan.swapchainImageViews[index] = VK_NULL_HANDLE;
        }
        GLOBAL.Vulkan.swapchainImages[index] = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.swapchainImageCount = 0;
    memset(GLOBAL.Vulkan.swapchainImages, 0, sizeof(GLOBAL.Vulkan.swapchainImages));
    memset(GLOBAL.Vulkan.swapchainImageViews, 0, sizeof(GLOBAL.Vulkan.swapchainImageViews));

    VulkanDestroySwapchainSemaphores();

    vkDestroySwapchainKHR(GLOBAL.Vulkan.device, GLOBAL.Vulkan.swapchain, NULL);
    GLOBAL.Vulkan.swapchain = VK_NULL_HANDLE;

    GLOBAL.Vulkan.swapchainExtent.width = 0;
    GLOBAL.Vulkan.swapchainExtent.height = 0;
    GLOBAL.Vulkan.swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VulkanRefreshReadyState();

    LogInfo("Vulkan swapchain destroyed");
}

void RecreateSwapchain(void)
{
    if ((GLOBAL.Vulkan.device == VK_NULL_HANDLE) || (GLOBAL.Vulkan.surface == VK_NULL_HANDLE))
    {
        return;
    }

    int32_t width = 0;
    int32_t height = 0;
    glfwGetFramebufferSize(GLOBAL.Window.window, &width, &height);
    if (width <= 0 || height <= 0)
    {
        return;
    }

    LogInfo("Recreating Vulkan swapchain");

    vkDeviceWaitIdle(GLOBAL.Vulkan.device);
    DestroySwapchain();
    CreateSwapchain();
}

void VulkanDrawFrame(void)
{
    if (!GLOBAL.Vulkan.ready)
    {
        return;
    }

    const VkExtent2D extent = GLOBAL.Vulkan.swapchainExtent;
    if (extent.width == 0 || extent.height == 0)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.commandBuffer != VK_NULL_HANDLE, "Vulkan command buffer is not ready");
    Assert(GLOBAL.Vulkan.imageAvailableSemaphore != VK_NULL_HANDLE, "Vulkan synchronization objects are not ready");
    Assert(GLOBAL.Vulkan.frameFence != VK_NULL_HANDLE, "Vulkan frame fence is not ready");

    VkResult fenceResult = vkWaitForFences(GLOBAL.Vulkan.device, 1, &GLOBAL.Vulkan.frameFence, VK_TRUE, UINT64_MAX);
    Assert(fenceResult == VK_SUCCESS, "Failed to wait for Vulkan frame fence");

    fenceResult = vkResetFences(GLOBAL.Vulkan.device, 1, &GLOBAL.Vulkan.frameFence);
    Assert(fenceResult == VK_SUCCESS, "Failed to reset Vulkan frame fence");

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        GLOBAL.Vulkan.device,
        GLOBAL.Vulkan.swapchain,
        UINT64_MAX,
        GLOBAL.Vulkan.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return;
    }

    Assert((acquireResult == VK_SUCCESS) || (acquireResult == VK_SUBOPTIMAL_KHR), "Failed to acquire Vulkan swapchain image");

    RtRecordFrame(imageIndex, extent);

    VkSemaphore renderFinishedSemaphore = GLOBAL.Vulkan.renderFinishedSemaphores[imageIndex];
    Assert(renderFinishedSemaphore != VK_NULL_HANDLE, "Vulkan render-finished semaphore is not ready");

    VkSemaphore waitSemaphores[] = { GLOBAL.Vulkan.imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = ARRAY_SIZE(waitSemaphores),
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &GLOBAL.Vulkan.commandBuffer,
        .signalSemaphoreCount = ARRAY_SIZE(signalSemaphores),
        .pSignalSemaphores = signalSemaphores,
    };

    VkResult submitResult = vkQueueSubmit(GLOBAL.Vulkan.queue, 1, &submitInfo, GLOBAL.Vulkan.frameFence);
    Assert(submitResult == VK_SUCCESS, "Failed to submit Vulkan frame commands");

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1,
        .pSwapchains = &GLOBAL.Vulkan.swapchain,
        .pImageIndices = &imageIndex,
        .waitSemaphoreCount = ARRAY_SIZE(signalSemaphores),
        .pWaitSemaphores = signalSemaphores,
    };

    VkResult presentResult = vkQueuePresentKHR(GLOBAL.Vulkan.queue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        RecreateSwapchain();
        return;
    }

    Assert(presentResult == VK_SUCCESS, "Failed to present Vulkan swapchain image");
    (void)imageIndex;
}
