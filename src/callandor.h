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

struct PhysicalDeviceFeatures
{
    VkPhysicalDeviceFeatures2 core;
    VkPhysicalDeviceVulkan13Features v13;
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
void FramebufferSizeCallback(GLFWwindow *window, int width, int height);
auto ConsumeFramebufferResize() -> bool;

void CreateWindow();
void DestroyWindow();

// Input-related functions
auto IsKeyPressed() -> bool;

// Eventloop-related functions
void PollEvents();
void MainLoop();
auto RequiresDebug() -> bool;
auto RequiresPortability() -> bool;

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
auto GetPhysicalDeviceFeatures(const VkPhysicalDevice&)         -> const PhysicalDeviceFeatures&;
void SetPhysicalDevice();

// Surface AND Physical device-realted functions
auto GetPhysicalDeviceSurfaceCapabilities() -> VkSurfaceCapabilitiesKHR;
auto GetPhysicalDeviceSurfaceFormats()      -> std::span<const VkSurfaceFormatKHR>;
auto GetPhysicalDeviceSurfacePresentModes() -> std::span<const VkPresentModeKHR>;

// Logical device-related functions
auto GetDeviceExtensionProperties() -> std::span<const VkExtensionProperties>;
auto CheckDeviceExtensionSupport(std::span<cstr> exts) -> bool;

void CreateDevice();
void DestroyDevice();

// queue-related functions
auto GetQueueFamilyProperties(const VkPhysicalDevice& device)                             -> std::span<const VkQueueFamilyProperties>;
auto GetGraphicsQueue() -> VkQueue;
auto GetComputeQueue()  -> VkQueue;
auto GetTransferQueue() -> VkQueue;
auto GetPresentQueue()  -> VkQueue;
auto GetQueueFamilies(
    const VkPhysicalDevice& device,
    VkSurfaceKHR surface,
    u32 &graphicsFamily,
    u32 &presentFamily,
    u32 &transferFamily,
    u32 &computeFamily) -> bool;

// Swapchain-related functions
auto GetSwapchainImages() -> std::span<const VkImage>;
auto GetSwapchainImageViews() -> std::span<const VkImageView>;
auto GetSwapchainExtent() -> VkExtent2D;
auto GetSwapchainFormat() -> VkFormat;

void CreateSwapchain();
void CreateSwapchainImageViews();
void DestroySwapchainImageViews();
void DestroySwapchain();
void RecreateSwapchain();

// Frame lifecycle functions
void CreateFrameResources();
void DestroyFrameResources();

// Presentation-related functions
auto AcquireNextSwapchainImage(VkSemaphore imageAvailableSemaphore, VkFence inFlightFence) -> u32;
void PresentSwapchainImage(u32 imageIndex, VkSemaphore renderFinishedSemaphore);

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
void CreateRenderSyncObjects();
void DestroyRenderSyncObjects();

// Descriptor-related functions
void CreateDescriptorSet();
void DestroyDescriptorSet();

// Shader-related functions
auto CreateShader() -> VkShaderModule;
void DestroyShader();

// Pipeline-related functions
void CreateFullscreenPipeline();
void DestroyFullscreenPipeline();
void CreatePathTracerPipeline();
void DestroyPathTracerPipeline();

// Path tracer-related functions
void CreatePathTracerImage();
void DestroyPathTracerImage();
void CreatePathTracerDescriptors();
void DestroyPathTracerDescriptors();
void DispatchPathTracer();

// Drawing-related functions
void DrawFrame();

// High-level-related functions
void CreateVulkan();
void DestroyVulkan();
