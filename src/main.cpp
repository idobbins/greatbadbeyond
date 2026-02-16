#include "triangle_comp_spv.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

constexpr uint32_t MAX_INSTANCE_EXTENSIONS = 16;
constexpr uint32_t MAX_DEVICE_EXTENSIONS = 4;
constexpr uint32_t MAX_PHYSICAL_DEVICES = 8;
constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 3;
constexpr uint32_t ARENA_HEADER_WORDS = 32;
constexpr uint32_t BRICK_WORDS = 16;
constexpr uint32_t BRICK_POOL_CAPACITY = 4;
constexpr uint32_t ARENA_BRICK_TABLE_BASE_WORD = ARENA_HEADER_WORDS;
constexpr uint32_t ARENA_BRICK_POOL_BASE_WORD = ARENA_HEADER_WORDS;
constexpr uint32_t SLOT_WORDS = ARENA_HEADER_WORDS + BRICK_WORDS * BRICK_POOL_CAPACITY;

constexpr uint32_t HDR_CAM_POS_X = 0;
constexpr uint32_t HDR_CAM_POS_Y = 1;
constexpr uint32_t HDR_CAM_POS_Z = 2;
constexpr uint32_t HDR_CAM_YAW = 3;
constexpr uint32_t HDR_CAM_PITCH = 4;
constexpr uint32_t HDR_CAM_MOVE_SPEED = 5;
constexpr uint32_t HDR_CAM_MOUSE_SENSITIVITY = 6;
constexpr uint32_t HDR_CAM_FRAME_INDEX = 7;
constexpr uint32_t HDR_CAM_FOV_Y = 8;
constexpr uint32_t HDR_GRID_MIN_X = 9;
constexpr uint32_t HDR_GRID_MIN_Y = 10;
constexpr uint32_t HDR_GRID_MIN_Z = 11;
constexpr uint32_t HDR_GRID_DIM_X = 12;
constexpr uint32_t HDR_GRID_DIM_Y = 13;
constexpr uint32_t HDR_GRID_DIM_Z = 14;
constexpr uint32_t HDR_BRICK_COUNT = 15;
constexpr uint32_t HDR_BRICK_TABLE_OFFSET_WORDS = 16;
constexpr uint32_t HDR_BRICK_POOL_OFFSET_WORDS = 17;

constexpr float CAMERA_MOVE_SPEED = 3.25f;
constexpr float CAMERA_MOUSE_SENSITIVITY = 0.0024f;
constexpr float CAMERA_FOV_Y = 1.0471976f;
constexpr float CAMERA_SPEED_BOOST_MULTIPLIER = 3.0f;
constexpr float TEST_BRICK_MIN_X = -1.0f;
constexpr float TEST_BRICK_MIN_Y = -1.0f;
constexpr float TEST_BRICK_MIN_Z = -1.0f;
constexpr float TEST_BRICK_VOXEL_SIZE = 0.5f;
constexpr float SCENE_GRID_MIN_X = -1.0f;
constexpr float SCENE_GRID_MIN_Y = -1.0f;
constexpr float SCENE_GRID_MIN_Z = -1.0f;
constexpr uint32_t SCENE_GRID_DIM_X = 1;
constexpr uint32_t SCENE_GRID_DIM_Y = 1;
constexpr uint32_t SCENE_GRID_DIM_Z = 1;
constexpr uint32_t SCENE_BRICK_COUNT = 1;

constexpr uint32_t DATA_WORD_COUNT = SLOT_WORDS * MAX_FRAMES_IN_FLIGHT;
constexpr VkDeviceSize DATA_BUFFER_SIZE = static_cast<VkDeviceSize>(DATA_WORD_COUNT) * sizeof(uint32_t);

static_assert(MAX_FRAMES_IN_FLIGHT == 3);
static_assert(MAX_SWAPCHAIN_IMAGES >= MAX_FRAMES_IN_FLIGHT);
static_assert(HDR_BRICK_POOL_OFFSET_WORDS < ARENA_HEADER_WORDS);
static_assert((ARENA_BRICK_POOL_BASE_WORD + BRICK_WORDS) <= SLOT_WORDS);
static_assert((kTriangleCompSpv_size != 0));
static_assert((kTriangleCompSpv_size % 4) == 0);

