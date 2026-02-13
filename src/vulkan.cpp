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
static constexpr u32 SceneGridWidth = 32;
static constexpr u32 SceneGridDepth = 32;
static constexpr float SceneGridSpacing = 1.15f;

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
   u32 currentFrame;

   VkShaderModule forwardVertexShader;
   VkShaderModule forwardFragmentShader;
   VkPipelineLayout forwardPipelineLayout;
   VkPipeline forwardPipeline;
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
   VkBuffer uploadStagingBuffer;
   VkDeviceMemory uploadStagingMemory;
   void *uploadStagingMapped;
   VkDeviceSize uploadStagingCapacity;
   u32 sceneIndexCount;
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
   bool forwardPipelineReady;

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
   Vulkan.uploadStagingBuffer = VK_NULL_HANDLE;
   Vulkan.uploadStagingMemory = VK_NULL_HANDLE;
   Vulkan.uploadStagingMapped = nullptr;
   Vulkan.uploadStagingCapacity = 0;
   Vulkan.sceneIndexCount = 0;
   Vulkan.decodeScratch.clear();
   Vulkan.colorResourcesReady = false;
   Vulkan.depthResourcesReady = false;
   Vulkan.sceneReady = false;
   Vulkan.forwardRendererReady = false;
   Vulkan.forwardPipelineReady = false;

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

   manifest::ResolvedAsset sceneAsset = manifest::kenney::handles::n3d_assets::prototype_kit::models_obj_format_shape_triangular_prism_obj.Resolve(manifestBlob);
   Assert(sceneAsset.valid, "Scene asset handle failed to resolve");
   Assert(sceneAsset.format == manifest::AssetFormat::MESH_PNUV_F32_U32, "Scene asset is not a packed mesh payload");

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

   for (u32 index : baseIndices)
   {
      Assert(index < baseVertices.size(), "Packed scene mesh index references out-of-range vertex");
   }

   // Center only on XZ so layout is world-ground aligned.
   Vec3 minBounds = baseVertices[0].position;
   Vec3 maxBounds = baseVertices[0].position;
   for (const Vertex &vertex : baseVertices)
   {
      minBounds.x = std::min(minBounds.x, vertex.position.x);
      minBounds.z = std::min(minBounds.z, vertex.position.z);

      maxBounds.x = std::max(maxBounds.x, vertex.position.x);
      maxBounds.z = std::max(maxBounds.z, vertex.position.z);
   }

   Vec3 centerXZ = {
      (minBounds.x + maxBounds.x) * 0.5f,
      0.0f,
      (minBounds.z + maxBounds.z) * 0.5f,
   };

   for (Vertex &vertex : baseVertices)
   {
      vertex.position.x -= centerXZ.x;
      vertex.position.z -= centerXZ.z;
   }

   // Put the cube base on the ground plane (y = 0) before grid expansion.
   float minY = baseVertices[0].position.y;
   for (const Vertex &vertex : baseVertices)
   {
      minY = std::min(minY, vertex.position.y);
   }
   for (Vertex &vertex : baseVertices)
   {
      vertex.position.y -= minY;
   }

   Assert(!baseVertices.empty(), "Base mesh vertices cannot be empty");
   Assert(!baseIndices.empty(), "Base mesh indices cannot be empty");

   usize gridInstanceCount = static_cast<usize>(SceneGridWidth) * static_cast<usize>(SceneGridDepth);
   std::vector<Vertex> vertices;
   std::vector<u32> indices;
   vertices.reserve(baseVertices.size() * gridInstanceCount);
   indices.reserve(baseIndices.size() * gridInstanceCount);

   float xOffsetStart = (static_cast<float>(SceneGridWidth) - 1.0f) * 0.5f;
   float zOffsetStart = (static_cast<float>(SceneGridDepth) - 1.0f) * 0.5f;

   for (u32 z = 0; z < SceneGridDepth; ++z)
   {
      for (u32 x = 0; x < SceneGridWidth; ++x)
      {
         float worldX = (static_cast<float>(x) - xOffsetStart) * SceneGridSpacing;
         float worldY = 0.0f;
         float worldZ = (static_cast<float>(z) - zOffsetStart) * SceneGridSpacing;

         u32 vertexOffset = static_cast<u32>(vertices.size());
         for (const Vertex &source : baseVertices)
         {
            Vertex expanded = source;
            expanded.position.x += worldX;
            expanded.position.y += worldY;
            expanded.position.z += worldZ;
            vertices.push_back(expanded);
         }

         for (u32 sourceIndex : baseIndices)
         {
            indices.push_back(vertexOffset + sourceIndex);
         }
      }
   }
   Assert(!vertices.empty(), "Grid vertices cannot be empty");
   Assert(!indices.empty(), "Grid indices cannot be empty");

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

   VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(vertices.size() * sizeof(Vertex));
   VkDeviceSize indexBytes = static_cast<VkDeviceSize>(indices.size() * sizeof(u32));
   VkDeviceSize totalUploadBytes = vertexBytes + indexBytes;

   createOrResizeStagingBuffer(totalUploadBytes);
   Assert(Vulkan.uploadStagingMapped != nullptr, "Staging buffer is not mapped");

   byte *stagingBytes = reinterpret_cast<byte *>(Vulkan.uploadStagingMapped);
   std::memcpy(stagingBytes, vertices.data(), static_cast<size_t>(vertexBytes));
   std::memcpy(stagingBytes + static_cast<usize>(vertexBytes), indices.data(), static_cast<size_t>(indexBytes));

   createDeviceLocalBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBytes, Vulkan.sceneVertexBuffer, Vulkan.sceneVertexMemory);
   createDeviceLocalBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBytes, Vulkan.sceneIndexBuffer, Vulkan.sceneIndexMemory);

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
      .size = vertexBytes,
   };
   vkCmdCopyBuffer(uploadCmd, Vulkan.uploadStagingBuffer, Vulkan.sceneVertexBuffer, 1, &vertexCopy);

   VkBufferCopy indexCopy = {
      .srcOffset = vertexBytes,
      .dstOffset = 0,
      .size = indexBytes,
   };
   vkCmdCopyBuffer(uploadCmd, Vulkan.uploadStagingBuffer, Vulkan.sceneIndexBuffer, 1, &indexCopy);

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

   Vulkan.sceneIndexCount = static_cast<u32>(indices.size());
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
      Vulkan.uploadStagingBuffer = VK_NULL_HANDLE;
      Vulkan.uploadStagingMemory = VK_NULL_HANDLE;
      Vulkan.uploadStagingMapped = nullptr;
      Vulkan.uploadStagingCapacity = 0;
      Vulkan.sceneIndexCount = 0;
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

   Vulkan.sceneIndexCount = 0;
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

