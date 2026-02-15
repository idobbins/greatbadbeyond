#include <greadbadbeyond.h>
#include <config.h>
#include <utils.h>
#include <manifest.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <zlib.h>

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory_resource>
#include <ostream>
#include <string>
#include <vector>

using namespace std;

#ifndef SHADER_CACHE_DIRECTORY
#define SHADER_CACHE_DIRECTORY ""
#endif

static constexpr const char *ShaderCacheDirectory = SHADER_CACHE_DIRECTORY;
static constexpr const char *ForwardVertexShaderName = "forward_opaque.vert.spv";
static constexpr const char *ForwardFragmentShaderName = "forward_opaque.frag.spv";
static constexpr const char *ShadowVertexShaderName = "shadow_depth.vert.spv";
static constexpr const char *SkyVertexShaderName = "sky.vert.spv";
static constexpr const char *SkyFragmentShaderName = "sky.frag.spv";
static constexpr u32 SceneGridWidth = 32;
static constexpr u32 SceneGridDepth = 32;
static constexpr float SceneGridSpacing = 1.15f;
static constexpr u32 ForwardTileSizePixels = 16;
static constexpr u32 ForwardMaxLights = 96;
static constexpr u32 ForwardMaxLightsPerTile = 64;
static constexpr u32 CsmCascadeCount = 3;
static constexpr u32 CsmShadowAtlasSize = 2048;
static constexpr float CsmSplitLambda = 0.62f;
static constexpr float CsmOverlapRatio = 0.12f;
static constexpr float CsmNearPlane = 0.05f;
static constexpr float CsmFarPlane = 200.0f;
static constexpr Vec3 SunDirection = {0.35f, 0.82f, 0.28f};
static constexpr u32 GpuTimestampSlotsPerFrame = 3;
static constexpr u32 GpuTimestampSlotShadowStart = 0;
static constexpr u32 GpuTimestampSlotShadowEnd = 1;
static constexpr u32 GpuTimestampSlotFrameEnd = 2;

struct ForwardGpuLight
{
   float positionRadius[4];
   float colorIntensity[4];
};

struct ForwardTileMeta
{
   u32 offset;
   u32 count;
};

struct InstanceData
{
   float translation[4];
};

struct alignas(16) FrameGlobalsGpu
{
   float viewProj[16];
   float cameraPosition[4];
   float sunDirection[4];
   u32 lightGrid[4];
   float frameParams[4];
};

struct ShadowAtlasRect
{
   u32 x;
   u32 y;
   u32 width;
   u32 height;
};

struct alignas(16) ShadowCascadeGpu
{
   float worldToShadow[16];
   float atlasRect[4];
   float params[4];
};

struct alignas(16) ShadowGlobalsGpu
{
   ShadowCascadeGpu cascades[CsmCascadeCount];
   float cameraForward[4];
   float atlasTexelSize[4];
};

struct ShadowCascadeRuntime
{
   float lightViewProj[16];
   VkRect2D atlasRectPixels;
};

struct ShadowPushConstants
{
   float mvp[16];
};

static constexpr array<ShadowAtlasRect, CsmCascadeCount> CsmAtlasRects = {
   ShadowAtlasRect{0u, 0u, 1024u, 1024u},
   ShadowAtlasRect{1024u, 0u, 512u, 512u},
   ShadowAtlasRect{1536u, 0u, 512u, 512u},
};

#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

static struct VulkanData
{
   VkInstance instance;
   VkDebugUtilsMessengerEXT debugMessenger;
   VkSurfaceKHR surface;
   VkPhysicalDevice physicalDevice;
   VkDevice device;

   VkQueue graphicsQueue;
   VkQueue presentQueue;
   VkQueue transferQueue;
   VkQueue computeQueue;

   u32 queueFamilyCount;
   u32 graphicsQueueFamilyIndex;
   u32 presentQueueFamilyIndex;
   u32 transferQueueFamilyIndex;
   u32 computeQueueFamilyIndex;
   VkSwapchainKHR swapchain;
   VkFormat swapchainFormat;
   VkExtent2D swapchainExtent;
   array<VkImage, MaxSwapchainImages> swapchainImages;
   array<VkImageView, MaxSwapchainImages> swapchainImageViews;
   array<VkImageLayout, MaxSwapchainImages> swapchainImageLayouts;
   array<VkSemaphore, MaxSwapchainImages> swapchainRenderFinishedSemaphores;
   array<VkFence, MaxSwapchainImages> swapchainImageFences;
   u32 swapchainImageCount;
   array<FrameResources, FrameOverlap> frames;
   VkQueryPool gpuTimestampQueryPool;
   float gpuTimestampPeriodNanoseconds;
   bool gpuTimestampsSupported;
   array<bool, FrameOverlap> gpuTimestampPending;
   u32 currentFrame;

   VkShaderModule forwardVertexShader;
   VkShaderModule forwardFragmentShader;
   VkShaderModule skyVertexShader;
   VkShaderModule skyFragmentShader;
   VkShaderModule shadowVertexShader;
   VkPipelineLayout shadowPipelineLayout;
   VkPipeline shadowPipeline;
   VkPipelineLayout forwardPipelineLayout;
   VkPipeline forwardPipeline;
   VkPipeline skyPipeline;
   VkDescriptorSetLayout forwardDescriptorSetLayout;
   VkDescriptorPool forwardDescriptorPool;
   array<VkDescriptorSet, FrameOverlap> forwardDescriptorSets;
   array<VkBuffer, FrameOverlap> frameGlobalsBuffers;
   array<VkDeviceMemory, FrameOverlap> frameGlobalsMemories;
   array<void *, FrameOverlap> frameGlobalsMapped;
   VkImage shadowAtlasImage;
   VkDeviceMemory shadowAtlasMemory;
   VkImageView shadowAtlasView;
   VkImageLayout shadowAtlasLayout;
   VkSampler shadowAtlasSampler;
   VkFormat shadowDepthFormat;
   array<VkBuffer, FrameOverlap> shadowGlobalsBuffers;
   array<VkDeviceMemory, FrameOverlap> shadowGlobalsMemories;
   array<void *, FrameOverlap> shadowGlobalsMapped;
   u32 shadowCascadeCount;
   array<ShadowCascadeRuntime, CsmCascadeCount> shadowCascadeRuntime;
   array<VkBuffer, FrameOverlap> forwardLightBuffers;
   array<VkDeviceMemory, FrameOverlap> forwardLightMemories;
   array<void *, FrameOverlap> forwardLightMapped;
   array<VkBuffer, FrameOverlap> forwardTileMetaBuffers;
   array<VkDeviceMemory, FrameOverlap> forwardTileMetaMemories;
   array<void *, FrameOverlap> forwardTileMetaMapped;
   array<VkBuffer, FrameOverlap> forwardTileIndexBuffers;
   array<VkDeviceMemory, FrameOverlap> forwardTileIndexMemories;
   array<void *, FrameOverlap> forwardTileIndexMapped;
   u32 forwardTileCountX;
   u32 forwardTileCountY;
   u32 forwardLightCount;
   vector<ForwardTileMeta> forwardTileMetaScratch;
   vector<u32> forwardTileIndexScratch;
   vector<ForwardGpuLight> forwardLightScratch;
   VkSampleCountFlagBits msaaSamples;
   VkImage colorImage;
   VkDeviceMemory colorMemory;
   VkImageView colorView;
   VkImageLayout colorLayout;
   VkImage depthImage;
   VkDeviceMemory depthMemory;
   VkImageView depthView;
   VkFormat depthFormat;
   VkImageLayout depthLayout;
   VkBuffer sceneVertexBuffer;
   VkDeviceMemory sceneVertexMemory;
   VkBuffer sceneIndexBuffer;
   VkDeviceMemory sceneIndexMemory;
   VkBuffer sceneInstanceBuffer;
   VkDeviceMemory sceneInstanceMemory;
   u32 sceneInstanceCount;
   u32 sceneCarInstanceCount;
   u32 sceneGroundInstanceIndex;
   u32 sceneCarIndexCount;
   u32 sceneGroundFirstIndex;
   u32 sceneGroundIndexCount;
   VkBuffer skyVertexBuffer;
   VkDeviceMemory skyVertexMemory;
   VkBuffer skyIndexBuffer;
   VkDeviceMemory skyIndexMemory;
   u32 skyIndexCount;
   VkBuffer uploadStagingBuffer;
   VkDeviceMemory uploadStagingMemory;
   void *uploadStagingMapped;
   VkDeviceSize uploadStagingCapacity;
   VkImage sceneTextureImage;
   VkDeviceMemory sceneTextureMemory;
   VkImageView sceneTextureView;
   VkSampler sceneTextureSampler;
   VkImageLayout sceneTextureLayout;
   u32 frameSeed;
   vector<byte> decodeScratch;

   bool instanceReady;
   bool validationLayersEnabled;
   bool debugMessengerReady;
   bool physicalDeviceReady;
   bool deviceReady;
   bool swapchainReady;
   bool swapchainImageViewsReady;
   bool frameResourcesReady;
   bool colorResourcesReady;
   bool depthResourcesReady;
   bool sceneReady;
   bool forwardRendererReady;
   bool frameGlobalsReady;
   bool shadowResourcesReady;
   bool shadowPipelineReady;
   bool forwardPipelineReady;
   bool forwardLightingReady;
   bool gpuTimestampsReady;

} Vulkan;

auto FindMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) -> u32
{
   Assert(Vulkan.physicalDevice != VK_NULL_HANDLE, "Physical device must be selected before querying memory types");

   VkPhysicalDeviceMemoryProperties memoryProperties = {};
   vkGetPhysicalDeviceMemoryProperties(Vulkan.physicalDevice, &memoryProperties);

   for (u32 index = 0; index < memoryProperties.memoryTypeCount; ++index)
   {
      bool typeSupported = (typeBits & (1u << index)) != 0;
      bool flagsMatch = (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;

      if (typeSupported && flagsMatch)
      {
         return index;
      }
   }

   Assert(false, "Failed to find compatible Vulkan memory type");
   return 0;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
    void *userData)
{
   (void)messageType;
   (void)userData;

   const char *message = (callbackData != NULL && callbackData->pMessage != NULL) ? callbackData->pMessage : "no message";

   if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
   {
      LogError("[vulkan] %s", message);
   }
   else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
   {
      LogWarn("[vulkan] %s", message);
   }
   else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
   {
      LogInfo("[vulkan] %s", message);
   }
   else
   {
      LogInfo("[vulkan][verbose] %s", message);
   }

   return VK_FALSE;
}

void CreateDebugMessenger()
{
   if (!Vulkan.validationLayersEnabled || Vulkan.debugMessengerReady)
   {
      return;
   }

   Assert(Vulkan.instance != VK_NULL_HANDLE, "Vulkan instance must exist before creating the debug messenger");

   VkDebugUtilsMessengerCreateInfoEXT createInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = DefaultDebugSeverityMask,
      .messageType = DefaultDebugTypeMask,
      .pfnUserCallback = VulkanDebugCallback,
      .pUserData = nullptr,
   };

   auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(Vulkan.instance, "vkCreateDebugUtilsMessengerEXT"));
   Assert(createFn != nullptr, "Failed to load vkCreateDebugUtilsMessengerEXT");

   VkResult result = createFn(Vulkan.instance, &createInfo, nullptr, &Vulkan.debugMessenger);
   Assert(result == VK_SUCCESS, "Failed to create Vulkan debug messenger");

   Vulkan.debugMessengerReady = true;
}

void DestroyDebugMessenger()
{
   if (Vulkan.debugMessenger == VK_NULL_HANDLE)
   {
      Vulkan.debugMessengerReady = false;
      return;
   }

   Assert(Vulkan.instance != VK_NULL_HANDLE, "Vulkan instance must be valid when destroying the debug messenger");

   auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(Vulkan.instance, "vkDestroyDebugUtilsMessengerEXT"));
   Assert(destroyFn != nullptr, "Failed to load vkDestroyDebugUtilsMessengerEXT");

   destroyFn(Vulkan.instance, Vulkan.debugMessenger, nullptr);
   Vulkan.debugMessenger = VK_NULL_HANDLE;
   Vulkan.debugMessengerReady = false;
}

void ResetCameraAccum()
{
   Vulkan.frameSeed = 0;
}

void CreateVulkan()
{
   ResetCameraAccum();
   CreateInstance();
   CreateDebugMessenger();
   CreateSurface();
   SetPhysicalDevice();

   CreateDevice();
   CreateSwapchain();
   CreateSwapchainImageViews();
   CreateScene();
   CreateForwardRenderer();
   CreateFrameResources();
}

void DestroyVulkan()
{
   if (Vulkan.device != VK_NULL_HANDLE)
   {
      vkDeviceWaitIdle(Vulkan.device);
   }

   DestroyFrameResources();
   DestroyForwardRenderer();
   DestroyScene();
   DestroySwapchain();
   DestroyDevice();
   DestroySurface();
   DestroyInstance();
}

void CreateInstance()
{
   bool debugEnabled = RequiresDebug();
   bool portabilityEnabled = RequiresPortability();

   VkApplicationInfo app_info {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = DefaultApplicationName,
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = DefaultEngineName,
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_3
   };

   // stack-only extensions
   static array<byte, InstanceExtensionScratchBytes> extensionBuffer;
   static pmr::monotonic_buffer_resource extensionStackOnlyResource {
      extensionBuffer.data(),
      extensionBuffer.size(),
      pmr::null_memory_resource() // Disallow heap fallback (stack-only vector)
   };
   pmr::vector<const char *> extensions {&extensionStackOnlyResource};

   // Build Instance Config
   for (auto ext : GetPlatformVulkanExtensions())
   {
      extensions.push_back(ext);
   }

   if (debugEnabled)
   {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
   }

   if (portabilityEnabled)
   {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
   }

   // stack-only layers
   static array<byte, InstanceLayerScratchBytes> layerBuffer;
   static pmr::monotonic_buffer_resource layerStackOnlyResource {
      layerBuffer.data(),
      layerBuffer.size(),
      pmr::null_memory_resource() // Disallow heap fallback (stack-only vector)
   };
   pmr::vector<const char *> layers {&layerStackOnlyResource };

   if (debugEnabled)
   {
      layers.push_back(ValidationLayerName);
   }

   for (auto ext: extensions)
   {
      cout << ext << endl;
   }

   VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { };
   if (debugEnabled)
   {
      debugCreateInfo = {
         .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
         .messageSeverity = DefaultDebugSeverityMask,
         .messageType = DefaultDebugTypeMask,
         .pfnUserCallback = VulkanDebugCallback,
         .pUserData = nullptr,
      };
   }

   VkInstanceCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = debugEnabled ? &debugCreateInfo : nullptr,
      .flags = portabilityEnabled ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0u,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

   VkResult result = vkCreateInstance(&createInfo, nullptr, &Vulkan.instance);
   Assert(result == VK_SUCCESS, "Failed to create Vulkan instance");

   Vulkan.validationLayersEnabled = (layers.size() > 0);
}

void DestroyInstance()
{
   DestroyDebugMessenger();

   if (Vulkan.instance != VK_NULL_HANDLE)
   {
      vkDestroyInstance(Vulkan.instance, nullptr);
      Vulkan.instance = VK_NULL_HANDLE;
   }

   Vulkan.validationLayersEnabled = false;
   Vulkan.debugMessengerReady = false;
   Vulkan.physicalDevice = VK_NULL_HANDLE;
   Vulkan.device = VK_NULL_HANDLE;
   Vulkan.graphicsQueue = VK_NULL_HANDLE;
   Vulkan.presentQueue = VK_NULL_HANDLE;
   Vulkan.transferQueue = VK_NULL_HANDLE;
   Vulkan.computeQueue = VK_NULL_HANDLE;
   Vulkan.queueFamilyCount = 0;
   Vulkan.graphicsQueueFamilyIndex = 0;
   Vulkan.presentQueueFamilyIndex = 0;
   Vulkan.transferQueueFamilyIndex = 0;
   Vulkan.computeQueueFamilyIndex = 0;
   Vulkan.physicalDeviceReady = false;
   Vulkan.deviceReady = false;
   Vulkan.swapchain = VK_NULL_HANDLE;
   Vulkan.swapchainReady = false;
   Vulkan.swapchainImageViewsReady = false;
   Vulkan.swapchainImageCount = 0;
   Vulkan.swapchainExtent = {0, 0};
   Vulkan.swapchainFormat = VK_FORMAT_UNDEFINED;
   Vulkan.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
   Vulkan.colorImage = VK_NULL_HANDLE;
   Vulkan.colorMemory = VK_NULL_HANDLE;
   Vulkan.colorView = VK_NULL_HANDLE;
   Vulkan.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   Vulkan.depthImage = VK_NULL_HANDLE;
   Vulkan.depthMemory = VK_NULL_HANDLE;
   Vulkan.depthView = VK_NULL_HANDLE;
   Vulkan.depthFormat = VK_FORMAT_UNDEFINED;
   Vulkan.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   Vulkan.sceneVertexBuffer = VK_NULL_HANDLE;
   Vulkan.sceneVertexMemory = VK_NULL_HANDLE;
   Vulkan.sceneIndexBuffer = VK_NULL_HANDLE;
   Vulkan.sceneIndexMemory = VK_NULL_HANDLE;
   Vulkan.sceneInstanceBuffer = VK_NULL_HANDLE;
   Vulkan.sceneInstanceMemory = VK_NULL_HANDLE;
   Vulkan.sceneInstanceCount = 0;
   Vulkan.sceneCarInstanceCount = 0;
   Vulkan.sceneGroundInstanceIndex = 0;
   Vulkan.sceneCarIndexCount = 0;
   Vulkan.sceneGroundFirstIndex = 0;
   Vulkan.sceneGroundIndexCount = 0;
   Vulkan.skyVertexBuffer = VK_NULL_HANDLE;
   Vulkan.skyVertexMemory = VK_NULL_HANDLE;
   Vulkan.skyIndexBuffer = VK_NULL_HANDLE;
   Vulkan.skyIndexMemory = VK_NULL_HANDLE;
   Vulkan.skyIndexCount = 0;
   Vulkan.uploadStagingBuffer = VK_NULL_HANDLE;
   Vulkan.uploadStagingMemory = VK_NULL_HANDLE;
   Vulkan.uploadStagingMapped = nullptr;
   Vulkan.uploadStagingCapacity = 0;
   Vulkan.decodeScratch.clear();
   Vulkan.colorResourcesReady = false;
   Vulkan.depthResourcesReady = false;
   Vulkan.sceneReady = false;
   Vulkan.forwardRendererReady = false;
   Vulkan.shadowResourcesReady = false;
   Vulkan.shadowPipelineReady = false;
   Vulkan.forwardPipelineReady = false;
   Vulkan.skyVertexShader = VK_NULL_HANDLE;
   Vulkan.skyFragmentShader = VK_NULL_HANDLE;
   Vulkan.shadowVertexShader = VK_NULL_HANDLE;
   Vulkan.shadowPipelineLayout = VK_NULL_HANDLE;
   Vulkan.shadowPipeline = VK_NULL_HANDLE;
   Vulkan.skyPipeline = VK_NULL_HANDLE;
   Vulkan.forwardDescriptorSetLayout = VK_NULL_HANDLE;
   Vulkan.forwardDescriptorPool = VK_NULL_HANDLE;
   Vulkan.forwardDescriptorSets.fill(VK_NULL_HANDLE);
   Vulkan.frameGlobalsBuffers.fill(VK_NULL_HANDLE);
   Vulkan.frameGlobalsMemories.fill(VK_NULL_HANDLE);
   Vulkan.frameGlobalsMapped.fill(nullptr);
   Vulkan.shadowAtlasImage = VK_NULL_HANDLE;
   Vulkan.shadowAtlasMemory = VK_NULL_HANDLE;
   Vulkan.shadowAtlasView = VK_NULL_HANDLE;
   Vulkan.shadowAtlasLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   Vulkan.shadowAtlasSampler = VK_NULL_HANDLE;
   Vulkan.shadowDepthFormat = VK_FORMAT_UNDEFINED;
   Vulkan.shadowGlobalsBuffers.fill(VK_NULL_HANDLE);
   Vulkan.shadowGlobalsMemories.fill(VK_NULL_HANDLE);
   Vulkan.shadowGlobalsMapped.fill(nullptr);
   Vulkan.shadowCascadeCount = 0;
   for (ShadowCascadeRuntime &cascade : Vulkan.shadowCascadeRuntime)
   {
      std::memset(cascade.lightViewProj, 0, sizeof(cascade.lightViewProj));
      cascade.atlasRectPixels = {};
   }
   Vulkan.forwardLightBuffers.fill(VK_NULL_HANDLE);
   Vulkan.forwardLightMemories.fill(VK_NULL_HANDLE);
   Vulkan.forwardLightMapped.fill(nullptr);
   Vulkan.forwardTileMetaBuffers.fill(VK_NULL_HANDLE);
   Vulkan.forwardTileMetaMemories.fill(VK_NULL_HANDLE);
   Vulkan.forwardTileMetaMapped.fill(nullptr);
   Vulkan.forwardTileIndexBuffers.fill(VK_NULL_HANDLE);
   Vulkan.forwardTileIndexMemories.fill(VK_NULL_HANDLE);
   Vulkan.forwardTileIndexMapped.fill(nullptr);
   Vulkan.forwardTileCountX = 0;
   Vulkan.forwardTileCountY = 0;
   Vulkan.forwardLightCount = 0;
   Vulkan.forwardTileMetaScratch.clear();
   Vulkan.forwardTileIndexScratch.clear();
   Vulkan.forwardLightScratch.clear();
   Vulkan.sceneTextureImage = VK_NULL_HANDLE;
   Vulkan.sceneTextureMemory = VK_NULL_HANDLE;
   Vulkan.sceneTextureView = VK_NULL_HANDLE;
   Vulkan.sceneTextureSampler = VK_NULL_HANDLE;
   Vulkan.sceneTextureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   Vulkan.frameGlobalsReady = false;
   Vulkan.forwardLightingReady = false;

   for (VkImage &image : Vulkan.swapchainImages)
   {
      image = VK_NULL_HANDLE;
   }

   for (VkImageView &imageView : Vulkan.swapchainImageViews)
   {
      imageView = VK_NULL_HANDLE;
   }
}

void CreateSurface()
{
   Assert(Vulkan.instance != VK_NULL_HANDLE, "Vulkan instance must be created before the surface");

   GLFWwindow *window = GetWindowHandle();
   Assert(window != nullptr, "GLFW window handle is null");

   VkResult result = glfwCreateWindowSurface(Vulkan.instance, window, nullptr, &Vulkan.surface);
   Assert(result == VK_SUCCESS, "Failed to create Vulkan surface");
}

void DestroySurface()
{
   if (Vulkan.surface == VK_NULL_HANDLE)
   {
      return;
   }

   Assert(Vulkan.instance != VK_NULL_HANDLE, "Vulkan instance must be valid when destroying the surface");

   vkDestroySurfaceKHR(Vulkan.instance, Vulkan.surface, nullptr);
   Vulkan.surface = VK_NULL_HANDLE;
}

auto GetPhysicalDeviceSurfaceCapabilities() -> VkSurfaceCapabilitiesKHR
{
   Assert(Vulkan.physicalDeviceReady, "Select a physical device before querying surface capabilities");
   Assert(Vulkan.surface != VK_NULL_HANDLE, "Create the Vulkan surface before querying surface capabilities");

   VkSurfaceCapabilitiesKHR capabilities = {};
   VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Vulkan.physicalDevice, Vulkan.surface, &capabilities);
   Assert(result == VK_SUCCESS, "Failed to query Vulkan surface capabilities");

   return capabilities;
}

auto GetPhysicalDeviceSurfaceFormats() -> span<const VkSurfaceFormatKHR>
{
   Assert(Vulkan.physicalDeviceReady, "Select a physical device before querying surface formats");
   Assert(Vulkan.surface != VK_NULL_HANDLE, "Create the Vulkan surface before querying surface formats");

   static array<VkSurfaceFormatKHR, MaxSurfaceFormats> formats {};
   static uint32_t cachedCount = 0;

   uint32_t count = 0;
   VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(Vulkan.physicalDevice, Vulkan.surface, &count, nullptr);
   Assert(result == VK_SUCCESS, "vkGetPhysicalDeviceSurfaceFormatsKHR (count) failed");
   Assert(count > 0, "Physical device reports zero surface formats");
   Assert(count <= formats.size(), "Too many Vulkan surface formats for cache");

   result = vkGetPhysicalDeviceSurfaceFormatsKHR(Vulkan.physicalDevice, Vulkan.surface, &count, formats.data());
   Assert(result == VK_SUCCESS, "vkGetPhysicalDeviceSurfaceFormatsKHR (fill) failed");

   cachedCount = count;
   return {formats.data(), cachedCount};
}

auto GetPhysicalDeviceSurfacePresentModes() -> span<const VkPresentModeKHR>
{
   Assert(Vulkan.physicalDeviceReady, "Select a physical device before querying present modes");
   Assert(Vulkan.surface != VK_NULL_HANDLE, "Create the Vulkan surface before querying present modes");

   static array<VkPresentModeKHR, MaxSurfacePresentModes> modes {};
   static uint32_t cachedCount = 0;

   uint32_t count = 0;
   VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(Vulkan.physicalDevice, Vulkan.surface, &count, nullptr);
   Assert(result == VK_SUCCESS, "vkGetPhysicalDeviceSurfacePresentModesKHR (count) failed");
   Assert(count > 0, "Physical device reports zero present modes");
   Assert(count <= modes.size(), "Too many Vulkan present modes for cache");

   result = vkGetPhysicalDeviceSurfacePresentModesKHR(Vulkan.physicalDevice, Vulkan.surface, &count, modes.data());
   Assert(result == VK_SUCCESS, "vkGetPhysicalDeviceSurfacePresentModesKHR (fill) failed");

   cachedCount = count;
   return {modes.data(), cachedCount};
}

