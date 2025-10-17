#define _CRT_SECURE_NO_WARNINGS

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "vk_mem_alloc.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#if !defined(NDEBUG)
    #define VULKAN_ENABLE_DEBUG 1
#else
    #define VULKAN_ENABLE_DEBUG 0
#endif

static const char *const vulkanValidationLayers[] = {
    "VK_LAYER_KHRONOS_validation",
};

#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    #define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#define VULKAN_MAX_ENABLED_EXTENSIONS 16
#define VULKAN_MAX_ENABLED_LAYERS ARRAY_SIZE(vulkanValidationLayers)
#define VULKAN_MAX_PHYSICAL_DEVICES 16
#define VULKAN_MAX_SWAPCHAIN_IMAGES 8
#define VULKAN_MAX_SURFACE_FORMATS 64
#define VULKAN_MAX_PRESENT_MODES 16
#define VULKAN_MAX_SHADER_SIZE (1024 * 1024)
#define VULKAN_COMPUTE_LOCAL_SIZE 16
#define VULKAN_MAX_PATH_LENGTH 512

#ifndef VULKAN_SHADER_DIRECTORY
    #define VULKAN_SHADER_DIRECTORY "./shaders"
#endif

static const char *const defaultApplicationTitle = "Callandor";

// Provide logging helpers

static void LogWrite(FILE *stream, const char *prefix, const char *format, va_list args)
{
    fprintf(stream, "%s ", prefix);
    vfprintf(stream, format, args);
    fputc('\n', stream);
}

static void LogError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stderr, "error:", format, args);
    va_end(args);
}

static void LogWarn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stderr, "warn :", format, args);
    va_end(args);
}

static void LogInfo(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stdout, "info :", format, args);
    va_end(args);
}

static inline void Assert(bool condition, const char *message)
{
    if (!condition)
    {
        LogError("assert: %s", message);
        exit(EXIT_FAILURE);
    }
}

// Track global renderer state

typedef struct GlobalData {
    struct {
        bool ready;
        bool vulkanSupported;

    } Glfw;
    struct {
        const char *title;
        bool ready;
        GLFWwindow *window;

    } Window;
    struct {
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkSurfaceKHR surface;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkQueue queue;
        uint32_t queueFamily;
        VkSwapchainKHR swapchain;
        VkImage swapchainImages[VULKAN_MAX_SWAPCHAIN_IMAGES];
        VkImageView swapchainImageViews[VULKAN_MAX_SWAPCHAIN_IMAGES];
        uint32_t swapchainImageCount;
        VkFormat swapchainImageFormat;
        VkExtent2D swapchainExtent;
        VkShaderModule computeShaderModule;
        VkShaderModule blitVertexShaderModule;
        VkShaderModule blitFragmentShaderModule;
        VkDescriptorSetLayout descriptorSetLayout;
        VkDescriptorPool descriptorPool;
        VkDescriptorSet descriptorSet;
        VkPipelineLayout computePipelineLayout;
        VkPipelineLayout blitPipelineLayout;
        VkPipeline computePipeline;
        VkPipeline blitPipeline;
        VmaAllocator vma;
        VkCommandPool commandPool;
        VkCommandBuffer commandBuffer;
        VkImage gradientImage;
        VmaAllocation gradientAlloc;
        VkImageView gradientImageView;
        VkSampler gradientSampler;
        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphores[VULKAN_MAX_SWAPCHAIN_IMAGES];
        VkFence frameFence;

        bool gradientInitialized;

        bool ready;
        bool debugEnabled;
        bool validationLayersEnabled;

    } Vulkan;
} GlobalData;

static GlobalData GLOBAL = { 0 };

// Manage GLFW and window lifecycle

static void GlfwErrorCallback(int32_t code, const char *desc)
{
    const char *message = (desc != NULL) ? desc : "no description";
    LogError("[glfw][%d] %s", code, message);
}

static void InitGlfwContext(void)
{
    glfwSetErrorCallback(GlfwErrorCallback);

    Assert(glfwInit() == true, "Failed to initialize GLFW");
    Assert(glfwVulkanSupported() == true, "Vulkan is not supported");

    GLOBAL.Glfw.ready = true;
    GLOBAL.Glfw.vulkanSupported = true;

    LogInfo("GLFW initialized (Vulkan supported)");
}

static void CloseGlfwContext(void)
{
    if (!GLOBAL.Glfw.ready)
    {
        return;
    }

    glfwTerminate();
    glfwSetErrorCallback(NULL);

    GLOBAL.Glfw.ready = false;
    GLOBAL.Glfw.vulkanSupported = false;
}

static void InitWindow(void)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    GLOBAL.Window.title = defaultApplicationTitle;
    GLOBAL.Window.window = glfwCreateWindow(1280, 720, GLOBAL.Window.title, NULL, NULL);
    Assert(GLOBAL.Window.window != NULL, "Failed to create window");

    GLOBAL.Window.ready = true;
}

static void CloseWindow(void)
{
    if (!GLOBAL.Window.ready)
    {
        return;
    }

    glfwDestroyWindow(GLOBAL.Window.window);
    GLOBAL.Window.window = NULL;
    GLOBAL.Window.ready = false;
}

static bool IsWindowReady(void)
{
    return GLOBAL.Window.ready;
}

static bool WindowShouldClose(void)
{
    Assert(IsWindowReady(), "Window is not ready");
    return glfwWindowShouldClose(GLOBAL.Window.window);
}

// Provide Vulkan helper utilities

