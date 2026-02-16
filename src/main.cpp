#include "triangle_frag_spv.h"
#include "triangle_vert_spv.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>

constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

constexpr uint32_t MAX_INSTANCE_EXTENSIONS = 16;
constexpr uint32_t MAX_DEVICE_EXTENSIONS = 4;
constexpr uint32_t MAX_PHYSICAL_DEVICES = 8;
constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 3;

static_assert(MAX_FRAMES_IN_FLIGHT > 0);
static_assert(MAX_SWAPCHAIN_IMAGES >= MAX_FRAMES_IN_FLIGHT);
static_assert((kTriangleVertSpv_size != 0) && (kTriangleFragSpv_size != 0));
static_assert((kTriangleVertSpv_size % 4) == 0);
static_assert((kTriangleFragSpv_size % 4) == 0);

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

std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFences{};

void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

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

    vkEndCommandBuffer(commandBuffer);
}

void DrawFrame(uint32_t currentFrame)
{
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    uint32_t imageIndex = 0;
    vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        &imageIndex);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    RecordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffers[currentFrame],
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    VkSwapchainKHR swapchains[] = {swapchain};

    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .swapchainCount = 1,
        .pSwapchains = swapchains,
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
            .pApplicationInfo = &appInfo,
            .flags = instanceCreateFlags,
            .enabledExtensionCount = instanceExtensionCount,
            .ppEnabledExtensionNames = instanceExtensions.data(),
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
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

        uint32_t imageCount = MAX_FRAMES_IN_FLIGHT;

        swapExtent = surfaceCaps.currentExtent;

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
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);

        swapFormat = VK_FORMAT_B8G8R8A8_SRGB;

        vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, nullptr);
        vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages.data());

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

            vkCreateImageView(device, &viewInfo, nullptr, &swapImageViews[i]);
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

        vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);

        VkShaderModuleCreateInfo vertModuleInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = kTriangleVertSpv_size,
            .pCode = reinterpret_cast<const uint32_t *>(kTriangleVertSpv),
        };
        VkShaderModule vertModule = VK_NULL_HANDLE;
        vkCreateShaderModule(device, &vertModuleInfo, nullptr, &vertModule);

        VkShaderModuleCreateInfo fragModuleInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = kTriangleFragSpv_size,
            .pCode = reinterpret_cast<const uint32_t *>(kTriangleFragSpv),
        };
        VkShaderModule fragModule = VK_NULL_HANDLE;
        vkCreateShaderModule(device, &fragModuleInfo, nullptr, &fragModule);

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
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

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

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);

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

            vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapFramebuffers[i]);
        }
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
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
        };

        vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
    }

    {
        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]);
        }

    }

    uint32_t currentFrame = 0;
    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
        DrawFrame(currentFrame);
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(device);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    for (uint32_t i = 0; i < swapImageCount; i++)
    {
        vkDestroyFramebuffer(device, swapFramebuffers[i], nullptr);
        vkDestroyImageView(device, swapImageViews[i], nullptr);
    }

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    window = nullptr;

    glfwTerminate();
    return 0;
}
