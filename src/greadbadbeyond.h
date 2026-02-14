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

struct Vec3
{
    float x;
    float y;
    float z;
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

struct FrameResources
{
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkFence inFlightFence;
    VkSemaphore imageAvailableSemaphore;
};

struct GradientParams
{
    Vec2 resolution;
    float time;
    float padding;
};

struct CameraParams
{
    Vec3 position;
    float verticalFovRadians;
    Vec3 forward;
    float aperture;
    Vec3 right;
    float focusDistance;
    Vec3 up;
    float pad3;
};

struct Vertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

struct ForwardPushConstants
{
    float model[16];
    float tint[4];
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
void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset);
auto ConsumeFramebufferResize() -> bool;
auto ConsumeMouseWheelDelta() -> float;

void CreateWindow();
void DestroyWindow();

// Input-related functions
auto IsKeyPressed() -> bool;

// Eventloop-related functions
void PollEvents();
void MainLoop();
auto GetFrameDeltaSeconds() -> float;
auto RequiresDebug() -> bool;
auto RequiresPortability() -> bool;

//------------------------------------------------------------------------------------
// Camera Functions
//------------------------------------------------------------------------------------

void CreateCamera();
void DestroyCamera();
void ResetCameraAccum();
void UpdateCameraFromInput(float deltaSeconds);
auto GetCameraParams() -> CameraParams;

//------------------------------------------------------------------------------------
// Asset Manifest Blob Functions (Module: manifest_blob)
//------------------------------------------------------------------------------------

void CreateManifestBlob();
void DestroyManifestBlob();
auto IsManifestBlobReady() -> bool;
auto GetManifestBlobBytes() -> std::span<const std::byte>;

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
auto AcquireNextImage(u32 &imageIndex, u32 &frameIndex) -> VkResult;
auto DrawFrameForward(u32 frameIndex, u32 imageIndex, const GradientParams &gradient) -> VkResult;
auto SubmitFrame(u32 frameIndex, u32 imageIndex) -> VkResult;

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
auto CreateShader(const char *path) -> VkShaderModule;
void DestroyShader(VkShaderModule &shader);
auto FindMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) -> u32;

// Pipeline-related functions
void CreateForwardPipeline();
void DestroyForwardPipeline();
void CreateFrameGlobalsResources();
void DestroyFrameGlobalsResources();
void UpdateFrameGlobals(const CameraParams &camera, VkExtent2D extent, float timeSeconds, u32 frameIndex);
void CreateShadowResources();
void DestroyShadowResources();
void CreateShadowPipeline();
void DestroyShadowPipeline();
void CreateForwardLightingResources();
void DestroyForwardLightingResources();
void CreateColorResources();
void DestroyColorResources();
void CreateDepthResources();
void DestroyDepthResources();
void CreateScene();
void DestroyScene();
void CreateForwardRenderer();
void DestroyForwardRenderer();
void UpdateForwardLightingData(const CameraParams &camera, VkExtent2D extent, float timeSeconds, u32 frameIndex);
void UpdateShadowCascades(const CameraParams &camera, VkExtent2D extent, u32 frameIndex);
void RecordShadowPass(VkCommandBuffer commandBuffer);

// Drawing-related functions
void DrawFrame();

// High-level-related functions
void CreateVulkan();
void DestroyVulkan();
