#pragma once

#include <span>

//------------------------------------------------------------------------------------
// Forward Declarations
//------------------------------------------------------------------------------------

template<typename T>struct Config;

struct Window;

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
bool IsDone(Window &window);
bool IsReady(Window &window);

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
