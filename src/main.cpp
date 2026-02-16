#include "triangle_comp_spv.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>

constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 1;

constexpr uint32_t MAX_INSTANCE_EXTENSIONS = 16;
constexpr uint32_t MAX_DEVICE_EXTENSIONS = 4;
constexpr uint32_t MAX_PHYSICAL_DEVICES = 8;
constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 3;

static_assert(MAX_FRAMES_IN_FLIGHT == 1);
static_assert(MAX_SWAPCHAIN_IMAGES >= MAX_FRAMES_IN_FLIGHT);
static_assert((kTriangleCompSpv_size != 0));
static_assert((kTriangleCompSpv_size % 4) == 0);

#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001
#endif
#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#if defined(__APPLE__)
constexpr uint32_t EXTRA_INSTANCE_EXTENSION_COUNT = 1;
constexpr const char *EXTRA_INSTANCE_EXTENSIONS[EXTRA_INSTANCE_EXTENSION_COUNT] = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
constexpr uint32_t EXTRA_DEVICE_EXTENSION_COUNT = 1;
constexpr const char *EXTRA_DEVICE_EXTENSIONS[EXTRA_DEVICE_EXTENSION_COUNT] = {
    VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
};
#else
constexpr uint32_t EXTRA_INSTANCE_EXTENSION_COUNT = 0;
constexpr const char **EXTRA_INSTANCE_EXTENSIONS = nullptr;
constexpr uint32_t EXTRA_DEVICE_EXTENSION_COUNT = 0;
constexpr const char **EXTRA_DEVICE_EXTENSIONS = nullptr;
#endif

static_assert(EXTRA_INSTANCE_EXTENSION_COUNT <= MAX_INSTANCE_EXTENSIONS);
static_assert((1 + EXTRA_DEVICE_EXTENSION_COUNT) <= MAX_DEVICE_EXTENSIONS);

GLFWwindow *window = nullptr;

VkInstance instance = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice device = VK_NULL_HANDLE;
VkQueue graphicsQueue = VK_NULL_HANDLE;

VkSurfaceKHR surface = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;
VkExtent2D swapExtent{};
uint32_t swapImageCount = 0;

std::array<VkImage, MAX_SWAPCHAIN_IMAGES> swapImages{};
std::array<VkImageView, MAX_SWAPCHAIN_IMAGES> swapImageViews{};

VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline computePipeline = VK_NULL_HANDLE;

VkCommandPool commandPool = VK_NULL_HANDLE;
VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

VkFence inFlightFence = VK_NULL_HANDLE;

void RecordCommandBuffer(uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    VkImageMemoryBarrier swapToGeneral{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &swapToGeneral);

    const uint32_t groupCountX = swapExtent.width;
    const uint32_t groupCountY = swapExtent.height;
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

    VkImageMemoryBarrier swapToPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &swapToPresent);

    vkEndCommandBuffer(commandBuffer);
}

void DrawFrame()
{
    uint32_t imageIndex = 0;
    vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        VK_NULL_HANDLE,
        inFlightFence,
        &imageIndex);
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    VkDescriptorImageInfo imageInfo{
        .imageView = swapImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    vkResetCommandBuffer(commandBuffer, 0);

    RecordCommandBuffer(imageIndex);

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
    };

    vkQueuePresentKHR(graphicsQueue, &presentInfo);
}

auto main() -> int
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(static_cast<int>(WINDOW_WIDTH), static_cast<int>(WINDOW_HEIGHT), "greadbadbeyond", nullptr, nullptr);

    {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::array<const char *, MAX_INSTANCE_EXTENSIONS> instanceExtensions{};
        uint32_t instanceExtensionCount = 0;

        for (uint32_t i = 0; i < glfwExtensionCount; i++)
        {
            instanceExtensions[instanceExtensionCount++] = glfwExtensions[i];
        }

        for (uint32_t i = 0; i < EXTRA_INSTANCE_EXTENSION_COUNT; i++)
        {
            instanceExtensions[instanceExtensionCount++] = EXTRA_INSTANCE_EXTENSIONS[i];
        }

#if defined(__APPLE__)
        constexpr uint32_t appApiVersion = VK_API_VERSION_1_1;
        constexpr VkInstanceCreateFlags instanceCreateFlags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#else
        constexpr uint32_t appApiVersion = VK_API_VERSION_1_3;
        constexpr VkInstanceCreateFlags instanceCreateFlags = 0;
#endif

        VkApplicationInfo appInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "greadbadbeyond",
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = "greadbadbeyond",
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = appApiVersion,
        };

        VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = instanceCreateFlags,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = instanceExtensionCount,
            .ppEnabledExtensionNames = instanceExtensions.data(),
        };

        vkCreateInstance(&createInfo, nullptr, &instance);
    }

    glfwCreateWindowSurface(instance, window, nullptr, &surface);

    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        std::array<VkPhysicalDevice, MAX_PHYSICAL_DEVICES> devices{};
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        physicalDevice = devices[0];
    }

    {
        std::array<const char *, MAX_DEVICE_EXTENSIONS> deviceExtensions{};
        uint32_t deviceExtensionCount = 0;
        deviceExtensions[deviceExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        for (uint32_t i = 0; i < EXTRA_DEVICE_EXTENSION_COUNT; i++)
        {
            deviceExtensions[deviceExtensionCount++] = EXTRA_DEVICE_EXTENSIONS[i];
        }

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = deviceExtensionCount,
            .ppEnabledExtensionNames = deviceExtensions.data(),
            .pEnabledFeatures = &deviceFeatures,
        };

        vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
        vkGetDeviceQueue(device, 0, 0, &graphicsQueue);
    }

    {
        VkSurfaceCapabilitiesKHR surfaceCaps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

        uint32_t imageCount = surfaceCaps.minImageCount;
        if (imageCount < 2)
        {
            imageCount = 2;
        }

        swapExtent = surfaceCaps.currentExtent;

        VkSwapchainCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = imageCount,
            .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
            .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            .imageExtent = swapExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = surfaceCaps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);

        swapImageCount = MAX_SWAPCHAIN_IMAGES;
        vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages.data());

        for (uint32_t i = 0; i < swapImageCount; i++)
        {
            VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_B8G8R8A8_UNORM,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };
            vkCreateImageView(device, &viewInfo, nullptr, &swapImageViews[i]);
        }
    }

    {
        VkDescriptorSetLayoutBinding layoutBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &layoutBinding,
        };
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptorSetLayout,
        };
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

        VkShaderModuleCreateInfo moduleInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = kTriangleCompSpv_size,
            .pCode = reinterpret_cast<const uint32_t *>(kTriangleCompSpv),
        };
        VkShaderModule computeModule = VK_NULL_HANDLE;
        vkCreateShaderModule(device, &moduleInfo, nullptr, &computeModule);

        VkPipelineShaderStageCreateInfo stageInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = computeModule,
            .pName = "main",
        };

        VkComputePipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stageInfo,
            .layout = pipelineLayout,
        };
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline);

        vkDestroyShaderModule(device, computeModule, nullptr);

        VkDescriptorPoolSize poolSize{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
        };
        VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

        VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptorSetLayout,
        };
        vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
    }

    {
        VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = 0,
        };

        vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

        VkCommandBufferAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    }

    {
        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = 0,
        };
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence);
    }

    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
        DrawFrame();
    }

    vkDeviceWaitIdle(device);

    vkDestroyFence(device, inFlightFence, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    for (uint32_t i = 0; i < swapImageCount; i++)
    {
        vkDestroyImageView(device, swapImageViews[i], nullptr);
    }

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    window = nullptr;

    glfwTerminate();
    return 0;
}
