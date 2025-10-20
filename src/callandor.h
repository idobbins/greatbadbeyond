#pragma once

#include <span>

//------------------------------------------------------------------------------------
// Window and Platform Functions (Module: platform)
//------------------------------------------------------------------------------------

// Glfw-specific functions
void InitGlfwContext();
void CloseGlfwContext();
void GlfwErrorCallback(int code, const char *description);

// Window-specific functions
void InitWindow();
void CloseWindow();
bool WindowShouldClose();
bool IsWindowReady();
int GetFramebufferHeight();
int GetFramebufferWidth();
std::span<const char *const> GetWindowVulkanExtensions();

// Input-related functions
bool IsKeyPressed();

// Eventloop-related functions
void Poll();

//------------------------------------------------------------------------------------
// Vulkan Functions (Module: vulkan)
//------------------------------------------------------------------------------------

void InitVulkan();
void CloseVulkan();

void InitInstance();
void CloseInstance();

void InitSurface();
void CloseSurface();

