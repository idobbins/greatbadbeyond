#pragma once

//------------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <cstddef>
#include <span>

//------------------------------------------------------------------------------------
// Primitive Type Aliases (C-style, C++20)
//------------------------------------------------------------------------------------
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

using b8  = bool;
using c8  = char;

using cstr = const char*;
using mut_cstr = char*;

using ptr = void*;
using cptr = const void*;

using usize = std::size_t;
using isize = std::ptrdiff_t;

//------------------------------------------------------------------------------------
// Common POD
//------------------------------------------------------------------------------------
struct Vec2 { f32 x, y; };
struct Size { i32 width, height; };

struct VulkanConfig {
    b8  debug;
    b8  portability;             // VK_KHR_portability_* (MoltenVK)
    b8  vsync;                   // FIFO if true; else MAILBOX/IMMEDIATE if available
    u32 frames_in_flight;        // 2 or 3
    VkSampleCountFlagBits msaa;  // VK_SAMPLE_COUNT_1_BIT.. etc
};

//====================================================================================
// Module: platform (GLFW-backed)
//====================================================================================
void CreateGlfwContext();
void DestroyGlfwContext();

void CreateWindow(i32 w, i32 h, cstr title);
void DestroyWindow();

auto WindowShouldClose()  -> b8;
auto IsWindowReady()      -> b8;
auto GetWindowSize()      -> Size;
auto GetFramebufferSize() -> Size;
auto GetWindowHandle()    -> GLFWwindow*;

void PollEvents();
void GlfwErrorCallback(i32 code, cstr description);
auto IsKeyPressed(i32 key) -> b8;

//====================================================================================
// Module: vulkan (lifecycle + global handles)
//====================================================================================
void CreateVulkan(const VulkanConfig& cfg);
void DestroyVulkan();                  // waits idle internally
void RecreateSwapchain();

auto GetInstance()        -> VkInstance;
auto GetDebugMessenger()  -> VkDebugUtilsMessengerEXT;
auto GetSurface()         -> VkSurfaceKHR;
auto GetPhysicalDevice()  -> VkPhysicalDevice;
auto GetDevice()          -> VkDevice;

auto GetFramesInFlight()  -> u32;

//====================================================================================
// Instance & Debug
//====================================================================================
auto GetPlatformVulkanExtensions() -> std::span<cstr>;
void CreateInstance(const VulkanConfig& cfg);
void DestroyInstance();

void CreateDebugMessenger();
void DestroyDebugMessenger();

VKAPI_ATTR auto VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT*,
    void*) -> VkBool32;

//====================================================================================
// Surface (GLFW)
//====================================================================================
void CreateSurface();
void DestroySurface();

//====================================================================================
// Physical Device (selection & queries)
//====================================================================================
auto GetPhysicalDevices()                                       -> std::span<const VkPhysicalDevice>;
auto GetQueueFamilyProperties(const VkPhysicalDevice&)          -> std::span<const VkQueueFamilyProperties>;
auto GetPhysicalDeviceFeatures2(const VkPhysicalDevice&)        -> const VkPhysicalDeviceFeatures2&;
auto GetPhysicalDeviceVulkan13Features(const VkPhysicalDevice&) -> const VkPhysicalDeviceVulkan13Features&;
auto GetPhysicalDeviceMemoryProperties(const VkPhysicalDevice&) -> const VkPhysicalDeviceMemoryProperties&;
auto GetPhysicalDeviceSurfaceCapabilities()                     -> VkSurfaceCapabilitiesKHR;
auto GetPhysicalDeviceSurfaceFormats()                          -> std::span<const VkSurfaceFormatKHR>;
auto GetPhysicalDeviceSurfacePresentModes()                     -> std::span<const VkPresentModeKHR>;

auto EnsurePhysicalDeviceSufficient()                           -> b8;

void SetPhysicalDevice(); // choose & cache

//====================================================================================
// Logical Device & Queues (dedicated queue management)
//====================================================================================
void CreateDevice(const VulkanConfig& cfg);
void DestroyDevice();

