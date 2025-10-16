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
#define VULKAN_MAX_SWAPCHAIN_IMAGES 8
#define VULKAN_MAX_SURFACE_FORMATS 64
#define VULKAN_MAX_PRESENT_MODES 16
#define VULKAN_MAX_FRAMEBUFFERS VULKAN_MAX_SWAPCHAIN_IMAGES
#define VULKAN_MAX_SHADER_SIZE (1024 * 1024)
#define VULKAN_COMPUTE_LOCAL_SIZE 16
#define VULKAN_MAX_PATH_LENGTH 512
#define VULKAN_INVALID_QUEUE_FAMILY UINT32_MAX

#ifndef VULKAN_SHADER_DIRECTORY
    #define VULKAN_SHADER_DIRECTORY "./shaders"
#endif

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
        VkRenderPass renderPass;
        VkFramebuffer framebuffers[VULKAN_MAX_FRAMEBUFFERS];
        VkCommandPool computeCommandPool;
        VkCommandPool graphicsCommandPool;
        VkCommandBuffer computeCommandBuffer;
        VkCommandBuffer graphicsCommandBuffer;
        VkImage gradientImage;
        VkDeviceMemory gradientImageMemory;
        VkImageView gradientImageView;
        VkSampler gradientSampler;
        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphore;
        VkFence frameFence;

        bool instanceReady;
        bool debugMessengerReady;
        bool surfaceReady;
        bool physicalDeviceReady;
        bool deviceReady;
        bool queuesReady;
        bool swapchainReady;
        bool shaderModulesReady;
        bool descriptorSetLayoutReady;
        bool descriptorPoolReady;
        bool descriptorSetReady;
        bool descriptorSetUpdated;
        bool renderPassReady;
        bool pipelinesReady;
        bool framebuffersReady;
        bool commandPoolsReady;
        bool commandBuffersReady;
        bool gradientReady;
        bool gradientInitialized;
        bool syncReady;

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
    Assert(GLOBAL.Vulkan.deviceReady, "Vulkan device is not ready");

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
    GLOBAL.Vulkan.renderPass = VK_NULL_HANDLE;
    memset(GLOBAL.Vulkan.framebuffers, 0, sizeof(GLOBAL.Vulkan.framebuffers));
    GLOBAL.Vulkan.computeCommandPool = VK_NULL_HANDLE;
    GLOBAL.Vulkan.graphicsCommandPool = VK_NULL_HANDLE;
    GLOBAL.Vulkan.computeCommandBuffer = VK_NULL_HANDLE;
    GLOBAL.Vulkan.graphicsCommandBuffer = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientImage = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientImageMemory = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientImageView = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientSampler = VK_NULL_HANDLE;
    GLOBAL.Vulkan.imageAvailableSemaphore = VK_NULL_HANDLE;
    GLOBAL.Vulkan.renderFinishedSemaphore = VK_NULL_HANDLE;
    GLOBAL.Vulkan.frameFence = VK_NULL_HANDLE;

    GLOBAL.Vulkan.instanceReady = false;
    GLOBAL.Vulkan.debugMessengerReady = false;
    GLOBAL.Vulkan.surfaceReady = false;
    GLOBAL.Vulkan.physicalDeviceReady = false;
    GLOBAL.Vulkan.deviceReady = false;
    GLOBAL.Vulkan.queuesReady = false;
    GLOBAL.Vulkan.swapchainReady = false;
    GLOBAL.Vulkan.shaderModulesReady = false;
    GLOBAL.Vulkan.descriptorSetLayoutReady = false;
    GLOBAL.Vulkan.descriptorPoolReady = false;
    GLOBAL.Vulkan.descriptorSetReady = false;
    GLOBAL.Vulkan.descriptorSetUpdated = false;
    GLOBAL.Vulkan.renderPassReady = false;
    GLOBAL.Vulkan.pipelinesReady = false;
    GLOBAL.Vulkan.framebuffersReady = false;
    GLOBAL.Vulkan.commandPoolsReady = false;
    GLOBAL.Vulkan.commandBuffersReady = false;
    GLOBAL.Vulkan.gradientReady = false;
    GLOBAL.Vulkan.gradientInitialized = false;
    GLOBAL.Vulkan.syncReady = false;

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

