#include <vulkan/vulkan.h>

#if defined(__APPLE__)
#define VK_PORTABLE 1u
#else
#define VK_PORTABLE 0u
#endif

static const char *APPLICATION_NAME = "greatbadbeyond";
static const char *const INSTANCE_EXTENSIONS[] = { VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME };

static VkInstance instance = VK_NULL_HANDLE;

int main(void)
{
    vkCreateInstance(&(VkInstanceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = VK_PORTABLE * (1u << 0),
        .pApplicationInfo = &(VkApplicationInfo){
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = APPLICATION_NAME,
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = APPLICATION_NAME,
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = VK_API_VERSION_1_3,
        },
        .enabledExtensionCount = VK_PORTABLE,
        .ppEnabledExtensionNames = VK_PORTABLE ? INSTANCE_EXTENSIONS: NULL,
    }, NULL, &instance);

    vkDestroyInstance(instance, NULL);
    return 0;
}