auto GetDeviceExtensionProperties()               -> std::span<const VkExtensionProperties>;
auto CheckDeviceExtensionSupport(std::span<cstr>) -> b8;

// Queue family indices (cached after SetPhysicalDevice)
auto GraphicsFamilyIndex() -> u32;
auto PresentFamilyIndex()  -> u32;
auto ComputeFamilyIndex()  -> u32;
auto TransferFamilyIndex() -> u32;

// Create queue handles from family indices (after CreateDevice)
void InitQueues();

auto GetGraphicsQueue() -> VkQueue;
auto GetPresentQueue()  -> VkQueue;
auto GetComputeQueue()  -> VkQueue;
auto GetTransferQueue() -> VkQueue;

// Submission helpers
void QueueSubmitGraphics(std::span<const VkSubmitInfo> submits, VkFence fence);
void QueueSubmitCompute (std::span<const VkSubmitInfo> submits, VkFence fence);
void QueueSubmitTransfer(std::span<const VkSubmitInfo> submits, VkFence fence);
void WaitGraphicsIdle();
void WaitPresentIdle();
void WaitComputeIdle();
void WaitTransferIdle();

//====================================================================================
// Swapchain & Views
//====================================================================================
void CreateSwapchain();
void DestroySwapchain();

auto GetSwapchainImages()     -> std::span<const VkImage>;
auto GetSwapchainImageViews() -> std::span<const VkImageView>;
auto GetSwapchainExtent()     -> VkExtent2D;
auto GetSwapchainFormat()     -> VkFormat;

auto ChooseSwapSurfaceFormat(std::span<const VkSurfaceFormatKHR>)                -> VkSurfaceFormatKHR;
auto ChooseSwapPresentMode(std::span<const VkPresentModeKHR>, b8 prefer_mailbox) -> VkPresentModeKHR;
auto ChooseSwapExtent(const VkSurfaceCapabilitiesKHR&, Size fb_size)             -> VkExtent2D;

auto CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect,
                     u32 mip_levels = 1, u32 base_mip = 0,
                     u32 layers = 1, u32 base_layer = 0) -> VkImageView;
void DestroyImageView(VkImageView view);

//====================================================================================
// Memory, Buffers, Images (VMA only)
//====================================================================================
void CreateVMAAllocator();
void DestroyVMAAllocator();
auto GetVMAAllocator() -> VmaAllocator;

auto CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VmaMemoryUsage mem_usage, VmaAllocationCreateFlags flags,
                  VkBuffer* out_buffer, VmaAllocation* out_alloc) -> b8;

auto CreateImage(u32 w, u32 h, VkFormat fmt, VkImageUsageFlags usage,
                 VmaMemoryUsage mem_usage, VmaAllocationCreateFlags flags,
                 VkImage* out_image, VmaAllocation* out_alloc,
                 u32 mip_levels = 1, u32 layers = 1,
                 VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) -> b8;

void DestroyBuffer(VkBuffer buf, VmaAllocation alloc);
void DestroyImage (VkImage img,  VmaAllocation alloc);

// Depth resources
auto FindSupportedFormat(std::span<const VkFormat> candidates,
                         VkImageTiling tiling,
                         VkFormatFeatureFlags features) -> VkFormat;
auto FindDepthFormat() -> VkFormat;
auto HasStencil(VkFormat depth_format) -> b8;

auto CreateDepthResources() -> b8;  // depth image + view sized to swapchain
void DestroyDepthResources();

auto GetDepthImage()     -> VkImage;
auto GetDepthImageView() -> VkImageView;
auto GetDepthFormat()    -> VkFormat;

// Sampler
auto CreateSampler(VkFilter min_f, VkFilter mag_f, VkSamplerAddressMode addr,
                   f32 max_aniso = 1.0f, u32 mip_levels = 1) -> VkSampler;
void DestroySampler(VkSampler);

