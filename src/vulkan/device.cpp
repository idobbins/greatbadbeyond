#pragma once

#include "types.cpp"
#include "assert.cpp"
#include "vulkan/headers.cpp"
#include "glfw/window.cpp" // Window + glfwCreateWindowSurface

#include <array>
#include <iostream>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>

using namespace std;

// --- Surface helpers ---------------------------------------------------------------------------

static VkSurfaceKHR createSurface(VkInstance instance, const Window& w) {
    runtime_assert(instance != VK_NULL_HANDLE, "createSurface: instance must be valid");
    runtime_assert(w.handle != nullptr,       "createSurface: window must be valid");

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult r = glfwCreateWindowSurface(instance, w.handle, nullptr, &surface);
    runtime_assert(r == VK_SUCCESS && surface != VK_NULL_HANDLE, "Failed to create window surface");
    return surface;
}

static void destroySurface(VkInstance instance, VkSurfaceKHR surface) {
    if (instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) return;
    vkDestroySurfaceKHR(instance, surface, nullptr);
}

// --- Device selection + creation ---------------------------------------------------------------

inline constexpr array<const char*, 1> baseDeviceExtensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

struct QueueFamilies {
    u32 graphics = UINT32_MAX;
    u32 present  = UINT32_MAX;
    bool complete() const { return graphics != UINT32_MAX && present != UINT32_MAX; }
};

struct Device {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice         logical  = VK_NULL_HANDLE;
    VkQueue          graphicsQueue = VK_NULL_HANDLE;
    VkQueue          presentQueue  = VK_NULL_HANDLE;
    u32              graphicsFamily = UINT32_MAX;
    u32              presentFamily  = UINT32_MAX;
};

static span<const VkPhysicalDevice> enumeratePhysicalDevices(VkInstance instance) {
    static array<VkPhysicalDevice, 16> cache{};
    static u32 count = 0;
    static once_flag once;

    call_once(once, [&] {
        VkResult r = vkEnumeratePhysicalDevices(instance, &count, nullptr);
        runtime_assert(r == VK_SUCCESS, "vkEnumeratePhysicalDevices (count) failed");
        runtime_assert(count > 0,       "No Vulkan-capable GPUs found");
        runtime_assert(count <= cache.size(), "Too many physical devices for cache");
        r = vkEnumeratePhysicalDevices(instance, &count, cache.data());
        runtime_assert(r == VK_SUCCESS, "vkEnumeratePhysicalDevices (fill) failed");
    });
    return {cache.data(), count};
}

static span<const VkExtensionProperties> enumerateDeviceExtensionProperties(VkPhysicalDevice dev) {
    // Note: single static buffer reused. Do not persist spans across calls.
    static array<VkExtensionProperties, 256> cache{};
    static u32 count = 0;

    VkResult r = vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
    runtime_assert(r == VK_SUCCESS, "vkEnumerateDeviceExtensionProperties (count) failed");
    runtime_assert(count <= cache.size(), "Too many device extensions for cache");
    r = vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, cache.data());
    runtime_assert(r == VK_SUCCESS, "vkEnumerateDeviceExtensionProperties (fill) failed");

    return {cache.data(), count};
}

static bool deviceHasExtension(VkPhysicalDevice dev, const char* name) {
    const auto supported = enumerateDeviceExtensionProperties(dev);
    auto proj = [](const VkExtensionProperties& p) { return string_view{p.extensionName}; };
    return ranges::contains(supported, string_view{name}, proj);
}

static QueueFamilies findQueueFamilies(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    QueueFamilies out{};

    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    runtime_assert(count > 0, "Device reports zero queue families");

    array<VkQueueFamilyProperties, 64> props{};
    runtime_assert(count <= props.size(), "Too many queue families for cache");
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props.data());

    for (u32 i = 0; i < count; ++i) {
        const bool supportsGraphics = (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;

        VkBool32 supportsPresent = VK_FALSE;
        if (surface != VK_NULL_HANDLE) {
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &supportsPresent);
        }

        if (supportsGraphics) out.graphics = (out.graphics == UINT32_MAX ? i : out.graphics);
        if (supportsPresent)  out.present  = (out.present  == UINT32_MAX ? i : out.present);

        if (out.complete()) break;
    }
    return out;
}

