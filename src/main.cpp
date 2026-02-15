#include "triangle_frag_spv.h"
#include "triangle_vert_spv.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

constexpr uint32_t MAX_INSTANCE_EXTENSIONS = 16;
constexpr uint32_t MAX_DEVICE_EXTENSIONS = 4;
constexpr uint32_t MAX_PHYSICAL_DEVICES = 8;
constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 8;

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

GLFWwindow *window = nullptr;

VkInstance instance = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice device = VK_NULL_HANDLE;
VkQueue graphicsQueue = VK_NULL_HANDLE;
VkQueue presentQueue = VK_NULL_HANDLE;

VkSurfaceKHR surface = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;
VkFormat swapFormat = VK_FORMAT_UNDEFINED;
VkExtent2D swapExtent{};
uint32_t swapImageCount = 0;

std::array<VkImage, MAX_SWAPCHAIN_IMAGES> swapImages{};
std::array<VkImageView, MAX_SWAPCHAIN_IMAGES> swapImageViews{};
std::array<VkFramebuffer, MAX_SWAPCHAIN_IMAGES> swapFramebuffers{};

VkRenderPass renderPass = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline graphicsPipeline = VK_NULL_HANDLE;

VkCommandPool commandPool = VK_NULL_HANDLE;
std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> commandBuffers{};

std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableSemaphores{};
std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> renderFinishedSemaphores{};
std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFences{};
std::array<VkFence, MAX_SWAPCHAIN_IMAGES> imageInFlightFences{};

[[noreturn]] void Fatal(const char *message)
{
    std::fprintf(stderr, "error: %s\n", message);
    std::abort();
}

void CheckVk(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
    {
        std::fprintf(stderr, "error: %s failed (VkResult=%d)\n", operation, static_cast<int>(result));
        std::abort();
    }
}

auto CreateShaderModule(const std::uint8_t *code, std::size_t size) -> VkShaderModule
{
    if ((size == 0) || ((size % 4) != 0))
    {
        Fatal("invalid embedded shader data");
    }

    VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = reinterpret_cast<const uint32_t *>(code),
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    CheckVk(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule), "vkCreateShaderModule");
    return shaderModule;
}

void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    VkClearValue clearColor{
        .color = {{0.05f, 0.05f, 0.08f, 1.0f}},
    };

    VkRenderPassBeginInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass,
        .framebuffer = swapFramebuffers[imageIndex],
        .renderArea = {
            .offset = {0, 0},
            .extent = swapExtent,
        },
        .clearValueCount = 1,
        .pClearValues = &clearColor,
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

void DrawFrame(uint32_t currentFrame)
{
    CheckVk(vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX), "vkWaitForFences");

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return;
    }

    if ((acquireResult != VK_SUCCESS) && (acquireResult != VK_SUBOPTIMAL_KHR))
    {
        CheckVk(acquireResult, "vkAcquireNextImageKHR");
    }

    if (imageInFlightFences[imageIndex] != VK_NULL_HANDLE)
    {
        CheckVk(vkWaitForFences(device, 1, &imageInFlightFences[imageIndex], VK_TRUE, UINT64_MAX), "vkWaitForFences(image)");
    }
    imageInFlightFences[imageIndex] = inFlightFences[currentFrame];

    CheckVk(vkResetFences(device, 1, &inFlightFences[currentFrame]), "vkResetFences");
    CheckVk(vkResetCommandBuffer(commandBuffers[currentFrame], 0), "vkResetCommandBuffer");

    RecordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffers[currentFrame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signalSemaphores,
    };

    CheckVk(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]), "vkQueueSubmit");

    VkSwapchainKHR swapchains[] = {swapchain};

    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,
        .swapchainCount = 1,
        .pSwapchains = swapchains,
        .pImageIndices = &imageIndex,
    };

    VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
    if ((presentResult != VK_SUCCESS) && (presentResult != VK_SUBOPTIMAL_KHR) && (presentResult != VK_ERROR_OUT_OF_DATE_KHR))
    {
        CheckVk(presentResult, "vkQueuePresentKHR");
    }
}


