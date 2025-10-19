#pragma once

#include <cstdint>
#include <pair>
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
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::string_view title = "callandor";
    bool resizable = false;
};

struct Window
{
    GLFWwindow *handle = nullptr;
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

Window Create(const Config<Window> &config);
void Destroy(Window &window);
void ErrorCallback(int error, const char *description);
bool ShouldClose(Window &window);
bool IsReady(Window &window);
bool IsKeyPressed(Window &window, int key);
void Poll(Window &window);
std::pair<std::uint32_t, std::uint32_t> FramebufferSize(const Window &window);
std::span<const PlatformExtension> Enumerate();

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
