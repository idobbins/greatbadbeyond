#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef VK_USE_PLATFORM_WIN32_KHR
        #define VK_USE_PLATFORM_WIN32_KHR
    #endif
    #include <windows.h>
#endif

#if defined(__APPLE__)
    #ifndef VK_ENABLE_BETA_EXTENSIONS
        #define VK_ENABLE_BETA_EXTENSIONS
    #endif
    #ifndef VK_USE_PLATFORM_MACOS_MVK
        #define VK_USE_PLATFORM_MACOS_MVK
    #endif
    #ifndef VK_USE_PLATFORM_METAL_EXT
        #define VK_USE_PLATFORM_METAL_EXT
    #endif
#endif

#if defined(__linux__)
    #ifndef VK_USE_PLATFORM_XCB_KHR
        #define VK_USE_PLATFORM_XCB_KHR
    #endif
#endif
#if defined(__ANDROID__)
    #ifndef VK_USE_PLATFORM_ANDROID_KHR
        #define VK_USE_PLATFORM_ANDROID_KHR
    #endif
#endif

#include <vulkan/vulkan.h>

#if defined(__APPLE__)
    #include <vulkan/vulkan_metal.h>
    #include <vulkan/vulkan_beta.h>
#endif

#if defined(_WIN32)
    #include <vulkan/vulkan_win32.h>
#endif

#if defined(__linux__)
    #include <vulkan/vulkan_xcb.h>
#endif

#include <GLFW/glfw3.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#if defined(VK_USE_PLATFORM_WIN32_KHR)
static const char *const VULKAN_PLATFORM_EXTENSIONS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
static const VkInstanceCreateFlags VULKAN_INSTANCE_FLAGS = 0;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
static const char *const VULKAN_PLATFORM_EXTENSIONS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
};
static const VkInstanceCreateFlags VULKAN_INSTANCE_FLAGS = 0;
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
static const char *const VULKAN_PLATFORM_EXTENSIONS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
};
static const VkInstanceCreateFlags VULKAN_INSTANCE_FLAGS = 0;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
static const char *const VULKAN_PLATFORM_EXTENSIONS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};
static const VkInstanceCreateFlags VULKAN_INSTANCE_FLAGS = 0;
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
static const char *const VULKAN_PLATFORM_EXTENSIONS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
};
static const VkInstanceCreateFlags VULKAN_INSTANCE_FLAGS = 0;
#elif defined(VK_USE_PLATFORM_MACOS_MVK) || defined(VK_USE_PLATFORM_METAL_EXT)
static const char *const VULKAN_PLATFORM_EXTENSIONS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
static const VkInstanceCreateFlags VULKAN_INSTANCE_FLAGS = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#elif defined(__APPLE__)
static const char *const VULKAN_PLATFORM_EXTENSIONS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
static const VkInstanceCreateFlags VULKAN_INSTANCE_FLAGS = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#else
static const char *const VULKAN_PLATFORM_EXTENSIONS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
};
static const VkInstanceCreateFlags VULKAN_INSTANCE_FLAGS = 0;
#endif

#if !defined(NDEBUG)
    #define VULKAN_ENABLE_DEBUG 1
#else
    #define VULKAN_ENABLE_DEBUG 0
#endif

static const char *const VULKAN_DEBUG_EXTENSIONS[] = {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

static const char *const VULKAN_VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation",
};

#define VULKAN_MAX_ENABLED_EXTENSIONS (ARRAY_SIZE(VULKAN_PLATFORM_EXTENSIONS) + ARRAY_SIZE(VULKAN_DEBUG_EXTENSIONS))
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
        bool instanceReady;
        bool debugMessengerReady;

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

static void VulkanValidateInstanceExtensions(uint32_t requiredCount, const char *const *required)
{
    VkExtensionProperties available[VULKAN_MAX_ENUMERATED_EXTENSIONS];
    const uint32_t availableCount = VulkanEnumerateInstanceExtensions(available, ARRAY_SIZE(available));

    for (uint32_t index = 0; index < requiredCount; index++)
    {
        bool found = false;
        for (uint32_t search = 0; search < availableCount; search++)
        {
            if (strcmp(required[index], available[search].extensionName) == 0)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            fprintf(stderr, "Missing required Vulkan instance extension: %s\n", required[index]);
            Assert(false, "Missing required Vulkan instance extension");
        }
    }
}

