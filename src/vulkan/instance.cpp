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

struct Instance {
    VkInstance handle = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
};

static span<const VkExtensionProperties> enumerateInstanceExtensionProperties() {
    static array<VkExtensionProperties, 256> cache{};
    static u32 count = 0;
    static once_flag once;

    call_once(once, [] {
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, cache.data());
    });

    return {cache.data(), count};
}

static span<const VkLayerProperties> enumerateInstanceLayerProperties() {
    static array<VkLayerProperties, 64> cache{};
    static u32 count = 0;
    static once_flag once;

    call_once(once, [] {
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        vkEnumerateInstanceLayerProperties(&count, cache.data());
    });

    return {cache.data(), count};
}

static const char* findMissingExtension(span<const char* const> required) {
    const auto supported = enumerateInstanceExtensionProperties();
    const auto missing = std::ranges::find_if(required, [&](const char* requiredName) {
        return !std::ranges::any_of(supported, [&](const VkExtensionProperties& property) {
            return string_view(property.extensionName) == requiredName;
        });
    });
    return missing == required.end() ? nullptr : *missing;
}

static const char* findMissingLayer(span<const char* const> required) {
    const auto supported = enumerateInstanceLayerProperties();
    const auto missing = std::ranges::find_if(required, [&](const char* requiredName) {
        return !std::ranges::any_of(supported, [&](const VkLayerProperties& property) {
            return string_view(property.layerName) == requiredName;
        });
    });
    return missing == required.end() ? nullptr : *missing;
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

static Instance createInstance(const InstanceConfig config = {})
{
    static constexpr VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = APP_NAME.data(),
        .pEngineName = APP_NAME.data(),
        .apiVersion = VK_API_VERSION_1_3,
    };

    array<const char*, platformExtensions.size() + debugExtensions.size()> extensionNames{};
    usize extensionCount = 0;
    const auto appendExtensions = [&](const auto& names) {
        for (const char* name : names) {
            extensionNames[extensionCount++] = name;
        }
    };
    appendExtensions(platformExtensions);
    if (config.enableDebug) {
        appendExtensions(debugExtensions);
    }

    const span<const char* const> requestedExtensions{extensionNames.data(), extensionCount};
    if (const char* missingExt = findMissingExtension(requestedExtensions)) {
        string message = "Missing required instance extension: ";
        message += missingExt;
        runtime_assert(false, message);
    }

    array<const char*, validationLayers.size()> layerNames{};
    usize layerCount = 0;
    if (config.enableDebug) {
        for (const char* layer : validationLayers) {
            layerNames[layerCount++] = layer;
        }

        const span<const char* const> requestedLayers{layerNames.data(), layerCount};
        if (const char* missingLayer = findMissingLayer(requestedLayers)) {
            string message = "Missing required validation layer: ";
            message += missingLayer;
            runtime_assert(false, message);
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

    Instance instance{};
    const VkResult instanceResult = vkCreateInstance(&createInfo, nullptr, &instance.handle);
    runtime_assert(instanceResult == VK_SUCCESS, "Failed to create instance");

    if (config.enableDebug) {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance.handle, "vkCreateDebugUtilsMessengerEXT"));
        runtime_assert(createMessenger != nullptr, "Failed to load vkCreateDebugUtilsMessengerEXT");

        const VkResult messengerResult = createMessenger(instance.handle, &debugCreateInfo, nullptr, &instance.debugMessenger);
        runtime_assert(messengerResult == VK_SUCCESS, "Failed to create debug messenger");
    }

    return instance;
}

static void destroyInstance(const Instance& instance)
{
    if (instance.handle == VK_NULL_HANDLE) {
        return;
    }

    if (instance.debugMessenger != VK_NULL_HANDLE) {
        auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance.handle, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger != nullptr) {
            destroyMessenger(instance.handle, instance.debugMessenger, nullptr);
        }
    }

    vkDestroyInstance(instance.handle, nullptr);
}
