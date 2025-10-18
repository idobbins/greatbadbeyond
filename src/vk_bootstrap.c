#include "runtime.h"
#include "vk_bootstrap.h"

#include <string.h>

static const char *const vulkanValidationLayers[] = {
    "VK_LAYER_KHRONOS_validation",
};

typedef struct VulkanInstanceConfig {
    const char *extensions[VULKAN_MAX_ENABLED_EXTENSIONS];
    uint32_t extensionCount;
    const char *layers[VULKAN_MAX_ENABLED_LAYERS];
    uint32_t layerCount;
    VkInstanceCreateFlags flags;
    bool debugExtensionEnabled;
} VulkanInstanceConfig;

static void PushUniqueString(const char **list, uint32_t *count, uint32_t capacity, const char *value)
{
    for (uint32_t index = 0; index < *count; index++)
    {
        if (strcmp(list[index], value) == 0)
        {
            return;
        }
    }

    Assert(*count < capacity, "Too many Vulkan instance entries requested");
    list[(*count)++] = value;
}

static VkDebugUtilsMessengerCreateInfoEXT VulkanMakeDebugMessengerCreateInfo(void)
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {
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
        .pfnUserCallback = NULL,
        .pUserData = NULL,
    };

    return createInfo;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
    void *userData)
{
    (void)messageType;
    (void)userData;

    const char *message = (callbackData != NULL && callbackData->pMessage != NULL) ? callbackData->pMessage : "no message";

    if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
    {
        LogError("[vulkan] %s", message);
    }
    else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
    {
        LogWarn("[vulkan] %s", message);
    }
    else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
    {
        LogInfo("[vulkan] %s", message);
    }
    else
    {
        LogInfo("[vulkan][verbose] %s", message);
    }

    return VK_FALSE;
}

static VulkanInstanceConfig VulkanBuildInstanceConfig(bool requestDebug)
{
    VulkanInstanceConfig config = {
        .extensions = { 0 },
        .extensionCount = 0,
        .layers = { 0 },
        .layerCount = 0,
        .flags = 0,
        .debugExtensionEnabled = false,
    };

    uint32_t requiredExtensionCount = 0;
    const char **requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredExtensionCount);
    Assert(requiredExtensions != NULL, "glfwGetRequiredInstanceExtensions returned NULL");
    Assert(requiredExtensionCount > 0, "GLFW did not report any required Vulkan instance extensions");

    for (uint32_t index = 0; index < requiredExtensionCount; index++)
    {
        const char *name = requiredExtensions[index];
        PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), name);
    }

    if (requestDebug)
    {
        PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        config.debugExtensionEnabled = true;
    }

#if defined(__APPLE__)
    PushUniqueString(config.extensions, &config.extensionCount, ARRAY_SIZE(config.extensions), VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    config.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    if (requestDebug)
    {
        for (uint32_t index = 0; index < ARRAY_SIZE(vulkanValidationLayers); index++)
        {
            PushUniqueString(config.layers, &config.layerCount, ARRAY_SIZE(config.layers), vulkanValidationLayers[index]);
        }
    }

    return config;
}

static void VulkanCreateInstance(const VulkanInstanceConfig *config, const VkApplicationInfo *appInfo)
{
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 0 };
    const void *next = NULL;
    if (config->debugExtensionEnabled)
    {
        debugCreateInfo = VulkanMakeDebugMessengerCreateInfo();
        debugCreateInfo.pfnUserCallback = VulkanDebugCallback;
        next = &debugCreateInfo;
    }

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = next,
        .pApplicationInfo = appInfo,
        .flags = config->flags,
        .enabledExtensionCount = config->extensionCount,
        .ppEnabledExtensionNames = (config->extensionCount > 0) ? config->extensions : NULL,
        .enabledLayerCount = config->layerCount,
        .ppEnabledLayerNames = (config->layerCount > 0) ? config->layers : NULL,
    };

    VkResult result = vkCreateInstance(&createInfo, NULL, &GLOBAL.Vulkan.instance);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan instance");

    GLOBAL.Vulkan.validationLayersEnabled = (config->layerCount > 0);
}