static uint32_t VulkanFindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(GLOBAL.Vulkan.physicalDevice, &memoryProperties);

    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; index++)
    {
        const bool typeSupported = (typeBits & (1u << index)) != 0;
        const bool propertiesMatch = (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;
        if (typeSupported && propertiesMatch)
        {
            return index;
        }
    }

    Assert(false, "Failed to find suitable Vulkan memory type");
    return 0;
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

// Vulkan Swapchain -----------------------------------------------------------

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
        (GLOBAL.Vulkan.instanceReady &&
        GLOBAL.Vulkan.surfaceReady &&
        GLOBAL.Vulkan.deviceReady &&
        GLOBAL.Vulkan.queuesReady &&
        GLOBAL.Vulkan.swapchainReady);
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

static void VulkanCreateCommandPools(void)
{
    if (GLOBAL.Vulkan.commandPoolsReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.deviceReady, "Vulkan logical device is not ready");
    Assert(GLOBAL.Vulkan.computeQueueFamily != VULKAN_INVALID_QUEUE_FAMILY, "Vulkan compute queue family is invalid");
    Assert(GLOBAL.Vulkan.graphicsQueueFamily != VULKAN_INVALID_QUEUE_FAMILY, "Vulkan graphics queue family is invalid");

    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = GLOBAL.Vulkan.computeQueueFamily,
    };

    VkResult result = vkCreateCommandPool(GLOBAL.Vulkan.device, &poolInfo, NULL, &GLOBAL.Vulkan.computeCommandPool);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan compute command pool");

    poolInfo.queueFamilyIndex = GLOBAL.Vulkan.graphicsQueueFamily;
    result = vkCreateCommandPool(GLOBAL.Vulkan.device, &poolInfo, NULL, &GLOBAL.Vulkan.graphicsCommandPool);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan graphics command pool");

    GLOBAL.Vulkan.commandPoolsReady = true;

    LogInfo("Vulkan command pools ready");
}

