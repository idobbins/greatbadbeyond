#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#define VK_PORTABLE 1u
#else
#define VK_PORTABLE 0u
#endif


static const char *APPLICATION_NAME = "greatbadbeyond";

#define MAX_INSTANCE_EXTENSIONS 8u
static const char *INSTANCE_EXTENSIONS[MAX_INSTANCE_EXTENSIONS] = { VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME };

static VkInstance instance = VK_NULL_HANDLE;

GLFWwindow *window = NULL;
VkSurfaceKHR surface = VK_NULL_HANDLE;

VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

#define MAX_DEVICE_EXTENSIONS 8u
static const char *DEVICE_EXTENSIONS[MAX_DEVICE_EXTENSIONS] = { VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME };

VkDevice device = VK_NULL_HANDLE;
VkQueue queue = VK_NULL_HANDLE;

VkSwapchainKHR swapchain = VK_NULL_HANDLE;

#define MAX_SWAPCHAIN_IMAGES 8u
VkImage swapImages[MAX_SWAPCHAIN_IMAGES];
VkImageView swapImageViews[MAX_SWAPCHAIN_IMAGES];
uint32_t swapImageCount = 0;

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

    float queuePriority = 1.0f;
    vkCreateDevice(physicalDevice, &(VkDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &(VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        },
        .enabledExtensionCount = 1u + VK_PORTABLE,
        .ppEnabledExtensionNames = DEVICE_EXTENSIONS,
    }, NULL, &device);
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkSurfaceCapabilitiesKHR surfaceCaps = { 0 };
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

    swapImageCount = surfaceCaps.minImageCount + (surfaceCaps.minImageCount < 2u);

    vkCreateSwapchainKHR(device, &(VkSwapchainCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = swapImageCount,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = surfaceCaps.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = surfaceCaps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    }, NULL, &swapchain);

    swapImageCount = MAX_SWAPCHAIN_IMAGES;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);

    for (uint32_t i = 0; i < swapImageCount; i++)
    {
        vkCreateImageView(device, &(VkImageViewCreateInfo){
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = (VkImageSubresourceRange){
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        }, NULL, &swapImageViews[i]);
    }

    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    for (uint32_t i = 0; i < swapImageCount; i++)
    {
        vkDestroyImageView(device, swapImageViews[i], NULL);
    }
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    glfwDestroyWindow(window);
    window = NULL;

    glfwTerminate();
    return 0;
}
