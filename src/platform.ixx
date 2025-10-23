//====================================================================================
// Global module fragment
//====================================================================================
module;

#include <span>

#include "GLFW/glfw3.h"

//====================================================================================
// Module: platform
//====================================================================================
export module platform;

import platform.glfw;
import platform.window;

using namespace std;

export class Platform
{
    Glfw glfw{};
    Window window{ 1280, 720, "Callandor" };

public:
    Platform() = default;

    span<const char *> RequiredVulkanExtensions()
    {
        return Glfw::RequiredVulkanExtensions();
    }

    void Tick()
    {
        while (!window.ShouldClose())
        {
            glfwPollEvents();
        }
    }
};
