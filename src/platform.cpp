#include <greadbadbeyond.h>
#include <config.h>
#include <utils.h>

#include <GLFW/glfw3.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

using namespace std;

static struct PlatformData
{
    struct
    {
        bool ready;

    } Glfw;
    struct
    {
        string_view title;
        GLFWwindow *handle;
        bool ready;
        bool framebufferResized;

    } Window;
    struct
    {
        float mouseWheelDelta;

    } Input;
    struct
    {
        double lastTime;
        double lastLogTime;
        double resetTime;
        double accumulatedMs;
        u32 samples;
        float deltaSeconds;
        bool warmupComplete;
        array<float, frameTimingHistoryCapacity> frameHistoryMs;
        array<float, frameTimingHistoryCapacity> acquireHistoryMs;
        array<float, frameTimingHistoryCapacity> acquireWaitFrameFenceHistoryMs;
        array<float, frameTimingHistoryCapacity> acquireCallHistoryMs;
        array<float, frameTimingHistoryCapacity> acquireWaitImageFenceHistoryMs;
        array<float, frameTimingHistoryCapacity> recordHistoryMs;
        array<float, frameTimingHistoryCapacity> submitHistoryMs;
        u32 historyCount;
        u32 historyHead;
        bool ready;

    } FrameTiming;
} Platform;

void GlfwErrorCallback(int code, const char *description)
{
    const char *message = description != nullptr ? description: "no description";
    cerr << "[glfw][error " << code << "] " << message << endl;
}

void CreateGlfwContext()
{
    if (Platform.Glfw.ready)
    {
        return;
    }

    glfwSetErrorCallback(GlfwErrorCallback);
    Assert(glfwInit() == GLFW_TRUE, "Failed to initialize GLFW");
    Assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW was not compiled with Vulkan support");

    Platform.Glfw.ready = true;
}

void DestroyGlfwContext()
{
    if (!Platform.Glfw.ready)
    {
        return;
    }

    glfwTerminate();
    glfwSetErrorCallback(nullptr);
    Platform.Glfw.ready = false;
}



auto GetPlatformVulkanExtensions() -> span<const char *>
{
    static array<const char*, MaxPlatformInstanceExtensions> cache {};
    static uint32_t count = 0;
    static bool ready = false;

    if (ready)
    {
        return {cache.data(), count};
    }

    const char **extensions = glfwGetRequiredInstanceExtensions(&count);
    Assert(extensions != nullptr, "glfwGetRequiredInstanceExtensions returned null");
    Assert(count > 0, "glfwGetRequiredInstanceExtensions returned no extensions");
    Assert(count <= cache.size(), "Too many GLFW-required extensions for cache");

    for (uint32_t index = 0; index < count; ++index) {
        cache[index] = extensions[index];
    }

    ready = true;
    return {cache.data(), count};
}

void CreateWindow()
{
    Assert(Platform.Glfw.ready, "GLFW must be initialized before trying to init window");

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#if defined(__APPLE__)
    // Keep logical window size while rendering at native HiDPI framebuffer resolution.
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif

    Platform.Window.title = DefaultWindowTitle;
    Platform.Window.handle = glfwCreateWindow(DefaultWindowWidth, DefaultWindowHeight, Platform.Window.title.data(), nullptr, nullptr);
    Assert(Platform.Window.handle != nullptr, "Failed to create GLFW window");
    glfwSetFramebufferSizeCallback(Platform.Window.handle, FramebufferSizeCallback);
    glfwSetScrollCallback(Platform.Window.handle, ScrollCallback);

    Platform.Window.ready = true;
    Platform.Window.framebufferResized = false;
    Platform.Input.mouseWheelDelta = 0.0f;
    ResetFrameTiming();
}

