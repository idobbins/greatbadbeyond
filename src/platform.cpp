#include <callandor.h>
#include <runtime.h>

#include <GLFW/glfw3.h>

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
        bool ready;

    } Window;
} Platform;

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

void GlfwErrorCallback(int code, const char *description)
{
    const char *message = description != nullptr ? description: "no description";
    cerr << "[glfw][error " << code << "] " << message << endl;
}