static void VulkanDestroyCommandPools(void)
{
    if (GLOBAL.Vulkan.graphicsCommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(GLOBAL.Vulkan.device, GLOBAL.Vulkan.graphicsCommandPool, NULL);
        GLOBAL.Vulkan.graphicsCommandPool = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.computeCommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(GLOBAL.Vulkan.device, GLOBAL.Vulkan.computeCommandPool, NULL);
        GLOBAL.Vulkan.computeCommandPool = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.commandBuffersReady = false;
    GLOBAL.Vulkan.commandPoolsReady = false;
    GLOBAL.Vulkan.graphicsCommandBuffer = VK_NULL_HANDLE;
    GLOBAL.Vulkan.computeCommandBuffer = VK_NULL_HANDLE;
}

static void VulkanAllocateCommandBuffers(void)
{
    if (GLOBAL.Vulkan.commandBuffersReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.commandPoolsReady, "Vulkan command pools are not ready");

    VkCommandBufferAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    allocateInfo.commandPool = GLOBAL.Vulkan.computeCommandPool;
    VkResult result = vkAllocateCommandBuffers(GLOBAL.Vulkan.device, &allocateInfo, &GLOBAL.Vulkan.computeCommandBuffer);
    Assert(result == VK_SUCCESS, "Failed to allocate Vulkan compute command buffer");

    allocateInfo.commandPool = GLOBAL.Vulkan.graphicsCommandPool;
    result = vkAllocateCommandBuffers(GLOBAL.Vulkan.device, &allocateInfo, &GLOBAL.Vulkan.graphicsCommandBuffer);
    Assert(result == VK_SUCCESS, "Failed to allocate Vulkan graphics command buffer");

    GLOBAL.Vulkan.commandBuffersReady = true;

    LogInfo("Vulkan command buffers ready");
}

static void VulkanCreateSyncObjects(void)
{
    if (GLOBAL.Vulkan.syncReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.deviceReady, "Vulkan logical device is not ready");

    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkResult result = vkCreateSemaphore(GLOBAL.Vulkan.device, &semaphoreInfo, NULL, &GLOBAL.Vulkan.imageAvailableSemaphore);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan semaphore");

    result = vkCreateSemaphore(GLOBAL.Vulkan.device, &semaphoreInfo, NULL, &GLOBAL.Vulkan.renderFinishedSemaphore);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan semaphore");

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    result = vkCreateFence(GLOBAL.Vulkan.device, &fenceInfo, NULL, &GLOBAL.Vulkan.frameFence);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan fence");

    GLOBAL.Vulkan.syncReady = true;

    LogInfo("Vulkan synchronization objects ready");
}

static void VulkanDestroySyncObjects(void)
{
    if (GLOBAL.Vulkan.frameFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(GLOBAL.Vulkan.device, GLOBAL.Vulkan.frameFence, NULL);
        GLOBAL.Vulkan.frameFence = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.renderFinishedSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(GLOBAL.Vulkan.device, GLOBAL.Vulkan.renderFinishedSemaphore, NULL);
        GLOBAL.Vulkan.renderFinishedSemaphore = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.imageAvailableSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(GLOBAL.Vulkan.device, GLOBAL.Vulkan.imageAvailableSemaphore, NULL);
        GLOBAL.Vulkan.imageAvailableSemaphore = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.syncReady = false;
}

static void VulkanCreateShaderModules(void)
{
    if (GLOBAL.Vulkan.shaderModulesReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.deviceReady, "Vulkan logical device is not ready");

    GLOBAL.Vulkan.computeShaderModule = VulkanLoadShaderModule("compute.spv");
    GLOBAL.Vulkan.blitVertexShaderModule = VulkanLoadShaderModule("blit.vert.spv");
    GLOBAL.Vulkan.blitFragmentShaderModule = VulkanLoadShaderModule("blit.frag.spv");

    GLOBAL.Vulkan.shaderModulesReady = true;

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

    GLOBAL.Vulkan.shaderModulesReady = false;
}

static void VulkanCreateDescriptorSetLayout(void)
{
    if (GLOBAL.Vulkan.descriptorSetLayoutReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.deviceReady, "Vulkan logical device is not ready");

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

    GLOBAL.Vulkan.descriptorSetLayoutReady = true;

    LogInfo("Vulkan descriptor set layout ready");
}

static void VulkanDestroyDescriptorSetLayout(void)
{
    if (GLOBAL.Vulkan.descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(GLOBAL.Vulkan.device, GLOBAL.Vulkan.descriptorSetLayout, NULL);
        GLOBAL.Vulkan.descriptorSetLayout = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.descriptorSetLayoutReady = false;
}

static void VulkanCreateDescriptorPool(void)
{
    if (GLOBAL.Vulkan.descriptorPoolReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.deviceReady, "Vulkan logical device is not ready");

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

    GLOBAL.Vulkan.descriptorPoolReady = true;

    LogInfo("Vulkan descriptor pool ready");
}

static void VulkanDestroyDescriptorPool(void)
{
    if (GLOBAL.Vulkan.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(GLOBAL.Vulkan.device, GLOBAL.Vulkan.descriptorPool, NULL);
        GLOBAL.Vulkan.descriptorPool = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.descriptorPoolReady = false;
    GLOBAL.Vulkan.descriptorSetReady = false;
    GLOBAL.Vulkan.descriptorSetUpdated = false;
    GLOBAL.Vulkan.descriptorSet = VK_NULL_HANDLE;
}

static void VulkanAllocateDescriptorSet(void)
{
    if (GLOBAL.Vulkan.descriptorSetReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.descriptorPoolReady, "Vulkan descriptor pool is not ready");
    Assert(GLOBAL.Vulkan.descriptorSetLayoutReady, "Vulkan descriptor set layout is not ready");

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = GLOBAL.Vulkan.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
    };

    VkResult result = vkAllocateDescriptorSets(GLOBAL.Vulkan.device, &allocInfo, &GLOBAL.Vulkan.descriptorSet);
    Assert(result == VK_SUCCESS, "Failed to allocate Vulkan descriptor set");

    GLOBAL.Vulkan.descriptorSetReady = true;
    GLOBAL.Vulkan.descriptorSetUpdated = false;

    LogInfo("Vulkan descriptor set ready");
}

static void VulkanUpdateDescriptorSet(void)
{
    Assert(GLOBAL.Vulkan.descriptorSetReady, "Vulkan descriptor set is not allocated");
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
    GLOBAL.Vulkan.descriptorSetUpdated = true;
}

static void VulkanCreateGradientResources(void)
{
    if (GLOBAL.Vulkan.gradientReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.deviceReady, "Vulkan logical device is not ready");
    Assert(GLOBAL.Vulkan.swapchainReady, "Vulkan swapchain is not ready");
    Assert(GLOBAL.Vulkan.descriptorSetReady, "Vulkan descriptor set is not ready");

    const VkExtent2D extent = GLOBAL.Vulkan.swapchainExtent;
    Assert(extent.width > 0 && extent.height > 0, "Vulkan swapchain extent is invalid");

    uint32_t queueFamilyIndices[2] = {
        GLOBAL.Vulkan.computeQueueFamily,
        GLOBAL.Vulkan.graphicsQueueFamily,
    };

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
    };

    if (GLOBAL.Vulkan.computeQueueFamily != GLOBAL.Vulkan.graphicsQueueFamily)
    {
        imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        imageInfo.queueFamilyIndexCount = 2;
        imageInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult imageResult = vkCreateImage(GLOBAL.Vulkan.device, &imageInfo, NULL, &GLOBAL.Vulkan.gradientImage);
    Assert(imageResult == VK_SUCCESS, "Failed to create Vulkan gradient image");

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gradientImage, &requirements);

    VkMemoryAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = VulkanFindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    VkResult memoryResult = vkAllocateMemory(GLOBAL.Vulkan.device, &allocateInfo, NULL, &GLOBAL.Vulkan.gradientImageMemory);
    Assert(memoryResult == VK_SUCCESS, "Failed to allocate Vulkan gradient image memory");

    VkResult bindResult = vkBindImageMemory(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gradientImage, GLOBAL.Vulkan.gradientImageMemory, 0);
    Assert(bindResult == VK_SUCCESS, "Failed to bind Vulkan gradient image memory");

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

    GLOBAL.Vulkan.gradientReady = true;
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
        vkDestroyImage(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gradientImage, NULL);
        GLOBAL.Vulkan.gradientImage = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.gradientImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(GLOBAL.Vulkan.device, GLOBAL.Vulkan.gradientImageMemory, NULL);
        GLOBAL.Vulkan.gradientImageMemory = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.gradientReady = false;
    GLOBAL.Vulkan.gradientInitialized = false;
    GLOBAL.Vulkan.descriptorSetUpdated = false;
}

static void VulkanCreateRenderPass(void)
{
    if (GLOBAL.Vulkan.renderPassReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.swapchainReady, "Vulkan swapchain is not ready");

    VkAttachmentDescription colorAttachment = {
        .format = GLOBAL.Vulkan.swapchainImageFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference colorReference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorReference,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VkResult result = vkCreateRenderPass(GLOBAL.Vulkan.device, &renderPassInfo, NULL, &GLOBAL.Vulkan.renderPass);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan render pass");

    GLOBAL.Vulkan.renderPassReady = true;

    LogInfo("Vulkan render pass ready");
}

static void VulkanDestroyRenderPass(void)
{
    if (GLOBAL.Vulkan.renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(GLOBAL.Vulkan.device, GLOBAL.Vulkan.renderPass, NULL);
        GLOBAL.Vulkan.renderPass = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.renderPassReady = false;
}

static void VulkanCreateFramebuffers(void)
{
    if (GLOBAL.Vulkan.framebuffersReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.renderPassReady, "Vulkan render pass is not ready");
    Assert(GLOBAL.Vulkan.swapchainReady, "Vulkan swapchain is not ready");

    for (uint32_t index = 0; index < GLOBAL.Vulkan.swapchainImageCount; index++)
    {
        VkImageView attachment = GLOBAL.Vulkan.swapchainImageViews[index];
        VkFramebufferCreateInfo framebufferInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = GLOBAL.Vulkan.renderPass,
            .attachmentCount = 1,
            .pAttachments = &attachment,
            .width = GLOBAL.Vulkan.swapchainExtent.width,
            .height = GLOBAL.Vulkan.swapchainExtent.height,
            .layers = 1,
        };

        VkResult result = vkCreateFramebuffer(GLOBAL.Vulkan.device, &framebufferInfo, NULL, &GLOBAL.Vulkan.framebuffers[index]);
        Assert(result == VK_SUCCESS, "Failed to create Vulkan framebuffer");
    }

    GLOBAL.Vulkan.framebuffersReady = true;

    LogInfo("Vulkan framebuffers ready");
}

static void VulkanDestroyFramebuffers(void)
{
    for (uint32_t index = 0; index < ARRAY_SIZE(GLOBAL.Vulkan.framebuffers); index++)
    {
        if (GLOBAL.Vulkan.framebuffers[index] != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(GLOBAL.Vulkan.device, GLOBAL.Vulkan.framebuffers[index], NULL);
            GLOBAL.Vulkan.framebuffers[index] = VK_NULL_HANDLE;
        }
    }

    GLOBAL.Vulkan.framebuffersReady = false;
}

static void VulkanCreatePipelines(void)
{
    if (GLOBAL.Vulkan.pipelinesReady)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.shaderModulesReady, "Vulkan shader modules are not ready");
    Assert(GLOBAL.Vulkan.descriptorSetLayoutReady, "Vulkan descriptor set layout is not ready");
    Assert(GLOBAL.Vulkan.renderPassReady, "Vulkan render pass is not ready");

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(VulkanComputePushConstants),
    };

    VkPipelineLayoutCreateInfo computeLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };

    VkResult result = vkCreatePipelineLayout(GLOBAL.Vulkan.device, &computeLayoutInfo, NULL, &GLOBAL.Vulkan.computePipelineLayout);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan compute pipeline layout");

    VkPipelineLayoutCreateInfo blitLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &GLOBAL.Vulkan.descriptorSetLayout,
    };

    result = vkCreatePipelineLayout(GLOBAL.Vulkan.device, &blitLayoutInfo, NULL, &GLOBAL.Vulkan.blitPipelineLayout);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan blit pipeline layout");

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

    result = vkCreateComputePipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &computeInfo, NULL, &GLOBAL.Vulkan.computePipeline);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan compute pipeline");

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

    VkGraphicsPipelineCreateInfo graphicsInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
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
        .renderPass = GLOBAL.Vulkan.renderPass,
        .subpass = 0,
    };

    result = vkCreateGraphicsPipelines(GLOBAL.Vulkan.device, VK_NULL_HANDLE, 1, &graphicsInfo, NULL, &GLOBAL.Vulkan.blitPipeline);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan blit pipeline");

    GLOBAL.Vulkan.pipelinesReady = true;

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

    GLOBAL.Vulkan.pipelinesReady = false;
}

