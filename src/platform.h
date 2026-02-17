#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

int gbbInitWindow(uint32_t width, uint32_t height, const char* title);
void gbbShutdownWindow(void);
int gbbPumpEventsOnce(void);
void gbbGetInstanceExtensions(const char*** extensions, uint32_t* extensionCount, VkInstanceCreateFlags* instanceFlags);
VkResult gbbCreateSurface(VkInstance instance, VkSurfaceKHR* surface);

#ifdef __cplusplus
}
#endif

#endif
