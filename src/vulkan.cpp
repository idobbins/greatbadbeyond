#include <callandor.h>
#include <config.h>
#include <runtime.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <iostream>
#include <memory_resource>
#include <ostream>
#include <vector>

using namespace std;

static struct VulkanData
{
   VkInstance instance;
   VkSurfaceKHR surface;

   bool validationLayersEnabled;

} Vulkan;

static VkDebugUtilsMessengerCreateInfoEXT VulkanMakeDebugMessengerCreateInfo(void)
{
   VkDebugUtilsMessengerCreateInfoEXT createInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = DefaultDebugSeverityMask,
      .messageType = DefaultDebugTypeMask,
      .pfnUserCallback = NULL,
      .pUserData = NULL,
  };

   return createInfo;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
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

void InitVulkan(const VulkanConfig &config)
{
   InitInstance(config);
   InitSurface();
}

void CloseVulkan(const VulkanConfig &config)
{
   CloseSurface();
   CloseInstance(config);
}

void InitInstance(const VulkanConfig &config)
{
   VkApplicationInfo app_info {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = DefaultApplicationName,
      .pEngineName = DefaultEngineName
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

   if (config.debug)
   {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
   }

   if (config.portability)
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

   if (config.debug)
   {
      layers.push_back(ValidationLayerName);
   }

   for (auto ext: extensions)
   {
      cout << ext << endl;
   }

   VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { };
   const void *next = nullptr;
   if (config.debug)
   {
      debugCreateInfo = VulkanMakeDebugMessengerCreateInfo();
      debugCreateInfo.pfnUserCallback = VulkanDebugCallback;
      next = &debugCreateInfo;
   }

   VkInstanceCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = next,
      .flags = config.portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0u,
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

void CloseInstance(const VulkanConfig &config)
{
   (void)config;

   if (Vulkan.instance != VK_NULL_HANDLE)
   {
      vkDestroyInstance(Vulkan.instance, nullptr);
      Vulkan.instance = VK_NULL_HANDLE;
   }

   Vulkan.validationLayersEnabled = false;
}

void InitSurface()
{
   Assert(Vulkan.instance != VK_NULL_HANDLE, "Vulkan instance must be created before the surface");

   GLFWwindow *window = GetWindowHandle();
   Assert(window != nullptr, "GLFW window handle is null");

   VkResult result = glfwCreateWindowSurface(Vulkan.instance, window, nullptr, &Vulkan.surface);
   Assert(result == VK_SUCCESS, "Failed to create Vulkan surface");
}

void CloseSurface()
{
   if (Vulkan.surface == VK_NULL_HANDLE)
   {
      return;
   }

   Assert(Vulkan.instance != VK_NULL_HANDLE, "Vulkan instance must be valid when destroying the surface");

   vkDestroySurfaceKHR(Vulkan.instance, Vulkan.surface, nullptr);
   Vulkan.surface = VK_NULL_HANDLE;
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
