#define _CRT_SECURE_NO_WARNINGS
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define VULKAN_MAX_ENABLED_EXTENSIONS 16
#define VULKAN_MAX_ENABLED_LAYERS ARRAY_SIZE(VULKAN_VALIDATION_LAYERS)
#define VULKAN_MAX_ENUMERATED_EXTENSIONS 256
#define VULKAN_MAX_ENUMERATED_LAYERS 64

static const char *const APPLICATION_TITLE = "Callandor";

static inline void Assert(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "Runtime assertion failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void LogError(const char *message)
{
    fprintf(stderr, "error: %s\n", message);
}

static void LogWarn(const char *message)
{
    fprintf(stderr, "warn : %s\n", message);
}

static void LogInfo(const char *message)
{
    fprintf(stdout, "info : %s\n", message);
}

// Global state
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
        bool instanceReady;
        bool debugMessengerReady;
        bool surfaceReady;

        bool ready;
        bool debugEnabled;

    } Vulkan;
} GlobalData;

GlobalData GLOBAL = { 0 };

static void GlfwErrorCallback(int32_t code, const char *desc) {
    fprintf(stderr, "[glfw][error %d] %s\n", code, desc ? desc : "no description");
}

static const char *VulkanDebugSeverityLabel(VkDebugUtilsMessageSeverityFlagBitsEXT severity)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
    {
        return "error";
    }
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
    {
        return "warning";
    }
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
    {
        return "info";
    }
    return "verbose";
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
    void *userData)
{
    (void)messageType;
    (void)userData;

    const char *message = (callbackData && callbackData->pMessage) ? callbackData->pMessage : "no message";
    fprintf(stderr, "[vulkan][%s] %s\n", VulkanDebugSeverityLabel(messageSeverity), message);

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
}