static void VulkanValidateInstanceLayers(uint32_t requiredCount, const char *const *required)
{
    VkLayerProperties available[VULKAN_MAX_ENUMERATED_LAYERS];
    const uint32_t availableCount = VulkanEnumerateInstanceLayers(available, ARRAY_SIZE(available));

    for (uint32_t index = 0; index < requiredCount; index++)
    {
        bool found = false;
        for (uint32_t search = 0; search < availableCount; search++)
        {
            if (strcmp(required[index], available[search].layerName) == 0)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            fprintf(stderr, "Missing required Vulkan instance layer: %s\n", required[index]);
            Assert(false, "Missing required Vulkan instance layer");
        }
    }
}
void InitGlfwContext(void)
{
    glfwSetErrorCallback(GlfwErrorCallback);

    Assert(glfwInit() == true, "Failed to initialize GLFW");
    Assert(glfwVulkanSupported() == true, "Vulkan is not supported");

    GLOBAL.Glfw.ready = true;
    GLOBAL.Glfw.vulkanSupported = true;
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

    const bool enableDebug = (VULKAN_ENABLE_DEBUG != 0);
    GLOBAL.Vulkan.debugEnabled = enableDebug;
    GLOBAL.Vulkan.debugMessengerReady = false;

    const uint32_t platformExtensionCount = (uint32_t)ARRAY_SIZE(VULKAN_PLATFORM_EXTENSIONS);
    const uint32_t debugExtensionCount = enableDebug ? (uint32_t)ARRAY_SIZE(VULKAN_DEBUG_EXTENSIONS) : 0;
    const uint32_t extensionsCount = platformExtensionCount + debugExtensionCount;
    Assert(extensionsCount > 0, "No Vulkan instance extensions configured");

    const char *enabledExtensions[VULKAN_MAX_ENABLED_EXTENSIONS] = { 0 };
    uint32_t extensionIndex = 0;
    for (uint32_t index = 0; index < platformExtensionCount; index++)
    {
        enabledExtensions[extensionIndex++] = VULKAN_PLATFORM_EXTENSIONS[index];
    }
    if (enableDebug)
    {
        for (uint32_t index = 0; index < ARRAY_SIZE(VULKAN_DEBUG_EXTENSIONS); index++)
        {
            enabledExtensions[extensionIndex++] = VULKAN_DEBUG_EXTENSIONS[index];
        }
    }
    Assert(extensionIndex == extensionsCount, "Mismatch in Vulkan extension count");

    VulkanValidateInstanceExtensions(extensionsCount, enabledExtensions);

    const char *enabledLayers[VULKAN_MAX_ENABLED_LAYERS] = { 0 };
    uint32_t layersCount = 0;
    if (enableDebug)
    {
        for (uint32_t index = 0; index < ARRAY_SIZE(VULKAN_VALIDATION_LAYERS); index++)
        {
            enabledLayers[layersCount++] = VULKAN_VALIDATION_LAYERS[index];
        }
        VulkanValidateInstanceLayers(layersCount, enabledLayers);
    }

    const char *applicationTitle = (GLOBAL.Window.title != NULL) ? GLOBAL.Window.title : APPLICATION_TITLE;

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationTitle,
        .pEngineName = "",
    };

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 0 };
    if (enableDebug)
    {
        debugCreateInfo = VulkanMakeDebugMessengerCreateInfo();
    }

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = enableDebug ? &debugCreateInfo : NULL,
        .pApplicationInfo = &appInfo,
        .flags = VULKAN_INSTANCE_FLAGS,
        .enabledLayerCount = layersCount,
        .ppEnabledLayerNames = (layersCount > 0) ? enabledLayers : NULL,
        .enabledExtensionCount = extensionsCount,
        .ppEnabledExtensionNames = enabledExtensions,
    };

    VkResult result = vkCreateInstance(&createInfo, NULL, &GLOBAL.Vulkan.instance);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan instance");

    if (enableDebug)
    {
        PFN_vkCreateDebugUtilsMessengerEXT createMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(GLOBAL.Vulkan.instance, "vkCreateDebugUtilsMessengerEXT");
        Assert(createMessenger != NULL, "Failed to load vkCreateDebugUtilsMessengerEXT");

        VkResult debugResult = createMessenger(GLOBAL.Vulkan.instance, &debugCreateInfo, NULL, &GLOBAL.Vulkan.debugMessenger);
        Assert(debugResult == VK_SUCCESS, "Failed to create Vulkan debug messenger");

        GLOBAL.Vulkan.debugMessengerReady = true;
    }

    GLOBAL.Vulkan.instanceReady = true;
    GLOBAL.Vulkan.ready = true;
}

void CloseVulkan(void)
{
    if (!GLOBAL.Vulkan.instanceReady)
    {
        return;
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
