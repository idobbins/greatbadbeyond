#pragma once

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

static constexpr uint32_t MaxPhysicalDevices = 16;
static constexpr uint32_t MaxQueueFamilies = 16;
static constexpr uint32_t MaxPlatformInstanceExtensions = 8;
static constexpr uint32_t MaxDeviceExtensions = 8;
static constexpr uint32_t MaxEnumeratedDeviceExtensions = 256;
static constexpr uint32_t MaxSurfaceFormats = 64;
static constexpr uint32_t MaxSurfacePresentModes = 32;
static constexpr uint32_t MaxSwapchainImages = 8;

static constexpr std::size_t InstanceExtensionScratchBytes = 1024;
static constexpr std::size_t InstanceLayerScratchBytes = 1024;

static constexpr uint32_t swapchainImageCount = 4;
static constexpr uint32_t FrameOverlap = 4;
static constexpr VkSampleCountFlagBits preferredMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
static constexpr uint32_t frameTimingHistoryCapacity = 1200;
static constexpr uint32_t frameTimingLogQueueCapacity = 8;
static constexpr uint32_t frameTimingHitchTraceQueueCapacity = 2048;
static constexpr double frameTimingLogIntervalSeconds = 5.0;
static constexpr double frameTimingWarmupSeconds = 2.0;
static constexpr double frameTimingCapFps = 0.0;
static constexpr float frameTimingMinSampleMs = 1.0f;
static constexpr float frameTimingHitchThresholdMs = 12.0f;
static constexpr float frameTimingWorkHitchThresholdMs = 10.0f;
static constexpr float frameTimingQueueSubmitHitchThresholdMs = 8.0f;

static constexpr int DefaultWindowWidth = 1280;
static constexpr int DefaultWindowHeight = 720;
static constexpr const char *DefaultWindowTitle = "Greadbadbeyond";

static constexpr const char *DefaultApplicationName = "Greadbadbeyond";
static constexpr const char *DefaultEngineName = "Greadbadbeyond";

static constexpr VkDebugUtilsMessageSeverityFlagsEXT DefaultDebugSeverityMask =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

static constexpr VkDebugUtilsMessageTypeFlagsEXT DefaultDebugTypeMask =
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

static constexpr const char *ValidationLayerName = "VK_LAYER_KHRONOS_validation";

static constexpr const char *LogErrorPrefix = "error:";
static constexpr const char *LogWarnPrefix = "warn :";
static constexpr const char *LogInfoPrefix = "info :";
