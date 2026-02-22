#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#endif
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <stdint.h>

#include "gradient_comp_spv.h"
#include "platform.h"

#define FRAME_COUNT 3u
#define COMPUTE_TILE_SIZE 8u

static const char* APPLICATION_NAME = "greatbadbeyond";

#if defined(_WIN32)
static const char* const INSTANCE_EXTS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
static const VkInstanceCreateFlags INSTANCE_FLAGS = 0u;
static const char* const DEVICE_EXTS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
#elif defined(__APPLE__)
static const char* const INSTANCE_EXTS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
static const VkInstanceCreateFlags INSTANCE_FLAGS = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
static const char* const DEVICE_EXTS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
};
#else
#error Unsupported platform
#endif

static VkInstance instance = VK_NULL_HANDLE;
static VkSurfaceKHR surface = VK_NULL_HANDLE;
static VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
static VkDevice device = VK_NULL_HANDLE;
static VkQueue queue = VK_NULL_HANDLE;
static VkSwapchainKHR swapchain = VK_NULL_HANDLE;
static VkExtent2D swapExtent = {0u, 0u};
static VkImage swapImages[FRAME_COUNT];
static VkImageView swapImageViews[FRAME_COUNT];
static VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet descriptorSets[FRAME_COUNT];
static VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
static VkPipeline pipeline = VK_NULL_HANDLE;
static VkCommandPool commandPool = VK_NULL_HANDLE;
static VkCommandBuffer commandBuffers[FRAME_COUNT];
static VkSemaphore imageAvailableSemaphores[FRAME_COUNT];
static VkSemaphore renderFinishedSemaphores[FRAME_COUNT];
static VkFence inFlightFences[FRAME_COUNT];

static void recordCommandBuffer(VkCommandBuffer cb, VkDescriptorSet ds, VkImage image)
{
    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1u,
        .layerCount = 1u
    };

    vkBeginCommandBuffer(cb, &(VkCommandBufferBeginInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    });

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, NULL, 0u, NULL, 1u,
    &(VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range
     });

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0u, 1u, &ds, 0u, NULL);
    vkCmdDispatch(cb, (swapExtent.width + COMPUTE_TILE_SIZE - 1u) / COMPUTE_TILE_SIZE,
                  (swapExtent.height + COMPUTE_TILE_SIZE - 1u) / COMPUTE_TILE_SIZE, 1u);

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u, 0u, NULL, 0u, NULL, 1u,
    &(VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range
    });

    vkEndCommandBuffer(cb);
}

