#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#endif
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gradient_comp_spv.h"
#include "platform.h"

#define MAX_SWAP_IMAGES 3u
#define FRAMES_IN_FLIGHT 1u
#define COMPUTE_TILE_SIZE 8u
#define MAX_PACKED_SPHERES 128u

static const char* APPLICATION_NAME = "greatbadbeyond";

#if defined(_WIN32)
static const char* const INSTANCE_EXTS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
static const VkInstanceCreateFlags INSTANCE_FLAGS = 0u;
static const char* const DEVICE_EXTS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
#elif defined(__APPLE__)
static const char* const INSTANCE_EXTS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
static const VkInstanceCreateFlags INSTANCE_FLAGS = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
static const char* const DEVICE_EXTS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    "VK_KHR_portability_subset",
};
#else
#error Unsupported platform
#endif

static VkInstance instance = VK_NULL_HANDLE;
static VkSurfaceKHR surface = VK_NULL_HANDLE;
static VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
static VkDevice device = VK_NULL_HANDLE;
static VkQueue queue = VK_NULL_HANDLE;
static VkSwapchainKHR swapchain = VK_NULL_HANDLE;
static VkExtent2D swapExtent = {0u, 0u};
static VkImage swapImages[MAX_SWAP_IMAGES];
static VkImageView swapImageViews[MAX_SWAP_IMAGES];
static VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet descriptorSets[MAX_SWAP_IMAGES];
static VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
static VkPipeline pipeline = VK_NULL_HANDLE;
static VkCommandPool commandPool = VK_NULL_HANDLE;
static VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
static VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
static VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
static VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
static VkFence inFlightFence = VK_NULL_HANDLE;
static VkBuffer sphereBuffer = VK_NULL_HANDLE;
static VkDeviceMemory sphereBufferMemory = VK_NULL_HANDLE;
static VkDeviceSize sphereBufferSize = 0u;
static uint32_t packedSphereWords[MAX_PACKED_SPHERES * 2u];
static uint32_t packedSphereCount = 0u;

static const float SCENE_MIN[3] = {-18.0f, 0.0f, -18.0f};
static const float SCENE_EXTENT[3] = {36.0f, 8.0f, 36.0f};
static const float SPHERE_RADIUS_MIN = 0.22f;
static const float SPHERE_RADIUS_MAX = 0.85f;

typedef struct ScenePushConstants {
    float origin[4];
    float forward_fov[4];
    float scene_min[4];
    float scene_extent[4];
    float radius_min_max[4];
    uint32_t counts[4];
} ScenePushConstants;

static float clampf01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static uint32_t nextRandom(uint32_t *state)
{
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static float random01(uint32_t *state)
{
    return (float)((nextRandom(state) >> 8u) & 0x00ffffffu) * (1.0f / 16777215.0f);
}

static uint32_t quantizeUnorm16(float v)
{
    return (uint32_t)floorf(clampf01(v) * 65535.0f + 0.5f);
}

static float dequantizeUnorm16(uint32_t q)
{
    return (float)q * (1.0f / 65535.0f);
}

static uint32_t quantizeRadius12(float radius)
{
    float range = fmaxf(SPHERE_RADIUS_MAX - SPHERE_RADIUS_MIN, 1e-6f);
    float radiusNorm = clampf01((radius - SPHERE_RADIUS_MIN) / range);
    float encoded = sqrtf(radiusNorm);
    return (uint32_t)floorf(encoded * 4095.0f + 0.5f);
}

static float dequantizeRadius12(uint32_t q)
{
    float range = SPHERE_RADIUS_MAX - SPHERE_RADIUS_MIN;
    float encoded = (float)q * (1.0f / 4095.0f);
    return SPHERE_RADIUS_MIN + (encoded * encoded) * range;
}

static uint32_t findMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags requiredFlags)
{
    VkPhysicalDeviceMemoryProperties memoryProperties = {0};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0u; i < memoryProperties.memoryTypeCount; ++i)
    {
        if (((typeBits & (1u << i)) != 0u) &&
            ((memoryProperties.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags))
        {
            return i;
        }
    }
    return 0u;
}