static void VulkanSetupDebugMessenger(bool debugExtensionEnabled)
{
    if (!debugExtensionEnabled)
    {
        return;
    }

    PFN_vkCreateDebugUtilsMessengerEXT createMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(GLOBAL.Vulkan.instance, "vkCreateDebugUtilsMessengerEXT");

    if (createMessenger == NULL)
    {
        LogWarn("vkCreateDebugUtilsMessengerEXT not available; debug messenger disabled");
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo = VulkanMakeDebugMessengerCreateInfo();
    createInfo.pfnUserCallback = VulkanDebugCallback;

    VkResult result = createMessenger(GLOBAL.Vulkan.instance, &createInfo, NULL, &GLOBAL.Vulkan.debugMessenger);
    if (result == VK_SUCCESS)
    {
        GLOBAL.Vulkan.debugEnabled = true;
    }
    else
    {
        LogWarn("Failed to create Vulkan debug messenger (error %d)", result);
    }
}

static void VulkanCreateSurface(void)
{
    VkResult surfaceResult = glfwCreateWindowSurface(GLOBAL.Vulkan.instance, GLOBAL.Window.window, NULL, &GLOBAL.Vulkan.surface);
    Assert(surfaceResult == VK_SUCCESS, "Failed to create Vulkan surface");
}

static void VulkanResetState(void)
{
    GLOBAL.Vulkan.instance = VK_NULL_HANDLE;
    GLOBAL.Vulkan.debugMessenger = VK_NULL_HANDLE;
    GLOBAL.Vulkan.surface = VK_NULL_HANDLE;
    GLOBAL.Vulkan.physicalDevice = VK_NULL_HANDLE;
    GLOBAL.Vulkan.device = VK_NULL_HANDLE;
    GLOBAL.Vulkan.queue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.queueFamily = UINT32_MAX;
    GLOBAL.Vulkan.swapchain = VK_NULL_HANDLE;
    memset(GLOBAL.Vulkan.swapchainImages, 0, sizeof(GLOBAL.Vulkan.swapchainImages));
    memset(GLOBAL.Vulkan.swapchainImageViews, 0, sizeof(GLOBAL.Vulkan.swapchainImageViews));
    GLOBAL.Vulkan.swapchainImageCount = 0;
    GLOBAL.Vulkan.swapchainImageFormat = VK_FORMAT_UNDEFINED;
    GLOBAL.Vulkan.swapchainExtent.width = 0;
    GLOBAL.Vulkan.swapchainExtent.height = 0;
    GLOBAL.Vulkan.primaryIntersectSM = VK_NULL_HANDLE;
    GLOBAL.Vulkan.shadeShadowSM = VK_NULL_HANDLE;
    GLOBAL.Vulkan.blitVertexShaderModule = VK_NULL_HANDLE;
    GLOBAL.Vulkan.blitFragmentShaderModule = VK_NULL_HANDLE;
    GLOBAL.Vulkan.descriptorSetLayout = VK_NULL_HANDLE;
    GLOBAL.Vulkan.descriptorPool = VK_NULL_HANDLE;
    GLOBAL.Vulkan.descriptorSet = VK_NULL_HANDLE;
    GLOBAL.Vulkan.computePipelineLayout = VK_NULL_HANDLE;
    GLOBAL.Vulkan.blitPipelineLayout = VK_NULL_HANDLE;
    GLOBAL.Vulkan.primaryIntersectPipe = VK_NULL_HANDLE;
    GLOBAL.Vulkan.shadeShadowPipe = VK_NULL_HANDLE;
    GLOBAL.Vulkan.blitPipeline = VK_NULL_HANDLE;
    GLOBAL.Vulkan.vma = NULL;
    GLOBAL.Vulkan.commandPool = VK_NULL_HANDLE;
    memset(GLOBAL.Vulkan.commandBuffers, 0, sizeof(GLOBAL.Vulkan.commandBuffers));
    GLOBAL.Vulkan.gradientImage = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientAlloc = NULL;
    GLOBAL.Vulkan.gradientImageView = VK_NULL_HANDLE;
    GLOBAL.Vulkan.gradientSampler = VK_NULL_HANDLE;
    memset(GLOBAL.Vulkan.imageAvailableSemaphores, 0, sizeof(GLOBAL.Vulkan.imageAvailableSemaphores));
    memset(GLOBAL.Vulkan.renderFinishedSemaphores, 0, sizeof(GLOBAL.Vulkan.renderFinishedSemaphores));
    memset(GLOBAL.Vulkan.inFlightFences, 0, sizeof(GLOBAL.Vulkan.inFlightFences));
    memset(GLOBAL.Vulkan.imagesInFlight, 0, sizeof(GLOBAL.Vulkan.imagesInFlight));
    GLOBAL.Vulkan.rt = (VulkanBuffers){ 0 };
    memset(GLOBAL.Vulkan.sphereCRHost, 0, sizeof(GLOBAL.Vulkan.sphereCRHost));
    memset(GLOBAL.Vulkan.sphereAlbHost, 0, sizeof(GLOBAL.Vulkan.sphereAlbHost));
    GLOBAL.Vulkan.groundY = 0.0f;
    GLOBAL.Vulkan.worldMinX = 0.0f;
    GLOBAL.Vulkan.worldMinZ = 0.0f;
    GLOBAL.Vulkan.worldMaxX = 0.0f;
    GLOBAL.Vulkan.worldMaxZ = 0.0f;
    GLOBAL.Vulkan.gridDimX = 0u;
    GLOBAL.Vulkan.gridDimY = 0u;
    GLOBAL.Vulkan.gridDimZ = 0u;
    GLOBAL.Vulkan.gridMinX = 0.0f;
    GLOBAL.Vulkan.gridMinY = 0.0f;
    GLOBAL.Vulkan.gridMinZ = 0.0f;
    GLOBAL.Vulkan.gridInvCellX = 0.0f;
    GLOBAL.Vulkan.gridInvCellY = 0.0f;
    GLOBAL.Vulkan.gridInvCellZ = 0.0f;
    GLOBAL.Vulkan.showGrid = false;
    GLOBAL.Vulkan.vendorId = 0;
    GLOBAL.Vulkan.subgroupSize = 0;
    GLOBAL.Vulkan.computeLocalSizeX = VULKAN_COMPUTE_LOCAL_SIZE;
    GLOBAL.Vulkan.computeLocalSizeY = VULKAN_COMPUTE_LOCAL_SIZE;
    GLOBAL.Vulkan.gradientInitialized = false;
    GLOBAL.Vulkan.sceneInitialized = false;
    GLOBAL.Vulkan.resetAccumulation = false;
    GLOBAL.Vulkan.frameIndex = 0;
    GLOBAL.Vulkan.currentFrame = 0;
    GLOBAL.Vulkan.sphereCount = 0;
    GLOBAL.Vulkan.ready = false;
    GLOBAL.Vulkan.debugEnabled = false;
    GLOBAL.Vulkan.validationLayersEnabled = false;
}

static uint32_t VulkanEnumeratePhysicalDevices(VkInstance instance, VkPhysicalDevice *buffer, uint32_t capacity)
{
    uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, NULL);
    Assert(result == VK_SUCCESS, "Failed to query Vulkan physical devices");
    Assert(count > 0, "No Vulkan physical devices available");
    Assert(count <= capacity, "Too many Vulkan physical devices for buffer");

    result = vkEnumeratePhysicalDevices(instance, &count, buffer);
    Assert(result == VK_SUCCESS, "Failed to enumerate Vulkan physical devices");

    return count;
}

