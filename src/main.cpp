#include <vulkan/vulkan.h>

#if defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#include <vulkan/vulkan_beta.h>
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
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
    cerr << "Vk Assert failed: " << message << " (" << result << ")" << endl;
    exit(EXIT_FAILURE);
  }
}

constexpr string_view APP_NAME = "callandor";
constexpr i32 WINDOW_WIDTH = 1280;
constexpr i32 WINDOW_HEIGHT = 720;

constexpr VkFormat SWAPCHAIN_FORMAT_PREFERRED = VK_FORMAT_B8G8R8A8_SRGB;
constexpr VkColorSpaceKHR SWAPCHAIN_COLOR_SPACE = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
constexpr VkFormat OFFSCREEN_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

constexpr u32 LOCAL_SIZE_X = 8;
constexpr u32 LOCAL_SIZE_Y = 8;
constexpr u32 FRAMES_IN_FLIGHT = 2;

constexpr const char* SHADER_PATH_COMPUTE = "./shaders/compute.comp.spv";
constexpr const char* SHADER_PATH_VERT    = "./shaders/fullscreen.vert.spv";
constexpr const char* SHADER_PATH_FRAG    = "./shaders/blit.frag.spv";

constexpr usize MAX_SHADER_BYTES = 1u << 18; // 256 KiB per shader module

struct Caps {
  static constexpr u32 MaxPhys            = 8;
  static constexpr u32 MaxQF              = 16;
  static constexpr u32 MaxDevExt          = 256;
  static constexpr u32 MaxSurfaceFormats  = 64;
  static constexpr u32 MaxPresentModes    = 32;
  static constexpr u32 MaxSwapchainImages = 4;
  static constexpr bool StrictCaps        = true;
};

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

struct SwapchainBundle {
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  u32 count = 0;
  array<VkImage, Caps::MaxSwapchainImages> images{};
  array<VkImageView, Caps::MaxSwapchainImages> views{};
  array<VkImageLayout, Caps::MaxSwapchainImages> layouts{};
};

struct OffscreenImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
};

struct FrameSync {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkSemaphore image_available = VK_NULL_HANDLE;
  VkSemaphore render_finished = VK_NULL_HANDLE;
  VkFence in_flight = VK_NULL_HANDLE;
};

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

inline VkSurfaceKHR create_surface(VkInstance inst, GLFWwindow* win) {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  result = glfwCreateWindowSurface(inst, win, nullptr, &surface);
  return surface;
}

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

inline bool queue_supports_present(VkPhysicalDevice d, u32 qf, VkSurfaceKHR surface) {
  VkBool32 present = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(d, qf, surface, &present);
  return present == VK_TRUE;
}

inline i32 first_graphics_compute_present_qf(VkPhysicalDevice d, VkSurfaceKHR surface) {
  u32 n = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
  if (n == 0) return -1;
  if (Caps::StrictCaps && n > Caps::MaxQF) return -1;
  if (n > Caps::MaxQF) n = Caps::MaxQF;

  array<VkQueueFamilyProperties, Caps::MaxQF> qfs{};
  vkGetPhysicalDeviceQueueFamilyProperties(d, &n, qfs.data());

  for (u32 i = 0; i < n; ++i) {
    const VkQueueFamilyProperties& info = qfs[i];
    const bool has_gfx = (info.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    const bool has_cmp = (info.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    if (!has_gfx || !has_cmp) continue;
    if (!queue_supports_present(d, i, surface)) continue;
    return static_cast<i32>(i);
  }
  return -1;
}

inline VkPhysicalDevice pick_device(VkInstance inst, VkSurfaceKHR surface, i32& out_qf) {
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

      const i32 gfx = first_graphics_compute_present_qf(d, surface);
      if (gfx < 0) continue;

      out_qf = gfx;
      return d;
    }
  }
  return VK_NULL_HANDLE;
}

inline VkDevice create_device(VkPhysicalDevice pd, u32 gfx_qf) {
  static constexpr f32 prio = 1.0f;
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

inline VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect) {
  const VkImageViewCreateInfo ici{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = format,
    .components{
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    },
    .subresourceRange{
      .aspectMask = aspect,
      .baseMipLevel = 0u,
      .levelCount = 1u,
      .baseArrayLayer = 0u,
      .layerCount = 1u,
    },
  };

  VkImageView view = VK_NULL_HANDLE;
  result = vkCreateImageView(device, &ici, nullptr, &view);
  vk_assert(result, "Failed to create image view");
  return view;
}