static void VulkanDestroySwapchainResources(void)
{
    VulkanDestroyPipelines();
    VulkanDestroyFramebuffers();
    VulkanDestroyGradientResources();
    VulkanDestroyRenderPass();
}

static void VulkanCreateSwapchainResources(void)
{
    Assert(GLOBAL.Vulkan.swapchainReady, "Vulkan swapchain is not ready");

    VulkanCreateRenderPass();
    VulkanCreateGradientResources();
    VulkanCreateFramebuffers();
    VulkanCreatePipelines();
}

static void VulkanCreateDeviceResources(void)
{
    VulkanCreateCommandPools();
    VulkanAllocateCommandBuffers();
    VulkanCreateSyncObjects();
    VulkanCreateShaderModules();
    VulkanCreateDescriptorSetLayout();
    VulkanCreateDescriptorPool();
    VulkanAllocateDescriptorSet();
}

static void VulkanDestroyDeviceResources(void)
{
    VulkanDestroySyncObjects();
    VulkanDestroyDescriptorPool();
    VulkanDestroyDescriptorSetLayout();
    VulkanDestroyShaderModules();
    VulkanDestroyCommandPools();
}

static void VulkanRecordComputeCommands(uint32_t width, uint32_t height)
{
    Assert(GLOBAL.Vulkan.commandBuffersReady, "Vulkan command buffers are not ready");
    Assert(GLOBAL.Vulkan.pipelinesReady, "Vulkan pipelines are not ready");
    Assert(GLOBAL.Vulkan.descriptorSetUpdated, "Vulkan descriptor set is not updated");

    VkResult resetResult = vkResetCommandBuffer(GLOBAL.Vulkan.computeCommandBuffer, 0);
    Assert(resetResult == VK_SUCCESS, "Failed to reset Vulkan compute command buffer");

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult beginResult = vkBeginCommandBuffer(GLOBAL.Vulkan.computeCommandBuffer, &beginInfo);
    Assert(beginResult == VK_SUCCESS, "Failed to begin Vulkan compute command buffer");

    VkImageMemoryBarrier toGeneral = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
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
        .srcAccessMask = GLOBAL.Vulkan.gradientInitialized ? VK_ACCESS_SHADER_READ_BIT : 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    };

    VkPipelineStageFlags srcStage = GLOBAL.Vulkan.gradientInitialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(
        GLOBAL.Vulkan.computeCommandBuffer,
        srcStage,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &toGeneral);

    vkCmdBindPipeline(GLOBAL.Vulkan.computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GLOBAL.Vulkan.computePipeline);
    vkCmdBindDescriptorSets(
        GLOBAL.Vulkan.computeCommandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        GLOBAL.Vulkan.computePipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);

    VulkanComputePushConstants pushConstants = {
        .width = width,
        .height = height,
    };

    vkCmdPushConstants(
        GLOBAL.Vulkan.computeCommandBuffer,
        GLOBAL.Vulkan.computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);

    const uint32_t groupCountX = (width + VULKAN_COMPUTE_LOCAL_SIZE - 1) / VULKAN_COMPUTE_LOCAL_SIZE;
    const uint32_t groupCountY = (height + VULKAN_COMPUTE_LOCAL_SIZE - 1) / VULKAN_COMPUTE_LOCAL_SIZE;

    vkCmdDispatch(GLOBAL.Vulkan.computeCommandBuffer, groupCountX, groupCountY, 1);

    VkImageMemoryBarrier toRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
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
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };

    vkCmdPipelineBarrier(
        GLOBAL.Vulkan.computeCommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &toRead);

    VkResult endResult = vkEndCommandBuffer(GLOBAL.Vulkan.computeCommandBuffer);
    Assert(endResult == VK_SUCCESS, "Failed to record Vulkan compute command buffer");

    GLOBAL.Vulkan.gradientInitialized = true;
}