auto GetPhysicalDeviceFeatures(const VkPhysicalDevice &device) -> const PhysicalDeviceFeatures&
{
   Assert(device != VK_NULL_HANDLE, "Physical device handle is null");

   static PhysicalDeviceFeatures features {};

   features.v13 = VkPhysicalDeviceVulkan13Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = nullptr,
   };

   features.core = VkPhysicalDeviceFeatures2{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &features.v13,
   };

   vkGetPhysicalDeviceFeatures2(device, &features.core);
   return features;
}

auto GetPhysicalDevices() -> span<const VkPhysicalDevice>
{
   static array<VkPhysicalDevice, MaxPhysicalDevices> cache {};
   static uint32_t count = 0;
   static bool ready = false;

   if (ready)
   {
      return {cache.data(), count};
   }

   Assert(Vulkan.instance != VK_NULL_HANDLE, "Vulkan instance must be created before enumerating physical devices");

   uint32_t physicalDeviceCount = 0;
   VkResult result = vkEnumeratePhysicalDevices(Vulkan.instance, &physicalDeviceCount, nullptr);
   Assert(result == VK_SUCCESS, "vkEnumeratePhysicalDevices (count) failed");
   Assert(physicalDeviceCount > 0, "No Vulkan-capable GPUs found");
   Assert(physicalDeviceCount <= cache.size(), "Too many Vulkan physical devices for cache");

   result = vkEnumeratePhysicalDevices(Vulkan.instance, &physicalDeviceCount, cache.data());
   Assert(result == VK_SUCCESS, "vkEnumeratePhysicalDevices (fill) failed");

   count = physicalDeviceCount;
   ready = true;

   return {cache.data(), count};
}

auto GetQueueFamilyProperties(const VkPhysicalDevice &device) -> span<const VkQueueFamilyProperties>
{
   Assert(device != VK_NULL_HANDLE, "Physical device handle is null");

   static array<VkQueueFamilyProperties, MaxQueueFamilies> properties {};
   static uint32_t familyCount = 0;

   uint32_t queriedCount = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(device, &queriedCount, nullptr);
   Assert(queriedCount > 0, "Physical device has no queue families");
   Assert(queriedCount <= properties.size(), "Too many queue families for cache entry");

   vkGetPhysicalDeviceQueueFamilyProperties(device, &queriedCount, properties.data());

   familyCount = queriedCount;
   return {properties.data(), familyCount};
}

auto GetQueueFamilies(
   const VkPhysicalDevice &device,
   VkSurfaceKHR surface,
   u32 &graphicsFamily,
   u32 &presentFamily,
   u32 &transferFamily,
   u32 &computeFamily) -> bool
{
   Assert(surface != VK_NULL_HANDLE, "Vulkan surface handle is null");
   Assert(device != VK_NULL_HANDLE, "Physical device handle is null");

   span<const VkQueueFamilyProperties> properties = GetQueueFamilyProperties(device);
   Assert(!properties.empty(), "Physical device reports zero queue families");

   bool graphicsReady = false;
   bool presentReady = false;
   bool transferReady = false;
   bool computeReady = false;

   for (uint32_t index = 0; index < properties.size(); ++index)
   {
      const VkQueueFamilyProperties &familyProperties = properties[index];

      if (!graphicsReady &&
          ((familyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) &&
          (familyProperties.queueCount > 0))
      {
         graphicsFamily = index;
         graphicsReady = true;
      }

      VkBool32 present = VK_FALSE;
      VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
      Assert(result == VK_SUCCESS, "Failed to query Vulkan surface support");

      if (!presentReady && (present == VK_TRUE) && (familyProperties.queueCount > 0))
      {
         presentFamily = index;
         presentReady = true;
      }

      if (!transferReady &&
          ((familyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0) &&
          (familyProperties.queueCount > 0))
      {
         transferFamily = index;
         transferReady = true;
      }

      if (!computeReady &&
          ((familyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) &&
          (familyProperties.queueCount > 0))
      {
         computeFamily = index;
         computeReady = true;
      }
   }

   return graphicsReady && presentReady && transferReady && computeReady;
}

void SetPhysicalDevice()
{
   if (Vulkan.physicalDeviceReady)
   {
      return;
   }

   Assert(Vulkan.instance != VK_NULL_HANDLE, "Vulkan instance must be created before selecting a physical device");
   Assert(Vulkan.surface != VK_NULL_HANDLE, "Vulkan surface must be created before selecting a physical device");
   Assert(!Vulkan.deviceReady, "Destroy the logical device before selecting a new physical device");

   span<const VkPhysicalDevice> devices = GetPhysicalDevices();
   Assert(!devices.empty(), "No Vulkan physical devices available");

   for (const VkPhysicalDevice &device : devices)
   {
      u32 graphicsFamily = 0;
      u32 presentFamily = 0;
      u32 transferFamily = 0;
      u32 computeFamily = 0;

      if (!GetQueueFamilies(device, Vulkan.surface, graphicsFamily, presentFamily, transferFamily, computeFamily))
      {
         continue;
      }

      VkPhysicalDeviceProperties properties = {};
      vkGetPhysicalDeviceProperties(device, &properties);

      if (properties.apiVersion < VK_API_VERSION_1_3)
      {
         continue;
      }

      VkSampleCountFlags supportedSampleCounts =
         properties.limits.framebufferColorSampleCounts &
         properties.limits.framebufferDepthSampleCounts;
      auto chooseSampleCount = [](VkSampleCountFlags supported, VkSampleCountFlagBits requested) -> VkSampleCountFlagBits
      {
         const std::array<VkSampleCountFlagBits, 7> candidates = {
            VK_SAMPLE_COUNT_64_BIT,
            VK_SAMPLE_COUNT_32_BIT,
            VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_8_BIT,
            VK_SAMPLE_COUNT_4_BIT,
            VK_SAMPLE_COUNT_2_BIT,
            VK_SAMPLE_COUNT_1_BIT,
         };

         bool allowCandidate = false;
         for (VkSampleCountFlagBits candidate : candidates)
         {
            if (candidate == requested)
            {
               allowCandidate = true;
            }

            if (!allowCandidate)
            {
               continue;
            }

            if ((supported & candidate) != 0)
            {
               return candidate;
            }
         }

         return VK_SAMPLE_COUNT_1_BIT;
      };

      const auto &features = GetPhysicalDeviceFeatures(device);
      const auto &features13 = features.v13;

      if ((features13.dynamicRendering != VK_TRUE) || (features13.synchronization2 != VK_TRUE))
      {
         continue;
      }

      Vulkan.physicalDevice = device;
      span<const VkQueueFamilyProperties> families = GetQueueFamilyProperties(device);
      Vulkan.queueFamilyCount = static_cast<u32>(families.size());
      Vulkan.graphicsQueueFamilyIndex = graphicsFamily;
      Vulkan.presentQueueFamilyIndex = presentFamily;
      Vulkan.transferQueueFamilyIndex = transferFamily;
      Vulkan.computeQueueFamilyIndex = computeFamily;
      Vulkan.physicalDeviceReady = true;
      Vulkan.deviceReady = false;
      Vulkan.graphicsQueue = VK_NULL_HANDLE;
      Vulkan.presentQueue = VK_NULL_HANDLE;
      Vulkan.transferQueue = VK_NULL_HANDLE;
      Vulkan.computeQueue = VK_NULL_HANDLE;
      Vulkan.msaaSamples = chooseSampleCount(supportedSampleCounts, preferredMsaaSamples);

      auto sampleCountToInt = [](VkSampleCountFlagBits sampleCount) -> u32
      {
         switch (sampleCount)
         {
            case VK_SAMPLE_COUNT_1_BIT: return 1;
            case VK_SAMPLE_COUNT_2_BIT: return 2;
            case VK_SAMPLE_COUNT_4_BIT: return 4;
            case VK_SAMPLE_COUNT_8_BIT: return 8;
            case VK_SAMPLE_COUNT_16_BIT: return 16;
            case VK_SAMPLE_COUNT_32_BIT: return 32;
            case VK_SAMPLE_COUNT_64_BIT: return 64;
            default: return 1;
         }
      };

      LogInfo("[vulkan] Selected physical device: %s (MSAA=%ux)", properties.deviceName, sampleCountToInt(Vulkan.msaaSamples));
      return;
   }

   Assert(false, "Failed to find a Vulkan physical device with required API support, features, and queue families");
}

void CreateDevice()
{
   if (Vulkan.deviceReady)
   {
      return;
   }

   Assert(Vulkan.physicalDeviceReady, "Select a physical device before creating the logical device");

   const auto &supportedFeatures = GetPhysicalDeviceFeatures(Vulkan.physicalDevice);
   const auto &supportedFeatures13 = supportedFeatures.v13;

   Assert(supportedFeatures13.dynamicRendering == VK_TRUE, "Physical device does not support dynamic rendering");
   Assert(supportedFeatures13.synchronization2 == VK_TRUE, "Physical device does not support synchronization2");

   VkPhysicalDeviceVulkan13Features features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .dynamicRendering = VK_TRUE,
      .synchronization2 = VK_TRUE,
   };

   VkPhysicalDeviceFeatures2 features2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &features13,
   };

   array<const char *, MaxDeviceExtensions> extensions = {};
   uint32_t extensionCount = 0;
   extensions[extensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
   Assert(extensionCount <= extensions.size(), "Too many requested device extensions");

   if (RequiresPortability())
   {
      extensions[extensionCount++] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
      Assert(extensionCount <= extensions.size(), "Too many requested device extensions");
   }

   uint32_t availableExtensionCount = 0;
   VkResult result = vkEnumerateDeviceExtensionProperties(Vulkan.physicalDevice, nullptr, &availableExtensionCount, nullptr);
   Assert(result == VK_SUCCESS, "vkEnumerateDeviceExtensionProperties (count) failed");
   Assert(availableExtensionCount <= MaxEnumeratedDeviceExtensions, "Too many Vulkan device extensions reported");

   array<VkExtensionProperties, MaxEnumeratedDeviceExtensions> availableExtensions = {};
   result = vkEnumerateDeviceExtensionProperties(Vulkan.physicalDevice, nullptr, &availableExtensionCount, availableExtensions.data());
   Assert(result == VK_SUCCESS, "vkEnumerateDeviceExtensionProperties (fill) failed");

   auto hasExtension = [&](const char *name) -> bool
   {
      for (uint32_t index = 0; index < availableExtensionCount; ++index)
      {
         if (strncmp(name, availableExtensions[index].extensionName, VK_MAX_EXTENSION_NAME_SIZE) == 0)
         {
            return true;
         }
      }

      return false;
   };

   Assert(hasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME), "Required Vulkan device extension VK_KHR_swapchain is missing");

   if (RequiresPortability())
   {
      Assert(hasExtension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME), "Required Vulkan device extension VK_KHR_portability_subset is missing");
   }

   Assert(Vulkan.queueFamilyCount > 0, "Queue families not discovered before device creation");
   Assert(Vulkan.graphicsQueueFamilyIndex < Vulkan.queueFamilyCount, "Invalid graphics queue family index");
   Assert(Vulkan.presentQueueFamilyIndex < Vulkan.queueFamilyCount, "Invalid present queue family index");
   Assert(Vulkan.transferQueueFamilyIndex < Vulkan.queueFamilyCount, "Invalid transfer queue family index");
   Assert(Vulkan.computeQueueFamilyIndex < Vulkan.queueFamilyCount, "Invalid compute queue family index");

   float queuePriority = 1.0f;

   array<uint32_t, 4> uniqueFamilies = {};
   array<VkDeviceQueueCreateInfo, 4> queueCreateInfos = {};
   uint32_t queueCreateInfoCount = 0;

   auto addQueueFamily = [&](uint32_t family)
   {
      for (uint32_t index = 0; index < queueCreateInfoCount; ++index)
      {
         if (uniqueFamilies[index] == family)
         {
            return;
         }
      }

      Assert(queueCreateInfoCount < queueCreateInfos.size(), "Too many queue families requested");

      uniqueFamilies[queueCreateInfoCount] = family;
      queueCreateInfos[queueCreateInfoCount] = {
         .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = family,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority,
      };

      queueCreateInfoCount += 1;
   };

   addQueueFamily(Vulkan.graphicsQueueFamilyIndex);
   addQueueFamily(Vulkan.presentQueueFamilyIndex);
   addQueueFamily(Vulkan.transferQueueFamilyIndex);
   addQueueFamily(Vulkan.computeQueueFamilyIndex);

   VkDeviceCreateInfo deviceCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features2,
      .queueCreateInfoCount = queueCreateInfoCount,
      .pQueueCreateInfos = queueCreateInfos.data(),
      .enabledExtensionCount = extensionCount,
      .ppEnabledExtensionNames = extensions.data(),
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .pEnabledFeatures = nullptr,
   };

   result = vkCreateDevice(Vulkan.physicalDevice, &deviceCreateInfo, nullptr, &Vulkan.device);
   Assert(result == VK_SUCCESS, "Failed to create Vulkan logical device");

   vkGetDeviceQueue(Vulkan.device, Vulkan.graphicsQueueFamilyIndex, 0, &Vulkan.graphicsQueue);
   Assert(Vulkan.graphicsQueue != VK_NULL_HANDLE, "Failed to retrieve Vulkan graphics queue");

   vkGetDeviceQueue(Vulkan.device, Vulkan.presentQueueFamilyIndex, 0, &Vulkan.presentQueue);
   Assert(Vulkan.presentQueue != VK_NULL_HANDLE, "Failed to retrieve Vulkan present queue");

   vkGetDeviceQueue(Vulkan.device, Vulkan.transferQueueFamilyIndex, 0, &Vulkan.transferQueue);
   Assert(Vulkan.transferQueue != VK_NULL_HANDLE, "Failed to retrieve Vulkan transfer queue");

   vkGetDeviceQueue(Vulkan.device, Vulkan.computeQueueFamilyIndex, 0, &Vulkan.computeQueue);
   Assert(Vulkan.computeQueue != VK_NULL_HANDLE, "Failed to retrieve Vulkan compute queue");

   Vulkan.deviceReady = true;

   LogInfo("[vulkan] Created logical device with queue families (graphics=%u present=%u transfer=%u compute=%u)",
           Vulkan.graphicsQueueFamilyIndex,
           Vulkan.presentQueueFamilyIndex,
           Vulkan.transferQueueFamilyIndex,
           Vulkan.computeQueueFamilyIndex);
}

void DestroyDevice()
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      return;
   }

   vkDeviceWaitIdle(Vulkan.device);
   vkDestroyDevice(Vulkan.device, nullptr);

   Vulkan.device = VK_NULL_HANDLE;
   Vulkan.graphicsQueue = VK_NULL_HANDLE;
   Vulkan.presentQueue = VK_NULL_HANDLE;
   Vulkan.transferQueue = VK_NULL_HANDLE;
   Vulkan.computeQueue = VK_NULL_HANDLE;
   Vulkan.deviceReady = false;
}

auto CreateShader(const char *path) -> VkShaderModule
{
   Assert(Vulkan.deviceReady, "Create the Vulkan device before creating shader modules");
   Assert(path != nullptr, "Shader path is null");

   FILE *file = std::fopen(path, "rb");
   Assert(file != nullptr, "Failed to open shader file");

   int seekResult = std::fseek(file, 0, SEEK_END);
   Assert(seekResult == 0, "Failed to seek to shader end");
   long fileSize = std::ftell(file);
   Assert(fileSize > 0, "Shader file is empty");
   Assert((fileSize % 4) == 0, "Shader file size must be a multiple of four bytes");
   std::rewind(file);

   vector<char> buffer(static_cast<size_t>(fileSize));
   size_t readSize = std::fread(buffer.data(), 1, buffer.size(), file);
   std::fclose(file);
   Assert(readSize == buffer.size(), "Failed to read shader file");

   VkShaderModuleCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = buffer.size(),
      .pCode = reinterpret_cast<const uint32_t *>(buffer.data()),
   };

   VkShaderModule module = VK_NULL_HANDLE;
   VkResult result = vkCreateShaderModule(Vulkan.device, &createInfo, nullptr, &module);
   Assert(result == VK_SUCCESS, "Failed to create shader module");
   return module;
}

void DestroyShader(VkShaderModule &shader)
{
   if ((shader == VK_NULL_HANDLE) || (Vulkan.device == VK_NULL_HANDLE))
   {
      shader = VK_NULL_HANDLE;
      return;
   }

   vkDestroyShaderModule(Vulkan.device, shader, nullptr);
   shader = VK_NULL_HANDLE;
}

auto GetGraphicsQueue() -> VkQueue
{
   Assert(Vulkan.deviceReady, "Create the Vulkan device before retrieving the graphics queue");
   Assert(Vulkan.graphicsQueue != VK_NULL_HANDLE, "Vulkan graphics queue is not initialized");
   return Vulkan.graphicsQueue;
}

auto GetComputeQueue() -> VkQueue
{
   Assert(Vulkan.deviceReady, "Create the Vulkan device before retrieving the compute queue");
   Assert(Vulkan.computeQueue != VK_NULL_HANDLE, "Vulkan compute queue is not initialized");
   return Vulkan.computeQueue;
}

auto GetTransferQueue() -> VkQueue
{
   Assert(Vulkan.deviceReady, "Create the Vulkan device before retrieving the transfer queue");
   Assert(Vulkan.transferQueue != VK_NULL_HANDLE, "Vulkan transfer queue is not initialized");
   return Vulkan.transferQueue;
}

auto GetPresentQueue() -> VkQueue
{
   Assert(Vulkan.deviceReady, "Create the Vulkan device before retrieving the present queue");
   Assert(Vulkan.presentQueue != VK_NULL_HANDLE, "Vulkan present queue is not initialized");
   return Vulkan.presentQueue;
}

auto GetSwapchainImages() -> span<const VkImage>
{
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before querying images");
   Assert(Vulkan.swapchainImageCount > 0, "Vulkan swapchain contains zero images");
   return {Vulkan.swapchainImages.data(), Vulkan.swapchainImageCount};
}

auto GetSwapchainImageViews() -> span<const VkImageView>
{
   Assert(Vulkan.swapchainImageViewsReady, "Create swapchain image views before querying them");
   Assert(Vulkan.swapchainImageCount > 0, "Vulkan swapchain contains zero images");
   return {Vulkan.swapchainImageViews.data(), Vulkan.swapchainImageCount};
}

auto GetSwapchainExtent() -> VkExtent2D
{
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before querying the extent");
   return Vulkan.swapchainExtent;
}

auto GetSwapchainFormat() -> VkFormat
{
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before querying the format");
   return Vulkan.swapchainFormat;
}

void CreateSwapchain()
{
   if (Vulkan.swapchainReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before the swapchain");
   Assert(Vulkan.surface != VK_NULL_HANDLE, "Create the Vulkan surface before the swapchain");

   VkSurfaceCapabilitiesKHR capabilities = GetPhysicalDeviceSurfaceCapabilities();
   span<const VkSurfaceFormatKHR> formats = GetPhysicalDeviceSurfaceFormats();
   span<const VkPresentModeKHR> presentModes = GetPhysicalDeviceSurfacePresentModes();

   const auto chooseSurfaceFormat = [&]() -> VkSurfaceFormatKHR
   {
      for (const VkSurfaceFormatKHR &format : formats)
      {
         if ((format.format == VK_FORMAT_B8G8R8A8_SRGB) &&
             (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
         {
            return format;
         }
      }

      return formats[0];
   };

   const auto choosePresentMode = [&]() -> VkPresentModeKHR
   {
      for (const VkPresentModeKHR &mode : presentModes)
      {
         if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
         {
            return mode;
         }
      }

      return VK_PRESENT_MODE_FIFO_KHR;
   };

   const auto clampValue = [](uint32_t value, uint32_t minValue, uint32_t maxValue) -> uint32_t
   {
      uint32_t result = value;

      if (result < minValue)
      {
         result = minValue;
      }

      if (result > maxValue)
      {
         result = maxValue;
      }

      return result;
   };

   VkExtent2D extent = {};
   if (capabilities.currentExtent.width != numeric_limits<uint32_t>::max())
   {
      extent = capabilities.currentExtent;
   }
   else
   {
      Size framebuffer = GetFramebufferSize();
      Assert((framebuffer.width > 0) && (framebuffer.height > 0), "Window framebuffer size is zero");

      extent.width = clampValue(static_cast<uint32_t>(framebuffer.width),
                                capabilities.minImageExtent.width,
                                capabilities.maxImageExtent.width);
      extent.height = clampValue(static_cast<uint32_t>(framebuffer.height),
                                 capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height);
   }

   uint32_t desiredImageCount = swapchainImageCount;
   if (desiredImageCount < capabilities.minImageCount)
   {
      desiredImageCount = capabilities.minImageCount;
   }

   if ((capabilities.maxImageCount > 0) && (desiredImageCount > capabilities.maxImageCount))
   {
      desiredImageCount = capabilities.maxImageCount;
   }

   Assert(desiredImageCount <= MaxSwapchainImages, "Requested swapchain images exceed cache capacity");

   VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat();
   VkPresentModeKHR presentMode = choosePresentMode();

   array<uint32_t, 2> queueFamilyIndices = {
      Vulkan.graphicsQueueFamilyIndex,
      Vulkan.presentQueueFamilyIndex
   };

   VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   uint32_t queueFamilyIndexCount = 0;
   const uint32_t *queueFamilyIndexPtr = nullptr;

   if (Vulkan.graphicsQueueFamilyIndex != Vulkan.presentQueueFamilyIndex)
   {
      sharingMode = VK_SHARING_MODE_CONCURRENT;
      queueFamilyIndexCount = 2;
      queueFamilyIndexPtr = queueFamilyIndices.data();
   }

   VkSwapchainCreateInfoKHR createInfo = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = Vulkan.surface,
      .minImageCount = desiredImageCount,
      .imageFormat = surfaceFormat.format,
      .imageColorSpace = surfaceFormat.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = sharingMode,
      .queueFamilyIndexCount = queueFamilyIndexCount,
      .pQueueFamilyIndices = queueFamilyIndexPtr,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = presentMode,
      .clipped = VK_TRUE,
      .oldSwapchain = Vulkan.swapchain,
   };

   VkResult result = vkCreateSwapchainKHR(Vulkan.device, &createInfo, nullptr, &Vulkan.swapchain);
   Assert(result == VK_SUCCESS, "Failed to create Vulkan swapchain");

   if (createInfo.oldSwapchain != VK_NULL_HANDLE)
   {
      vkDestroySwapchainKHR(Vulkan.device, createInfo.oldSwapchain, nullptr);
   }

   uint32_t imageCount = 0;
   result = vkGetSwapchainImagesKHR(Vulkan.device, Vulkan.swapchain, &imageCount, nullptr);
   Assert(result == VK_SUCCESS, "vkGetSwapchainImagesKHR (count) failed");
   Assert(imageCount > 0, "Vulkan swapchain returned zero images");
   Assert(imageCount <= Vulkan.swapchainImages.size(), "Swapchain image count exceeds cache size");

   result = vkGetSwapchainImagesKHR(Vulkan.device, Vulkan.swapchain, &imageCount, Vulkan.swapchainImages.data());
   Assert(result == VK_SUCCESS, "vkGetSwapchainImagesKHR (fill) failed");

   for (VkImageView &imageView : Vulkan.swapchainImageViews)
   {
      imageView = VK_NULL_HANDLE;
   }
   for (VkImageLayout &layout : Vulkan.swapchainImageLayouts)
   {
      layout = VK_IMAGE_LAYOUT_UNDEFINED;
   }

   Vulkan.swapchainImageCount = imageCount;
   Vulkan.swapchainExtent = extent;
   Vulkan.swapchainFormat = surfaceFormat.format;
   Vulkan.swapchainReady = true;
   Vulkan.swapchainImageViewsReady = false;

   LogInfo("[vulkan] Created swapchain %ux%u (%u images, format=%u, presentMode=%u)",
           extent.width,
           extent.height,
           Vulkan.swapchainImageCount,
           static_cast<uint32_t>(Vulkan.swapchainFormat),
           static_cast<uint32_t>(presentMode));
}

void CreateSwapchainImageViews()
{
   if (Vulkan.swapchainImageViewsReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before swapchain image views");
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before image views");
   Assert(Vulkan.swapchainImageCount > 0, "Vulkan swapchain contains zero images");

   for (uint32_t index = 0; index < Vulkan.swapchainImageCount; ++index)
   {
      VkImageViewCreateInfo createInfo = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = Vulkan.swapchainImages[index],
         .viewType = VK_IMAGE_VIEW_TYPE_2D,
         .format = Vulkan.swapchainFormat,
         .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
         },
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      };

      VkResult result = vkCreateImageView(Vulkan.device, &createInfo, nullptr, &Vulkan.swapchainImageViews[index]);
      Assert(result == VK_SUCCESS, "Failed to create Vulkan swapchain image view");
   }

   Vulkan.swapchainImageViewsReady = true;
}

void DestroySwapchainImageViews()
{
   if (Vulkan.swapchainImageCount == 0 || Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.swapchainImageViewsReady = false;
      return;
   }

   for (uint32_t index = 0; index < Vulkan.swapchainImageCount; ++index)
   {
      if (Vulkan.swapchainImageViews[index] != VK_NULL_HANDLE)
      {
         vkDestroyImageView(Vulkan.device, Vulkan.swapchainImageViews[index], nullptr);
         Vulkan.swapchainImageViews[index] = VK_NULL_HANDLE;
      }
   }

   Vulkan.swapchainImageViewsReady = false;
}

void DestroySwapchain()
{
   DestroySwapchainImageViews();

   if (Vulkan.swapchain == VK_NULL_HANDLE)
   {
      Vulkan.swapchainReady = false;
      Vulkan.swapchainImageViewsReady = false;
      Vulkan.swapchainImageCount = 0;
      Vulkan.swapchainExtent = {0, 0};
      Vulkan.swapchainFormat = VK_FORMAT_UNDEFINED;
      for (VkImageLayout &layout : Vulkan.swapchainImageLayouts)
      {
         layout = VK_IMAGE_LAYOUT_UNDEFINED;
      }
      for (VkSemaphore &semaphore : Vulkan.swapchainRenderFinishedSemaphores)
      {
         semaphore = VK_NULL_HANDLE;
      }
      for (VkFence &fence : Vulkan.swapchainImageFences)
      {
         fence = VK_NULL_HANDLE;
      }
      return;
   }

   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.swapchain = VK_NULL_HANDLE;
      Vulkan.swapchainReady = false;
      Vulkan.swapchainImageViewsReady = false;
      Vulkan.swapchainImageCount = 0;
      Vulkan.swapchainExtent = {0, 0};
      Vulkan.swapchainFormat = VK_FORMAT_UNDEFINED;
      for (VkImageLayout &layout : Vulkan.swapchainImageLayouts)
      {
         layout = VK_IMAGE_LAYOUT_UNDEFINED;
      }
      for (VkSemaphore &semaphore : Vulkan.swapchainRenderFinishedSemaphores)
      {
         semaphore = VK_NULL_HANDLE;
      }
      for (VkFence &fence : Vulkan.swapchainImageFences)
      {
         fence = VK_NULL_HANDLE;
      }
      return;
   }

   vkDestroySwapchainKHR(Vulkan.device, Vulkan.swapchain, nullptr);
   Vulkan.swapchain = VK_NULL_HANDLE;
   Vulkan.swapchainReady = false;
   Vulkan.swapchainImageViewsReady = false;
   Vulkan.swapchainImageCount = 0;
   Vulkan.swapchainExtent = {0, 0};
   Vulkan.swapchainFormat = VK_FORMAT_UNDEFINED;
   for (VkImageLayout &layout : Vulkan.swapchainImageLayouts)
   {
      layout = VK_IMAGE_LAYOUT_UNDEFINED;
   }
   for (VkSemaphore &semaphore : Vulkan.swapchainRenderFinishedSemaphores)
   {
      semaphore = VK_NULL_HANDLE;
   }
   for (VkFence &fence : Vulkan.swapchainImageFences)
   {
      fence = VK_NULL_HANDLE;
   }

   for (VkImage &image : Vulkan.swapchainImages)
   {
      image = VK_NULL_HANDLE;
   }
}

