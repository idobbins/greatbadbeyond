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
enum {
    GBB_KEY_W = 0u,
    GBB_KEY_A = 1u,
    GBB_KEY_S = 2u,
    GBB_KEY_D = 3u,
    GBB_KEY_COUNT = 4u,
};
int gbbIsKeyDown(uint32_t key);
void gbbConsumeMouseWheel(float* delta);
uint64_t gbbGetTimeNs(void);

#ifdef __cplusplus
}
#endif

#endif