void CreateForwardRenderer()
{
   if (Vulkan.forwardRendererReady)
   {
      return;
   }

   Assert(Vulkan.sceneReady, "Create scene before creating forward renderer");
   CreateColorResources();
   CreateDepthResources();
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
   if (Vulkan.msaaSamples != VK_SAMPLE_COUNT_1_BIT)
   {
      Assert(Vulkan.colorResourcesReady, "Create color resources before MSAA forward pipeline");
   }
   Assert(ShaderCacheDirectory[0] != '\0', "Shader cache directory is not defined");

   array<char, 512> vertexPath {};
   array<char, 512> fragmentPath {};

   const auto buildPath = [](const char *directory, const char *fileName, array<char, 512> &buffer)
   {
      int written = std::snprintf(buffer.data(), buffer.size(), "%s/%s", directory, fileName);
      Assert((written > 0) && (static_cast<size_t>(written) < buffer.size()), "Shader path truncated");
   };

   buildPath(ShaderCacheDirectory, ForwardVertexShaderName, vertexPath);
   buildPath(ShaderCacheDirectory, ForwardFragmentShaderName, fragmentPath);

   Vulkan.forwardVertexShader = CreateShader(vertexPath.data());
   Vulkan.forwardFragmentShader = CreateShader(fragmentPath.data());

   VkPushConstantRange pushConstant = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .offset = 0,
      .size = sizeof(ForwardPushConstants),
   };

   VkPipelineLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pSetLayouts = nullptr,
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

   VkVertexInputBindingDescription vertexBinding = {
      .binding = 0,
      .stride = static_cast<u32>(sizeof(Vertex)),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
   };

   std::array<VkVertexInputAttributeDescription, 3> vertexAttributes = {
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
   };

   VkPipelineVertexInputStateCreateInfo vertexInput = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
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

   Vulkan.forwardPipelineReady = true;
}

