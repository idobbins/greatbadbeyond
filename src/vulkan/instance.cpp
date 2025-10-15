#pragma once

#include "types.cpp"
#include "assert.cpp"
#include "vulkan/headers.cpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace std;

constexpr string_view APP_NAME = "callandor";

#if defined(VK_USE_PLATFORM_WIN32_KHR)
inline constexpr array<const char*, 2> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = 0;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
inline constexpr array<const char*, 2> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = 0;
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
inline constexpr array<const char*, 2> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = 0;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
inline constexpr array<const char*, 2> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = 0;
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
inline constexpr array<const char*, 2> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = 0;
#elif defined(VK_USE_PLATFORM_METAL_EXT)
inline constexpr array<const char*, 3> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#elif defined(__APPLE__)
inline constexpr array<const char*, 3> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#elif defined(_WIN32)
inline constexpr array<const char*, 2> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = 0;
#elif defined(__linux__)
inline constexpr array<const char*, 2> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = 0;
#else
inline constexpr array<const char*, 1> platformExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
};
inline constexpr VkInstanceCreateFlags instanceFlags = 0;
#endif

inline constexpr array<const char*, 1> debugExtensions{
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

inline constexpr array<const char*, 1> validationLayers{
    "VK_LAYER_KHRONOS_validation",
};

struct InstanceConfig {
    bool enableDebug = false;
};

static std::span<const VkExtensionProperties> enumerateInstanceExtensionProperties() {
    static std::array<VkExtensionProperties, 256> cache{};
    static u32 count = 0;
    static std::once_flag once;

    std::call_once(once, [] {
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        runtime_assert(count <= cache.size(), "Too many instance extensions for cache");
        vkEnumerateInstanceExtensionProperties(nullptr, &count, cache.data());
    });
    return {cache.data(), count};
}

static std::span<const VkLayerProperties> enumerateInstanceLayerProperties() {
    static std::array<VkLayerProperties, 64> cache{};
    static u32 count = 0;
    static std::once_flag once;

    std::call_once(once, [] {
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        runtime_assert(count <= cache.size(), "Too many instance layers for cache");
        vkEnumerateInstanceLayerProperties(&count, cache.data());
    });
    return {cache.data(), count};
}

static const char* findMissingExtension(std::span<const char* const> required) {
    const auto supported = enumerateInstanceExtensionProperties();
    auto proj = [](const VkExtensionProperties& p) { return std::string_view{p.extensionName}; };

    const auto it = std::ranges::find_if(required, [&](const char* r) {
        return !std::ranges::contains(supported, std::string_view{r}, proj);
    });
    return it == required.end() ? nullptr : *it;
}

static const char* findMissingLayer(std::span<const char* const> required) {
    const auto supported = enumerateInstanceLayerProperties();
    auto proj = [](const VkLayerProperties& p) { return std::string_view{p.layerName}; };

    const auto it = std::ranges::find_if(required, [&](const char* r) {
        return !std::ranges::contains(supported, std::string_view{r}, proj);
    });
    return it == required.end() ? nullptr : *it;
}

static const char* toSeverityLabel(const VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        return "error";
    }
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return "warning";
    }
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        return "info";
    }
    return "verbose";
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*)
{
    (void)messageType;

    cerr << "[vulkan][" << toSeverityLabel(messageSeverity) << "] "
         << (callbackData && callbackData->pMessage ? callbackData->pMessage : "no message") << '\n';
    return VK_FALSE;
}

static VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerCreateInfo() {
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };
}

static std::pair<VkInstance, VkDebugUtilsMessengerEXT> createInstance(const InstanceConfig config = {})
{
    static constexpr VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = APP_NAME.data(),
        .pEngineName = APP_NAME.data(),
        .apiVersion = VK_API_VERSION_1_3,
    };

    // Extensions (platform + optional debug)
    std::array<const char*, platformExtensions.size() + debugExtensions.size()> extensionNames{};
    auto extOut = std::ranges::copy(platformExtensions, extensionNames.begin()).out;
    if (config.enableDebug) extOut = std::ranges::copy(debugExtensions, extOut).out;
    const usize extensionCount = static_cast<usize>(extOut - extensionNames.begin());

    // Validate extensions
    {
        const std::span<const char* const> requested{extensionNames.data(), extensionCount};
        if (const char* missing = findMissingExtension(requested)) {
            std::string msg = "Missing required instance extension: ";
            msg += missing;
            runtime_assert(false, msg);
        }
    }

    // Layers (only if debug)
    std::array<const char*, validationLayers.size()> layerNames{};
    usize layerCount = 0;
    if (config.enableDebug) {
        auto layerOut = std::ranges::copy(validationLayers, layerNames.begin()).out;
        layerCount = static_cast<usize>(layerOut - layerNames.begin());

        const std::span<const char* const> requested{layerNames.data(), layerCount};
        if (const char* missing = findMissingLayer(requested)) {
            std::string msg = "Missing required validation layer: ";
            msg += missing;
            runtime_assert(false, msg);
        }
    }

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (config.enableDebug) {
        debugCreateInfo = makeDebugMessengerCreateInfo();
    }

    VkInstanceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = config.enableDebug ? &debugCreateInfo : nullptr,
        .flags = instanceFlags,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<u32>(layerCount),
        .ppEnabledLayerNames = layerCount ? layerNames.data() : nullptr,
        .enabledExtensionCount = static_cast<u32>(extensionCount),
        .ppEnabledExtensionNames = extensionNames.data(),
    };

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult instanceResult = vkCreateInstance(&createInfo, nullptr, &instance);
    runtime_assert(instanceResult == VK_SUCCESS, "Failed to create instance");

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    if (config.enableDebug) {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        runtime_assert(createMessenger != nullptr, "Failed to load vkCreateDebugUtilsMessengerEXT");

        const VkResult messengerResult = createMessenger(instance, &debugCreateInfo, nullptr, &debugMessenger);
        runtime_assert(messengerResult == VK_SUCCESS, "Failed to create debug messenger");
    }

    return {instance, debugMessenger};
}

static void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    if (instance == VK_NULL_HANDLE || messenger == VK_NULL_HANDLE) {
        return;
    }

    auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroyMessenger != nullptr) {
        destroyMessenger(instance, messenger, nullptr);
    }
}

static void destroyInstance(VkInstance instance)
{
    if (instance == VK_NULL_HANDLE) {
        return;
    }

    vkDestroyInstance(instance, nullptr);
}