static bool FindUniversalQueue(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t *family)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    Assert(count > 0, "Vulkan physical device reports zero queue families");
    VkQueueFamilyProperties props[16];
    if (count > ARRAY_SIZE(props))
    {
        count = ARRAY_SIZE(props);
    }
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

    for (uint32_t index = 0; index < count; index++)
    {
        VkBool32 present = VK_FALSE;
        VkResult presentResult = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
        Assert(presentResult == VK_SUCCESS, "Failed to query Vulkan surface support");

        if ((present == VK_TRUE) &&
            ((props[index].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0) &&
            props[index].queueCount > 0)
        {
            *family = index;
            return true;
        }
    }

    return false;
}

static void VulkanCacheDeviceCapabilities(VkPhysicalDevice device)
{
    VkPhysicalDeviceSubgroupProperties subgroup = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
    };

    VkPhysicalDeviceProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &subgroup,
    };

    vkGetPhysicalDeviceProperties2(device, &properties);

    GLOBAL.Vulkan.vendorId = properties.properties.vendorID;
    GLOBAL.Vulkan.subgroupSize = subgroup.subgroupSize;

    uint32_t sizeX = VULKAN_COMPUTE_LOCAL_SIZE;
    uint32_t sizeY = VULKAN_COMPUTE_LOCAL_SIZE;

    switch (GLOBAL.Vulkan.vendorId)
    {
        case 0x10DEu: /* NVIDIA */
        case 0x8086u: /* Intel */
        case 0x106Bu: /* Apple */
            sizeX = 8u;
            sizeY = 16u;
            break;
        case 0x1002u: /* AMD */
        case 0x1022u: /* AMD (older PCI IDs) */
            sizeX = 16u;
            sizeY = 16u;
            break;
        default:
            break;
    }

    const VkPhysicalDeviceLimits *limits = &properties.properties.limits;
    if (sizeX > limits->maxComputeWorkGroupSize[0])
    {
        sizeX = limits->maxComputeWorkGroupSize[0];
    }
    if (sizeY > limits->maxComputeWorkGroupSize[1])
    {
        sizeY = limits->maxComputeWorkGroupSize[1];
    }

    const uint32_t maxInvocations = limits->maxComputeWorkGroupInvocations;
    while ((sizeX * sizeY) > maxInvocations)
    {
        if (sizeY > 1u)
        {
            sizeY >>= 1;
        }
        else if (sizeX > 1u)
        {
            sizeX >>= 1;
        }
        else
        {
            break;
        }
    }

    if (sizeX == 0u)
    {
        sizeX = 1u;
    }
    if (sizeY == 0u)
    {
        sizeY = 1u;
    }

    const uint32_t totalInvocations = sizeX * sizeY;
    if ((GLOBAL.Vulkan.subgroupSize > 0u) && ((totalInvocations % GLOBAL.Vulkan.subgroupSize) != 0u))
    {
        LogWarn(
            "Selected compute workgroup size %ux%u is not aligned to subgroup size %u",
            sizeX,
            sizeY,
            GLOBAL.Vulkan.subgroupSize);
    }

    GLOBAL.Vulkan.computeLocalSizeX = sizeX;
    GLOBAL.Vulkan.computeLocalSizeY = sizeY;

    LogInfo(
        "Compute workgroup configured as %ux%u (subgroup %u, vendor 0x%04X)",
        sizeX,
        sizeY,
        GLOBAL.Vulkan.subgroupSize,
        GLOBAL.Vulkan.vendorId);
}

