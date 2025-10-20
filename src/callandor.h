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
auto GetPlatformVulkanExtensions() -> std::span<cstr>;

// Window-specific functions
void InitWindow();
void CloseWindow();
auto WindowShouldClose() -> bool;
auto IsWindowReady() -> bool;
auto GetWindowSize() -> Size;
auto GetFramebufferSize() -> Size;
auto GetWindowHandle() -> GLFWwindow *;

// Input-related functions
bool IsKeyPressed();

// Eventloop-related functions
void PollEvents();

//------------------------------------------------------------------------------------
// Vulkan Functions (Module: vulkan)
//------------------------------------------------------------------------------------

// Top-level-related functions
auto VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT ,
    const VkDebugUtilsMessengerCallbackDataEXT *,
    void *) -> VKAPI_ATTR VkBool32 VKAPI_CALL;

void InitVulkan(const VulkanConfig &config);
void CloseVulkan(const VulkanConfig &config);

// Instance-related functions
void InitInstance(const VulkanConfig &config);
void CloseInstance(const VulkanConfig &config);

// Surface-related functions
void InitSurface();
void CloseSurface();

// Physical device-related functions
auto GetPhysicalDeviceFeatures() -> VkPhysicalDeviceFeatures2;
auto EnsurePhysicalDeviceSufficient() -> bool;
auto GetPhysicalDevices() -> std::span<const VkPhysicalDevice>;
void SetPhysicalDevice();

// Surface AND Physical device-realted functions
auto GetPhysicalDeviceSurfaceCapabilities() -> VkSurfaceCapabilitiesKHR;
auto GetPhysicalDeviceSurfaceFormats()      -> std::span<const VkSurfaceFormatKHR>;
auto GetPhysicalDeviceSurfacePresentModes() -> std::span<const VkPresentModeKHR>;

// Logical device-related functions
auto GetDeviceExtensionProperties() -> std::span<const VkExtensionProperties>;
bool CheckDeviceExtensionSupport(std::span<cstr> exts);

void InitDevice(const VulkanConfig &config);
void CloseDevice();

// queue-related functions
auto GetQueueFamilyProperties(const VkPhysicalDevice& device) -> std::span<const VkQueueFamilyProperties>;
auto GetUniversalQueue(const VkPhysicalDevice& device, VkSurfaceKHR surface, u32 *family) -> bool;

auto GetGraphicsQueue() -> VkQueue;
auto GetComputeQueue()  -> VkQueue;
auto GetTransferQueue() -> VkQueue;
auto GetPresentQueue()  -> VkQueue;
void GetQueueFamilies();


// Swapchain-related functions
void CreateSwapchain();
void DestroySwapchain();
void RecreateSwapchain();

u32 GetSwapchainImageCount();

void GetSwapchainImageView();
void GetSwapchainImageViewCount();