auto main() -> int
{
    if ((kTriangleVertSpv_size == 0) || (kTriangleFragSpv_size == 0))
    {
        Fatal("embedded shader headers are empty");
    }

    if ((kTriangleVertSpv_size % 4) != 0)
    {
        Fatal("vertex shader size must be 4-byte aligned");
    }

    if ((kTriangleFragSpv_size % 4) != 0)
    {
        Fatal("fragment shader size must be 4-byte aligned");
    }

    if (glfwInit() == GLFW_FALSE)
    {
        Fatal("failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(static_cast<int>(WINDOW_WIDTH), static_cast<int>(WINDOW_HEIGHT), "greadbadbeyond", nullptr, nullptr);
    if (window == nullptr)
    {
        Fatal("failed to create GLFW window");
    }

    {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        if ((glfwExtensions == nullptr) || (glfwExtensionCount == 0))
        {
            Fatal("glfwGetRequiredInstanceExtensions returned no extensions");
        }

        if ((glfwExtensionCount + EXTRA_INSTANCE_EXTENSION_COUNT) > MAX_INSTANCE_EXTENSIONS)
        {
            Fatal("MAX_INSTANCE_EXTENSIONS too small");
        }

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
            .pApplicationInfo = &appInfo,
            .flags = instanceCreateFlags,
            .enabledExtensionCount = instanceExtensionCount,
            .ppEnabledExtensionNames = instanceExtensions.data(),
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
        };

        CheckVk(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
    }

    CheckVk(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");

    {
        uint32_t deviceCount = 0;
        CheckVk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices(count)");

        if (deviceCount == 0)
        {
            Fatal("no Vulkan physical devices found");
        }

        if (deviceCount > MAX_PHYSICAL_DEVICES)
        {
            Fatal("MAX_PHYSICAL_DEVICES too small");
        }

        std::array<VkPhysicalDevice, MAX_PHYSICAL_DEVICES> devices{};
        CheckVk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices(list)");

        physicalDevice = devices[0];

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        if (queueFamilyCount == 0)
        {
            Fatal("selected GPU has no queue families");
        }

        std::array<VkQueueFamilyProperties, 16> queueFamilies{};
        if (queueFamilyCount > queueFamilies.size())
        {
            Fatal("queue family count exceeds fixed queue family cache");
        }

        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        if ((queueFamilies[0].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
        {
            Fatal("queue family 0 does not support graphics");
        }

        VkBool32 presentSupport = VK_FALSE;
        CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, 0, surface, &presentSupport), "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (presentSupport == VK_FALSE)
        {
            Fatal("queue family 0 does not support present");
        }
    }

    {
        if ((1 + EXTRA_DEVICE_EXTENSION_COUNT) > MAX_DEVICE_EXTENSIONS)
        {
            Fatal("MAX_DEVICE_EXTENSIONS too small");
        }

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

        CheckVk(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, 0, 0, &graphicsQueue);
        vkGetDeviceQueue(device, 0, 0, &presentQueue);
    }

    {
        VkSurfaceCapabilitiesKHR surfaceCaps{};
        CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        uint32_t imageCount = MAX_FRAMES_IN_FLIGHT;
        if (imageCount < surfaceCaps.minImageCount)
        {
            imageCount = surfaceCaps.minImageCount;
        }
        if ((surfaceCaps.maxImageCount > 0) && (imageCount > surfaceCaps.maxImageCount))
        {
            imageCount = surfaceCaps.maxImageCount;
        }

        if (surfaceCaps.currentExtent.width != UINT32_MAX)
        {
            swapExtent = surfaceCaps.currentExtent;
        }
        else
        {
            swapExtent.width = WINDOW_WIDTH;
            swapExtent.height = WINDOW_HEIGHT;

            if (swapExtent.width < surfaceCaps.minImageExtent.width) swapExtent.width = surfaceCaps.minImageExtent.width;
            if (swapExtent.width > surfaceCaps.maxImageExtent.width) swapExtent.width = surfaceCaps.maxImageExtent.width;
            if (swapExtent.height < surfaceCaps.minImageExtent.height) swapExtent.height = surfaceCaps.minImageExtent.height;
            if (swapExtent.height > surfaceCaps.maxImageExtent.height) swapExtent.height = surfaceCaps.maxImageExtent.height;
        }

        VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if ((surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0)
        {
            if ((surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0)
                compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
            else if ((surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0)
                compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
            else
                compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }

        VkSwapchainCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = imageCount,
            .imageFormat = VK_FORMAT_B8G8R8A8_SRGB,
            .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            .imageExtent = swapExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = surfaceCaps.currentTransform,
            .compositeAlpha = compositeAlpha,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        CheckVk(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain), "vkCreateSwapchainKHR");

        swapFormat = VK_FORMAT_B8G8R8A8_SRGB;

        CheckVk(vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, nullptr), "vkGetSwapchainImagesKHR(count)");
        if ((swapImageCount == 0) || (swapImageCount > MAX_SWAPCHAIN_IMAGES))
        {
            Fatal("swapchain image count invalid for fixed cache");
        }

        CheckVk(vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages.data()), "vkGetSwapchainImagesKHR(list)");

        for (uint32_t i = 0; i < swapImageCount; i++)
        {
            VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = swapFormat,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };

            CheckVk(vkCreateImageView(device, &viewInfo, nullptr, &swapImageViews[i]), "vkCreateImageView");
        }
    }

    {
        VkAttachmentDescription colorAttachment{
            .format = swapFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };

        VkAttachmentReference colorAttachmentRef{
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        VkSubpassDescription subpass{
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentRef,
        };

        VkSubpassDependency dependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };

        VkRenderPassCreateInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colorAttachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &dependency,
        };

        CheckVk(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass), "vkCreateRenderPass");

        VkShaderModule vertModule = CreateShaderModule(kTriangleVertSpv, kTriangleVertSpv_size);
        VkShaderModule fragModule = CreateShaderModule(kTriangleFragSpv, kTriangleFragSpv_size);

        VkPipelineShaderStageCreateInfo shaderStages[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertModule,
                .pName = "main",
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragModule,
                .pName = "main",
            },
        };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .vertexAttributeDescriptionCount = 0,
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(swapExtent.width),
            .height = static_cast<float>(swapExtent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        VkRect2D scissor{
            .offset = {0, 0},
            .extent = swapExtent,
        };

        VkPipelineViewportStateCreateInfo viewportState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports = &viewport,
            .scissorCount = 1,
            .pScissors = &scissor,
        };

        VkPipelineRasterizationStateCreateInfo rasterizer{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .lineWidth = 1.0f,
        };

        VkPipelineMultisampleStateCreateInfo multisampling{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .sampleShadingEnable = VK_FALSE,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable = VK_FALSE,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT,
        };

        VkPipelineColorBlendStateCreateInfo colorBlending{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        };
        CheckVk(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

        VkGraphicsPipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pColorBlendState = &colorBlending,
            .layout = pipelineLayout,
            .renderPass = renderPass,
            .subpass = 0,
        };

        CheckVk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline), "vkCreateGraphicsPipelines");

        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);

        for (uint32_t i = 0; i < swapImageCount; i++)
        {
            VkImageView attachments[] = {swapImageViews[i]};

            VkFramebufferCreateInfo framebufferInfo{
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = renderPass,
                .attachmentCount = 1,
                .pAttachments = attachments,
                .width = swapExtent.width,
                .height = swapExtent.height,
                .layers = 1,
            };

            CheckVk(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapFramebuffers[i]), "vkCreateFramebuffer");
        }
    }

    {
        VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = 0,
        };

        CheckVk(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
        };

        CheckVk(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()), "vkAllocateCommandBuffers");
    }

    {
        VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            CheckVk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]), "vkCreateSemaphore(imageAvailable)");
            CheckVk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]), "vkCreateSemaphore(renderFinished)");
            CheckVk(vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]), "vkCreateFence");
        }

        for (uint32_t i = 0; i < MAX_SWAPCHAIN_IMAGES; i++)
        {
            imageInFlightFences[i] = VK_NULL_HANDLE;
        }
    }

    uint32_t currentFrame = 0;
    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
        DrawFrame(currentFrame);
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (inFlightFences[i] != VK_NULL_HANDLE) vkDestroyFence(device, inFlightFences[i], nullptr);
        if (renderFinishedSemaphores[i] != VK_NULL_HANDLE) vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        if (imageAvailableSemaphores[i] != VK_NULL_HANDLE) vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    }

    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, commandPool, nullptr);
    }

    for (uint32_t i = 0; i < swapImageCount; i++)
    {
        if (swapFramebuffers[i] != VK_NULL_HANDLE) vkDestroyFramebuffer(device, swapFramebuffers[i], nullptr);
        if (swapImageViews[i] != VK_NULL_HANDLE) vkDestroyImageView(device, swapImageViews[i], nullptr);
    }

    if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);

    if (device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device, nullptr);
    }

    if (surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
    }

    if (window != nullptr)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    glfwTerminate();
    return 0;
}
