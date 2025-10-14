#include "types.cpp"
#include "assert.cpp"
#include "defer.cpp"

#include "vulkan/headers.cpp"
#include "vulkan/instance.cpp"

int main()
{
    VkInstance instance = create_instance();
    defer { vkDestroyInstance(instance, nullptr); };


}