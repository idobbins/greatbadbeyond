#include <callandor.h>
#include <runtime.h>

#include <GLFW/glfw3.h>

#include <array>
#include <iostream>
#include <string_view>

using namespace std;

struct PlatformData
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

    } Window;
} Platform;

void GlfwErrorCallback(int code, const char *description)
{
    const char *message = description != nullptr ? description: "no description";
    cerr << "[glfw][error " << code << "] " << message << endl;
}

void InitGlfwContext()
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

void CloseGlfwContext()
{
    if (!Platform.Glfw.ready)
    {
        return;
    }

    glfwTerminate();
    glfwSetErrorCallback(nullptr);
    Platform.Glfw.ready = false;
}



span<const char *> GetPlatformVulkanExtensions()
{
    static array<const char*, 8> cache {};
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

void InitWindow()
{
    Assert(Platform.Glfw.ready, "GLFW must be initialized before trying to init window");

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    Platform.Window.title = "Callandor";
    Platform.Window.handle = glfwCreateWindow(1270, 720, Platform.Window.title.data(), nullptr, nullptr);

    Platform.Window.ready = true;
}

void CloseWindow()
{
    if (!Platform.Window.ready)
    {
        return;
    }

    glfwDestroyWindow(Platform.Window.handle);
    Platform.Window.handle = nullptr;

    Platform.Window.ready = false;
}

bool WindowShouldClose()
{
    if (!Platform.Window.ready || !Platform.Window.handle)
    {
        return true;
    }

    return glfwWindowShouldClose(Platform.Window.handle);
}

bool IsWindowReady()
{
    return Platform.Window.ready;
}

int GetFramebufferHeight()
{

}

int GetFramebufferWidth()
{

}


void PollEvents()
{
    glfwPollEvents();
}