void InitWindow(void)
{
    // apply window hints
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

void InitVulkan(void)
{
    if (GLOBAL.Vulkan.instanceReady)
    {
        return;
    }

    Assert(GLOBAL.Glfw.ready, "GLFW is not initialized");
    Assert(GLOBAL.Glfw.vulkanSupported, "Vulkan is not supported");
    Assert(GLOBAL.Window.ready, "Window is not created");

    const bool requestDebug = (VULKAN_ENABLE_DEBUG != 0);
    GLOBAL.Vulkan.debugEnabled = false;
    GLOBAL.Vulkan.debugMessengerReady = false;
    GLOBAL.Vulkan.surfaceReady = false;
    GLOBAL.Vulkan.ready = false;

    const char *enabledExtensions[VULKAN_MAX_ENABLED_EXTENSIONS] = { 0 };
    uint32_t extensionCount = 0;

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
            fprintf(stderr, "Missing required Vulkan instance extension reported by GLFW: %s\n", name);
            Assert(false, "Missing GLFW-required Vulkan instance extension");
        }
        PushUniqueString(enabledExtensions, &extensionCount, ARRAY_SIZE(enabledExtensions), name);
    }

    bool debugExtensionAvailable = false;
    if (requestDebug)
    {
        debugExtensionAvailable = VulkanExtensionListHas(availableExtensions, availableExtensionCount, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (debugExtensionAvailable)
        {
            PushUniqueString(enabledExtensions, &extensionCount, ARRAY_SIZE(enabledExtensions), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        else
        {
            LogWarn("VK_EXT_debug_utils not available; continuing without debug messenger");
        }
    }

    VkInstanceCreateFlags instanceFlags = 0;
    const bool portabilityExtensionAvailable = VulkanExtensionListHas(availableExtensions, availableExtensionCount, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (portabilityExtensionAvailable)
    {
        PushUniqueString(enabledExtensions, &extensionCount, ARRAY_SIZE(enabledExtensions), VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    Assert(extensionCount > 0, "No Vulkan instance extensions configured");

    const char *enabledLayers[VULKAN_MAX_ENABLED_LAYERS] = { 0 };
    uint32_t layersCount = 0;

    bool debugLayerAvailable = false;
    if (requestDebug)
    {
        VkLayerProperties availableLayers[VULKAN_MAX_ENUMERATED_LAYERS];
        const uint32_t availableLayerCount = VulkanEnumerateInstanceLayers(availableLayers, ARRAY_SIZE(availableLayers));

        debugLayerAvailable = VulkanLayerListHas(availableLayers, availableLayerCount, VULKAN_VALIDATION_LAYERS[0]);
        if (debugLayerAvailable)
        {
            enabledLayers[layersCount++] = VULKAN_VALIDATION_LAYERS[0];
        }
        else
        {
            LogWarn("Vulkan validation layer not available; continuing without validation");
        }
    }

    const char *applicationTitle = (GLOBAL.Window.title != NULL) ? GLOBAL.Window.title : APPLICATION_TITLE;

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationTitle,
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName = "",
        .engineVersion = VK_MAKE_VERSION(0, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 0 };
    if (debugExtensionAvailable)
    {
        debugCreateInfo = VulkanMakeDebugMessengerCreateInfo();
    }

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = debugExtensionAvailable ? &debugCreateInfo : NULL,
        .pApplicationInfo = &appInfo,
        .flags = instanceFlags,
        .enabledLayerCount = layersCount,
        .ppEnabledLayerNames = (layersCount > 0) ? enabledLayers : NULL,
        .enabledExtensionCount = extensionCount,
        .ppEnabledExtensionNames = enabledExtensions,
    };

    VkResult result = vkCreateInstance(&createInfo, NULL, &GLOBAL.Vulkan.instance);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan instance");

    if (debugExtensionAvailable)
    {
        PFN_vkCreateDebugUtilsMessengerEXT createMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(GLOBAL.Vulkan.instance, "vkCreateDebugUtilsMessengerEXT");
        if (createMessenger != NULL)
        {
            VkResult debugResult = createMessenger(GLOBAL.Vulkan.instance, &debugCreateInfo, NULL, &GLOBAL.Vulkan.debugMessenger);
            if (debugResult == VK_SUCCESS)
            {
                GLOBAL.Vulkan.debugMessengerReady = true;
                GLOBAL.Vulkan.debugEnabled = true;
            }
            else
            {
                fprintf(stderr, "Failed to create Vulkan debug messenger (error %d)\n", debugResult);
            }
        }
        else
        {
            LogWarn("vkCreateDebugUtilsMessengerEXT not available; debug messenger disabled");
        }
    }

    VkResult surfaceResult = glfwCreateWindowSurface(GLOBAL.Vulkan.instance, GLOBAL.Window.window, NULL, &GLOBAL.Vulkan.surface);
    Assert(surfaceResult == VK_SUCCESS, "Failed to create Vulkan surface");

    GLOBAL.Vulkan.surfaceReady = true;
    GLOBAL.Vulkan.instanceReady = true;
    GLOBAL.Vulkan.ready = (GLOBAL.Vulkan.instanceReady && GLOBAL.Vulkan.surfaceReady);

    LogInfo("Vulkan instance and surface ready");
}

void CloseVulkan(void)
{
    if (!GLOBAL.Vulkan.instanceReady)
    {
        return;
    }

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

    vkDestroyInstance(GLOBAL.Vulkan.instance, NULL);
    GLOBAL.Vulkan.instance = VK_NULL_HANDLE;

    GLOBAL.Vulkan.instanceReady = false;
    GLOBAL.Vulkan.ready = false;
    GLOBAL.Vulkan.debugEnabled = false;
}

int main(void)
{
    InitGlfwContext();
    InitWindow();
    InitVulkan();

// Main loop
    while (!WindowShouldClose())
    {
        glfwPollEvents();
    }

    CloseVulkan();
    CloseWindow();
    CloseGlfwContext();

    return 0;
}