static void buildPackedSpheres(void)
{
    float placedCenterX[MAX_PACKED_SPHERES];
    float placedCenterY[MAX_PACKED_SPHERES];
    float placedCenterZ[MAX_PACKED_SPHERES];
    float placedRadius[MAX_PACKED_SPHERES];

    packedSphereCount = 0u;
    uint32_t rng = 0x1f2e3d4cu;
    for (uint32_t i = 0u; i < MAX_PACKED_SPHERES; ++i)
    {
        uint32_t placed = 0u;
        for (uint32_t attempt = 0u; attempt < 64u; ++attempt)
        {
            float radiusMix = random01(&rng);
            float radius = SPHERE_RADIUS_MIN + (SPHERE_RADIUS_MAX - SPHERE_RADIUS_MIN) * (0.25f + 0.75f * radiusMix);

            float minX = SCENE_MIN[0] + radius;
            float maxX = SCENE_MIN[0] + SCENE_EXTENT[0] - radius;
            float minZ = SCENE_MIN[2] + radius;
            float maxZ = SCENE_MIN[2] + SCENE_EXTENT[2] - radius;
            if ((maxX <= minX) || (maxZ <= minZ)) continue;

            float centerX = minX + (maxX - minX) * random01(&rng);
            float centerY = SCENE_MIN[1] + radius;
            float centerZ = minZ + (maxZ - minZ) * random01(&rng);
            uint32_t materialId = nextRandom(&rng) % 3u;

            uint32_t qx = quantizeUnorm16((centerX - SCENE_MIN[0]) / SCENE_EXTENT[0]);
            uint32_t qy = quantizeUnorm16((centerY - SCENE_MIN[1]) / SCENE_EXTENT[1]);
            uint32_t qz = quantizeUnorm16((centerZ - SCENE_MIN[2]) / SCENE_EXTENT[2]);
            uint32_t qRadius = quantizeRadius12(radius);

            float decodedX = SCENE_MIN[0] + dequantizeUnorm16(qx) * SCENE_EXTENT[0];
            float decodedY = SCENE_MIN[1] + dequantizeUnorm16(qy) * SCENE_EXTENT[1];
            float decodedZ = SCENE_MIN[2] + dequantizeUnorm16(qz) * SCENE_EXTENT[2];
            float decodedRadius = dequantizeRadius12(qRadius);

            uint32_t overlap = 0u;
            for (uint32_t s = 0u; s < packedSphereCount; ++s)
            {
                float dx = decodedX - placedCenterX[s];
                float dy = decodedY - placedCenterY[s];
                float dz = decodedZ - placedCenterZ[s];
                float minDist = decodedRadius + placedRadius[s] + 0.03f;
                if ((dx * dx + dy * dy + dz * dz) < (minDist * minDist))
                {
                    overlap = 1u;
                    break;
                }
            }
            if (overlap != 0u) continue;

            uint32_t base = packedSphereCount * 2u;
            packedSphereWords[base + 0u] = (qx & 0xffffu) | ((qy & 0xffffu) << 16u);
            packedSphereWords[base + 1u] = (qz & 0xffffu) | ((qRadius & 0x0fffu) << 16u) | ((materialId & 0x0fu) << 28u);

            placedCenterX[packedSphereCount] = decodedX;
            placedCenterY[packedSphereCount] = decodedY;
            placedCenterZ[packedSphereCount] = decodedZ;
            placedRadius[packedSphereCount] = decodedRadius;
            packedSphereCount += 1u;
            placed = 1u;
            break;
        }
        if ((placed == 0u) && (packedSphereCount >= 32u)) break;
    }

    if (packedSphereCount == 0u)
    {
        uint32_t qx = quantizeUnorm16((0.0f - SCENE_MIN[0]) / SCENE_EXTENT[0]);
        uint32_t qy = quantizeUnorm16((0.8f - SCENE_MIN[1]) / SCENE_EXTENT[1]);
        uint32_t qz = quantizeUnorm16((-6.0f - SCENE_MIN[2]) / SCENE_EXTENT[2]);
        uint32_t qRadius = quantizeRadius12(0.8f);
        packedSphereWords[0] = (qx & 0xffffu) | ((qy & 0xffffu) << 16u);
        packedSphereWords[1] = (qz & 0xffffu) | ((qRadius & 0x0fffu) << 16u);
        packedSphereCount = 1u;
    }
}

