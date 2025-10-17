#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>

#include "runtime.h"
#include "vk_bootstrap.h"
#include "vk_descriptors.h"
#include "vk_pipelines.h"
#include "rt_resources.h"
#include "rt_frame.h"
#include "vk_swapchain.h"

static const char *const defaultApplicationTitle = "Callandor";

// Provide logging helpers

static void LogWrite(FILE *stream, const char *prefix, const char *format, va_list args)
{
    fprintf(stream, "%s ", prefix);
    vfprintf(stream, format, args);
    fputc('\n', stream);
}

void LogError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stderr, "error:", format, args);
    va_end(args);
}

void LogWarn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stderr, "warn :", format, args);
    va_end(args);
}

void LogInfo(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stdout, "info :", format, args);
    va_end(args);
}

void Assert(bool condition, const char *message)
{
    if (!condition)
    {
        LogError("assert: %s", message);
        exit(EXIT_FAILURE);
    }
}

// Provide small vector helpers

static float3 add3(float3 a, float3 b)
{
    float3 r = { a.x + b.x, a.y + b.y, a.z + b.z };
    return r;
}

static float3 mul3f(float3 v, float s)
{
    float3 r = { v.x * s, v.y * s, v.z * s };
    return r;
}

static float3 cross3(float3 a, float3 b)
{
    float3 r = {
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x),
    };
    return r;
}

