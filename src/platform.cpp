#include <greadbadbeyond.h>
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
    struct
    {
        double lastTime;
        double lastLogTime;
        double accumulatedMs;
        u32 samples;
        float deltaSeconds;
        bool ready;

    } FrameTiming;
} Platform;

static constexpr double FrameTimingLogIntervalSeconds = 1.0;

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

    double now = glfwGetTime();
    Platform.FrameTiming.lastTime = now;
    Platform.FrameTiming.lastLogTime = now;
    Platform.FrameTiming.accumulatedMs = 0.0;
    Platform.FrameTiming.samples = 0;
    Platform.FrameTiming.deltaSeconds = 0.0f;
    Platform.FrameTiming.ready = true;
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

    Platform.FrameTiming.ready = false;
    Platform.FrameTiming.deltaSeconds = 0.0f;
    Platform.FrameTiming.samples = 0;
    Platform.FrameTiming.accumulatedMs = 0.0;
    Platform.FrameTiming.lastTime = 0.0;
    Platform.FrameTiming.lastLogTime = 0.0;
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

auto GetFrameDeltaSeconds() -> float
{
    return Platform.FrameTiming.deltaSeconds;
}

void MainLoop()
{
    if (!Platform.FrameTiming.ready)
    {
        double now = glfwGetTime();
        Platform.FrameTiming.lastTime = now;
        Platform.FrameTiming.lastLogTime = now;
        Platform.FrameTiming.accumulatedMs = 0.0;
        Platform.FrameTiming.samples = 0;
        Platform.FrameTiming.deltaSeconds = 0.0f;
        Platform.FrameTiming.ready = true;
    }

    while (!WindowShouldClose())
    {
        double now = glfwGetTime();
        double deltaSeconds = now - Platform.FrameTiming.lastTime;
        if (deltaSeconds < 0.0)
        {
            deltaSeconds = 0.0;
        }

        Platform.FrameTiming.lastTime = now;
        Platform.FrameTiming.deltaSeconds = static_cast<float>(deltaSeconds);
        Platform.FrameTiming.accumulatedMs += deltaSeconds * 1000.0;
        Platform.FrameTiming.samples += 1;

        if ((now - Platform.FrameTiming.lastLogTime) >= FrameTimingLogIntervalSeconds)
        {
            u32 frameSamples = Platform.FrameTiming.samples;
            if (frameSamples > 0)
            {
                double averageMs = Platform.FrameTiming.accumulatedMs / static_cast<double>(frameSamples);
                double fps = (averageMs > 0.0) ? (1000.0 / averageMs) : 0.0;
                LogInfo("[frame] avg %.3f ms (%.1f fps) over %u frames",
                    averageMs,
                    fps,
                    static_cast<unsigned>(frameSamples));
            }

            Platform.FrameTiming.lastLogTime = now;
            Platform.FrameTiming.accumulatedMs = 0.0;
            Platform.FrameTiming.samples = 0;
        }

        PollEvents();
        GLFWwindow *window = GetWindowHandle();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        }
        UpdateCameraFromInput(static_cast<float>(Platform.FrameTiming.deltaSeconds));

        if (ConsumeFramebufferResize())
        {
            ResetCameraAccum();
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

        GradientParams gradient = {};
        Size framebuffer = GetFramebufferSize();
        gradient.resolution.x = (framebuffer.width > 0)? static_cast<float>(framebuffer.width) : 1.0f;
        gradient.resolution.y = (framebuffer.height > 0)? static_cast<float>(framebuffer.height) : 1.0f;
        gradient.time = static_cast<float>(glfwGetTime());
        gradient.padding = 0.0f;

        VkResult recordResult = RecordCommandBuffer(frameIndex, imageIndex, gradient);
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