void CreateScene()
{
   if (Vulkan.sceneReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create Vulkan device before scene");

   Assert(IsManifestBlobReady(), "Create manifest pack before scene");
   std::span<const std::byte> manifestBlob = GetManifestBlobBytes();
   Assert(!manifestBlob.empty(), "Manifest pack is empty");

   manifest::ResolvedAsset sceneAsset = manifest::kenney::handles::n3d_assets::car_kit::models_obj_format_police_obj.Resolve(manifestBlob);
   Assert(sceneAsset.valid, "Scene asset handle failed to resolve");
   Assert(sceneAsset.format == manifest::AssetFormat::MESH_PNUV_F32_U32, "Scene asset is not a packed mesh payload");

   manifest::ResolvedAsset sceneTextureAsset = manifest::kenney::handles::n3d_assets::car_kit::models_obj_format_textures_colormap_png.Resolve(manifestBlob);
   Assert(sceneTextureAsset.valid, "Scene texture asset handle failed to resolve");
   Assert(sceneTextureAsset.format == manifest::AssetFormat::IMAGE_RGBA8_MIPS, "Scene texture asset is not a packed RGBA8 payload");

   std::span<const std::byte> payload = sceneAsset.payload;
   if (sceneAsset.compression == manifest::CompressionCodec::DEFLATE_ZLIB)
   {
      Assert(sceneAsset.decodedSize > 0, "Compressed scene mesh has zero decoded size");
      Assert(sceneAsset.decodedSize <= static_cast<u64>(numeric_limits<usize>::max()), "Decoded scene mesh exceeds addressable memory");
      Assert(sceneAsset.payload.size() <= static_cast<usize>(numeric_limits<uLong>::max()), "Compressed payload size exceeds zlib input limits");

      usize decodedSize = static_cast<usize>(sceneAsset.decodedSize);
      Vulkan.decodeScratch.resize(decodedSize);
      uLongf inflateSize = static_cast<uLongf>(decodedSize);
      int inflateResult = uncompress(
         reinterpret_cast<Bytef *>(Vulkan.decodeScratch.data()),
         &inflateSize,
         reinterpret_cast<const Bytef *>(sceneAsset.payload.data()),
         static_cast<uLong>(sceneAsset.payload.size()));
      Assert(inflateResult == Z_OK, "Failed to decompress scene mesh payload");
      Assert(inflateSize == decodedSize, "Scene mesh decompressed size mismatch");
      payload = {Vulkan.decodeScratch.data(), decodedSize};
   }
   else
   {
      Assert(sceneAsset.compression == manifest::CompressionCodec::NONE, "Unsupported scene asset compression codec");
      Vulkan.decodeScratch.clear();
   }

   usize vertexCount = static_cast<usize>(sceneAsset.meta0);
   usize indexCount = static_cast<usize>(sceneAsset.meta1);
   usize vertexStride = static_cast<usize>(sceneAsset.meta2);
   usize indexOffset = static_cast<usize>(sceneAsset.meta3);

   Assert(vertexCount > 0, "Packed scene mesh has zero vertices");
   Assert(indexCount > 0, "Packed scene mesh has zero indices");
   Assert(vertexStride == sizeof(Vertex), "Packed scene mesh vertex stride does not match Vertex layout");

   usize expectedVertexBytes = vertexCount * sizeof(Vertex);
   usize expectedIndexBytes = indexCount * sizeof(u32);
   usize expectedPayloadBytes = expectedVertexBytes + expectedIndexBytes;

   Assert(indexOffset == expectedVertexBytes, "Packed scene mesh index offset is invalid");
   Assert(payload.size() == expectedPayloadBytes, "Packed scene mesh payload size does not match metadata");

   std::vector<Vertex> baseVertices(vertexCount);
   std::vector<u32> baseIndices(indexCount);
   std::memcpy(baseVertices.data(), payload.data(), expectedVertexBytes);
   std::memcpy(baseIndices.data(), payload.data() + indexOffset, expectedIndexBytes);

   std::span<const std::byte> texturePayload = sceneTextureAsset.payload;
   std::vector<std::byte> textureDecodeScratch;
   if (sceneTextureAsset.compression == manifest::CompressionCodec::DEFLATE_ZLIB)
   {
      Assert(sceneTextureAsset.decodedSize > 0, "Compressed scene texture has zero decoded size");
      Assert(sceneTextureAsset.decodedSize <= static_cast<u64>(numeric_limits<usize>::max()), "Decoded scene texture exceeds addressable memory");
      Assert(sceneTextureAsset.payload.size() <= static_cast<usize>(numeric_limits<uLong>::max()), "Compressed scene texture payload exceeds zlib input limits");

      usize decodedSize = static_cast<usize>(sceneTextureAsset.decodedSize);
      textureDecodeScratch.resize(decodedSize);
      uLongf inflateSize = static_cast<uLongf>(decodedSize);
      int inflateResult = uncompress(
         reinterpret_cast<Bytef *>(textureDecodeScratch.data()),
         &inflateSize,
         reinterpret_cast<const Bytef *>(sceneTextureAsset.payload.data()),
         static_cast<uLong>(sceneTextureAsset.payload.size()));
      Assert(inflateResult == Z_OK, "Failed to decompress scene texture payload");
      Assert(inflateSize == decodedSize, "Scene texture decompressed size mismatch");
      texturePayload = {textureDecodeScratch.data(), decodedSize};
   }
   else
   {
      Assert(sceneTextureAsset.compression == manifest::CompressionCodec::NONE, "Unsupported scene texture compression codec");
   }

   Assert(texturePayload.size() >= sizeof(u32), "Scene texture payload is missing mip header");
   u32 mipCount = 0;
   std::memcpy(&mipCount, texturePayload.data(), sizeof(u32));
   Assert(mipCount > 0, "Scene texture payload has zero mip levels");

   usize mipDirectoryBytes = sizeof(u32) + static_cast<usize>(mipCount) * (sizeof(u32) * 4);
   Assert(texturePayload.size() >= mipDirectoryBytes, "Scene texture payload mip directory is truncated");

   const std::byte *mipEntry = texturePayload.data() + sizeof(u32);
   u32 textureWidth = 0;
   u32 textureHeight = 0;
   u32 textureOffset = 0;
   u32 textureSize = 0;
   std::memcpy(&textureWidth, mipEntry + sizeof(u32) * 0, sizeof(u32));
   std::memcpy(&textureHeight, mipEntry + sizeof(u32) * 1, sizeof(u32));
   std::memcpy(&textureOffset, mipEntry + sizeof(u32) * 2, sizeof(u32));
   std::memcpy(&textureSize, mipEntry + sizeof(u32) * 3, sizeof(u32));

   Assert(textureWidth > 0, "Scene texture width is zero");
   Assert(textureHeight > 0, "Scene texture height is zero");
   Assert(textureSize > 0, "Scene texture payload size is zero");
   Assert(textureOffset >= mipDirectoryBytes, "Scene texture mip payload offset overlaps the mip directory");
   Assert((static_cast<usize>(textureOffset) + static_cast<usize>(textureSize)) <= texturePayload.size(), "Scene texture mip payload is out of bounds");
   Assert(sceneTextureAsset.meta0 == textureWidth, "Scene texture width metadata mismatch");
   Assert(sceneTextureAsset.meta1 == textureHeight, "Scene texture height metadata mismatch");

   usize expectedTextureBytes = static_cast<usize>(textureWidth) * static_cast<usize>(textureHeight) * 4;
   Assert(static_cast<usize>(textureSize) == expectedTextureBytes, "Scene texture mip payload has unexpected byte count");
   std::span<const std::byte> textureLevel0Bytes = {
      texturePayload.data() + static_cast<usize>(textureOffset),
      static_cast<usize>(textureSize),
   };

   for (u32 index : baseIndices)
   {
      Assert(index < baseVertices.size(), "Packed scene mesh index references out-of-range vertex");
   }

   manifest::MeshBounds packedBounds = manifest::TryGetMeshBounds(sceneAsset);
   bool usePackedBounds = packedBounds.valid;

   // Center only on XZ so layout is world-ground aligned.
   Vec3 minBounds = {};
   Vec3 maxBounds = {};
   if (usePackedBounds)
   {
      minBounds = {packedBounds.minX, packedBounds.minY, packedBounds.minZ};
      maxBounds = {packedBounds.maxX, packedBounds.maxY, packedBounds.maxZ};
   }
   else
   {
      minBounds = baseVertices[0].position;
      maxBounds = baseVertices[0].position;
      for (const Vertex &vertex : baseVertices)
      {
         minBounds.x = std::min(minBounds.x, vertex.position.x);
         minBounds.y = std::min(minBounds.y, vertex.position.y);
         minBounds.z = std::min(minBounds.z, vertex.position.z);

         maxBounds.x = std::max(maxBounds.x, vertex.position.x);
         maxBounds.y = std::max(maxBounds.y, vertex.position.y);
         maxBounds.z = std::max(maxBounds.z, vertex.position.z);
      }
   }

   float extentX = maxBounds.x - minBounds.x;
   float extentZ = maxBounds.z - minBounds.z;
   float maxFootprintExtent = std::max(extentX, extentZ);
   if (maxFootprintExtent <= 0.000001f)
   {
      maxFootprintExtent = 1.0f;
   }
   float footprintScale = 1.0f / maxFootprintExtent;

   Vec3 centerXZ = {
      (minBounds.x + maxBounds.x) * 0.5f,
      0.0f,
      (minBounds.z + maxBounds.z) * 0.5f,
   };

   // Normalize mesh into a unit-cube XZ footprint and keep the base on y = 0.
   float minY = minBounds.y;
   for (Vertex &vertex : baseVertices)
   {
      vertex.position.x = (vertex.position.x - centerXZ.x) * footprintScale;
      vertex.position.y = (vertex.position.y - minY) * footprintScale;
      vertex.position.z = (vertex.position.z - centerXZ.z) * footprintScale;
   }

   Assert(!baseVertices.empty(), "Base mesh vertices cannot be empty");
   Assert(!baseIndices.empty(), "Base mesh indices cannot be empty");

   usize gridInstanceCount = static_cast<usize>(SceneGridWidth) * static_cast<usize>(SceneGridDepth);
   std::vector<InstanceData> sceneInstances;
   sceneInstances.reserve(gridInstanceCount + 1);

   float gridHalfWidth = static_cast<float>(SceneGridWidth) * 0.5f;
   float gridHalfDepth = static_cast<float>(SceneGridDepth) * 0.5f;
   for (u32 z = 0; z < SceneGridDepth; ++z)
   {
      for (u32 x = 0; x < SceneGridWidth; ++x)
      {
         float worldX = ((static_cast<float>(x) + 0.5f) - gridHalfWidth) * SceneGridSpacing;
         float worldZ = ((static_cast<float>(z) + 0.5f) - gridHalfDepth) * SceneGridSpacing;
         sceneInstances.push_back({
            .translation = {worldX, 0.0f, worldZ, 0.0f},
         });
      }
   }

   std::vector<Vertex> sceneVertices = baseVertices;
   std::vector<u32> sceneIndices = baseIndices;
   Vulkan.sceneCarIndexCount = static_cast<u32>(baseIndices.size());

   // Append one large ground quad to the scene mesh.
   float halfExtentX = (gridHalfWidth * SceneGridSpacing) + (2.0f * SceneGridSpacing);
   float halfExtentZ = (gridHalfDepth * SceneGridSpacing) + (2.0f * SceneGridSpacing);
   float groundY = -0.02f;
   u32 groundBaseIndex = static_cast<u32>(sceneVertices.size());
   sceneVertices.push_back({
      .position = {-halfExtentX, groundY, -halfExtentZ},
      .normal = {0.0f, 1.0f, 0.0f},
      .uv = {-halfExtentX, -halfExtentZ},
   });
   sceneVertices.push_back({
      .position = {halfExtentX, groundY, -halfExtentZ},
      .normal = {0.0f, 1.0f, 0.0f},
      .uv = {halfExtentX, -halfExtentZ},
   });
   sceneVertices.push_back({
      .position = {halfExtentX, groundY, halfExtentZ},
      .normal = {0.0f, 1.0f, 0.0f},
      .uv = {halfExtentX, halfExtentZ},
   });
   sceneVertices.push_back({
      .position = {-halfExtentX, groundY, halfExtentZ},
      .normal = {0.0f, 1.0f, 0.0f},
      .uv = {-halfExtentX, halfExtentZ},
   });
   Vulkan.sceneGroundFirstIndex = static_cast<u32>(sceneIndices.size());
   sceneIndices.push_back(groundBaseIndex + 0);
   sceneIndices.push_back(groundBaseIndex + 1);
   sceneIndices.push_back(groundBaseIndex + 2);
   sceneIndices.push_back(groundBaseIndex + 2);
   sceneIndices.push_back(groundBaseIndex + 3);
   sceneIndices.push_back(groundBaseIndex + 0);
   Vulkan.sceneGroundIndexCount = static_cast<u32>(sceneIndices.size()) - Vulkan.sceneGroundFirstIndex;

   Vulkan.sceneCarInstanceCount = static_cast<u32>(gridInstanceCount);
   Vulkan.sceneGroundInstanceIndex = static_cast<u32>(sceneInstances.size());
   sceneInstances.push_back({
      .translation = {0.0f, 0.0f, 0.0f, 0.0f},
   });
   Vulkan.sceneInstanceCount = static_cast<u32>(sceneInstances.size());

   Assert(!sceneVertices.empty(), "Scene vertices cannot be empty");
   Assert(!sceneIndices.empty(), "Scene indices cannot be empty");
   Assert(!sceneInstances.empty(), "Scene instances cannot be empty");

   // Sky is uploaded as a separate mesh and rendered by a separate pipeline.
   std::vector<Vertex> skyVertices = {
      {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
   };
   std::vector<u32> skyIndices = {
      4, 5, 6, 6, 7, 4,
      1, 0, 3, 3, 2, 1,
      0, 4, 7, 7, 3, 0,
      5, 1, 2, 2, 6, 5,
      3, 7, 6, 6, 2, 3,
      0, 1, 5, 5, 4, 0,
   };
   Vulkan.skyIndexCount = static_cast<u32>(skyIndices.size());
   Assert(!skyVertices.empty(), "Sky vertices cannot be empty");
   Assert(!skyIndices.empty(), "Sky indices cannot be empty");

   const auto createDeviceLocalBuffer = [&](VkBufferUsageFlags usage, VkDeviceSize size, VkBuffer &buffer, VkDeviceMemory &memory)
   {
      VkBufferCreateInfo bufferInfo = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
         .size = size,
         .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      };

      VkResult createResult = vkCreateBuffer(Vulkan.device, &bufferInfo, nullptr, &buffer);
      Assert(createResult == VK_SUCCESS, "Failed to create scene buffer");

      VkMemoryRequirements requirements = {};
      vkGetBufferMemoryRequirements(Vulkan.device, buffer, &requirements);

      VkMemoryAllocateInfo allocInfo = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = requirements.size,
         .memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
      };

      VkResult allocResult = vkAllocateMemory(Vulkan.device, &allocInfo, nullptr, &memory);
      Assert(allocResult == VK_SUCCESS, "Failed to allocate scene buffer memory");

      VkResult bindResult = vkBindBufferMemory(Vulkan.device, buffer, memory, 0);
      Assert(bindResult == VK_SUCCESS, "Failed to bind scene buffer memory");
   };

   VkImageCreateInfo textureImageInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_SRGB,
      .extent = {textureWidth, textureHeight, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VkResult textureImageResult = vkCreateImage(Vulkan.device, &textureImageInfo, nullptr, &Vulkan.sceneTextureImage);
   Assert(textureImageResult == VK_SUCCESS, "Failed to create scene texture image");

   VkMemoryRequirements textureRequirements = {};
   vkGetImageMemoryRequirements(Vulkan.device, Vulkan.sceneTextureImage, &textureRequirements);
   VkMemoryAllocateInfo textureAllocInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = textureRequirements.size,
      .memoryTypeIndex = FindMemoryType(textureRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };
   VkResult textureAllocResult = vkAllocateMemory(Vulkan.device, &textureAllocInfo, nullptr, &Vulkan.sceneTextureMemory);
   Assert(textureAllocResult == VK_SUCCESS, "Failed to allocate scene texture image memory");

   VkResult textureBindResult = vkBindImageMemory(Vulkan.device, Vulkan.sceneTextureImage, Vulkan.sceneTextureMemory, 0);
   Assert(textureBindResult == VK_SUCCESS, "Failed to bind scene texture image memory");

   VkImageViewCreateInfo textureViewInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = Vulkan.sceneTextureImage,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_SRGB,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };
   VkResult textureViewResult = vkCreateImageView(Vulkan.device, &textureViewInfo, nullptr, &Vulkan.sceneTextureView);
   Assert(textureViewResult == VK_SUCCESS, "Failed to create scene texture image view");

   VkSamplerCreateInfo textureSamplerInfo = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .minLod = 0.0f,
      .maxLod = 0.0f,
      .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
      .unnormalizedCoordinates = VK_FALSE,
   };
   VkResult textureSamplerResult = vkCreateSampler(Vulkan.device, &textureSamplerInfo, nullptr, &Vulkan.sceneTextureSampler);
   Assert(textureSamplerResult == VK_SUCCESS, "Failed to create scene texture sampler");
   Vulkan.sceneTextureLayout = VK_IMAGE_LAYOUT_UNDEFINED;

   const auto createOrResizeStagingBuffer = [&](VkDeviceSize requiredSize)
   {
      Assert(requiredSize > 0, "Staging buffer size must be non-zero");

      if ((Vulkan.uploadStagingBuffer != VK_NULL_HANDLE) && (Vulkan.uploadStagingCapacity >= requiredSize))
      {
         return;
      }

      if (Vulkan.uploadStagingMapped != nullptr)
      {
         vkUnmapMemory(Vulkan.device, Vulkan.uploadStagingMemory);
         Vulkan.uploadStagingMapped = nullptr;
      }
      if (Vulkan.uploadStagingBuffer != VK_NULL_HANDLE)
      {
         vkDestroyBuffer(Vulkan.device, Vulkan.uploadStagingBuffer, nullptr);
         Vulkan.uploadStagingBuffer = VK_NULL_HANDLE;
      }
      if (Vulkan.uploadStagingMemory != VK_NULL_HANDLE)
      {
         vkFreeMemory(Vulkan.device, Vulkan.uploadStagingMemory, nullptr);
         Vulkan.uploadStagingMemory = VK_NULL_HANDLE;
      }

      VkBufferCreateInfo bufferInfo = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
         .size = requiredSize,
         .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      };

      VkResult createResult = vkCreateBuffer(Vulkan.device, &bufferInfo, nullptr, &Vulkan.uploadStagingBuffer);
      Assert(createResult == VK_SUCCESS, "Failed to create staging buffer");

      VkMemoryRequirements requirements = {};
      vkGetBufferMemoryRequirements(Vulkan.device, Vulkan.uploadStagingBuffer, &requirements);

      VkMemoryAllocateInfo allocInfo = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = requirements.size,
         .memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
      };

      VkResult allocResult = vkAllocateMemory(Vulkan.device, &allocInfo, nullptr, &Vulkan.uploadStagingMemory);
      Assert(allocResult == VK_SUCCESS, "Failed to allocate staging buffer memory");

      VkResult bindResult = vkBindBufferMemory(Vulkan.device, Vulkan.uploadStagingBuffer, Vulkan.uploadStagingMemory, 0);
      Assert(bindResult == VK_SUCCESS, "Failed to bind staging buffer memory");

      void *mapped = nullptr;
      VkResult mapResult = vkMapMemory(Vulkan.device, Vulkan.uploadStagingMemory, 0, requiredSize, 0, &mapped);
      Assert(mapResult == VK_SUCCESS, "Failed to map staging buffer memory");

      Vulkan.uploadStagingMapped = mapped;
      Vulkan.uploadStagingCapacity = requiredSize;
   };

   VkDeviceSize sceneVertexBytes = static_cast<VkDeviceSize>(sceneVertices.size() * sizeof(Vertex));
   VkDeviceSize sceneIndexBytes = static_cast<VkDeviceSize>(sceneIndices.size() * sizeof(u32));
   VkDeviceSize sceneInstanceBytes = static_cast<VkDeviceSize>(sceneInstances.size() * sizeof(InstanceData));
   VkDeviceSize skyVertexBytes = static_cast<VkDeviceSize>(skyVertices.size() * sizeof(Vertex));
   VkDeviceSize skyIndexBytes = static_cast<VkDeviceSize>(skyIndices.size() * sizeof(u32));
   VkDeviceSize textureBytes = static_cast<VkDeviceSize>(textureLevel0Bytes.size());
   VkDeviceSize sceneIndexUploadOffset = sceneVertexBytes;
   VkDeviceSize sceneInstanceUploadOffset = sceneIndexUploadOffset + sceneIndexBytes;
   VkDeviceSize skyVertexUploadOffset = sceneInstanceUploadOffset + sceneInstanceBytes;
   VkDeviceSize skyIndexUploadOffset = skyVertexUploadOffset + skyVertexBytes;
   VkDeviceSize textureUploadOffset = skyIndexUploadOffset + skyIndexBytes;
   VkDeviceSize totalUploadBytes = textureUploadOffset + textureBytes;

   createOrResizeStagingBuffer(totalUploadBytes);
   Assert(Vulkan.uploadStagingMapped != nullptr, "Staging buffer is not mapped");

   byte *stagingBytes = reinterpret_cast<byte *>(Vulkan.uploadStagingMapped);
   std::memcpy(stagingBytes, sceneVertices.data(), static_cast<size_t>(sceneVertexBytes));
   std::memcpy(stagingBytes + static_cast<usize>(sceneIndexUploadOffset), sceneIndices.data(), static_cast<size_t>(sceneIndexBytes));
   std::memcpy(stagingBytes + static_cast<usize>(sceneInstanceUploadOffset), sceneInstances.data(), static_cast<size_t>(sceneInstanceBytes));
   std::memcpy(stagingBytes + static_cast<usize>(skyVertexUploadOffset), skyVertices.data(), static_cast<size_t>(skyVertexBytes));
   std::memcpy(stagingBytes + static_cast<usize>(skyIndexUploadOffset), skyIndices.data(), static_cast<size_t>(skyIndexBytes));
   std::memcpy(stagingBytes + static_cast<usize>(textureUploadOffset), textureLevel0Bytes.data(), static_cast<size_t>(textureBytes));

   createDeviceLocalBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sceneVertexBytes, Vulkan.sceneVertexBuffer, Vulkan.sceneVertexMemory);
   createDeviceLocalBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sceneIndexBytes, Vulkan.sceneIndexBuffer, Vulkan.sceneIndexMemory);
   createDeviceLocalBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sceneInstanceBytes, Vulkan.sceneInstanceBuffer, Vulkan.sceneInstanceMemory);
   createDeviceLocalBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, skyVertexBytes, Vulkan.skyVertexBuffer, Vulkan.skyVertexMemory);
   createDeviceLocalBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, skyIndexBytes, Vulkan.skyIndexBuffer, Vulkan.skyIndexMemory);

   VkCommandPool uploadPool = VK_NULL_HANDLE;
   VkCommandBuffer uploadCmd = VK_NULL_HANDLE;
   VkFence uploadFence = VK_NULL_HANDLE;

   VkCommandPoolCreateInfo poolInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = Vulkan.graphicsQueueFamilyIndex,
   };
   VkResult poolResult = vkCreateCommandPool(Vulkan.device, &poolInfo, nullptr, &uploadPool);
   Assert(poolResult == VK_SUCCESS, "Failed to create upload command pool");

   VkCommandBufferAllocateInfo cmdAlloc = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = uploadPool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkResult cmdAllocResult = vkAllocateCommandBuffers(Vulkan.device, &cmdAlloc, &uploadCmd);
   Assert(cmdAllocResult == VK_SUCCESS, "Failed to allocate upload command buffer");

   VkCommandBufferBeginInfo cmdBegin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   VkResult cmdBeginResult = vkBeginCommandBuffer(uploadCmd, &cmdBegin);
   Assert(cmdBeginResult == VK_SUCCESS, "Failed to begin upload command buffer");

   VkBufferCopy vertexCopy = {
      .srcOffset = 0,
      .dstOffset = 0,
      .size = sceneVertexBytes,
   };
   vkCmdCopyBuffer(uploadCmd, Vulkan.uploadStagingBuffer, Vulkan.sceneVertexBuffer, 1, &vertexCopy);

   VkBufferCopy indexCopy = {
      .srcOffset = sceneIndexUploadOffset,
      .dstOffset = 0,
      .size = sceneIndexBytes,
   };
   vkCmdCopyBuffer(uploadCmd, Vulkan.uploadStagingBuffer, Vulkan.sceneIndexBuffer, 1, &indexCopy);

   VkBufferCopy instanceCopy = {
      .srcOffset = sceneInstanceUploadOffset,
      .dstOffset = 0,
      .size = sceneInstanceBytes,
   };
   vkCmdCopyBuffer(uploadCmd, Vulkan.uploadStagingBuffer, Vulkan.sceneInstanceBuffer, 1, &instanceCopy);

   VkBufferCopy skyVertexCopy = {
      .srcOffset = skyVertexUploadOffset,
      .dstOffset = 0,
      .size = skyVertexBytes,
   };
   vkCmdCopyBuffer(uploadCmd, Vulkan.uploadStagingBuffer, Vulkan.skyVertexBuffer, 1, &skyVertexCopy);

   VkBufferCopy skyIndexCopy = {
      .srcOffset = skyIndexUploadOffset,
      .dstOffset = 0,
      .size = skyIndexBytes,
   };
   vkCmdCopyBuffer(uploadCmd, Vulkan.uploadStagingBuffer, Vulkan.skyIndexBuffer, 1, &skyIndexCopy);

   VkImageSubresourceRange textureSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
   };
   VkImageMemoryBarrier textureToTransferBarrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = Vulkan.sceneTextureLayout,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = Vulkan.sceneTextureImage,
      .subresourceRange = textureSubresource,
   };
   vkCmdPipelineBarrier(
      uploadCmd,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &textureToTransferBarrier);

   VkBufferImageCopy textureCopy = {
      .bufferOffset = textureUploadOffset,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
      .imageOffset = {0, 0, 0},
      .imageExtent = {textureWidth, textureHeight, 1},
   };
   vkCmdCopyBufferToImage(
      uploadCmd,
      Vulkan.uploadStagingBuffer,
      Vulkan.sceneTextureImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &textureCopy);

   VkImageMemoryBarrier textureToShaderReadBarrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = Vulkan.sceneTextureImage,
      .subresourceRange = textureSubresource,
   };
   vkCmdPipelineBarrier(
      uploadCmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &textureToShaderReadBarrier);

   VkResult cmdEndResult = vkEndCommandBuffer(uploadCmd);
   Assert(cmdEndResult == VK_SUCCESS, "Failed to end upload command buffer");

   VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
   };
   VkResult fenceResult = vkCreateFence(Vulkan.device, &fenceInfo, nullptr, &uploadFence);
   Assert(fenceResult == VK_SUCCESS, "Failed to create upload fence");

   VkSubmitInfo submitInfo = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &uploadCmd,
   };
   VkResult submitResult = vkQueueSubmit(Vulkan.graphicsQueue, 1, &submitInfo, uploadFence);
   Assert(submitResult == VK_SUCCESS, "Failed to submit upload command buffer");

   VkResult waitResult = vkWaitForFences(Vulkan.device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
   Assert(waitResult == VK_SUCCESS, "Failed to wait for upload fence");

   vkDestroyFence(Vulkan.device, uploadFence, nullptr);
   vkDestroyCommandPool(Vulkan.device, uploadPool, nullptr);

   Vulkan.sceneTextureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   Assert(Vulkan.sceneCarIndexCount > 0, "Scene car index count is zero");
   Assert(Vulkan.sceneGroundIndexCount > 0, "Scene ground index count is zero");
   Assert(Vulkan.sceneCarInstanceCount > 0, "Scene car instance count is zero");
   Assert(Vulkan.sceneInstanceCount > 0, "Scene instance count is zero");
   Assert(Vulkan.skyIndexCount > 0, "Sky index count is zero");
   Vulkan.sceneReady = true;
}

