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

#define SCENE_MAX_SPHERES 256u
#define SCENE_COARSE_DIM 16u
#define SCENE_FINE_DIM 4u
#define SCENE_FINE_CELLS_PER_COARSE (SCENE_FINE_DIM * SCENE_FINE_DIM)
#define SCENE_FINE_CELL_COUNT (SCENE_COARSE_DIM * SCENE_COARSE_DIM * SCENE_FINE_CELLS_PER_COARSE)
#define SCENE_MAX_FINE_REFS_PER_CELL 8u
#define SCENE_MAX_SPHERE_REFS (SCENE_FINE_CELL_COUNT * SCENE_MAX_FINE_REFS_PER_CELL)

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
static VkBuffer sceneBuffer = VK_NULL_HANDLE;
static VkDeviceMemory sceneBufferMemory = VK_NULL_HANDLE;

typedef struct CameraPushConstants {
    float focus_zoom[4];
    float params[4];
} CameraPushConstants;

typedef struct SceneHeaderGpu {
    uint32_t sphere_count;
    uint32_t overflow_count;
    uint32_t coarse_dim;
    uint32_t fine_dim;
    float world_min_x;
    float world_min_z;
    float world_max_x;
    float world_max_z;
    float coarse_cell_size_x;
    float coarse_cell_size_z;
    float fine_cell_size_x;
    float fine_cell_size_z;
} SceneHeaderGpu;

typedef struct SphereGpu {
    float center_radius[4];
    float color[4];
} SphereGpu;

typedef struct FineCellGpu {
    uint32_t count;
    uint32_t base_index;
    uint32_t _pad0;
    uint32_t _pad1;
} FineCellGpu;

typedef struct SceneBufferGpu {
    SceneHeaderGpu header;
    SphereGpu spheres[SCENE_MAX_SPHERES];
    FineCellGpu fine_cells[SCENE_FINE_CELL_COUNT];
    uint32_t sphere_indices[SCENE_MAX_SPHERE_REFS];
    uint32_t coarse_masks[SCENE_COARSE_DIM * SCENE_COARSE_DIM];
} SceneBufferGpu;

static uint32_t gbbFindMemoryType(uint32_t type_mask, VkMemoryPropertyFlags required_flags)
{
    VkPhysicalDeviceMemoryProperties memory_props = {0};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory_props);

    for (uint32_t i = 0u; i < memory_props.memoryTypeCount; ++i)
    {
        const uint32_t type_supported = (type_mask & (1u << i)) != 0u;
        const uint32_t flags_match = ((memory_props.memoryTypes[i].propertyFlags & required_flags) == required_flags);
        if (type_supported && flags_match) return i;
    }
    return UINT32_MAX;
}