static void PushUniqueString(const char **list, uint32_t *count, uint32_t capacity, const char *value)
{
    for (uint32_t index = 0; index < *count; index++)
    {
        if (strcmp(list[index], value) == 0)
        {
            return;
        }
    }

    Assert(*count < capacity, "Too many Vulkan instance entries requested");
    list[(*count)++] = value;
}

static void VulkanBuildShaderPath(const char *name, char *buffer, uint32_t capacity)
{
    Assert(name != NULL, "Shader name is null");
    Assert(buffer != NULL, "Shader path buffer is null");
    Assert(capacity > 0, "Shader path buffer capacity is zero");

    int written = snprintf(buffer, capacity, "%s/%s", VULKAN_SHADER_DIRECTORY, name);
    Assert(written > 0, "Failed to compose shader path");
    Assert((uint32_t)written < capacity, "Shader path buffer overflow");
}

static uint32_t VulkanReadBinaryFile(const char *path, uint8_t *buffer, uint32_t capacity)
{
    Assert(path != NULL, "File path is null");
    Assert(buffer != NULL, "File buffer is null");
    Assert(capacity > 0, "File buffer capacity is zero");

    FILE *file = fopen(path, "rb");
    Assert(file != NULL, "Failed to open file");

    int seekResult = fseek(file, 0, SEEK_END);
    Assert(seekResult == 0, "Failed to seek file end");
    long size = ftell(file);
    Assert(size >= 0, "Failed to query file size");
    Assert((uint32_t)size <= capacity, "File size exceeds buffer capacity");
    seekResult = fseek(file, 0, SEEK_SET);
    Assert(seekResult == 0, "Failed to seek file start");

    size_t readCount = fread(buffer, 1, (size_t)size, file);
    Assert(readCount == (size_t)size, "Failed to read file");

    int closeResult = fclose(file);
    Assert(closeResult == 0, "Failed to close file");

    return (uint32_t)size;
}

static VkShaderModule VulkanLoadShaderModule(const char *filename)
{
    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan device is not ready");

    char path[VULKAN_MAX_PATH_LENGTH];
    VulkanBuildShaderPath(filename, path, ARRAY_SIZE(path));

    uint8_t shaderData[VULKAN_MAX_SHADER_SIZE];
    uint32_t shaderSize = VulkanReadBinaryFile(path, shaderData, sizeof(shaderData));
    Assert(shaderSize > 0, "Shader file is empty");
    Assert((shaderSize % 4) == 0, "Shader file size is not aligned to 4 bytes");

    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderSize,
        .pCode = (const uint32_t *)shaderData,
    };

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(GLOBAL.Vulkan.device, &createInfo, NULL, &module);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan shader module");

    return module;
}

// Handle Vulkan instance setup

typedef struct VulkanInstanceConfig {
    const char *extensions[VULKAN_MAX_ENABLED_EXTENSIONS];
    uint32_t extensionCount;
    const char *layers[VULKAN_MAX_ENABLED_LAYERS];
    uint32_t layerCount;
    VkInstanceCreateFlags flags;
    bool debugExtensionEnabled;
} VulkanInstanceConfig;

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
    void *userData)
{
    (void)messageType;
    (void)userData;

    const char *message = (callbackData != NULL && callbackData->pMessage != NULL) ? callbackData->pMessage : "no message";

    if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
    {
        LogError("[vulkan] %s", message);
    }
    else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
    {
        LogWarn("[vulkan] %s", message);
    }
    else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
    {
        LogInfo("[vulkan] %s", message);
    }
    else
    {
        LogInfo("[vulkan][verbose] %s", message);
    }

    return VK_FALSE;
}

static VkDebugUtilsMessengerCreateInfoEXT VulkanMakeDebugMessengerCreateInfo(void)
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = VulkanDebugCallback,
        .pUserData = NULL,
    };

    return createInfo;
}

static VulkanInstanceConfig VulkanBuildInstanceConfig(bool requestDebug)
{
    VulkanInstanceConfig config = {
        .extensions = { 0 },
        .extensionCount = 0,
        .layers = { 0 },
        .layerCount = 0,
        .flags = 0,
        .debugExtensionEnabled = false,
    };

    uint32_t requiredExtensionCount = 0;
    const char **requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredExtensionCount);
    Assert(requiredExtensions != NULL, "glfwGetRequiredInstanceExtensions returned NULL");
    Assert(requiredExtensionCount > 0, "GLFW did not report any required Vulkan instance extensions");

    for (uint32_t index = 0; index < requiredExtensionCount; index++)
    {
        const char *name = requiredExtensions[index];
        PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), name);
    }

    if (requestDebug)
    {
        PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        config.debugExtensionEnabled = true;
    }

    #if defined(__APPLE__)
        PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        config.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    #endif

    Assert(config.extensionCount > 0, "No Vulkan instance extensions configured");

    if (requestDebug)
    {
        for (uint32_t index = 0; index < ARRAY_SIZE(vulkanValidationLayers); index++)
        {
            PushUniqueString(config.layers, &config.layerCount, ARRAY_SIZE(config.layers), vulkanValidationLayers[index]);
        }
    }

    return config;
}