void DestroyScene()
{
   if (!Vulkan.sceneReady)
   {
      return;
   }

   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.sceneVertexBuffer = VK_NULL_HANDLE;
      Vulkan.sceneVertexMemory = VK_NULL_HANDLE;
      Vulkan.sceneIndexBuffer = VK_NULL_HANDLE;
      Vulkan.sceneIndexMemory = VK_NULL_HANDLE;
      Vulkan.sceneInstanceBuffer = VK_NULL_HANDLE;
      Vulkan.sceneInstanceMemory = VK_NULL_HANDLE;
      Vulkan.sceneInstanceCount = 0;
      Vulkan.sceneCarInstanceCount = 0;
      Vulkan.sceneGroundInstanceIndex = 0;
      Vulkan.sceneCarIndexCount = 0;
      Vulkan.sceneGroundFirstIndex = 0;
      Vulkan.sceneGroundIndexCount = 0;
      Vulkan.skyVertexBuffer = VK_NULL_HANDLE;
      Vulkan.skyVertexMemory = VK_NULL_HANDLE;
      Vulkan.skyIndexBuffer = VK_NULL_HANDLE;
      Vulkan.skyIndexMemory = VK_NULL_HANDLE;
      Vulkan.skyIndexCount = 0;
      Vulkan.sceneTextureImage = VK_NULL_HANDLE;
      Vulkan.sceneTextureMemory = VK_NULL_HANDLE;
      Vulkan.sceneTextureView = VK_NULL_HANDLE;
      Vulkan.sceneTextureSampler = VK_NULL_HANDLE;
      Vulkan.sceneTextureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      Vulkan.uploadStagingBuffer = VK_NULL_HANDLE;
      Vulkan.uploadStagingMemory = VK_NULL_HANDLE;
      Vulkan.uploadStagingMapped = nullptr;
      Vulkan.uploadStagingCapacity = 0;
      Vulkan.decodeScratch.clear();
      Vulkan.sceneReady = false;
      return;
   }

   if (Vulkan.sceneVertexBuffer != VK_NULL_HANDLE)
   {
      vkDestroyBuffer(Vulkan.device, Vulkan.sceneVertexBuffer, nullptr);
      Vulkan.sceneVertexBuffer = VK_NULL_HANDLE;
   }
   if (Vulkan.sceneVertexMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.sceneVertexMemory, nullptr);
      Vulkan.sceneVertexMemory = VK_NULL_HANDLE;
   }

   if (Vulkan.sceneIndexBuffer != VK_NULL_HANDLE)
   {
      vkDestroyBuffer(Vulkan.device, Vulkan.sceneIndexBuffer, nullptr);
      Vulkan.sceneIndexBuffer = VK_NULL_HANDLE;
   }
   if (Vulkan.sceneIndexMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.sceneIndexMemory, nullptr);
      Vulkan.sceneIndexMemory = VK_NULL_HANDLE;
   }
   if (Vulkan.sceneInstanceBuffer != VK_NULL_HANDLE)
   {
      vkDestroyBuffer(Vulkan.device, Vulkan.sceneInstanceBuffer, nullptr);
      Vulkan.sceneInstanceBuffer = VK_NULL_HANDLE;
   }
   if (Vulkan.sceneInstanceMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.sceneInstanceMemory, nullptr);
      Vulkan.sceneInstanceMemory = VK_NULL_HANDLE;
   }
   if (Vulkan.skyVertexBuffer != VK_NULL_HANDLE)
   {
      vkDestroyBuffer(Vulkan.device, Vulkan.skyVertexBuffer, nullptr);
      Vulkan.skyVertexBuffer = VK_NULL_HANDLE;
   }
   if (Vulkan.skyVertexMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.skyVertexMemory, nullptr);
      Vulkan.skyVertexMemory = VK_NULL_HANDLE;
   }
   if (Vulkan.skyIndexBuffer != VK_NULL_HANDLE)
   {
      vkDestroyBuffer(Vulkan.device, Vulkan.skyIndexBuffer, nullptr);
      Vulkan.skyIndexBuffer = VK_NULL_HANDLE;
   }
   if (Vulkan.skyIndexMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.skyIndexMemory, nullptr);
      Vulkan.skyIndexMemory = VK_NULL_HANDLE;
   }

   if (Vulkan.sceneTextureSampler != VK_NULL_HANDLE)
   {
      vkDestroySampler(Vulkan.device, Vulkan.sceneTextureSampler, nullptr);
      Vulkan.sceneTextureSampler = VK_NULL_HANDLE;
   }
   if (Vulkan.sceneTextureView != VK_NULL_HANDLE)
   {
      vkDestroyImageView(Vulkan.device, Vulkan.sceneTextureView, nullptr);
      Vulkan.sceneTextureView = VK_NULL_HANDLE;
   }
   if (Vulkan.sceneTextureImage != VK_NULL_HANDLE)
   {
      vkDestroyImage(Vulkan.device, Vulkan.sceneTextureImage, nullptr);
      Vulkan.sceneTextureImage = VK_NULL_HANDLE;
   }
   if (Vulkan.sceneTextureMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.sceneTextureMemory, nullptr);
      Vulkan.sceneTextureMemory = VK_NULL_HANDLE;
   }
   Vulkan.sceneTextureLayout = VK_IMAGE_LAYOUT_UNDEFINED;

   if (Vulkan.uploadStagingMapped != nullptr)
   {
      vkUnmapMemory(Vulkan.device, Vulkan.uploadStagingMemory);
      Vulkan.uploadStagingMapped = nullptr;
   }
   if (Vulkan.uploadStagingBuffer != VK_NULL_HANDLE)
   {
      vkDestroyBuffer(Vulkan.device, Vulkan.uploadStagingBuffer, nullptr);
      Vulkan.uploadStagingBuffer = VK_NULL_HANDLE;
   }
   if (Vulkan.uploadStagingMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.uploadStagingMemory, nullptr);
      Vulkan.uploadStagingMemory = VK_NULL_HANDLE;
   }
   Vulkan.uploadStagingCapacity = 0;
   Vulkan.decodeScratch.clear();

   Vulkan.sceneInstanceCount = 0;
   Vulkan.sceneCarInstanceCount = 0;
   Vulkan.sceneGroundInstanceIndex = 0;
   Vulkan.sceneCarIndexCount = 0;
   Vulkan.sceneGroundFirstIndex = 0;
   Vulkan.sceneGroundIndexCount = 0;
   Vulkan.skyIndexCount = 0;
   Vulkan.sceneReady = false;
}

void CreateColorResources()
{
   if (Vulkan.msaaSamples == VK_SAMPLE_COUNT_1_BIT)
   {
      Vulkan.colorResourcesReady = false;
      Vulkan.colorImage = VK_NULL_HANDLE;
      Vulkan.colorMemory = VK_NULL_HANDLE;
      Vulkan.colorView = VK_NULL_HANDLE;
      Vulkan.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      return;
   }

   if (Vulkan.colorResourcesReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before color resources");
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before color resources");
   Assert(Vulkan.msaaSamples != static_cast<VkSampleCountFlagBits>(0), "MSAA sample count is not initialized");

   VkExtent2D extent = Vulkan.swapchainExtent;
   Assert((extent.width > 0) && (extent.height > 0), "Swapchain extent is invalid for color resources");

   VkImageCreateInfo imageInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = Vulkan.swapchainFormat,
      .extent = {extent.width, extent.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = Vulkan.msaaSamples,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   VkResult imageResult = vkCreateImage(Vulkan.device, &imageInfo, nullptr, &Vulkan.colorImage);
   Assert(imageResult == VK_SUCCESS, "Failed to create color image");

   VkMemoryRequirements requirements = {};
   vkGetImageMemoryRequirements(Vulkan.device, Vulkan.colorImage, &requirements);

   VkMemoryAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = requirements.size,
      .memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };

   VkResult allocResult = vkAllocateMemory(Vulkan.device, &allocInfo, nullptr, &Vulkan.colorMemory);
   Assert(allocResult == VK_SUCCESS, "Failed to allocate color image memory");

   VkResult bindResult = vkBindImageMemory(Vulkan.device, Vulkan.colorImage, Vulkan.colorMemory, 0);
   Assert(bindResult == VK_SUCCESS, "Failed to bind color image memory");

   VkImageViewCreateInfo viewInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = Vulkan.colorImage,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = Vulkan.swapchainFormat,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };

   VkResult viewResult = vkCreateImageView(Vulkan.device, &viewInfo, nullptr, &Vulkan.colorView);
   Assert(viewResult == VK_SUCCESS, "Failed to create color image view");

   Vulkan.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   Vulkan.colorResourcesReady = true;
}

void DestroyColorResources()
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.colorResourcesReady = false;
      Vulkan.colorImage = VK_NULL_HANDLE;
      Vulkan.colorMemory = VK_NULL_HANDLE;
      Vulkan.colorView = VK_NULL_HANDLE;
      Vulkan.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      return;
   }

   if (Vulkan.colorView != VK_NULL_HANDLE)
   {
      vkDestroyImageView(Vulkan.device, Vulkan.colorView, nullptr);
      Vulkan.colorView = VK_NULL_HANDLE;
   }

   if (Vulkan.colorImage != VK_NULL_HANDLE)
   {
      vkDestroyImage(Vulkan.device, Vulkan.colorImage, nullptr);
      Vulkan.colorImage = VK_NULL_HANDLE;
   }

   if (Vulkan.colorMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.colorMemory, nullptr);
      Vulkan.colorMemory = VK_NULL_HANDLE;
   }

   Vulkan.colorResourcesReady = false;
   Vulkan.colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void CreateDepthResources()
{
   if (Vulkan.depthResourcesReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before depth resources");
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before depth resources");
   Assert(Vulkan.physicalDeviceReady, "Select a physical device before creating depth resources");

   Assert(Vulkan.msaaSamples != static_cast<VkSampleCountFlagBits>(0), "MSAA sample count is not initialized");

   VkFormat selectedFormat = VK_FORMAT_UNDEFINED;
   const std::array<VkFormat, 3> depthCandidates = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D32_SFLOAT_S8_UINT,
      VK_FORMAT_D24_UNORM_S8_UINT,
   };

   for (VkFormat candidate : depthCandidates)
   {
      VkFormatProperties properties = {};
      vkGetPhysicalDeviceFormatProperties(Vulkan.physicalDevice, candidate, &properties);
      if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
      {
         selectedFormat = candidate;
         break;
      }
   }

   Assert(selectedFormat != VK_FORMAT_UNDEFINED, "No supported depth format found");
   Vulkan.depthFormat = selectedFormat;

   VkExtent2D extent = Vulkan.swapchainExtent;
   Assert((extent.width > 0) && (extent.height > 0), "Swapchain extent is invalid for depth resources");

   VkImageCreateInfo imageInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = Vulkan.depthFormat,
      .extent = {extent.width, extent.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = Vulkan.msaaSamples,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   VkResult imageResult = vkCreateImage(Vulkan.device, &imageInfo, nullptr, &Vulkan.depthImage);
   Assert(imageResult == VK_SUCCESS, "Failed to create depth image");

   VkMemoryRequirements requirements = {};
   vkGetImageMemoryRequirements(Vulkan.device, Vulkan.depthImage, &requirements);

   VkMemoryAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = requirements.size,
      .memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };

   VkResult allocResult = vkAllocateMemory(Vulkan.device, &allocInfo, nullptr, &Vulkan.depthMemory);
   Assert(allocResult == VK_SUCCESS, "Failed to allocate depth image memory");

   VkResult bindResult = vkBindImageMemory(Vulkan.device, Vulkan.depthImage, Vulkan.depthMemory, 0);
   Assert(bindResult == VK_SUCCESS, "Failed to bind depth image memory");

   bool hasStencil = (Vulkan.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) ||
                     (Vulkan.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT);
   VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
   if (hasStencil)
   {
      aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
   }

   VkImageViewCreateInfo viewInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = Vulkan.depthImage,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = Vulkan.depthFormat,
      .subresourceRange = {
         .aspectMask = aspectMask,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };

   VkResult viewResult = vkCreateImageView(Vulkan.device, &viewInfo, nullptr, &Vulkan.depthView);
   Assert(viewResult == VK_SUCCESS, "Failed to create depth image view");

   Vulkan.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   Vulkan.depthResourcesReady = true;
}

void DestroyDepthResources()
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.depthResourcesReady = false;
      Vulkan.depthImage = VK_NULL_HANDLE;
      Vulkan.depthMemory = VK_NULL_HANDLE;
      Vulkan.depthView = VK_NULL_HANDLE;
      Vulkan.depthFormat = VK_FORMAT_UNDEFINED;
      Vulkan.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      return;
   }

   if (Vulkan.depthView != VK_NULL_HANDLE)
   {
      vkDestroyImageView(Vulkan.device, Vulkan.depthView, nullptr);
      Vulkan.depthView = VK_NULL_HANDLE;
   }

   if (Vulkan.depthImage != VK_NULL_HANDLE)
   {
      vkDestroyImage(Vulkan.device, Vulkan.depthImage, nullptr);
      Vulkan.depthImage = VK_NULL_HANDLE;
   }

   if (Vulkan.depthMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.depthMemory, nullptr);
      Vulkan.depthMemory = VK_NULL_HANDLE;
   }

   Vulkan.depthResourcesReady = false;
   Vulkan.depthFormat = VK_FORMAT_UNDEFINED;
   Vulkan.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void CreateFrameGlobalsResources()
{
   if (Vulkan.frameGlobalsReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create Vulkan device before frame globals resources");
   Assert(Vulkan.swapchainReady, "Create Vulkan swapchain before frame globals resources");

   VkBufferCreateInfo bufferInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sizeof(FrameGlobalsGpu),
      .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };

   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      VkResult createResult = vkCreateBuffer(Vulkan.device, &bufferInfo, nullptr, &Vulkan.frameGlobalsBuffers[frameIndex]);
      Assert(createResult == VK_SUCCESS, "Failed to create frame globals buffer");

      VkMemoryRequirements requirements = {};
      vkGetBufferMemoryRequirements(Vulkan.device, Vulkan.frameGlobalsBuffers[frameIndex], &requirements);
      VkMemoryAllocateInfo allocInfo = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = requirements.size,
         .memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
      };
      VkResult allocResult = vkAllocateMemory(Vulkan.device, &allocInfo, nullptr, &Vulkan.frameGlobalsMemories[frameIndex]);
      Assert(allocResult == VK_SUCCESS, "Failed to allocate frame globals memory");

      VkResult bindResult = vkBindBufferMemory(Vulkan.device, Vulkan.frameGlobalsBuffers[frameIndex], Vulkan.frameGlobalsMemories[frameIndex], 0);
      Assert(bindResult == VK_SUCCESS, "Failed to bind frame globals memory");

      void *mapped = nullptr;
      VkResult mapResult = vkMapMemory(Vulkan.device, Vulkan.frameGlobalsMemories[frameIndex], 0, VK_WHOLE_SIZE, 0, &mapped);
      Assert(mapResult == VK_SUCCESS, "Failed to map frame globals memory");
      Assert(mapped != nullptr, "Frame globals mapping returned null");
      Vulkan.frameGlobalsMapped[frameIndex] = mapped;
      std::memset(Vulkan.frameGlobalsMapped[frameIndex], 0, sizeof(FrameGlobalsGpu));
   }

   Vulkan.frameGlobalsReady = true;
}

void DestroyFrameGlobalsResources()
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.frameGlobalsBuffers.fill(VK_NULL_HANDLE);
      Vulkan.frameGlobalsMemories.fill(VK_NULL_HANDLE);
      Vulkan.frameGlobalsMapped.fill(nullptr);
      Vulkan.frameGlobalsReady = false;
      return;
   }

   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      if (Vulkan.frameGlobalsMapped[frameIndex] != nullptr)
      {
         vkUnmapMemory(Vulkan.device, Vulkan.frameGlobalsMemories[frameIndex]);
         Vulkan.frameGlobalsMapped[frameIndex] = nullptr;
      }
      if (Vulkan.frameGlobalsBuffers[frameIndex] != VK_NULL_HANDLE)
      {
         vkDestroyBuffer(Vulkan.device, Vulkan.frameGlobalsBuffers[frameIndex], nullptr);
         Vulkan.frameGlobalsBuffers[frameIndex] = VK_NULL_HANDLE;
      }
      if (Vulkan.frameGlobalsMemories[frameIndex] != VK_NULL_HANDLE)
      {
         vkFreeMemory(Vulkan.device, Vulkan.frameGlobalsMemories[frameIndex], nullptr);
         Vulkan.frameGlobalsMemories[frameIndex] = VK_NULL_HANDLE;
      }
   }

   Vulkan.frameGlobalsReady = false;
}

void UpdateFrameGlobals(const CameraParams &camera, VkExtent2D extent, float timeSeconds, u32 frameIndex)
{
   Assert(frameIndex < FrameOverlap, "Frame globals frame index is out of range");
   Assert(Vulkan.frameGlobalsReady, "Frame globals resources are not ready");
   Assert(Vulkan.frameGlobalsMapped[frameIndex] != nullptr, "Frame globals buffer is not mapped");
   Assert((extent.width > 0) && (extent.height > 0), "Frame globals update requires non-zero extent");

   const auto dot3 = [](const Vec3 &a, const Vec3 &b) -> float
   {
      return a.x*b.x + a.y*b.y + a.z*b.z;
   };
   const auto multiplyMat4 = [](const float *a, const float *b, float *result)
   {
      float temp[16] = {};
      for (int column = 0; column < 4; ++column)
      {
         for (int row = 0; row < 4; ++row)
         {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
               sum += a[k*4 + row] * b[column*4 + k];
            }
            temp[column*4 + row] = sum;
         }
      }
      std::memcpy(result, temp, sizeof(temp));
   };

   float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
   if (aspect <= 0.0f)
   {
      aspect = 1.0f;
   }

   float view[16] = {};
   view[0] = camera.right.x;
   view[1] = camera.up.x;
   view[2] = -camera.forward.x;
   view[3] = 0.0f;
   view[4] = camera.right.y;
   view[5] = camera.up.y;
   view[6] = -camera.forward.y;
   view[7] = 0.0f;
   view[8] = camera.right.z;
   view[9] = camera.up.z;
   view[10] = -camera.forward.z;
   view[11] = 0.0f;
   view[12] = -dot3(camera.right, camera.position);
   view[13] = -dot3(camera.up, camera.position);
   view[14] = dot3(camera.forward, camera.position);
   view[15] = 1.0f;

   float proj[16] = {};
   float tanHalfFov = std::tan(camera.verticalFovRadians * 0.5f);
   if (tanHalfFov <= 0.0f)
   {
      tanHalfFov = 0.001f;
   }
   float focal = 1.0f / tanHalfFov;
   proj[0] = focal / aspect;
   proj[5] = -focal;
   proj[10] = CsmFarPlane / (CsmNearPlane - CsmFarPlane);
   proj[11] = -1.0f;
   proj[14] = (CsmNearPlane * CsmFarPlane) / (CsmNearPlane - CsmFarPlane);

   FrameGlobalsGpu globals = {};
   multiplyMat4(proj, view, globals.viewProj);
   globals.cameraPosition[0] = camera.position.x;
   globals.cameraPosition[1] = camera.position.y;
   globals.cameraPosition[2] = camera.position.z;
   globals.cameraPosition[3] = 0.0f;
   globals.sunDirection[0] = SunDirection.x;
   globals.sunDirection[1] = SunDirection.y;
   globals.sunDirection[2] = SunDirection.z;
   globals.sunDirection[3] = 0.0f;
   globals.lightGrid[0] = Vulkan.forwardLightCount;
   globals.lightGrid[1] = Vulkan.forwardTileCountX;
   globals.lightGrid[2] = Vulkan.forwardTileCountY;
   globals.lightGrid[3] = ForwardTileSizePixels;
   globals.frameParams[0] = timeSeconds;
   globals.frameParams[1] = static_cast<float>(extent.width);
   globals.frameParams[2] = static_cast<float>(extent.height);
   globals.frameParams[3] = 0.0f;

   std::memcpy(Vulkan.frameGlobalsMapped[frameIndex], &globals, sizeof(globals));
}

