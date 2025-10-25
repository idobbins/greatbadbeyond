#include <callandor.h>
#include <config.h>
#include <utils.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstring>
#include <iostream>
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

   bool instanceReady;
   bool validationLayersEnabled;
   bool debugMessengerReady;
   bool physicalDeviceReady;
   bool deviceReady;

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
}

void DestroyVulkan()
{
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