static void VulkanRecordGraphicsCommands(uint32_t imageIndex)
{
    Assert(GLOBAL.Vulkan.commandBuffersReady, "Vulkan command buffers are not ready");
    Assert(GLOBAL.Vulkan.pipelinesReady, "Vulkan pipelines are not ready");
    Assert(GLOBAL.Vulkan.framebuffersReady, "Vulkan framebuffers are not ready");
    Assert(imageIndex < GLOBAL.Vulkan.swapchainImageCount, "Vulkan swapchain image index out of range");

    VkResult resetResult = vkResetCommandBuffer(GLOBAL.Vulkan.graphicsCommandBuffer, 0);
    Assert(resetResult == VK_SUCCESS, "Failed to reset Vulkan graphics command buffer");

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult beginResult = vkBeginCommandBuffer(GLOBAL.Vulkan.graphicsCommandBuffer, &beginInfo);
    Assert(beginResult == VK_SUCCESS, "Failed to begin Vulkan graphics command buffer");

    VkClearValue clearColor = {
        .color = { { 0.0f, 0.0f, 0.0f, 1.0f } },
    };

    VkRenderPassBeginInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = GLOBAL.Vulkan.renderPass,
        .framebuffer = GLOBAL.Vulkan.framebuffers[imageIndex],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = GLOBAL.Vulkan.swapchainExtent,
        },
        .clearValueCount = 1,
        .pClearValues = &clearColor,
    };

    vkCmdBeginRenderPass(GLOBAL.Vulkan.graphicsCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(GLOBAL.Vulkan.graphicsCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GLOBAL.Vulkan.blitPipeline);
    vkCmdBindDescriptorSets(
        GLOBAL.Vulkan.graphicsCommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        GLOBAL.Vulkan.blitPipelineLayout,
        0,
        1,
        &GLOBAL.Vulkan.descriptorSet,
        0,
        NULL);

    vkCmdDraw(GLOBAL.Vulkan.graphicsCommandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(GLOBAL.Vulkan.graphicsCommandBuffer);

    VkResult endResult = vkEndCommandBuffer(GLOBAL.Vulkan.graphicsCommandBuffer);
    Assert(endResult == VK_SUCCESS, "Failed to record Vulkan graphics command buffer");
}

void VulkanRecreateSwapchain(void);

static void VulkanDrawFrame(void)
{
    if (!GLOBAL.Vulkan.ready || !GLOBAL.Vulkan.swapchainReady)
    {
        return;
    }

    const VkExtent2D extent = GLOBAL.Vulkan.swapchainExtent;
    if (extent.width == 0 || extent.height == 0)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.syncReady, "Vulkan synchronization objects are not ready");

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

    VulkanRecordComputeCommands(extent.width, extent.height);

    VkSubmitInfo computeSubmit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &GLOBAL.Vulkan.computeCommandBuffer,
    };

    VkResult submitResult = vkQueueSubmit(GLOBAL.Vulkan.computeQueue, 1, &computeSubmit, VK_NULL_HANDLE);
    Assert(submitResult == VK_SUCCESS, "Failed to submit Vulkan compute commands");

    VkResult waitResult = vkQueueWaitIdle(GLOBAL.Vulkan.computeQueue);
    Assert(waitResult == VK_SUCCESS, "Failed to wait for Vulkan compute queue");

    VulkanRecordGraphicsCommands(imageIndex);

    VkSemaphore waitSemaphores[] = { GLOBAL.Vulkan.imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { GLOBAL.Vulkan.renderFinishedSemaphore };

    VkSubmitInfo graphicsSubmit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = ARRAY_SIZE(waitSemaphores),
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &GLOBAL.Vulkan.graphicsCommandBuffer,
        .signalSemaphoreCount = ARRAY_SIZE(signalSemaphores),
        .pSignalSemaphores = signalSemaphores,
    };

    submitResult = vkQueueSubmit(GLOBAL.Vulkan.graphicsQueue, 1, &graphicsSubmit, GLOBAL.Vulkan.frameFence);
    Assert(submitResult == VK_SUCCESS, "Failed to submit Vulkan graphics commands");

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1,
        .pSwapchains = &GLOBAL.Vulkan.swapchain,
        .pImageIndices = &imageIndex,
        .waitSemaphoreCount = ARRAY_SIZE(signalSemaphores),
        .pWaitSemaphores = signalSemaphores,
    };

    VkResult presentResult = vkQueuePresentKHR(GLOBAL.Vulkan.presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        VulkanRecreateSwapchain();
        return;
    }

    Assert(presentResult == VK_SUCCESS, "Failed to present Vulkan swapchain image");
}