void CreateShadowResources()
{
   if (Vulkan.shadowResourcesReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create Vulkan device before shadow resources");
   Assert(Vulkan.physicalDeviceReady, "Select a physical device before shadow resources");

   VkFormat selectedFormat = VK_FORMAT_UNDEFINED;
   const array<VkFormat, 3> depthCandidates = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D16_UNORM,
      VK_FORMAT_D24_UNORM_S8_UINT,
   };

   for (VkFormat candidate : depthCandidates)
   {
      VkFormatProperties properties = {};
      vkGetPhysicalDeviceFormatProperties(Vulkan.physicalDevice, candidate, &properties);
      bool supportsDepthAttachment = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
      bool supportsSampling = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
      if (supportsDepthAttachment && supportsSampling)
      {
         selectedFormat = candidate;
         break;
      }
   }

   Assert(selectedFormat != VK_FORMAT_UNDEFINED, "No depth format supports both depth attachment and depth sampling for CSM");
   Vulkan.shadowDepthFormat = selectedFormat;

   VkImageCreateInfo imageInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = Vulkan.shadowDepthFormat,
      .extent = {CsmShadowAtlasSize, CsmShadowAtlasSize, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VkResult imageResult = vkCreateImage(Vulkan.device, &imageInfo, nullptr, &Vulkan.shadowAtlasImage);
   Assert(imageResult == VK_SUCCESS, "Failed to create CSM shadow atlas image");

   VkMemoryRequirements imageRequirements = {};
   vkGetImageMemoryRequirements(Vulkan.device, Vulkan.shadowAtlasImage, &imageRequirements);
   VkMemoryAllocateInfo imageAllocInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = imageRequirements.size,
      .memoryTypeIndex = FindMemoryType(imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };
   VkResult imageAllocResult = vkAllocateMemory(Vulkan.device, &imageAllocInfo, nullptr, &Vulkan.shadowAtlasMemory);
   Assert(imageAllocResult == VK_SUCCESS, "Failed to allocate CSM shadow atlas memory");

   VkResult imageBindResult = vkBindImageMemory(Vulkan.device, Vulkan.shadowAtlasImage, Vulkan.shadowAtlasMemory, 0);
   Assert(imageBindResult == VK_SUCCESS, "Failed to bind CSM shadow atlas memory");

   bool hasStencil = Vulkan.shadowDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT;
   VkImageAspectFlags depthAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
   if (hasStencil)
   {
      depthAspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
   }

   VkImageViewCreateInfo imageViewInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = Vulkan.shadowAtlasImage,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = Vulkan.shadowDepthFormat,
      .subresourceRange = {
         .aspectMask = depthAspectMask,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };
   VkResult imageViewResult = vkCreateImageView(Vulkan.device, &imageViewInfo, nullptr, &Vulkan.shadowAtlasView);
   Assert(imageViewResult == VK_SUCCESS, "Failed to create CSM shadow atlas view");

   VkSamplerCreateInfo shadowSamplerInfo = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .minLod = 0.0f,
      .maxLod = 0.0f,
      .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
      .unnormalizedCoordinates = VK_FALSE,
   };
   VkResult shadowSamplerResult = vkCreateSampler(Vulkan.device, &shadowSamplerInfo, nullptr, &Vulkan.shadowAtlasSampler);
   Assert(shadowSamplerResult == VK_SUCCESS, "Failed to create CSM shadow sampler");

   VkBufferCreateInfo globalsBufferInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sizeof(ShadowGlobalsGpu),
      .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      VkResult globalsCreateResult = vkCreateBuffer(Vulkan.device, &globalsBufferInfo, nullptr, &Vulkan.shadowGlobalsBuffers[frameIndex]);
      Assert(globalsCreateResult == VK_SUCCESS, "Failed to create CSM globals buffer");

      VkMemoryRequirements globalsRequirements = {};
      vkGetBufferMemoryRequirements(Vulkan.device, Vulkan.shadowGlobalsBuffers[frameIndex], &globalsRequirements);
      VkMemoryAllocateInfo globalsAllocInfo = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = globalsRequirements.size,
         .memoryTypeIndex = FindMemoryType(
            globalsRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
      };
      VkResult globalsAllocResult = vkAllocateMemory(Vulkan.device, &globalsAllocInfo, nullptr, &Vulkan.shadowGlobalsMemories[frameIndex]);
      Assert(globalsAllocResult == VK_SUCCESS, "Failed to allocate CSM globals memory");

      VkResult globalsBindResult = vkBindBufferMemory(Vulkan.device, Vulkan.shadowGlobalsBuffers[frameIndex], Vulkan.shadowGlobalsMemories[frameIndex], 0);
      Assert(globalsBindResult == VK_SUCCESS, "Failed to bind CSM globals buffer memory");

      void *globalsMapped = nullptr;
      VkResult globalsMapResult = vkMapMemory(Vulkan.device, Vulkan.shadowGlobalsMemories[frameIndex], 0, VK_WHOLE_SIZE, 0, &globalsMapped);
      Assert(globalsMapResult == VK_SUCCESS, "Failed to map CSM globals buffer");
      Assert(globalsMapped != nullptr, "CSM globals mapping returned null");
      Vulkan.shadowGlobalsMapped[frameIndex] = globalsMapped;
      std::memset(Vulkan.shadowGlobalsMapped[frameIndex], 0, sizeof(ShadowGlobalsGpu));
   }

   Vulkan.shadowCascadeCount = CsmCascadeCount;
   for (u32 index = 0; index < CsmCascadeCount; ++index)
   {
      const ShadowAtlasRect &rect = CsmAtlasRects[index];
      ShadowCascadeRuntime &runtime = Vulkan.shadowCascadeRuntime[index];
      std::memset(runtime.lightViewProj, 0, sizeof(runtime.lightViewProj));
      runtime.atlasRectPixels.offset = {
         static_cast<int32_t>(rect.x),
         static_cast<int32_t>(rect.y),
      };
      runtime.atlasRectPixels.extent = {
         rect.width,
         rect.height,
      };
   }

   Vulkan.shadowAtlasLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   Vulkan.shadowResourcesReady = true;
}

void DestroyShadowResources()
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.shadowAtlasImage = VK_NULL_HANDLE;
      Vulkan.shadowAtlasMemory = VK_NULL_HANDLE;
      Vulkan.shadowAtlasView = VK_NULL_HANDLE;
      Vulkan.shadowAtlasLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      Vulkan.shadowAtlasSampler = VK_NULL_HANDLE;
      Vulkan.shadowDepthFormat = VK_FORMAT_UNDEFINED;
      Vulkan.shadowGlobalsBuffers.fill(VK_NULL_HANDLE);
      Vulkan.shadowGlobalsMemories.fill(VK_NULL_HANDLE);
      Vulkan.shadowGlobalsMapped.fill(nullptr);
      Vulkan.shadowCascadeCount = 0;
      Vulkan.shadowResourcesReady = false;
      for (ShadowCascadeRuntime &runtime : Vulkan.shadowCascadeRuntime)
      {
         std::memset(runtime.lightViewProj, 0, sizeof(runtime.lightViewProj));
         runtime.atlasRectPixels = {};
      }
      return;
   }

   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      if (Vulkan.shadowGlobalsMapped[frameIndex] != nullptr)
      {
         vkUnmapMemory(Vulkan.device, Vulkan.shadowGlobalsMemories[frameIndex]);
         Vulkan.shadowGlobalsMapped[frameIndex] = nullptr;
      }
      if (Vulkan.shadowGlobalsBuffers[frameIndex] != VK_NULL_HANDLE)
      {
         vkDestroyBuffer(Vulkan.device, Vulkan.shadowGlobalsBuffers[frameIndex], nullptr);
         Vulkan.shadowGlobalsBuffers[frameIndex] = VK_NULL_HANDLE;
      }
      if (Vulkan.shadowGlobalsMemories[frameIndex] != VK_NULL_HANDLE)
      {
         vkFreeMemory(Vulkan.device, Vulkan.shadowGlobalsMemories[frameIndex], nullptr);
         Vulkan.shadowGlobalsMemories[frameIndex] = VK_NULL_HANDLE;
      }
   }

   if (Vulkan.shadowAtlasSampler != VK_NULL_HANDLE)
   {
      vkDestroySampler(Vulkan.device, Vulkan.shadowAtlasSampler, nullptr);
      Vulkan.shadowAtlasSampler = VK_NULL_HANDLE;
   }
   if (Vulkan.shadowAtlasView != VK_NULL_HANDLE)
   {
      vkDestroyImageView(Vulkan.device, Vulkan.shadowAtlasView, nullptr);
      Vulkan.shadowAtlasView = VK_NULL_HANDLE;
   }
   if (Vulkan.shadowAtlasImage != VK_NULL_HANDLE)
   {
      vkDestroyImage(Vulkan.device, Vulkan.shadowAtlasImage, nullptr);
      Vulkan.shadowAtlasImage = VK_NULL_HANDLE;
   }
   if (Vulkan.shadowAtlasMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.shadowAtlasMemory, nullptr);
      Vulkan.shadowAtlasMemory = VK_NULL_HANDLE;
   }

   Vulkan.shadowAtlasLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   Vulkan.shadowDepthFormat = VK_FORMAT_UNDEFINED;
   Vulkan.shadowCascadeCount = 0;
   Vulkan.shadowResourcesReady = false;
   for (ShadowCascadeRuntime &runtime : Vulkan.shadowCascadeRuntime)
   {
      std::memset(runtime.lightViewProj, 0, sizeof(runtime.lightViewProj));
      runtime.atlasRectPixels = {};
   }
}

void CreateShadowPipeline()
{
   if (Vulkan.shadowPipelineReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create Vulkan device before shadow pipeline");
   Assert(Vulkan.shadowResourcesReady, "Create shadow resources before shadow pipeline");
   Assert(ShaderCacheDirectory[0] != '\0', "Shader cache directory is not defined");

   array<char, 512> vertexPath {};
   const auto buildPath = [](const char *directory, const char *fileName, array<char, 512> &buffer)
   {
      int written = std::snprintf(buffer.data(), buffer.size(), "%s/%s", directory, fileName);
      Assert((written > 0) && (static_cast<size_t>(written) < buffer.size()), "Shader path truncated");
   };
   buildPath(ShaderCacheDirectory, ShadowVertexShaderName, vertexPath);
   Vulkan.shadowVertexShader = CreateShader(vertexPath.data());

   VkPushConstantRange pushConstant = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .offset = 0,
      .size = sizeof(ShadowPushConstants),
   };
   VkPipelineLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pSetLayouts = nullptr,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstant,
   };
   VkResult layoutResult = vkCreatePipelineLayout(Vulkan.device, &layoutInfo, nullptr, &Vulkan.shadowPipelineLayout);
   Assert(layoutResult == VK_SUCCESS, "Failed to create shadow pipeline layout");

   VkPipelineShaderStageCreateInfo shaderStage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = Vulkan.shadowVertexShader,
      .pName = "main",
   };

   std::array<VkVertexInputBindingDescription, 2> vertexBindings = {
      VkVertexInputBindingDescription{
         .binding = 0,
         .stride = static_cast<u32>(sizeof(Vertex)),
         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
      VkVertexInputBindingDescription{
         .binding = 1,
         .stride = static_cast<u32>(sizeof(InstanceData)),
         .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
      },
   };
   std::array<VkVertexInputAttributeDescription, 2> vertexAttributes = {
      VkVertexInputAttributeDescription{
         .location = 0,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32_SFLOAT,
         .offset = static_cast<u32>(offsetof(Vertex, position)),
      },
      VkVertexInputAttributeDescription{
         .location = 1,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = static_cast<u32>(offsetof(InstanceData, translation)),
      },
   };
   VkPipelineVertexInputStateCreateInfo vertexInput = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = static_cast<u32>(vertexBindings.size()),
      .pVertexBindingDescriptions = vertexBindings.data(),
      .vertexAttributeDescriptionCount = static_cast<u32>(vertexAttributes.size()),
      .pVertexAttributeDescriptions = vertexAttributes.data(),
   };

   VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
   };

   VkPipelineViewportStateCreateInfo viewportState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
   };

   VkPipelineRasterizationStateCreateInfo rasterizer = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable = VK_TRUE,
      .lineWidth = 1.0f,
   };

   VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
   };

   VkPipelineDepthStencilStateCreateInfo depthStencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = VK_FALSE,
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 1.0f,
   };

   VkPipelineColorBlendStateCreateInfo colorBlending = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = VK_FALSE,
      .attachmentCount = 0,
      .pAttachments = nullptr,
   };

   VkDynamicState dynamicStates[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_DEPTH_BIAS,
   };
   VkPipelineDynamicStateCreateInfo dynamicState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<u32>(sizeof(dynamicStates) / sizeof(dynamicStates[0])),
      .pDynamicStates = dynamicStates,
   };

   VkPipelineRenderingCreateInfo renderingInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 0,
      .pColorAttachmentFormats = nullptr,
      .depthAttachmentFormat = Vulkan.shadowDepthFormat,
      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
   };

   VkGraphicsPipelineCreateInfo pipelineInfo = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &renderingInfo,
      .stageCount = 1,
      .pStages = &shaderStage,
      .pVertexInputState = &vertexInput,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewportState,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pDepthStencilState = &depthStencil,
      .pColorBlendState = &colorBlending,
      .pDynamicState = &dynamicState,
      .layout = Vulkan.shadowPipelineLayout,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
   };

   VkResult pipelineResult = vkCreateGraphicsPipelines(
      Vulkan.device,
      VK_NULL_HANDLE,
      1,
      &pipelineInfo,
      nullptr,
      &Vulkan.shadowPipeline);
   Assert(pipelineResult == VK_SUCCESS, "Failed to create shadow pipeline");

   Vulkan.shadowPipelineReady = true;
}

void DestroyShadowPipeline()
{
   if ((Vulkan.shadowPipeline == VK_NULL_HANDLE) &&
       (Vulkan.shadowPipelineLayout == VK_NULL_HANDLE) &&
       (Vulkan.shadowVertexShader == VK_NULL_HANDLE))
   {
      Vulkan.shadowPipelineReady = false;
      return;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.shadowPipeline != VK_NULL_HANDLE))
   {
      vkDestroyPipeline(Vulkan.device, Vulkan.shadowPipeline, nullptr);
      Vulkan.shadowPipeline = VK_NULL_HANDLE;
   }
   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.shadowPipelineLayout != VK_NULL_HANDLE))
   {
      vkDestroyPipelineLayout(Vulkan.device, Vulkan.shadowPipelineLayout, nullptr);
      Vulkan.shadowPipelineLayout = VK_NULL_HANDLE;
   }
   DestroyShader(Vulkan.shadowVertexShader);

   Vulkan.shadowPipelineReady = false;
}

void UpdateShadowCascades(const CameraParams &camera, VkExtent2D extent, u32 frameIndex)
{
   Assert(Vulkan.shadowResourcesReady, "Shadow resources are not ready");
   Assert(frameIndex < FrameOverlap, "Shadow update frame index is out of range");
   Assert(Vulkan.shadowGlobalsMapped[frameIndex] != nullptr, "Shadow globals buffer is not mapped");
   Assert((extent.width > 0) && (extent.height > 0), "Shadow update requires non-zero extent");

   const auto dot3 = [](const Vec3 &a, const Vec3 &b) -> float
   {
      return a.x*b.x + a.y*b.y + a.z*b.z;
   };
   const auto cross3 = [](const Vec3 &a, const Vec3 &b) -> Vec3
   {
      return Vec3{
         a.y*b.z - a.z*b.y,
         a.z*b.x - a.x*b.z,
         a.x*b.y - a.y*b.x,
      };
   };
   const auto normalize3 = [](const Vec3 &v) -> Vec3
   {
      float length = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
      if (length <= 0.00001f)
      {
         return Vec3{0.0f, 1.0f, 0.0f};
      }
      float inv = 1.0f / length;
      return Vec3{v.x * inv, v.y * inv, v.z * inv};
   };
   const auto setIdentity = [](float *matrix)
   {
      std::memset(matrix, 0, sizeof(float) * 16);
      matrix[0] = 1.0f;
      matrix[5] = 1.0f;
      matrix[10] = 1.0f;
      matrix[15] = 1.0f;
   };
   const auto multiplyMat4 = [](const float *a, const float *b, float *result)
   {
      float temp[16] = {};
      for (int column = 0; column < 4; ++column)
      {
         for (int row = 0; row < 4; ++row)
         {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
               sum += a[k*4 + row] * b[column*4 + k];
            }
            temp[column*4 + row] = sum;
         }
      }
      std::memcpy(result, temp, sizeof(temp));
   };
   const auto transformPoint = [](const float *matrix, const Vec3 &point) -> Vec3
   {
      return Vec3{
         matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
         matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
         matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14],
      };
   };
   const auto buildView = [&](const Vec3 &origin, const Vec3 &xAxis, const Vec3 &yAxis, const Vec3 &zAxis, float *outMatrix)
   {
      outMatrix[0] = xAxis.x;
      outMatrix[1] = xAxis.y;
      outMatrix[2] = xAxis.z;
      outMatrix[3] = 0.0f;
      outMatrix[4] = yAxis.x;
      outMatrix[5] = yAxis.y;
      outMatrix[6] = yAxis.z;
      outMatrix[7] = 0.0f;
      outMatrix[8] = zAxis.x;
      outMatrix[9] = zAxis.y;
      outMatrix[10] = zAxis.z;
      outMatrix[11] = 0.0f;
      outMatrix[12] = -dot3(xAxis, origin);
      outMatrix[13] = -dot3(yAxis, origin);
      outMatrix[14] = -dot3(zAxis, origin);
      outMatrix[15] = 1.0f;
   };
   const auto buildOrtho = [](float left, float right, float bottom, float top, float nearZ, float farZ, float *outMatrix)
   {
      std::memset(outMatrix, 0, sizeof(float) * 16);
      float invWidth = 1.0f / (right - left);
      float invHeight = 1.0f / (top - bottom);
      float invDepth = 1.0f / (farZ - nearZ);
      outMatrix[0] = 2.0f * invWidth;
      outMatrix[5] = 2.0f * invHeight;
      outMatrix[10] = invDepth;
      outMatrix[12] = -(right + left) * invWidth;
      outMatrix[13] = -(top + bottom) * invHeight;
      outMatrix[14] = -nearZ * invDepth;
      outMatrix[15] = 1.0f;
   };

   float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
   if (aspect <= 0.0f)
   {
      aspect = 1.0f;
   }
   float tanHalfFov = std::tan(camera.verticalFovRadians * 0.5f);
   if (tanHalfFov <= 0.0f)
   {
      tanHalfFov = 0.001f;
   }

   float splitEnds[CsmCascadeCount] = {};
   float clipRange = CsmFarPlane - CsmNearPlane;
   for (u32 cascadeIndex = 0; cascadeIndex < CsmCascadeCount; ++cascadeIndex)
   {
      float splitRatio = static_cast<float>(cascadeIndex + 1) / static_cast<float>(CsmCascadeCount);
      float logSplit = CsmNearPlane * std::pow(CsmFarPlane / CsmNearPlane, splitRatio);
      float uniformSplit = CsmNearPlane + clipRange * splitRatio;
      splitEnds[cascadeIndex] = uniformSplit + (logSplit - uniformSplit) * CsmSplitLambda;
   }

   ShadowGlobalsGpu globals = {};
   globals.cameraForward[0] = camera.forward.x;
   globals.cameraForward[1] = camera.forward.y;
   globals.cameraForward[2] = camera.forward.z;
   globals.cameraForward[3] = 0.0f;
   globals.atlasTexelSize[0] = 1.0f / static_cast<float>(CsmShadowAtlasSize);
   globals.atlasTexelSize[1] = 1.0f / static_cast<float>(CsmShadowAtlasSize);
   globals.atlasTexelSize[2] = static_cast<float>(CsmCascadeCount);
   globals.atlasTexelSize[3] = CsmOverlapRatio;

   Vec3 lightForward = normalize3(Vec3{-SunDirection.x, -SunDirection.y, -SunDirection.z});
   Vec3 upHint = (std::fabs(lightForward.y) > 0.95f) ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
   Vec3 lightRight = normalize3(cross3(upHint, lightForward));
   Vec3 lightUp = normalize3(cross3(lightForward, lightRight));

   for (u32 cascadeIndex = 0; cascadeIndex < CsmCascadeCount; ++cascadeIndex)
   {
      float baseNear = (cascadeIndex == 0) ? CsmNearPlane : splitEnds[cascadeIndex - 1];
      float baseFar = splitEnds[cascadeIndex];
      float baseRange = std::max(baseFar - baseNear, 0.001f);

      float overlapDistance = baseRange * CsmOverlapRatio;
      float sliceNear = (cascadeIndex == 0) ? baseNear : std::max(CsmNearPlane, baseNear - overlapDistance);
      float sliceFar = (cascadeIndex == (CsmCascadeCount - 1)) ? baseFar : std::min(CsmFarPlane, baseFar + overlapDistance);

      float nearHalfHeight = sliceNear * tanHalfFov;
      float nearHalfWidth = nearHalfHeight * aspect;
      float farHalfHeight = sliceFar * tanHalfFov;
      float farHalfWidth = farHalfHeight * aspect;

      Vec3 nearCenter = {
         camera.position.x + camera.forward.x * sliceNear,
         camera.position.y + camera.forward.y * sliceNear,
         camera.position.z + camera.forward.z * sliceNear,
      };
      Vec3 farCenter = {
         camera.position.x + camera.forward.x * sliceFar,
         camera.position.y + camera.forward.y * sliceFar,
         camera.position.z + camera.forward.z * sliceFar,
      };

      array<Vec3, 8> corners = {
         Vec3{nearCenter.x - camera.right.x * nearHalfWidth + camera.up.x * nearHalfHeight,
              nearCenter.y - camera.right.y * nearHalfWidth + camera.up.y * nearHalfHeight,
              nearCenter.z - camera.right.z * nearHalfWidth + camera.up.z * nearHalfHeight},
         Vec3{nearCenter.x + camera.right.x * nearHalfWidth + camera.up.x * nearHalfHeight,
              nearCenter.y + camera.right.y * nearHalfWidth + camera.up.y * nearHalfHeight,
              nearCenter.z + camera.right.z * nearHalfWidth + camera.up.z * nearHalfHeight},
         Vec3{nearCenter.x + camera.right.x * nearHalfWidth - camera.up.x * nearHalfHeight,
              nearCenter.y + camera.right.y * nearHalfWidth - camera.up.y * nearHalfHeight,
              nearCenter.z + camera.right.z * nearHalfWidth - camera.up.z * nearHalfHeight},
         Vec3{nearCenter.x - camera.right.x * nearHalfWidth - camera.up.x * nearHalfHeight,
              nearCenter.y - camera.right.y * nearHalfWidth - camera.up.y * nearHalfHeight,
              nearCenter.z - camera.right.z * nearHalfWidth - camera.up.z * nearHalfHeight},
         Vec3{farCenter.x - camera.right.x * farHalfWidth + camera.up.x * farHalfHeight,
              farCenter.y - camera.right.y * farHalfWidth + camera.up.y * farHalfHeight,
              farCenter.z - camera.right.z * farHalfWidth + camera.up.z * farHalfHeight},
         Vec3{farCenter.x + camera.right.x * farHalfWidth + camera.up.x * farHalfHeight,
              farCenter.y + camera.right.y * farHalfWidth + camera.up.y * farHalfHeight,
              farCenter.z + camera.right.z * farHalfWidth + camera.up.z * farHalfHeight},
         Vec3{farCenter.x + camera.right.x * farHalfWidth - camera.up.x * farHalfHeight,
              farCenter.y + camera.right.y * farHalfWidth - camera.up.y * farHalfHeight,
              farCenter.z + camera.right.z * farHalfWidth - camera.up.z * farHalfHeight},
         Vec3{farCenter.x - camera.right.x * farHalfWidth - camera.up.x * farHalfHeight,
              farCenter.y - camera.right.y * farHalfWidth - camera.up.y * farHalfHeight,
              farCenter.z - camera.right.z * farHalfWidth - camera.up.z * farHalfHeight},
      };

      Vec3 cascadeCenter = {0.0f, 0.0f, 0.0f};
      for (const Vec3 &corner : corners)
      {
         cascadeCenter.x += corner.x;
         cascadeCenter.y += corner.y;
         cascadeCenter.z += corner.z;
      }
      cascadeCenter.x /= static_cast<float>(corners.size());
      cascadeCenter.y /= static_cast<float>(corners.size());
      cascadeCenter.z /= static_cast<float>(corners.size());

      float radius = 0.0f;
      for (const Vec3 &corner : corners)
      {
         float dx = corner.x - cascadeCenter.x;
         float dy = corner.y - cascadeCenter.y;
         float dz = corner.z - cascadeCenter.z;
         radius = std::max(radius, std::sqrt(dx*dx + dy*dy + dz*dz));
      }
      radius = std::max(radius, 0.5f);
      radius = std::ceil(radius * 16.0f) / 16.0f;

      const ShadowAtlasRect &atlasRect = CsmAtlasRects[cascadeIndex];
      float unitsPerTexel = (2.0f * radius) / static_cast<float>(atlasRect.width);
      float centerX = dot3(lightRight, cascadeCenter);
      float centerY = dot3(lightUp, cascadeCenter);
      float snappedX = std::floor(centerX / unitsPerTexel) * unitsPerTexel;
      float snappedY = std::floor(centerY / unitsPerTexel) * unitsPerTexel;
      Vec3 snappedCenter = {
         cascadeCenter.x + lightRight.x * (snappedX - centerX) + lightUp.x * (snappedY - centerY),
         cascadeCenter.y + lightRight.y * (snappedX - centerX) + lightUp.y * (snappedY - centerY),
         cascadeCenter.z + lightRight.z * (snappedX - centerX) + lightUp.z * (snappedY - centerY),
      };

      float lightDistance = radius * 2.0f + 48.0f;
      Vec3 lightOrigin = {
         snappedCenter.x - lightForward.x * lightDistance,
         snappedCenter.y - lightForward.y * lightDistance,
         snappedCenter.z - lightForward.z * lightDistance,
      };

      float lightView[16] = {};
      buildView(lightOrigin, lightRight, lightUp, lightForward, lightView);

      float minDepth = std::numeric_limits<float>::max();
      float maxDepth = -std::numeric_limits<float>::max();
      for (const Vec3 &corner : corners)
      {
         Vec3 lightSpace = transformPoint(lightView, corner);
         minDepth = std::min(minDepth, lightSpace.z);
         maxDepth = std::max(maxDepth, lightSpace.z);
      }
      minDepth -= 24.0f;
      maxDepth += 24.0f;
      if (maxDepth <= minDepth + 0.01f)
      {
         maxDepth = minDepth + 0.01f;
      }

      float lightOrtho[16] = {};
      buildOrtho(
         -radius,
         radius,
         -radius,
         radius,
         minDepth,
         maxDepth,
         lightOrtho);

      float lightViewProj[16] = {};
      multiplyMat4(lightOrtho, lightView, lightViewProj);

      float clipToUv[16] = {};
      setIdentity(clipToUv);
      clipToUv[0] = 0.5f;
      clipToUv[5] = 0.5f;
      clipToUv[12] = 0.5f;
      clipToUv[13] = 0.5f;

      float worldToShadow[16] = {};
      multiplyMat4(clipToUv, lightViewProj, worldToShadow);

      ShadowCascadeRuntime &runtime = Vulkan.shadowCascadeRuntime[cascadeIndex];
      std::memcpy(runtime.lightViewProj, lightViewProj, sizeof(lightViewProj));
      runtime.atlasRectPixels.offset = {
         static_cast<int32_t>(atlasRect.x),
         static_cast<int32_t>(atlasRect.y),
      };
      runtime.atlasRectPixels.extent = {
         atlasRect.width,
         atlasRect.height,
      };

      ShadowCascadeGpu &gpuCascade = globals.cascades[cascadeIndex];
      std::memcpy(gpuCascade.worldToShadow, worldToShadow, sizeof(worldToShadow));
      gpuCascade.atlasRect[0] = static_cast<float>(atlasRect.x) / static_cast<float>(CsmShadowAtlasSize);
      gpuCascade.atlasRect[1] = static_cast<float>(atlasRect.y) / static_cast<float>(CsmShadowAtlasSize);
      gpuCascade.atlasRect[2] = static_cast<float>(atlasRect.width) / static_cast<float>(CsmShadowAtlasSize);
      gpuCascade.atlasRect[3] = static_cast<float>(atlasRect.height) / static_cast<float>(CsmShadowAtlasSize);
      gpuCascade.params[0] = baseFar;
      gpuCascade.params[1] = std::max(baseNear, baseFar - overlapDistance);
      gpuCascade.params[2] = 0.0008f + static_cast<float>(cascadeIndex) * 0.00035f;
      gpuCascade.params[3] = 0.006f + static_cast<float>(cascadeIndex) * 0.003f;
   }

   std::memcpy(Vulkan.shadowGlobalsMapped[frameIndex], &globals, sizeof(globals));
}

