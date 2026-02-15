#include <greadbadbeyond.h>
#include <config.h>
#include <utils.h>

#include <GLFW/glfw3.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>

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
        struct FrameLogSnapshot
        {
            u32 frameSamples;
            u32 historySamples;
            array<float, frameTimingHistoryCapacity> frameHistoryMs;
            array<float, frameTimingHistoryCapacity> acquireHistoryMs;
            array<float, frameTimingHistoryCapacity> acquireWaitFrameFenceHistoryMs;
            array<float, frameTimingHistoryCapacity> acquireCallHistoryMs;
            array<float, frameTimingHistoryCapacity> acquireWaitImageFenceHistoryMs;
            array<float, frameTimingHistoryCapacity> recordHistoryMs;
            array<float, frameTimingHistoryCapacity> submitHistoryMs;
            array<float, frameTimingHistoryCapacity> gpuShadowHistoryMs;
            array<float, frameTimingHistoryCapacity> gpuForwardHistoryMs;
            array<float, frameTimingHistoryCapacity> gpuTotalHistoryMs;

        };

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
        array<float, frameTimingHistoryCapacity> gpuShadowHistoryMs;
        array<float, frameTimingHistoryCapacity> gpuForwardHistoryMs;
        array<float, frameTimingHistoryCapacity> gpuTotalHistoryMs;
        u32 historyCount;
        u32 historyHead;
        array<FrameLogSnapshot, frameTimingLogQueueCapacity> logQueue;
        u32 logQueueHead;
        u32 logQueueTail;
        u32 logQueueCount;
        u32 droppedLogCount;
        mutex logMutex;
        condition_variable logCondition;
        thread logThread;
        bool logThreadReady;
        bool logStopRequested;
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

    if (Platform.FrameTiming.logThreadReady)
    {
        {
            lock_guard<mutex> lock(Platform.FrameTiming.logMutex);
            Platform.FrameTiming.logStopRequested = true;
        }
        Platform.FrameTiming.logCondition.notify_all();
        if (Platform.FrameTiming.logThread.joinable())
        {
            Platform.FrameTiming.logThread.join();
        }
        Platform.FrameTiming.logThreadReady = false;
        Platform.FrameTiming.logStopRequested = false;
        Platform.FrameTiming.logQueueHead = 0;
        Platform.FrameTiming.logQueueTail = 0;
        Platform.FrameTiming.logQueueCount = 0;
        Platform.FrameTiming.droppedLogCount = 0;
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
    Platform.FrameTiming.gpuShadowHistoryMs.fill(std::numeric_limits<float>::quiet_NaN());
    Platform.FrameTiming.gpuForwardHistoryMs.fill(std::numeric_limits<float>::quiet_NaN());
    Platform.FrameTiming.gpuTotalHistoryMs.fill(std::numeric_limits<float>::quiet_NaN());
    Platform.FrameTiming.historyCount = 0;
    Platform.FrameTiming.historyHead = 0;
    Platform.FrameTiming.logQueueHead = 0;
    Platform.FrameTiming.logQueueTail = 0;
    Platform.FrameTiming.logQueueCount = 0;
    Platform.FrameTiming.droppedLogCount = 0;
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
    {
        lock_guard<mutex> lock(Platform.FrameTiming.logMutex);
        Platform.FrameTiming.logQueueHead = 0;
        Platform.FrameTiming.logQueueTail = 0;
        Platform.FrameTiming.logQueueCount = 0;
        Platform.FrameTiming.droppedLogCount = 0;
    }

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
    Platform.FrameTiming.gpuShadowHistoryMs.fill(std::numeric_limits<float>::quiet_NaN());
    Platform.FrameTiming.gpuForwardHistoryMs.fill(std::numeric_limits<float>::quiet_NaN());
    Platform.FrameTiming.gpuTotalHistoryMs.fill(std::numeric_limits<float>::quiet_NaN());
    Platform.FrameTiming.historyCount = 0;
    Platform.FrameTiming.historyHead = 0;
    Platform.FrameTiming.ready = true;
}

