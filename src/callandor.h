#pragma once

#include <span>

struct Vec2
{
    float x;
    float y;
};

struct Size
{
    int width;
    int height;
};

//------------------------------------------------------------------------------------
// Window and Platform Functions (Module: platform)
//------------------------------------------------------------------------------------

// Glfw-specific functions
void GlfwErrorCallback(int code, const char *description);
void InitGlfwContext();
void CloseGlfwContext();
std::span<const char *> GetPlatformVulkanExtensions();

// Window-specific functions
void InitWindow();
void CloseWindow();
bool WindowShouldClose();
bool IsWindowReady();
Size GetWindowSize();
Size GetFramebufferSize();

// Input-related functions
bool IsKeyPressed();

// Eventloop-related functions
void PollEvents();

//------------------------------------------------------------------------------------
// Vulkan Functions (Module: vulkan)
//------------------------------------------------------------------------------------

void InitVulkan();
void CloseVulkan();

void InitInstance();
void CloseInstance();

void InitSurface();
void CloseSurface();