static bool swapchainAdequate(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    u32 nFormats = 0, nModes = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &nFormats, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &nModes, nullptr);
    // counts being zero implies unusable swapchain on this device/surface
    return nFormats > 0 && nModes > 0;
}

static int deviceRank(const VkPhysicalDeviceProperties& p) {
    switch (p.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 3;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
        default:                                     return 1;
    }
}

static pair<VkPhysicalDevice, QueueFamilies>
pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
{
    VkPhysicalDevice best = VK_NULL_HANDLE;
    QueueFamilies    bestQ{};

    int bestScore = -1;

    for (VkPhysicalDevice dev : enumeratePhysicalDevices(instance)) {
        // Required: graphics + present queues
        const QueueFamilies q = findQueueFamilies(dev, surface);
        if (!q.complete()) continue;

        // Required: swapchain extension
        if (!deviceHasExtension(dev, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

        // Required: swapchain formats + present modes available
        if (!swapchainAdequate(dev, surface)) continue;

        // Rank preference (discrete > integrated > others)
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);

        const int score = deviceRank(props);
        if (score > bestScore) { bestScore = score; best = dev; bestQ = q; }
    }

    runtime_assert(best != VK_NULL_HANDLE, "Failed to find a suitable GPU");
    return {best, bestQ};
}

static Device createDevice(VkInstance instance, VkSurfaceKHR surface) {
    runtime_assert(instance != VK_NULL_HANDLE, "createDevice: instance must be valid");
    runtime_assert(surface  != VK_NULL_HANDLE, "createDevice: surface must be valid");

    const auto [physical, q] = pickPhysicalDevice(instance, surface);

    // Unique queue families (1 if same, else 2)
    array<VkDeviceQueueCreateInfo, 2> queueInfos{};
    float priority = 1.0f;
    u32 qCount = 0;

    queueInfos[qCount++] = VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = q.graphics,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    if (q.present != q.graphics) {
        queueInfos[qCount++] = VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = q.present,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
    }

    // Extensions to enable (always swapchain; enable portability subset if the device exposes it)
    array<const char*, 8> extNames{};
    auto out = ranges::copy(baseDeviceExtensions, extNames.begin()).out;

#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (deviceHasExtension(physical, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        *out++ = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
    }
#endif
    const u32 extCount = static_cast<u32>(out - extNames.begin());

    VkPhysicalDeviceFeatures features{}; // keep minimal for now

    const VkDeviceCreateInfo ci{
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = qCount,
        .pQueueCreateInfos       = queueInfos.data(),
        .enabledLayerCount       = 0,                // device layers deprecated
        .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = extCount,
        .ppEnabledExtensionNames = extNames.data(),
        .pEnabledFeatures        = &features,
    };

    VkDevice logical = VK_NULL_HANDLE;
    const VkResult r = vkCreateDevice(physical, &ci, nullptr, &logical);
    runtime_assert(r == VK_SUCCESS && logical != VK_NULL_HANDLE, "Failed to create logical device");

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue  = VK_NULL_HANDLE;
    vkGetDeviceQueue(logical, q.graphics, 0, &graphicsQueue);
    vkGetDeviceQueue(logical, q.present,  0, &presentQueue);

    return {
        .physical       = physical,
        .logical        = logical,
        .graphicsQueue  = graphicsQueue,
        .presentQueue   = presentQueue,
        .graphicsFamily = q.graphics,
        .presentFamily  = q.present,
    };
}

static void destroyDevice(VkDevice device) {
    if (device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device); // be explicit; cheap and safe at this scale
    vkDestroyDevice(device, nullptr);
}