// Staging & layout helpers
auto BeginSingleTimeCommands() -> VkCommandBuffer;
void EndSingleTimeCommands(VkCommandBuffer cmd);

void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
void CopyBufferToImage(VkBuffer src, VkImage dst, u32 w, u32 h,
                       u32 layers = 1, u32 base_layer = 0);

void TransitionImageLayout(VkImage image, VkFormat fmt,
                           VkImageLayout old_layout, VkImageLayout new_layout,
                           VkImageAspectFlags aspect,
                           u32 mip_levels = 1, u32 layers = 1);

//====================================================================================
// Descriptors & Pipeline Layout
//====================================================================================
void CreateDescriptorSetLayout();      // define bindings
void DestroyDescriptorSetLayout();

void CreateDescriptorPool(u32 max_sets);
void DestroyDescriptorPool();

void AllocateDescriptorSets(u32 count);
void FreeDescriptorSets();
void UpdateDescriptorSets();           // write UBO/SSBO/samplers

void CreatePipelineLayout();           // uses descriptor layouts + push constants
void DestroyPipelineLayout();

auto GetDescriptorSetLayout() -> VkDescriptorSetLayout;
auto GetPipelineLayout()      -> VkPipelineLayout;

//====================================================================================
// Shaders (SPIR-V)
//====================================================================================
auto CreateShaderModuleFromFile(cstr spv_path)                        -> VkShaderModule;
auto CreateShaderModuleFromMemory(const u32* words, usize word_count) -> VkShaderModule;
void DestroyShaderModule(VkShaderModule module);

//====================================================================================
// Pipelines (dynamic rendering only) + optional compute
//====================================================================================
void CreateGraphicsPipeline();   // uses VkPipelineRenderingCreateInfo (swapchain/depth formats)
void DestroyGraphicsPipeline();

void CreateComputePipeline();    // optional
void DestroyComputePipeline();

auto GetGraphicsPipeline() -> VkPipeline;
auto GetComputePipeline()  -> VkPipeline;

//====================================================================================
// Command Pools & Buffers
//====================================================================================
void CreateCommandPool(u32 queue_family_index);
void DestroyCommandPool();

void AllocateCommandBuffers(u32 count);           // primary CBs
void FreeCommandBuffers();

void BeginCommandBuffer(VkCommandBuffer cmd);
void EndCommandBuffer  (VkCommandBuffer cmd);

void RecordMainCommandBuffer(u32 image_index);    // record draw for current frame

//====================================================================================
// Synchronization (per-frame)
//====================================================================================
void CreateSyncObjects(u32 frames_in_flight);
void DestroySyncObjects();

auto GetImageAvailableSemaphore(u32 frame_index) -> VkSemaphore;
auto GetRenderFinishedSemaphore(u32 frame_index) -> VkSemaphore;
auto GetInFlightFence(u32 frame_index)           -> VkFence;

void WaitFrameFence(u32 frame_index);
void ResetFrameFence(u32 frame_index);
void WaitDeviceIdle();

//====================================================================================
// Frame Loop (acquire -> submit -> present)
//====================================================================================
auto GetCurrentFrame() -> u32;

auto AcquireNextImage(u32* out_image_index) -> b8; // false if out-of-date/suboptimal
void SubmitGraphics(u32 image_index, VkCommandBuffer cmd);
auto Present(u32 image_index) -> VkResult;

void DrawFrame(); // acquire -> record -> submit -> present

//====================================================================================
// Dynamic Rendering helpers
//====================================================================================
void CmdBeginRendering(VkCommandBuffer cmd,
                       VkImageView color_view,
                       VkImageView depth_view,
                       VkExtent2D extent,
                       VkClearColorValue color_clear,
                       float depth_clear = 1.0f);
void CmdEndRendering(VkCommandBuffer cmd);

//====================================================================================
// Utilities
//====================================================================================
auto IsSwapchainOutOfDate(VkResult r) -> b8; // checks OUT_OF_DATE/SUBOPTIMAL
