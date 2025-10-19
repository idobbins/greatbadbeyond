#include <callandor.h>
#include <runtime.h>

#include <GLFW/glfw3.h>

#include <array>
#include <iostream>
#include <mutex>

using namespace std;

template<>
struct Config<Window>
{
    uint32_t width;
    uint32_t height;
    string_view title;
    bool resizable;
    bool ready;
};

struct Window
{
    GLFWwindow *handle;
};

void ErrorCallback(int code, const char *desc)
{
    cerr << "[glfw][error " << code << "] " << (desc ? desc : "no description") << '\n';
}

Window Create(Config<Window> &config)
{
    glfwSetErrorCallback(ErrorCallback);
    Assert(glfwInit() == GLFW_TRUE, "Failed to initialize GLFW");
    Assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW was not compiled with Vulkan support");

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    GLFWwindow* w = glfwCreateWindow(config.width, config.height, config.title.data(), nullptr, nullptr);

    return {w};
}

void Destroy(Window &window)
{
    if (window.handle)
    {
        glfwDestroyWindow(window.handle);
        window.handle = nullptr;
    }
}

span<PlatformExtension> Enumerate() {
    static array<const char*, 8> cache{};
    static uint32_t count = 0;
    static once_flag once;

    call_once(once, [&] {
        const char** exts = glfwGetRequiredInstanceExtensions(&count);

        Assert(exts && count > 0, "glfwGetRequiredInstanceExtensions failed");
        Assert(count <= cache.size(), "Too many GLFW-required extensions for cache");

        for (uint32_t i = 0; i < count; ++i)
        {
            cache[i] = exts[i];
        }
    });

    return {cache.data(), count};
}