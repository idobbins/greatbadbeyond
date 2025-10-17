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
#define VULKAN_COMPUTE_LOCAL_SIZE 16
#define VULKAN_MAX_PATH_LENGTH 512
#define RT_MAX_SPHERES 1000000u
#define FRAME_TIME_SAMPLES 240

#define GRID_MAX_LEVEL0_DIM 128u
#define GRID_FINE_DIM 8u
#define GRID_LEVEL0_CELLS (GRID_MAX_LEVEL0_DIM * GRID_MAX_LEVEL0_DIM)
#define GRID_LEVEL1_CELLS (GRID_LEVEL0_CELLS * GRID_FINE_DIM * GRID_FINE_DIM)

typedef struct VulkanBuffers {
    VkBuffer sphereCR;
    VmaAllocation sphereCRAlloc;
    VkBuffer sphereAlb;
    VmaAllocation sphereAlbAlloc;
    VkBuffer hitT;
    VmaAllocation hitTAlloc;
    VkBuffer hitN;
    VmaAllocation hitNAlloc;
    VkBuffer gridLevel0Meta;
    VmaAllocation gridLevel0MetaAlloc;
    VkBuffer gridLevel0Counter;
    VmaAllocation gridLevel0CounterAlloc;
    VkBuffer gridLevel0Indices;
    VmaAllocation gridLevel0IndicesAlloc;
    VkBuffer gridLevel1Meta;
    VmaAllocation gridLevel1MetaAlloc;
    VkBuffer gridLevel1Counter;
    VmaAllocation gridLevel1CounterAlloc;
    VkBuffer gridLevel1Indices;
    VmaAllocation gridLevel1IndicesAlloc;
    VkBuffer gridState;
    VmaAllocation gridStateAlloc;
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
        VkShaderModule spheresInitSM;
        VkShaderModule primaryIntersectSM;
        VkShaderModule shadeShadowSM;
        VkShaderModule gridCountSM;
        VkShaderModule gridClassifySM;
        VkShaderModule gridScatterSM;
        VkShaderModule blitVertexShaderModule;
        VkShaderModule blitFragmentShaderModule;
        VkDescriptorSetLayout descriptorSetLayout;
        VkDescriptorPool descriptorPool;
        VkDescriptorSet descriptorSet;
        VkPipelineLayout computePipelineLayout;
        VkPipelineLayout blitPipelineLayout;
        VkPipeline spheresInitPipe;
        VkPipeline primaryIntersectPipe;
        VkPipeline shadeShadowPipe;
        VkPipeline gridCountPipe;
        VkPipeline gridClassifyPipe;
        VkPipeline gridScatterPipe;
        VkPipeline blitPipeline;
        VmaAllocator vma;
        VkCommandPool commandPool;
        VkCommandBuffer commandBuffer;
        VkImage gradientImage;
        VmaAllocation gradientAlloc;
        VkImageView gradientImageView;
        VkSampler gradientSampler;
        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphores[VULKAN_MAX_SWAPCHAIN_IMAGES];
        VkFence frameFence;

        VulkanBuffers rt;

        bool gradientInitialized;
        bool sceneInitialized;

        uint32_t sphereCount;
        float sphereRadius;
        float groundY;
        float worldMinX;
        float worldMinZ;
        float worldMaxX;
        float worldMaxZ;

        Camera cam;
        uint32_t frameIndex;

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