static void VulkanSelectPhysicalDevice(void)
{
    if (GLOBAL.Vulkan.physicalDevice != VK_NULL_HANDLE)
    {
        return;
    }

    VkPhysicalDevice devices[VULKAN_MAX_PHYSICAL_DEVICES];
    const uint32_t deviceCount = VulkanEnumeratePhysicalDevices(GLOBAL.Vulkan.instance, devices, ARRAY_SIZE(devices));

    for (uint32_t index = 0; index < deviceCount; index++)
    {
        VkPhysicalDevice candidate = devices[index];
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(candidate, &properties);

        uint32_t universalQueueFamily = UINT32_MAX;
        if (!FindUniversalQueue(candidate, GLOBAL.Vulkan.surface, &universalQueueFamily))
        {
            LogWarn("Skipping Vulkan physical device: %s (no universal queue)", properties.deviceName);
            continue;
        }

        GLOBAL.Vulkan.physicalDevice = candidate;
        GLOBAL.Vulkan.queueFamily = universalQueueFamily;

        VulkanCacheDeviceCapabilities(candidate);

        LogInfo("Selected Vulkan physical device: %s", properties.deviceName);
        return;
    }

    Assert(false, "Failed to find a suitable Vulkan physical device");
}

static void VulkanCreateLogicalDevice(void)
{
    if (GLOBAL.Vulkan.device != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.physicalDevice != VK_NULL_HANDLE, "Vulkan physical device is not selected");
    Assert(GLOBAL.Vulkan.queueFamily != UINT32_MAX, "Vulkan queue family is invalid");

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = GLOBAL.Vulkan.queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkPhysicalDeviceFeatures deviceFeatures = { 0 };

    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features13,
    };

    vkGetPhysicalDeviceFeatures2(GLOBAL.Vulkan.physicalDevice, &features2);
    Assert((features13.dynamicRendering == VK_TRUE) && (features13.synchronization2 == VK_TRUE), "Vulkan 1.3 features missing");

    const char *enabledDeviceExtensions[VULKAN_MAX_ENABLED_EXTENSIONS] = { 0 };
    uint32_t enabledDeviceExtensionCount = 0;
    PushUniqueString(enabledDeviceExtensions, &enabledDeviceExtensionCount, ARRAY_SIZE(enabledDeviceExtensions), VK_KHR_SWAPCHAIN_EXTENSION_NAME);