static void VulkanCreateInstance(const VulkanInstanceConfig *config, const VkApplicationInfo *appInfo)
{
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 0 };
    const void *next = NULL;
    if (config->debugExtensionEnabled)
    {
        debugCreateInfo = VulkanMakeDebugMessengerCreateInfo();
        next = &debugCreateInfo;
    }

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = next,
        .pApplicationInfo = appInfo,
        .flags = config->flags,
        .enabledExtensionCount = config->extensionCount,
        .ppEnabledExtensionNames = (config->extensionCount > 0) ? config->extensions : NULL,
        .enabledLayerCount = config->layerCount,
        .ppEnabledLayerNames = (config->layerCount > 0) ? config->layers : NULL,
    };

    VkResult result = vkCreateInstance(&createInfo, NULL, &GLOBAL.Vulkan.instance);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan instance");

    GLOBAL.Vulkan.validationLayersEnabled = (config->layerCount > 0);
}

static void VulkanSetupDebugMessenger(bool debugExtensionEnabled)
{
    if (!debugExtensionEnabled)
    {
        return;
    }

    PFN_vkCreateDebugUtilsMessengerEXT createMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(GLOBAL.Vulkan.instance, "vkCreateDebugUtilsMessengerEXT");

    if (createMessenger == NULL)
    {
        LogWarn("vkCreateDebugUtilsMessengerEXT not available; debug messenger disabled");
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo = VulkanMakeDebugMessengerCreateInfo();
    VkResult result = createMessenger(GLOBAL.Vulkan.instance, &createInfo, NULL, &GLOBAL.Vulkan.debugMessenger);
    if (result == VK_SUCCESS)
    {
        GLOBAL.Vulkan.debugEnabled = true;
    }
    else
    {
        LogWarn("Failed to create Vulkan debug messenger (error %d)", result);
    }
}

static void VulkanCreateSurface(void)
{
    VkResult surfaceResult = glfwCreateWindowSurface(GLOBAL.Vulkan.instance, GLOBAL.Window.window, NULL, &GLOBAL.Vulkan.surface);
    Assert(surfaceResult == VK_SUCCESS, "Failed to create Vulkan surface");
}

static void VulkanResetState(void)
{
    GLOBAL.Vulkan.instance = VK_NULL_HANDLE;
    GLOBAL.Vulkan.debugMessenger = VK_NULL_HANDLE;
    GLOBAL.Vulkan.surface = VK_NULL_HANDLE;
    GLOBAL.Vulkan.physicalDevice = VK_NULL_HANDLE;
    GLOBAL.Vulkan.device = VK_NULL_HANDLE;
    GLOBAL.Vulkan.queue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.queueFamily = UINT32_MAX;
    GLOBAL.Vulkan.swapchain = VK_NULL_HANDLE;
    memset(GLOBAL.Vulkan.swapchainImages, 0, sizeof(GLOBAL.Vulkan.swapchainImages));
    memset(GLOBAL.Vulkan.swapchainImageViews, 0, sizeof(GLOBAL.Vulkan.swapchainImageViews));
    GLOBAL.Vulkan.swapchainImageCount = 0;
    GLOBAL.Vulkan.swapchainImageFormat = VK_FORMAT_UNDEFINED;
    GLOBAL.Vulkan.swapchainExtent.width = 0;
    GLOBAL.Vulkan.swapchainExtent.height = 0;
    GLOBAL.Vulkan.computeShaderModule = VK_NULL_HANDLE;
    GLOBAL.Vulkan.blitVertexShaderModule = VK_NULL_HANDLE;
    GLOBAL.Vulkan.blitFragmentShaderModule = VK_NULL_HANDLE;
    GLOBAL.Vulkan.descriptorSetLayout = VK_NULL_HANDLE;
    GLOBAL.Vulkan.descriptorPool = VK_NULL_HANDLE;
    GLOBAL.Vulkan.descriptorSet = VK_NULL_HANDLE;
    GLOBAL.Vulkan.computePipelineLayout = VK_NULL_HANDLE;
    GLOBAL.Vulkan.blitPipelineLayout = VK_NULL_HANDLE;
    GLOBAL.Vulkan.computePipeline = VK_NULL_HANDLE;
    GLOBAL.Vulkan.blitPipeline = VK_NULL_HANDLE;
    GLOBAL.Vulkan.vma = NULL;
    GLOBAL.Vulkan.commandPool = VK_NULL_HANDLE;
    GLOBAL.Vulkan.commandBuffer = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientImage = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientAlloc = NULL;
    GLOBAL.Vulkan.gradientImageView = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientSampler = VK_NULL_HANDLE;
    GLOBAL.Vulkan.imageAvailableSemaphore = VK_NULL_HANDLE;
    memset(GLOBAL.Vulkan.renderFinishedSemaphores, 0, sizeof(GLOBAL.Vulkan.renderFinishedSemaphores));
    GLOBAL.Vulkan.frameFence = VK_NULL_HANDLE;

    GLOBAL.Vulkan.gradientInitialized = false;

    GLOBAL.Vulkan.ready = false;
    GLOBAL.Vulkan.debugEnabled = false;
    GLOBAL.Vulkan.validationLayersEnabled = false;
}

// Manage Vulkan device resources

static uint32_t VulkanEnumeratePhysicalDevices(VkInstance instance, VkPhysicalDevice *buffer, uint32_t capacity)
{
    uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, NULL);
    Assert(result == VK_SUCCESS, "Failed to query Vulkan physical devices");
    Assert(count > 0, "No Vulkan physical devices available");
    Assert(count <= capacity, "Too many Vulkan physical devices for buffer");

    result = vkEnumeratePhysicalDevices(instance, &count, buffer);
    Assert(result == VK_SUCCESS, "Failed to enumerate Vulkan physical devices");

    return count;
}