constexpr VkInstanceCreateFlags PORTABILITY_ENUMERATE_FLAG = 0x00000001;
constexpr const char *PORTABILITY_ENUMERATION_EXTENSION = "VK_KHR_portability_enumeration";
constexpr const char *PORTABILITY_SUBSET_EXTENSION = "VK_KHR_portability_subset";

#if defined(__APPLE__)
constexpr uint32_t EXTRA_INSTANCE_EXTENSION_COUNT = 1;
constexpr const char *EXTRA_INSTANCE_EXTENSIONS[EXTRA_INSTANCE_EXTENSION_COUNT] = {
    PORTABILITY_ENUMERATION_EXTENSION,
};
constexpr uint32_t EXTRA_DEVICE_EXTENSION_COUNT = 1;
constexpr const char *EXTRA_DEVICE_EXTENSIONS[EXTRA_DEVICE_EXTENSION_COUNT] = {
    PORTABILITY_SUBSET_EXTENSION,
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
std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets{};

VkBuffer dataBuffer = VK_NULL_HANDLE;
VkDeviceMemory dataBufferMemory = VK_NULL_HANDLE;
uint32_t *dataBufferWords = nullptr;

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline computePipeline = VK_NULL_HANDLE;

VkCommandPool commandPool = VK_NULL_HANDLE;
std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> commandBuffers{};

std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFences{};
std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableSemaphores{};
std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> renderFinishedSemaphores{};

float cameraPosX = 0.0f;
float cameraPosY = 0.0f;
float cameraPosZ = 2.5f;
float cameraYaw = -1.5707963f;
float cameraPitch = 0.0f;
double lastMouseX = 0.0;
double lastMouseY = 0.0;
bool mouseInitialized = false;
bool cameraTimeInitialized = false;
double lastCameraSampleTime = 0.0;
uint32_t frameCounter = 0;

uint32_t FindMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags requiredFlags)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        const uint32_t typeMatch = (typeBits & (1u << i));
        const uint32_t flagsMatch = (memoryProperties.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags;
        if (typeMatch != 0 && flagsMatch != 0)
        {
            return i;
        }
    }

    return 0;
}

