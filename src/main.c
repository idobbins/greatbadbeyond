#define _CRT_SECURE_NO_WARNINGS
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#if !defined(NDEBUG)
    #define VULKAN_ENABLE_DEBUG 1
#else
    #define VULKAN_ENABLE_DEBUG 0
#endif

static const char *const VULKAN_VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation",
};

static const char *const VULKAN_REQUIRED_DEVICE_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    #define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#define VULKAN_MAX_ENABLED_EXTENSIONS 16
#define VULKAN_MAX_ENABLED_LAYERS ARRAY_SIZE(VULKAN_VALIDATION_LAYERS)
#define VULKAN_MAX_ENUMERATED_EXTENSIONS 256
#define VULKAN_MAX_ENUMERATED_LAYERS 64
#define VULKAN_MAX_PHYSICAL_DEVICES 16
#define VULKAN_MAX_QUEUE_FAMILIES 16
#define VULKAN_MAX_DEVICE_EXTENSIONS 256
#define VULKAN_INVALID_QUEUE_FAMILY UINT32_MAX

static const char *const APPLICATION_TITLE = "Callandor";

// Logging --------------------------------------------------------------------

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

// Global State ---------------------------------------------------------------

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
        VkQueue graphicsQueue;
        VkQueue presentQueue;
        VkQueue computeQueue;
        uint32_t graphicsQueueFamily;
        uint32_t presentQueueFamily;
        uint32_t computeQueueFamily;

        bool instanceReady;
        bool debugMessengerReady;
        bool surfaceReady;
        bool physicalDeviceReady;
        bool deviceReady;
        bool queuesReady;

        bool ready;
        bool debugEnabled;
        bool validationLayersEnabled;
        bool portabilitySubsetRequired;

    } Vulkan;
} GlobalData;

static GlobalData GLOBAL = { 0 };

// GLFW / Window --------------------------------------------------------------

static void GlfwErrorCallback(int32_t code, const char *desc)
{
    const char *message = (desc != NULL) ? desc : "no description";
    LogError("[glfw][%d] %s", code, message);
}

void InitGlfwContext(void)
{
    glfwSetErrorCallback(GlfwErrorCallback);

    Assert(glfwInit() == true, "Failed to initialize GLFW");
    Assert(glfwVulkanSupported() == true, "Vulkan is not supported");

    GLOBAL.Glfw.ready = true;
    GLOBAL.Glfw.vulkanSupported = true;

    LogInfo("GLFW initialized (Vulkan supported)");
}

void CloseGlfwContext(void)
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

void InitWindow(void)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    GLOBAL.Window.title = APPLICATION_TITLE;
    GLOBAL.Window.window = glfwCreateWindow(1280, 720, GLOBAL.Window.title, NULL, NULL);
    Assert(GLOBAL.Window.window != NULL, "Failed to create window");

    GLOBAL.Window.ready = true;
}

void CloseWindow(void)
{
    if (!GLOBAL.Window.ready)
    {
        return;
    }

    glfwDestroyWindow(GLOBAL.Window.window);
    GLOBAL.Window.window = NULL;
    GLOBAL.Window.ready = false;
}

bool IsWindowReady(void)
{
    return GLOBAL.Window.ready;
}

bool WindowShouldClose(void)
{
    Assert(GLOBAL.Window.ready, "Window is not ready");
    return glfwWindowShouldClose(GLOBAL.Window.window);
}

// Vulkan Helpers -------------------------------------------------------------

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

static void PushUniqueUint32(uint32_t *list, uint32_t *count, uint32_t capacity, uint32_t value)
{
    for (uint32_t index = 0; index < *count; index++)
    {
        if (list[index] == value)
        {
            return;
        }
    }

    Assert(*count < capacity, "Too many Vulkan queue families requested");
    list[(*count)++] = value;
}