static bool FindUniversalQueue(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t *family)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    Assert(count > 0, "Vulkan physical device reports zero queue families");
    VkQueueFamilyProperties props[16];
    if (count > ARRAY_SIZE(props))
    {
        count = ARRAY_SIZE(props);
    }
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

    for (uint32_t index = 0; index < count; index++)
    {
        VkBool32 present = VK_FALSE;
        VkResult presentResult = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
        Assert(presentResult == VK_SUCCESS, "Failed to query Vulkan surface support");

        if ((present == VK_TRUE) &&
            ((props[index].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0) &&
            props[index].queueCount > 0)
        {
            *family = index;
            return true;
        }
    }

    return false;
}

static void VulkanSelectPhysicalDevice(void)
{
    if (GLOBAL.Vulkan.physicalDevice != VK_NULL_HANDLE)
    {
        return;
    }

    VkPhysicalDevice devices[VULKAN_MAX_PHYSICAL_DEVICES];
    const uint32_t deviceCount = VulkanEnumeratePhysicalDevices(GLOBAL.Vulkan.instance, devices, ARRAY_SIZE(devices));

    for (uint32_t index = 0; index < deviceCount; index++)
    {
        VkPhysicalDevice candidate = devices[index];
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(candidate, &properties);

        uint32_t universalQueueFamily = UINT32_MAX;
        if (!FindUniversalQueue(candidate, GLOBAL.Vulkan.surface, &universalQueueFamily))
        {
            LogWarn("Skipping Vulkan physical device: %s (no universal queue)", properties.deviceName);
            continue;
        }

        GLOBAL.Vulkan.physicalDevice = candidate;
        GLOBAL.Vulkan.queueFamily = universalQueueFamily;

        LogInfo("Selected Vulkan physical device: %s", properties.deviceName);
        return;
    }

    Assert(false, "Failed to find a suitable Vulkan physical device");
}

static void VulkanCreateLogicalDevice(void)
{
    if (GLOBAL.Vulkan.device != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.physicalDevice != VK_NULL_HANDLE, "Vulkan physical device is not selected");
    Assert(GLOBAL.Vulkan.queueFamily != UINT32_MAX, "Vulkan queue family is invalid");

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = GLOBAL.Vulkan.queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkPhysicalDeviceFeatures deviceFeatures = { 0 };

    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features13,
    };

    vkGetPhysicalDeviceFeatures2(GLOBAL.Vulkan.physicalDevice, &features2);
    Assert((features13.dynamicRendering == VK_TRUE) && (features13.synchronization2 == VK_TRUE), "Vulkan 1.3 features missing");

    const char *enabledDeviceExtensions[VULKAN_MAX_ENABLED_EXTENSIONS] = { 0 };
    uint32_t enabledDeviceExtensionCount = 0;
    PushUniqueString(enabledDeviceExtensions, &enabledDeviceExtensionCount, ARRAY_SIZE(enabledDeviceExtensions), VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    #if defined(__APPLE__)
        PushUniqueString(enabledDeviceExtensions, &enabledDeviceExtensionCount, ARRAY_SIZE(enabledDeviceExtensions), VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    #endif

    Assert(enabledDeviceExtensionCount > 0, "No Vulkan device extensions configured");

    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = enabledDeviceExtensionCount,
        .ppEnabledExtensionNames = enabledDeviceExtensions,
        .pEnabledFeatures = &deviceFeatures,
        .pNext = &features13,
    };

    if (GLOBAL.Vulkan.validationLayersEnabled)
    {
        createInfo.enabledLayerCount = ARRAY_SIZE(vulkanValidationLayers);
        createInfo.ppEnabledLayerNames = vulkanValidationLayers;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.ppEnabledLayerNames = NULL;
    }

    VkResult result = vkCreateDevice(GLOBAL.Vulkan.physicalDevice, &createInfo, NULL, &GLOBAL.Vulkan.device);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan logical device");

    vkGetDeviceQueue(GLOBAL.Vulkan.device, GLOBAL.Vulkan.queueFamily, 0, &GLOBAL.Vulkan.queue);

    LogInfo("Vulkan logical device ready");
}

