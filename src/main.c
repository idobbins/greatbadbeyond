#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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

    GLOBAL.Window.window = glfwCreateWindow(1280, 720, "Callandor", NULL, NULL);
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

int main(void)
{
    InitGlfwContext();
    InitWindow();

// Main loop
    while (!glfwWindowShouldClose(GLOBAL.Window.window))
    {
        glfwPollEvents();
    }

    CloseWindow();
    CloseGlfwContext();

    return 0;
}