void MainLoop()
{
    static bool frameLogConfigured = false;
    static bool frameLogEnabled = true;
    if (!frameLogConfigured)
    {
        const char *frameLogEnv = std::getenv("GBB_FRAME_LOG");
        if ((frameLogEnv != nullptr) && (frameLogEnv[0] == '0'))
        {
            frameLogEnabled = false;
            LogInfo("[frame] Periodic frame log disabled via GBB_FRAME_LOG=0");
        }
        frameLogConfigured = true;
    }

    if (!Platform.FrameTiming.ready)
    {
        ResetFrameTiming();
    }

    if (frameLogEnabled && !Platform.FrameTiming.logThreadReady)
    {
        {
            lock_guard<mutex> lock(Platform.FrameTiming.logMutex);
            Platform.FrameTiming.logStopRequested = false;
            Platform.FrameTiming.logQueueHead = 0;
            Platform.FrameTiming.logQueueTail = 0;
            Platform.FrameTiming.logQueueCount = 0;
            Platform.FrameTiming.droppedLogCount = 0;
        }

        Platform.FrameTiming.logThread = thread([]()
        {
            while (true)
            {
                auto snapshot = Platform.FrameTiming.logQueue[0];
                bool hasSnapshot = false;
                u32 droppedLogCount = 0;

                {
                    unique_lock<mutex> lock(Platform.FrameTiming.logMutex);
                    Platform.FrameTiming.logCondition.wait(lock, []()
                    {
                        return Platform.FrameTiming.logStopRequested || (Platform.FrameTiming.logQueueCount > 0);
                    });

                    if (Platform.FrameTiming.logQueueCount > 0)
                    {
                        u32 queueIndex = Platform.FrameTiming.logQueueHead;
                        snapshot = Platform.FrameTiming.logQueue[queueIndex];
                        Platform.FrameTiming.logQueueHead = (queueIndex + 1u) % frameTimingLogQueueCapacity;
                        Platform.FrameTiming.logQueueCount -= 1;
                        droppedLogCount = Platform.FrameTiming.droppedLogCount;
                        Platform.FrameTiming.droppedLogCount = 0;
                        hasSnapshot = true;
                    }
                    else if (Platform.FrameTiming.logStopRequested)
                    {
                        break;
                    }
                }

                if (droppedLogCount > 0)
                {
                    LogWarn("[frame] Dropped %u periodic log snapshots because logger queue was full", static_cast<unsigned>(droppedLogCount));
                }

                if (!hasSnapshot)
                {
                    continue;
                }

                u32 frameSamples = snapshot.frameSamples;
                u32 historySamples = snapshot.historySamples;
                if ((frameSamples == 0) || (historySamples == 0))
                {
                    continue;
                }

                array<float, frameTimingHistoryCapacity> sortedFrameMs = {};

                double rollingFrameSumMs = 0.0;
                double rollingAcquireSumMs = 0.0;
                double rollingAcquireWaitFrameFenceSumMs = 0.0;
                double rollingAcquireCallSumMs = 0.0;
                double rollingAcquireWaitImageFenceSumMs = 0.0;
                double rollingRecordSumMs = 0.0;
                double rollingSubmitSumMs = 0.0;
                double rollingGpuShadowSumMs = 0.0;
                double rollingGpuForwardSumMs = 0.0;
                double rollingGpuTotalSumMs = 0.0;
                u32 gpuHistorySamples = 0;

                for (u32 index = 0; index < historySamples; ++index)
                {
                    float frameSampleMs = snapshot.frameHistoryMs[index];
                    float acquireSampleMs = snapshot.acquireHistoryMs[index];
                    float acquireWaitFrameFenceSampleMs = snapshot.acquireWaitFrameFenceHistoryMs[index];
                    float acquireCallSampleMs = snapshot.acquireCallHistoryMs[index];
                    float acquireWaitImageFenceSampleMs = snapshot.acquireWaitImageFenceHistoryMs[index];
                    float recordSampleMs = snapshot.recordHistoryMs[index];
                    float submitSampleMs = snapshot.submitHistoryMs[index];
                    float gpuShadowSampleMs = snapshot.gpuShadowHistoryMs[index];
                    float gpuForwardSampleMs = snapshot.gpuForwardHistoryMs[index];
                    float gpuTotalSampleMs = snapshot.gpuTotalHistoryMs[index];

                    sortedFrameMs[index] = frameSampleMs;

                    rollingFrameSumMs += static_cast<double>(frameSampleMs);
                    rollingAcquireSumMs += static_cast<double>(acquireSampleMs);
                    rollingAcquireWaitFrameFenceSumMs += static_cast<double>(acquireWaitFrameFenceSampleMs);
                    rollingAcquireCallSumMs += static_cast<double>(acquireCallSampleMs);
                    rollingAcquireWaitImageFenceSumMs += static_cast<double>(acquireWaitImageFenceSampleMs);
                    rollingRecordSumMs += static_cast<double>(recordSampleMs);
                    rollingSubmitSumMs += static_cast<double>(submitSampleMs);
                    if (std::isfinite(gpuShadowSampleMs) && std::isfinite(gpuForwardSampleMs) && std::isfinite(gpuTotalSampleMs))
                    {
                        rollingGpuShadowSumMs += static_cast<double>(gpuShadowSampleMs);
                        rollingGpuForwardSumMs += static_cast<double>(gpuForwardSampleMs);
                        rollingGpuTotalSumMs += static_cast<double>(gpuTotalSampleMs);
                        gpuHistorySamples += 1;
                    }
                }

                std::sort(sortedFrameMs.begin(), sortedFrameMs.begin() + historySamples);

                const auto percentileFrameMs = [&](double percentile) -> double
                {
                    double p = std::clamp(percentile, 0.0, 1.0);
                    double rank = std::ceil(p * static_cast<double>(historySamples));
                    u32 rankIndex = (rank <= 1.0) ? 0u : static_cast<u32>(rank - 1.0);
                    if (rankIndex >= historySamples)
                    {
                        rankIndex = historySamples - 1;
                    }
                    return static_cast<double>(sortedFrameMs[rankIndex]);
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
                double p50Ms = percentileFrameMs(0.50);
                double p95Ms = percentileFrameMs(0.95);
                double p99Ms = percentileFrameMs(0.99);
                double p01Ms = percentileFrameMs(0.001);
                double p999Ms = percentileFrameMs(0.999);
                double p0_1LowFps = (p999Ms > 0.0) ? (1000.0 / p999Ms) : 0.0;
                double p99_9HighFps = (p01Ms > 0.0) ? (1000.0 / p01Ms) : 0.0;

                double acquireAverageMs = rollingAcquireSumMs / static_cast<double>(historySamples);
                double acquireWaitFrameFenceAverageMs = rollingAcquireWaitFrameFenceSumMs / static_cast<double>(historySamples);
                double acquireCallAverageMs = rollingAcquireCallSumMs / static_cast<double>(historySamples);
                double acquireWaitImageFenceAverageMs = rollingAcquireWaitImageFenceSumMs / static_cast<double>(historySamples);
                double recordAverageMs = rollingRecordSumMs / static_cast<double>(historySamples);
                double submitAverageMs = rollingSubmitSumMs / static_cast<double>(historySamples);

                if (gpuHistorySamples > 0)
                {
                    double gpuShadowAverageMs = rollingGpuShadowSumMs / static_cast<double>(gpuHistorySamples);
                    double gpuForwardAverageMs = rollingGpuForwardSumMs / static_cast<double>(gpuHistorySamples);
                    double gpuTotalAverageMs = rollingGpuTotalSumMs / static_cast<double>(gpuHistorySamples);

                    LogInfo("[frame] avg %.1f fps (%.3f ms) | 1%% low %.1f | 99%% high %.1f | p0.1 low %.1f | p99.9 high %.1f | p50 %.3f ms p95 %.3f ms p99 %.3f ms | stage avg %.3f/%.3f/%.3f ms (acq/record/submit) | acq split avg %.3f/%.3f/%.3f ms (wait/acquire/imgwait) | gpu avg %.3f/%.3f/%.3f ms (shadow/forward/total) | samples=%u window=%u gpuSamples=%u",
                        averageFps,
                        averageMs,
                        onePercentLowFps,
                        ninetyNinePercentHighFps,
                        p0_1LowFps,
                        p99_9HighFps,
                        p50Ms,
                        p95Ms,
                        p99Ms,
                        acquireAverageMs,
                        recordAverageMs,
                        submitAverageMs,
                        acquireWaitFrameFenceAverageMs,
                        acquireCallAverageMs,
                        acquireWaitImageFenceAverageMs,
                        gpuShadowAverageMs,
                        gpuForwardAverageMs,
                        gpuTotalAverageMs,
                        static_cast<unsigned>(frameSamples),
                        static_cast<unsigned>(historySamples),
                        static_cast<unsigned>(gpuHistorySamples));
                }
                else
                {
                    LogInfo("[frame] avg %.1f fps (%.3f ms) | 1%% low %.1f | 99%% high %.1f | p0.1 low %.1f | p99.9 high %.1f | p50 %.3f ms p95 %.3f ms p99 %.3f ms | stage avg %.3f/%.3f/%.3f ms (acq/record/submit) | acq split avg %.3f/%.3f/%.3f ms (wait/acquire/imgwait) | gpu n/a | samples=%u window=%u",
                        averageFps,
                        averageMs,
                        onePercentLowFps,
                        ninetyNinePercentHighFps,
                        p0_1LowFps,
                        p99_9HighFps,
                        p50Ms,
                        p95Ms,
                        p99Ms,
                        acquireAverageMs,
                        recordAverageMs,
                        submitAverageMs,
                        acquireWaitFrameFenceAverageMs,
                        acquireCallAverageMs,
                        acquireWaitImageFenceAverageMs,
                        static_cast<unsigned>(frameSamples),
                        static_cast<unsigned>(historySamples));
                }
            }
        });
        Platform.FrameTiming.logThreadReady = true;
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
        float frameWorkMs = static_cast<float>((frameEndTime - frameStartTime) * 1000.0);
        if (!std::isfinite(frameWorkMs) || (frameWorkMs < 0.0f))
        {
            frameWorkMs = 0.0f;
        }

        // Use start-to-start cadence so stalls outside submit (for example logging work)
        // still appear in frame timing stats.
        float frameMs = static_cast<float>(deltaSeconds * 1000.0);
        if (!std::isfinite(frameMs) || (frameMs <= 0.0f))
        {
            frameMs = frameWorkMs;
        }
        bool frameSampleValid = frameMs >= frameTimingMinSampleMs;
        if (!frameSampleValid)
        {
            LogWarn("[frame] Ignoring short sample %.3f ms (< %.3f ms threshold)", frameMs, frameTimingMinSampleMs);
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

        if (Platform.FrameTiming.warmupComplete && frameSampleValid)
        {
            u32 sampleIndex = Platform.FrameTiming.historyHead;
            Platform.FrameTiming.frameHistoryMs[sampleIndex] = frameMs;
            Platform.FrameTiming.acquireHistoryMs[sampleIndex] = acquireMs;
            Platform.FrameTiming.acquireWaitFrameFenceHistoryMs[sampleIndex] = acquireTiming.waitFrameFenceMs;
            Platform.FrameTiming.acquireCallHistoryMs[sampleIndex] = acquireTiming.acquireCallMs;
            Platform.FrameTiming.acquireWaitImageFenceHistoryMs[sampleIndex] = acquireTiming.waitImageFenceMs;
            Platform.FrameTiming.recordHistoryMs[sampleIndex] = recordMs;
            Platform.FrameTiming.submitHistoryMs[sampleIndex] = submitMs;
            Platform.FrameTiming.gpuShadowHistoryMs[sampleIndex] = acquireTiming.gpuValid ? acquireTiming.gpuShadowMs : std::numeric_limits<float>::quiet_NaN();
            Platform.FrameTiming.gpuForwardHistoryMs[sampleIndex] = acquireTiming.gpuValid ? acquireTiming.gpuForwardMs : std::numeric_limits<float>::quiet_NaN();
            Platform.FrameTiming.gpuTotalHistoryMs[sampleIndex] = acquireTiming.gpuValid ? acquireTiming.gpuTotalMs : std::numeric_limits<float>::quiet_NaN();
            Platform.FrameTiming.historyHead = (Platform.FrameTiming.historyHead + 1) % frameTimingHistoryCapacity;
            if (Platform.FrameTiming.historyCount < frameTimingHistoryCapacity)
            {
                Platform.FrameTiming.historyCount += 1;
            }

            Platform.FrameTiming.accumulatedMs += static_cast<double>(frameMs);
            Platform.FrameTiming.samples += 1;
        }

        if (frameLogEnabled && ((frameEndTime - Platform.FrameTiming.lastLogTime) >= frameTimingLogIntervalSeconds))
        {
            u32 frameSamples = Platform.FrameTiming.samples;
            u32 historySamples = Platform.FrameTiming.historyCount;
            if (Platform.FrameTiming.warmupComplete && (frameSamples > 0) && (historySamples > 0))
            {
                bool queued = false;
                {
                    lock_guard<mutex> lock(Platform.FrameTiming.logMutex);
                    if (Platform.FrameTiming.logQueueCount < frameTimingLogQueueCapacity)
                    {
                        u32 queueIndex = Platform.FrameTiming.logQueueTail;
                        auto &snapshot = Platform.FrameTiming.logQueue[queueIndex];
                        snapshot.frameSamples = frameSamples;
                        snapshot.historySamples = historySamples;
                        for (u32 index = 0; index < historySamples; ++index)
                        {
                            snapshot.frameHistoryMs[index] = Platform.FrameTiming.frameHistoryMs[index];
                            snapshot.acquireHistoryMs[index] = Platform.FrameTiming.acquireHistoryMs[index];
                            snapshot.acquireWaitFrameFenceHistoryMs[index] = Platform.FrameTiming.acquireWaitFrameFenceHistoryMs[index];
                            snapshot.acquireCallHistoryMs[index] = Platform.FrameTiming.acquireCallHistoryMs[index];
                            snapshot.acquireWaitImageFenceHistoryMs[index] = Platform.FrameTiming.acquireWaitImageFenceHistoryMs[index];
                            snapshot.recordHistoryMs[index] = Platform.FrameTiming.recordHistoryMs[index];
                            snapshot.submitHistoryMs[index] = Platform.FrameTiming.submitHistoryMs[index];
                            snapshot.gpuShadowHistoryMs[index] = Platform.FrameTiming.gpuShadowHistoryMs[index];
                            snapshot.gpuForwardHistoryMs[index] = Platform.FrameTiming.gpuForwardHistoryMs[index];
                            snapshot.gpuTotalHistoryMs[index] = Platform.FrameTiming.gpuTotalHistoryMs[index];
                        }
                        Platform.FrameTiming.logQueueTail = (queueIndex + 1u) % frameTimingLogQueueCapacity;
                        Platform.FrameTiming.logQueueCount += 1;
                        queued = true;
                    }
                    else
                    {
                        Platform.FrameTiming.droppedLogCount += 1;
                    }
                }

                if (queued)
                {
                    Platform.FrameTiming.logCondition.notify_one();
                }
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
    const char *validationEnv = std::getenv("GBB_VALIDATION");
    if ((validationEnv != nullptr) && (validationEnv[0] == '0'))
    {
        return false;
    }
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