// Manage Vulkan swapchain resources

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
        Assert(presentModeResult == VK_SUCCESS, "Failed to enumerate Vulkan surface present modes");
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

    for (uint32_t index = 0; index < count; index++)
    {
        if (presentModes[index] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
    }

    for (uint32_t index = 0; index < count; index++)
    {
        if (presentModes[index] == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
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

typedef struct VulkanComputePushConstants {
    uint32_t width;
    uint32_t height;
} VulkanComputePushConstants;

static void VulkanCreateCommandPool(void)
{
    if (GLOBAL.Vulkan.commandPool != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");
    Assert(GLOBAL.Vulkan.queueFamily != UINT32_MAX, "Vulkan queue family is invalid");

    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = GLOBAL.Vulkan.queueFamily,
    };

    VkResult result = vkCreateCommandPool(GLOBAL.Vulkan.device, &poolInfo, NULL, &GLOBAL.Vulkan.commandPool);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan command pool");

    LogInfo("Vulkan command pool ready");
}

static void VulkanDestroyCommandPool(void)
{
    if (GLOBAL.Vulkan.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(GLOBAL.Vulkan.device, GLOBAL.Vulkan.commandPool, NULL);
        GLOBAL.Vulkan.commandPool = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.commandBuffer = VK_NULL_HANDLE;
}

static void VulkanAllocateCommandBuffer(void)
{
    if (GLOBAL.Vulkan.commandBuffer != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.commandPool != VK_NULL_HANDLE, "Vulkan command pool is not ready");

    VkCommandBufferAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = GLOBAL.Vulkan.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkResult result = vkAllocateCommandBuffers(GLOBAL.Vulkan.device, &allocateInfo, &GLOBAL.Vulkan.commandBuffer);
    Assert(result == VK_SUCCESS, "Failed to allocate Vulkan command buffer");

    LogInfo("Vulkan command buffer ready");
}

static void VulkanCreateSyncObjects(void)
{
    if ((GLOBAL.Vulkan.imageAvailableSemaphore != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.frameFence != VK_NULL_HANDLE))
    {
        return;
    }

    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");

    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkResult result = vkCreateSemaphore(GLOBAL.Vulkan.device, &semaphoreInfo, NULL, &GLOBAL.Vulkan.imageAvailableSemaphore);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan semaphore");

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    result = vkCreateFence(GLOBAL.Vulkan.device, &fenceInfo, NULL, &GLOBAL.Vulkan.frameFence);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan fence");

    LogInfo("Vulkan synchronization objects ready");
}

static void VulkanDestroySyncObjects(void)
{
    if (GLOBAL.Vulkan.frameFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(GLOBAL.Vulkan.device, GLOBAL.Vulkan.frameFence, NULL);
        GLOBAL.Vulkan.frameFence = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.imageAvailableSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(GLOBAL.Vulkan.device, GLOBAL.Vulkan.imageAvailableSemaphore, NULL);
        GLOBAL.Vulkan.imageAvailableSemaphore = VK_NULL_HANDLE;
    }
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

static void VulkanCreateShaderModules(void)
{
    if ((GLOBAL.Vulkan.computeShaderModule != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.blitVertexShaderModule != VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.blitFragmentShaderModule != VK_NULL_HANDLE))
    {
        return;
    }

    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");

    GLOBAL.Vulkan.computeShaderModule = VulkanLoadShaderModule("compute.spv");
    GLOBAL.Vulkan.blitVertexShaderModule = VulkanLoadShaderModule("blit.vert.spv");
    GLOBAL.Vulkan.blitFragmentShaderModule = VulkanLoadShaderModule("blit.frag.spv");

    LogInfo("Vulkan shader modules ready");
}

static void VulkanDestroyShaderModules(void)
{
    if (GLOBAL.Vulkan.computeShaderModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.computeShaderModule, NULL);
        GLOBAL.Vulkan.computeShaderModule = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.blitVertexShaderModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.blitVertexShaderModule, NULL);
        GLOBAL.Vulkan.blitVertexShaderModule = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.blitFragmentShaderModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(GLOBAL.Vulkan.device, GLOBAL.Vulkan.blitFragmentShaderModule, NULL);
        GLOBAL.Vulkan.blitFragmentShaderModule = VK_NULL_HANDLE;
    }
}

static void VulkanCreateDescriptorSetLayout(void)
{
    if (GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");

    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_SIZE(bindings),
        .pBindings = bindings,
    };

    VkResult result = vkCreateDescriptorSetLayout(GLOBAL.Vulkan.device, &layoutInfo, NULL, &GLOBAL.Vulkan.descriptorSetLayout);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan descriptor set layout");

    LogInfo("Vulkan descriptor set layout ready");
}

static void VulkanDestroyDescriptorSetLayout(void)
{
    if (GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(GLOBAL.Vulkan.device, GLOBAL.Vulkan.descriptorSetLayout, NULL);
        GLOBAL.Vulkan.descriptorSetLayout = VK_NULL_HANDLE;
    }
}

static void VulkanCreateDescriptorPool(void)
{
    if (GLOBAL.Vulkan.descriptorPool != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");

    VkDescriptorPoolSize poolSizes[2] = {
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
        },
    };

    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1,
        .poolSizeCount = ARRAY_SIZE(poolSizes),
        .pPoolSizes = poolSizes,
    };

    VkResult result = vkCreateDescriptorPool(GLOBAL.Vulkan.device, &poolInfo, NULL, &GLOBAL.Vulkan.descriptorPool);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan descriptor pool");

    LogInfo("Vulkan descriptor pool ready");
}

static void VulkanDestroyDescriptorPool(void)
{
    if (GLOBAL.Vulkan.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(GLOBAL.Vulkan.device, GLOBAL.Vulkan.descriptorPool, NULL);
        GLOBAL.Vulkan.descriptorPool = VK_NULL_HANDLE;
    }
    GLOBAL.Vulkan.descriptorSet = VK_NULL_HANDLE;
}

static void VulkanAllocateDescriptorSet(void)
{
    if (GLOBAL.Vulkan.descriptorSet != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.descriptorPool != VK_NULL_HANDLE, "Vulkan descriptor pool is not ready");
    Assert(GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE, "Vulkan descriptor set layout is not ready");

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = GLOBAL.Vulkan.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
    };

    VkResult result = vkAllocateDescriptorSets(GLOBAL.Vulkan.device, &allocInfo, &GLOBAL.Vulkan.descriptorSet);
    Assert(result == VK_SUCCESS, "Failed to allocate Vulkan descriptor set");

    LogInfo("Vulkan descriptor set ready");
}

static void VulkanUpdateDescriptorSet(void)
{
    Assert(GLOBAL.Vulkan.descriptorSet != VK_NULL_HANDLE, "Vulkan descriptor set is not allocated");
    Assert(GLOBAL.Vulkan.gradientImageView != VK_NULL_HANDLE, "Vulkan gradient image view is not ready");
    Assert(GLOBAL.Vulkan.gradientSampler != VK_NULL_HANDLE, "Vulkan gradient sampler is not ready");

    VkDescriptorImageInfo storageInfo = {
        .imageView = GLOBAL.Vulkan.gradientImageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    VkDescriptorImageInfo samplerInfo = {
        .sampler = GLOBAL.Vulkan.gradientSampler,
        .imageView = GLOBAL.Vulkan.gradientImageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &storageInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = GLOBAL.Vulkan.descriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &samplerInfo,
        },
    };

    vkUpdateDescriptorSets(GLOBAL.Vulkan.device, ARRAY_SIZE(writes), writes, 0, NULL);
}

static void VulkanCreateGradientResources(void)
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

    VulkanUpdateDescriptorSet();

    LogInfo("Vulkan gradient image ready");
}

