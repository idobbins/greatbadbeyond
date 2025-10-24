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
   VkSurfaceKHR surface;
   VkPhysicalDevice physicalDevice;
   VkDevice device;
   VkQueue universalQueue;
   u32 universalQueueFamily;

   bool validationLayersEnabled;
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

void CreateVulkan()
{
   CreateInstance();
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
   static pmr::vector<const char *> extensions {&extensionStackOnlyResource};

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
   static pmr::vector<const char *> layers {&layerStackOnlyResource };

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
   if (Vulkan.instance != VK_NULL_HANDLE)
   {
      vkDestroyInstance(Vulkan.instance, nullptr);
      Vulkan.instance = VK_NULL_HANDLE;
   }

   Vulkan.validationLayersEnabled = false;
   Vulkan.physicalDevice = VK_NULL_HANDLE;
   Vulkan.device = VK_NULL_HANDLE;
   Vulkan.universalQueue = VK_NULL_HANDLE;
   Vulkan.universalQueueFamily = 0;
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

auto GetPhysicalDeviceFeatures2(const VkPhysicalDevice &device) -> const VkPhysicalDeviceFeatures2&
{
   Assert(device != VK_NULL_HANDLE, "Physical device handle is null");

   struct CacheEntry {
      VkPhysicalDevice          physicalDevice{};
      VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
      bool                      ready{};
   };

   static array<CacheEntry, MaxPhysicalDevices> cache {};
   static u32 cacheCount = 0;

   for (u32 index = 0; index < cacheCount; ++index)
   {
      const auto &entry = cache[index];
      if (entry.ready && entry.physicalDevice == device)
      {
         return entry.features2;
      }
   }

   Assert(cacheCount < cache.size(), "Too many features2 cache entries");
   auto &entry = cache[cacheCount++];
   entry = CacheEntry{ .physicalDevice = device };
   vkGetPhysicalDeviceFeatures2(device, &entry.features2);
   entry.ready = true;

   return entry.features2;
}

auto GetPhysicalDeviceVulkan13Features(const VkPhysicalDevice &device) -> const VkPhysicalDeviceVulkan13Features&
{
   Assert(device != VK_NULL_HANDLE, "Physical device handle is null");

   struct CacheEntry {
      VkPhysicalDevice                 physicalDevice{};
      VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
      bool                             ready{};
   };

   static array<CacheEntry, MaxPhysicalDevices> cache {};
   static u32 cacheCount = 0;

   for (u32 index = 0; index < cacheCount; ++index)
   {
      const auto &entry = cache[index];
      if (entry.ready && entry.physicalDevice == device)
      {
         return entry.features13;
      }
   }

   Assert(cacheCount < cache.size(), "Too many v1.3 feature cache entries");
   auto &entry = cache[cacheCount++];
   entry = CacheEntry{ .physicalDevice = device };

   VkPhysicalDeviceFeatures2 head { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
   head.pNext = &entry.features13;
   vkGetPhysicalDeviceFeatures2(device, &head);

   entry.ready = true;
   return entry.features13;
}

span<const VkPhysicalDevice> GetPhysicalDevices()
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

span<const VkQueueFamilyProperties> GetQueueFamilyProperties(const VkPhysicalDevice &device)
{
   Assert(device != VK_NULL_HANDLE, "Physical device handle is null");

   struct QueueFamilyCacheEntry
   {
      VkPhysicalDevice physicalDevice;
      array<VkQueueFamilyProperties, MaxQueueFamilies> properties;
      uint32_t count;
      bool ready;
   };

   static array<QueueFamilyCacheEntry, MaxPhysicalDevices> cache {};
   static uint32_t cacheCount = 0;

   for (uint32_t index = 0; index < cacheCount; ++index)
   {
      QueueFamilyCacheEntry &entry = cache[index];
      if (entry.ready && (entry.physicalDevice == device))
      {
         return {entry.properties.data(), entry.count};
      }
   }

   Assert(cacheCount < cache.size(), "Too many queue family cache entries");

   QueueFamilyCacheEntry &entry = cache[cacheCount];
   entry = QueueFamilyCacheEntry {
      .physicalDevice = device,
      .properties = {},
      .count = 0,
      .ready = false
   };

   uint32_t familyCount = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
   Assert(familyCount > 0, "Physical device has no queue families");
   Assert(familyCount <= entry.properties.size(), "Too many queue families for cache entry");

   vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, entry.properties.data());

   entry.count = familyCount;
   entry.ready = true;
   cacheCount += 1;

   return {entry.properties.data(), entry.count};
}

bool GetUniversalQueue(const VkPhysicalDevice &device, VkSurfaceKHR surface, uint32_t *family)
{
   Assert(family != nullptr, "Queue family output pointer is null");
   Assert(surface != VK_NULL_HANDLE, "Vulkan surface handle is null");

   span<const VkQueueFamilyProperties> properties = GetQueueFamilyProperties(device);
   Assert(!properties.empty(), "Physical device reports zero queue families");

   const VkQueueFlags universalMask = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

   for (uint32_t index = 0; index < properties.size(); ++index)
   {
      VkBool32 present = VK_FALSE;
      VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
      Assert(result == VK_SUCCESS, "Failed to query Vulkan surface support");

      const VkQueueFamilyProperties &familyProperties = properties[index];

      if ((present == VK_TRUE) &&
          ((familyProperties.queueFlags & universalMask) != 0) &&
          (familyProperties.queueCount > 0))
      {
         *family = index;
         return true;
      }
   }

   return false;
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
      uint32_t queueFamily = 0;
      if (!GetUniversalQueue(device, Vulkan.surface, &queueFamily))
      {
         continue;
      }

      VkPhysicalDeviceProperties properties = {};
      vkGetPhysicalDeviceProperties(device, &properties);

      Vulkan.physicalDevice = device;
      Vulkan.universalQueueFamily = queueFamily;
      Vulkan.physicalDeviceReady = true;
      Vulkan.deviceReady = false;

      LogInfo("[vulkan] Selected physical device: %s", properties.deviceName);
      return;
   }

   Assert(false, "Failed to find a Vulkan physical device with universal queue support");
}

