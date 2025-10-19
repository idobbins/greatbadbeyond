#include <callandor.h>
#include <config.h>
#include <runtime.h>

#include <vulkan/vulkan.h>

#include <array>
#include <mutex>

using namespace std;

span<const VkPhysicalDevice> Enumerate(const VkInstance &instance) {
    static array<VkPhysicalDevice, MaxPhysicalDevices> cache{};
    static uint32_t count = 0;
    static once_flag once;

    call_once(once, [&] {
        VkResult r = vkEnumeratePhysicalDevices(instance, &count, nullptr);
        Assert(r == VK_SUCCESS, "vkEnumeratePhysicalDevices (count) failed");
        Assert(count > 0,       "No Vulkan-capable GPUs found");
        Assert(count <= cache.size(), "Too many physical devices for cache");
        r = vkEnumeratePhysicalDevices(instance, &count, cache.data());
        Assert(r == VK_SUCCESS, "vkEnumeratePhysicalDevices (fill) failed");
    });

    return {cache.data(), count};
}