void RecordShadowPass(VkCommandBuffer commandBuffer)
{
   Assert(commandBuffer != VK_NULL_HANDLE, "Shadow pass requires a valid command buffer");
   Assert(Vulkan.shadowResourcesReady, "Shadow resources must be ready before recording shadows");
   Assert(Vulkan.shadowPipelineReady, "Shadow pipeline must be ready before recording shadows");
   Assert(Vulkan.sceneReady, "Scene must be ready before recording shadows");
   Assert(Vulkan.sceneVertexBuffer != VK_NULL_HANDLE, "Scene vertex buffer is not initialized");
   Assert(Vulkan.sceneIndexBuffer != VK_NULL_HANDLE, "Scene index buffer is not initialized");
   Assert(Vulkan.sceneInstanceBuffer != VK_NULL_HANDLE, "Scene instance buffer is not initialized");
   Assert(Vulkan.sceneCarIndexCount > 0, "Scene car index count is zero");
   Assert(Vulkan.sceneGroundIndexCount > 0, "Scene ground index count is zero");
   Assert(Vulkan.sceneCarInstanceCount > 0, "Scene car instance count is zero");
   Assert(Vulkan.sceneInstanceCount > Vulkan.sceneGroundInstanceIndex, "Scene ground instance index is out of range");

   bool hasStencil = Vulkan.shadowDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT;
   VkImageAspectFlags depthAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
   if (hasStencil)
   {
      depthAspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
   }

   VkImageSubresourceRange depthSubresource = {
      .aspectMask = depthAspectMask,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
   };

   VkImageMemoryBarrier toDepthAttachment = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = (Vulkan.shadowAtlasLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0u : VK_ACCESS_SHADER_READ_BIT,
      .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .oldLayout = Vulkan.shadowAtlasLayout,
      .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = Vulkan.shadowAtlasImage,
      .subresourceRange = depthSubresource,
   };
   vkCmdPipelineBarrier(
      commandBuffer,
      (Vulkan.shadowAtlasLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &toDepthAttachment);

   vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan.shadowPipeline);
   std::array<VkBuffer, 2> vertexBuffers = {Vulkan.sceneVertexBuffer, Vulkan.sceneInstanceBuffer};
   std::array<VkDeviceSize, 2> vertexOffsets = {0, 0};
   vkCmdBindVertexBuffers(commandBuffer, 0, static_cast<u32>(vertexBuffers.size()), vertexBuffers.data(), vertexOffsets.data());
   vkCmdBindIndexBuffer(commandBuffer, Vulkan.sceneIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

   for (u32 cascadeIndex = 0; cascadeIndex < Vulkan.shadowCascadeCount; ++cascadeIndex)
   {
      const ShadowCascadeRuntime &runtime = Vulkan.shadowCascadeRuntime[cascadeIndex];

      VkClearDepthStencilValue clearDepth = {
         .depth = 1.0f,
         .stencil = 0u,
      };
      VkRenderingAttachmentInfo depthAttachment = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = Vulkan.shadowAtlasView,
         .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue = {.depthStencil = clearDepth},
      };

      VkRenderingInfo renderingInfo = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
         .renderArea = runtime.atlasRectPixels,
         .layerCount = 1,
         .colorAttachmentCount = 0,
         .pColorAttachments = nullptr,
         .pDepthAttachment = &depthAttachment,
      };
      vkCmdBeginRendering(commandBuffer, &renderingInfo);

      VkViewport viewport = {
         .x = static_cast<float>(runtime.atlasRectPixels.offset.x),
         .y = static_cast<float>(runtime.atlasRectPixels.offset.y),
         .width = static_cast<float>(runtime.atlasRectPixels.extent.width),
         .height = static_cast<float>(runtime.atlasRectPixels.extent.height),
         .minDepth = 0.0f,
         .maxDepth = 1.0f,
      };
      vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
      vkCmdSetScissor(commandBuffer, 0, 1, &runtime.atlasRectPixels);

      float constantBias = 1.15f + static_cast<float>(cascadeIndex) * 0.35f;
      float slopeBias = 1.75f + static_cast<float>(cascadeIndex) * 0.55f;
      vkCmdSetDepthBias(commandBuffer, constantBias, 0.0f, slopeBias);

      ShadowPushConstants push = {};
      std::memcpy(push.mvp, runtime.lightViewProj, sizeof(push.mvp));
      vkCmdPushConstants(
         commandBuffer,
         Vulkan.shadowPipelineLayout,
         VK_SHADER_STAGE_VERTEX_BIT,
         0,
         sizeof(ShadowPushConstants),
         &push);

      vkCmdDrawIndexed(commandBuffer, Vulkan.sceneCarIndexCount, Vulkan.sceneCarInstanceCount, 0, 0, 0);
      vkCmdDrawIndexed(commandBuffer, Vulkan.sceneGroundIndexCount, 1, Vulkan.sceneGroundFirstIndex, 0, Vulkan.sceneGroundInstanceIndex);
      vkCmdEndRendering(commandBuffer);
   }

   VkImageMemoryBarrier toReadOnly = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = Vulkan.shadowAtlasImage,
      .subresourceRange = depthSubresource,
   };
   vkCmdPipelineBarrier(
      commandBuffer,
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &toReadOnly);

   Vulkan.shadowAtlasLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}

void CreateForwardLightingResources()
{
   if (Vulkan.forwardLightingReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create Vulkan device before forward lighting resources");
   Assert(Vulkan.swapchainReady, "Create Vulkan swapchain before forward lighting resources");

   VkExtent2D extent = Vulkan.swapchainExtent;
   Assert((extent.width > 0) && (extent.height > 0), "Swapchain extent is invalid for forward lighting resources");

   Vulkan.forwardTileCountX = (extent.width + ForwardTileSizePixels - 1) / ForwardTileSizePixels;
   Vulkan.forwardTileCountY = (extent.height + ForwardTileSizePixels - 1) / ForwardTileSizePixels;
   Assert(Vulkan.forwardTileCountX > 0, "Forward lighting tile count X is zero");
   Assert(Vulkan.forwardTileCountY > 0, "Forward lighting tile count Y is zero");

   u32 tileCount = Vulkan.forwardTileCountX * Vulkan.forwardTileCountY;
   Assert(tileCount > 0, "Forward lighting tile count is zero");

   VkDeviceSize lightBytes = static_cast<VkDeviceSize>(sizeof(ForwardGpuLight)) * static_cast<VkDeviceSize>(ForwardMaxLights);
   VkDeviceSize tileMetaBytes = static_cast<VkDeviceSize>(sizeof(ForwardTileMeta)) * static_cast<VkDeviceSize>(tileCount);
   VkDeviceSize tileIndexBytes = static_cast<VkDeviceSize>(sizeof(u32)) * static_cast<VkDeviceSize>(tileCount) * static_cast<VkDeviceSize>(ForwardMaxLightsPerTile);

   const auto createHostVisibleStorageBuffer = [&](VkDeviceSize size, VkBuffer &buffer, VkDeviceMemory &memory, void *&mapped)
   {
      VkBufferCreateInfo bufferInfo = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
         .size = size,
         .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      };
      VkResult createResult = vkCreateBuffer(Vulkan.device, &bufferInfo, nullptr, &buffer);
      Assert(createResult == VK_SUCCESS, "Failed to create forward lighting storage buffer");

      VkMemoryRequirements requirements = {};
      vkGetBufferMemoryRequirements(Vulkan.device, buffer, &requirements);
      VkMemoryAllocateInfo allocInfo = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = requirements.size,
         .memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
      };
      VkResult allocResult = vkAllocateMemory(Vulkan.device, &allocInfo, nullptr, &memory);
      Assert(allocResult == VK_SUCCESS, "Failed to allocate forward lighting storage memory");

      VkResult bindResult = vkBindBufferMemory(Vulkan.device, buffer, memory, 0);
      Assert(bindResult == VK_SUCCESS, "Failed to bind forward lighting storage memory");

      mapped = nullptr;
      VkResult mapResult = vkMapMemory(Vulkan.device, memory, 0, size, 0, &mapped);
      Assert(mapResult == VK_SUCCESS, "Failed to map forward lighting storage memory");
      Assert(mapped != nullptr, "Forward lighting storage mapping returned null");
   };

   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      createHostVisibleStorageBuffer(
         lightBytes,
         Vulkan.forwardLightBuffers[frameIndex],
         Vulkan.forwardLightMemories[frameIndex],
         Vulkan.forwardLightMapped[frameIndex]);
      createHostVisibleStorageBuffer(
         tileMetaBytes,
         Vulkan.forwardTileMetaBuffers[frameIndex],
         Vulkan.forwardTileMetaMemories[frameIndex],
         Vulkan.forwardTileMetaMapped[frameIndex]);
      createHostVisibleStorageBuffer(
         tileIndexBytes,
         Vulkan.forwardTileIndexBuffers[frameIndex],
         Vulkan.forwardTileIndexMemories[frameIndex],
         Vulkan.forwardTileIndexMapped[frameIndex]);
   }

   Vulkan.forwardLightCount = 0;
   Vulkan.forwardTileMetaScratch.resize(tileCount);
   Vulkan.forwardTileIndexScratch.resize(static_cast<usize>(tileCount) * static_cast<usize>(ForwardMaxLightsPerTile));
   Vulkan.forwardLightScratch.resize(ForwardMaxLights);
   Vulkan.forwardLightingReady = true;
}

void DestroyForwardLightingResources()
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.forwardLightBuffers.fill(VK_NULL_HANDLE);
      Vulkan.forwardLightMemories.fill(VK_NULL_HANDLE);
      Vulkan.forwardLightMapped.fill(nullptr);
      Vulkan.forwardTileMetaBuffers.fill(VK_NULL_HANDLE);
      Vulkan.forwardTileMetaMemories.fill(VK_NULL_HANDLE);
      Vulkan.forwardTileMetaMapped.fill(nullptr);
      Vulkan.forwardTileIndexBuffers.fill(VK_NULL_HANDLE);
      Vulkan.forwardTileIndexMemories.fill(VK_NULL_HANDLE);
      Vulkan.forwardTileIndexMapped.fill(nullptr);
      Vulkan.forwardTileCountX = 0;
      Vulkan.forwardTileCountY = 0;
      Vulkan.forwardLightCount = 0;
      Vulkan.forwardTileMetaScratch.clear();
      Vulkan.forwardTileIndexScratch.clear();
      Vulkan.forwardLightScratch.clear();
      Vulkan.forwardLightingReady = false;
      return;
   }

   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      if (Vulkan.forwardLightMapped[frameIndex] != nullptr)
      {
         vkUnmapMemory(Vulkan.device, Vulkan.forwardLightMemories[frameIndex]);
         Vulkan.forwardLightMapped[frameIndex] = nullptr;
      }
      if (Vulkan.forwardTileMetaMapped[frameIndex] != nullptr)
      {
         vkUnmapMemory(Vulkan.device, Vulkan.forwardTileMetaMemories[frameIndex]);
         Vulkan.forwardTileMetaMapped[frameIndex] = nullptr;
      }
      if (Vulkan.forwardTileIndexMapped[frameIndex] != nullptr)
      {
         vkUnmapMemory(Vulkan.device, Vulkan.forwardTileIndexMemories[frameIndex]);
         Vulkan.forwardTileIndexMapped[frameIndex] = nullptr;
      }

      if (Vulkan.forwardLightBuffers[frameIndex] != VK_NULL_HANDLE)
      {
         vkDestroyBuffer(Vulkan.device, Vulkan.forwardLightBuffers[frameIndex], nullptr);
         Vulkan.forwardLightBuffers[frameIndex] = VK_NULL_HANDLE;
      }
      if (Vulkan.forwardLightMemories[frameIndex] != VK_NULL_HANDLE)
      {
         vkFreeMemory(Vulkan.device, Vulkan.forwardLightMemories[frameIndex], nullptr);
         Vulkan.forwardLightMemories[frameIndex] = VK_NULL_HANDLE;
      }

      if (Vulkan.forwardTileMetaBuffers[frameIndex] != VK_NULL_HANDLE)
      {
         vkDestroyBuffer(Vulkan.device, Vulkan.forwardTileMetaBuffers[frameIndex], nullptr);
         Vulkan.forwardTileMetaBuffers[frameIndex] = VK_NULL_HANDLE;
      }
      if (Vulkan.forwardTileMetaMemories[frameIndex] != VK_NULL_HANDLE)
      {
         vkFreeMemory(Vulkan.device, Vulkan.forwardTileMetaMemories[frameIndex], nullptr);
         Vulkan.forwardTileMetaMemories[frameIndex] = VK_NULL_HANDLE;
      }

      if (Vulkan.forwardTileIndexBuffers[frameIndex] != VK_NULL_HANDLE)
      {
         vkDestroyBuffer(Vulkan.device, Vulkan.forwardTileIndexBuffers[frameIndex], nullptr);
         Vulkan.forwardTileIndexBuffers[frameIndex] = VK_NULL_HANDLE;
      }
      if (Vulkan.forwardTileIndexMemories[frameIndex] != VK_NULL_HANDLE)
      {
         vkFreeMemory(Vulkan.device, Vulkan.forwardTileIndexMemories[frameIndex], nullptr);
         Vulkan.forwardTileIndexMemories[frameIndex] = VK_NULL_HANDLE;
      }
   }

   Vulkan.forwardTileCountX = 0;
   Vulkan.forwardTileCountY = 0;
   Vulkan.forwardLightCount = 0;
   Vulkan.forwardTileMetaScratch.clear();
   Vulkan.forwardTileIndexScratch.clear();
   Vulkan.forwardLightScratch.clear();
   Vulkan.forwardLightingReady = false;
}

void UpdateForwardLightingData(const CameraParams &camera, VkExtent2D extent, float timeSeconds, u32 frameIndex)
{
   Assert(frameIndex < FrameOverlap, "Forward lighting frame index is out of range");
   Assert(Vulkan.forwardLightingReady, "Forward lighting resources are not ready");
   Assert(Vulkan.forwardLightMapped[frameIndex] != nullptr, "Forward light buffer is not mapped");
   Assert(Vulkan.forwardTileMetaMapped[frameIndex] != nullptr, "Forward tile metadata buffer is not mapped");
   Assert(Vulkan.forwardTileIndexMapped[frameIndex] != nullptr, "Forward tile index buffer is not mapped");
   Assert((extent.width > 0) && (extent.height > 0), "Forward lighting update requires non-zero extent");
   Assert(Vulkan.forwardTileCountX > 0, "Forward tile count X is zero");
   Assert(Vulkan.forwardTileCountY > 0, "Forward tile count Y is zero");

   u32 tileCount = Vulkan.forwardTileCountX * Vulkan.forwardTileCountY;
   Assert(tileCount > 0, "Forward tile count is zero");

   if (Vulkan.forwardTileMetaScratch.size() != static_cast<usize>(tileCount))
   {
      Vulkan.forwardTileMetaScratch.resize(tileCount);
   }
   usize tileIndexCount = static_cast<usize>(tileCount) * static_cast<usize>(ForwardMaxLightsPerTile);
   if (Vulkan.forwardTileIndexScratch.size() != tileIndexCount)
   {
      Vulkan.forwardTileIndexScratch.resize(tileIndexCount);
   }
   if (Vulkan.forwardLightScratch.size() != static_cast<usize>(ForwardMaxLights))
   {
      Vulkan.forwardLightScratch.resize(ForwardMaxLights);
   }

   u32 generatedLights = 0;
   for (i32 z = -4; z <= 4 && generatedLights < ForwardMaxLights; ++z)
   {
      for (i32 x = -4; x <= 4 && generatedLights < ForwardMaxLights; ++x)
      {
         float phase = static_cast<float>(generatedLights) * 0.37f;
         float pulse = 0.5f + 0.5f * std::sin((timeSeconds * 0.85f) + phase);
         float lightX = static_cast<float>(x) * 3.25f;
         float lightZ = static_cast<float>(z) * 3.25f;
         float lightY = 1.2f + (0.9f * pulse);
         float radius = 3.0f + (1.4f * pulse);
         float intensity = 2.2f + (0.8f * pulse);
         float colorR = 0.35f + (0.65f * (0.5f + 0.5f * std::sin(phase * 1.31f + 0.4f)));
         float colorG = 0.35f + (0.65f * (0.5f + 0.5f * std::sin(phase * 1.79f + 1.1f)));
         float colorB = 0.35f + (0.65f * (0.5f + 0.5f * std::sin(phase * 2.17f + 2.2f)));

         ForwardGpuLight light = {};
         light.positionRadius[0] = lightX;
         light.positionRadius[1] = lightY;
         light.positionRadius[2] = lightZ;
         light.positionRadius[3] = radius;
         light.colorIntensity[0] = colorR;
         light.colorIntensity[1] = colorG;
         light.colorIntensity[2] = colorB;
         light.colorIntensity[3] = intensity;
         Vulkan.forwardLightScratch[generatedLights] = light;
         generatedLights += 1;
      }
   }

   const auto dot3 = [](const Vec3 &a, const Vec3 &b) -> float
   {
      return a.x*b.x + a.y*b.y + a.z*b.z;
   };
   const auto multiplyMat4 = [](const float *a, const float *b, float *result)
   {
      float temp[16] = {};
      for (int column = 0; column < 4; ++column)
      {
         for (int row = 0; row < 4; ++row)
         {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
               sum += a[k*4 + row] * b[column*4 + k];
            }
            temp[column*4 + row] = sum;
         }
      }
      std::memcpy(result, temp, sizeof(temp));
   };

   float nearPlane = 0.05f;
   float farPlane = 200.0f;
   float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
   if (aspect <= 0.0f)
   {
      aspect = 1.0f;
   }

   float view[16] = {};
   view[0] = camera.right.x;
   view[1] = camera.up.x;
   view[2] = -camera.forward.x;
   view[3] = 0.0f;
   view[4] = camera.right.y;
   view[5] = camera.up.y;
   view[6] = -camera.forward.y;
   view[7] = 0.0f;
   view[8] = camera.right.z;
   view[9] = camera.up.z;
   view[10] = -camera.forward.z;
   view[11] = 0.0f;
   view[12] = -dot3(camera.right, camera.position);
   view[13] = -dot3(camera.up, camera.position);
   view[14] = dot3(camera.forward, camera.position);
   view[15] = 1.0f;

   float proj[16] = {};
   float tanHalfFov = std::tan(camera.verticalFovRadians * 0.5f);
   if (tanHalfFov <= 0.0f)
   {
      tanHalfFov = 0.001f;
   }
   float focal = 1.0f / tanHalfFov;
   proj[0] = focal / aspect;
   proj[5] = -focal;
   proj[10] = farPlane / (nearPlane - farPlane);
   proj[11] = -1.0f;
   proj[14] = (nearPlane * farPlane) / (nearPlane - farPlane);

   float viewProj[16] = {};
   multiplyMat4(proj, view, viewProj);

   auto projectToScreen = [&](const Vec3 &world, float &x, float &y) -> bool
   {
      float clipX = (viewProj[0] * world.x) + (viewProj[4] * world.y) + (viewProj[8] * world.z) + viewProj[12];
      float clipY = (viewProj[1] * world.x) + (viewProj[5] * world.y) + (viewProj[9] * world.z) + viewProj[13];
      float clipW = (viewProj[3] * world.x) + (viewProj[7] * world.y) + (viewProj[11] * world.z) + viewProj[15];
      if (clipW <= 0.0001f)
      {
         return false;
      }
      float invW = 1.0f / clipW;
      float ndcX = clipX * invW;
      float ndcY = clipY * invW;
      x = (ndcX * 0.5f + 0.5f) * static_cast<float>(extent.width);
      y = (ndcY * 0.5f + 0.5f) * static_cast<float>(extent.height);
      return true;
   };

   for (u32 tileIndex = 0; tileIndex < tileCount; ++tileIndex)
   {
      Vulkan.forwardTileMetaScratch[tileIndex].offset = tileIndex * ForwardMaxLightsPerTile;
      Vulkan.forwardTileMetaScratch[tileIndex].count = 0;
   }
   std::fill(Vulkan.forwardTileIndexScratch.begin(), Vulkan.forwardTileIndexScratch.end(), 0u);

   for (u32 lightIndex = 0; lightIndex < generatedLights; ++lightIndex)
   {
      const ForwardGpuLight &light = Vulkan.forwardLightScratch[lightIndex];
      Vec3 center = {light.positionRadius[0], light.positionRadius[1], light.positionRadius[2]};
      float radius = light.positionRadius[3];

      float centerX = 0.0f;
      float centerY = 0.0f;
      if (!projectToScreen(center, centerX, centerY))
      {
         continue;
      }

      Vec3 rightPoint = {
         center.x + camera.right.x * radius,
         center.y + camera.right.y * radius,
         center.z + camera.right.z * radius,
      };
      Vec3 upPoint = {
         center.x + camera.up.x * radius,
         center.y + camera.up.y * radius,
         center.z + camera.up.z * radius,
      };

      float radiusPixels = 2.0f;
      float edgeX = 0.0f;
      float edgeY = 0.0f;
      if (projectToScreen(rightPoint, edgeX, edgeY))
      {
         radiusPixels = std::max(radiusPixels, std::fabs(edgeX - centerX));
         radiusPixels = std::max(radiusPixels, std::fabs(edgeY - centerY));
      }
      if (projectToScreen(upPoint, edgeX, edgeY))
      {
         radiusPixels = std::max(radiusPixels, std::fabs(edgeX - centerX));
         radiusPixels = std::max(radiusPixels, std::fabs(edgeY - centerY));
      }

      i32 minTileX = static_cast<i32>(std::floor((centerX - radiusPixels) / static_cast<float>(ForwardTileSizePixels)));
      i32 maxTileX = static_cast<i32>(std::floor((centerX + radiusPixels) / static_cast<float>(ForwardTileSizePixels)));
      i32 minTileY = static_cast<i32>(std::floor((centerY - radiusPixels) / static_cast<float>(ForwardTileSizePixels)));
      i32 maxTileY = static_cast<i32>(std::floor((centerY + radiusPixels) / static_cast<float>(ForwardTileSizePixels)));

      minTileX = std::max(minTileX, 0);
      minTileY = std::max(minTileY, 0);
      maxTileX = std::min(maxTileX, static_cast<i32>(Vulkan.forwardTileCountX) - 1);
      maxTileY = std::min(maxTileY, static_cast<i32>(Vulkan.forwardTileCountY) - 1);
      if (minTileX > maxTileX || minTileY > maxTileY)
      {
         continue;
      }

      for (i32 tileY = minTileY; tileY <= maxTileY; ++tileY)
      {
         for (i32 tileX = minTileX; tileX <= maxTileX; ++tileX)
         {
            u32 tileIndex = static_cast<u32>(tileY) * Vulkan.forwardTileCountX + static_cast<u32>(tileX);
            ForwardTileMeta &meta = Vulkan.forwardTileMetaScratch[tileIndex];
            if (meta.count >= ForwardMaxLightsPerTile)
            {
               continue;
            }
            usize listOffset = static_cast<usize>(meta.offset + meta.count);
            Vulkan.forwardTileIndexScratch[listOffset] = lightIndex;
            meta.count += 1;
         }
      }
   }

   std::memset(Vulkan.forwardLightMapped[frameIndex], 0, static_cast<usize>(sizeof(ForwardGpuLight)) * static_cast<usize>(ForwardMaxLights));
   std::memcpy(
      Vulkan.forwardLightMapped[frameIndex],
      Vulkan.forwardLightScratch.data(),
      static_cast<usize>(sizeof(ForwardGpuLight)) * static_cast<usize>(generatedLights));
   std::memcpy(
      Vulkan.forwardTileMetaMapped[frameIndex],
      Vulkan.forwardTileMetaScratch.data(),
      static_cast<usize>(sizeof(ForwardTileMeta)) * static_cast<usize>(tileCount));
   std::memcpy(
      Vulkan.forwardTileIndexMapped[frameIndex],
      Vulkan.forwardTileIndexScratch.data(),
      static_cast<usize>(sizeof(u32)) * static_cast<usize>(tileCount) * static_cast<usize>(ForwardMaxLightsPerTile));

   Vulkan.forwardLightCount = generatedLights;
}