void CreateDevice()
{
   if (Vulkan.deviceReady)
   {
      return;
   }

   Assert(Vulkan.physicalDeviceReady, "Select a physical device before creating the logical device");

   GetPhysicalDeviceFeatures2(Vulkan.physicalDevice);
   const auto &supportedFeatures13 = GetPhysicalDeviceVulkan13Features(Vulkan.physicalDevice);

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

   float queuePriority = 1.0f;

   VkDeviceQueueCreateInfo queueCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = Vulkan.universalQueueFamily,
      .queueCount = 1,
      .pQueuePriorities = &queuePriority,
   };

   VkDeviceCreateInfo deviceCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features2,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queueCreateInfo,
      .enabledExtensionCount = extensionCount,
      .ppEnabledExtensionNames = extensions.data(),
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .pEnabledFeatures = nullptr,
   };

   result = vkCreateDevice(Vulkan.physicalDevice, &deviceCreateInfo, nullptr, &Vulkan.device);
   Assert(result == VK_SUCCESS, "Failed to create Vulkan logical device");

   vkGetDeviceQueue(Vulkan.device, Vulkan.universalQueueFamily, 0, &Vulkan.universalQueue);
   Assert(Vulkan.universalQueue != VK_NULL_HANDLE, "Failed to retrieve Vulkan universal queue");

   Vulkan.deviceReady = true;

   LogInfo("[vulkan] Created logical device with universal queue family %u", Vulkan.universalQueueFamily);
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
   Vulkan.universalQueue = VK_NULL_HANDLE;
   Vulkan.deviceReady = false;
}
