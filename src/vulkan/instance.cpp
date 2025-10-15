#ifndef CALLANDOR_VULKAN_INSTANCE_CPP
#define CALLANDOR_VULKAN_INSTANCE_CPP

#include "types.cpp"
#include "assert.cpp"
#include "vulkan/headers.cpp"  // was: "headers.cpp" via ../

#include <string_view>
#include <array>
#include <vector>

using namespace std;

constexpr string_view APP_NAME = "callandor";

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

static std::vector<VkExtensionProperties> exts() {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);

    std::vector<VkExtensionProperties> exts(count);

    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());

    return exts;
}

VkInstance create_instance()
{
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
    runtime_assert(vkCreateInstance(&ici, nullptr, &inst) == VK_SUCCESS, "Failed to create instance");
    return inst;
}

#endif //CALLANDOR_VULKAN_INSTANCE_CPP
