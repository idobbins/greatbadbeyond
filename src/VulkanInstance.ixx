//====================================================================================
// Global module fragment
//====================================================================================
module;

#include <stdexcept>
#include <vulkan/vulkan.h>

//====================================================================================
// Module: vulkan.instance
//====================================================================================
export module vulkan.instance;

export struct VulkanInstance
{
    VkInstance handle {VK_NULL_HANDLE};

    VulkanInstance(const VkInstanceCreateInfo& info)
    {
        vkCreateInstance(&info, nullptr, &handle);
    }

    ~VulkanInstance()
    {
        ThrowIfHandleIsNull();

        vkDestroyInstance(handle, nullptr);
    }

    void ThrowIfHandleIsNull()
    {
        if (handle == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Handle is null when it should not be");
        }
    }
};
