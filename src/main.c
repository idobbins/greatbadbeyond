#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#define VK_PORTABLE 1u
#else
#define VK_PORTABLE 0u
#endif

static const char *APPLICATION_NAME = "greatbadbeyond";
static const char *INSTANCE_EXTENSIONS[8] = { VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME };

static VkInstance instance = VK_NULL_HANDLE;

GLFWwindow *window = NULL;
VkSurfaceKHR surface = VK_NULL_HANDLE;

VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

static const char *DEVICE_EXTENSIONS[8] = { VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME };

int main(void)
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(1920, 1080, "greadbadbeyond", NULL, NULL);

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    for (uint32_t i = VK_PORTABLE; i < VK_PORTABLE + glfwExtensionCount; i++)
    {
        INSTANCE_EXTENSIONS[i] = glfwExtensions[i];
    }

    vkCreateInstance(&(VkInstanceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = VK_PORTABLE * VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        .pApplicationInfo = &(VkApplicationInfo){
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = APPLICATION_NAME,
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = APPLICATION_NAME,
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = VK_API_VERSION_1_3,
        },
        .enabledExtensionCount = VK_PORTABLE + glfwExtensionCount,
        .ppEnabledExtensionNames = INSTANCE_EXTENSIONS,
    }, NULL, &instance);

    glfwCreateWindowSurface(instance, window, NULL, &surface);

    // If your first physical device sucks, oh well.
    uint32_t physicalDeviceCount = 1;
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, &physicalDevice);
    DEVICE_EXTENSIONS[VK_PORTABLE] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    vkDestroyInstance(instance, NULL);
    glfwDestroyWindow(window);
    window = NULL;

    glfwTerminate();
    return 0;
}
