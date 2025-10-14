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
  #include <windows.h>              // brings in HINSTANCE, HWND, HANDLE
#endif

#include <vulkan/vulkan.h>          // pulls vulkan_win32 when VK_USE_PLATFORM_WIN32_KHR is set

#include "../defer.cpp"

#if defined(_WIN32)
// (optional) explicit Win32 header; safe since windows.h is already included
#include <vulkan/vulkan_win32.h>
#endif

#endif //CALLCALLANDOR_VULKAN_HEADERS_CPP