#include <callandor.h>
#include <config.h>
#include <utils.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory_resource>
#include <ostream>
#include <vector>

using namespace std;

#ifndef SHADER_CACHE_DIRECTORY
#define SHADER_CACHE_DIRECTORY ""
#endif

static constexpr const char *ShaderCacheDirectory = SHADER_CACHE_DIRECTORY;
static constexpr const char *FullscreenVertexShaderName = "fullscreen_triangle.vert.spv";
static constexpr const char *FullscreenFragmentShaderName = "fullscreen_triangle.frag.spv";
static constexpr const char *PathTracerComputeShaderName = "pathtracer.comp.spv";
static constexpr VkFormat PathTracerImageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
static constexpr u32 DefaultSphereCount = 512;

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

   VkShaderModule fullscreenVertexShader;
   VkShaderModule fullscreenFragmentShader;
   VkPipelineLayout fullscreenPipelineLayout;
   VkPipeline fullscreenPipeline;
   VkShaderModule pathTracerComputeShader;
   VkPipelineLayout pathTracerPipelineLayout;
   VkPipeline pathTracerPipeline;
   VkImage pathTracerImage;
   VkDeviceMemory pathTracerImageMemory;
   VkImageView pathTracerImageView;
   VkSampler pathTracerSampler;
   VkDescriptorSetLayout pathTracerDescriptorSetLayout;
   VkDescriptorPool pathTracerDescriptorPool;
   VkDescriptorSet pathTracerDescriptorSet;
   VkImageLayout pathTracerImageLayout;
   VkBuffer spheresHotBuffer;
   VkDeviceMemory spheresHotMemory;
   VkBuffer sphereMaterialIdsBuffer;
   VkDeviceMemory sphereMaterialIdsMemory;
   VkBuffer materialAlbedoRoughnessBuffer;
   VkDeviceMemory materialAlbedoRoughnessMemory;
   VkBuffer materialEmissiveBuffer;
   VkDeviceMemory materialEmissiveMemory;
   u32 sphereCount;
   u32 materialCount;
   u32 accumFrame;

   bool instanceReady;
   bool validationLayersEnabled;
   bool debugMessengerReady;
   bool physicalDeviceReady;
   bool deviceReady;
   bool swapchainReady;
   bool swapchainImageViewsReady;
   bool frameResourcesReady;
   bool pathTracerImageReady;
   bool pathTracerDescriptorsReady;
   bool fullscreenPipelineReady;
   bool pathTracerPipelineReady;
   bool pathTracerSceneReady;

} Vulkan;

static SphereQuantConfig SceneQuantization = {};

static auto FindMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) -> u32
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

static void DestroyBuffer(VkBuffer &buffer, VkDeviceMemory &memory)
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      buffer = VK_NULL_HANDLE;
      memory = VK_NULL_HANDLE;
      return;
   }

   if (buffer != VK_NULL_HANDLE)
   {
      vkDestroyBuffer(Vulkan.device, buffer, nullptr);
      buffer = VK_NULL_HANDLE;
   }

   if (memory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, memory, nullptr);
      memory = VK_NULL_HANDLE;
   }
}

static void CreateStorageBuffer(const void *data, VkDeviceSize size, VkBuffer &buffer, VkDeviceMemory &memory)
{
   Assert(size > 0, "Storage buffer size must be greater than zero");
   Assert(Vulkan.device != VK_NULL_HANDLE, "Vulkan device must exist before allocating buffers");

   VkBufferCreateInfo bufferInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };

   VkResult createResult = vkCreateBuffer(Vulkan.device, &bufferInfo, nullptr, &buffer);
   Assert(createResult == VK_SUCCESS, "Failed to create storage buffer");

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
   Assert(allocResult == VK_SUCCESS, "Failed to allocate storage buffer memory");

   VkResult bindResult = vkBindBufferMemory(Vulkan.device, buffer, memory, 0);
   Assert(bindResult == VK_SUCCESS, "Failed to bind storage buffer memory");

   void *mapped = nullptr;
   VkResult mapResult = vkMapMemory(Vulkan.device, memory, 0, size, 0, &mapped);
   Assert(mapResult == VK_SUCCESS, "Failed to map storage buffer memory");

   std::memcpy(mapped, data, static_cast<size_t>(size));
   vkUnmapMemory(Vulkan.device, memory);
}

static auto Clamp01(float value) -> float
{
   if (value < 0.0f)
   {
      return 0.0f;
   }
   if (value > 1.0f)
   {
      return 1.0f;
   }
   return value;
}

static auto PackUnorm2x16(float a, float b) -> u32
{
   float clampedA = Clamp01(a);
   float clampedB = Clamp01(b);
   u32 A = static_cast<u32>(std::round(clampedA * 65535.0f));
   u32 B = static_cast<u32>(std::round(clampedB * 65535.0f));
   return (B << 16) | A;
}

