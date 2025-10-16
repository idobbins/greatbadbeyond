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
        bool instanceReady;

        bool ready;

    } Vulkan;
} GlobalData;

GlobalData GLOBAL = { 0 };

static void GlfwErrorCallback(int32_t code, const char *desc) {
    fprintf(stderr, "[glfw][error %d] %s\n", code, desc ? desc : "no description");
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

    const char *const *enabledExtensions = VULKAN_PLATFORM_EXTENSIONS;
    const uint32_t extensionsCount = (uint32_t)ARRAY_SIZE(VULKAN_PLATFORM_EXTENSIONS);
    Assert((enabledExtensions != NULL) && (extensionsCount > 0), "No Vulkan instance extensions configured");

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = APPLICATION_TITLE,
        .pEngineName = "",
    };

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .flags = VULKAN_INSTANCE_FLAGS,
        .enabledExtensionCount = extensionsCount,
        .ppEnabledExtensionNames = enabledExtensions,
    };

    VkResult result = vkCreateInstance(&createInfo, NULL, &GLOBAL.Vulkan.instance);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan instance");

    GLOBAL.Vulkan.instanceReady = true;
    GLOBAL.Vulkan.ready = true;
}

void CloseVulkan(void)
{
    if (!GLOBAL.Vulkan.instanceReady)
    {
        return;
    }

    vkDestroyInstance(GLOBAL.Vulkan.instance, NULL);
    GLOBAL.Vulkan.instance = VK_NULL_HANDLE;

    GLOBAL.Vulkan.instanceReady = false;
    GLOBAL.Vulkan.ready = false;
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