void CreateForwardRenderer()
{
   if (Vulkan.forwardRendererReady)
   {
      return;
   }

   Assert(Vulkan.sceneReady, "Create scene before creating forward renderer");
   CreateColorResources();
   CreateDepthResources();
   CreateFrameGlobalsResources();
   CreateShadowResources();
   CreateShadowPipeline();
   CreateForwardLightingResources();
   CreateForwardPipeline();
   Vulkan.forwardRendererReady = true;
}

void DestroyForwardRenderer()
{
   if (!Vulkan.forwardRendererReady)
   {
      return;
   }

   DestroyForwardPipeline();
   DestroyForwardLightingResources();
   DestroyShadowPipeline();
   DestroyShadowResources();
   DestroyFrameGlobalsResources();
   DestroyDepthResources();
   DestroyColorResources();
   Vulkan.forwardRendererReady = false;
}

void CreateForwardPipeline()
{
   if (Vulkan.forwardPipelineReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before pipelines");
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before pipelines");
   Assert(Vulkan.msaaSamples != static_cast<VkSampleCountFlagBits>(0), "MSAA sample count is not initialized");
   Assert(Vulkan.depthResourcesReady, "Create depth resources before pipelines");
   Assert(Vulkan.sceneTextureView != VK_NULL_HANDLE, "Scene texture view is not initialized");
   Assert(Vulkan.sceneTextureSampler != VK_NULL_HANDLE, "Scene texture sampler is not initialized");
   Assert(Vulkan.sceneTextureLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, "Scene texture layout is not shader-read optimal");
   Assert(Vulkan.frameGlobalsReady, "Create frame globals resources before forward pipeline");
   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      Assert(Vulkan.frameGlobalsBuffers[frameIndex] != VK_NULL_HANDLE, "Frame globals buffer is not initialized");
   }
   Assert(Vulkan.shadowResourcesReady, "Create shadow resources before forward pipeline");
   Assert(Vulkan.shadowAtlasView != VK_NULL_HANDLE, "Shadow atlas view is not initialized");
   Assert(Vulkan.shadowAtlasSampler != VK_NULL_HANDLE, "Shadow atlas sampler is not initialized");
   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      Assert(Vulkan.shadowGlobalsBuffers[frameIndex] != VK_NULL_HANDLE, "Shadow globals buffer is not initialized");
   }
   Assert(Vulkan.forwardLightingReady, "Create forward lighting resources before pipeline");
   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      Assert(Vulkan.forwardLightBuffers[frameIndex] != VK_NULL_HANDLE, "Forward light buffer is not initialized");
      Assert(Vulkan.forwardTileMetaBuffers[frameIndex] != VK_NULL_HANDLE, "Forward tile metadata buffer is not initialized");
      Assert(Vulkan.forwardTileIndexBuffers[frameIndex] != VK_NULL_HANDLE, "Forward tile index buffer is not initialized");
   }
   if (Vulkan.msaaSamples != VK_SAMPLE_COUNT_1_BIT)
   {
      Assert(Vulkan.colorResourcesReady, "Create color resources before MSAA forward pipeline");
   }
   Assert(ShaderCacheDirectory[0] != '\0', "Shader cache directory is not defined");

   array<char, 512> vertexPath {};
   array<char, 512> fragmentPath {};
   array<char, 512> skyVertexPath {};
   array<char, 512> skyFragmentPath {};

   const auto buildPath = [](const char *directory, const char *fileName, array<char, 512> &buffer)
   {
      int written = std::snprintf(buffer.data(), buffer.size(), "%s/%s", directory, fileName);
      Assert((written > 0) && (static_cast<size_t>(written) < buffer.size()), "Shader path truncated");
   };

   buildPath(ShaderCacheDirectory, ForwardVertexShaderName, vertexPath);
   buildPath(ShaderCacheDirectory, ForwardFragmentShaderName, fragmentPath);
   buildPath(ShaderCacheDirectory, SkyVertexShaderName, skyVertexPath);
   buildPath(ShaderCacheDirectory, SkyFragmentShaderName, skyFragmentPath);

   Vulkan.forwardVertexShader = CreateShader(vertexPath.data());
   Vulkan.forwardFragmentShader = CreateShader(fragmentPath.data());
   Vulkan.skyVertexShader = CreateShader(skyVertexPath.data());
   Vulkan.skyFragmentShader = CreateShader(skyFragmentPath.data());

   VkDescriptorSetLayoutBinding frameGlobalsBinding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr,
   };
   VkDescriptorSetLayoutBinding textureBinding = {
      .binding = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr,
   };
   VkDescriptorSetLayoutBinding lightBinding = {
      .binding = 2,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr,
   };
   VkDescriptorSetLayoutBinding tileMetaBinding = {
      .binding = 3,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr,
   };
   VkDescriptorSetLayoutBinding tileIndexBinding = {
      .binding = 4,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr,
   };
   VkDescriptorSetLayoutBinding shadowGlobalsBinding = {
      .binding = 5,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr,
   };
   VkDescriptorSetLayoutBinding shadowAtlasBinding = {
      .binding = 6,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr,
   };
   array<VkDescriptorSetLayoutBinding, 7> descriptorBindings = {
      frameGlobalsBinding,
      textureBinding,
      lightBinding,
      tileMetaBinding,
      tileIndexBinding,
      shadowGlobalsBinding,
      shadowAtlasBinding,
   };
   VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<u32>(descriptorBindings.size()),
      .pBindings = descriptorBindings.data(),
   };
   VkResult descriptorLayoutResult = vkCreateDescriptorSetLayout(Vulkan.device, &descriptorLayoutInfo, nullptr, &Vulkan.forwardDescriptorSetLayout);
   Assert(descriptorLayoutResult == VK_SUCCESS, "Failed to create forward descriptor set layout");

   array<VkDescriptorPoolSize, 3> descriptorPoolSizes = {
      VkDescriptorPoolSize{
         .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 2 * FrameOverlap,
      },
      VkDescriptorPoolSize{
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 3 * FrameOverlap,
      },
      VkDescriptorPoolSize{
         .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 2 * FrameOverlap,
      },
   };
   VkDescriptorPoolCreateInfo descriptorPoolInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = FrameOverlap,
      .poolSizeCount = static_cast<u32>(descriptorPoolSizes.size()),
      .pPoolSizes = descriptorPoolSizes.data(),
   };
   VkResult descriptorPoolResult = vkCreateDescriptorPool(Vulkan.device, &descriptorPoolInfo, nullptr, &Vulkan.forwardDescriptorPool);
   Assert(descriptorPoolResult == VK_SUCCESS, "Failed to create forward descriptor pool");

   array<VkDescriptorSetLayout, FrameOverlap> descriptorSetLayouts = {};
   descriptorSetLayouts.fill(Vulkan.forwardDescriptorSetLayout);
   VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = Vulkan.forwardDescriptorPool,
      .descriptorSetCount = FrameOverlap,
      .pSetLayouts = descriptorSetLayouts.data(),
   };
   VkResult descriptorSetResult = vkAllocateDescriptorSets(Vulkan.device, &descriptorSetAllocInfo, Vulkan.forwardDescriptorSets.data());
   Assert(descriptorSetResult == VK_SUCCESS, "Failed to allocate forward descriptor set");

   for (u32 frameIndex = 0; frameIndex < FrameOverlap; ++frameIndex)
   {
      VkDescriptorBufferInfo frameGlobalsBufferInfo = {
         .buffer = Vulkan.frameGlobalsBuffers[frameIndex],
         .offset = 0,
         .range = sizeof(FrameGlobalsGpu),
      };
      VkWriteDescriptorSet frameGlobalsDescriptorWrite = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.forwardDescriptorSets[frameIndex],
         .dstBinding = 0,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .pBufferInfo = &frameGlobalsBufferInfo,
      };

      VkDescriptorImageInfo textureDescriptorImage = {
         .sampler = Vulkan.sceneTextureSampler,
         .imageView = Vulkan.sceneTextureView,
         .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      };
      VkWriteDescriptorSet textureDescriptorWrite = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.forwardDescriptorSets[frameIndex],
         .dstBinding = 1,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = &textureDescriptorImage,
      };

      VkDescriptorBufferInfo lightBufferInfo = {
         .buffer = Vulkan.forwardLightBuffers[frameIndex],
         .offset = 0,
         .range = VK_WHOLE_SIZE,
      };
      VkWriteDescriptorSet lightDescriptorWrite = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.forwardDescriptorSets[frameIndex],
         .dstBinding = 2,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &lightBufferInfo,
      };

      VkDescriptorBufferInfo tileMetaBufferInfo = {
         .buffer = Vulkan.forwardTileMetaBuffers[frameIndex],
         .offset = 0,
         .range = VK_WHOLE_SIZE,
      };
      VkWriteDescriptorSet tileMetaDescriptorWrite = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.forwardDescriptorSets[frameIndex],
         .dstBinding = 3,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &tileMetaBufferInfo,
      };

      VkDescriptorBufferInfo tileIndexBufferInfo = {
         .buffer = Vulkan.forwardTileIndexBuffers[frameIndex],
         .offset = 0,
         .range = VK_WHOLE_SIZE,
      };
      VkWriteDescriptorSet tileIndexDescriptorWrite = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.forwardDescriptorSets[frameIndex],
         .dstBinding = 4,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &tileIndexBufferInfo,
      };

      VkDescriptorBufferInfo shadowGlobalsBufferInfo = {
         .buffer = Vulkan.shadowGlobalsBuffers[frameIndex],
         .offset = 0,
         .range = sizeof(ShadowGlobalsGpu),
      };
      VkWriteDescriptorSet shadowGlobalsDescriptorWrite = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.forwardDescriptorSets[frameIndex],
         .dstBinding = 5,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .pBufferInfo = &shadowGlobalsBufferInfo,
      };

      VkDescriptorImageInfo shadowAtlasDescriptorImage = {
         .sampler = Vulkan.shadowAtlasSampler,
         .imageView = Vulkan.shadowAtlasView,
         .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
      };
      VkWriteDescriptorSet shadowAtlasDescriptorWrite = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.forwardDescriptorSets[frameIndex],
         .dstBinding = 6,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = &shadowAtlasDescriptorImage,
      };

      array<VkWriteDescriptorSet, 7> descriptorWrites = {
         frameGlobalsDescriptorWrite,
         textureDescriptorWrite,
         lightDescriptorWrite,
         tileMetaDescriptorWrite,
         tileIndexDescriptorWrite,
         shadowGlobalsDescriptorWrite,
         shadowAtlasDescriptorWrite,
      };
      vkUpdateDescriptorSets(
         Vulkan.device,
         static_cast<u32>(descriptorWrites.size()),
         descriptorWrites.data(),
         0,
         nullptr);
   }

   VkPushConstantRange pushConstant = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .offset = 0,
      .size = sizeof(ForwardPushConstants),
   };

   VkPipelineLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &Vulkan.forwardDescriptorSetLayout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstant,
   };

   VkResult layoutResult = vkCreatePipelineLayout(Vulkan.device, &layoutInfo, nullptr, &Vulkan.forwardPipelineLayout);
   Assert(layoutResult == VK_SUCCESS, "Failed to create forward pipeline layout");

   VkPipelineShaderStageCreateInfo shaderStages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = Vulkan.forwardVertexShader,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = Vulkan.forwardFragmentShader,
         .pName = "main",
      },
   };

   std::array<VkVertexInputBindingDescription, 2> vertexBindings = {
      VkVertexInputBindingDescription{
         .binding = 0,
         .stride = static_cast<u32>(sizeof(Vertex)),
         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
      VkVertexInputBindingDescription{
         .binding = 1,
         .stride = static_cast<u32>(sizeof(InstanceData)),
         .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
      },
   };

   std::array<VkVertexInputAttributeDescription, 4> vertexAttributes = {
      VkVertexInputAttributeDescription{
         .location = 0,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32_SFLOAT,
         .offset = static_cast<u32>(offsetof(Vertex, position)),
      },
      VkVertexInputAttributeDescription{
         .location = 1,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32_SFLOAT,
         .offset = static_cast<u32>(offsetof(Vertex, normal)),
      },
      VkVertexInputAttributeDescription{
         .location = 2,
         .binding = 0,
         .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = static_cast<u32>(offsetof(Vertex, uv)),
      },
      VkVertexInputAttributeDescription{
         .location = 3,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32A32_SFLOAT,
         .offset = static_cast<u32>(offsetof(InstanceData, translation)),
      },
   };

   VkPipelineVertexInputStateCreateInfo vertexInput = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = static_cast<u32>(vertexBindings.size()),
      .pVertexBindingDescriptions = vertexBindings.data(),
      .vertexAttributeDescriptionCount = static_cast<u32>(vertexAttributes.size()),
      .pVertexAttributeDescriptions = vertexAttributes.data(),
   };

   VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
   };

   VkPipelineViewportStateCreateInfo viewportState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = nullptr,
      .scissorCount = 1,
      .pScissors = nullptr,
   };

   VkPipelineRasterizationStateCreateInfo rasterizer = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable = VK_FALSE,
      .lineWidth = 1.0f,
   };

   VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = Vulkan.msaaSamples,
      .sampleShadingEnable = VK_FALSE,
   };

   VkPipelineDepthStencilStateCreateInfo depthStencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = VK_FALSE,
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 1.0f,
   };

   VkPipelineColorBlendAttachmentState colorBlendAttachment = {
      .blendEnable = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                        VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT,
   };

   VkPipelineColorBlendStateCreateInfo colorBlending = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = VK_FALSE,
      .attachmentCount = 1,
      .pAttachments = &colorBlendAttachment,
   };

   VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   uint32_t dynamicStateCount = static_cast<uint32_t>(sizeof(dynamicStates) / sizeof(dynamicStates[0]));
   VkPipelineDynamicStateCreateInfo dynamicState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = dynamicStateCount,
      .pDynamicStates = dynamicStates,
   };

   VkPipelineRenderingCreateInfo renderingInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &Vulkan.swapchainFormat,
      .depthAttachmentFormat = Vulkan.depthFormat,
      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
   };

   uint32_t shaderStageCount = static_cast<uint32_t>(sizeof(shaderStages) / sizeof(shaderStages[0]));

   VkGraphicsPipelineCreateInfo pipelineInfo = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &renderingInfo,
      .stageCount = shaderStageCount,
      .pStages = shaderStages,
      .pVertexInputState = &vertexInput,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewportState,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pDepthStencilState = &depthStencil,
      .pColorBlendState = &colorBlending,
      .pDynamicState = &dynamicState,
      .layout = Vulkan.forwardPipelineLayout,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
   };

   VkResult pipelineResult = vkCreateGraphicsPipelines(
      Vulkan.device,
      VK_NULL_HANDLE,
      1,
      &pipelineInfo,
      nullptr,
      &Vulkan.forwardPipeline);
   Assert(pipelineResult == VK_SUCCESS, "Failed to create forward graphics pipeline");

   VkPipelineShaderStageCreateInfo skyShaderStages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = Vulkan.skyVertexShader,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = Vulkan.skyFragmentShader,
         .pName = "main",
      },
   };

   VkVertexInputBindingDescription skyBinding = {
      .binding = 0,
      .stride = static_cast<u32>(sizeof(Vertex)),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
   };
   VkVertexInputAttributeDescription skyPositionAttribute = {
      .location = 0,
      .binding = 0,
      .format = VK_FORMAT_R32G32B32_SFLOAT,
      .offset = static_cast<u32>(offsetof(Vertex, position)),
   };
   VkPipelineVertexInputStateCreateInfo skyVertexInput = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &skyBinding,
      .vertexAttributeDescriptionCount = 1,
      .pVertexAttributeDescriptions = &skyPositionAttribute,
   };

   VkPipelineDepthStencilStateCreateInfo skyDepthStencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_FALSE,
      .depthWriteEnable = VK_FALSE,
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = VK_FALSE,
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 1.0f,
   };

   VkGraphicsPipelineCreateInfo skyPipelineInfo = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &renderingInfo,
      .stageCount = 2,
      .pStages = skyShaderStages,
      .pVertexInputState = &skyVertexInput,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewportState,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pDepthStencilState = &skyDepthStencil,
      .pColorBlendState = &colorBlending,
      .pDynamicState = &dynamicState,
      .layout = Vulkan.forwardPipelineLayout,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
   };

   VkResult skyPipelineResult = vkCreateGraphicsPipelines(
      Vulkan.device,
      VK_NULL_HANDLE,
      1,
      &skyPipelineInfo,
      nullptr,
      &Vulkan.skyPipeline);
   Assert(skyPipelineResult == VK_SUCCESS, "Failed to create sky graphics pipeline");

   Vulkan.forwardPipelineReady = true;
}

void DestroyForwardPipeline()
{
   if ((Vulkan.forwardPipeline == VK_NULL_HANDLE) &&
       (Vulkan.skyPipeline == VK_NULL_HANDLE) &&
       (Vulkan.forwardPipelineLayout == VK_NULL_HANDLE) &&
       (Vulkan.forwardDescriptorSetLayout == VK_NULL_HANDLE) &&
       (Vulkan.forwardDescriptorPool == VK_NULL_HANDLE) &&
       (Vulkan.forwardVertexShader == VK_NULL_HANDLE) &&
       (Vulkan.forwardFragmentShader == VK_NULL_HANDLE) &&
       (Vulkan.skyVertexShader == VK_NULL_HANDLE) &&
       (Vulkan.skyFragmentShader == VK_NULL_HANDLE))
   {
      Vulkan.forwardPipelineReady = false;
      return;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.skyPipeline != VK_NULL_HANDLE))
   {
      vkDestroyPipeline(Vulkan.device, Vulkan.skyPipeline, nullptr);
      Vulkan.skyPipeline = VK_NULL_HANDLE;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.forwardPipeline != VK_NULL_HANDLE))
   {
      vkDestroyPipeline(Vulkan.device, Vulkan.forwardPipeline, nullptr);
      Vulkan.forwardPipeline = VK_NULL_HANDLE;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.forwardPipelineLayout != VK_NULL_HANDLE))
   {
      vkDestroyPipelineLayout(Vulkan.device, Vulkan.forwardPipelineLayout, nullptr);
      Vulkan.forwardPipelineLayout = VK_NULL_HANDLE;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.forwardDescriptorPool != VK_NULL_HANDLE))
   {
      vkDestroyDescriptorPool(Vulkan.device, Vulkan.forwardDescriptorPool, nullptr);
      Vulkan.forwardDescriptorPool = VK_NULL_HANDLE;
   }
   Vulkan.forwardDescriptorSets.fill(VK_NULL_HANDLE);

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.forwardDescriptorSetLayout != VK_NULL_HANDLE))
   {
      vkDestroyDescriptorSetLayout(Vulkan.device, Vulkan.forwardDescriptorSetLayout, nullptr);
      Vulkan.forwardDescriptorSetLayout = VK_NULL_HANDLE;
   }

   DestroyShader(Vulkan.forwardVertexShader);
   DestroyShader(Vulkan.forwardFragmentShader);
   DestroyShader(Vulkan.skyVertexShader);
   DestroyShader(Vulkan.skyFragmentShader);

   Vulkan.forwardPipelineReady = false;
}

void RecreateSwapchain()
{
   if (!Vulkan.deviceReady)
   {
      return;
   }

   Size framebuffer = GetFramebufferSize();
   while (((framebuffer.width == 0) || (framebuffer.height == 0)) && !WindowShouldClose())
   {
      PollEvents();
      framebuffer = GetFramebufferSize();
   }

   if ((framebuffer.width == 0) || (framebuffer.height == 0))
   {
      LogWarn("[vulkan] Skipping swapchain recreation because framebuffer is zero-sized");
      return;
   }

   vkDeviceWaitIdle(Vulkan.device);

   DestroyFrameResources();
   DestroyForwardRenderer();
   DestroySwapchainImageViews();
   Vulkan.swapchainReady = false;
   Vulkan.swapchainImageViewsReady = false;

   CreateSwapchain();
   CreateSwapchainImageViews();
   CreateForwardRenderer();
   CreateFrameResources();
   ResetCameraAccum();
}