static auto RandomUnorm(u32 &state) -> float
{
   state = state * 1664525u + 1013904223u;
   u32 mantissa = (state >> 9u) | 0x3f800000u;
   float result = std::bit_cast<float>(mantissa) - 1.0f;
   return Clamp01(result);
}

static void UpdateSceneDescriptorBindings();
static void DestroyPathTracerSceneBuffers();
static void DestroyPathTracerDescriptorLayout();

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
   Vulkan.accumFrame = 0;
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
   CreatePathTracerImage();
   CreatePathTracerDescriptors();
   CreatePathTracerPipeline();
   BuildSpheres();
   CreateFullscreenPipeline();
   CreateFrameResources();
}

void DestroyVulkan()
{
   if (Vulkan.device != VK_NULL_HANDLE)
   {
      vkDeviceWaitIdle(Vulkan.device);
   }

   DestroyFrameResources();
   DestroyFullscreenPipeline();
   DestroyPathTracerPipeline();
   DestroyPathTracerSceneBuffers();
   DestroyPathTracerDescriptors();
   DestroyPathTracerDescriptorLayout();
   DestroyPathTracerImage();
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

      LogInfo("[vulkan] Selected physical device: %s", properties.deviceName);
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

void CreatePathTracerImage()
{
   if (Vulkan.pathTracerImageReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before the path tracer image");
   Assert(Vulkan.swapchainReady, "Create the swapchain before the path tracer image");
   Assert(Vulkan.physicalDeviceReady, "Select a physical device before creating the path tracer image");

   VkExtent2D extent = Vulkan.swapchainExtent;
   Assert((extent.width > 0) && (extent.height > 0), "Swapchain extent is invalid for the path tracer image");

   VkFormatProperties formatProperties = {};
   vkGetPhysicalDeviceFormatProperties(Vulkan.physicalDevice, PathTracerImageFormat, &formatProperties);
   VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
   Assert((formatProperties.optimalTilingFeatures & requiredFeatures) == requiredFeatures,
          "Path tracer image format does not support required sampled/storage usage");

   VkImageCreateInfo imageInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = PathTracerImageFormat,
      .extent = {extent.width, extent.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_STORAGE_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   VkResult imageResult = vkCreateImage(Vulkan.device, &imageInfo, nullptr, &Vulkan.pathTracerImage);
   Assert(imageResult == VK_SUCCESS, "Failed to create path tracer image");

   VkMemoryRequirements requirements = {};
   vkGetImageMemoryRequirements(Vulkan.device, Vulkan.pathTracerImage, &requirements);

   VkMemoryAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = requirements.size,
      .memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };

   VkResult allocResult = vkAllocateMemory(Vulkan.device, &allocInfo, nullptr, &Vulkan.pathTracerImageMemory);
   Assert(allocResult == VK_SUCCESS, "Failed to allocate memory for the path tracer image");

   VkResult bindResult = vkBindImageMemory(Vulkan.device, Vulkan.pathTracerImage, Vulkan.pathTracerImageMemory, 0);
   Assert(bindResult == VK_SUCCESS, "Failed to bind memory to the path tracer image");

   VkImageViewCreateInfo viewInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = Vulkan.pathTracerImage,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = PathTracerImageFormat,
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

   VkResult viewResult = vkCreateImageView(Vulkan.device, &viewInfo, nullptr, &Vulkan.pathTracerImageView);
   Assert(viewResult == VK_SUCCESS, "Failed to create path tracer image view");

   Vulkan.pathTracerImageReady = true;
   Vulkan.pathTracerImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void DestroyPathTracerImage()
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.pathTracerImageReady = false;
      Vulkan.pathTracerImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      Vulkan.pathTracerImage = VK_NULL_HANDLE;
      Vulkan.pathTracerImageMemory = VK_NULL_HANDLE;
      Vulkan.pathTracerImageView = VK_NULL_HANDLE;
      return;
   }

   if (Vulkan.pathTracerImageView != VK_NULL_HANDLE)
   {
      vkDestroyImageView(Vulkan.device, Vulkan.pathTracerImageView, nullptr);
      Vulkan.pathTracerImageView = VK_NULL_HANDLE;
   }

   if (Vulkan.pathTracerImage != VK_NULL_HANDLE)
   {
      vkDestroyImage(Vulkan.device, Vulkan.pathTracerImage, nullptr);
      Vulkan.pathTracerImage = VK_NULL_HANDLE;
   }

   if (Vulkan.pathTracerImageMemory != VK_NULL_HANDLE)
   {
      vkFreeMemory(Vulkan.device, Vulkan.pathTracerImageMemory, nullptr);
      Vulkan.pathTracerImageMemory = VK_NULL_HANDLE;
   }

   Vulkan.pathTracerImageReady = false;
   Vulkan.pathTracerImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void CreatePathTracerDescriptors()
{
   if (Vulkan.pathTracerDescriptorsReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before descriptor sets");
   Assert(Vulkan.pathTracerImageReady, "Create the path tracer image before descriptors");

   if (Vulkan.pathTracerDescriptorSetLayout == VK_NULL_HANDLE)
   {
      VkDescriptorSetLayoutBinding storageBinding = {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      };

      VkDescriptorSetLayoutBinding samplerBinding = {
         .binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      };

      VkDescriptorSetLayoutBinding sphereBinding = {
         .binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      };

      VkDescriptorSetLayoutBinding matIdBinding = {
         .binding = 3,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      };

      VkDescriptorSetLayoutBinding albedoBinding = {
         .binding = 4,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      };

      VkDescriptorSetLayoutBinding emissiveBinding = {
         .binding = 5,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      };

      array<VkDescriptorSetLayoutBinding, 6> bindings = {
         storageBinding,
         samplerBinding,
         sphereBinding,
         matIdBinding,
         albedoBinding,
         emissiveBinding,
      };

      VkDescriptorSetLayoutCreateInfo layoutInfo = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = static_cast<uint32_t>(bindings.size()),
         .pBindings = bindings.data(),
      };

      VkResult layoutResult = vkCreateDescriptorSetLayout(
         Vulkan.device,
         &layoutInfo,
         nullptr,
         &Vulkan.pathTracerDescriptorSetLayout);
      Assert(layoutResult == VK_SUCCESS, "Failed to create path tracer descriptor set layout");
   }

   VkDescriptorPoolSize poolSizes[] = {
      {
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .descriptorCount = 1,
      },
      {
         .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
      },
      {
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 4,
      },
   };

   VkDescriptorPoolCreateInfo poolInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0])),
      .pPoolSizes = poolSizes,
   };

   VkResult poolResult = vkCreateDescriptorPool(Vulkan.device, &poolInfo, nullptr, &Vulkan.pathTracerDescriptorPool);
   Assert(poolResult == VK_SUCCESS, "Failed to create path tracer descriptor pool");

   VkSamplerCreateInfo samplerInfo = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .minLod = 0.0f,
      .maxLod = 0.0f,
      .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
      .unnormalizedCoordinates = VK_FALSE,
   };

   VkResult samplerResult = vkCreateSampler(Vulkan.device, &samplerInfo, nullptr, &Vulkan.pathTracerSampler);
   Assert(samplerResult == VK_SUCCESS, "Failed to create path tracer sampler");

   VkDescriptorSetAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = Vulkan.pathTracerDescriptorPool,
      .descriptorSetCount = 1,
      .pSetLayouts = &Vulkan.pathTracerDescriptorSetLayout,
   };

   VkResult allocResult = vkAllocateDescriptorSets(
      Vulkan.device,
      &allocInfo,
      &Vulkan.pathTracerDescriptorSet);
   Assert(allocResult == VK_SUCCESS, "Failed to allocate path tracer descriptor set");

   VkDescriptorImageInfo storageInfo = {
      .sampler = VK_NULL_HANDLE,
      .imageView = Vulkan.pathTracerImageView,
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };

   VkDescriptorImageInfo sampledInfo = {
      .sampler = Vulkan.pathTracerSampler,
      .imageView = Vulkan.pathTracerImageView,
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };

   VkWriteDescriptorSet writes[] = {
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.pathTracerDescriptorSet,
         .dstBinding = 0,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .pImageInfo = &storageInfo,
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.pathTracerDescriptorSet,
         .dstBinding = 1,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = &sampledInfo,
      },
   };

   vkUpdateDescriptorSets(
      Vulkan.device,
      static_cast<uint32_t>(sizeof(writes) / sizeof(writes[0])),
      writes,
      0,
      nullptr);

   Vulkan.pathTracerDescriptorsReady = true;
   UpdateSceneDescriptorBindings();
}

