#include <callandor.h>
#include <runtime.h>

#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <span>
#include <string_view>

template<>
struct Config<Window>
{
    uint32_t width = 1280;
    uint32_t height = 720;
    std::string_view title = "callandor";
    bool resizable = false;
};

struct Window
{
    GLFWwindow *handle = nullptr;
};

static bool glfwInitialized = false;

static void InitializeGlfwContext();
static void ShutdownGlfwContext();
static void ApplyWindowHints(const Config<Window> &config);
static GLFWwindow *CreateGlfwWindow(const Config<Window> &config);

void ErrorCallback(int code, const char *desc)
{
    const char *message = desc != nullptr ? desc : "no description";
    std::cerr << "[glfw][error " << code << "] " << message << '\n';
}

Window Create(const Config<Window> &config)
{
    InitializeGlfwContext();
    ApplyWindowHints(config);

    GLFWwindow *handle = CreateGlfwWindow(config);
    Assert(handle != nullptr, "Failed to create GLFW window");

    Window window{};
    window.handle = handle;
    return window;
}

void Destroy(Window &window)
{
    if (window.handle != nullptr) {
        glfwDestroyWindow(window.handle);
        window.handle = nullptr;
    }

    ShutdownGlfwContext();
}

bool ShouldClose(Window &window)
{
    if (window.handle == nullptr) {
        return true;
    }

    return glfwWindowShouldClose(window.handle) == GLFW_TRUE;
}

bool IsReady(Window &window)
{
    return window.handle != nullptr;
}

void Poll(Window &window)
{
    (void)window;
    glfwPollEvents();
}

void FramebufferSize(const Window &window, uint32_t &width, uint32_t &height)
{
    if (window.handle == nullptr) {
        width = 0;
        height = 0;
        return;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window.handle, &framebufferWidth, &framebufferHeight);

    width = static_cast<uint32_t>(framebufferWidth);
    height = static_cast<uint32_t>(framebufferHeight);
}

template<>
std::span<const PlatformExtension> Enumerate()
{
    InitializeGlfwContext();

    static std::array<const char *, 8> cache{};
    static uint32_t count = 0;
    static std::once_flag once;

    std::call_once(once, [] {
        const char **extensions = glfwGetRequiredInstanceExtensions(&count);
        Assert(extensions != nullptr, "glfwGetRequiredInstanceExtensions returned null");
        Assert(count > 0, "glfwGetRequiredInstanceExtensions returned no extensions");
        Assert(count <= cache.size(), "Too many GLFW-required extensions for cache");

        for (uint32_t index = 0; index < count; ++index) {
            cache[index] = extensions[index];
        }
    });

    return std::span<const PlatformExtension>(cache.data(), count);
}

static void InitializeGlfwContext()
{
    if (glfwInitialized) {
        return;
    }

    glfwSetErrorCallback(ErrorCallback);
    Assert(glfwInit() == GLFW_TRUE, "Failed to initialize GLFW");
    Assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW was not compiled with Vulkan support");
    glfwInitialized = true;
}

static void ShutdownGlfwContext()
{
    if (!glfwInitialized) {
        return;
    }

    glfwTerminate();
    glfwSetErrorCallback(nullptr);
    glfwInitialized = false;
}

static void ApplyWindowHints(const Config<Window> &config)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif
}

static GLFWwindow *CreateGlfwWindow(const Config<Window> &config)
{
    GLFWwindow *handle = glfwCreateWindow(
        static_cast<int>(config.width),
        static_cast<int>(config.height),
        config.title.data(),
        nullptr,
        nullptr);

    return handle;
}
