#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <stdint.h>

#include "gradient_comp_spv.h"

#if defined(__APPLE__)
#define VK_PORTABLE 1u
#else
#define VK_PORTABLE 0u
#endif

#define MAX_INSTANCE_EXTENSIONS 8u
#define MAX_DEVICE_EXTENSIONS 8u
#define FRAME_COUNT 3u
#define COMPUTE_TILE_SIZE 8u

static const char *APPLICATION_NAME = "greatbadbeyond";

static const char *INSTANCE_EXTENSIONS[MAX_INSTANCE_EXTENSIONS] = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
static const char *DEVICE_EXTENSIONS[MAX_DEVICE_EXTENSIONS] = {
    VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
};

static VkInstance instance = VK_NULL_HANDLE;
static GLFWwindow *window = NULL;
static VkSurfaceKHR surface = VK_NULL_HANDLE;
static VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
static VkDevice device = VK_NULL_HANDLE;
static VkQueue queue = VK_NULL_HANDLE;
static VkSwapchainKHR swapchain = VK_NULL_HANDLE;
static VkImage swapImages[FRAME_COUNT];
static VkImageView swapImageViews[FRAME_COUNT];
static VkExtent2D swapExtent = { 0u, 0u };

static VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet descriptorSets[FRAME_COUNT];

static VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
static VkPipeline pipeline = VK_NULL_HANDLE;

static VkCommandPool commandPool = VK_NULL_HANDLE;
static VkCommandBuffer commandBuffers[FRAME_COUNT];

static VkSemaphore imageAvailableSemaphore[FRAME_COUNT];
static VkSemaphore renderFinishedSemaphore[FRAME_COUNT];
static VkFence inFlightFences[FRAME_COUNT];
static uint32_t frameIndex = 0u;

static void recordCommandBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, VkImage swapImage)
{
    vkBeginCommandBuffer(commandBuffer, &(VkCommandBufferBeginInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    });

    VkImageSubresourceRange colorRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkImageMemoryBarrier toGeneralBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapImage,
        .subresourceRange = colorRange,
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &toGeneralBarrier);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

    vkCmdDispatch(
        commandBuffer,
        (swapExtent.width + (COMPUTE_TILE_SIZE - 1u)) / COMPUTE_TILE_SIZE,
        (swapExtent.height + (COMPUTE_TILE_SIZE - 1u)) / COMPUTE_TILE_SIZE,
        1u);

    VkImageMemoryBarrier toPresentBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapImage,
        .subresourceRange = colorRange,
    };

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &toPresentBarrier);

    vkEndCommandBuffer(commandBuffer);
}