void DestroyPathTracerDescriptors()
{
   if (Vulkan.device == VK_NULL_HANDLE)
   {
      Vulkan.pathTracerDescriptorSet = VK_NULL_HANDLE;
      Vulkan.pathTracerDescriptorSetLayout = VK_NULL_HANDLE;
      Vulkan.pathTracerDescriptorPool = VK_NULL_HANDLE;
      Vulkan.pathTracerSampler = VK_NULL_HANDLE;
      Vulkan.pathTracerDescriptorsReady = false;
      return;
   }

   if (Vulkan.pathTracerDescriptorPool != VK_NULL_HANDLE)
   {
      vkDestroyDescriptorPool(Vulkan.device, Vulkan.pathTracerDescriptorPool, nullptr);
      Vulkan.pathTracerDescriptorPool = VK_NULL_HANDLE;
   }

   if (Vulkan.pathTracerSampler != VK_NULL_HANDLE)
   {
      vkDestroySampler(Vulkan.device, Vulkan.pathTracerSampler, nullptr);
      Vulkan.pathTracerSampler = VK_NULL_HANDLE;
   }

   Vulkan.pathTracerDescriptorSet = VK_NULL_HANDLE;
   Vulkan.pathTracerDescriptorsReady = false;
   Vulkan.pathTracerSceneReady = false;
}

