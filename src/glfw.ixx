module;

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <iostream>
#include <span>

export module platform.glfw;

import assert;

using namespace std;

export class Glfw
{
public:
    Glfw()
    {
        glfwSetErrorCallback([](int code, const char *description)
        {
            const char *message = description != nullptr ? description : "no description";
            cerr << "[glfw][error " << code << "] " << message << endl;
        });

        Assert(glfwInit() == GLFW_TRUE, "Failed to initialize GLFW");
        Assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW was not compiled with Vulkan support");
    }

    ~Glfw()
    {
        glfwTerminate();
        glfwSetErrorCallback(nullptr);
    }

    // prevent copy/move to force single ownership
    Glfw(const Glfw&) = delete;
    Glfw& operator=(const Glfw&) = delete;
    Glfw(Glfw&&) = delete;
    Glfw& operator=(Glfw&&) = delete;

    static span<const char *> RequiredVulkanExtensions()
    {
        static constexpr uint32_t MaxPlatformInstanceExtensions = 8;
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
};
