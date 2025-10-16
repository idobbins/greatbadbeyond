#include "vulkan/vulkan_core.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
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

}

void CloseWindow(void)
{

}

int main(void)
{
    InitGlfwContext();

    CloseGlfwContext();

    return 0;
}