static void VulkanDestroyGradientResources(void)
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

static void VulkanCreatePipelines(void)
{
    const bool computeReady = (GLOBAL.Vulkan.computePipeline != VK_NULL_HANDLE);
    const bool blitReady = (GLOBAL.Vulkan.blitPipeline != VK_NULL_HANDLE);
    if (computeReady && blitReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.computeShaderModule != VK_NULL_HANDLE, "Vulkan compute shader module is not ready");
    Assert(GLOBAL.Vulkan.blitVertexShaderModule != VK_NULL_HANDLE, "Vulkan blit vertex shader module is not ready");
    Assert(GLOBAL.Vulkan.blitFragmentShaderModule != VK_NULL_HANDLE, "Vulkan blit fragment shader module is not ready");
    Assert(GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE, "Vulkan descriptor set layout is not ready");

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(VulkanComputePushConstants),
    };

    if (GLOBAL.Vulkan.computePipelineLayout == VK_NULL_HANDLE)
    {
        VkPipelineLayoutCreateInfo computeLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushRange,
        };

        VkResult layoutResult = vkCreatePipelineLayout(GLOBAL.Vulkan.device, &computeLayoutInfo, NULL, &GLOBAL.Vulkan.computePipelineLayout);
        Assert(layoutResult == VK_SUCCESS, "Failed to create Vulkan compute pipeline layout");
    }

    if (GLOBAL.Vulkan.blitPipelineLayout == VK_NULL_HANDLE)
    {
        VkPipelineLayoutCreateInfo blitLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
        };

        VkResult blitLayoutResult = vkCreatePipelineLayout(GLOBAL.Vulkan.device, &blitLayoutInfo, NULL, &GLOBAL.Vulkan.blitPipelineLayout);
        Assert(blitLayoutResult == VK_SUCCESS, "Failed to create Vulkan blit pipeline layout");
    }

    if (!computeReady)
    {
        VkPipelineShaderStageCreateInfo computeStage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = GLOBAL.Vulkan.computeShaderModule,
            .pName = "main",
        };

        VkComputePipelineCreateInfo computeInfo = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = computeStage,
            .layout = GLOBAL.Vulkan.computePipelineLayout,
        };

        VkResult computeResult = vkCreateComputePipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &computeInfo, NULL, &GLOBAL.Vulkan.computePipeline);
        Assert(computeResult == VK_SUCCESS, "Failed to create Vulkan compute pipeline");
    }

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = GLOBAL.Vulkan.blitVertexShaderModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = GLOBAL.Vulkan.blitFragmentShaderModule,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)GLOBAL.Vulkan.swapchainExtent.width,
        .height = (float)GLOBAL.Vulkan.swapchainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = GLOBAL.Vulkan.swapchainExtent,
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState colorAttachment = {
        .blendEnable = VK_FALSE,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
    };

    VkPipelineRenderingCreateInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &GLOBAL.Vulkan.swapchainImageFormat,
    };

    VkGraphicsPipelineCreateInfo graphicsInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = ARRAY_SIZE(shaderStages),
        .pStages = shaderStages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = NULL,
        .pColorBlendState = &colorBlend,
        .pDynamicState = NULL,
        .layout = GLOBAL.Vulkan.blitPipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
    };

    if (!blitReady)
    {
        VkResult graphicsResult = vkCreateGraphicsPipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &graphicsInfo, NULL, &GLOBAL.Vulkan.blitPipeline);
        Assert(graphicsResult == VK_SUCCESS, "Failed to create Vulkan blit pipeline");
    }

    LogInfo("Vulkan pipelines ready");
}

static void VulkanDestroyPipelines(void)
{
    if (GLOBAL.Vulkan.blitPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.blitPipeline, NULL);
        GLOBAL.Vulkan.blitPipeline = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.computePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GLOBAL.Vulkan.device, GLOBAL.Vulkan.computePipeline, NULL);
        GLOBAL.Vulkan.computePipeline = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.blitPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(GLOBAL.Vulkan.device, GLOBAL.Vulkan.blitPipelineLayout, NULL);
        GLOBAL.Vulkan.blitPipelineLayout = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.computePipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(GLOBAL.Vulkan.device, GLOBAL.Vulkan.computePipelineLayout, NULL);
        GLOBAL.Vulkan.computePipelineLayout = VK_NULL_HANDLE;
    }
}

static void VulkanDestroySwapchainResources(void)
{
    VulkanDestroyPipelines();
    VulkanDestroyGradientResources();
}

