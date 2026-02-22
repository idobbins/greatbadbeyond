#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
extern void *window_handle;
#elif defined(__APPLE__)
extern void *surface_layer;
#endif

int gbbInitWindow(uint32_t width, uint32_t height, const char* title);
void gbbShutdownWindow(void);
int gbbPumpEventsOnce(void);

#ifdef __cplusplus
}
#endif

#endif
