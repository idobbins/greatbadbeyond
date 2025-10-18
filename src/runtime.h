#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "vk_mem_alloc.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#if !defined(NDEBUG)
    #define VULKAN_ENABLE_DEBUG 1
#else
    #define VULKAN_ENABLE_DEBUG 0
#endif

#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    #define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#ifndef VULKAN_SHADER_DIRECTORY
    #define VULKAN_SHADER_DIRECTORY "./shaders"
#endif

#define VULKAN_MAX_ENABLED_EXTENSIONS 16
#define VULKAN_MAX_ENABLED_LAYERS 16
#define VULKAN_MAX_PHYSICAL_DEVICES 16
#define VULKAN_MAX_SWAPCHAIN_IMAGES 8
#define VULKAN_MAX_SURFACE_FORMATS 64
#define VULKAN_MAX_PRESENT_MODES 16
#define VULKAN_MAX_SHADER_SIZE (1024 * 1024)
#define VULKAN_FRAMES_IN_FLIGHT 2
#define VULKAN_COMPUTE_LOCAL_SIZE 16
#define VULKAN_MAX_PATH_LENGTH 512
#define RT_MAX_SPHERES 10000u
#define FRAME_TIME_SAMPLES 240

typedef struct VulkanBuffers {
    VkBuffer sphereCR;
    VmaAllocation sphereCRAlloc;
    VkBuffer sphereAlb;
    VmaAllocation sphereAlbAlloc;
    VkBuffer hitT;
    VmaAllocation hitTAlloc;
    VkBuffer hitN;
    VmaAllocation hitNAlloc;
    VkBuffer accum;
    VmaAllocation accumAlloc;
    VkBuffer spp;
    VmaAllocation sppAlloc;
    VkBuffer epoch;
    VmaAllocation epochAlloc;
    // Uniform grid
    VkBuffer gridRanges;   // uvec2 {start,count} per cell
    VmaAllocation gridRangesAlloc;
    VkBuffer gridIndices;  // uint sphere indices
    VmaAllocation gridIndicesAlloc;
    VkBuffer gridCoarseCounts; // uint total sphere refs per coarse cell
    VmaAllocation gridCoarseCountsAlloc;
} VulkanBuffers;

typedef struct Float3 {
    float x;
    float y;
    float z;
} float3;

typedef struct Camera {
    float3 pos;
    float yaw;
    float pitch;
    float fovY;
    float3 fwd;
    float3 right;
    float3 up;
} Camera;

typedef struct GlobalData {
    struct {
        bool ready;
        bool vulkanSupported;

    } Glfw;
    struct {
        const char *title;
        bool ready;
        GLFWwindow *window;

    } Window;
    struct {
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkSurfaceKHR surface;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkQueue queue;
        uint32_t queueFamily;
        VkSwapchainKHR swapchain;
        VkImage swapchainImages[VULKAN_MAX_SWAPCHAIN_IMAGES];
        VkImageView swapchainImageViews[VULKAN_MAX_SWAPCHAIN_IMAGES];
        uint32_t swapchainImageCount;
        VkFormat swapchainImageFormat;
        VkExtent2D swapchainExtent;
        VkShaderModule primaryIntersectSM;
        VkShaderModule shadeShadowSM;
        VkShaderModule blitVertexShaderModule;
        VkShaderModule blitFragmentShaderModule;
        VkDescriptorSetLayout descriptorSetLayout;
        VkDescriptorPool descriptorPool;
        VkDescriptorSet descriptorSet;
        VkPipelineLayout computePipelineLayout;
        VkPipelineLayout blitPipelineLayout;
        VkPipeline primaryIntersectPipe;
        VkPipeline shadeShadowPipe;
        VkPipeline blitPipeline;
        VmaAllocator vma;
        VkCommandPool commandPool;
        VkCommandBuffer commandBuffers[VULKAN_FRAMES_IN_FLIGHT];
        VkImage gradientImage;
        VmaAllocation gradientAlloc;
        VkImageView gradientImageView;
        VkSampler gradientSampler;
        VkSemaphore imageAvailableSemaphores[VULKAN_FRAMES_IN_FLIGHT];
        VkSemaphore renderFinishedSemaphores[VULKAN_FRAMES_IN_FLIGHT];
        VkFence inFlightFences[VULKAN_FRAMES_IN_FLIGHT];
        VkFence imagesInFlight[VULKAN_MAX_SWAPCHAIN_IMAGES];

        VulkanBuffers rt;

        bool gradientInitialized;
        bool sceneInitialized;
        bool resetAccumulation;
        uint32_t accumulationEpoch;

        uint32_t sphereTargetCount;
        uint32_t sphereCount;
        float sphereMinRadius;
        float sphereMaxRadius;
        float sphereCRHost[RT_MAX_SPHERES * 4];
        float sphereAlbHost[RT_MAX_SPHERES * 4];
        float groundY;
        float worldMinX;
        float worldMinZ;
        float worldMaxX;
        float worldMaxZ;
        // Uniform grid metadata
        uint32_t gridDimX;
        uint32_t gridDimY;
        uint32_t gridDimZ;
        float gridMinX;
        float gridMinY;
        float gridMinZ;
        float gridInvCellX;
        float gridInvCellY;
        float gridInvCellZ;
        bool showGrid;
        uint32_t coarseDimX;
        uint32_t coarseDimY;
        uint32_t coarseDimZ;
        float coarseInvCellX;
        float coarseInvCellY;
        float coarseInvCellZ;
        uint32_t coarseFactor;

        uint32_t vendorId;
        uint32_t subgroupSize;
        uint32_t computeLocalSizeX;
        uint32_t computeLocalSizeY;

        Camera cam;
        uint32_t frameIndex;
        uint32_t currentFrame;

        bool ready;
        bool debugEnabled;
        bool validationLayersEnabled;

    } Vulkan;
    struct {
        double samples[FRAME_TIME_SAMPLES];
        uint32_t sampleCount;
        uint32_t sampleCursor;
        double lastTimestamp;
        double lastReportTime;
    } Frame;
} GlobalData;

extern GlobalData GLOBAL;

void LogError(const char *format, ...);
void LogWarn(const char *format, ...);
void LogInfo(const char *format, ...);
void Assert(bool condition, const char *message);