#if defined(__APPLE__)
    PushUniqueString(enabledDeviceExtensions, &enabledDeviceExtensionCount, ARRAY_SIZE(enabledDeviceExtensions), VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    Assert(enabledDeviceExtensionCount > 0, "No Vulkan device extensions configured");

    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = enabledDeviceExtensionCount,
        .ppEnabledExtensionNames = enabledDeviceExtensions,
        .pEnabledFeatures = &deviceFeatures,
        .pNext = &features13,
    };

    if (GLOBAL.Vulkan.validationLayersEnabled)
    {
        createInfo.enabledLayerCount = ARRAY_SIZE(vulkanValidationLayers);
        createInfo.ppEnabledLayerNames = vulkanValidationLayers;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.ppEnabledLayerNames = NULL;
    }

    VkResult result = vkCreateDevice(GLOBAL.Vulkan.physicalDevice, &createInfo, NULL, &GLOBAL.Vulkan.device);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan logical device");

    vkGetDeviceQueue(GLOBAL.Vulkan.device, GLOBAL.Vulkan.queueFamily, 0, &GLOBAL.Vulkan.queue);

    LogInfo("Vulkan logical device ready");
}

static void VulkanCreateCommandPool(void)
{
    if (GLOBAL.Vulkan.commandPool != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");
    Assert(GLOBAL.Vulkan.queueFamily != UINT32_MAX, "Vulkan queue family is invalid");

    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = GLOBAL.Vulkan.queueFamily,
    };

    VkResult result = vkCreateCommandPool(GLOBAL.Vulkan.device, &poolInfo, NULL, &GLOBAL.Vulkan.commandPool);
    Assert(result == VK_SUCCESS, "Failed to create Vulkan command pool");

    LogInfo("Vulkan command pool ready");
}

static void VulkanDestroyCommandPool(void)
{
    if (GLOBAL.Vulkan.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(GLOBAL.Vulkan.device, GLOBAL.Vulkan.commandPool, NULL);
        GLOBAL.Vulkan.commandPool = VK_NULL_HANDLE;
        memset(GLOBAL.Vulkan.commandBuffers, 0, sizeof(GLOBAL.Vulkan.commandBuffers));
    }
}