void DestroyWindow()
{
    if (!Platform.Window.ready)
    {
        return;
    }

    glfwDestroyWindow(Platform.Window.handle);
    Platform.Window.handle = nullptr;

    Platform.Window.ready = false;
    Platform.Window.framebufferResized = false;
    Platform.Input.mouseWheelDelta = 0.0f;

    Platform.FrameTiming.ready = false;
    Platform.FrameTiming.deltaSeconds = 0.0f;
    Platform.FrameTiming.samples = 0;
    Platform.FrameTiming.accumulatedMs = 0.0;
    Platform.FrameTiming.lastTime = 0.0;
    Platform.FrameTiming.lastLogTime = 0.0;
    Platform.FrameTiming.resetTime = 0.0;
    Platform.FrameTiming.warmupComplete = false;
    Platform.FrameTiming.frameHistoryMs.fill(0.0f);
    Platform.FrameTiming.acquireHistoryMs.fill(0.0f);
    Platform.FrameTiming.acquireWaitFrameFenceHistoryMs.fill(0.0f);
    Platform.FrameTiming.acquireCallHistoryMs.fill(0.0f);
    Platform.FrameTiming.acquireWaitImageFenceHistoryMs.fill(0.0f);
    Platform.FrameTiming.recordHistoryMs.fill(0.0f);
    Platform.FrameTiming.submitHistoryMs.fill(0.0f);
    Platform.FrameTiming.historyCount = 0;
    Platform.FrameTiming.historyHead = 0;
}

auto WindowShouldClose() -> bool
{
    if (!Platform.Window.ready || !Platform.Window.handle)
    {
        return true;
    }

    return glfwWindowShouldClose(Platform.Window.handle);
}

auto IsWindowReady() -> bool
{
    return Platform.Window.ready;
}

auto GetWindowSize() -> Size
{
    Size size = {0, 0};

    if (!Platform.Window.ready)
    {
        return size;
    }

    int width = 0;
    int height = 0;
    glfwGetWindowSize(Platform.Window.handle, &width, &height);

    size.width = width;
    size.height = height;

    return size;
}

auto GetFramebufferSize() -> Size
{
    Size size = {0, 0};

    if (!Platform.Window.ready)
    {
        return size;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(Platform.Window.handle, &width, &height);

    size.width = width;
    size.height = height;

    return size;
}

void FramebufferSizeCallback(GLFWwindow *window, int width, int height)
{
    (void)window;
    (void)width;
    (void)height;

    Platform.Window.framebufferResized = true;
}

void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    (void)window;
    (void)xoffset;

    Platform.Input.mouseWheelDelta += static_cast<float>(yoffset);
}

auto ConsumeFramebufferResize() -> bool
{
    bool resized = Platform.Window.framebufferResized;
    Platform.Window.framebufferResized = false;
    return resized;
}

auto ConsumeMouseWheelDelta() -> float
{
    float delta = Platform.Input.mouseWheelDelta;
    Platform.Input.mouseWheelDelta = 0.0f;
    return delta;
}

auto GetWindowHandle() -> GLFWwindow *
{
    Assert(Platform.Window.ready && Platform.Window.handle != nullptr, "Window is not ready");
    return Platform.Window.handle;
}

void PollEvents()
{
    glfwPollEvents();
}

auto GetFrameDeltaSeconds() -> float
{
    return Platform.FrameTiming.deltaSeconds;
}

void ResetFrameTiming()
{
    double now = glfwGetTime();
    Platform.FrameTiming.lastTime = now;
    Platform.FrameTiming.lastLogTime = now;
    Platform.FrameTiming.resetTime = now;
    Platform.FrameTiming.accumulatedMs = 0.0;
    Platform.FrameTiming.samples = 0;
    Platform.FrameTiming.deltaSeconds = 0.0f;
    Platform.FrameTiming.warmupComplete = false;
    Platform.FrameTiming.frameHistoryMs.fill(0.0f);
    Platform.FrameTiming.acquireHistoryMs.fill(0.0f);
    Platform.FrameTiming.acquireWaitFrameFenceHistoryMs.fill(0.0f);
    Platform.FrameTiming.acquireCallHistoryMs.fill(0.0f);
    Platform.FrameTiming.acquireWaitImageFenceHistoryMs.fill(0.0f);
    Platform.FrameTiming.recordHistoryMs.fill(0.0f);
    Platform.FrameTiming.submitHistoryMs.fill(0.0f);
    Platform.FrameTiming.historyCount = 0;
    Platform.FrameTiming.historyHead = 0;
    Platform.FrameTiming.ready = true;
}

