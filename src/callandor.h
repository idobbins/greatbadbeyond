#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

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

struct VulkanConfig
{
    bool debug;
    bool portability;
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
GLFWwindow *GetWindowHandle();

// Input-related functions
bool IsKeyPressed();

// Eventloop-related functions
void PollEvents();

//------------------------------------------------------------------------------------
// Vulkan Functions (Module: vulkan)
//------------------------------------------------------------------------------------

void InitVulkan(const VulkanConfig &config);
void CloseVulkan(const VulkanConfig &config);

void InitInstance(const VulkanConfig &config);
void CloseInstance(const VulkanConfig &config);

void InitSurface();
void CloseSurface();

std::span<const VkPhysicalDevice> GetPhysicalDevices();
std::span<const VkQueueFamilyProperties> GetQueueFamilyProperties(const VkPhysicalDevice& device);
bool GetUniversalQueue(const VkPhysicalDevice& device, VkSurfaceKHR surface, uint32_t *family);
void SetPhysicalDevice();