inline u32 find_memory_type_index(const VkPhysicalDeviceMemoryProperties& props, u32 type_bits, VkMemoryPropertyFlags required) {
  for (u32 i = 0; i < props.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) == 0u) continue;
    if ((props.memoryTypes[i].propertyFlags & required) == required) return i;
  }
  return UINT32_MAX;
}

inline SwapchainBundle create_swapchain(VkPhysicalDevice pd, VkDevice device, VkSurfaceKHR surface, u32 gfx_qf, GLFWwindow* window) {
  SwapchainBundle bundle{};

  VkSurfaceCapabilitiesKHR caps{};
  vk_assert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps), "Failed to query surface capabilities");

  u32 format_count = 0;
  vk_assert(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &format_count, nullptr), "Failed to query surface format count");
  runtime_assert(format_count > 0, "Surface offers no formats");
  if (Caps::StrictCaps) runtime_assert(format_count <= Caps::MaxSurfaceFormats, "Surface format count exceeds static cap");
  u32 limited_format_count = format_count;
  if (limited_format_count > Caps::MaxSurfaceFormats) limited_format_count = Caps::MaxSurfaceFormats;
  array<VkSurfaceFormatKHR, Caps::MaxSurfaceFormats> formats{};
  VkResult fmt_res = vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &limited_format_count, formats.data());
  runtime_assert(fmt_res == VK_SUCCESS || fmt_res == VK_INCOMPLETE, "Failed to query surface formats");
  runtime_assert(limited_format_count > 0, "No surface formats returned");

  VkSurfaceFormatKHR chosen_format = formats[0];
  for (u32 i = 0; i < limited_format_count; ++i) {
    if (formats[i].format == SWAPCHAIN_FORMAT_PREFERRED && formats[i].colorSpace == SWAPCHAIN_COLOR_SPACE) {
      chosen_format = formats[i];
      break;
    }
  }

  u32 present_mode_count = 0;
  vk_assert(vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &present_mode_count, nullptr), "Failed to query present mode count");
  runtime_assert(present_mode_count > 0, "Surface offers no present modes");
  if (Caps::StrictCaps) runtime_assert(present_mode_count <= Caps::MaxPresentModes, "Present mode count exceeds static cap");
  u32 limited_present_mode_count = present_mode_count;
  if (limited_present_mode_count > Caps::MaxPresentModes) limited_present_mode_count = Caps::MaxPresentModes;
  array<VkPresentModeKHR, Caps::MaxPresentModes> present_modes{};
  VkResult pm_res = vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &limited_present_mode_count, present_modes.data());
  runtime_assert(pm_res == VK_SUCCESS || pm_res == VK_INCOMPLETE, "Failed to query present modes");
  runtime_assert(limited_present_mode_count > 0, "No present modes returned");

  VkPresentModeKHR chosen_present = VK_PRESENT_MODE_FIFO_KHR;
  for (u32 i = 0; i < limited_present_mode_count; ++i) {
    if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
      chosen_present = VK_PRESENT_MODE_MAILBOX_KHR;
      break;
    }
  }

  VkExtent2D extent = {};
  if (caps.currentExtent.width != UINT32_MAX) {
    extent = caps.currentExtent;
  } else {
    i32 fb_width = 0;
    i32 fb_height = 0;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    extent.width = static_cast<u32>(fb_width);
    extent.height = static_cast<u32>(fb_height);
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }

  u32 image_count = caps.minImageCount + 1u;
  if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
  if (image_count > Caps::MaxSwapchainImages) image_count = Caps::MaxSwapchainImages;

  const VkSwapchainCreateInfoKHR sci{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface,
    .minImageCount = image_count,
    .imageFormat = chosen_format.format,
    .imageColorSpace = chosen_format.colorSpace,
    .imageExtent = extent,
    .imageArrayLayers = 1u,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0u,
    .pQueueFamilyIndices = nullptr,
    .preTransform = caps.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = chosen_present,
    .clipped = VK_TRUE,
    .oldSwapchain = VK_NULL_HANDLE,
  };

  vk_assert(vkCreateSwapchainKHR(device, &sci, nullptr, &bundle.swapchain), "Failed to create swapchain");

  u32 retrieved = 0;
  vk_assert(vkGetSwapchainImagesKHR(device, bundle.swapchain, &retrieved, nullptr), "Failed to query swapchain image count");
  runtime_assert(retrieved <= Caps::MaxSwapchainImages, "Swapchain returned more images than supported");
  vk_assert(vkGetSwapchainImagesKHR(device, bundle.swapchain, &retrieved, bundle.images.data()), "Failed to get swapchain images");

  bundle.count = retrieved;
  bundle.format = chosen_format.format;
  bundle.extent = extent;
  bundle.layouts.fill(VK_IMAGE_LAYOUT_UNDEFINED);

  for (u32 i = 0; i < bundle.count; ++i) {
    bundle.views[i] = create_image_view(device, bundle.images[i], bundle.format, VK_IMAGE_ASPECT_COLOR_BIT);
  }

  return bundle;
}