static void VulkanAllocateCommandBuffer(void)
{
    if (GLOBAL.Vulkan.commandBuffers[0] != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.commandPool != VK_NULL_HANDLE, "Vulkan command pool is not ready");

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = GLOBAL.Vulkan.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VULKAN_FRAMES_IN_FLIGHT,
    };

    VkResult result = vkAllocateCommandBuffers(GLOBAL.Vulkan.device, &allocInfo, GLOBAL.Vulkan.commandBuffers);
    Assert(result == VK_SUCCESS, "Failed to allocate Vulkan command buffers");
}

static void VulkanCreateSyncObjects(void)
{
    bool ready = true;
    for (uint32_t index = 0; index < VULKAN_FRAMES_IN_FLIGHT; index++)
    {
        if ((GLOBAL.Vulkan.imageAvailableSemaphores[index] == VK_NULL_HANDLE) ||
            (GLOBAL.Vulkan.renderFinishedSemaphores[index] == VK_NULL_HANDLE) ||
            (GLOBAL.Vulkan.inFlightFences[index] == VK_NULL_HANDLE))
        {
            ready = false;
            break;
        }
    }

    if (ready)
    {
        return;
    }

    Assert(GLOBAL.Vulkan.device != VK_NULL_HANDLE, "Vulkan logical device is not ready");

    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t index = 0; index < VULKAN_FRAMES_IN_FLIGHT; index++)
    {
        if (GLOBAL.Vulkan.imageAvailableSemaphores[index] == VK_NULL_HANDLE)
        {
            VkResult semaphoreResult = vkCreateSemaphore(GLOBAL.Vulkan.device, &semaphoreInfo, NULL, &GLOBAL.Vulkan.imageAvailableSemaphores[index]);
            Assert(semaphoreResult == VK_SUCCESS, "Failed to create Vulkan image-available semaphore");
        }

        if (GLOBAL.Vulkan.renderFinishedSemaphores[index] == VK_NULL_HANDLE)
        {
            VkResult semaphoreResult = vkCreateSemaphore(GLOBAL.Vulkan.device, &semaphoreInfo, NULL, &GLOBAL.Vulkan.renderFinishedSemaphores[index]);
            Assert(semaphoreResult == VK_SUCCESS, "Failed to create Vulkan render-finished semaphore");
        }

        if (GLOBAL.Vulkan.inFlightFences[index] == VK_NULL_HANDLE)
        {
            VkResult fenceResult = vkCreateFence(GLOBAL.Vulkan.device, &fenceInfo, NULL, &GLOBAL.Vulkan.inFlightFences[index]);
            Assert(fenceResult == VK_SUCCESS, "Failed to create Vulkan in-flight fence");
        }
    }

    GLOBAL.Vulkan.currentFrame = 0;

    LogInfo("Vulkan synchronization objects ready");
}

static void VulkanDestroySyncObjects(void)
{
    for (uint32_t index = 0; index < VULKAN_FRAMES_IN_FLIGHT; index++)
    {
        if (GLOBAL.Vulkan.inFlightFences[index] != VK_NULL_HANDLE)
        {
            vkDestroyFence(GLOBAL.Vulkan.device, GLOBAL.Vulkan.inFlightFences[index], NULL);
            GLOBAL.Vulkan.inFlightFences[index] = VK_NULL_HANDLE;
        }

        if (GLOBAL.Vulkan.imageAvailableSemaphores[index] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(GLOBAL.Vulkan.device, GLOBAL.Vulkan.imageAvailableSemaphores[index], NULL);
            GLOBAL.Vulkan.imageAvailableSemaphores[index] = VK_NULL_HANDLE;
        }

        if (GLOBAL.Vulkan.renderFinishedSemaphores[index] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(GLOBAL.Vulkan.device, GLOBAL.Vulkan.renderFinishedSemaphores[index], NULL);
            GLOBAL.Vulkan.renderFinishedSemaphores[index] = VK_NULL_HANDLE;
        }
    }

    GLOBAL.Vulkan.currentFrame = 0;
    memset(GLOBAL.Vulkan.imagesInFlight, 0, sizeof(GLOBAL.Vulkan.imagesInFlight));
}

