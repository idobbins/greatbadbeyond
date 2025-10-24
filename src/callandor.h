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

//------------------------------------------------------------------------------------
// Window and Platform Functions (Module: platform)
//------------------------------------------------------------------------------------

// Glfw-specific functions
void GlfwErrorCallback(i32 code, cstr description);

auto GetPlatformVulkanExtensions() -> std::span<cstr>;

void CreateGlfwContext();
void DestroyGlfwContext();

// Window-specific functions
auto WindowShouldClose()  -> bool;

auto IsWindowReady()      -> bool;

auto GetWindowSize()      -> Size;
auto GetFramebufferSize() -> Size;
auto GetWindowHandle()    -> GLFWwindow *;

void CreateWindow();
void DestroyWindow();

// Input-related functions
bool IsKeyPressed();

// Eventloop-related functions
void PollEvents();
void MainLoop();
bool RequiresDebug();
bool RequiresPortability();

//------------------------------------------------------------------------------------
// Vulkan Functions (Module: vulkan)
//------------------------------------------------------------------------------------

// Debug-related functions
// Keep standard return declaration so VKAPI_CALL maps to __stdcall on Windows without warnings.
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *,
    void *);

void CreateDebugMessenger();
void DestroyDebugMessenger();

// Instance-related functions
void CreateInstance();
void DestroyInstance();

// Surface-related functions
void CreateSurface();
void DestroySurface();

// Physical device-related functions
auto EnsurePhysicalDeviceSufficient()                           -> bool;
auto GetPhysicalDevices()                                       -> std::span<const VkPhysicalDevice>;
auto GetPhysicalDeviceFeatures2(const VkPhysicalDevice&)        -> const VkPhysicalDeviceFeatures2&;
auto GetPhysicalDeviceVulkan13Features(const VkPhysicalDevice&) -> const VkPhysicalDeviceVulkan13Features&;
void SetPhysicalDevice();

// Surface AND Physical device-realted functions
auto GetPhysicalDeviceSurfaceCapabilities() -> VkSurfaceCapabilitiesKHR;
auto GetPhysicalDeviceSurfaceFormats()      -> std::span<const VkSurfaceFormatKHR>;
auto GetPhysicalDeviceSurfacePresentModes() -> std::span<const VkPresentModeKHR>;

// Logical device-related functions
auto GetDeviceExtensionProperties() -> std::span<const VkExtensionProperties>;
bool CheckDeviceExtensionSupport(std::span<cstr> exts);

void CreateDevice();
void DestroyDevice();

// queue-related functions
auto GetQueueFamilyProperties(const VkPhysicalDevice& device)                             -> std::span<const VkQueueFamilyProperties>;
auto GetUniversalQueue(const VkPhysicalDevice& device, VkSurfaceKHR surface, u32 *family) -> bool;

auto GetGraphicsQueue() -> VkQueue;
auto GetComputeQueue()  -> VkQueue;
auto GetTransferQueue() -> VkQueue;
auto GetPresentQueue()  -> VkQueue;
void GetQueueFamilies();

// Swapchain-related functions
auto GetSwapchainImages() -> std::span<const VkImage>;
auto GetSwapchainImageViews() -> std::span<const VkImageView>;
auto GetSwapchainExtent() -> VkExtent2D;
auto GetSwapchainFormat() -> VkFormat;

void GetSwapchainImageView();
void GetSwapchainImageViewCount();

void CreateSwapchain();
void DestroySwapchain();
void RecreateSwapchain();

// VMA-related functions
void CreateVMAAllocator();
void DestroyVMAAllocator();
void AllocateBuffer();
void FreeBuffer();
void AllocateImage();
void FreeImage();
void AllocateDescriptorSet();
void FreeDescriptorSet();

// Command pool-related functions
void CreateCommandPool();
void DestroyCommandPool();
void CreateCommandBuffer();
void DestroyCommandBuffer();

// Command buffer-related functions
void RecordCommandBuffer();

// Synchronization-related functions
void CreateSemaphore();
void DestroySemaphore();
void CreateFence();
void DestroyFence();

// Descriptor-related functions
void CreateDescriptorSet();
void DestroyDescriptorSet();

// Shader-related functions
auto CreateShader() -> VkShaderModule;
void DestroyShader();

// Pipeline-related functions
void CreatePipeline();
void DestroyPipeline();

// Drawing-related functions
void DrawFrame();

// High-level-related functions
void CreateVulkan();
void DestroyVulkan();