static void VulkanCreateSwapchainResources(void)
{
    Assert(GLOBAL.Vulkan.swapchain != VK_NULL_HANDLE, "Vulkan swapchain is not ready");

    VulkanCreateGradientResources();
    VulkanCreatePipelines();
}

static void VulkanCreateDeviceResources(void)
{
    VulkanCreateCommandPool();
    VulkanAllocateCommandBuffer();
    VulkanCreateSyncObjects();
    VulkanCreateShaderModules();
    VulkanCreateDescriptorSetLayout();
    VulkanCreateDescriptorPool();
    VulkanAllocateDescriptorSet();

    if (GLOBAL.Vulkan.vma == NULL)
    {
        VmaAllocatorCreateInfo vmaInfo = {
            .physicalDevice = GLOBAL.Vulkan.physicalDevice,
            .device = GLOBAL.Vulkan.device,
            .instance = GLOBAL.Vulkan.instance,
            .vulkanApiVersion = VK_API_VERSION_1_3,
        };

        VkResult allocatorResult = vmaCreateAllocator(&vmaInfo, &GLOBAL.Vulkan.vma);
        Assert(allocatorResult == VK_SUCCESS, "Failed to create VMA allocator");
    }
}

static void VulkanDestroyDeviceResources(void)
{
    VulkanDestroySyncObjects();
    VulkanDestroyDescriptorPool();
    VulkanDestroyDescriptorSetLayout();
    VulkanDestroyShaderModules();
    VulkanDestroyCommandPool();
    VulkanDestroySwapchainSemaphores();

    if (GLOBAL.Vulkan.vma != NULL)
    {
        vmaDestroyAllocator(GLOBAL.Vulkan.vma);
        GLOBAL.Vulkan.vma = NULL;
        GLOBAL.Vulkan.gradientAlloc = NULL;
    }
}

static void VulkanRecordFrameCommands(uint32_t imageIndex, VkExtent2D extent)
{
    Assert(GLOBAL.Vulkan.commandBuffer != VK_NULL_HANDLE, "Vulkan command buffer is not available");
    Assert(GLOBAL.Vulkan.computePipeline != VK_NULL_HANDLE, "Vulkan compute pipeline is not ready");
    Assert(GLOBAL.Vulkan.blitPipeline != VK_NULL_HANDLE, "Vulkan blit pipeline is not ready");
    Assert(GLOBAL.Vulkan.descriptorSet != VK_NULL_HANDLE, "Vulkan descriptor set is not ready");
    Assert(GLOBAL.Vulkan.gradientImage != VK_NULL_HANDLE, "Vulkan gradient image is not ready");
    Assert(GLOBAL.Vulkan.gradientImageView != VK_NULL_HANDLE, "Vulkan gradient image view is not ready");
    Assert(GLOBAL.Vulkan.computePipelineLayout != VK_NULL_HANDLE, "Vulkan compute pipeline layout is not ready");
    Assert(GLOBAL.Vulkan.blitPipelineLayout != VK_NULL_HANDLE, "Vulkan blit pipeline layout is not ready");
    Assert(GLOBAL.Vulkan.swapchainImageViews[imageIndex] != VK_NULL_HANDLE, "Vulkan swapchain image view is not ready");
    Assert(imageIndex < GLOBAL.Vulkan.swapchainImageCount, "Vulkan swapchain image index out of range");

    VkResult resetResult = vkResetCommandBuffer(GLOBAL.Vulkan.commandBuffer, 0);
    Assert(resetResult == VK_SUCCESS, "Failed to reset Vulkan command buffer");

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult beginResult = vkBeginCommandBuffer(GLOBAL.Vulkan.commandBuffer, &beginInfo);
    Assert(beginResult == VK_SUCCESS, "Failed to begin Vulkan command buffer");

    VkImageMemoryBarrier2 toGeneral = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = GLOBAL.Vulkan.gradientInitialized ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask = GLOBAL.Vulkan.gradientInitialized ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .oldLayout = GLOBAL.Vulkan.gradientInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = GLOBAL.Vulkan.gradientImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo toGeneralDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toGeneral,
    };

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &toGeneralDependency);

    vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.computePipeline);
    vkCmdBindDescriptorSets(
        GLOBAL.Vulkan.commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        GLOBAL.Vulkan.computePipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);

    VulkanComputePushConstants pushConstants = {
        .width = extent.width,
        .height = extent.height,
    };

    vkCmdPushConstants(
        GLOBAL.Vulkan.commandBuffer,
        GLOBAL.Vulkan.computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);

    const uint32_t groupCountX = (extent.width + VULKAN_COMPUTE_LOCAL_SIZE - 1) / VULKAN_COMPUTE_LOCAL_SIZE;
    const uint32_t groupCountY = (extent.height + VULKAN_COMPUTE_LOCAL_SIZE - 1) / VULKAN_COMPUTE_LOCAL_SIZE;

    vkCmdDispatch(GLOBAL.Vulkan.commandBuffer, groupCountX, groupCountY, 1);

    VkImageMemoryBarrier2 toRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = GLOBAL.Vulkan.gradientImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo toReadDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toRead,
    };

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &toReadDependency);

    VkImageMemoryBarrier2 swapchainPre = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image = GLOBAL.Vulkan.swapchainImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo swapchainPreDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &swapchainPre,
    };

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &swapchainPreDependency);

    VkClearValue clearColor = {
        .color = { { 0.0f, 0.0f, 0.0f, 1.0f } },
    };

    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = GLOBAL.Vulkan.swapchainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = extent,
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(GLOBAL.Vulkan.commandBuffer, &renderingInfo);
    vkCmdBindPipeline(GLOBAL.Vulkan.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GLOBAL.Vulkan.blitPipeline);
    vkCmdBindDescriptorSets(
        GLOBAL.Vulkan.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        GLOBAL.Vulkan.blitPipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);

    vkCmdDraw(GLOBAL.Vulkan.commandBuffer, 3, 1, 0, 0);
    vkCmdEndRendering(GLOBAL.Vulkan.commandBuffer);

    VkImageMemoryBarrier2 swapchainPost = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = GLOBAL.Vulkan.swapchainImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkDependencyInfo swapchainPostDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &swapchainPost,
    };

    vkCmdPipelineBarrier2(GLOBAL.Vulkan.commandBuffer, &swapchainPostDependency);

    VkResult endResult = vkEndCommandBuffer(GLOBAL.Vulkan.commandBuffer);
    Assert(endResult == VK_SUCCESS, "Failed to record Vulkan frame command buffer");

    GLOBAL.Vulkan.gradientInitialized = true;
}

