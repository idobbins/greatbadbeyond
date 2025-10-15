#ifndef CALLANDOR_VULKAN_HEADERS_CPP
#define CALLANDOR_VULKAN_HEADERS_CPP

#if defined(_WIN32)
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #define VK_USE_PLATFORM_WIN32_KHR
  #include <windows.h>
#endif

#include <vulkan/vulkan.h>

#if defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#include <vulkan/vulkan_beta.h>
#endif

#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#endif

#endif //CALLCALLANDOR_VULKAN_HEADERS_CPP