static float dot3(float3 a, float3 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

static float3 normalize3(float3 v)
{
    float lenSq = dot3(v, v);
    if (lenSq <= 1e-12f)
    {
        float3 zero = { 0.0f, 0.0f, 0.0f };
        return zero;
    }

    float invLen = 1.0f / sqrtf(lenSq);
    float3 r = { v.x * invLen, v.y * invLen, v.z * invLen };
    return r;
}

GlobalData GLOBAL = (GlobalData){ 0 };

static void FrameStatsReset(void);
static void FrameStatsAddSample(double deltaSeconds, double nowSeconds);

// Manage GLFW and window lifecycle

static void GlfwErrorCallback(int32_t code, const char *desc)
{
    const char *message = (desc != NULL) ? desc : "no description";
    LogError("[glfw][%d] %s", code, message);
}

static void InitGlfwContext(void)
{
    glfwSetErrorCallback(GlfwErrorCallback);

    Assert(glfwInit() == true, "Failed to initialize GLFW");
    Assert(glfwVulkanSupported() == true, "Vulkan is not supported");

    GLOBAL.Glfw.ready = true;
    GLOBAL.Glfw.vulkanSupported = true;

    LogInfo("GLFW initialized (Vulkan supported)");
}

static void CloseGlfwContext(void)
{
    if (!GLOBAL.Glfw.ready)
    {
        return;
    }

    glfwTerminate();
    glfwSetErrorCallback(NULL);

    GLOBAL.Glfw.ready = false;
    GLOBAL.Glfw.vulkanSupported = false;
}

static void InitWindow(void)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    GLOBAL.Window.title = defaultApplicationTitle;
    GLOBAL.Window.window = glfwCreateWindow(1280, 720, GLOBAL.Window.title, NULL, NULL);
    Assert(GLOBAL.Window.window != NULL, "Failed to create window");

    glfwSetInputMode(GLOBAL.Window.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    GLOBAL.Vulkan.cam.pos = (float3){ 0.0f, 1.5f, 4.0f };
    GLOBAL.Vulkan.cam.yaw = 0.0f;
    GLOBAL.Vulkan.cam.pitch = 0.0f;
    GLOBAL.Vulkan.cam.fovY = 60.0f * 3.14159265f / 180.0f;
    GLOBAL.Vulkan.cam.fwd = (float3){ 0.0f, 0.0f, -1.0f };
    GLOBAL.Vulkan.cam.right = (float3){ 1.0f, 0.0f, 0.0f };
    GLOBAL.Vulkan.cam.up = (float3){ 0.0f, 1.0f, 0.0f };
    GLOBAL.Vulkan.frameIndex = 0;
    GLOBAL.Vulkan.sphereCount = 512;
    GLOBAL.Vulkan.sphereRadius = 0.25f;
    GLOBAL.Vulkan.groundY = 0.0f;
    GLOBAL.Vulkan.worldMinX = -8.0f;
    GLOBAL.Vulkan.worldMinZ = -8.0f;
    GLOBAL.Vulkan.worldMaxX = 8.0f;
    GLOBAL.Vulkan.worldMaxZ = 8.0f;
    GLOBAL.Vulkan.sceneInitialized = false;

    Assert(GLOBAL.Vulkan.sphereCount <= RT_MAX_SPHERES, "Sphere count exceeds capacity");

    RtUpdateSpawnArea();
    FrameStatsReset();

    GLOBAL.Window.ready = true;
}

static void CloseWindow(void)
{
    if (!GLOBAL.Window.ready)
    {
        return;
    }

    glfwDestroyWindow(GLOBAL.Window.window);
    GLOBAL.Window.window = NULL;
    GLOBAL.Window.ready = false;
}

static bool IsWindowReady(void)
{
    return GLOBAL.Window.ready;
}

static bool WindowShouldClose(void)
{
    Assert(IsWindowReady(), "Window is not ready");
    return glfwWindowShouldClose(GLOBAL.Window.window);
}

// Track frame statistics

static uint32_t FrameStatsPercentileIndex(uint32_t count, double percentile)
{
    if (count == 0)
    {
        return 0;
    }

    double scaled = percentile * (double)(count - 1);
    uint32_t index = (uint32_t)(scaled + 0.5);
    if (index >= count)
    {
        index = count - 1;
    }

    return index;
}

static void FrameStatsAddSample(double deltaSeconds, double nowSeconds)
{
    if (deltaSeconds < 0.0)
    {
        return;
    }

    GLOBAL.Frame.samples[GLOBAL.Frame.sampleCursor] = deltaSeconds;
    if (GLOBAL.Frame.sampleCount < FRAME_TIME_SAMPLES)
    {
        GLOBAL.Frame.sampleCount++;
    }
    GLOBAL.Frame.sampleCursor = (GLOBAL.Frame.sampleCursor + 1u) % FRAME_TIME_SAMPLES;

    if ((nowSeconds - GLOBAL.Frame.lastReportTime) < 1.0)
    {
        return;
    }

    if (GLOBAL.Frame.sampleCount < 5)
    {
        return;
    }

    double sorted[FRAME_TIME_SAMPLES];
    const uint32_t count = GLOBAL.Frame.sampleCount;
    for (uint32_t index = 0; index < count; index++)
    {
        sorted[index] = GLOBAL.Frame.samples[index];
    }

    for (uint32_t i = 1; i < count; i++)
    {
        double key = sorted[i];
        int32_t j = (int32_t)i - 1;
        while ((j >= 0) && (sorted[j] > key))
        {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    uint32_t idx0 = FrameStatsPercentileIndex(count, 0.0);
    uint32_t idx50 = FrameStatsPercentileIndex(count, 0.5);
    uint32_t idx99 = FrameStatsPercentileIndex(count, 0.99);

    double p0 = sorted[idx0] * 1000.0;
    double p50 = sorted[idx50] * 1000.0;
    double p99 = sorted[idx99] * 1000.0;

    LogInfo("frame ms: p0=%.3f p50=%.3f p99=%.3f (n=%u)", p0, p50, p99, count);

    GLOBAL.Frame.lastReportTime = nowSeconds;
}

static void FrameStatsReset(void)
{
    memset(GLOBAL.Frame.samples, 0, sizeof(GLOBAL.Frame.samples));
    GLOBAL.Frame.sampleCount = 0;
    GLOBAL.Frame.sampleCursor = 0;
    double now = glfwGetTime();
    GLOBAL.Frame.lastTimestamp = now;
    GLOBAL.Frame.lastReportTime = now;
}

// Handle input and camera controls

static void UpdateCameraControls(void)
{
    GLFWwindow *window = GLOBAL.Window.window;
    if (window == NULL)
    {
        return;
    }

    static double lastTime = 0.0;
    double now = glfwGetTime();
    double delta = now - lastTime;
    lastTime = now;
    float dt = (float)delta;
    if (dt > 0.25f)
    {
        dt = 0.25f;
    }

    static double lastX = 0.0;
    static double lastY = 0.0;
    static bool firstMouse = true;
    if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
        firstMouse = true;
    }

    double mx = 0.0;
    double my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    if (firstMouse)
    {
        lastX = mx;
        lastY = my;
        firstMouse = false;
    }

    float dx = (float)(mx - lastX);
    float dy = (float)(my - lastY);
    lastX = mx;
    lastY = my;

    const float sens = 0.0025f;
    GLOBAL.Vulkan.cam.yaw += dx * sens;
    GLOBAL.Vulkan.cam.pitch += -dy * sens;

    const float limit = 1.55f;
    if (GLOBAL.Vulkan.cam.pitch > limit)
    {
        GLOBAL.Vulkan.cam.pitch = limit;
    }
    if (GLOBAL.Vulkan.cam.pitch < -limit)
    {
        GLOBAL.Vulkan.cam.pitch = -limit;
    }

    float cy = cosf(GLOBAL.Vulkan.cam.yaw);
    float sy = sinf(GLOBAL.Vulkan.cam.yaw);
    float cp = cosf(GLOBAL.Vulkan.cam.pitch);
    float sp = sinf(GLOBAL.Vulkan.cam.pitch);

    GLOBAL.Vulkan.cam.fwd = normalize3((float3){ cp * cy, sp, cp * sy });
    float3 worldUp = { 0.0f, 1.0f, 0.0f };
    GLOBAL.Vulkan.cam.right = normalize3(cross3(GLOBAL.Vulkan.cam.fwd, worldUp));
    if ((GLOBAL.Vulkan.cam.right.x == 0.0f) && (GLOBAL.Vulkan.cam.right.y == 0.0f) && (GLOBAL.Vulkan.cam.right.z == 0.0f))
    {
        GLOBAL.Vulkan.cam.right = (float3){ 1.0f, 0.0f, 0.0f };
    }
    GLOBAL.Vulkan.cam.up = normalize3(cross3(GLOBAL.Vulkan.cam.right, GLOBAL.Vulkan.cam.fwd));

    float speed = 4.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    {
        speed *= 3.0f;
    }

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    {
        GLOBAL.Vulkan.cam.pos = add3(GLOBAL.Vulkan.cam.pos, mul3f(GLOBAL.Vulkan.cam.fwd, speed * dt));
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {
        GLOBAL.Vulkan.cam.pos = add3(GLOBAL.Vulkan.cam.pos, mul3f(GLOBAL.Vulkan.cam.fwd, -speed * dt));
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    {
        GLOBAL.Vulkan.cam.pos = add3(GLOBAL.Vulkan.cam.pos, mul3f(GLOBAL.Vulkan.cam.right, speed * dt));
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    {
        GLOBAL.Vulkan.cam.pos = add3(GLOBAL.Vulkan.cam.pos, mul3f(GLOBAL.Vulkan.cam.right, -speed * dt));
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
    {
        GLOBAL.Vulkan.cam.pos.y += speed * dt;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    {
        GLOBAL.Vulkan.cam.pos.y -= speed * dt;
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

// Manage Vulkan lifecycle

static void InitVulkan(void)
{
    if (GLOBAL.Vulkan.ready)
    {
        return;
    }

    VulkanInitCore();
    LoadShaderModules();
    VulkanCreateDescriptorInfra();
    CreateComputePipelines();
    CreateSwapchain();

    Assert(GLOBAL.Vulkan.ready, "Vulkan initialization incomplete");
    LogInfo("Vulkan initialization complete");
}

static void CloseVulkan(void)
{
    if ((GLOBAL.Vulkan.instance == VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.device == VK_NULL_HANDLE) &&
        (GLOBAL.Vulkan.surface == VK_NULL_HANDLE))
    {
        return;
    }

    if (GLOBAL.Vulkan.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(GLOBAL.Vulkan.device);
    }

    DestroySwapchain();
    DestroyPipelines();
    DestroyShaderModules();
    VulkanDestroyDescriptorInfra();
    VulkanShutdownCore();
}

// Provide application entry point

int main(void)
{
    InitGlfwContext();
    InitWindow();
    InitVulkan();

    while (!WindowShouldClose())
    {
        glfwPollEvents();
        UpdateCameraControls();
        VulkanDrawFrame();

        double now = glfwGetTime();
        if (GLOBAL.Frame.lastTimestamp > 0.0)
        {
            double delta = now - GLOBAL.Frame.lastTimestamp;
            FrameStatsAddSample(delta, now);
        }
        GLOBAL.Frame.lastTimestamp = now;
    }

    CloseVulkan();
    CloseWindow();
    CloseGlfwContext();

    return 0;
}