static int32_t gbbClampInt32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void gbbBuildSceneBuffer(SceneBufferGpu* scene)
{
    if (!scene) return;

    memset(scene, 0, sizeof(*scene));

    scene->header.coarse_dim = SCENE_COARSE_DIM;
    scene->header.fine_dim = SCENE_FINE_DIM;
    scene->header.world_min_x = -24.0f;
    scene->header.world_min_z = -24.0f;
    scene->header.world_max_x = 24.0f;
    scene->header.world_max_z = 24.0f;
    scene->header.coarse_cell_size_x = (scene->header.world_max_x - scene->header.world_min_x) / (float)SCENE_COARSE_DIM;
    scene->header.coarse_cell_size_z = (scene->header.world_max_z - scene->header.world_min_z) / (float)SCENE_COARSE_DIM;
    scene->header.fine_cell_size_x = scene->header.coarse_cell_size_x / (float)SCENE_FINE_DIM;
    scene->header.fine_cell_size_z = scene->header.coarse_cell_size_z / (float)SCENE_FINE_DIM;

    for (uint32_t i = 0u; i < SCENE_FINE_CELL_COUNT; ++i)
    {
        scene->fine_cells[i].count = 0u;
        scene->fine_cells[i].base_index = i * SCENE_MAX_FINE_REFS_PER_CELL;
    }

    const uint32_t sphere_grid_dim = 10u;
    const float sphere_spacing = 1.6f;
    const float sphere_radius = 0.45f;
    const float sphere_half_grid = 0.5f * (float)(sphere_grid_dim - 1u);
    uint32_t sphere_count = 0u;

    for (uint32_t z = 0u; z < sphere_grid_dim; ++z)
    {
        for (uint32_t x = 0u; x < sphere_grid_dim; ++x)
        {
            if (sphere_count >= SCENE_MAX_SPHERES) break;

            SphereGpu* sphere = &scene->spheres[sphere_count];
            sphere->center_radius[0] = ((float)x - sphere_half_grid) * sphere_spacing;
            sphere->center_radius[1] = sphere_radius;
            sphere->center_radius[2] = ((float)z - sphere_half_grid) * sphere_spacing;
            sphere->center_radius[3] = sphere_radius;

            sphere->color[0] = 0.55f + 0.45f * cosf(6.28318530718f * (0.10f + (float)sphere_count * 0.071f));
            sphere->color[1] = 0.55f + 0.45f * cosf(6.28318530718f * (0.38f + (float)sphere_count * 0.113f));
            sphere->color[2] = 0.55f + 0.45f * cosf(6.28318530718f * (0.63f + (float)sphere_count * 0.173f));
            sphere->color[3] = 1.0f;

            sphere_count += 1u;
        }
    }
    scene->header.sphere_count = sphere_count;

    const uint32_t fine_grid_dim = SCENE_COARSE_DIM * SCENE_FINE_DIM;

    for (uint32_t sphere_index = 0u; sphere_index < sphere_count; ++sphere_index)
    {
        const SphereGpu* sphere = &scene->spheres[sphere_index];
        const float center_x = sphere->center_radius[0];
        const float center_z = sphere->center_radius[2];
        const float radius = sphere->center_radius[3];

        int32_t min_fine_x = (int32_t)floorf((center_x - radius - scene->header.world_min_x) / scene->header.fine_cell_size_x);
        int32_t max_fine_x = (int32_t)floorf((center_x + radius - scene->header.world_min_x) / scene->header.fine_cell_size_x);
        int32_t min_fine_z = (int32_t)floorf((center_z - radius - scene->header.world_min_z) / scene->header.fine_cell_size_z);
        int32_t max_fine_z = (int32_t)floorf((center_z + radius - scene->header.world_min_z) / scene->header.fine_cell_size_z);

        if ((max_fine_x < 0) || (max_fine_z < 0)) continue;
        if ((min_fine_x >= (int32_t)fine_grid_dim) || (min_fine_z >= (int32_t)fine_grid_dim)) continue;

        min_fine_x = gbbClampInt32(min_fine_x, 0, (int32_t)fine_grid_dim - 1);
        max_fine_x = gbbClampInt32(max_fine_x, 0, (int32_t)fine_grid_dim - 1);
        min_fine_z = gbbClampInt32(min_fine_z, 0, (int32_t)fine_grid_dim - 1);
        max_fine_z = gbbClampInt32(max_fine_z, 0, (int32_t)fine_grid_dim - 1);

        for (int32_t fine_z = min_fine_z; fine_z <= max_fine_z; ++fine_z)
        {
            for (int32_t fine_x = min_fine_x; fine_x <= max_fine_x; ++fine_x)
            {
                const uint32_t coarse_x = (uint32_t)fine_x / SCENE_FINE_DIM;
                const uint32_t coarse_z = (uint32_t)fine_z / SCENE_FINE_DIM;
                const uint32_t local_x = (uint32_t)fine_x % SCENE_FINE_DIM;
                const uint32_t local_z = (uint32_t)fine_z % SCENE_FINE_DIM;
                const uint32_t local_cell = local_z * SCENE_FINE_DIM + local_x;
                const uint32_t coarse_cell = coarse_z * SCENE_COARSE_DIM + coarse_x;
                const uint32_t fine_cell = coarse_cell * SCENE_FINE_CELLS_PER_COARSE + local_cell;

                scene->coarse_masks[coarse_cell] |= (1u << local_cell);

                FineCellGpu* cell = &scene->fine_cells[fine_cell];
                if (cell->count < SCENE_MAX_FINE_REFS_PER_CELL)
                {
                    const uint32_t write_index = cell->base_index + cell->count;
                    scene->sphere_indices[write_index] = sphere_index;
                    cell->count += 1u;
                }
                else
                {
                    scene->header.overflow_count += 1u;
                }
            }
        }
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

    SceneBufferGpu scene_data = {0};
    gbbBuildSceneBuffer(&scene_data);

    vkCreateBuffer(device, &(VkBufferCreateInfo){
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(SceneBufferGpu),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    }, NULL, &sceneBuffer);

    VkMemoryRequirements scene_memory_reqs = {0};
    vkGetBufferMemoryRequirements(device, sceneBuffer, &scene_memory_reqs);
    const uint32_t scene_memory_type = gbbFindMemoryType(
        scene_memory_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (scene_memory_type == UINT32_MAX) return 1;

    vkAllocateMemory(device, &(VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = scene_memory_reqs.size,
        .memoryTypeIndex = scene_memory_type,
    }, NULL, &sceneBufferMemory);
    vkBindBufferMemory(device, sceneBuffer, sceneBufferMemory, 0u);

    void* mapped_scene = NULL;
    vkMapMemory(device, sceneBufferMemory, 0u, sizeof(SceneBufferGpu), 0u, &mapped_scene);
    memcpy(mapped_scene, &scene_data, sizeof(SceneBufferGpu));
    vkUnmapMemory(device, sceneBufferMemory);

    if (scene_data.header.overflow_count != 0u)
    {
        printf("scene grid overflowed %u references (increase SCENE_MAX_FINE_REFS_PER_CELL)\n",
               scene_data.header.overflow_count);
    }

    VkDescriptorSetLayoutBinding descriptor_bindings[2] = {
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
        }
    };
    vkCreateDescriptorSetLayout(device, &(VkDescriptorSetLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2u,
        .pBindings = descriptor_bindings,
    }, NULL, &descriptorSetLayout);

    VkDescriptorPoolSize descriptor_pool_sizes[2] = {
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = MAX_SWAP_IMAGES,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = MAX_SWAP_IMAGES,
        }
    };
    vkCreateDescriptorPool(device, &(VkDescriptorPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = MAX_SWAP_IMAGES,
        .poolSizeCount = 2u,
        .pPoolSizes = descriptor_pool_sizes,
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
        .size = sizeof(CameraPushConstants),
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

        VkDescriptorImageInfo image_info = {
            .imageView = swapImageViews[i],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL
        };
        VkDescriptorBufferInfo buffer_info = {
            .buffer = sceneBuffer,
            .offset = 0u,
            .range = sizeof(SceneBufferGpu),
        };
        VkWriteDescriptorSet descriptor_writes[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 0u,
                .descriptorCount = 1u,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &image_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 1u,
                .descriptorCount = 1u,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffer_info,
            }
        };
        vkUpdateDescriptorSets(device, 2u, descriptor_writes, 0u, NULL);
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

        CameraPushConstants cameraPush = {
            .focus_zoom = {cameraFocus[0], cameraFocus[1], cameraFocus[2], cameraZoom},
            .params = {cameraFov, 0.0f, 0.0f, 0.0f},
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
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(cameraPush), &cameraPush);
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