static void VulkanDestroySwapchain(void)
{
    VulkanDestroySwapchainResources();

    if (!GLOBAL.Vulkan.swapchainReady)
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

    if (GLOBAL.Vulkan.swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(GLOBAL.Vulkan.device, GLOBAL.Vulkan.swapchain, NULL);
        GLOBAL.Vulkan.swapchain = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.swapchainExtent.width = 0;
    GLOBAL.Vulkan.swapchainExtent.height = 0;
    GLOBAL.Vulkan.swapchainImageFormat = VK_FORMAT_UNDEFINED;
    GLOBAL.Vulkan.swapchainReady = false;
    VulkanRefreshReadyState();

    LogInfo("Vulkan swapchain destroyed");
}

static void VulkanCreateSwapchain(void)
{
    Assert(GLOBAL.Vulkan.deviceReady, "Vulkan logical device is not ready");
    Assert(GLOBAL.Vulkan.surfaceReady, "Vulkan surface is not created");
    Assert(GLOBAL.Window.ready, "Window is not created");

    VulkanSwapchainSupport support;
    VulkanQuerySwapchainSupport(GLOBAL.Vulkan.physicalDevice, GLOBAL.Vulkan.surface, &support);
    Assert(support.formatCount > 0, "No Vulkan surface formats available");
    Assert(support.presentModeCount > 0, "No Vulkan present modes available");

    VkSurfaceFormatKHR surfaceFormat = VulkanChooseSurfaceFormat(support.formats, support.formatCount);
    VkPresentModeKHR presentMode = VulkanChoosePresentMode(support.presentModes, support.presentModeCount);
    VkExtent2D extent = VulkanChooseExtent(&support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
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

    uint32_t queueFamilyIndices[2] = {
        GLOBAL.Vulkan.graphicsQueueFamily,
        GLOBAL.Vulkan.presentQueueFamily,
    };

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = GLOBAL.Vulkan.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = transform,
        .compositeAlpha = compositeAlpha,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (GLOBAL.Vulkan.graphicsQueueFamily != GLOBAL.Vulkan.presentQueueFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

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
    GLOBAL.Vulkan.swapchainReady = true;
    VulkanCreateSwapchainResources();
    VulkanRefreshReadyState();

    LogInfo("Vulkan swapchain ready: %u images (%ux%u)", GLOBAL.Vulkan.swapchainImageCount, extent.width, extent.height);
}

void VulkanRecreateSwapchain(void)
{
    if (!GLOBAL.Vulkan.deviceReady || !GLOBAL.Vulkan.surfaceReady)
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
    VulkanCreateDeviceResources();
    VulkanCreateSwapchain();

    VulkanRefreshReadyState();
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
        VulkanDestroySwapchain();
        VulkanDestroyDeviceResources();
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
        VulkanDrawFrame();
    }

    CloseVulkan();
    CloseWindow();
    CloseGlfwContext();

    return 0;
}