int main(void)
{
    gbbInitWindow(1280u, 720u, APPLICATION_NAME);

    vkCreateInstance(&(VkInstanceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = INSTANCE_FLAGS,
        .pApplicationInfo = &(VkApplicationInfo){
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = APPLICATION_NAME,
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = APPLICATION_NAME,
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = VK_API_VERSION_1_3,
        },
        .enabledExtensionCount = (uint32_t)(sizeof(INSTANCE_EXTS) / sizeof(*INSTANCE_EXTS)),
        .ppEnabledExtensionNames = INSTANCE_EXTS,
    }, NULL, &instance);

#if defined(_WIN32)
    vkCreateWin32SurfaceKHR(instance, &(VkWin32SurfaceCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandleA(NULL),
        .hwnd = (HWND)window_handle,
    }, NULL, &surface);
#elif defined(__APPLE__)
    vkCreateMetalSurfaceEXT(instance, &(VkMetalSurfaceCreateInfoEXT){
        .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pLayer = surface_layer,
    }, NULL, &surface);
#endif

    uint32_t deviceCount = 1u;
    vkEnumeratePhysicalDevices(instance, &deviceCount, &physicalDevice);

    float priority = 1.0f;
    vkCreateDevice(physicalDevice, &(VkDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &(VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0u,
            .queueCount = 1u,
            .pQueuePriorities = &priority,
        },
        .enabledExtensionCount = (uint32_t)(sizeof(DEVICE_EXTS) / sizeof(*DEVICE_EXTS)),
        .ppEnabledExtensionNames = DEVICE_EXTS,
    }, NULL, &device);

    vkGetDeviceQueue(device, 0u, 0u, &queue);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
    swapExtent = caps.currentExtent;

    vkCreateSwapchainKHR(device, &(VkSwapchainCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = FRAME_COUNT,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = swapExtent,
        .imageArrayLayers = 1u,
        .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    }, NULL, &swapchain);

    uint32_t swapImageCount = FRAME_COUNT;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);

    for (uint32_t i = 0u; i < FRAME_COUNT; i++)
    {
        vkCreateImageView(device, &(VkImageViewCreateInfo){
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1u,
                .layerCount = 1u,
            },
        }, NULL, &swapImageViews[i]);
    }

    vkCreateDescriptorSetLayout(device, &(VkDescriptorSetLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1u,
        .pBindings = &(VkDescriptorSetLayoutBinding){
            .binding = 0u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }, NULL, &descriptorSetLayout);

    vkCreateDescriptorPool(device, &(VkDescriptorPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FRAME_COUNT,
        .poolSizeCount = 1u,
        .pPoolSizes = &(VkDescriptorPoolSize){
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = FRAME_COUNT,
        },
    }, NULL, &descriptorPool);

    VkDescriptorSetLayout setLayouts[FRAME_COUNT];
    for (uint32_t i = 0u; i < FRAME_COUNT; i++) setLayouts[i] = descriptorSetLayout;
    vkAllocateDescriptorSets(device, &(VkDescriptorSetAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = FRAME_COUNT,
        .pSetLayouts = setLayouts,
    }, descriptorSets);

    vkCreatePipelineLayout(device, &(VkPipelineLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1u,
        .pSetLayouts = &descriptorSetLayout,
    }, NULL, &pipelineLayout);

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = gradientCompSpv_size,
        .pCode = gradientCompSpv,
     }, NULL, &shaderModule);

    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u, &(VkComputePipelineCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main",
        },
        .layout = pipelineLayout,
        .basePipelineIndex = -1,
    }, NULL, &pipeline);

    vkDestroyShaderModule(device, shaderModule, NULL);

    vkCreateCommandPool(device, &(VkCommandPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    }, NULL, &commandPool);

    vkAllocateCommandBuffers(device, &(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = FRAME_COUNT,
    }, commandBuffers);

    for (uint32_t i = 0u; i < FRAME_COUNT; i++)
    {
        VkDescriptorImageInfo imageInfo = {.imageView = swapImageViews[i], .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        vkUpdateDescriptorSets(device, 1u, &(VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSets[i],
            .dstBinding = 0u,
            .descriptorCount = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &imageInfo,
        }, 0u, NULL);

        recordCommandBuffer(commandBuffers[i], descriptorSets[i], swapImages[i]);
        vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        }, NULL, &imageAvailableSemaphores[i]);

        vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        }, NULL, &renderFinishedSemaphores[i]);

        vkCreateFence(device, &(VkFenceCreateInfo){
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT
        }, NULL, &inFlightFences[i]);
    }

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    uint32_t frameIndex = 0u;
    while (gbbPumpEventsOnce() == 0)
    {
        uint32_t fi = frameIndex;
        vkWaitForFences(device, 1u, &inFlightFences[fi], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1u, &inFlightFences[fi]);

        uint32_t imageIndex = 0u;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[fi], VK_NULL_HANDLE, &imageIndex);
        vkQueueSubmit(queue, 1u, &(VkSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &imageAvailableSemaphores[fi],
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1u,
            .pCommandBuffers = &commandBuffers[imageIndex],
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &renderFinishedSemaphores[fi],
        }, inFlightFences[fi]);

        vkQueuePresentKHR(queue, &(VkPresentInfoKHR){
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &renderFinishedSemaphores[fi],
            .swapchainCount = 1u,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
        });

        frameIndex = (frameIndex + 1u) % FRAME_COUNT;
    }

    vkDeviceWaitIdle(device);

    for (uint32_t i = 0u; i < FRAME_COUNT; i++)
    {
        vkDestroyFence(device, inFlightFences[i], NULL);
        vkDestroySemaphore(device, renderFinishedSemaphores[i], NULL);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], NULL);
        vkDestroyImageView(device, swapImageViews[i], NULL);
    }

    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    gbbShutdownWindow();
    return 0;
}