void DestroyForwardPipeline()
{
   if ((Vulkan.forwardPipeline == VK_NULL_HANDLE) &&
       (Vulkan.forwardPipelineLayout == VK_NULL_HANDLE) &&
       (Vulkan.forwardVertexShader == VK_NULL_HANDLE) &&
       (Vulkan.forwardFragmentShader == VK_NULL_HANDLE))
   {
      Vulkan.forwardPipelineReady = false;
      return;
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

   DestroyShader(Vulkan.forwardVertexShader);
   DestroyShader(Vulkan.forwardFragmentShader);

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
      Vulkan.frameResourcesReady = false;
      return;
   }

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

auto AcquireNextImage(u32 &imageIndex, u32 &frameIndex) -> VkResult
{
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before acquiring images");
   Assert(Vulkan.deviceReady, "Create the Vulkan device before acquiring images");
   Assert(Vulkan.frameResourcesReady, "Create frame resources before acquiring images");

   frameIndex = Vulkan.currentFrame;
   Assert(frameIndex < FrameOverlap, "Frame index out of range");

   FrameResources &frame = Vulkan.frames[frameIndex];
   Assert(frame.inFlightFence != VK_NULL_HANDLE, "Frame fence is not initialized");
   Assert(frame.imageAvailableSemaphore != VK_NULL_HANDLE, "Frame image-available semaphore is not initialized");

   VkResult waitResult = vkWaitForFences(Vulkan.device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
   Assert(waitResult == VK_SUCCESS, "Failed to wait for in-flight fence");

   VkResult resetResult = vkResetFences(Vulkan.device, 1, &frame.inFlightFence);
   Assert(resetResult == VK_SUCCESS, "Failed to reset in-flight fence");

   imageIndex = UINT32_MAX;
   VkResult acquireResult = vkAcquireNextImageKHR(
      Vulkan.device,
      Vulkan.swapchain,
      UINT64_MAX,
      frame.imageAvailableSemaphore,
      VK_NULL_HANDLE,
      &imageIndex);

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
         VkResult waitResult = vkWaitForFences(Vulkan.device, 1, &imageFence, VK_TRUE, UINT64_MAX);
         Assert(waitResult == VK_SUCCESS, "Failed to wait for image fence");
      }
      Vulkan.swapchainImageFences[imageIndex] = frame.inFlightFence;
   }

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

   VkExtent2D extent = Vulkan.swapchainExtent;
   Assert((extent.width > 0) && (extent.height > 0), "Swapchain extent is invalid");

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

   vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan.forwardPipeline);
   Assert(Vulkan.sceneVertexBuffer != VK_NULL_HANDLE, "Scene vertex buffer is not initialized");
   Assert(Vulkan.sceneIndexBuffer != VK_NULL_HANDLE, "Scene index buffer is not initialized");
   Assert(Vulkan.sceneIndexCount > 0, "Scene index count is zero");

   VkDeviceSize vertexOffset = 0;
   vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &Vulkan.sceneVertexBuffer, &vertexOffset);
   vkCmdBindIndexBuffer(frame.commandBuffer, Vulkan.sceneIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

   CameraParams camera = GetCameraParams();
   float nearPlane = 0.05f;
   float farPlane = 200.0f;
   float aspect = (extent.height > 0) ? (static_cast<float>(extent.width) / static_cast<float>(extent.height)) : 1.0f;
   if (aspect <= 0.0f)
   {
      aspect = 1.0f;
   }

   auto dot3 = [](const Vec3 &a, const Vec3 &b) -> float
   {
      return a.x*b.x + a.y*b.y + a.z*b.z;
   };
   auto setIdentity = [](float *matrix)
   {
      std::memset(matrix, 0, sizeof(float) * 16);
      matrix[0] = 1.0f;
      matrix[5] = 1.0f;
      matrix[10] = 1.0f;
      matrix[15] = 1.0f;
   };
   auto multiplyMat4 = [](const float *a, const float *b, float *result)
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

   float model[16] = {};
   setIdentity(model);

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

   ForwardPushConstants constants = {};
   float viewProj[16] = {};
   multiplyMat4(proj, view, viewProj);
   multiplyMat4(viewProj, model, constants.mvp);

   float pulse = 0.92f + 0.08f * std::sin(gradient.time * 0.75f);
   constants.tint[0] = pulse;
   constants.tint[1] = pulse;
   constants.tint[2] = pulse;
   constants.tint[3] = 1.0f;

   vkCmdPushConstants(
      frame.commandBuffer,
      Vulkan.forwardPipelineLayout,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      0,
      sizeof(ForwardPushConstants),
      &constants);

   vkCmdDrawIndexed(frame.commandBuffer, Vulkan.sceneIndexCount, 1, 0, 0, 0);

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

auto SubmitFrame(u32 frameIndex, u32 imageIndex) -> VkResult
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

   VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
   VkSemaphore waitSemaphores[] = {frame.imageAvailableSemaphore};
   VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};

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

   VkResult submitResult = vkQueueSubmit(Vulkan.graphicsQueue, 1, &submitInfo, frame.inFlightFence);
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

   VkResult presentResult = vkQueuePresentKHR(Vulkan.presentQueue, &presentInfo);

   if ((presentResult == VK_ERROR_OUT_OF_DATE_KHR) || (presentResult == VK_SUBOPTIMAL_KHR))
   {
      return presentResult;
   }

   Assert(presentResult == VK_SUCCESS, "Failed to present swapchain image");

   Vulkan.currentFrame = (Vulkan.currentFrame + 1) % FrameOverlap;
   return VK_SUCCESS;
}