static void DestroyPathTracerDescriptorLayout()
{
   if ((Vulkan.device == VK_NULL_HANDLE) || (Vulkan.pathTracerDescriptorSetLayout == VK_NULL_HANDLE))
   {
      Vulkan.pathTracerDescriptorSetLayout = VK_NULL_HANDLE;
      return;
   }

   vkDestroyDescriptorSetLayout(Vulkan.device, Vulkan.pathTracerDescriptorSetLayout, nullptr);
   Vulkan.pathTracerDescriptorSetLayout = VK_NULL_HANDLE;
}

static void DestroyPathTracerSceneBuffers()
{
   DestroyBuffer(Vulkan.spheresHotBuffer, Vulkan.spheresHotMemory);
   DestroyBuffer(Vulkan.sphereMaterialIdsBuffer, Vulkan.sphereMaterialIdsMemory);
   DestroyBuffer(Vulkan.materialAlbedoRoughnessBuffer, Vulkan.materialAlbedoRoughnessMemory);
   DestroyBuffer(Vulkan.materialEmissiveBuffer, Vulkan.materialEmissiveMemory);
   Vulkan.sphereCount = 0;
   Vulkan.materialCount = 0;
   Vulkan.pathTracerSceneReady = false;
}

static void UpdateSceneDescriptorBindings()
{
   if (!Vulkan.pathTracerDescriptorsReady)
   {
      return;
   }

    if (Vulkan.pathTracerDescriptorSet == VK_NULL_HANDLE)
    {
       return;
    }

   if ((Vulkan.spheresHotBuffer == VK_NULL_HANDLE) ||
       (Vulkan.sphereMaterialIdsBuffer == VK_NULL_HANDLE) ||
       (Vulkan.materialAlbedoRoughnessBuffer == VK_NULL_HANDLE) ||
       (Vulkan.materialEmissiveBuffer == VK_NULL_HANDLE))
   {
      return;
   }

   VkDeviceSize spheresSize = static_cast<VkDeviceSize>(Vulkan.sphereCount) * sizeof(u32) * 2;
   VkDeviceSize materialIdsSize = static_cast<VkDeviceSize>(Vulkan.sphereCount) * sizeof(u32);
   VkDeviceSize albedoSize = static_cast<VkDeviceSize>(Vulkan.materialCount) * sizeof(float) * 4;
   VkDeviceSize emissiveSize = albedoSize;

   VkDescriptorBufferInfo sphereInfo = {
      .buffer = Vulkan.spheresHotBuffer,
      .offset = 0,
      .range = spheresSize,
   };

   VkDescriptorBufferInfo matIdInfo = {
      .buffer = Vulkan.sphereMaterialIdsBuffer,
      .offset = 0,
      .range = materialIdsSize,
   };

   VkDescriptorBufferInfo albedoInfo = {
      .buffer = Vulkan.materialAlbedoRoughnessBuffer,
      .offset = 0,
      .range = albedoSize,
   };

   VkDescriptorBufferInfo emissiveInfo = {
      .buffer = Vulkan.materialEmissiveBuffer,
      .offset = 0,
      .range = emissiveSize,
   };

   VkWriteDescriptorSet writes[] = {
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.pathTracerDescriptorSet,
         .dstBinding = 2,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &sphereInfo,
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.pathTracerDescriptorSet,
         .dstBinding = 3,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &matIdInfo,
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.pathTracerDescriptorSet,
         .dstBinding = 4,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &albedoInfo,
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = Vulkan.pathTracerDescriptorSet,
         .dstBinding = 5,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &emissiveInfo,
      },
   };

   vkUpdateDescriptorSets(
      Vulkan.device,
      static_cast<uint32_t>(sizeof(writes) / sizeof(writes[0])),
      writes,
      0,
      nullptr);

   Vulkan.pathTracerSceneReady = true;
}

