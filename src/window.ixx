module;

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string_view>

export module platform.window;

import assert;

using namespace std;

export class Window
{
    GLFWwindow *handle;

public:
    Window(int width, int height, string_view title)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        // #if defined(__APPLE__)
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
        // #endif

        handle = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);
        Assert(handle != nullptr, "Failed to create GLFW window.");
    }

    ~Window()
    {
        glfwDestroyWindow(handle);
        handle = nullptr;
    }

    // prevent copy/move to force single ownership
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    bool ShouldClose()
    {
        if (!handle) return true;

        return glfwWindowShouldClose(handle);
    }
};