inline OffscreenImage create_offscreen_image(VkPhysicalDevice pd, VkDevice device, VkExtent2D extent) {
  OffscreenImage off{};

  const VkImageCreateInfo ici{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = OFFSCREEN_FORMAT,
    .extent = { extent.width, extent.height, 1u },
    .mipLevels = 1u,
    .arrayLayers = 1u,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  vk_assert(vkCreateImage(device, &ici, nullptr, &off.image), "Failed to create offscreen image");

  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(device, off.image, &req);

  VkPhysicalDeviceMemoryProperties mem_props{};
  vkGetPhysicalDeviceMemoryProperties(pd, &mem_props);

  const u32 memory_index = find_memory_type_index(mem_props, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  runtime_assert(memory_index != UINT32_MAX, "No suitable memory type for offscreen image");

  const VkMemoryAllocateInfo mai{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = req.size,
    .memoryTypeIndex = memory_index,
  };
  vk_assert(vkAllocateMemory(device, &mai, nullptr, &off.memory), "Failed to allocate image memory");
  vk_assert(vkBindImageMemory(device, off.image, off.memory, 0u), "Failed to bind image memory");

  off.view = create_image_view(device, off.image, OFFSCREEN_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);

  const VkSamplerCreateInfo sci{
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .mipLodBias = 0.0f,
    .maxAnisotropy = 1.0f,
    .minLod = 0.0f,
    .maxLod = 0.0f,
    .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    .unnormalizedCoordinates = VK_FALSE,
  };
  vk_assert(vkCreateSampler(device, &sci, nullptr, &off.sampler), "Failed to create sampler");

  return off;
}

inline VkShaderModule load_shader_module(VkDevice device, const char* path) {
  std::array<u32, MAX_SHADER_BYTES / sizeof(u32)> buffer{};
  std::FILE* file = std::fopen(path, "rb");
  runtime_assert(file != nullptr, "Failed to open shader file");

  const size_t bytes_read = std::fread(buffer.data(), 1, MAX_SHADER_BYTES, file);
  runtime_assert(bytes_read > 0, "Shader file is empty");
  runtime_assert(bytes_read < MAX_SHADER_BYTES, "Shader file exceeds static buffer");
  runtime_assert((bytes_read % sizeof(u32)) == 0u, "Shader byte count is not aligned");
  const bool read_error = std::ferror(file) != 0;
  std::fclose(file);
  runtime_assert(!read_error, "Error reading shader file");

  const VkShaderModuleCreateInfo smci{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = bytes_read,
    .pCode = buffer.data(),
  };

  VkShaderModule module = VK_NULL_HANDLE;
  vk_assert(vkCreateShaderModule(device, &smci, nullptr, &module), "Failed to create shader module");
  return module;
}

struct ComputePushConstants {
  f32 time = 0.0f;
  f32 inv_width = 0.0f;
  f32 inv_height = 0.0f;
  u32 frame_index = 0u;
};
static_assert(sizeof(ComputePushConstants) <= 128, "Push constants exceed spec limit");

int main() {
  init_glfw();
  GLFWwindow* window = create_window();

  VkInstance instance = create_instance();
  vk_assert(result, "Failed to create instance");

  VkSurfaceKHR surface = create_surface(instance, window);
  vk_assert(result, "Failed to create surface");

  i32 gfx_qf = -1;
  VkPhysicalDevice physical = pick_device(instance, surface, gfx_qf);
  runtime_assert(physical != VK_NULL_HANDLE, "No suitable physical device");
  runtime_assert(gfx_qf >= 0, "No graphics/compute/present queue family found");

  VkDevice device = create_device(physical, static_cast<u32>(gfx_qf));
  vk_assert(result, "Failed to create device");

  VkQueue queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, static_cast<u32>(gfx_qf), 0u, &queue);
  runtime_assert(queue != VK_NULL_HANDLE, "No queue returned");

  SwapchainBundle swapchain = create_swapchain(physical, device, surface, static_cast<u32>(gfx_qf), window);
  runtime_assert(swapchain.count > 0u, "Swapchain has no images");

  OffscreenImage offscreen = create_offscreen_image(physical, device, swapchain.extent);

  const VkDescriptorSetLayoutBinding storage_binding{
    .binding = 0u,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  };
  const VkDescriptorSetLayoutBinding sampled_binding{
    .binding = 1u,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1u,
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  const array layout_bindings{ storage_binding, sampled_binding };
  const VkDescriptorSetLayoutCreateInfo ds_layout_ci{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = static_cast<u32>(layout_bindings.size()),
    .pBindings = layout_bindings.data(),
  };
  VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
  vk_assert(vkCreateDescriptorSetLayout(device, &ds_layout_ci, nullptr, &descriptor_layout), "Failed to create descriptor set layout");

  const VkDescriptorPoolSize pool_sizes[]{
    {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = 1u,
    },
    {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1u,
    },
  };
  const VkDescriptorPoolCreateInfo dpci{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1u,
    .poolSizeCount = static_cast<u32>(std::size(pool_sizes)),
    .pPoolSizes = pool_sizes,
  };
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  vk_assert(vkCreateDescriptorPool(device, &dpci, nullptr, &descriptor_pool), "Failed to create descriptor pool");

  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  const VkDescriptorSetAllocateInfo dsai{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = descriptor_pool,
    .descriptorSetCount = 1u,
    .pSetLayouts = &descriptor_layout,
  };
  vk_assert(vkAllocateDescriptorSets(device, &dsai, &descriptor_set), "Failed to allocate descriptor set");

  const VkDescriptorImageInfo storage_info{
    .sampler = VK_NULL_HANDLE,
    .imageView = offscreen.view,
    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  };
  const VkDescriptorImageInfo sampler_info{
    .sampler = offscreen.sampler,
    .imageView = offscreen.view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  const VkWriteDescriptorSet descriptor_writes[]{
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptor_set,
      .dstBinding = 0u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = &storage_info,
    },
    {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptor_set,
      .dstBinding = 1u,
      .descriptorCount = 1u,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &sampler_info,
    },
  };
  vkUpdateDescriptorSets(device, static_cast<u32>(std::size(descriptor_writes)), descriptor_writes, 0u, nullptr);

  VkShaderModule compute_module = load_shader_module(device, SHADER_PATH_COMPUTE);
  VkShaderModule vert_module = load_shader_module(device, SHADER_PATH_VERT);
  VkShaderModule frag_module = load_shader_module(device, SHADER_PATH_FRAG);

  const VkPushConstantRange compute_push_range{
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    .offset = 0u,
    .size = static_cast<u32>(sizeof(ComputePushConstants)),
  };

  const VkPipelineLayoutCreateInfo compute_pl_ci{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1u,
    .pSetLayouts = &descriptor_layout,
    .pushConstantRangeCount = 1u,
    .pPushConstantRanges = &compute_push_range,
  };
  VkPipelineLayout compute_layout = VK_NULL_HANDLE;
  vk_assert(vkCreatePipelineLayout(device, &compute_pl_ci, nullptr, &compute_layout), "Failed to create compute pipeline layout");

  const VkPipelineLayoutCreateInfo graphics_pl_ci{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1u,
    .pSetLayouts = &descriptor_layout,
    .pushConstantRangeCount = 0u,
    .pPushConstantRanges = nullptr,
  };
  VkPipelineLayout graphics_layout = VK_NULL_HANDLE;
  vk_assert(vkCreatePipelineLayout(device, &graphics_pl_ci, nullptr, &graphics_layout), "Failed to create graphics pipeline layout");

  const VkPipelineShaderStageCreateInfo compute_stage{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = compute_module,
    .pName = "main",
  };
  const VkComputePipelineCreateInfo cpci_compute{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = compute_stage,
    .layout = compute_layout,
  };
  VkPipeline compute_pipeline = VK_NULL_HANDLE;
  vk_assert(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u, &cpci_compute, nullptr, &compute_pipeline), "Failed to create compute pipeline");

  const VkPipelineShaderStageCreateInfo shader_stages[]{
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_module,
      .pName = "main",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_module,
      .pName = "main",
    },
  };
  const VkPipelineVertexInputStateCreateInfo vi_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 0u,
    .vertexAttributeDescriptionCount = 0u,
  };
  const VkPipelineInputAssemblyStateCreateInfo ia_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
  };
  const VkPipelineViewportStateCreateInfo vp_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1u,
    .pViewports = nullptr,
    .scissorCount = 1u,
    .pScissors = nullptr,
  };
  const VkPipelineRasterizationStateCreateInfo rs_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,
    .lineWidth = 1.0f,
  };
  const VkPipelineMultisampleStateCreateInfo ms_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
  };
  const VkPipelineColorBlendAttachmentState blend_attachment{
    .blendEnable = VK_FALSE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };
  const VkPipelineColorBlendStateCreateInfo cb_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1u,
    .pAttachments = &blend_attachment,
  };
  constexpr array dynamic_states{
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };
  const VkPipelineDynamicStateCreateInfo dyn_state{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
    .pDynamicStates = dynamic_states.data(),
  };

  const VkAttachmentDescription color_attachment{
    .format = swapchain.format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  const VkAttachmentReference color_ref{
    .attachment = 0u,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  const VkSubpassDescription subpass{
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1u,
    .pColorAttachments = &color_ref,
  };
  const VkSubpassDependency dependency{
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0u,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = 0u,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };
  const VkRenderPassCreateInfo rpci{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1u,
    .pAttachments = &color_attachment,
    .subpassCount = 1u,
    .pSubpasses = &subpass,
    .dependencyCount = 1u,
    .pDependencies = &dependency,
  };
  VkRenderPass render_pass = VK_NULL_HANDLE;
  vk_assert(vkCreateRenderPass(device, &rpci, nullptr, &render_pass), "Failed to create render pass");

  array<VkFramebuffer, Caps::MaxSwapchainImages> framebuffers{};
  for (u32 i = 0; i < swapchain.count; ++i) {
    const VkImageView attachments[] { swapchain.views[i] };
    const VkFramebufferCreateInfo fbci{
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = render_pass,
      .attachmentCount = 1u,
      .pAttachments = attachments,
      .width = swapchain.extent.width,
      .height = swapchain.extent.height,
      .layers = 1u,
    };
    vk_assert(vkCreateFramebuffer(device, &fbci, nullptr, &framebuffers[i]), "Failed to create framebuffer");
  }

  const VkGraphicsPipelineCreateInfo gpci{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = static_cast<u32>(std::size(shader_stages)),
    .pStages = shader_stages,
    .pVertexInputState = &vi_state,
    .pInputAssemblyState = &ia_state,
    .pViewportState = &vp_state,
    .pRasterizationState = &rs_state,
    .pMultisampleState = &ms_state,
    .pDepthStencilState = nullptr,
    .pColorBlendState = &cb_state,
    .pDynamicState = &dyn_state,
    .layout = graphics_layout,
    .renderPass = render_pass,
    .subpass = 0u,
  };
  VkPipeline graphics_pipeline = VK_NULL_HANDLE;
  vk_assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &gpci, nullptr, &graphics_pipeline), "Failed to create graphics pipeline");

  // Shader modules no longer needed once pipelines are created.
  vkDestroyShaderModule(device, frag_module, nullptr);
  vkDestroyShaderModule(device, vert_module, nullptr);
  vkDestroyShaderModule(device, compute_module, nullptr);

  const VkCommandPoolCreateInfo cpci_pool{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = static_cast<u32>(gfx_qf),
  };
  VkCommandPool command_pool = VK_NULL_HANDLE;
  vk_assert(vkCreateCommandPool(device, &cpci_pool, nullptr, &command_pool), "Failed to create command pool");

  array<FrameSync, FRAMES_IN_FLIGHT> frames{};
  const VkCommandBufferAllocateInfo cbai{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = command_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = FRAMES_IN_FLIGHT,
  };
  VkCommandBuffer cmd_buffers[FRAMES_IN_FLIGHT] = {};
  vk_assert(vkAllocateCommandBuffers(device, &cbai, cmd_buffers), "Failed to allocate command buffers");

  for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    frames[i].cmd = cmd_buffers[i];

    const VkSemaphoreCreateInfo sci_sync{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    vk_assert(vkCreateSemaphore(device, &sci_sync, nullptr, &frames[i].image_available), "Failed to create semaphore");
    vk_assert(vkCreateSemaphore(device, &sci_sync, nullptr, &frames[i].render_finished), "Failed to create semaphore");

    const VkFenceCreateInfo fci{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vk_assert(vkCreateFence(device, &fci, nullptr, &frames[i].in_flight), "Failed to create fence");
  }

  const auto start_time = chrono::steady_clock::now();
  bool running = true;
  bool first_compute = true;
  u32 frame_cursor = 0u;
  u64 frame_counter = 0u;

  while (running) {
    glfwPollEvents();
    running = glfwWindowShouldClose(window) == GLFW_FALSE;
    if (!running) break;

    FrameSync& frame = frames[frame_cursor];
    vk_assert(vkWaitForFences(device, 1u, &frame.in_flight, VK_TRUE, UINT64_MAX), "Failed waiting for fence");
    vk_assert(vkResetFences(device, 1u, &frame.in_flight), "Failed to reset fence");

    u32 image_index = 0;
    const VkResult acquire = vkAcquireNextImageKHR(device, swapchain.swapchain, UINT64_MAX, frame.image_available, VK_NULL_HANDLE, &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
      // Minimal implementation: exit main loop on resize/out-of-date.
      running = false;
      break;
    }
    vk_assert(acquire, "Failed to acquire swapchain image");

    vk_assert(vkResetCommandBuffer(frame.cmd, 0u), "Failed to reset command buffer");

    const VkCommandBufferBeginInfo cbbi{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk_assert(vkBeginCommandBuffer(frame.cmd, &cbbi), "Failed to begin command buffer");

    const VkImageMemoryBarrier to_storage{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = first_compute ? 0u : VK_ACCESS_SHADER_READ_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .oldLayout = first_compute ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = offscreen.image,
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = 1u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
    };
    vkCmdPipelineBarrier(
      frame.cmd,
      first_compute ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      0u, nullptr,
      0u, nullptr,
      1u, &to_storage
    );
    first_compute = false;

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
    vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout, 0u, 1u, &descriptor_set, 0u, nullptr);

    const auto now = chrono::steady_clock::now();
    const chrono::duration<f32> elapsed = now - start_time;
    const ComputePushConstants push{
      .time = elapsed.count(),
      .inv_width = 1.0f / static_cast<f32>(swapchain.extent.width),
      .inv_height = 1.0f / static_cast<f32>(swapchain.extent.height),
      .frame_index = static_cast<u32>(frame_counter),
    };
    vkCmdPushConstants(frame.cmd, compute_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, static_cast<u32>(sizeof(ComputePushConstants)), &push);

    const u32 group_count_x = (swapchain.extent.width + LOCAL_SIZE_X - 1u) / LOCAL_SIZE_X;
    const u32 group_count_y = (swapchain.extent.height + LOCAL_SIZE_Y - 1u) / LOCAL_SIZE_Y;
    vkCmdDispatch(frame.cmd, group_count_x, group_count_y, 1u);

    const VkImageMemoryBarrier to_sampled{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = offscreen.image,
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = 1u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
    };
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0u,
      0u, nullptr,
      0u, nullptr,
      1u, &to_sampled
    );

    const VkImageLayout previous_layout = swapchain.layouts[image_index];
    const VkImageMemoryBarrier swap_to_color{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0u,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = previous_layout,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = swapchain.images[image_index],
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = 1u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
    };
    const VkPipelineStageFlags swap_src_stage =
      (previous_layout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
        : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    vkCmdPipelineBarrier(
      frame.cmd,
      swap_src_stage,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0u,
      0u, nullptr,
      0u, nullptr,
      1u, &swap_to_color
    );

    const VkClearValue clear_color{ .color{ {0.0f, 0.0f, 0.0f, 1.0f} } };
    const VkRenderPassBeginInfo rp_begin{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = render_pass,
      .framebuffer = framebuffers[image_index],
      .renderArea{
        .offset{ 0, 0 },
        .extent = swapchain.extent,
      },
      .clearValueCount = 1u,
      .pClearValues = &clear_color,
    };
    vkCmdBeginRenderPass(frame.cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
    vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout, 0u, 1u, &descriptor_set, 0u, nullptr);

    const VkViewport viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<f32>(swapchain.extent.width),
      .height = static_cast<f32>(swapchain.extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
    };
    const VkRect2D scissor{
      .offset{ 0, 0 },
      .extent = swapchain.extent,
    };
    vkCmdSetViewport(frame.cmd, 0u, 1u, &viewport);
    vkCmdSetScissor(frame.cmd, 0u, 1u, &scissor);
    vkCmdDraw(frame.cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(frame.cmd);

    const VkImageMemoryBarrier back_to_general{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = offscreen.image,
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = 1u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
    };
    vkCmdPipelineBarrier(
      frame.cmd,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0u,
      0u, nullptr,
      0u, nullptr,
      1u, &back_to_general
    );

    vk_assert(vkEndCommandBuffer(frame.cmd), "Failed to end command buffer");

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1u,
      .pWaitSemaphores = &frame.image_available,
      .pWaitDstStageMask = &wait_stage,
      .commandBufferCount = 1u,
      .pCommandBuffers = &frame.cmd,
      .signalSemaphoreCount = 1u,
      .pSignalSemaphores = &frame.render_finished,
    };
    vk_assert(vkQueueSubmit(queue, 1u, &submit, frame.in_flight), "Failed to submit queue");

    const VkPresentInfoKHR present_info{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1u,
      .pWaitSemaphores = &frame.render_finished,
      .swapchainCount = 1u,
      .pSwapchains = &swapchain.swapchain,
      .pImageIndices = &image_index,
    };
    const VkResult present_res = vkQueuePresentKHR(queue, &present_info);
    if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR) {
      running = false;
    } else {
      vk_assert(present_res, "Failed to present swapchain image");
    }

    swapchain.layouts[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    frame_cursor = (frame_cursor + 1u) % FRAMES_IN_FLIGHT;
    ++frame_counter;
  }

  vkDeviceWaitIdle(device);

  for (FrameSync& frame : frames) {
    vkDestroySemaphore(device, frame.render_finished, nullptr);
    vkDestroySemaphore(device, frame.image_available, nullptr);
    vkDestroyFence(device, frame.in_flight, nullptr);
  }

  vkFreeCommandBuffers(device, command_pool, FRAMES_IN_FLIGHT, cmd_buffers);
  vkDestroyCommandPool(device, command_pool, nullptr);

  vkDestroyPipeline(device, graphics_pipeline, nullptr);
  vkDestroyPipeline(device, compute_pipeline, nullptr);
  vkDestroyPipelineLayout(device, graphics_layout, nullptr);
  vkDestroyPipelineLayout(device, compute_layout, nullptr);

  for (u32 i = 0; i < swapchain.count; ++i) {
    vkDestroyFramebuffer(device, framebuffers[i], nullptr);
  }

  vkDestroyRenderPass(device, render_pass, nullptr);

  vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);

  vkDestroySampler(device, offscreen.sampler, nullptr);
  vkDestroyImageView(device, offscreen.view, nullptr);
  vkDestroyImage(device, offscreen.image, nullptr);
  vkFreeMemory(device, offscreen.memory, nullptr);

  for (u32 i = 0; i < swapchain.count; ++i) {
    vkDestroyImageView(device, swapchain.views[i], nullptr);
  }
  vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);

  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);

  glfwDestroyWindow(window);
  glfwTerminate();

  return EXIT_SUCCESS;
}
