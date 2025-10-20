#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

//------------------------------------------------------------------------------------
// Primitive Type Aliases (Rust-style semantics, C++20)
//------------------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <span>

// Signed integers
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// Unsigned integers
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Floating-point
using f32 = float;
using f64 = double;

// Boolean and character
using b8  = bool;
using c8  = char;

// Strings and pointers
using cstr     = const char*;
using mut_cstr = char*;
using ptr      = void*;
using cptr     = const void*;

// Size and indexing
using usize = std::size_t;
using isize = std::ptrdiff_t;

//------------------------------------------------------------------------------------
// Common Types
//------------------------------------------------------------------------------------

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
void GlfwErrorCallback(i32 code, cstr description);
void InitGlfwContext();
void CloseGlfwContext();
std::span<cstr> GetPlatformVulkanExtensions();

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

// Top-level-related functions
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
VkDebugUtilsMessageSeverityFlagBitsEXT,
VkDebugUtilsMessageTypeFlagsEXT ,
const VkDebugUtilsMessengerCallbackDataEXT *,
void *);

void InitVulkan(const VulkanConfig &config);
void CloseVulkan(const VulkanConfig &config);

// Instance-related functions
void InitInstance(const VulkanConfig &config);
void CloseInstance(const VulkanConfig &config);

// Surface-related functions
void InitSurface();
void CloseSurface();

// Physical device-related functions
std::span<const VkPhysicalDevice> GetPhysicalDevices();
void SetPhysicalDevice();

// Surface AND Physical device-realted functions
VkSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilities();
std::span<const VkSurfaceFormatKHR> GetPhysicalDeviceSurfaceFormats();
std::span<const VkPresentModeKHR> GetPhysicalDeviceSurfacePresentModes();

// Logical device-related functions
void InitDevice(const VulkanConfig &config);
void CloseDevice();

// queue-related functions
std::span<const VkQueueFamilyProperties> GetQueueFamilyProperties(const VkPhysicalDevice& device);
bool GetUniversalQueue(const VkPhysicalDevice& device, VkSurfaceKHR surface, u32 *family);

VkQueue GetGraphicsQueue();
VkQueue GetComputeQueue();
VkQueue GetTransferQueue();
VkQueue GetPresentQueue();
void GetQueueFamilies();


// Swapchain-related functions
void CreateSwapchain();
void DestroySwapchain();
void RecreateSwapchain();

u32 GetSwapchainImageCount();

void GetSwapchainImageView();
void GetSwapchainImageViewCount();