void BuildSpheres()
{
   Assert(Vulkan.deviceReady, "Create the Vulkan device before building spheres");

   DestroyPathTracerSceneBuffers();

   constexpr u32 MaterialCount = 8;
   constexpr float MinRadius = 0.15f;
   constexpr float PlacementSpacing = 0.01f;
   constexpr u32 MaxPlacementAttempts = 128;
   static constexpr std::array<std::array<float, 4>, MaterialCount> MaterialAlbedoRoughness = {{
      {0.8f, 0.3f, 0.3f, 0.9f},   // diffuse red
      {0.3f, 0.8f, 0.3f, 0.85f},  // diffuse green
      {0.3f, 0.3f, 0.8f, 0.8f},   // diffuse blue
      {0.9f, 0.8f, 0.6f, 0.9f},   // diffuse sand
      {1.0f, 0.85f, 0.4f, 0.12f}, // metal gold
      {0.95f, 0.95f, 0.95f, 0.08f}, // metal chrome
      {0.95f, 0.55f, 0.3f, 0.15f}, // metal copper
      {1.0f, 1.0f, 1.0f, 0.2f},   // emissive white
   }};

   static constexpr std::array<std::array<float, 4>, MaterialCount> MaterialEmissive = {{
      {0.0f, 0.0f, 0.0f, 0.0f},  // diffuse
      {0.0f, 0.0f, 0.0f, 0.0f},  // diffuse
      {0.0f, 0.0f, 0.0f, 0.0f},  // diffuse
      {0.0f, 0.0f, 0.0f, 0.0f},  // diffuse
      {0.0f, 0.0f, 0.0f, 1.0f},  // metal
      {0.0f, 0.0f, 0.0f, 1.0f},  // metal
      {0.0f, 0.0f, 0.0f, 1.0f},  // metal
      {6.5f, 6.0f, 5.5f, 0.0f},  // emissive
   }};

   const u32 desiredSphereCount = DefaultSphereCount;
   std::vector<u32> sphereWords;
   sphereWords.reserve(static_cast<size_t>(desiredSphereCount) * 2);
   std::vector<u32> materialIds;
   materialIds.reserve(desiredSphereCount);
   std::vector<Vec3> centers;
   centers.reserve(desiredSphereCount);
   std::vector<float> radii;
   radii.reserve(desiredSphereCount);

   SceneQuantization.origin = {-32.0f, 0.0f, -32.0f};
   SceneQuantization.pad0 = 0.0f;
   SceneQuantization.scale = {64.0f, 24.0f, 64.0f};
   SceneQuantization.scaleMax = 2.5f;

   const auto distanceSquared = [](const Vec3 &a, const Vec3 &b) -> float
   {
      float dx = a.x - b.x;
      float dy = a.y - b.y;
      float dz = a.z - b.z;
      return dx*dx + dy*dy + dz*dz;
   };
   const auto randomIndex = [](const auto &indices, u32 &state) -> u32
   {
      float selection = RandomUnorm(state);
      size_t pick = static_cast<size_t>(selection * static_cast<float>(indices.size()));
      if (pick >= indices.size())
      {
         pick = indices.size() - 1;
      }
      return indices[pick];
   };
   static constexpr std::array<u32, 4> DiffuseIndices = {0, 1, 2, 3};
   static constexpr std::array<u32, 3> MetallicIndices = {4, 5, 6};
   static constexpr std::array<u32, 1> EmissiveIndices = {7};

   u32 rng = 0x9e3779b9u;
   for (u32 index = 0; index < desiredSphereCount; ++index)
   {
      bool placed = false;
      Vec3 center = {};
      float radius = MinRadius;

      for (u32 attempt = 0; attempt < MaxPlacementAttempts; ++attempt)
      {
         radius = MinRadius + RandomUnorm(rng) * (SceneQuantization.scaleMax - MinRadius);

         float px = SceneQuantization.origin.x + RandomUnorm(rng) * SceneQuantization.scale.x;
         float pz = SceneQuantization.origin.z + RandomUnorm(rng) * SceneQuantization.scale.z;
         float py = SceneQuantization.origin.y + radius;

         Vec3 candidate = {px, py, pz};
         bool overlaps = false;

         for (size_t existing = 0; existing < centers.size(); ++existing)
         {
            float needed = radius + radii[existing] + PlacementSpacing;
            if (distanceSquared(candidate, centers[existing]) < (needed * needed))
            {
               overlaps = true;
               break;
            }
         }

         if (!overlaps)
         {
            center = candidate;
            placed = true;
            break;
         }
      }

      if (!placed)
      {
         LogWarn("[scene] Failed to place sphere %u without overlaps (skipping)", index);
         continue;
      }

      Vec3 normalized = {
         Clamp01((center.x - SceneQuantization.origin.x) / SceneQuantization.scale.x),
         Clamp01((center.y - SceneQuantization.origin.y) / SceneQuantization.scale.y),
         Clamp01((center.z - SceneQuantization.origin.z) / SceneQuantization.scale.z),
      };

      float encodedRadius = std::sqrt(Clamp01(radius / SceneQuantization.scaleMax));

      sphereWords.push_back(PackUnorm2x16(normalized.x, normalized.y));
      sphereWords.push_back(PackUnorm2x16(normalized.z, encodedRadius));

      float materialRoll = RandomUnorm(rng);
      u32 materialIndex = 0;
      if (materialRoll < 0.7f)
      {
         materialIndex = randomIndex(DiffuseIndices, rng);
      }
      else if (materialRoll < 0.95f)
      {
         materialIndex = randomIndex(MetallicIndices, rng);
      }
      else
      {
         materialIndex = randomIndex(EmissiveIndices, rng);
      }
      materialIds.push_back(materialIndex);
      centers.push_back(center);
      radii.push_back(radius);
   }

   if (sphereWords.empty())
   {
      LogError("[scene] Unable to place any non-overlapping spheres");
      Assert(false, "Sphere placement failed");
   }

   VkDeviceSize hotSize = static_cast<VkDeviceSize>(sphereWords.size()) * sizeof(u32);
   VkDeviceSize materialIdsSize = static_cast<VkDeviceSize>(materialIds.size()) * sizeof(u32);
   VkDeviceSize albedoSize = static_cast<VkDeviceSize>(MaterialAlbedoRoughness.size()) * sizeof(std::array<float, 4>);
   VkDeviceSize emissiveSize = static_cast<VkDeviceSize>(MaterialEmissive.size()) * sizeof(std::array<float, 4>);

   CreateStorageBuffer(sphereWords.data(), hotSize, Vulkan.spheresHotBuffer, Vulkan.spheresHotMemory);
   CreateStorageBuffer(materialIds.data(), materialIdsSize, Vulkan.sphereMaterialIdsBuffer, Vulkan.sphereMaterialIdsMemory);
   CreateStorageBuffer(MaterialAlbedoRoughness.data(), albedoSize, Vulkan.materialAlbedoRoughnessBuffer, Vulkan.materialAlbedoRoughnessMemory);
   CreateStorageBuffer(MaterialEmissive.data(), emissiveSize, Vulkan.materialEmissiveBuffer, Vulkan.materialEmissiveMemory);

   Vulkan.sphereCount = static_cast<u32>(materialIds.size());
   Vulkan.materialCount = MaterialCount;
   Vulkan.pathTracerSceneReady = true;

   UpdateSceneDescriptorBindings();
   ResetCameraAccum();
}

