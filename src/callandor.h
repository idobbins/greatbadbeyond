#pragma once

#include <span>

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
int GetFramebufferHeight();
int GetFramebufferWidth();

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

