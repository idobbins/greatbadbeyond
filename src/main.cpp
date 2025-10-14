#include <vulkan/vulkan.h>

#if defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#include <vulkan/vulkan_beta.h>
#endif

#include <GLFW/glfw3.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>

using namespace std;

using  u8  = uint8_t;
using  u16 = uint16_t;
using  u32 = uint32_t;
using  u64 = uint64_t;

using  i8  = int8_t;
using  i16 = int16_t;
using  i32 = int32_t;
using  i64 = int64_t;

using  f32 = float;
using  f64 = double;

using  usize = size_t;
using  isize = ptrdiff_t;

inline void runtime_assert(const bool condition, const string_view message) {
  if (!condition) {
    cerr << "Runtime assertion failed: " << message << endl;
    exit(EXIT_FAILURE);
  }
}

inline void vk_assert(const VkResult result, const string_view message) {
  if (result != VK_SUCCESS) {
    cerr << "Vk Assert failed: " << message << endl;
    exit(EXIT_FAILURE);
  }
}

constexpr string_view APP_NAME = "callandor";
constexpr i32 WINDOW_WIDTH = 800;
constexpr i32 WINDOW_HEIGHT = 600;

// ---------- Instance ----------
VkResult result = VK_SUCCESS;

#if defined(__APPLE__)
constexpr array<const char*, 3> instance_exts{
  VK_KHR_SURFACE_EXTENSION_NAME,
  VK_EXT_METAL_SURFACE_EXTENSION_NAME,
  VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
#elif defined(_WIN32)
constexpr array<const char*, 2> instance_exts{
  VK_KHR_SURFACE_EXTENSION_NAME,
  VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
#elif defined(__linux__)
constexpr array<const char*, 2> instance_exts{
  VK_KHR_SURFACE_EXTENSION_NAME,
  VK_KHR_XCB_SURFACE_EXTENSION_NAME,
};
#else
constexpr array<const char*, 1> instance_exts{
  VK_KHR_SURFACE_EXTENSION_NAME,
};
#endif

inline VkInstance create_instance() {
  const VkApplicationInfo app_info{
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = APP_NAME.data(),
    .pEngineName = APP_NAME.data(),
    .apiVersion = VK_API_VERSION_1_3,
  };

  VkInstanceCreateInfo ici{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app_info,
    .enabledExtensionCount = static_cast<u32>(instance_exts.size()),
    .ppEnabledExtensionNames = instance_exts.data(),
  };

#if defined(__APPLE__)
  ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

  VkInstance inst = VK_NULL_HANDLE;
  result = vkCreateInstance(&ici, nullptr, &inst);
  return inst;
}

inline void init_glfw() {
  const int ok = glfwInit();
  runtime_assert(ok == GLFW_TRUE, "Failed to initialize GLFW");
  runtime_assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW reports no Vulkan support");
}

inline GLFWwindow* create_window() {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  GLFWwindow* win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APP_NAME.data(), nullptr, nullptr);
  runtime_assert(win != nullptr, "Failed to create GLFW window");
  return win;
}

inline VkSurfaceKHR create_surface(VkInstance inst, GLFWwindow* win) {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  result = glfwCreateWindowSurface(inst, win, nullptr, &surface);
  return surface;
}

// ---------- Device selection policy (all constexpr) ----------
struct Caps {
  static constexpr u32 MaxPhys   = 8;
  static constexpr u32 MaxQF     = 16;
  static constexpr u32 MaxDevExt = 256;
  static constexpr bool StrictCaps = true; // fail if counts exceed caps
};

#if defined(__APPLE__)
constexpr array device_exts{
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
};
#else
constexpr array device_exts{
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
#endif

constexpr array preferred_types{
  VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
  VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
  VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
  VK_PHYSICAL_DEVICE_TYPE_CPU,
};

constexpr VkPhysicalDeviceFeatures required_features{
  .samplerAnisotropy = VK_TRUE,
};

// ---------- No-heap helpers ----------
inline bool has_required_extensions(VkPhysicalDevice d) {
  u32 n = 0;
  VkResult r = vkEnumerateDeviceExtensionProperties(d, nullptr, &n, nullptr);
  if (r != VK_SUCCESS) return false;
  if (Caps::StrictCaps && n > Caps::MaxDevExt) return false;
  if (n > Caps::MaxDevExt) n = Caps::MaxDevExt;

  array<VkExtensionProperties, Caps::MaxDevExt> props{};
  r = vkEnumerateDeviceExtensionProperties(d, nullptr, &n, props.data());
  if (r != VK_SUCCESS && r != VK_INCOMPLETE) return false;

  for (const char* need : device_exts) {
    bool ok = false;
    for (u32 i = 0; i < n; ++i) {
      if (std::strcmp(props[i].extensionName, need) == 0) { ok = true; break; }
    }
    if (!ok) return false;
  }
  return true;
}

inline bool meets_required_features(VkPhysicalDevice d) {
  VkPhysicalDeviceFeatures f{};
  vkGetPhysicalDeviceFeatures(d, &f);
  if (required_features.samplerAnisotropy && !f.samplerAnisotropy) return false;
  return true;
}

inline i32 first_graphics_qf(VkPhysicalDevice d) {
  u32 n = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
  if (n == 0) return -1;
  if (Caps::StrictCaps && n > Caps::MaxQF) return -1;
  if (n > Caps::MaxQF) n = Caps::MaxQF;

  array<VkQueueFamilyProperties, Caps::MaxQF> qfs{};
  vkGetPhysicalDeviceQueueFamilyProperties(d, &n, qfs.data());

  for (u32 i = 0; i < n; ++i)
    if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) return (i32)i;

  return -1;
}

// First match by preferred type that satisfies policy; no scoring needed.
inline VkPhysicalDevice pick_device(VkInstance inst, i32& out_gfx_qf) {
  u32 n = 0;
  VkResult r = vkEnumeratePhysicalDevices(inst, &n, nullptr);
  if (r != VK_SUCCESS || n == 0) return VK_NULL_HANDLE;
  if (Caps::StrictCaps && n > Caps::MaxPhys) return VK_NULL_HANDLE;
  if (n > Caps::MaxPhys) n = Caps::MaxPhys;

  array<VkPhysicalDevice, Caps::MaxPhys> devs{};
  r = vkEnumeratePhysicalDevices(inst, &n, devs.data());
  if (r != VK_SUCCESS) return VK_NULL_HANDLE;

  for (VkPhysicalDeviceType want : preferred_types) {
    for (u32 i = 0; i < n; ++i) {
      VkPhysicalDevice d = devs[i];
      VkPhysicalDeviceProperties p{};
      vkGetPhysicalDeviceProperties(d, &p);
      if (p.deviceType != want) continue;

      if (!has_required_extensions(d)) continue;
      if (!meets_required_features(d)) continue;

      const i32 gfx = first_graphics_qf(d);
      if (gfx < 0) continue;

      out_gfx_qf = gfx;
      return d; // early exit on the first acceptable device
    }
  }
  return VK_NULL_HANDLE;
}

inline VkDevice create_device(VkPhysicalDevice pd, u32 gfx_qf) {
  constexpr f32 prio = 1.0f;
  const VkDeviceQueueCreateInfo qci{
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = gfx_qf,
    .queueCount = 1,
    .pQueuePriorities = &prio,
  };
  const VkDeviceCreateInfo dci{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1u,
    .pQueueCreateInfos = &qci,
    .enabledExtensionCount = static_cast<u32>(device_exts.size()),
    .ppEnabledExtensionNames = device_exts.data(),
    .pEnabledFeatures = &required_features,
  };
  VkDevice device = VK_NULL_HANDLE;
  result = vkCreateDevice(pd, &dci, nullptr, &device);
  return device;
}

// ---------- Main ----------
int main() {
  init_glfw();
  GLFWwindow* window = create_window();

  VkInstance instance = create_instance();
  vk_assert(result, "Failed to create instance");

  VkSurfaceKHR surface = create_surface(instance, window);
  vk_assert(result, "Failed to create surface");

  i32 gfx_qf = -1;
  VkPhysicalDevice physical = pick_device(instance, gfx_qf);
  runtime_assert(physical != VK_NULL_HANDLE, "No suitable physical device");
  runtime_assert(gfx_qf >= 0, "No graphics queue family found");

  VkDevice device = create_device(physical, static_cast<u32>(gfx_qf));
  vk_assert(result, "Failed to create device");

  VkQueue graphics_queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, static_cast<u32>(gfx_qf), 0, &graphics_queue);
  runtime_assert(graphics_queue != VK_NULL_HANDLE, "No graphics queue returned");

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    // ... your app ...
  }

  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
  glfwDestroyWindow(window);
  glfwTerminate();
}