static void VulkanCreateVmaAllocator(void)
{
    if (GLOBAL.Vulkan.vma != NULL)
    {
        return;
    }

    VmaAllocatorCreateInfo vmaInfo = {
        .physicalDevice = GLOBAL.Vulkan.physicalDevice,
        .device = GLOBAL.Vulkan.device,
        .instance = GLOBAL.Vulkan.instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };

    VkResult allocatorResult = vmaCreateAllocator(&vmaInfo, &GLOBAL.Vulkan.vma);
    Assert(allocatorResult == VK_SUCCESS, "Failed to create VMA allocator");
}

static void VulkanDestroyVmaAllocator(void)
{
    if (GLOBAL.Vulkan.vma != NULL)
    {
        vmaDestroyAllocator(GLOBAL.Vulkan.vma);
        GLOBAL.Vulkan.vma = NULL;
        GLOBAL.Vulkan.gradientAlloc = NULL;
    }
}

void VulkanInitCore(void)
{
    if (GLOBAL.Vulkan.instance != VK_NULL_HANDLE)
    {
        return;
    }

    Assert(GLOBAL.Glfw.ready, "GLFW is not initialized");
    Assert(GLOBAL.Glfw.vulkanSupported, "Vulkan is not supported");
    Assert(GLOBAL.Window.ready, "Window is not created");

    VulkanResetState();

    const bool requestDebug = (VULKAN_ENABLE_DEBUG != 0);
    VulkanInstanceConfig instanceConfig = VulkanBuildInstanceConfig(requestDebug);

    const char *applicationTitle = (GLOBAL.Window.title != NULL) ? GLOBAL.Window.title : "Callandor";
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationTitle,
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName = "",
        .engineVersion = VK_MAKE_VERSION(0, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VulkanCreateInstance(&instanceConfig, &appInfo);
    VulkanSetupDebugMessenger(instanceConfig.debugExtensionEnabled);
    VulkanCreateSurface();
    VulkanSelectPhysicalDevice();
    VulkanCreateLogicalDevice();
    VulkanCreateCommandPool();
    VulkanAllocateCommandBuffer();
    VulkanCreateSyncObjects();
    VulkanCreateVmaAllocator();

    LogInfo("Vulkan core ready");
}

void VulkanShutdownCore(void)
{
    if ((GLOBAL.Vulkan.instance == VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.device == VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.surface == VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.debugMessenger == VK_NULL_HANDLE))
    {
        return;
    }

    if (GLOBAL.Vulkan.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(GLOBAL.Vulkan.device);
    }

    VulkanDestroySyncObjects();
    VulkanDestroyCommandPool();
    VulkanDestroyVmaAllocator();

    if (GLOBAL.Vulkan.device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(GLOBAL.Vulkan.device, NULL);
        GLOBAL.Vulkan.device = VK_NULL_HANDLE;
    }

    GLOBAL.Vulkan.queue = VK_NULL_HANDLE;
    GLOBAL.Vulkan.queueFamily = UINT32_MAX;
    GLOBAL.Vulkan.physicalDevice = VK_NULL_HANDLE;

    if (GLOBAL.Vulkan.surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(GLOBAL.Vulkan.instance, GLOBAL.Vulkan.surface, NULL);
        GLOBAL.Vulkan.surface = VK_NULL_HANDLE;
    }

    if (GLOBAL.Vulkan.debugMessenger != VK_NULL_HANDLE)
    {
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(GLOBAL.Vulkan.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyMessenger != NULL)
        {
            destroyMessenger(GLOBAL.Vulkan.instance, GLOBAL.Vulkan.debugMessenger, NULL);
        }
        GLOBAL.Vulkan.debugMessenger = VK_NULL_HANDLE;
        GLOBAL.Vulkan.debugEnabled = false;
    }

    if (GLOBAL.Vulkan.instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(GLOBAL.Vulkan.instance, NULL);
        GLOBAL.Vulkan.instance = VK_NULL_HANDLE;
    }

    VulkanResetState();
}