void CreateFrameResources()
{
   if (Vulkan.frameResourcesReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before frame resources");
   Assert(Vulkan.graphicsQueueFamilyIndex < Vulkan.queueFamilyCount, "Graphics queue family index is invalid");
   Assert(Vulkan.swapchainImageCount > 0, "Swapchain images must exist before creating frame resources");

   Vulkan.gpuTimestampQueryPool = VK_NULL_HANDLE;
   Vulkan.gpuTimestampPeriodNanoseconds = 0.0f;
   Vulkan.gpuTimestampsSupported = false;
   Vulkan.gpuTimestampPending.fill(false);
   Vulkan.gpuTimestampsReady = false;

   VkPhysicalDeviceProperties physicalProperties = {};
   vkGetPhysicalDeviceProperties(Vulkan.physicalDevice, &physicalProperties);
   Vulkan.gpuTimestampPeriodNanoseconds = physicalProperties.limits.timestampPeriod;

   span<const VkQueueFamilyProperties> queueFamilies = GetQueueFamilyProperties(Vulkan.physicalDevice);
   bool graphicsQueueSupportsTimestamps = queueFamilies[Vulkan.graphicsQueueFamilyIndex].timestampValidBits > 0;

   if (graphicsQueueSupportsTimestamps && (Vulkan.gpuTimestampPeriodNanoseconds > 0.0f))
   {
      VkQueryPoolCreateInfo queryPoolInfo = {
         .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
         .queryType = VK_QUERY_TYPE_TIMESTAMP,
         .queryCount = FrameOverlap * GpuTimestampSlotsPerFrame,
      };
      VkResult queryPoolResult = vkCreateQueryPool(Vulkan.device, &queryPoolInfo, nullptr, &Vulkan.gpuTimestampQueryPool);
      Assert(queryPoolResult == VK_SUCCESS, "Failed to create GPU timestamp query pool");
      Vulkan.gpuTimestampsSupported = true;
      Vulkan.gpuTimestampsReady = true;
   }
   else
   {
      LogWarn("[vulkan] GPU timestamps unsupported on graphics queue; GPU stage timing disabled");
   }

   for (u32 index = 0; index < FrameOverlap; ++index)
   {
      FrameResources &frame = Vulkan.frames[index];

      VkCommandPoolCreateInfo poolInfo = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
         .queueFamilyIndex = Vulkan.graphicsQueueFamilyIndex,
      };

      VkResult result = vkCreateCommandPool(Vulkan.device, &poolInfo, nullptr, &frame.commandPool);
      Assert(result == VK_SUCCESS, "Failed to create command pool");

      VkCommandBufferAllocateInfo allocInfo = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .commandPool = frame.commandPool,
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      };

      result = vkAllocateCommandBuffers(Vulkan.device, &allocInfo, &frame.commandBuffer);
      Assert(result == VK_SUCCESS, "Failed to allocate command buffer");

      VkFenceCreateInfo fenceInfo = {
         .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
         .flags = VK_FENCE_CREATE_SIGNALED_BIT,
      };

      result = vkCreateFence(Vulkan.device, &fenceInfo, nullptr, &frame.inFlightFence);
      Assert(result == VK_SUCCESS, "Failed to create fence");

      VkSemaphoreCreateInfo semaphoreInfo = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      };

      result = vkCreateSemaphore(Vulkan.device, &semaphoreInfo, nullptr, &frame.imageAvailableSemaphore);
      Assert(result == VK_SUCCESS, "Failed to create image-available semaphore");
   }

   for (u32 index = 0; index < Vulkan.swapchainImageCount; ++index)
   {
      VkSemaphoreCreateInfo semaphoreInfo = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      };

      VkResult result = vkCreateSemaphore(Vulkan.device, &semaphoreInfo, nullptr, &Vulkan.swapchainRenderFinishedSemaphores[index]);
      Assert(result == VK_SUCCESS, "Failed to create render-finished semaphore");

      Vulkan.swapchainImageFences[index] = VK_NULL_HANDLE;
   }

   for (u32 index = Vulkan.swapchainImageCount; index < MaxSwapchainImages; ++index)
   {
      Vulkan.swapchainRenderFinishedSemaphores[index] = VK_NULL_HANDLE;
      Vulkan.swapchainImageFences[index] = VK_NULL_HANDLE;
   }

   Vulkan.frameResourcesReady = true;
   Vulkan.currentFrame = 0;
}

void DestroyFrameResources()
{
   if (!Vulkan.frameResourcesReady)
   {
      return;
   }

   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.gpuTimestampQueryPool = VK_NULL_HANDLE;
      Vulkan.gpuTimestampPeriodNanoseconds = 0.0f;
      Vulkan.gpuTimestampsSupported = false;
      Vulkan.gpuTimestampPending.fill(false);
      Vulkan.gpuTimestampsReady = false;
      Vulkan.frameResourcesReady = false;
      return;
   }

   if (Vulkan.gpuTimestampQueryPool != VK_NULL_HANDLE)
   {
      vkDestroyQueryPool(Vulkan.device, Vulkan.gpuTimestampQueryPool, nullptr);
      Vulkan.gpuTimestampQueryPool = VK_NULL_HANDLE;
   }
   Vulkan.gpuTimestampPeriodNanoseconds = 0.0f;
   Vulkan.gpuTimestampsSupported = false;
   Vulkan.gpuTimestampPending.fill(false);
   Vulkan.gpuTimestampsReady = false;

   for (u32 index = 0; index < FrameOverlap; ++index)
   {
      FrameResources &frame = Vulkan.frames[index];

      if (frame.imageAvailableSemaphore != VK_NULL_HANDLE)
      {
         vkDestroySemaphore(Vulkan.device, frame.imageAvailableSemaphore, nullptr);
         frame.imageAvailableSemaphore = VK_NULL_HANDLE;
      }

      if (frame.inFlightFence != VK_NULL_HANDLE)
      {
         vkDestroyFence(Vulkan.device, frame.inFlightFence, nullptr);
         frame.inFlightFence = VK_NULL_HANDLE;
      }

      if ((frame.commandBuffer != VK_NULL_HANDLE) && (frame.commandPool != VK_NULL_HANDLE))
      {
         vkFreeCommandBuffers(Vulkan.device, frame.commandPool, 1, &frame.commandBuffer);
         frame.commandBuffer = VK_NULL_HANDLE;
      }

      if (frame.commandPool != VK_NULL_HANDLE)
      {
         vkDestroyCommandPool(Vulkan.device, frame.commandPool, nullptr);
         frame.commandPool = VK_NULL_HANDLE;
      }
   }

   for (u32 index = 0; index < Vulkan.swapchainImageCount; ++index)
   {
      if (Vulkan.swapchainRenderFinishedSemaphores[index] != VK_NULL_HANDLE)
      {
         vkDestroySemaphore(Vulkan.device, Vulkan.swapchainRenderFinishedSemaphores[index], nullptr);
         Vulkan.swapchainRenderFinishedSemaphores[index] = VK_NULL_HANDLE;
      }

      Vulkan.swapchainImageFences[index] = VK_NULL_HANDLE;
   }

   for (u32 index = Vulkan.swapchainImageCount; index < MaxSwapchainImages; ++index)
   {
      Vulkan.swapchainRenderFinishedSemaphores[index] = VK_NULL_HANDLE;
      Vulkan.swapchainImageFences[index] = VK_NULL_HANDLE;
   }

   Vulkan.frameResourcesReady = false;
   Vulkan.currentFrame = 0;
}

auto AcquireNextImage(u32 &imageIndex, u32 &frameIndex, AcquireTiming &timing) -> VkResult
{
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before acquiring images");
   Assert(Vulkan.deviceReady, "Create the Vulkan device before acquiring images");
   Assert(Vulkan.frameResourcesReady, "Create frame resources before acquiring images");

   timing = {};
   const auto toMilliseconds = [](double seconds) -> float
   {
      float ms = static_cast<float>(seconds * 1000.0);
      if (!std::isfinite(ms) || (ms < 0.0f))
      {
         return 0.0f;
      }
      return ms;
   };

   frameIndex = Vulkan.currentFrame;
   Assert(frameIndex < FrameOverlap, "Frame index out of range");

   FrameResources &frame = Vulkan.frames[frameIndex];
   Assert(frame.inFlightFence != VK_NULL_HANDLE, "Frame fence is not initialized");
   Assert(frame.imageAvailableSemaphore != VK_NULL_HANDLE, "Frame image-available semaphore is not initialized");

   double totalStartTime = glfwGetTime();
   double waitFrameFenceStartTime = glfwGetTime();
   VkResult waitResult = vkWaitForFences(Vulkan.device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
   double waitFrameFenceEndTime = glfwGetTime();
   timing.waitFrameFenceMs = toMilliseconds(waitFrameFenceEndTime - waitFrameFenceStartTime);
   Assert(waitResult == VK_SUCCESS, "Failed to wait for in-flight fence");

   if (Vulkan.gpuTimestampsReady && Vulkan.gpuTimestampsSupported && Vulkan.gpuTimestampPending[frameIndex])
   {
      u32 queryBase = frameIndex * GpuTimestampSlotsPerFrame;
      std::array<u64, GpuTimestampSlotsPerFrame> timestampValues = {};
      VkResult queryResult = vkGetQueryPoolResults(
         Vulkan.device,
         Vulkan.gpuTimestampQueryPool,
         queryBase,
         GpuTimestampSlotsPerFrame,
         sizeof(timestampValues),
         timestampValues.data(),
         sizeof(u64),
         VK_QUERY_RESULT_64_BIT);

      if (queryResult == VK_SUCCESS)
      {
         u64 shadowStart = timestampValues[GpuTimestampSlotShadowStart];
         u64 shadowEnd = timestampValues[GpuTimestampSlotShadowEnd];
         u64 frameEnd = timestampValues[GpuTimestampSlotFrameEnd];
         if ((shadowEnd >= shadowStart) && (frameEnd >= shadowEnd))
         {
            double tickToMilliseconds = static_cast<double>(Vulkan.gpuTimestampPeriodNanoseconds) / 1000000.0;
            timing.gpuShadowMs = static_cast<float>(static_cast<double>(shadowEnd - shadowStart) * tickToMilliseconds);
            timing.gpuForwardMs = static_cast<float>(static_cast<double>(frameEnd - shadowEnd) * tickToMilliseconds);
            timing.gpuTotalMs = static_cast<float>(static_cast<double>(frameEnd - shadowStart) * tickToMilliseconds);
            timing.gpuValid = true;
         }
      }
      Vulkan.gpuTimestampPending[frameIndex] = false;
   }

   imageIndex = UINT32_MAX;
   double acquireStartTime = glfwGetTime();
   VkResult acquireResult = vkAcquireNextImageKHR(
      Vulkan.device,
      Vulkan.swapchain,
      UINT64_MAX,
      frame.imageAvailableSemaphore,
      VK_NULL_HANDLE,
      &imageIndex);
   double acquireEndTime = glfwGetTime();
   timing.acquireCallMs = toMilliseconds(acquireEndTime - acquireStartTime);

   if ((acquireResult != VK_SUCCESS) &&
       (acquireResult != VK_SUBOPTIMAL_KHR) &&
       (acquireResult != VK_ERROR_OUT_OF_DATE_KHR))
   {
      Assert(false, "Failed to acquire swapchain image");
   }

   if ((acquireResult == VK_SUCCESS) || (acquireResult == VK_SUBOPTIMAL_KHR))
   {
      Assert(imageIndex < Vulkan.swapchainImageCount, "Vulkan returned an invalid swapchain image index");
      VkFence &imageFence = Vulkan.swapchainImageFences[imageIndex];
      if (imageFence != VK_NULL_HANDLE)
      {
         if (imageFence != frame.inFlightFence)
         {
            double waitImageFenceStartTime = glfwGetTime();
            VkResult waitResult = vkWaitForFences(Vulkan.device, 1, &imageFence, VK_TRUE, UINT64_MAX);
            double waitImageFenceEndTime = glfwGetTime();
            timing.waitImageFenceMs = toMilliseconds(waitImageFenceEndTime - waitImageFenceStartTime);
            Assert(waitResult == VK_SUCCESS, "Failed to wait for image fence");
         }
      }
      Vulkan.swapchainImageFences[imageIndex] = frame.inFlightFence;
   }

   double totalEndTime = glfwGetTime();
   timing.totalMs = toMilliseconds(totalEndTime - totalStartTime);
   return acquireResult;
}

auto DrawFrameForward(u32 frameIndex, u32 imageIndex, const GradientParams &gradient) -> VkResult
{
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before recording commands");
   Assert(Vulkan.swapchainImageViewsReady, "Create swapchain image views before recording commands");
   Assert(Vulkan.frameResourcesReady, "Frame resources must exist before recording commands");
   Assert(Vulkan.sceneReady, "Create the scene before drawing");
   Assert(Vulkan.forwardRendererReady, "Create the forward renderer before drawing");
   Assert(Vulkan.forwardPipelineReady, "Forward pipeline must be ready before recording commands");
   Assert(Vulkan.forwardLightingReady, "Forward lighting resources must be ready before recording commands");
   Assert(Vulkan.frameGlobalsReady, "Frame globals resources must be ready before recording commands");
   Assert(Vulkan.shadowResourcesReady, "Shadow resources must be ready before recording commands");
   Assert(Vulkan.shadowPipelineReady, "Shadow pipeline must be ready before recording commands");
   Assert(Vulkan.msaaSamples != static_cast<VkSampleCountFlagBits>(0), "MSAA sample count is not initialized");
   Assert(Vulkan.depthResourcesReady, "Depth resources must be ready before recording commands");
   Assert(Vulkan.depthView != VK_NULL_HANDLE, "Depth view is not initialized");
   bool msaaEnabled = Vulkan.msaaSamples != VK_SAMPLE_COUNT_1_BIT;
   if (msaaEnabled)
   {
      Assert(Vulkan.colorResourcesReady, "MSAA color resources must be ready before recording commands");
      Assert(Vulkan.colorView != VK_NULL_HANDLE, "MSAA color view is not initialized");
   }
   Assert(frameIndex < FrameOverlap, "Frame index out of range");
   Assert(imageIndex < Vulkan.swapchainImageCount, "Swapchain image index out of range");

   FrameResources &frame = Vulkan.frames[frameIndex];
   Assert(frame.commandPool != VK_NULL_HANDLE, "Frame command pool is not initialized");
   Assert(frame.commandBuffer != VK_NULL_HANDLE, "Frame command buffer is not initialized");

   VkResult resetResult = vkResetCommandPool(Vulkan.device, frame.commandPool, 0);
   Assert(resetResult == VK_SUCCESS, "Failed to reset command pool");

   VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };

   VkResult beginResult = vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);
   if (beginResult != VK_SUCCESS)
   {
      return beginResult;
   }

   u32 timestampQueryBase = frameIndex * GpuTimestampSlotsPerFrame;
   if (Vulkan.gpuTimestampsReady && Vulkan.gpuTimestampsSupported)
   {
      vkCmdResetQueryPool(frame.commandBuffer, Vulkan.gpuTimestampQueryPool, timestampQueryBase, GpuTimestampSlotsPerFrame);
      vkCmdWriteTimestamp(
         frame.commandBuffer,
         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
         Vulkan.gpuTimestampQueryPool,
         timestampQueryBase + GpuTimestampSlotShadowStart);
   }

   VkExtent2D extent = Vulkan.swapchainExtent;
   Assert((extent.width > 0) && (extent.height > 0), "Swapchain extent is invalid");

   CameraParams camera = GetCameraParams();
   UpdateForwardLightingData(camera, extent, gradient.time, frameIndex);
   UpdateFrameGlobals(camera, extent, gradient.time, frameIndex);
   UpdateShadowCascades(camera, extent, frameIndex);
   RecordShadowPass(frame.commandBuffer);
   if (Vulkan.gpuTimestampsReady && Vulkan.gpuTimestampsSupported)
   {
      vkCmdWriteTimestamp(
         frame.commandBuffer,
         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
         Vulkan.gpuTimestampQueryPool,
         timestampQueryBase + GpuTimestampSlotShadowEnd);
   }

   VkImage image = Vulkan.swapchainImages[imageIndex];
   VkImageView imageView = Vulkan.swapchainImageViews[imageIndex];
   VkImageLayout currentLayout = Vulkan.swapchainImageLayouts[imageIndex];
   if ((currentLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) && (currentLayout != VK_IMAGE_LAYOUT_UNDEFINED))
   {
      currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   }

   VkImageSubresourceRange subresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
   };

   bool hasStencil = (Vulkan.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) ||
                     (Vulkan.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT);
   VkImageAspectFlags depthAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
   if (hasStencil)
   {
      depthAspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
   }

   VkImageSubresourceRange depthSubresource = {
      .aspectMask = depthAspectMask,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
   };

   VkImageMemoryBarrier barrierToAttachment = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = currentLayout,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = subresource,
   };

   vkCmdPipelineBarrier(
      frame.commandBuffer,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &barrierToAttachment);

   if (msaaEnabled)
   {
      VkImageMemoryBarrier colorBarrier = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = (Vulkan.colorLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0u : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .oldLayout = Vulkan.colorLayout,
         .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = Vulkan.colorImage,
         .subresourceRange = subresource,
      };

      vkCmdPipelineBarrier(
         frame.commandBuffer,
         (Vulkan.colorLayout == VK_IMAGE_LAYOUT_UNDEFINED)
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         0,
         0,
         nullptr,
         0,
         nullptr,
         1,
         &colorBarrier);
   }

   VkImageMemoryBarrier depthBarrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = (Vulkan.depthLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0u : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .oldLayout = Vulkan.depthLayout,
      .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = Vulkan.depthImage,
      .subresourceRange = depthSubresource,
   };

   vkCmdPipelineBarrier(
      frame.commandBuffer,
      (Vulkan.depthLayout == VK_IMAGE_LAYOUT_UNDEFINED)
         ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
         : (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &depthBarrier);

   VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
   VkClearDepthStencilValue clearDepth = {
      .depth = 1.0f,
      .stencil = 0u,
   };

   VkImageView colorAttachmentView = msaaEnabled ? Vulkan.colorView : imageView;
   VkAttachmentStoreOp colorStoreOp = msaaEnabled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;

   VkRenderingAttachmentInfo colorAttachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = colorAttachmentView,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .resolveMode = msaaEnabled ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE,
      .resolveImageView = msaaEnabled ? imageView : VK_NULL_HANDLE,
      .resolveImageLayout = msaaEnabled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = colorStoreOp,
      .clearValue = {.color = clearColor},
   };

   VkRenderingAttachmentInfo depthAttachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = Vulkan.depthView,
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.depthStencil = clearDepth},
   };

   VkRenderingInfo renderingInfo = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {
         .offset = {0, 0},
         .extent = extent,
      },
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachment,
      .pDepthAttachment = &depthAttachment,
   };

   vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);

   VkViewport viewport = {
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<float>(extent.width),
      .height = static_cast<float>(extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);

   VkRect2D scissor = {
      .offset = {0, 0},
      .extent = extent,
   };
   vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);

   Assert(Vulkan.forwardDescriptorSets[frameIndex] != VK_NULL_HANDLE, "Forward descriptor set is not initialized");
   Assert(Vulkan.skyPipeline != VK_NULL_HANDLE, "Sky pipeline is not initialized");
   Assert(Vulkan.sceneVertexBuffer != VK_NULL_HANDLE, "Scene vertex buffer is not initialized");
   Assert(Vulkan.sceneIndexBuffer != VK_NULL_HANDLE, "Scene index buffer is not initialized");
   Assert(Vulkan.sceneInstanceBuffer != VK_NULL_HANDLE, "Scene instance buffer is not initialized");
   Assert(Vulkan.skyVertexBuffer != VK_NULL_HANDLE, "Sky vertex buffer is not initialized");
   Assert(Vulkan.skyIndexBuffer != VK_NULL_HANDLE, "Sky index buffer is not initialized");
   Assert(Vulkan.sceneCarIndexCount > 0, "Scene car index count is zero");
   Assert(Vulkan.sceneGroundIndexCount > 0, "Scene ground index count is zero");
   Assert(Vulkan.sceneCarInstanceCount > 0, "Scene car instance count is zero");
   Assert(Vulkan.sceneInstanceCount > Vulkan.sceneGroundInstanceIndex, "Scene ground instance index is out of range");
   Assert(Vulkan.skyIndexCount > 0, "Sky index count is zero");

   vkCmdBindDescriptorSets(
      frame.commandBuffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      Vulkan.forwardPipelineLayout,
      0,
      1,
      &Vulkan.forwardDescriptorSets[frameIndex],
      0,
      nullptr);

   ForwardPushConstants constants = {};
   constants.model[0] = 1.0f;
   constants.model[5] = 1.0f;
   constants.model[10] = 1.0f;
   constants.model[15] = 1.0f;
   float pulse = 0.92f + 0.08f * std::sin(gradient.time * 0.75f);
   constants.tint[0] = pulse;
   constants.tint[1] = pulse;
   constants.tint[2] = pulse;
   constants.tint[3] = 1.0f;

   vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan.skyPipeline);
   VkDeviceSize skyVertexOffset = 0;
   vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &Vulkan.skyVertexBuffer, &skyVertexOffset);
   vkCmdBindIndexBuffer(frame.commandBuffer, Vulkan.skyIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
   vkCmdPushConstants(
      frame.commandBuffer,
      Vulkan.forwardPipelineLayout,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      0,
      sizeof(ForwardPushConstants),
      &constants);
   vkCmdDrawIndexed(frame.commandBuffer, Vulkan.skyIndexCount, 1, 0, 0, 0);

   vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan.forwardPipeline);
   std::array<VkBuffer, 2> sceneVertexBuffers = {Vulkan.sceneVertexBuffer, Vulkan.sceneInstanceBuffer};
   std::array<VkDeviceSize, 2> sceneVertexOffsets = {0, 0};
   vkCmdBindVertexBuffers(
      frame.commandBuffer,
      0,
      static_cast<u32>(sceneVertexBuffers.size()),
      sceneVertexBuffers.data(),
      sceneVertexOffsets.data());
   vkCmdBindIndexBuffer(frame.commandBuffer, Vulkan.sceneIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
   vkCmdPushConstants(
      frame.commandBuffer,
      Vulkan.forwardPipelineLayout,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      0,
      sizeof(ForwardPushConstants),
      &constants);
   vkCmdDrawIndexed(frame.commandBuffer, Vulkan.sceneCarIndexCount, Vulkan.sceneCarInstanceCount, 0, 0, 0);
   vkCmdDrawIndexed(frame.commandBuffer, Vulkan.sceneGroundIndexCount, 1, Vulkan.sceneGroundFirstIndex, 0, Vulkan.sceneGroundInstanceIndex);

   vkCmdEndRendering(frame.commandBuffer);

   VkImageMemoryBarrier barrierToPresent = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = 0,
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = subresource,
   };

   vkCmdPipelineBarrier(
      frame.commandBuffer,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &barrierToPresent);

   if (Vulkan.gpuTimestampsReady && Vulkan.gpuTimestampsSupported)
   {
      vkCmdWriteTimestamp(
         frame.commandBuffer,
         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
         Vulkan.gpuTimestampQueryPool,
         timestampQueryBase + GpuTimestampSlotFrameEnd);
   }

   VkResult endResult = vkEndCommandBuffer(frame.commandBuffer);
   if (endResult == VK_SUCCESS)
   {
      Vulkan.swapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      Vulkan.depthLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      if (msaaEnabled)
      {
         Vulkan.colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      }
   }

   return endResult;
}

auto SubmitFrame(u32 frameIndex, u32 imageIndex, SubmitTiming &timing) -> VkResult
{
   Assert(Vulkan.frameResourcesReady, "Frame resources must exist before submitting work");
   Assert(Vulkan.graphicsQueue != VK_NULL_HANDLE, "Graphics queue is not initialized");
   Assert(Vulkan.presentQueue != VK_NULL_HANDLE, "Present queue is not initialized");
   Assert(frameIndex < FrameOverlap, "Frame index out of range");
   Assert(imageIndex < Vulkan.swapchainImageCount, "Swapchain image index out of range");

   FrameResources &frame = Vulkan.frames[frameIndex];
   Assert(frame.commandBuffer != VK_NULL_HANDLE, "Frame command buffer is not initialized");
   Assert(frame.imageAvailableSemaphore != VK_NULL_HANDLE, "Frame image-available semaphore is not initialized");
   Assert(frame.inFlightFence != VK_NULL_HANDLE, "Frame fence is not initialized");
   VkSemaphore renderFinishedSemaphore = Vulkan.swapchainRenderFinishedSemaphores[imageIndex];
   Assert(renderFinishedSemaphore != VK_NULL_HANDLE, "Render-finished semaphore for swapchain image is not initialized");

   timing = {};
   const auto toMilliseconds = [](double seconds) -> float
   {
      float ms = static_cast<float>(seconds * 1000.0);
      if (!std::isfinite(ms) || (ms < 0.0f))
      {
         return 0.0f;
      }
      return ms;
   };
   double totalStartTime = glfwGetTime();

   VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
   VkSemaphore waitSemaphores[] = {frame.imageAvailableSemaphore};
   VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};

   double resetStartTime = glfwGetTime();
   VkResult resetResult = vkResetFences(Vulkan.device, 1, &frame.inFlightFence);
   double resetEndTime = glfwGetTime();
   timing.resetFenceMs = toMilliseconds(resetEndTime - resetStartTime);
   Assert(resetResult == VK_SUCCESS, "Failed to reset in-flight fence");

   VkSubmitInfo submitInfo = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = waitSemaphores,
      .pWaitDstStageMask = waitStages,
      .commandBufferCount = 1,
      .pCommandBuffers = &frame.commandBuffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = signalSemaphores,
   };

   double submitStartTime = glfwGetTime();
   VkResult submitResult = vkQueueSubmit(Vulkan.graphicsQueue, 1, &submitInfo, frame.inFlightFence);
   double submitEndTime = glfwGetTime();
   timing.queueSubmitMs = toMilliseconds(submitEndTime - submitStartTime);
   Assert(submitResult == VK_SUCCESS, "Failed to submit command buffer");

   VkSwapchainKHR swapchainHandle = Vulkan.swapchain;
   VkPresentInfoKHR presentInfo = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = signalSemaphores,
      .swapchainCount = 1,
      .pSwapchains = &swapchainHandle,
      .pImageIndices = &imageIndex,
      .pResults = nullptr,
   };

   double presentStartTime = glfwGetTime();
   VkResult presentResult = vkQueuePresentKHR(Vulkan.presentQueue, &presentInfo);
   double presentEndTime = glfwGetTime();
   timing.queuePresentMs = toMilliseconds(presentEndTime - presentStartTime);
   timing.totalMs = toMilliseconds(presentEndTime - totalStartTime);

   if ((presentResult == VK_ERROR_OUT_OF_DATE_KHR) || (presentResult == VK_SUBOPTIMAL_KHR))
   {
      return presentResult;
   }

   Assert(presentResult == VK_SUCCESS, "Failed to present swapchain image");

   if (Vulkan.gpuTimestampsReady && Vulkan.gpuTimestampsSupported)
   {
      Vulkan.gpuTimestampPending[frameIndex] = true;
   }

   Vulkan.currentFrame = (Vulkan.currentFrame + 1) % FrameOverlap;
   return VK_SUCCESS;
}