void UpdateFlightCamera()
{
    const double now = glfwGetTime();
    if (!cameraTimeInitialized)
    {
        cameraTimeInitialized = true;
        lastCameraSampleTime = now;
    }
    double deltaTimeSeconds = now - lastCameraSampleTime;
    lastCameraSampleTime = now;
    if (deltaTimeSeconds < 0.0)
    {
        deltaTimeSeconds = 0.0;
    }
    if (deltaTimeSeconds > 0.05)
    {
        deltaTimeSeconds = 0.05;
    }
    const float deltaTime = static_cast<float>(deltaTimeSeconds);

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (!mouseInitialized)
    {
        mouseInitialized = true;
        lastMouseX = mouseX;
        lastMouseY = mouseY;
    }

    const double mouseDeltaX = mouseX - lastMouseX;
    const double mouseDeltaY = mouseY - lastMouseY;
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    cameraYaw += static_cast<float>(mouseDeltaX) * CAMERA_MOUSE_SENSITIVITY;
    cameraPitch -= static_cast<float>(mouseDeltaY) * CAMERA_MOUSE_SENSITIVITY;

    constexpr float maxPitch = 1.5533430f;
    if (cameraPitch > maxPitch)
    {
        cameraPitch = maxPitch;
    }
    if (cameraPitch < -maxPitch)
    {
        cameraPitch = -maxPitch;
    }

    const float cosPitch = std::cos(cameraPitch);
    const float sinPitch = std::sin(cameraPitch);
    const float cosYaw = std::cos(cameraYaw);
    const float sinYaw = std::sin(cameraYaw);

    const float forwardX = cosPitch * cosYaw;
    const float forwardY = sinPitch;
    const float forwardZ = cosPitch * sinYaw;

    const float rightX = -sinYaw;
    const float rightY = 0.0f;
    const float rightZ = cosYaw;

    const float speedBoost = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? CAMERA_SPEED_BOOST_MULTIPLIER : 1.0f;
    const float step = CAMERA_MOVE_SPEED * speedBoost * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    {
        cameraPosX += forwardX * step;
        cameraPosY += forwardY * step;
        cameraPosZ += forwardZ * step;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {
        cameraPosX -= forwardX * step;
        cameraPosY -= forwardY * step;
        cameraPosZ -= forwardZ * step;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    {
        cameraPosX += rightX * step;
        cameraPosY += rightY * step;
        cameraPosZ += rightZ * step;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    {
        cameraPosX -= rightX * step;
        cameraPosY -= rightY * step;
        cameraPosZ -= rightZ * step;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
    {
        cameraPosY += step;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
    {
        cameraPosY -= step;
    }
}

void WriteArenaHeaderData(uint32_t currentFrame)
{
    const uint32_t base = currentFrame * SLOT_WORDS;
    dataBufferWords[base + HDR_CAM_POS_X] = std::bit_cast<uint32_t>(cameraPosX);
    dataBufferWords[base + HDR_CAM_POS_Y] = std::bit_cast<uint32_t>(cameraPosY);
    dataBufferWords[base + HDR_CAM_POS_Z] = std::bit_cast<uint32_t>(cameraPosZ);
    dataBufferWords[base + HDR_CAM_YAW] = std::bit_cast<uint32_t>(cameraYaw);
    dataBufferWords[base + HDR_CAM_PITCH] = std::bit_cast<uint32_t>(cameraPitch);
    dataBufferWords[base + HDR_CAM_MOVE_SPEED] = std::bit_cast<uint32_t>(CAMERA_MOVE_SPEED);
    dataBufferWords[base + HDR_CAM_MOUSE_SENSITIVITY] = std::bit_cast<uint32_t>(CAMERA_MOUSE_SENSITIVITY);
    dataBufferWords[base + HDR_CAM_FRAME_INDEX] = frameCounter++;
    dataBufferWords[base + HDR_CAM_FOV_Y] = std::bit_cast<uint32_t>(CAMERA_FOV_Y);

    dataBufferWords[base + HDR_GRID_MIN_X] = std::bit_cast<uint32_t>(SCENE_GRID_MIN_X);
    dataBufferWords[base + HDR_GRID_MIN_Y] = std::bit_cast<uint32_t>(SCENE_GRID_MIN_Y);
    dataBufferWords[base + HDR_GRID_MIN_Z] = std::bit_cast<uint32_t>(SCENE_GRID_MIN_Z);
    dataBufferWords[base + HDR_GRID_DIM_X] = SCENE_GRID_DIM_X;
    dataBufferWords[base + HDR_GRID_DIM_Y] = SCENE_GRID_DIM_Y;
    dataBufferWords[base + HDR_GRID_DIM_Z] = SCENE_GRID_DIM_Z;
    dataBufferWords[base + HDR_BRICK_COUNT] = SCENE_BRICK_COUNT;
    dataBufferWords[base + HDR_BRICK_TABLE_OFFSET_WORDS] = ARENA_BRICK_TABLE_BASE_WORD;
    dataBufferWords[base + HDR_BRICK_POOL_OFFSET_WORDS] = ARENA_BRICK_POOL_BASE_WORD;
}

void WriteBrickData(uint32_t currentFrame)
{
    const uint32_t base = currentFrame * SLOT_WORDS + ARENA_BRICK_POOL_BASE_WORD;

    uint64_t occupancy = 0;
    for (uint32_t z = 0; z < 4; z++)
    {
        for (uint32_t y = 0; y < 4; y++)
        {
            for (uint32_t x = 0; x < 4; x++)
            {
                const float fx = static_cast<float>(x) - 1.5f;
                const float fy = static_cast<float>(y) - 1.5f;
                const float fz = static_cast<float>(z) - 1.5f;
                const float radius2 = fx * fx + fy * fy + fz * fz;
                if (radius2 <= 2.6f)
                {
                    const uint32_t bitIndex = x + y * 4 + z * 16;
                    occupancy |= (1ull << bitIndex);
                }
            }
        }
    }

    dataBufferWords[base + 0] = static_cast<uint32_t>(occupancy & 0xFFFFFFFFull);
    dataBufferWords[base + 1] = static_cast<uint32_t>(occupancy >> 32);
    dataBufferWords[base + 2] = std::bit_cast<uint32_t>(TEST_BRICK_MIN_X);
    dataBufferWords[base + 3] = std::bit_cast<uint32_t>(TEST_BRICK_MIN_Y);
    dataBufferWords[base + 4] = std::bit_cast<uint32_t>(TEST_BRICK_MIN_Z);
    dataBufferWords[base + 5] = std::bit_cast<uint32_t>(TEST_BRICK_VOXEL_SIZE);
}

void RecordCommandBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, uint32_t imageIndex)
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

    const uint32_t groupCountX = (swapExtent.width + 7) / 8;
    const uint32_t groupCountY = (swapExtent.height + 7) / 8;
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

void DrawFrame(uint32_t currentFrame)
{
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    WriteArenaHeaderData(currentFrame);

    uint32_t imageIndex = 0;
    vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex);

    VkDescriptorImageInfo imageInfo{
        .imageView = swapImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSets[currentFrame],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfo,
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    RecordCommandBuffer(commandBuffers[currentFrame], descriptorSets[currentFrame], imageIndex);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAvailableSemaphores[currentFrame],
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffers[currentFrame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinishedSemaphores[currentFrame],
    };

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);

    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphores[currentFrame],
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
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

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
        constexpr VkInstanceCreateFlags instanceCreateFlags = PORTABILITY_ENUMERATE_FLAG;
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
        VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = DATA_BUFFER_SIZE,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(device, &bufferInfo, nullptr, &dataBuffer);

        VkMemoryRequirements memReqs{};
        vkGetBufferMemoryRequirements(device, dataBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = FindMemoryTypeIndex(
                memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkAllocateMemory(device, &allocInfo, nullptr, &dataBufferMemory);
        vkBindBufferMemory(device, dataBuffer, dataBufferMemory, 0);
        void *mapped = nullptr;
        vkMapMemory(device, dataBufferMemory, 0, DATA_BUFFER_SIZE, 0, &mapped);
        dataBufferWords = static_cast<uint32_t *>(mapped);

        for (uint32_t i = 0; i < DATA_WORD_COUNT; i++)
        {
            dataBufferWords[i] = 0;
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            WriteArenaHeaderData(i);
            WriteBrickData(i);
        }
    }

    {
        VkDescriptorSetLayoutBinding imageBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        };
        VkDescriptorSetLayoutBinding bufferBinding{
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        };
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            imageBinding,
            bufferBinding,
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings = bindings.data(),
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

        VkDescriptorPoolSize imagePoolSize{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = MAX_FRAMES_IN_FLIGHT,
        };
        VkDescriptorPoolSize bufferPoolSize{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = MAX_FRAMES_IN_FLIGHT,
        };
        std::array<VkDescriptorPoolSize, 2> poolSizes{
            imagePoolSize,
            bufferPoolSize,
        };
        VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = MAX_FRAMES_IN_FLIGHT,
            .poolSizeCount = 2,
            .pPoolSizes = poolSizes.data(),
        };
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

        std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> setLayouts{};
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            setLayouts[i] = descriptorSetLayout;
        }

        VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
            .pSetLayouts = setLayouts.data(),
        };
        vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data());

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorBufferInfo bufferInfo{
                .buffer = dataBuffer,
                .offset = static_cast<VkDeviceSize>(i * SLOT_WORDS * sizeof(uint32_t)),
                .range = static_cast<VkDeviceSize>(SLOT_WORDS * sizeof(uint32_t)),
            };
            VkWriteDescriptorSet bufferWrite{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo = nullptr,
                .pBufferInfo = &bufferInfo,
                .pTexelBufferView = nullptr,
            };
            vkUpdateDescriptorSets(device, 1, &bufferWrite, 0, nullptr);
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

        VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]);
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
        }
    }

    uint32_t currentFrame = 0;
    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        UpdateFlightCamera();
        DrawFrame(currentFrame);
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(device);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    for (uint32_t i = 0; i < swapImageCount; i++)
    {
        vkDestroyImageView(device, swapImageViews[i], nullptr);
    }

    vkUnmapMemory(device, dataBufferMemory);
    vkDestroyBuffer(device, dataBuffer, nullptr);
    vkFreeMemory(device, dataBufferMemory, nullptr);

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    window = nullptr;

    glfwTerminate();
    return 0;
}