auto GetSphereQuantConfig() -> SphereQuantConfig
{
   return SceneQuantization;
}

void CreateFullscreenPipeline()
{
   if (Vulkan.fullscreenPipelineReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before pipelines");
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before pipelines");
   Assert(Vulkan.pathTracerDescriptorSetLayout != VK_NULL_HANDLE, "Create path tracer descriptors before pipelines");
   Assert(ShaderCacheDirectory[0] != '\0', "Shader cache directory is not defined");

   array<char, 512> vertexPath {};
   array<char, 512> fragmentPath {};

   const auto buildPath = [](const char *directory, const char *fileName, array<char, 512> &buffer)
   {
      int written = std::snprintf(buffer.data(), buffer.size(), "%s/%s", directory, fileName);
      Assert((written > 0) && (static_cast<size_t>(written) < buffer.size()), "Shader path truncated");
   };

   buildPath(ShaderCacheDirectory, FullscreenVertexShaderName, vertexPath);
   buildPath(ShaderCacheDirectory, FullscreenFragmentShaderName, fragmentPath);

   Vulkan.fullscreenVertexShader = CreateShader(vertexPath.data());
   Vulkan.fullscreenFragmentShader = CreateShader(fragmentPath.data());

   VkPushConstantRange pushConstant = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .offset = 0,
      .size = sizeof(GradientParams),
   };

   VkDescriptorSetLayout descriptorLayouts[] = {Vulkan.pathTracerDescriptorSetLayout};

   VkPipelineLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = static_cast<uint32_t>(sizeof(descriptorLayouts) / sizeof(descriptorLayouts[0])),
      .pSetLayouts = descriptorLayouts,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstant,
   };

   VkResult layoutResult = vkCreatePipelineLayout(Vulkan.device, &layoutInfo, nullptr, &Vulkan.fullscreenPipelineLayout);
   Assert(layoutResult == VK_SUCCESS, "Failed to create fullscreen pipeline layout");

   VkPipelineShaderStageCreateInfo shaderStages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = Vulkan.fullscreenVertexShader,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = Vulkan.fullscreenFragmentShader,
         .pName = "main",
      },
   };

   VkPipelineVertexInputStateCreateInfo vertexInput = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .pVertexBindingDescriptions = nullptr,
      .vertexAttributeDescriptionCount = 0,
      .pVertexAttributeDescriptions = nullptr,
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
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
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
      .pDepthStencilState = nullptr,
      .pColorBlendState = &colorBlending,
      .pDynamicState = &dynamicState,
      .layout = Vulkan.fullscreenPipelineLayout,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
   };

   VkResult pipelineResult = vkCreateGraphicsPipelines(
      Vulkan.device,
      VK_NULL_HANDLE,
      1,
      &pipelineInfo,
      nullptr,
      &Vulkan.fullscreenPipeline);
   Assert(pipelineResult == VK_SUCCESS, "Failed to create fullscreen graphics pipeline");

   Vulkan.fullscreenPipelineReady = true;
}