int main(void)
{
    gbbInitWindow(1280u, 720u, APPLICATION_NAME);

    vkCreateInstance(&(VkInstanceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = INSTANCE_FLAGS,
        .pApplicationInfo = &(VkApplicationInfo){
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = APPLICATION_NAME,
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = APPLICATION_NAME,
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = VK_API_VERSION_1_3,
        },
        .enabledExtensionCount = (uint32_t)(sizeof(INSTANCE_EXTS) / sizeof(*INSTANCE_EXTS)),
        .ppEnabledExtensionNames = INSTANCE_EXTS,
    }, NULL, &instance);

#if defined(_WIN32)
    vkCreateWin32SurfaceKHR(instance, &(VkWin32SurfaceCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandleA(NULL),
        .hwnd = (HWND)window_handle,
    }, NULL, &surface);
#elif defined(__APPLE__)
    vkCreateMetalSurfaceEXT(instance, &(VkMetalSurfaceCreateInfoEXT){
        .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pLayer = surface_layer,
    }, NULL, &surface);
#endif

    uint32_t deviceCount = 1u;
    vkEnumeratePhysicalDevices(instance, &deviceCount, &physicalDevice);

    float priority = 1.0f;
    vkCreateDevice(physicalDevice, &(VkDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &(VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0u,
            .queueCount = 1u,
            .pQueuePriorities = &priority,
        },
        .enabledExtensionCount = (uint32_t)(sizeof(DEVICE_EXTS) / sizeof(*DEVICE_EXTS)),
        .ppEnabledExtensionNames = DEVICE_EXTS,
    }, NULL, &device);

    vkGetDeviceQueue(device, 0u, 0u, &queue);
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
    const float timestampPeriodNs = deviceProps.limits.timestampPeriod;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
    swapExtent = caps.currentExtent;
    uint32_t swapchainMinImageCount = 3u;
    if (swapchainMinImageCount < caps.minImageCount) swapchainMinImageCount = caps.minImageCount;
    if ((caps.maxImageCount != 0u) && (swapchainMinImageCount > caps.maxImageCount)) swapchainMinImageCount = caps.maxImageCount;

    vkCreateSwapchainKHR(device, &(VkSwapchainCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = swapchainMinImageCount,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = swapExtent,
        .imageArrayLayers = 1u,
        .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    }, NULL, &swapchain);

    uint32_t swapImageCount = 0u;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, NULL);

    if (swapImageCount > MAX_SWAP_IMAGES) return 1;

    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);

    buildPackedSpheres();
    sphereBufferSize = (VkDeviceSize)(packedSphereCount * 2u * sizeof(uint32_t));
    vkCreateBuffer(device, &(VkBufferCreateInfo){
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sphereBufferSize,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    }, NULL, &sphereBuffer);
    VkMemoryRequirements sphereMemoryRequirements = {0};
    vkGetBufferMemoryRequirements(device, sphereBuffer, &sphereMemoryRequirements);
    vkAllocateMemory(device, &(VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = sphereMemoryRequirements.size,
        .memoryTypeIndex = findMemoryTypeIndex(
            sphereMemoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    }, NULL, &sphereBufferMemory);
    vkBindBufferMemory(device, sphereBuffer, sphereBufferMemory, 0u);
    void *mappedSphereWords = NULL;
    vkMapMemory(device, sphereBufferMemory, 0u, sphereBufferSize, 0u, &mappedSphereWords);
    memcpy(mappedSphereWords, packedSphereWords, (size_t)sphereBufferSize);
    vkUnmapMemory(device, sphereBufferMemory);

    VkDescriptorSetLayoutBinding descriptorBindings[2] = {
        {
            .binding = 0u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    vkCreateDescriptorSetLayout(device, &(VkDescriptorSetLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2u,
        .pBindings = descriptorBindings,
    }, NULL, &descriptorSetLayout);

    VkDescriptorPoolSize descriptorPoolSizes[2] = {
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = MAX_SWAP_IMAGES,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = MAX_SWAP_IMAGES,
        },
    };
    vkCreateDescriptorPool(device, &(VkDescriptorPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = MAX_SWAP_IMAGES,
        .poolSizeCount = 2u,
        .pPoolSizes = descriptorPoolSizes,
    }, NULL, &descriptorPool);

    VkDescriptorSetLayout setLayouts[MAX_SWAP_IMAGES] = {
        descriptorSetLayout,
        descriptorSetLayout,
        descriptorSetLayout,
    };
    vkAllocateDescriptorSets(device, &(VkDescriptorSetAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = MAX_SWAP_IMAGES,
        .pSetLayouts = setLayouts,
    }, descriptorSets);

    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0u,
        .size = sizeof(ScenePushConstants),
    };
    vkCreatePipelineLayout(device, &(VkPipelineLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1u,
        .pSetLayouts = &descriptorSetLayout,
        .pushConstantRangeCount = 1u,
        .pPushConstantRanges = &pushConstantRange,
    }, NULL, &pipelineLayout);

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = gradientCompSpv_size,
        .pCode = gradientCompSpv,
     }, NULL, &shaderModule);

    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u, &(VkComputePipelineCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main",
        },
        .layout = pipelineLayout,
        .basePipelineIndex = -1,
    }, NULL, &pipeline);

    vkDestroyShaderModule(device, shaderModule, NULL);

    vkCreateCommandPool(device, &(VkCommandPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0u,
    }, NULL, &commandPool);

    vkAllocateCommandBuffers(device, &(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    }, &commandBuffer);
    vkCreateQueryPool(device, &(VkQueryPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2u,
    }, NULL, &timestampQueryPool);

    VkImageSubresourceRange imageRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1u,
        .layerCount = 1u
    };

    for (uint32_t i = 0u; i < swapImageCount; i++)
    {
        vkCreateImageView(device, &(VkImageViewCreateInfo){
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1u,
                .layerCount = 1u,
            },
        }, NULL, &swapImageViews[i]);

        VkDescriptorImageInfo imageInfo = {
            .imageView = swapImageViews[i],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        VkDescriptorBufferInfo sphereBufferInfo = {
            .buffer = sphereBuffer,
            .offset = 0u,
            .range = sphereBufferSize,
        };
        VkWriteDescriptorSet writes[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 0u,
                .descriptorCount = 1u,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &imageInfo,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 1u,
                .descriptorCount = 1u,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &sphereBufferInfo,
            },
        };
        vkUpdateDescriptorSets(device, 2u, writes, 0u, NULL);
    }

    vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    }, NULL, &imageAvailableSemaphore);

    vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    }, NULL, &renderFinishedSemaphore);

    vkCreateFence(device, &(VkFenceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT
    }, NULL, &inFlightFence);

    float cameraFocus[3] = {0.0f, 0.0f, 0.0f};
    float cameraZoom = 26.0f;
    const float cameraYaw = 0.7853981634f;
    const float cameraPitch = -0.7853981634f;
    const float cameraFov = 0.2967059728f;
    const float cameraForwardX = sinf(cameraYaw) * cosf(cameraPitch);
    const float cameraForwardY = sinf(cameraPitch);
    const float cameraForwardZ = cosf(cameraYaw) * cosf(cameraPitch);
    const float forwardLenXZ = sqrtf(cameraForwardX * cameraForwardX + cameraForwardZ * cameraForwardZ);
    const float moveForwardX = cameraForwardX / fmaxf(forwardLenXZ, 1e-6f);
    const float moveForwardZ = cameraForwardZ / fmaxf(forwardLenXZ, 1e-6f);
    const float moveRightX = -moveForwardZ;
    const float moveRightZ = moveForwardX;

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    uint64_t last_time = gbbGetTimeNs();
    float frame_time_accum_ms = 0.0f;
    uint32_t frame_time_count = 0u;
    float gpu_time_accum_ms = 0.0f;
    uint32_t gpu_time_count = 0u;
    uint32_t has_gpu_timestamps = 0u;
    while (gbbPumpEventsOnce() == 0)
    {
        uint64_t now_time = gbbGetTimeNs();
        float delta_time = (float)(now_time - last_time) * 1e-9f;
        last_time = now_time;
        float delta_ms = delta_time * 1000.0f;
        frame_time_accum_ms += delta_ms;
        frame_time_count += 1u;
        if (frame_time_accum_ms >= 1000.0f)
        {
            float avg_ms = frame_time_accum_ms / (float)frame_time_count;
            float fps = 1000.0f / avg_ms;
            float avg_gpu_ms = (gpu_time_count > 0u) ? (gpu_time_accum_ms / (float)gpu_time_count) : 0.0f;
            printf("frame %.2f ms (%.1f FPS), gpu %.3f ms\n", avg_ms, fps, avg_gpu_ms);
            frame_time_accum_ms = 0.0f;
            frame_time_count = 0u;
            gpu_time_accum_ms = 0.0f;
            gpu_time_count = 0u;
        }

        vkWaitForFences(device, 1u, &inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1u, &inFlightFence);
        if (has_gpu_timestamps != 0u)
        {
            uint64_t timestamps[2] = {0u, 0u};
            vkGetQueryPoolResults(device, timestampQueryPool, 0u, 2u, sizeof(timestamps), timestamps, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            gpu_time_accum_ms += (float)(timestamps[1] - timestamps[0]) * timestampPeriodNs * 1e-6f;
            gpu_time_count += 1u;
        }

        uint32_t imageIndex = 0u;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        float wheelDelta = 0.0f;
        gbbConsumeMouseWheel(&wheelDelta);
        cameraZoom *= expf(-wheelDelta * 0.12f);
        cameraZoom = fmaxf(6.0f, fminf(80.0f, cameraZoom));

        const float move_speed = 8.0f + cameraZoom * 0.35f;

        const float moveForward = (float)gbbIsKeyDown(GBB_KEY_W) - (float)gbbIsKeyDown(GBB_KEY_S);
        const float moveRight = (float)gbbIsKeyDown(GBB_KEY_D) - (float)gbbIsKeyDown(GBB_KEY_A);
        float moveNorm = sqrtf(moveForward * moveForward + moveRight * moveRight);
        float moveForwardUnit = moveForward;
        float moveRightUnit = moveRight;
        if (moveNorm > 1e-6f)
        {
            moveForwardUnit /= moveNorm;
            moveRightUnit /= moveNorm;
        }
        cameraFocus[0] += (moveForwardX * moveForwardUnit + moveRightX * moveRightUnit) * move_speed * delta_time;
        cameraFocus[2] += (moveForwardZ * moveForwardUnit + moveRightZ * moveRightUnit) * move_speed * delta_time;

        float cameraPositionX = cameraFocus[0] - cameraForwardX * cameraZoom;
        float cameraPositionY = cameraFocus[1] - cameraForwardY * cameraZoom;
        float cameraPositionZ = cameraFocus[2] - cameraForwardZ * cameraZoom;

        ScenePushConstants scenePush = {
            .origin = {cameraPositionX, cameraPositionY, cameraPositionZ, 0.0f},
            .forward_fov = {cameraForwardX, cameraForwardY, cameraForwardZ, cameraFov},
            .scene_min = {SCENE_MIN[0], SCENE_MIN[1], SCENE_MIN[2], 0.0f},
            .scene_extent = {SCENE_EXTENT[0], SCENE_EXTENT[1], SCENE_EXTENT[2], 0.0f},
            .radius_min_max = {SPHERE_RADIUS_MIN, SPHERE_RADIUS_MAX, 0.0f, 0.0f},
            .counts = {packedSphereCount, 0u, 0u, 0u},
        };

        vkResetCommandBuffer(commandBuffer, 0u);
        vkBeginCommandBuffer(commandBuffer, &(VkCommandBufferBeginInfo){
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        });
        vkCmdResetQueryPool(commandBuffer, timestampQueryPool, 0u, 2u);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampQueryPool, 0u);

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapImages[imageIndex],
            .subresourceRange = imageRange
        });

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0u, 1u, &descriptorSets[imageIndex], 0u, NULL);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(scenePush), &scenePush);
        vkCmdDispatch(commandBuffer, (swapExtent.width + COMPUTE_TILE_SIZE - 1u) / COMPUTE_TILE_SIZE,
                      (swapExtent.height + COMPUTE_TILE_SIZE - 1u) / COMPUTE_TILE_SIZE, 1u);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampQueryPool, 1u);

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapImages[imageIndex],
            .subresourceRange = imageRange
        });

        vkEndCommandBuffer(commandBuffer);

        vkQueueSubmit(queue, 1u, &(VkSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &imageAvailableSemaphore,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1u,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &renderFinishedSemaphore,
        }, inFlightFence);
        has_gpu_timestamps = 1u;

        vkQueuePresentKHR(queue, &(VkPresentInfoKHR){
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1u,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex,
        });
    }
    return 0;
}
