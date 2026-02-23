#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#endif
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <math.h>

#include "gradient_comp_spv.h"
#include "platform.h"

#define MAX_SWAP_IMAGES 3u
#define FRAMES_IN_FLIGHT 1u
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
    "VK_KHR_portability_subset",
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
static VkImage swapImages[MAX_SWAP_IMAGES];
static VkImageView swapImageViews[MAX_SWAP_IMAGES];
static VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet descriptorSets[MAX_SWAP_IMAGES];
static VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
static VkPipeline pipeline = VK_NULL_HANDLE;
static VkCommandPool commandPool = VK_NULL_HANDLE;
static VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
static VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
static VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
static VkFence inFlightFence = VK_NULL_HANDLE;

typedef struct CameraPushConstants {
    float origin[4];
    float forward_fov[4];
} CameraPushConstants;

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
    uint32_t swapchainMinImageCount = 2u;
    if (swapchainMinImageCount < caps.minImageCount) swapchainMinImageCount = caps.minImageCount;
    if ((caps.maxImageCount != 0u) && (swapchainMinImageCount > caps.maxImageCount)) swapchainMinImageCount = caps.maxImageCount;

    vkCreateSwapchainKHR(device, &(VkSwapchainCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = swapchainMinImageCount,
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

    uint32_t swapImageCount = 0u;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, NULL);

    if (swapImageCount > MAX_SWAP_IMAGES) return 1;

    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);

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
        .maxSets = MAX_SWAP_IMAGES,
        .poolSizeCount = 1u,
        .pPoolSizes = &(VkDescriptorPoolSize){
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = MAX_SWAP_IMAGES,
        },
    }, NULL, &descriptorPool);

    VkDescriptorSetLayout setLayouts[MAX_SWAP_IMAGES] = {
        descriptorSetLayout,
        descriptorSetLayout,
        descriptorSetLayout,
    };
    vkAllocateDescriptorSets(device, &(VkDescriptorSetAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = MAX_SWAP_IMAGES,
        .pSetLayouts = setLayouts,
    }, descriptorSets);

    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0u,
        .size = sizeof(CameraPushConstants),
    };
    vkCreatePipelineLayout(device, &(VkPipelineLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1u,
        .pSetLayouts = &descriptorSetLayout,
        .pushConstantRangeCount = 1u,
        .pPushConstantRanges = &pushConstantRange,
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
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0u,
    }, NULL, &commandPool);

    vkAllocateCommandBuffers(device, &(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    }, &commandBuffer);

    VkImageSubresourceRange imageRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1u,
        .layerCount = 1u
    };

    for (uint32_t i = 0u; i < swapImageCount; i++)
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

        vkUpdateDescriptorSets(device, 1u, &(VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSets[i],
            .dstBinding = 0u,
            .descriptorCount = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &(VkDescriptorImageInfo){
                .imageView = swapImageViews[i],
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL
            },
        }, 0u, NULL);
    }

    vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    }, NULL, &imageAvailableSemaphore);

    vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    }, NULL, &renderFinishedSemaphore);

    vkCreateFence(device, &(VkFenceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT
    }, NULL, &inFlightFence);

    float cameraPosition[3] = {0.0f, 0.0f, -2.0f};
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    const float cameraFov = 1.0471975512f;

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    uint64_t last_time = gbbGetTimeNs();
    while (gbbPumpEventsOnce() == 0)
    {
        uint64_t now_time = gbbGetTimeNs();
        float delta_time = (float)(now_time - last_time) * 1e-9f;
        last_time = now_time;

        vkWaitForFences(device, 1u, &inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1u, &inFlightFence);

        uint32_t imageIndex = 0u;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        float mouseDeltaX = 0.0f;
        float mouseDeltaY = 0.0f;
        gbbConsumeMouseDelta(&mouseDeltaX, &mouseDeltaY);
        const float lookSensitivity = 0.0025f;
        const float base_speed = 3.0f;
        const float speed_multiplier = (float)gbbIsKeyDown(GBB_KEY_SHIFT) * 2.0f + 1.0f;
        const float move_speed = base_speed * speed_multiplier;
        cameraYaw += mouseDeltaX * lookSensitivity;
        cameraPitch -= mouseDeltaY * lookSensitivity;
        cameraPitch = fmaxf(-1.553343f, fminf(1.553343f, cameraPitch));

        const float pitchCos = cosf(cameraPitch);
        const float forwardX = sinf(cameraYaw) * pitchCos;
        const float forwardY = sinf(cameraPitch);
        const float forwardZ = cosf(cameraYaw) * pitchCos;

        float rightX = forwardZ;
        float rightZ = -forwardX;
        const float rightLen = sqrtf(rightX * rightX + rightZ * rightZ);
        const float inv_rightLen = 1.0f / fmaxf(rightLen, 1e-6f);
        rightX *= inv_rightLen;
        rightZ *= inv_rightLen;

        const float moveForward = (float)gbbIsKeyDown(GBB_KEY_W) - (float)gbbIsKeyDown(GBB_KEY_S);
        const float moveRight = (float)gbbIsKeyDown(GBB_KEY_D) - (float)gbbIsKeyDown(GBB_KEY_A);
        const float moveUp = (float)gbbIsKeyDown(GBB_KEY_E) - (float)gbbIsKeyDown(GBB_KEY_Q);
        cameraPosition[0] += (forwardX * moveForward + rightX * moveRight) * move_speed * delta_time;
        cameraPosition[1] += (forwardY * moveForward + moveUp) * move_speed * delta_time;
        cameraPosition[2] += (forwardZ * moveForward + rightZ * moveRight) * move_speed * delta_time;

        CameraPushConstants cameraPush = {
            .origin = {cameraPosition[0], cameraPosition[1], cameraPosition[2], 0.0f},
            .forward_fov = {forwardX, forwardY, forwardZ, cameraFov},
        };

        vkResetCommandBuffer(commandBuffer, 0u);
        vkBeginCommandBuffer(commandBuffer, &(VkCommandBufferBeginInfo){
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        });

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapImages[imageIndex],
            .subresourceRange = imageRange
        });

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0u, 1u, &descriptorSets[imageIndex], 0u, NULL);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(cameraPush), &cameraPush);
        vkCmdDispatch(commandBuffer, (swapExtent.width + COMPUTE_TILE_SIZE - 1u) / COMPUTE_TILE_SIZE,
                      (swapExtent.height + COMPUTE_TILE_SIZE - 1u) / COMPUTE_TILE_SIZE, 1u);

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapImages[imageIndex],
            .subresourceRange = imageRange
        });

        vkEndCommandBuffer(commandBuffer);

        vkQueueSubmit(queue, 1u, &(VkSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &imageAvailableSemaphore,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1u,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &renderFinishedSemaphore,
        }, inFlightFence);

        vkQueuePresentKHR(queue, &(VkPresentInfoKHR){
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1u,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
        });
    }
    return 0;
}
