#pragma once

#include "types.cpp"
#include "assert.cpp"

#include <GLFW/glfw3.h>

#include <iostream>
#include <string>
#include <string_view>

using namespace std;

struct GlfwContext {
    bool initialized = false;
};

struct WindowConfig {
    i32 width = 1280;
    i32 height = 720;
    string title = string("callandor");
    bool resizable = false;
};

struct Window {
    GLFWwindow* handle = nullptr;
};

static void glfwErrorCallback(const i32 code, const char* description)
{
    cerr << "[glfw][error " << code << "] " << (description ? description : "no description") << '\n';
}

static GlfwContext createGlfwContext()
{
    glfwSetErrorCallback(glfwErrorCallback);

    const i32 initResult = glfwInit();
    runtime_assert(initResult == GLFW_TRUE, "Failed to initialize GLFW");
    runtime_assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW was not compiled with Vulkan support");

    return {.initialized = true};
}

static void destroyGlfwContext(const GlfwContext& context)
{
    if (!context.initialized) {
        return;
    }

    glfwTerminate();
    glfwSetErrorCallback(nullptr);
}

static void applyWindowHints(const WindowConfig& config)
{
    glfwDefaultWindowHints();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif
}

static Window createWindow(const GlfwContext& context, const WindowConfig& config = {})
{
    runtime_assert(context.initialized, "GLFW context must be initialized before creating windows");

    applyWindowHints(config);

    GLFWwindow* handle = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    runtime_assert(handle != nullptr, "Failed to create GLFW window");

    return {.handle = handle};
}

static void destroyWindow(const Window& window)
{
    if (window.handle == nullptr) {
        return;
    }
    glfwDestroyWindow(window.handle);
}

static bool windowShouldClose(const Window& window)
{
    if (window.handle == nullptr) {
        return true;
    }

    return glfwWindowShouldClose(window.handle) == GLFW_TRUE;
}

static void pollWindowEvents()
{
    glfwPollEvents();
}