void DestroyFullscreenPipeline()
{
   if ((Vulkan.fullscreenPipeline == VK_NULL_HANDLE) &&
       (Vulkan.fullscreenPipelineLayout == VK_NULL_HANDLE) &&
       (Vulkan.fullscreenVertexShader == VK_NULL_HANDLE) &&
       (Vulkan.fullscreenFragmentShader == VK_NULL_HANDLE))
   {
      Vulkan.fullscreenPipelineReady = false;
      return;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.fullscreenPipeline != VK_NULL_HANDLE))
   {
      vkDestroyPipeline(Vulkan.device, Vulkan.fullscreenPipeline, nullptr);
      Vulkan.fullscreenPipeline = VK_NULL_HANDLE;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.fullscreenPipelineLayout != VK_NULL_HANDLE))
   {
      vkDestroyPipelineLayout(Vulkan.device, Vulkan.fullscreenPipelineLayout, nullptr);
      Vulkan.fullscreenPipelineLayout = VK_NULL_HANDLE;
   }

   DestroyShader(Vulkan.fullscreenVertexShader);
   DestroyShader(Vulkan.fullscreenFragmentShader);

   Vulkan.fullscreenPipelineReady = false;
}

void CreatePathTracerPipeline()
{
   if (Vulkan.pathTracerPipelineReady)
   {
      return;
   }

   Assert(Vulkan.deviceReady, "Create the Vulkan device before compute pipelines");
   Assert(Vulkan.pathTracerDescriptorSetLayout != VK_NULL_HANDLE, "Descriptor set layout must exist before compute pipeline");
   Assert(ShaderCacheDirectory[0] != '\0', "Shader cache directory is not defined");

   VkPhysicalDeviceProperties properties = {};
   vkGetPhysicalDeviceProperties(Vulkan.physicalDevice, &properties);
   Assert(sizeof(PathParams) <= properties.limits.maxPushConstantsSize, "Path tracer push constants exceed device limit");

   array<char, 512> computePath {};
   int written = std::snprintf(computePath.data(), computePath.size(), "%s/%s", ShaderCacheDirectory, PathTracerComputeShaderName);
   Assert((written > 0) && (static_cast<size_t>(written) < computePath.size()), "Compute shader path truncated");

   Vulkan.pathTracerComputeShader = CreateShader(computePath.data());

   VkPushConstantRange pushConstant = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = static_cast<uint32_t>(sizeof(PathParams)),
   };

   VkPipelineLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &Vulkan.pathTracerDescriptorSetLayout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstant,
   };

   VkResult layoutResult = vkCreatePipelineLayout(Vulkan.device, &layoutInfo, nullptr, &Vulkan.pathTracerPipelineLayout);
   Assert(layoutResult == VK_SUCCESS, "Failed to create path tracer pipeline layout");

   VkPipelineShaderStageCreateInfo stageInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = Vulkan.pathTracerComputeShader,
      .pName = "main",
   };

   VkComputePipelineCreateInfo pipelineInfo = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stageInfo,
      .layout = Vulkan.pathTracerPipelineLayout,
   };

   VkResult pipelineResult = vkCreateComputePipelines(
      Vulkan.device,
      VK_NULL_HANDLE,
      1,
      &pipelineInfo,
      nullptr,
      &Vulkan.pathTracerPipeline);
   Assert(pipelineResult == VK_SUCCESS, "Failed to create path tracer compute pipeline");

   Vulkan.pathTracerPipelineReady = true;
}

void DestroyPathTracerPipeline()
{
   if ((Vulkan.pathTracerPipeline == VK_NULL_HANDLE) &&
       (Vulkan.pathTracerPipelineLayout == VK_NULL_HANDLE) &&
       (Vulkan.pathTracerComputeShader == VK_NULL_HANDLE))
   {
      Vulkan.pathTracerPipelineReady = false;
      return;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.pathTracerPipeline != VK_NULL_HANDLE))
   {
      vkDestroyPipeline(Vulkan.device, Vulkan.pathTracerPipeline, nullptr);
      Vulkan.pathTracerPipeline = VK_NULL_HANDLE;
   }

   if ((Vulkan.device != VK_NULL_HANDLE) && (Vulkan.pathTracerPipelineLayout != VK_NULL_HANDLE))
   {
      vkDestroyPipelineLayout(Vulkan.device, Vulkan.pathTracerPipelineLayout, nullptr);
      Vulkan.pathTracerPipelineLayout = VK_NULL_HANDLE;
   }

   DestroyShader(Vulkan.pathTracerComputeShader);
   Vulkan.pathTracerPipelineReady = false;
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
   DestroyFullscreenPipeline();
   DestroyPathTracerDescriptors();
   DestroyPathTracerImage();
   DestroySwapchainImageViews();
   Vulkan.swapchainReady = false;
   Vulkan.swapchainImageViewsReady = false;

   CreateSwapchain();
   CreateSwapchainImageViews();
   CreatePathTracerImage();
   CreatePathTracerDescriptors();
   UpdateSceneDescriptorBindings();
   CreateFullscreenPipeline();
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