static void VulkanRecreateSwapchain(void);

static void VulkanDrawFrame(void)
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
        VulkanRecreateSwapchain();
        return;
    }

    Assert((acquireResult == VK_SUCCESS) || (acquireResult == VK_SUBOPTIMAL_KHR), "Failed to acquire Vulkan swapchain image");

    VulkanRecordFrameCommands(imageIndex, extent);

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
        VulkanRecreateSwapchain();
        return;
    }

    Assert(presentResult == VK_SUCCESS, "Failed to present Vulkan swapchain image");
    (void)imageIndex;
}

static void VulkanDestroySwapchain(void)
{
    VulkanDestroySwapchainResources();

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

static void VulkanCreateSwapchain(void)
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
    VulkanCreateSwapchainResources();
    VulkanRefreshReadyState();

    LogInfo("Vulkan swapchain ready: %u images (%ux%u)", GLOBAL.Vulkan.swapchainImageCount, extent.width, extent.height);
}

static void VulkanRecreateSwapchain(void)
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
    VulkanDestroySwapchain();
    VulkanCreateSwapchain();
}

// Manage Vulkan lifecycle

static void InitVulkan(void)
{
    if (GLOBAL.Vulkan.ready)
    {
        return;
    }

    Assert(GLOBAL.Glfw.ready, "GLFW is not initialized");
    Assert(GLOBAL.Glfw.vulkanSupported, "Vulkan is not supported");
    Assert(GLOBAL.Window.ready, "Window is not created");

    VulkanResetState();

    const bool requestDebug = (VULKAN_ENABLE_DEBUG != 0);
    VulkanInstanceConfig instanceConfig = VulkanBuildInstanceConfig(requestDebug);

    const char *applicationTitle = (GLOBAL.Window.title != NULL) ? GLOBAL.Window.title : defaultApplicationTitle;
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationTitle,
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName = "",
        .engineVersion = VK_MAKE_VERSION(0, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VulkanCreateInstance(&instanceConfig, &appInfo);
    VulkanSetupDebugMessenger(instanceConfig.debugExtensionEnabled);
    VulkanCreateSurface();
    VulkanSelectPhysicalDevice();
    VulkanCreateLogicalDevice();
    VulkanCreateDeviceResources();
    VulkanCreateSwapchain();

    VulkanRefreshReadyState();
    Assert(GLOBAL.Vulkan.ready, "Vulkan initialization incomplete");

    LogInfo("Vulkan initialization complete");
}

static void CloseVulkan(void)
{
    if ((GLOBAL.Vulkan.instance == VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.device == VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.surface == VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.debugMessenger == VK_NULL_HANDLE))
    {
        return;
    }

    if (GLOBAL.Vulkan.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(GLOBAL.Vulkan.device);
        VulkanDestroySwapchain();
        VulkanDestroyDeviceResources();
        vkDestroyDevice(GLOBAL.Vulkan.device, NULL);
        GLOBAL.Vulkan.device = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.queue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.queueFamily = UINT32_MAX;

    GLOBAL.Vulkan.physicalDevice = VK_NULL_HANDLE;

    if (GLOBAL.Vulkan.surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(GLOBAL.Vulkan.instance, GLOBAL.Vulkan.surface, NULL);
        GLOBAL.Vulkan.surface = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.debugMessenger != VK_NULL_HANDLE)
    {
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(GLOBAL.Vulkan.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyMessenger != NULL)
        {
            destroyMessenger(GLOBAL.Vulkan.instance, GLOBAL.Vulkan.debugMessenger, NULL);
        }
        GLOBAL.Vulkan.debugMessenger = VK_NULL_HANDLE;
        GLOBAL.Vulkan.debugEnabled = false;
    }

    if (GLOBAL.Vulkan.instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(GLOBAL.Vulkan.instance, NULL);
        GLOBAL.Vulkan.instance = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.ready = false;
    GLOBAL.Vulkan.validationLayersEnabled = false;
}

// Provide application entry point

int main(void)
{
    InitGlfwContext();
    InitWindow();
    InitVulkan();

    while (!WindowShouldClose())
    {
        glfwPollEvents();
        VulkanDrawFrame();
    }

    CloseVulkan();
    CloseWindow();
    CloseGlfwContext();

    return 0;
}
