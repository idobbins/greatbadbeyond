#pragma once

#if defined(_WIN32)
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef VK_USE_PLATFORM_WIN32_KHR
  #define VK_USE_PLATFORM_WIN32_KHR
  #endif
  #include <windows.h>
#endif

#if defined(__APPLE__)
  #ifndef VK_ENABLE_BETA_EXTENSIONS
  #define VK_ENABLE_BETA_EXTENSIONS
  #endif
  #ifndef VK_USE_PLATFORM_METAL_EXT
  #define VK_USE_PLATFORM_METAL_EXT
  #endif
#endif

#if defined(__ANDROID__)
  #ifndef VK_USE_PLATFORM_ANDROID_KHR
  #define VK_USE_PLATFORM_ANDROID_KHR
  #endif
#endif

#include <vulkan/vulkan.h>

#if defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#include <vulkan/vulkan_beta.h>
#endif

#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#endif