auto RecordCommandBuffer(u32 frameIndex, u32 imageIndex, const GradientParams &gradient) -> VkResult
{
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before recording commands");
   Assert(Vulkan.swapchainImageViewsReady, "Create swapchain image views before recording commands");
   Assert(Vulkan.frameResourcesReady, "Frame resources must exist before recording commands");
   Assert(Vulkan.fullscreenPipelineReady, "Fullscreen pipeline must be ready before recording commands");
   Assert(Vulkan.pathTracerImageReady, "Path tracer image must exist before recording commands");
   Assert(Vulkan.pathTracerDescriptorsReady, "Path tracer descriptors must exist before recording commands");
   Assert(Vulkan.pathTracerPipelineReady, "Path tracer compute pipeline must exist before recording commands");
   Assert(Vulkan.pathTracerSceneReady, "Path tracer scene buffers must exist before recording commands");
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

   VkImageSubresourceRange pathTracerSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
   };

   VkImageLayout desiredPathTracerLayout = VK_IMAGE_LAYOUT_GENERAL;
   if (Vulkan.pathTracerImageLayout != desiredPathTracerLayout)
   {
      VkImageMemoryBarrier initializePathTracer = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = 0,
         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout = Vulkan.pathTracerImageLayout,
         .newLayout = desiredPathTracerLayout,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = Vulkan.pathTracerImage,
         .subresourceRange = pathTracerSubresource,
      };

      vkCmdPipelineBarrier(
         frame.commandBuffer,
         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT,
         0,
         0,
         nullptr,
         0,
         nullptr,
         1,
         &initializePathTracer);
   }

   VkExtent2D extent = Vulkan.swapchainExtent;
   float safeWidth = (extent.width > 0) ? static_cast<float>(extent.width) : 1.0f;
   float safeHeight = (extent.height > 0) ? static_cast<float>(extent.height) : 1.0f;

   vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Vulkan.pathTracerPipeline);
   vkCmdBindDescriptorSets(
      frame.commandBuffer,
      VK_PIPELINE_BIND_POINT_COMPUTE,
      Vulkan.pathTracerPipelineLayout,
      0,
      1,
      &Vulkan.pathTracerDescriptorSet,
      0,
      nullptr);

   PathParams params = {};
   params.resolution = {safeWidth, safeHeight};
   params.time = gradient.time;
   params.frameIndex = Vulkan.accumFrame;
   params.camera = GetCameraParams();
   params.quant = GetSphereQuantConfig();
   params.sphereCount = Vulkan.sphereCount;
   params.padA = 0;
   params.padB = 0;
   params.padC = 0;

   vkCmdPushConstants(
      frame.commandBuffer,
      Vulkan.pathTracerPipelineLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0,
      sizeof(PathParams),
      &params);

   uint32_t groupX = (extent.width + 7u) / 8u;
   uint32_t groupY = (extent.height + 7u) / 8u;
   groupX = (groupX > 0) ? groupX : 1u;
   groupY = (groupY > 0) ? groupY : 1u;
   vkCmdDispatch(frame.commandBuffer, groupX, groupY, 1);

   VkImageMemoryBarrier storageToSample = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = desiredPathTracerLayout,
      .newLayout = desiredPathTracerLayout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = Vulkan.pathTracerImage,
      .subresourceRange = pathTracerSubresource,
   };

   vkCmdPipelineBarrier(
      frame.commandBuffer,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &storageToSample);

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
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &barrierToAttachment);

   VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};

   VkRenderingAttachmentInfo colorAttachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = imageView,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.color = clearColor},
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

   vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan.fullscreenPipeline);
   vkCmdBindDescriptorSets(
      frame.commandBuffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      Vulkan.fullscreenPipelineLayout,
      0,
      1,
      &Vulkan.pathTracerDescriptorSet,
      0,
      nullptr);

   vkCmdPushConstants(
      frame.commandBuffer,
      Vulkan.fullscreenPipelineLayout,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      0,
      sizeof(GradientParams),
      &gradient);

   vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);

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
      Vulkan.pathTracerImageLayout = desiredPathTracerLayout;
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

   if (Vulkan.accumFrame < (std::numeric_limits<u32>::max() - 1u))
   {
      Vulkan.accumFrame += 1u;
   }

   Vulkan.currentFrame = (Vulkan.currentFrame + 1) % FrameOverlap;
   return VK_SUCCESS;
}
