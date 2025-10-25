#include <callandor.h>
#include <config.h>
#include <utils.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory_resource>
#include <ostream>
#include <vector>

using namespace std;

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

   bool instanceReady;
   bool validationLayersEnabled;
   bool debugMessengerReady;
   bool physicalDeviceReady;
   bool deviceReady;
   bool swapchainReady;
   bool swapchainImageViewsReady;
   bool frameResourcesReady;

} Vulkan;

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

void CreateVulkan()
{
   CreateInstance();
   CreateDebugMessenger();
   CreateSurface();
   SetPhysicalDevice();

   CreateDevice();
   CreateSwapchain();
   CreateSwapchainImageViews();
   CreateFrameResources();
}

void DestroyVulkan()
{
   if (Vulkan.device != VK_NULL_HANDLE)
   {
      vkDeviceWaitIdle(Vulkan.device);
   }

   DestroyFrameResources();
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
   DestroySwapchainImageViews();
   Vulkan.swapchainReady = false;
   Vulkan.swapchainImageViewsReady = false;

   CreateSwapchain();
   CreateSwapchainImageViews();
   CreateFrameResources();
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

auto RecordCommandBuffer(u32 frameIndex, u32 imageIndex, VkClearColorValue clearColor) -> VkResult
{
   Assert(Vulkan.swapchainReady, "Create the Vulkan swapchain before recording commands");
   Assert(Vulkan.swapchainImageViewsReady, "Create swapchain image views before recording commands");
   Assert(Vulkan.frameResourcesReady, "Frame resources must exist before recording commands");
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

   VkRenderingAttachmentInfo colorAttachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = imageView,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.color = clearColor},
   };

   VkExtent2D extent = Vulkan.swapchainExtent;
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
