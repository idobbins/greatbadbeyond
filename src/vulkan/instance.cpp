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
constexpr array<const char*, 3> instanceExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
  };
#elif defined(_WIN32)
constexpr array<const char*, 2> instanceExtensions{
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

static vector<VkExtensionProperties> enumerateInstanceExtensionProperties() {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);

    vector<VkExtensionProperties> exts(count);

    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());

    return exts;
}

static VkInstance createInstance()
{
    static constexpr VkApplicationInfo app_info {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = APP_NAME.data(),
        .pEngineName = APP_NAME.data(),
        .apiVersion = VK_API_VERSION_1_3,
    };

    static constexpr VkInstanceCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#if defined(__APPLE__)
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
#endif
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = static_cast<u32>(instanceExtensions.size()),
        .ppEnabledExtensionNames = instanceExtensions.data(),
    };

    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&create_info, nullptr, &inst);
    runtime_assert(r == VK_SUCCESS, "Failed to create instance");
    return inst;
}

#endif //CALLANDOR_VULKAN_INSTANCE_CPP