void MainLoop()
{
    if (!Platform.FrameTiming.ready)
    {
        ResetFrameTiming();
    }

    while (!WindowShouldClose())
    {
        double frameStartTime = glfwGetTime();
        double deltaSeconds = frameStartTime - Platform.FrameTiming.lastTime;
        if (deltaSeconds < 0.0)
        {
            deltaSeconds = 0.0;
        }

        Platform.FrameTiming.lastTime = frameStartTime;
        Platform.FrameTiming.deltaSeconds = static_cast<float>(deltaSeconds);

        PollEvents();
        GLFWwindow *window = GetWindowHandle();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        }
        UpdateCameraFromInput(static_cast<float>(Platform.FrameTiming.deltaSeconds));

        if (ConsumeFramebufferResize())
        {
            ResetCameraAccum();
            RecreateSwapchain();
            continue;
        }

        u32 imageIndex = 0;
        u32 frameIndex = 0;
        AcquireTiming acquireTiming = {};
        VkResult acquireResult = AcquireNextImage(imageIndex, frameIndex, acquireTiming);
        float acquireMs = acquireTiming.totalMs;

        if ((acquireResult == VK_ERROR_OUT_OF_DATE_KHR) || (acquireResult == VK_SUBOPTIMAL_KHR))
        {
            RecreateSwapchain();
            continue;
        }

        if (acquireResult != VK_SUCCESS)
        {
            LogError("[vulkan] AcquireNextImage failed (result=%d)", acquireResult);
            break;
        }

        GradientParams gradient = {};
        Size framebuffer = GetFramebufferSize();
        gradient.resolution.x = (framebuffer.width > 0)? static_cast<float>(framebuffer.width) : 1.0f;
        gradient.resolution.y = (framebuffer.height > 0)? static_cast<float>(framebuffer.height) : 1.0f;
        gradient.time = static_cast<float>(glfwGetTime());
        gradient.padding = 0.0f;

        double recordStartTime = glfwGetTime();
        VkResult recordResult = DrawFrameForward(frameIndex, imageIndex, gradient);
        double recordEndTime = glfwGetTime();
        float recordMs = static_cast<float>((recordEndTime - recordStartTime) * 1000.0);
        if (!std::isfinite(recordMs) || (recordMs < 0.0f))
        {
            recordMs = 0.0f;
        }

        if (recordResult != VK_SUCCESS)
        {
            LogError("[vulkan] DrawFrameForward failed (result=%d)", recordResult);
            break;
        }

        double submitStartTime = glfwGetTime();
        VkResult submitResult = SubmitFrame(frameIndex, imageIndex);
        double submitEndTime = glfwGetTime();
        float submitMs = static_cast<float>((submitEndTime - submitStartTime) * 1000.0);
        if (!std::isfinite(submitMs) || (submitMs < 0.0f))
        {
            submitMs = 0.0f;
        }

        if ((submitResult == VK_ERROR_OUT_OF_DATE_KHR) || (submitResult == VK_SUBOPTIMAL_KHR))
        {
            RecreateSwapchain();
            continue;
        }

        if (submitResult != VK_SUCCESS)
        {
            LogError("[vulkan] SubmitFrame failed (result=%d)", submitResult);
            break;
        }

        double frameEndTime = submitEndTime;
        float frameMs = static_cast<float>((frameEndTime - frameStartTime) * 1000.0);
        if (!std::isfinite(frameMs) || (frameMs < 0.0f))
        {
            frameMs = 0.0f;
        }

        if (!Platform.FrameTiming.warmupComplete &&
            ((frameEndTime - Platform.FrameTiming.resetTime) >= frameTimingWarmupSeconds))
        {
            Platform.FrameTiming.warmupComplete = true;
            Platform.FrameTiming.lastLogTime = frameEndTime;
            Platform.FrameTiming.accumulatedMs = 0.0;
            Platform.FrameTiming.samples = 0;
            LogInfo("[frame] Warmup complete after %.1f s; collecting timing samples", frameTimingWarmupSeconds);
        }

        if (Platform.FrameTiming.warmupComplete)
        {
            u32 sampleIndex = Platform.FrameTiming.historyHead;
            Platform.FrameTiming.frameHistoryMs[sampleIndex] = frameMs;
            Platform.FrameTiming.acquireHistoryMs[sampleIndex] = acquireMs;
            Platform.FrameTiming.acquireWaitFrameFenceHistoryMs[sampleIndex] = acquireTiming.waitFrameFenceMs;
            Platform.FrameTiming.acquireCallHistoryMs[sampleIndex] = acquireTiming.acquireCallMs;
            Platform.FrameTiming.acquireWaitImageFenceHistoryMs[sampleIndex] = acquireTiming.waitImageFenceMs;
            Platform.FrameTiming.recordHistoryMs[sampleIndex] = recordMs;
            Platform.FrameTiming.submitHistoryMs[sampleIndex] = submitMs;
            Platform.FrameTiming.historyHead = (Platform.FrameTiming.historyHead + 1) % frameTimingHistoryCapacity;
            if (Platform.FrameTiming.historyCount < frameTimingHistoryCapacity)
            {
                Platform.FrameTiming.historyCount += 1;
            }

            Platform.FrameTiming.accumulatedMs += static_cast<double>(frameMs);
            Platform.FrameTiming.samples += 1;
        }

        if ((frameEndTime - Platform.FrameTiming.lastLogTime) >= frameTimingLogIntervalSeconds)
        {
            u32 frameSamples = Platform.FrameTiming.samples;
            u32 historySamples = Platform.FrameTiming.historyCount;
            if (Platform.FrameTiming.warmupComplete && (frameSamples > 0) && (historySamples > 0))
            {
                array<float, frameTimingHistoryCapacity> sortedFrameMs = {};
                array<float, frameTimingHistoryCapacity> sortedAcquireMs = {};
                array<float, frameTimingHistoryCapacity> sortedAcquireWaitFrameFenceMs = {};
                array<float, frameTimingHistoryCapacity> sortedAcquireCallMs = {};
                array<float, frameTimingHistoryCapacity> sortedAcquireWaitImageFenceMs = {};
                array<float, frameTimingHistoryCapacity> sortedRecordMs = {};
                array<float, frameTimingHistoryCapacity> sortedSubmitMs = {};

                double rollingFrameSumMs = 0.0;
                double rollingAcquireSumMs = 0.0;
                double rollingAcquireWaitFrameFenceSumMs = 0.0;
                double rollingAcquireCallSumMs = 0.0;
                double rollingAcquireWaitImageFenceSumMs = 0.0;
                double rollingRecordSumMs = 0.0;
                double rollingSubmitSumMs = 0.0;
                float minFrameMs = std::numeric_limits<float>::max();
                float maxFrameMs = 0.0f;

                for (u32 index = 0; index < historySamples; ++index)
                {
                    float frameSampleMs = Platform.FrameTiming.frameHistoryMs[index];
                    float acquireSampleMs = Platform.FrameTiming.acquireHistoryMs[index];
                    float acquireWaitFrameFenceSampleMs = Platform.FrameTiming.acquireWaitFrameFenceHistoryMs[index];
                    float acquireCallSampleMs = Platform.FrameTiming.acquireCallHistoryMs[index];
                    float acquireWaitImageFenceSampleMs = Platform.FrameTiming.acquireWaitImageFenceHistoryMs[index];
                    float recordSampleMs = Platform.FrameTiming.recordHistoryMs[index];
                    float submitSampleMs = Platform.FrameTiming.submitHistoryMs[index];

                    sortedFrameMs[index] = frameSampleMs;
                    sortedAcquireMs[index] = acquireSampleMs;
                    sortedAcquireWaitFrameFenceMs[index] = acquireWaitFrameFenceSampleMs;
                    sortedAcquireCallMs[index] = acquireCallSampleMs;
                    sortedAcquireWaitImageFenceMs[index] = acquireWaitImageFenceSampleMs;
                    sortedRecordMs[index] = recordSampleMs;
                    sortedSubmitMs[index] = submitSampleMs;

                    rollingFrameSumMs += static_cast<double>(frameSampleMs);
                    rollingAcquireSumMs += static_cast<double>(acquireSampleMs);
                    rollingAcquireWaitFrameFenceSumMs += static_cast<double>(acquireWaitFrameFenceSampleMs);
                    rollingAcquireCallSumMs += static_cast<double>(acquireCallSampleMs);
                    rollingAcquireWaitImageFenceSumMs += static_cast<double>(acquireWaitImageFenceSampleMs);
                    rollingRecordSumMs += static_cast<double>(recordSampleMs);
                    rollingSubmitSumMs += static_cast<double>(submitSampleMs);

                    minFrameMs = std::min(minFrameMs, frameSampleMs);
                    maxFrameMs = std::max(maxFrameMs, frameSampleMs);
                }

                std::sort(sortedFrameMs.begin(), sortedFrameMs.begin() + historySamples);
                std::sort(sortedAcquireMs.begin(), sortedAcquireMs.begin() + historySamples);
                std::sort(sortedAcquireWaitFrameFenceMs.begin(), sortedAcquireWaitFrameFenceMs.begin() + historySamples);
                std::sort(sortedAcquireCallMs.begin(), sortedAcquireCallMs.begin() + historySamples);
                std::sort(sortedAcquireWaitImageFenceMs.begin(), sortedAcquireWaitImageFenceMs.begin() + historySamples);
                std::sort(sortedRecordMs.begin(), sortedRecordMs.begin() + historySamples);
                std::sort(sortedSubmitMs.begin(), sortedSubmitMs.begin() + historySamples);

                const auto percentileMs = [&](const array<float, frameTimingHistoryCapacity> &sortedValues, double percentile) -> double
                {
                    double p = std::clamp(percentile, 0.0, 1.0);
                    double rank = std::ceil(p * static_cast<double>(historySamples));
                    u32 rankIndex = (rank <= 1.0) ? 0u : static_cast<u32>(rank - 1.0);
                    if (rankIndex >= historySamples)
                    {
                        rankIndex = historySamples - 1;
                    }
                    return static_cast<double>(sortedValues[rankIndex]);
                };

                u32 tailSampleCount = std::max<u32>(1u, historySamples/100u);
                double bestTailMs = 0.0;
                double worstTailMs = 0.0;
                for (u32 index = 0; index < tailSampleCount; ++index)
                {
                    bestTailMs += static_cast<double>(sortedFrameMs[index]);
                    worstTailMs += static_cast<double>(sortedFrameMs[(historySamples - 1u) - index]);
                }
                bestTailMs /= static_cast<double>(tailSampleCount);
                worstTailMs /= static_cast<double>(tailSampleCount);

                double averageMs = rollingFrameSumMs / static_cast<double>(historySamples);
                double averageFps = (averageMs > 0.0) ? (1000.0 / averageMs) : 0.0;
                double onePercentLowFps = (worstTailMs > 0.0) ? (1000.0 / worstTailMs) : 0.0;
                double ninetyNinePercentHighFps = (bestTailMs > 0.0) ? (1000.0 / bestTailMs) : 0.0;
                double minFps = (maxFrameMs > 0.0f) ? (1000.0 / static_cast<double>(maxFrameMs)) : 0.0;
                double maxFps = (minFrameMs > 0.0f) ? (1000.0 / static_cast<double>(minFrameMs)) : 0.0;
                double p50Ms = percentileMs(sortedFrameMs, 0.50);
                double p95Ms = percentileMs(sortedFrameMs, 0.95);
                double p99Ms = percentileMs(sortedFrameMs, 0.99);

                double acquireAverageMs = rollingAcquireSumMs / static_cast<double>(historySamples);
                double acquireWaitFrameFenceAverageMs = rollingAcquireWaitFrameFenceSumMs / static_cast<double>(historySamples);
                double acquireCallAverageMs = rollingAcquireCallSumMs / static_cast<double>(historySamples);
                double acquireWaitImageFenceAverageMs = rollingAcquireWaitImageFenceSumMs / static_cast<double>(historySamples);
                double recordAverageMs = rollingRecordSumMs / static_cast<double>(historySamples);
                double submitAverageMs = rollingSubmitSumMs / static_cast<double>(historySamples);
                double acquireP95Ms = percentileMs(sortedAcquireMs, 0.95);
                double acquireWaitFrameFenceP95Ms = percentileMs(sortedAcquireWaitFrameFenceMs, 0.95);
                double acquireCallP95Ms = percentileMs(sortedAcquireCallMs, 0.95);
                double acquireWaitImageFenceP95Ms = percentileMs(sortedAcquireWaitImageFenceMs, 0.95);
                double recordP95Ms = percentileMs(sortedRecordMs, 0.95);
                double submitP95Ms = percentileMs(sortedSubmitMs, 0.95);
                double acquireP99Ms = percentileMs(sortedAcquireMs, 0.99);
                double acquireWaitFrameFenceP99Ms = percentileMs(sortedAcquireWaitFrameFenceMs, 0.99);
                double acquireCallP99Ms = percentileMs(sortedAcquireCallMs, 0.99);
                double acquireWaitImageFenceP99Ms = percentileMs(sortedAcquireWaitImageFenceMs, 0.99);
                double recordP99Ms = percentileMs(sortedRecordMs, 0.99);
                double submitP99Ms = percentileMs(sortedSubmitMs, 0.99);

                LogInfo("[frame] avg %.1f fps (%.3f ms) | 1%% low %.1f | 99%% high %.1f | p50 %.3f ms p95 %.3f ms p99 %.3f ms | stage avg %.3f/%.3f/%.3f ms p95 %.3f/%.3f/%.3f ms p99 %.3f/%.3f/%.3f ms (acq/record/submit) | acq split avg %.3f/%.3f/%.3f ms p95 %.3f/%.3f/%.3f ms p99 %.3f/%.3f/%.3f ms (wait/acquire/imgwait) | min %.1f fps max %.1f fps | samples=%u window=%u",
                    averageFps,
                    averageMs,
                    onePercentLowFps,
                    ninetyNinePercentHighFps,
                    p50Ms,
                    p95Ms,
                    p99Ms,
                    acquireAverageMs,
                    recordAverageMs,
                    submitAverageMs,
                    acquireP95Ms,
                    recordP95Ms,
                    submitP95Ms,
                    acquireP99Ms,
                    recordP99Ms,
                    submitP99Ms,
                    acquireWaitFrameFenceAverageMs,
                    acquireCallAverageMs,
                    acquireWaitImageFenceAverageMs,
                    acquireWaitFrameFenceP95Ms,
                    acquireCallP95Ms,
                    acquireWaitImageFenceP95Ms,
                    acquireWaitFrameFenceP99Ms,
                    acquireCallP99Ms,
                    acquireWaitImageFenceP99Ms,
                    minFps,
                    maxFps,
                    static_cast<unsigned>(frameSamples),
                    static_cast<unsigned>(historySamples));
            }

            Platform.FrameTiming.lastLogTime = frameEndTime;
            Platform.FrameTiming.accumulatedMs = 0.0;
            Platform.FrameTiming.samples = 0;
        }
    }
}

auto RequiresDebug() -> bool
{
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}

auto RequiresPortability() -> bool
{
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}