static bool VulkanExtensionListHas(const VkExtensionProperties *extensions, uint32_t count, const char *name)
{
    for (uint32_t index = 0; index < count; index++)
    {
        if (strcmp(extensions[index].extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool VulkanLayerListHas(const VkLayerProperties *layers, uint32_t count, const char *name)
{
    for (uint32_t index = 0; index < count; index++)
    {
        if (strcmp(layers[index].layerName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

// Vulkan Instance ------------------------------------------------------------

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

static uint32_t VulkanEnumerateInstanceExtensions(VkExtensionProperties *buffer, uint32_t capacity)
{
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    Assert((result == VK_SUCCESS) || (result == VK_INCOMPLETE), "Failed to query Vulkan instance extensions");
    Assert(count <= capacity, "Too many Vulkan instance extensions for buffer");

    result = vkEnumerateInstanceExtensionProperties(NULL, &count, buffer);
    Assert(result == VK_SUCCESS, "Failed to enumerate Vulkan instance extensions");

    return count;
}

static uint32_t VulkanEnumerateInstanceLayers(VkLayerProperties *buffer, uint32_t capacity)
{
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceLayerProperties(&count, NULL);
    Assert((result == VK_SUCCESS) || (result == VK_INCOMPLETE), "Failed to query Vulkan instance layers");
    Assert(count <= capacity, "Too many Vulkan instance layers for buffer");

    result = vkEnumerateInstanceLayerProperties(&count, buffer);
    Assert(result == VK_SUCCESS, "Failed to enumerate Vulkan instance layers");

    return count;
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

    VkExtensionProperties availableExtensions[VULKAN_MAX_ENUMERATED_EXTENSIONS];
    const uint32_t availableExtensionCount = VulkanEnumerateInstanceExtensions(availableExtensions, ARRAY_SIZE(availableExtensions));

    for (uint32_t index = 0; index < requiredExtensionCount; index++)
    {
        const char *name = requiredExtensions[index];
        if (!VulkanExtensionListHas(availableExtensions, availableExtensionCount, name))
        {
            LogError("Missing required Vulkan instance extension reported by GLFW: %s", name);
            Assert(false, "Missing GLFW-required Vulkan instance extension");
        }
        PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), name);
    }

    if (requestDebug)
    {
        const bool debugExtensionAvailable = VulkanExtensionListHas(availableExtensions, availableExtensionCount, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (debugExtensionAvailable)
        {
            PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            config.debugExtensionEnabled = true;
        }
        else
        {
            LogWarn("VK_EXT_debug_utils not available; continuing without debug messenger");
        }
    }

    const bool portabilityExtensionAvailable = VulkanExtensionListHas(availableExtensions, availableExtensionCount, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (portabilityExtensionAvailable)
    {
        PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        config.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    Assert(config.extensionCount > 0, "No Vulkan instance extensions configured");

    if (requestDebug)
    {
        VkLayerProperties availableLayers[VULKAN_MAX_ENUMERATED_LAYERS];
        const uint32_t availableLayerCount = VulkanEnumerateInstanceLayers(availableLayers, ARRAY_SIZE(availableLayers));

        const bool debugLayerAvailable = VulkanLayerListHas(availableLayers, availableLayerCount, VULKAN_VALIDATION_LAYERS[0]);
        if (debugLayerAvailable)
        {
            config.layers[config.layerCount++] = VULKAN_VALIDATION_LAYERS[0];
        }
        else
        {
            LogWarn("Vulkan validation layer not available; continuing without validation");
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

    GLOBAL.Vulkan.instanceReady = true;
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
        GLOBAL.Vulkan.debugMessengerReady = true;
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

    GLOBAL.Vulkan.surfaceReady = true;
}

static void VulkanResetState(void)
{
    GLOBAL.Vulkan.instance = VK_NULL_HANDLE;
    GLOBAL.Vulkan.debugMessenger = VK_NULL_HANDLE;
    GLOBAL.Vulkan.surface = VK_NULL_HANDLE;
    GLOBAL.Vulkan.physicalDevice = VK_NULL_HANDLE;
    GLOBAL.Vulkan.device = VK_NULL_HANDLE;
    GLOBAL.Vulkan.graphicsQueue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.presentQueue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.computeQueue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.graphicsQueueFamily = VULKAN_INVALID_QUEUE_FAMILY;
    GLOBAL.Vulkan.presentQueueFamily = VULKAN_INVALID_QUEUE_FAMILY;
    GLOBAL.Vulkan.computeQueueFamily = VULKAN_INVALID_QUEUE_FAMILY;

    GLOBAL.Vulkan.instanceReady = false;
    GLOBAL.Vulkan.debugMessengerReady = false;
    GLOBAL.Vulkan.surfaceReady = false;
    GLOBAL.Vulkan.physicalDeviceReady = false;
    GLOBAL.Vulkan.deviceReady = false;
    GLOBAL.Vulkan.queuesReady = false;

    GLOBAL.Vulkan.ready = false;
    GLOBAL.Vulkan.debugEnabled = false;
    GLOBAL.Vulkan.validationLayersEnabled = false;
    GLOBAL.Vulkan.portabilitySubsetRequired = false;
}

// Vulkan Device --------------------------------------------------------------

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

static uint32_t VulkanEnumerateDeviceExtensions(VkPhysicalDevice device, VkExtensionProperties *buffer, uint32_t capacity)
{
    uint32_t count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    Assert((result == VK_SUCCESS) || (result == VK_INCOMPLETE), "Failed to query Vulkan device extensions");
    Assert(count <= capacity, "Too many Vulkan device extensions for buffer");

    result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, buffer);
    Assert(result == VK_SUCCESS, "Failed to enumerate Vulkan device extensions");

    return count;
}

static uint32_t VulkanGetQueueFamilyProperties(VkPhysicalDevice device, VkQueueFamilyProperties *buffer, uint32_t capacity)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    Assert(count > 0, "Vulkan physical device reports zero queue families");
    Assert(count <= capacity, "Too many Vulkan queue families for buffer");

    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, buffer);
    return count;
}

static bool VulkanCheckDeviceExtensions(VkPhysicalDevice device, bool *requiresPortabilitySubset)
{
    VkExtensionProperties extensions[VULKAN_MAX_DEVICE_EXTENSIONS];
    const uint32_t extensionCount = VulkanEnumerateDeviceExtensions(device, extensions, ARRAY_SIZE(extensions));

    bool requiredFound[ARRAY_SIZE(VULKAN_REQUIRED_DEVICE_EXTENSIONS)] = { false };
    bool portabilitySubsetPresent = false;

    for (uint32_t requiredIndex = 0; requiredIndex < ARRAY_SIZE(VULKAN_REQUIRED_DEVICE_EXTENSIONS); requiredIndex++)
    {
        const char *name = VULKAN_REQUIRED_DEVICE_EXTENSIONS[requiredIndex];
        bool found = false;
        for (uint32_t availableIndex = 0; availableIndex < extensionCount; availableIndex++)
        {
            if (strcmp(extensions[availableIndex].extensionName, name) == 0)
            {
                found = true;
                break;
            }
        }
        requiredFound[requiredIndex] = found;
    }

    for (uint32_t availableIndex = 0; availableIndex < extensionCount; availableIndex++)
    {
        if (strcmp(extensions[availableIndex].extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) == 0)
        {
            portabilitySubsetPresent = true;
            break;
        }
    }

    bool allRequired = true;
    for (uint32_t index = 0; index < ARRAY_SIZE(requiredFound); index++)
    {
        if (!requiredFound[index])
        {
            allRequired = false;
            break;
        }
    }

    if (requiresPortabilitySubset != NULL)
    {
        *requiresPortabilitySubset = portabilitySubsetPresent;
    }

    if (!allRequired)
    {
        return false;
    }

    return true;
}

static bool VulkanFindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t *graphicsFamily, uint32_t *presentFamily, uint32_t *computeFamily)
{
    VkQueueFamilyProperties queueFamilies[VULKAN_MAX_QUEUE_FAMILIES];
    const uint32_t queueFamilyCount = VulkanGetQueueFamilyProperties(device, queueFamilies, ARRAY_SIZE(queueFamilies));

    bool graphicsFound = false;
    bool presentFound = false;
    bool computeFound = false;
    bool computeDedicated = false;

    for (uint32_t index = 0; index < queueFamilyCount; index++)
    {
        const VkQueueFamilyProperties *family = &queueFamilies[index];

        if (!graphicsFound && (family->queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && family->queueCount > 0)
        {
            *graphicsFamily = index;
            graphicsFound = true;
        }

        if (!presentFound && family->queueCount > 0)
        {
            VkBool32 supported = VK_FALSE;
            VkResult presentResult = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &supported);
            Assert(presentResult == VK_SUCCESS, "Failed to query Vulkan surface support");
            if (supported == VK_TRUE)
            {
                *presentFamily = index;
                presentFound = true;
            }
        }

        if (family->queueCount > 0 && (family->queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
        {
            const bool familyHasGraphics = (family->queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (!computeFound || (!familyHasGraphics && !computeDedicated))
            {
                *computeFamily = index;
                computeFound = true;
                computeDedicated = !familyHasGraphics;
            }
        }

        if (graphicsFound && presentFound)
        {
            if (computeFound && computeDedicated)
            {
                break;
            }
        }
    }

    return (graphicsFound && presentFound && computeFound);
}

static void VulkanSelectPhysicalDevice(void)
{
    if (GLOBAL.Vulkan.physicalDeviceReady)
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

        bool requiresPortabilitySubset = false;
        if (!VulkanCheckDeviceExtensions(candidate, &requiresPortabilitySubset))
        {
            LogWarn("Skipping Vulkan physical device: %s (missing required extensions)", properties.deviceName);
            continue;
        }

        uint32_t graphicsFamily = VULKAN_INVALID_QUEUE_FAMILY;
        uint32_t presentFamily = VULKAN_INVALID_QUEUE_FAMILY;
        uint32_t computeFamily = VULKAN_INVALID_QUEUE_FAMILY;
        if (!VulkanFindQueueFamilies(candidate, GLOBAL.Vulkan.surface, &graphicsFamily, &presentFamily, &computeFamily))
        {
            LogWarn("Skipping Vulkan physical device: %s (missing graphics/present/compute queues)", properties.deviceName);
            continue;
        }

        GLOBAL.Vulkan.physicalDevice = candidate;
        GLOBAL.Vulkan.graphicsQueueFamily = graphicsFamily;
        GLOBAL.Vulkan.presentQueueFamily = presentFamily;
        GLOBAL.Vulkan.computeQueueFamily = computeFamily;
        GLOBAL.Vulkan.physicalDeviceReady = true;
        GLOBAL.Vulkan.portabilitySubsetRequired = requiresPortabilitySubset;

        LogInfo("Selected Vulkan physical device: %s", properties.deviceName);
        return;
    }

    Assert(false, "Failed to find a suitable Vulkan physical device");
}

static void VulkanCreateLogicalDevice(void)
{
    if (GLOBAL.Vulkan.deviceReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.physicalDeviceReady, "Vulkan physical device is not selected");
    Assert(GLOBAL.Vulkan.graphicsQueueFamily != VULKAN_INVALID_QUEUE_FAMILY, "Vulkan graphics queue family is invalid");
    Assert(GLOBAL.Vulkan.presentQueueFamily != VULKAN_INVALID_QUEUE_FAMILY, "Vulkan presentation queue family is invalid");
    Assert(GLOBAL.Vulkan.computeQueueFamily != VULKAN_INVALID_QUEUE_FAMILY, "Vulkan compute queue family is invalid");

    uint32_t queueFamilies[3] = { 0 };
    uint32_t queueFamilyCount = 0;
    PushUniqueUint32(queueFamilies, &queueFamilyCount, ARRAY_SIZE(queueFamilies), GLOBAL.Vulkan.graphicsQueueFamily);
    PushUniqueUint32(queueFamilies, &queueFamilyCount, ARRAY_SIZE(queueFamilies), GLOBAL.Vulkan.presentQueueFamily);
    PushUniqueUint32(queueFamilies, &queueFamilyCount, ARRAY_SIZE(queueFamilies), GLOBAL.Vulkan.computeQueueFamily);
    Assert(queueFamilyCount > 0, "No queue families selected for Vulkan logical device");

    VkDeviceQueueCreateInfo queueCreateInfos[ARRAY_SIZE(queueFamilies)];
    memset(queueCreateInfos, 0, sizeof(queueCreateInfos));
    const float queuePriority = 1.0f;
    for (uint32_t index = 0; index < queueFamilyCount; index++)
    {
        queueCreateInfos[index].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfos[index].queueFamilyIndex = queueFamilies[index];
        queueCreateInfos[index].queueCount = 1;
        queueCreateInfos[index].pQueuePriorities = &queuePriority;
    }

    VkPhysicalDeviceFeatures deviceFeatures = { 0 };

    const char *enabledDeviceExtensions[VULKAN_MAX_ENABLED_EXTENSIONS] = { 0 };
    uint32_t enabledDeviceExtensionCount = 0;
    for (uint32_t index = 0; index < ARRAY_SIZE(VULKAN_REQUIRED_DEVICE_EXTENSIONS); index++)
    {
        PushUniqueString(enabledDeviceExtensions, &enabledDeviceExtensionCount, ARRAY_SIZE(enabledDeviceExtensions), VULKAN_REQUIRED_DEVICE_EXTENSIONS[index]);
    }

    if (GLOBAL.Vulkan.portabilitySubsetRequired)
    {
        PushUniqueString(enabledDeviceExtensions, &enabledDeviceExtensionCount, ARRAY_SIZE(enabledDeviceExtensions), VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
        LogInfo("Enabling Vulkan device extension: %s", VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }

    Assert(enabledDeviceExtensionCount > 0, "No Vulkan device extensions configured");

    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queueFamilyCount,
        .pQueueCreateInfos = queueCreateInfos,
        .enabledExtensionCount = enabledDeviceExtensionCount,
        .ppEnabledExtensionNames = enabledDeviceExtensions,
        .pEnabledFeatures = &deviceFeatures,
    };

    if (GLOBAL.Vulkan.validationLayersEnabled)
    {
        createInfo.enabledLayerCount = ARRAY_SIZE(VULKAN_VALIDATION_LAYERS);
        createInfo.ppEnabledLayerNames = VULKAN_VALIDATION_LAYERS;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.ppEnabledLayerNames = NULL;
    }

    VkResult result = vkCreateDevice(GLOBAL.Vulkan.physicalDevice, &createInfo, NULL, &GLOBAL.Vulkan.device);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan logical device");

    vkGetDeviceQueue(GLOBAL.Vulkan.device, GLOBAL.Vulkan.graphicsQueueFamily, 0, &GLOBAL.Vulkan.graphicsQueue);
    vkGetDeviceQueue(GLOBAL.Vulkan.device, GLOBAL.Vulkan.presentQueueFamily, 0, &GLOBAL.Vulkan.presentQueue);
    vkGetDeviceQueue(GLOBAL.Vulkan.device, GLOBAL.Vulkan.computeQueueFamily, 0, &GLOBAL.Vulkan.computeQueue);

    GLOBAL.Vulkan.deviceReady = true;
    GLOBAL.Vulkan.queuesReady = true;

    LogInfo("Vulkan logical device ready");
}

// Vulkan Lifecycle -----------------------------------------------------------

void InitVulkan(void)
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

    const char *applicationTitle = (GLOBAL.Window.title != NULL) ? GLOBAL.Window.title : APPLICATION_TITLE;
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationTitle,
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName = "",
        .engineVersion = VK_MAKE_VERSION(0, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    VulkanCreateInstance(&instanceConfig, &appInfo);
    VulkanSetupDebugMessenger(instanceConfig.debugExtensionEnabled);
    VulkanCreateSurface();
    VulkanSelectPhysicalDevice();
    VulkanCreateLogicalDevice();

    GLOBAL.Vulkan.ready =
        (GLOBAL.Vulkan.instanceReady &&
        GLOBAL.Vulkan.surfaceReady &&
        GLOBAL.Vulkan.deviceReady &&
        GLOBAL.Vulkan.queuesReady);
    Assert(GLOBAL.Vulkan.ready, "Vulkan initialization incomplete");

    LogInfo("Vulkan initialization complete");
}

void CloseVulkan(void)
{
    if (!GLOBAL.Vulkan.instanceReady &&
        !GLOBAL.Vulkan.deviceReady &&
        !GLOBAL.Vulkan.surfaceReady &&
        !GLOBAL.Vulkan.debugMessengerReady)
    {
        return;
    }

    if (GLOBAL.Vulkan.deviceReady)
    {
        vkDeviceWaitIdle(GLOBAL.Vulkan.device);
        vkDestroyDevice(GLOBAL.Vulkan.device, NULL);
        GLOBAL.Vulkan.device = VK_NULL_HANDLE;
        GLOBAL.Vulkan.deviceReady = false;
    }

    GLOBAL.Vulkan.queuesReady = false;
    GLOBAL.Vulkan.graphicsQueue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.presentQueue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.computeQueue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.graphicsQueueFamily = VULKAN_INVALID_QUEUE_FAMILY;
    GLOBAL.Vulkan.presentQueueFamily = VULKAN_INVALID_QUEUE_FAMILY;
    GLOBAL.Vulkan.computeQueueFamily = VULKAN_INVALID_QUEUE_FAMILY;

    GLOBAL.Vulkan.physicalDevice = VK_NULL_HANDLE;
    GLOBAL.Vulkan.physicalDeviceReady = false;

    if (GLOBAL.Vulkan.surfaceReady)
    {
        vkDestroySurfaceKHR(GLOBAL.Vulkan.instance, GLOBAL.Vulkan.surface, NULL);
        GLOBAL.Vulkan.surface = VK_NULL_HANDLE;
        GLOBAL.Vulkan.surfaceReady = false;
    }

    if (GLOBAL.Vulkan.debugMessengerReady)
    {
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(GLOBAL.Vulkan.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyMessenger != NULL)
        {
            destroyMessenger(GLOBAL.Vulkan.instance, GLOBAL.Vulkan.debugMessenger, NULL);
        }
        GLOBAL.Vulkan.debugMessenger = VK_NULL_HANDLE;
        GLOBAL.Vulkan.debugMessengerReady = false;
    }

    if (GLOBAL.Vulkan.instanceReady)
    {
        vkDestroyInstance(GLOBAL.Vulkan.instance, NULL);
        GLOBAL.Vulkan.instance = VK_NULL_HANDLE;
        GLOBAL.Vulkan.instanceReady = false;
    }

    GLOBAL.Vulkan.ready = false;
    GLOBAL.Vulkan.debugEnabled = false;
    GLOBAL.Vulkan.validationLayersEnabled = false;
    GLOBAL.Vulkan.portabilitySubsetRequired = false;
}

// Entry Point ----------------------------------------------------------------

int main(void)
{
    InitGlfwContext();
    InitWindow();
    InitVulkan();

    while (!WindowShouldClose())
    {
        glfwPollEvents();
    }

    CloseVulkan();
    CloseWindow();
    CloseGlfwContext();

    return 0;
}