int main(void)
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(1280, 720, APPLICATION_NAME, NULL, NULL);

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    for (uint32_t i = 0; i < glfwExtensionCount; i++)
    {
        INSTANCE_EXTENSIONS[VK_PORTABLE + i] = glfwExtensions[i];
    }

    VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = APPLICATION_NAME,
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = APPLICATION_NAME,
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkInstanceCreateInfo instanceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_PORTABLE * VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        .pApplicationInfo = &applicationInfo,
        .enabledExtensionCount = VK_PORTABLE + glfwExtensionCount,
        .ppEnabledExtensionNames = INSTANCE_EXTENSIONS,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
    };
    vkCreateInstance(&instanceCreateInfo, NULL, &instance);

    glfwCreateWindowSurface(instance, window, NULL, &surface);

    uint32_t physicalDeviceCount = 1;
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, &physicalDevice);

    DEVICE_EXTENSIONS[VK_PORTABLE] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = 0u,
        .queueCount = 1u,
        .pQueuePriorities = &queuePriority,
    };

    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = 1u + VK_PORTABLE,
        .ppEnabledExtensionNames = DEVICE_EXTENSIONS,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .pEnabledFeatures = NULL,
    };
    vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device);
    vkGetDeviceQueue(device, 0u, 0u, &queue);

    VkSurfaceCapabilitiesKHR surfaceCaps = { 0 };
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

    swapExtent = surfaceCaps.currentExtent;

    VkSwapchainCreateInfoKHR swapchainCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = surface,
        .minImageCount = FRAME_COUNT,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = swapExtent,
        .imageArrayLayers = 1u,
        .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0u,
        .pQueueFamilyIndices = NULL,
        .preTransform = surfaceCaps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    vkCreateSwapchainKHR(device, &swapchainCreateInfo, NULL, &swapchain);

    uint32_t swapImageCount = FRAME_COUNT;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);

    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        vkCreateImageView(device, &(VkImageViewCreateInfo){
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = (VkImageSubresourceRange){
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        }, NULL, &swapImageViews[i]);
    }

    VkDescriptorSetLayoutBinding storageImageBinding = {
        .binding = 0u,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .pImmutableSamplers = NULL,
    };

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .bindingCount = 1u,
        .pBindings = &storageImageBinding,
    };
    vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &descriptorSetLayout);

    VkDescriptorPoolSize descriptorPoolSize = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = FRAME_COUNT,
    };

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .maxSets = FRAME_COUNT,
        .poolSizeCount = 1u,
        .pPoolSizes = &descriptorPoolSize,
    };
    vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, NULL, &descriptorPool);

    VkDescriptorSetLayout descriptorSetLayouts[FRAME_COUNT];
    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        descriptorSetLayouts[i] = descriptorSetLayout;
    }

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = FRAME_COUNT,
        .pSetLayouts = descriptorSetLayouts,
    };
    vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, descriptorSets);

    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        VkDescriptorImageInfo descriptorImageInfo = {
            .sampler = VK_NULL_HANDLE,
            .imageView = swapImageViews[i],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

        VkWriteDescriptorSet writeDescriptorSet = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = NULL,
            .dstSet = descriptorSets[i],
            .dstBinding = 0u,
            .dstArrayElement = 0u,
            .descriptorCount = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &descriptorImageInfo,
            .pBufferInfo = NULL,
            .pTexelBufferView = NULL,
        };
        vkUpdateDescriptorSets(device, 1u, &writeDescriptorSet, 0u, NULL);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .setLayoutCount = 1u,
        .pSetLayouts = &descriptorSetLayout,
        .pushConstantRangeCount = 0u,
        .pPushConstantRanges = NULL,
    };
    vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &pipelineLayout);

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shaderModuleCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .codeSize = kGradientCompSpv_size,
        .pCode = kGradientCompSpv,
    };
    vkCreateShaderModule(device, &shaderModuleCreateInfo, NULL, &shaderModule);

    VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shaderModule,
        .pName = "main",
        .pSpecializationInfo = NULL,
    };

    VkComputePipelineCreateInfo computePipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = pipelineShaderStageCreateInfo,
        .layout = pipelineLayout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u, &computePipelineCreateInfo, NULL, &pipeline);
    vkDestroyShaderModule(device, shaderModule, NULL);

    VkCommandPoolCreateInfo commandPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = 0u,
    };
    vkCreateCommandPool(device, &commandPoolCreateInfo, NULL, &commandPool);

    vkAllocateCommandBuffers(device, &(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = FRAME_COUNT,
    }, commandBuffers);

    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        recordCommandBuffer(commandBuffers[i], descriptorSets[i], swapImages[i]);
    }

    VkSemaphoreCreateInfo semaphoreCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
    };
    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        vkCreateSemaphore(device, &semaphoreCreateInfo, NULL, &imageAvailableSemaphore[i]);
        vkCreateSemaphore(device, &semaphoreCreateInfo, NULL, &renderFinishedSemaphore[i]);
        vkCreateFence(device, &(VkFenceCreateInfo){
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        }, NULL, &inFlightFences[i]);
    }

    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        uint32_t fi = frameIndex;
        vkWaitForFences(device, 1u, &inFlightFences[fi], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1u, &inFlightFences[fi]);

        uint32_t imageIndex = 0u;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore[fi], VK_NULL_HANDLE, &imageIndex);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkSemaphore waitSems[] = { imageAvailableSemaphore[fi] };
        VkSemaphore signalSems[] = { renderFinishedSemaphore[fi] };

        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = NULL,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = waitSems,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1u,
            .pCommandBuffers = &commandBuffers[imageIndex],
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = signalSems,
        };
        vkQueueSubmit(queue, 1u, &submitInfo, inFlightFences[fi]);

        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = NULL,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &renderFinishedSemaphore[fi],
            .swapchainCount = 1u,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
            .pResults = NULL,
        };
        vkQueuePresentKHR(queue, &presentInfo);
        frameIndex = (frameIndex + 1u) % FRAME_COUNT;
    }

    vkDeviceWaitIdle(device);

    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        vkDestroyFence(device, inFlightFences[i], NULL);
        vkDestroySemaphore(device, renderFinishedSemaphore[i], NULL);
        vkDestroySemaphore(device, imageAvailableSemaphore[i], NULL);
    }

    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);

    for (uint32_t i = 0; i < FRAME_COUNT; i++)
    {
        vkDestroyImageView(device, swapImageViews[i], NULL);
    }

    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    glfwDestroyWindow(window);
    window = NULL;

    glfwTerminate();
    return 0;
}
