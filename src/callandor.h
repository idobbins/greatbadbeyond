#pragma once

#include <vulkan/vulkan.h>

#include <span>
#include <string_view>

//------------------------------------------------------------------------------------
// Forward Declarations
//------------------------------------------------------------------------------------

template<typename T>struct Config;

struct GLFWwindow;
struct Window;
using PlatformExtension = const char *;

template<>
struct Config<Window>
{
    std::uint32_t width;
    std::uint32_t height;
    std::string_view title;
    bool resizable;
};

struct Window
{
    GLFWwindow *handle;
};

// Vulkan (c library) forward declarations
struct VkInstance_T;
typedef VkInstance_T* VkInstance;

struct VkExtensionProperties;

struct VkSurfaceKHR_T;
typedef VkSurfaceKHR_T *VkSurfaceKHR;

struct VkPhysicalDevice_T;
typedef VkPhysicalDevice_T* VkPhysicalDevice;

struct Vulkan;
struct InstanceConfig;

//------------------------------------------------------------------------------------
// Window Functions (Module: window)
//------------------------------------------------------------------------------------

void WindowErrorCallback(int code, const char *description);

void CreateWindow();
void DestroyWindow();
bool WindowShouldClose();
bool IsWindowReady();
bool IsKeyPressed();
void Poll();
int FramebufferSize(const Window &window);
int Enumerate();

//------------------------------------------------------------------------------------
// Vulkan Functions (Module: vulkan)
//------------------------------------------------------------------------------------

void Create(Vulkan &v);
void Destroy(Vulkan &v);

VkInstance Create(const Config<VkInstance> &config);
void Destroy(VkInstance &instance);

VkSurfaceKHR Create(const Config<VkSurfaceKHR> &config);
void Destroy(VkSurfaceKHR &surface);

template <typename T>
std::span<const T> Enumerate();

template <typename T>
std::span<const T> Enumerate(const VkInstance &instance);

// Shader management functions
