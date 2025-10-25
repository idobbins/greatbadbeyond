#include <callandor.h>
#include <config.h>
#include <utils.h>

#include <GLFW/glfw3.h>

#include <array>
#include <iostream>
#include <string_view>

using namespace std;

static struct PlatformData
{
    struct
    {
        bool ready;

    } Glfw;
    struct
    {
        string_view title;
        GLFWwindow *handle;
        bool ready;
        bool framebufferResized;

    } Window;
} Platform;

void GlfwErrorCallback(int code, const char *description)
{
    const char *message = description != nullptr ? description: "no description";
    cerr << "[glfw][error " << code << "] " << message << endl;
}

void CreateGlfwContext()
{
    if (Platform.Glfw.ready)
    {
        return;
    }

    glfwSetErrorCallback(GlfwErrorCallback);
    Assert(glfwInit() == GLFW_TRUE, "Failed to initialize GLFW");
    Assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW was not compiled with Vulkan support");

    Platform.Glfw.ready = true;
}

void DestroyGlfwContext()
{
    if (!Platform.Glfw.ready)
    {
        return;
    }

    glfwTerminate();
    glfwSetErrorCallback(nullptr);
    Platform.Glfw.ready = false;
}



auto GetPlatformVulkanExtensions() -> span<const char *>
{
    static array<const char*, MaxPlatformInstanceExtensions> cache {};
    static uint32_t count = 0;
    static bool ready = false;

    if (ready)
    {
        return {cache.data(), count};
    }

    const char **extensions = glfwGetRequiredInstanceExtensions(&count);
    Assert(extensions != nullptr, "glfwGetRequiredInstanceExtensions returned null");
    Assert(count > 0, "glfwGetRequiredInstanceExtensions returned no extensions");
    Assert(count <= cache.size(), "Too many GLFW-required extensions for cache");

    for (uint32_t index = 0; index < count; ++index) {
        cache[index] = extensions[index];
    }

    ready = true;
    return {cache.data(), count};
}

void CreateWindow()
{
    Assert(Platform.Glfw.ready, "GLFW must be initialized before trying to init window");

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
// #if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
// #endif

    Platform.Window.title = DefaultWindowTitle;
    Platform.Window.handle = glfwCreateWindow(DefaultWindowWidth, DefaultWindowHeight, Platform.Window.title.data(), nullptr, nullptr);
    Assert(Platform.Window.handle != nullptr, "Failed to create GLFW window");
    glfwSetFramebufferSizeCallback(Platform.Window.handle, FramebufferSizeCallback);

    Platform.Window.ready = true;
    Platform.Window.framebufferResized = false;
}

void DestroyWindow()
{
    if (!Platform.Window.ready)
    {
        return;
    }

    glfwDestroyWindow(Platform.Window.handle);
    Platform.Window.handle = nullptr;

    Platform.Window.ready = false;
    Platform.Window.framebufferResized = false;
}

auto WindowShouldClose() -> bool
{
    if (!Platform.Window.ready || !Platform.Window.handle)
    {
        return true;
    }

    return glfwWindowShouldClose(Platform.Window.handle);
}

auto IsWindowReady() -> bool
{
    return Platform.Window.ready;
}

auto GetWindowSize() -> Size
{
    Size size = {0, 0};

    if (!Platform.Window.ready)
    {
        return size;
    }

    int width = 0;
    int height = 0;
    glfwGetWindowSize(Platform.Window.handle, &width, &height);

    size.width = width;
    size.height = height;

    return size;
}

auto GetFramebufferSize() -> Size
{
    Size size = {0, 0};

    if (!Platform.Window.ready)
    {
        return size;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(Platform.Window.handle, &width, &height);

    size.width = width;
    size.height = height;

    return size;
}

void FramebufferSizeCallback(GLFWwindow *window, int width, int height)
{
    (void)window;
    (void)width;
    (void)height;

    Platform.Window.framebufferResized = true;
}

auto ConsumeFramebufferResize() -> bool
{
    bool resized = Platform.Window.framebufferResized;
    Platform.Window.framebufferResized = false;
    return resized;
}

auto GetWindowHandle() -> GLFWwindow *
{
    Assert(Platform.Window.ready && Platform.Window.handle != nullptr, "Window is not ready");
    return Platform.Window.handle;
}

void PollEvents()
{
    glfwPollEvents();
}

void MainLoop()
{
    while (!WindowShouldClose())
    {
        PollEvents();

        if (ConsumeFramebufferResize())
        {
            RecreateSwapchain();
            continue;
        }

        u32 imageIndex = 0;
        u32 frameIndex = 0;
        VkResult acquireResult = AcquireNextImage(imageIndex, frameIndex);

        if ((acquireResult == VK_ERROR_OUT_OF_DATE_KHR) || (acquireResult == VK_SUBOPTIMAL_KHR))
        {
            RecreateSwapchain();
            continue;
        }

        if (acquireResult != VK_SUCCESS)
        {
            LogError("[vulkan] AcquireNextImage failed (result=%d)", acquireResult);
            break;
        }

        VkClearColorValue clearColor = {{0.05f, 0.15f, 0.45f, 1.0f}};
        VkResult recordResult = RecordCommandBuffer(frameIndex, imageIndex, clearColor);
        if (recordResult != VK_SUCCESS)
        {
            LogError("[vulkan] RecordCommandBuffer failed (result=%d)", recordResult);
            break;
        }

        VkResult submitResult = SubmitFrame(frameIndex, imageIndex);
        if ((submitResult == VK_ERROR_OUT_OF_DATE_KHR) || (submitResult == VK_SUBOPTIMAL_KHR))
        {
            RecreateSwapchain();
            continue;
        }

        if (submitResult != VK_SUCCESS)
        {
            LogError("[vulkan] SubmitFrame failed (result=%d)", submitResult);
            break;
        }
    }
}

auto RequiresDebug() -> bool
{
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}

auto RequiresPortability() -> bool
{
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}
