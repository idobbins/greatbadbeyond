#include <greadbadbeyond.h>
#include <config.h>
#include <utils.h>

#include <GLFW/glfw3.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
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
            double wallTimeSeconds;
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

        struct HitchTraceEvent
        {
            u32 kind;
            u64 loopFrameId;
            u32 frameIndex;
            u32 imageIndex;
            u32 frameSamples;
            u32 historySamples;
            int acquireResult;
            int submitResult;
            bool frameSampleValid;
            bool gpuValid;
            u32 triggerMask;
            double wallTimeSeconds;
            float frameMs;
            float frameWorkMs;
            float frameOutsideWorkMs;
            float pollEventsMs;
            float inputUpdateMs;
            float prepMs;
            float acquireMs;
            float acquireWaitFrameFenceMs;
            float acquireCallMs;
            float acquireWaitImageFenceMs;
            float recordMs;
            float submitMs;
            float submitResetFenceMs;
            float submitQueueMs;
            float presentMs;
            float recreateSwapchainMs;
            float gpuShadowMs;
            float gpuForwardMs;
            float gpuTotalMs;

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
        u64 loopFrameCounter;
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
        bool hitchTraceEnabled;
        bool hitchTraceAllFrames;
        float hitchTraceFrameThresholdMs;
        float hitchTraceWorkThresholdMs;
        float hitchTraceQueueSubmitThresholdMs;
        array<HitchTraceEvent, frameTimingHitchTraceQueueCapacity> hitchQueue;
        u32 hitchQueueHead;
        u32 hitchQueueTail;
        u32 hitchQueueCount;
        u32 droppedHitchTraceCount;
        mutex hitchMutex;
        condition_variable hitchCondition;
        thread hitchThread;
        bool hitchThreadReady;
        bool hitchStopRequested;
        bool ready;

    } FrameTiming;
} Platform;

static constexpr u32 HitchTraceEventFrame = 1u;
static constexpr u32 HitchTraceEventResize = 2u;
static constexpr u32 HitchTraceEventAcquireOutOfDate = 3u;
static constexpr u32 HitchTraceEventSubmitOutOfDate = 4u;
static constexpr u32 HitchTraceEventAcquireFailure = 5u;
static constexpr u32 HitchTraceEventSubmitFailure = 6u;
static constexpr u32 HitchTraceEventWarmupComplete = 7u;
static constexpr u32 HitchTriggerInvalidSample = 1u << 0u;
static constexpr u32 HitchTriggerFrameCadence = 1u << 1u;
static constexpr u32 HitchTriggerFrameWork = 1u << 2u;
static constexpr u32 HitchTriggerQueueSubmit = 1u << 3u;
static constexpr const char *HitchTraceDefaultPath = "hitch_trace.csv";
static constexpr const char *FrameStatsDefaultPath = "frame_stats.csv";

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

    if (Platform.FrameTiming.hitchThreadReady)
    {
        {
            lock_guard<mutex> lock(Platform.FrameTiming.hitchMutex);
            Platform.FrameTiming.hitchStopRequested = true;
        }
        Platform.FrameTiming.hitchCondition.notify_all();
        if (Platform.FrameTiming.hitchThread.joinable())
        {
            Platform.FrameTiming.hitchThread.join();
        }
        Platform.FrameTiming.hitchThreadReady = false;
        Platform.FrameTiming.hitchStopRequested = false;
        Platform.FrameTiming.hitchQueueHead = 0;
        Platform.FrameTiming.hitchQueueTail = 0;
        Platform.FrameTiming.hitchQueueCount = 0;
        Platform.FrameTiming.droppedHitchTraceCount = 0;
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
    Platform.FrameTiming.loopFrameCounter = 0;
    Platform.FrameTiming.logQueueHead = 0;
    Platform.FrameTiming.logQueueTail = 0;
    Platform.FrameTiming.logQueueCount = 0;
    Platform.FrameTiming.droppedLogCount = 0;
    Platform.FrameTiming.hitchTraceEnabled = false;
    Platform.FrameTiming.hitchTraceAllFrames = false;
    Platform.FrameTiming.hitchTraceFrameThresholdMs = frameTimingHitchThresholdMs;
    Platform.FrameTiming.hitchTraceWorkThresholdMs = frameTimingWorkHitchThresholdMs;
    Platform.FrameTiming.hitchTraceQueueSubmitThresholdMs = frameTimingQueueSubmitHitchThresholdMs;
    Platform.FrameTiming.hitchQueueHead = 0;
    Platform.FrameTiming.hitchQueueTail = 0;
    Platform.FrameTiming.hitchQueueCount = 0;
    Platform.FrameTiming.droppedHitchTraceCount = 0;
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
    {
        lock_guard<mutex> lock(Platform.FrameTiming.hitchMutex);
        Platform.FrameTiming.hitchQueueHead = 0;
        Platform.FrameTiming.hitchQueueTail = 0;
        Platform.FrameTiming.hitchQueueCount = 0;
        Platform.FrameTiming.droppedHitchTraceCount = 0;
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
    Platform.FrameTiming.loopFrameCounter = 0;
    Platform.FrameTiming.ready = true;
}

void MainLoop()
{
    static bool frameLogConfigured = false;
    static bool frameLogEnabled = true;
    static bool hitchTraceConfigured = false;
    static bool frameCapConfigured = false;
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

    if (!hitchTraceConfigured)
    {
        Platform.FrameTiming.hitchTraceEnabled = true;
        Platform.FrameTiming.hitchTraceAllFrames = false;
        Platform.FrameTiming.hitchTraceFrameThresholdMs = frameTimingHitchThresholdMs;
        Platform.FrameTiming.hitchTraceWorkThresholdMs = frameTimingWorkHitchThresholdMs;
        Platform.FrameTiming.hitchTraceQueueSubmitThresholdMs = frameTimingQueueSubmitHitchThresholdMs;
        LogInfo(
            "[hitch] Trace enabled (path=%s frame>=%.3f ms work>=%.3f ms submit>=%.3f ms mode=%s)",
            HitchTraceDefaultPath,
            Platform.FrameTiming.hitchTraceFrameThresholdMs,
            Platform.FrameTiming.hitchTraceWorkThresholdMs,
            Platform.FrameTiming.hitchTraceQueueSubmitThresholdMs,
            Platform.FrameTiming.hitchTraceAllFrames ? "all-frames" : "hitches-only");

        hitchTraceConfigured = true;
    }

    if (!frameCapConfigured)
    {
        if (frameTimingCapFps > 0.0)
        {
            LogInfo("[frame] Hard cap enabled at %.1f fps", frameTimingCapFps);
        }
        frameCapConfigured = true;
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

        const char *statsPath = FrameStatsDefaultPath;
        LogInfo("[frame] Periodic stats capture enabled (path=%s)", statsPath);
        Platform.FrameTiming.logThread = thread([statsPath]()
        {
            FILE *statsFile = std::fopen(statsPath, "w");
            if (statsFile == nullptr)
            {
                LogWarn("[frame] Failed to open frame stats output at %s", statsPath);
            }
            else
            {
                std::fprintf(statsFile, "wall_s,avg_fps,avg_ms,low1_fps,high99_fps,p0_1_low_fps,p99_9_high_fps,p50_ms,p95_ms,p99_ms,acq_avg_ms,record_avg_ms,submit_avg_ms,acq_wait_avg_ms,acq_call_avg_ms,img_wait_avg_ms,gpu_shadow_avg_ms,gpu_forward_avg_ms,gpu_total_avg_ms,samples,window,gpu_samples\n");
            }

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
                    if (statsFile != nullptr)
                    {
                        std::fprintf(
                            statsFile,
                            "%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u\n",
                            snapshot.wallTimeSeconds,
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
                }
                else
                {
                    if (statsFile != nullptr)
                    {
                        std::fprintf(
                            statsFile,
                            "%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,nan,nan,nan,%u,%u,%u\n",
                            snapshot.wallTimeSeconds,
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
                            static_cast<unsigned>(historySamples),
                            0u);
                    }
                }
            }

            if (statsFile != nullptr)
            {
                std::fflush(statsFile);
                std::fclose(statsFile);
            }
        });
        Platform.FrameTiming.logThreadReady = true;
    }

    if (Platform.FrameTiming.hitchTraceEnabled && !Platform.FrameTiming.hitchThreadReady)
    {
        {
            lock_guard<mutex> lock(Platform.FrameTiming.hitchMutex);
            Platform.FrameTiming.hitchStopRequested = false;
            Platform.FrameTiming.hitchQueueHead = 0;
            Platform.FrameTiming.hitchQueueTail = 0;
            Platform.FrameTiming.hitchQueueCount = 0;
            Platform.FrameTiming.droppedHitchTraceCount = 0;
        }

        const char *tracePath = HitchTraceDefaultPath;
        Platform.FrameTiming.hitchThread = thread([tracePath]()
        {
            FILE *traceFile = std::fopen(tracePath, "w");
            if (traceFile == nullptr)
            {
                LogWarn("[hitch] Failed to open trace output at %s", tracePath);
            }
            else
            {
                int bufferResult = std::setvbuf(traceFile, nullptr, _IOLBF, 4096);
                if (bufferResult != 0)
                {
                    LogWarn("[hitch] Failed to set line buffering for trace output at %s", tracePath);
                }
                std::fprintf(traceFile, "event,wall_s,loop_frame,frame_ms,frame_work_ms,frame_outside_work_ms,poll_ms,input_ms,prep_ms,acq_ms,acq_wait_ms,acq_call_ms,img_wait_ms,record_ms,submit_ms,submit_reset_ms,queue_submit_ms,present_ms,recreate_ms,gpu_shadow_ms,gpu_forward_ms,gpu_total_ms,gpu_valid,frame_sample_valid,trigger_mask,frame_index,image_index,acquire_result,submit_result,samples,window\n");
            }

            while (true)
            {
                auto event = Platform.FrameTiming.hitchQueue[0];
                bool hasEvent = false;
                u32 droppedEventCount = 0;
                {
                    unique_lock<mutex> lock(Platform.FrameTiming.hitchMutex);
                    Platform.FrameTiming.hitchCondition.wait(lock, []()
                    {
                        return Platform.FrameTiming.hitchStopRequested || (Platform.FrameTiming.hitchQueueCount > 0);
                    });

                    if (Platform.FrameTiming.hitchQueueCount > 0)
                    {
                        u32 queueIndex = Platform.FrameTiming.hitchQueueHead;
                        event = Platform.FrameTiming.hitchQueue[queueIndex];
                        Platform.FrameTiming.hitchQueueHead = (queueIndex + 1u) % frameTimingHitchTraceQueueCapacity;
                        Platform.FrameTiming.hitchQueueCount -= 1;
                        droppedEventCount = Platform.FrameTiming.droppedHitchTraceCount;
                        Platform.FrameTiming.droppedHitchTraceCount = 0;
                        hasEvent = true;
                    }
                    else if (Platform.FrameTiming.hitchStopRequested)
                    {
                        break;
                    }
                }

                if (droppedEventCount > 0)
                {
                    LogWarn("[hitch] Dropped %u hitch events because trace queue was full", static_cast<unsigned>(droppedEventCount));
                }

                if (!hasEvent || (traceFile == nullptr))
                {
                    continue;
                }

                const char *eventName = "unknown";
                if (event.kind == HitchTraceEventFrame)
                {
                    eventName = "frame";
                }
                else if (event.kind == HitchTraceEventResize)
                {
                    eventName = "resize";
                }
                else if (event.kind == HitchTraceEventAcquireOutOfDate)
                {
                    eventName = "acquire_out_of_date";
                }
                else if (event.kind == HitchTraceEventSubmitOutOfDate)
                {
                    eventName = "submit_out_of_date";
                }
                else if (event.kind == HitchTraceEventAcquireFailure)
                {
                    eventName = "acquire_failure";
                }
                else if (event.kind == HitchTraceEventSubmitFailure)
                {
                    eventName = "submit_failure";
                }
                else if (event.kind == HitchTraceEventWarmupComplete)
                {
                    eventName = "warmup_complete";
                }

                std::fprintf(
                    traceFile,
                    "%s,%.6f,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%u,%u,%d,%d,%u,%u\n",
                    eventName,
                    event.wallTimeSeconds,
                    static_cast<unsigned long long>(event.loopFrameId),
                    event.frameMs,
                    event.frameWorkMs,
                    event.frameOutsideWorkMs,
                    event.pollEventsMs,
                    event.inputUpdateMs,
                    event.prepMs,
                    event.acquireMs,
                    event.acquireWaitFrameFenceMs,
                    event.acquireCallMs,
                    event.acquireWaitImageFenceMs,
                    event.recordMs,
                    event.submitMs,
                    event.submitResetFenceMs,
                    event.submitQueueMs,
                    event.presentMs,
                    event.recreateSwapchainMs,
                    event.gpuShadowMs,
                    event.gpuForwardMs,
                    event.gpuTotalMs,
                    event.gpuValid ? 1u : 0u,
                    event.frameSampleValid ? 1u : 0u,
                    event.triggerMask,
                    event.frameIndex,
                    event.imageIndex,
                    event.acquireResult,
                    event.submitResult,
                    event.frameSamples,
                    event.historySamples);
            }

            if (traceFile != nullptr)
            {
                std::fflush(traceFile);
                std::fclose(traceFile);
            }
        });
        Platform.FrameTiming.hitchThreadReady = true;
    }

    const auto queueHitchEvent = [&](const auto &event)
    {
        if (!Platform.FrameTiming.hitchThreadReady)
        {
            return;
        }

        bool queued = false;
        {
            lock_guard<mutex> lock(Platform.FrameTiming.hitchMutex);
            if (Platform.FrameTiming.hitchQueueCount < frameTimingHitchTraceQueueCapacity)
            {
                u32 queueIndex = Platform.FrameTiming.hitchQueueTail;
                Platform.FrameTiming.hitchQueue[queueIndex] = event;
                Platform.FrameTiming.hitchQueueTail = (queueIndex + 1u) % frameTimingHitchTraceQueueCapacity;
                Platform.FrameTiming.hitchQueueCount += 1;
                queued = true;
            }
            else
            {
                Platform.FrameTiming.droppedHitchTraceCount += 1;
            }
        }

        if (queued)
        {
            Platform.FrameTiming.hitchCondition.notify_one();
        }
    };

    const auto toMilliseconds = [](double seconds) -> float
    {
        float ms = static_cast<float>(seconds * 1000.0);
        if (!std::isfinite(ms) || (ms < 0.0f))
        {
            return 0.0f;
        }
        return ms;
    };
    double frameCapSeconds = (frameTimingCapFps > 0.0) ? (1.0 / frameTimingCapFps) : 0.0;
    double cameraStepAccumulatorSeconds = 0.0;
    double cameraFixedStepSeconds = static_cast<double>(cameraFixedDeltaSeconds);
    double cameraMaxAccumulatedSeconds = cameraFixedStepSeconds * static_cast<double>(cameraMaxSubstepsPerFrame);

    while (!WindowShouldClose())
    {
        double frameStartTime = glfwGetTime();
        double deltaSeconds = frameStartTime - Platform.FrameTiming.lastTime;
        u64 loopFrameId = Platform.FrameTiming.loopFrameCounter;
        Platform.FrameTiming.loopFrameCounter += 1;
        if (deltaSeconds < 0.0)
        {
            deltaSeconds = 0.0;
        }

        Platform.FrameTiming.lastTime = frameStartTime;
        Platform.FrameTiming.deltaSeconds = static_cast<float>(deltaSeconds);
        float deltaMs = toMilliseconds(deltaSeconds);

        double pollStartTime = glfwGetTime();
        PollEvents();
        double pollEndTime = glfwGetTime();
        float pollEventsMs = toMilliseconds(pollEndTime - pollStartTime);

        double inputStartTime = glfwGetTime();
        GLFWwindow *window = GetWindowHandle();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        }

        // Keep camera motion deterministic by integrating with a fixed simulation step.
        cameraStepAccumulatorSeconds += Platform.FrameTiming.deltaSeconds;
        if (cameraStepAccumulatorSeconds > cameraMaxAccumulatedSeconds)
        {
            cameraStepAccumulatorSeconds = cameraMaxAccumulatedSeconds;
        }

        u32 cameraSubsteps = 0;
        while ((cameraStepAccumulatorSeconds >= cameraFixedStepSeconds) && (cameraSubsteps < cameraMaxSubstepsPerFrame))
        {
            UpdateCameraFromInput(cameraFixedDeltaSeconds);
            cameraStepAccumulatorSeconds -= cameraFixedStepSeconds;
            cameraSubsteps += 1;
        }

        if (cameraSubsteps == 0)
        {
            UpdateCameraFromInput(0.0f);
        }

        double inputEndTime = glfwGetTime();
        float inputUpdateMs = toMilliseconds(inputEndTime - inputStartTime);

        if (ConsumeFramebufferResize())
        {
            float recreateSwapchainMs = 0.0f;
            if (Platform.FrameTiming.hitchTraceEnabled)
            {
                double recreateStartTime = glfwGetTime();
                ResetCameraAccum();
                RecreateSwapchain();
                double recreateEndTime = glfwGetTime();
                recreateSwapchainMs = toMilliseconds(recreateEndTime - recreateStartTime);

                auto hitchEvent = Platform.FrameTiming.hitchQueue[0];
                hitchEvent = {};
                hitchEvent.kind = HitchTraceEventResize;
                hitchEvent.loopFrameId = loopFrameId;
                hitchEvent.wallTimeSeconds = recreateEndTime;
                hitchEvent.frameMs = deltaMs;
                hitchEvent.pollEventsMs = pollEventsMs;
                hitchEvent.inputUpdateMs = inputUpdateMs;
                hitchEvent.recreateSwapchainMs = recreateSwapchainMs;
                hitchEvent.frameSampleValid = false;
                queueHitchEvent(hitchEvent);
            }
            else
            {
                ResetCameraAccum();
                RecreateSwapchain();
            }
            continue;
        }

        u32 imageIndex = 0;
        u32 frameIndex = 0;
        AcquireTiming acquireTiming = {};
        VkResult acquireResult = AcquireNextImage(imageIndex, frameIndex, acquireTiming);
        float acquireMs = acquireTiming.totalMs;

        if ((acquireResult == VK_ERROR_OUT_OF_DATE_KHR) || (acquireResult == VK_SUBOPTIMAL_KHR))
        {
            float recreateSwapchainMs = 0.0f;
            double recreateStartTime = glfwGetTime();
            RecreateSwapchain();
            double recreateEndTime = glfwGetTime();
            recreateSwapchainMs = toMilliseconds(recreateEndTime - recreateStartTime);
            if (Platform.FrameTiming.hitchTraceEnabled)
            {
                auto hitchEvent = Platform.FrameTiming.hitchQueue[0];
                hitchEvent = {};
                hitchEvent.kind = HitchTraceEventAcquireOutOfDate;
                hitchEvent.loopFrameId = loopFrameId;
                hitchEvent.wallTimeSeconds = recreateEndTime;
                hitchEvent.frameIndex = frameIndex;
                hitchEvent.imageIndex = imageIndex;
                hitchEvent.acquireResult = acquireResult;
                hitchEvent.submitResult = VK_SUCCESS;
                hitchEvent.frameMs = deltaMs;
                hitchEvent.pollEventsMs = pollEventsMs;
                hitchEvent.inputUpdateMs = inputUpdateMs;
                hitchEvent.acquireMs = acquireMs;
                hitchEvent.acquireWaitFrameFenceMs = acquireTiming.waitFrameFenceMs;
                hitchEvent.acquireCallMs = acquireTiming.acquireCallMs;
                hitchEvent.acquireWaitImageFenceMs = acquireTiming.waitImageFenceMs;
                hitchEvent.recreateSwapchainMs = recreateSwapchainMs;
                hitchEvent.gpuValid = acquireTiming.gpuValid;
                hitchEvent.gpuShadowMs = acquireTiming.gpuShadowMs;
                hitchEvent.gpuForwardMs = acquireTiming.gpuForwardMs;
                hitchEvent.gpuTotalMs = acquireTiming.gpuTotalMs;
                queueHitchEvent(hitchEvent);
            }
            continue;
        }

        if (acquireResult != VK_SUCCESS)
        {
            if (Platform.FrameTiming.hitchTraceEnabled)
            {
                auto hitchEvent = Platform.FrameTiming.hitchQueue[0];
                hitchEvent = {};
                hitchEvent.kind = HitchTraceEventAcquireFailure;
                hitchEvent.loopFrameId = loopFrameId;
                hitchEvent.wallTimeSeconds = glfwGetTime();
                hitchEvent.frameIndex = frameIndex;
                hitchEvent.imageIndex = imageIndex;
                hitchEvent.acquireResult = acquireResult;
                hitchEvent.submitResult = VK_SUCCESS;
                hitchEvent.frameMs = deltaMs;
                hitchEvent.pollEventsMs = pollEventsMs;
                hitchEvent.inputUpdateMs = inputUpdateMs;
                hitchEvent.acquireMs = acquireMs;
                hitchEvent.acquireWaitFrameFenceMs = acquireTiming.waitFrameFenceMs;
                hitchEvent.acquireCallMs = acquireTiming.acquireCallMs;
                hitchEvent.acquireWaitImageFenceMs = acquireTiming.waitImageFenceMs;
                hitchEvent.gpuValid = acquireTiming.gpuValid;
                hitchEvent.gpuShadowMs = acquireTiming.gpuShadowMs;
                hitchEvent.gpuForwardMs = acquireTiming.gpuForwardMs;
                hitchEvent.gpuTotalMs = acquireTiming.gpuTotalMs;
                queueHitchEvent(hitchEvent);
            }
            LogError("[vulkan] AcquireNextImage failed (result=%d)", acquireResult);
            break;
        }

        double prepStartTime = glfwGetTime();
        GradientParams gradient = {};
        Size framebuffer = GetFramebufferSize();
        gradient.resolution.x = (framebuffer.width > 0)? static_cast<float>(framebuffer.width) : 1.0f;
        gradient.resolution.y = (framebuffer.height > 0)? static_cast<float>(framebuffer.height) : 1.0f;
        gradient.time = static_cast<float>(glfwGetTime());
        gradient.padding = 0.0f;
        double prepEndTime = glfwGetTime();
        float prepMs = toMilliseconds(prepEndTime - prepStartTime);

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

        SubmitTiming submitTiming = {};
        VkResult submitResult = SubmitFrame(frameIndex, imageIndex, submitTiming);
        double submitEndTime = glfwGetTime();
        float submitMs = submitTiming.totalMs;

        if ((submitResult == VK_ERROR_OUT_OF_DATE_KHR) || (submitResult == VK_SUBOPTIMAL_KHR))
        {
            float recreateSwapchainMs = 0.0f;
            double recreateStartTime = glfwGetTime();
            RecreateSwapchain();
            double recreateEndTime = glfwGetTime();
            recreateSwapchainMs = toMilliseconds(recreateEndTime - recreateStartTime);
            if (Platform.FrameTiming.hitchTraceEnabled)
            {
                auto hitchEvent = Platform.FrameTiming.hitchQueue[0];
                hitchEvent = {};
                hitchEvent.kind = HitchTraceEventSubmitOutOfDate;
                hitchEvent.loopFrameId = loopFrameId;
                hitchEvent.wallTimeSeconds = recreateEndTime;
                hitchEvent.frameIndex = frameIndex;
                hitchEvent.imageIndex = imageIndex;
                hitchEvent.acquireResult = acquireResult;
                hitchEvent.submitResult = submitResult;
                hitchEvent.frameMs = deltaMs;
                hitchEvent.pollEventsMs = pollEventsMs;
                hitchEvent.inputUpdateMs = inputUpdateMs;
                hitchEvent.prepMs = prepMs;
                hitchEvent.acquireMs = acquireMs;
                hitchEvent.acquireWaitFrameFenceMs = acquireTiming.waitFrameFenceMs;
                hitchEvent.acquireCallMs = acquireTiming.acquireCallMs;
                hitchEvent.acquireWaitImageFenceMs = acquireTiming.waitImageFenceMs;
                hitchEvent.recordMs = recordMs;
                hitchEvent.submitMs = submitMs;
                hitchEvent.submitResetFenceMs = submitTiming.resetFenceMs;
                hitchEvent.submitQueueMs = submitTiming.queueSubmitMs;
                hitchEvent.presentMs = submitTiming.queuePresentMs;
                hitchEvent.recreateSwapchainMs = recreateSwapchainMs;
                hitchEvent.gpuValid = acquireTiming.gpuValid;
                hitchEvent.gpuShadowMs = acquireTiming.gpuShadowMs;
                hitchEvent.gpuForwardMs = acquireTiming.gpuForwardMs;
                hitchEvent.gpuTotalMs = acquireTiming.gpuTotalMs;
                queueHitchEvent(hitchEvent);
            }
            continue;
        }

        if (submitResult != VK_SUCCESS)
        {
            if (Platform.FrameTiming.hitchTraceEnabled)
            {
                auto hitchEvent = Platform.FrameTiming.hitchQueue[0];
                hitchEvent = {};
                hitchEvent.kind = HitchTraceEventSubmitFailure;
                hitchEvent.loopFrameId = loopFrameId;
                hitchEvent.wallTimeSeconds = submitEndTime;
                hitchEvent.frameIndex = frameIndex;
                hitchEvent.imageIndex = imageIndex;
                hitchEvent.acquireResult = acquireResult;
                hitchEvent.submitResult = submitResult;
                hitchEvent.frameMs = deltaMs;
                hitchEvent.pollEventsMs = pollEventsMs;
                hitchEvent.inputUpdateMs = inputUpdateMs;
                hitchEvent.prepMs = prepMs;
                hitchEvent.acquireMs = acquireMs;
                hitchEvent.acquireWaitFrameFenceMs = acquireTiming.waitFrameFenceMs;
                hitchEvent.acquireCallMs = acquireTiming.acquireCallMs;
                hitchEvent.acquireWaitImageFenceMs = acquireTiming.waitImageFenceMs;
                hitchEvent.recordMs = recordMs;
                hitchEvent.submitMs = submitMs;
                hitchEvent.submitResetFenceMs = submitTiming.resetFenceMs;
                hitchEvent.submitQueueMs = submitTiming.queueSubmitMs;
                hitchEvent.presentMs = submitTiming.queuePresentMs;
                hitchEvent.gpuValid = acquireTiming.gpuValid;
                hitchEvent.gpuShadowMs = acquireTiming.gpuShadowMs;
                hitchEvent.gpuForwardMs = acquireTiming.gpuForwardMs;
                hitchEvent.gpuTotalMs = acquireTiming.gpuTotalMs;
                queueHitchEvent(hitchEvent);
            }
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
        float frameOutsideWorkMs = frameMs - frameWorkMs;
        if (!std::isfinite(frameOutsideWorkMs) || (frameOutsideWorkMs < 0.0f))
        {
            frameOutsideWorkMs = 0.0f;
        }
        bool frameSampleValid = frameMs >= frameTimingMinSampleMs;

        if (!Platform.FrameTiming.warmupComplete &&
            ((frameEndTime - Platform.FrameTiming.resetTime) >= frameTimingWarmupSeconds))
        {
            Platform.FrameTiming.warmupComplete = true;
            Platform.FrameTiming.lastLogTime = frameEndTime;
            Platform.FrameTiming.accumulatedMs = 0.0;
            Platform.FrameTiming.samples = 0;
            if (Platform.FrameTiming.hitchTraceEnabled)
            {
                auto hitchEvent = Platform.FrameTiming.hitchQueue[0];
                hitchEvent = {};
                hitchEvent.kind = HitchTraceEventWarmupComplete;
                hitchEvent.loopFrameId = loopFrameId;
                hitchEvent.wallTimeSeconds = frameEndTime;
                hitchEvent.frameIndex = frameIndex;
                hitchEvent.imageIndex = imageIndex;
                hitchEvent.frameMs = frameMs;
                hitchEvent.frameWorkMs = frameWorkMs;
                hitchEvent.frameOutsideWorkMs = frameOutsideWorkMs;
                hitchEvent.pollEventsMs = pollEventsMs;
                hitchEvent.inputUpdateMs = inputUpdateMs;
                hitchEvent.prepMs = prepMs;
                hitchEvent.acquireMs = acquireMs;
                hitchEvent.acquireWaitFrameFenceMs = acquireTiming.waitFrameFenceMs;
                hitchEvent.acquireCallMs = acquireTiming.acquireCallMs;
                hitchEvent.acquireWaitImageFenceMs = acquireTiming.waitImageFenceMs;
                hitchEvent.recordMs = recordMs;
                hitchEvent.submitMs = submitMs;
                hitchEvent.submitResetFenceMs = submitTiming.resetFenceMs;
                hitchEvent.submitQueueMs = submitTiming.queueSubmitMs;
                hitchEvent.presentMs = submitTiming.queuePresentMs;
                hitchEvent.gpuValid = acquireTiming.gpuValid;
                hitchEvent.gpuShadowMs = acquireTiming.gpuShadowMs;
                hitchEvent.gpuForwardMs = acquireTiming.gpuForwardMs;
                hitchEvent.gpuTotalMs = acquireTiming.gpuTotalMs;
                queueHitchEvent(hitchEvent);
            }
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

        if (Platform.FrameTiming.hitchTraceEnabled)
        {
            u32 triggerMask = 0;
            if (!frameSampleValid)
            {
                triggerMask |= HitchTriggerInvalidSample;
            }
            if (frameMs >= Platform.FrameTiming.hitchTraceFrameThresholdMs)
            {
                triggerMask |= HitchTriggerFrameCadence;
            }
            if (frameWorkMs >= Platform.FrameTiming.hitchTraceWorkThresholdMs)
            {
                triggerMask |= HitchTriggerFrameWork;
            }
            if (submitTiming.queueSubmitMs >= Platform.FrameTiming.hitchTraceQueueSubmitThresholdMs)
            {
                triggerMask |= HitchTriggerQueueSubmit;
            }

            bool logFrameEvent = Platform.FrameTiming.hitchTraceAllFrames || (triggerMask != 0u);
            if (logFrameEvent)
            {
                auto hitchEvent = Platform.FrameTiming.hitchQueue[0];
                hitchEvent = {};
                hitchEvent.kind = HitchTraceEventFrame;
                hitchEvent.loopFrameId = loopFrameId;
                hitchEvent.wallTimeSeconds = frameEndTime;
                hitchEvent.frameIndex = frameIndex;
                hitchEvent.imageIndex = imageIndex;
                hitchEvent.frameSamples = Platform.FrameTiming.samples;
                hitchEvent.historySamples = Platform.FrameTiming.historyCount;
                hitchEvent.acquireResult = acquireResult;
                hitchEvent.submitResult = submitResult;
                hitchEvent.frameSampleValid = frameSampleValid;
                hitchEvent.gpuValid = acquireTiming.gpuValid;
                hitchEvent.triggerMask = triggerMask;
                hitchEvent.frameMs = frameMs;
                hitchEvent.frameWorkMs = frameWorkMs;
                hitchEvent.frameOutsideWorkMs = frameOutsideWorkMs;
                hitchEvent.pollEventsMs = pollEventsMs;
                hitchEvent.inputUpdateMs = inputUpdateMs;
                hitchEvent.prepMs = prepMs;
                hitchEvent.acquireMs = acquireMs;
                hitchEvent.acquireWaitFrameFenceMs = acquireTiming.waitFrameFenceMs;
                hitchEvent.acquireCallMs = acquireTiming.acquireCallMs;
                hitchEvent.acquireWaitImageFenceMs = acquireTiming.waitImageFenceMs;
                hitchEvent.recordMs = recordMs;
                hitchEvent.submitMs = submitMs;
                hitchEvent.submitResetFenceMs = submitTiming.resetFenceMs;
                hitchEvent.submitQueueMs = submitTiming.queueSubmitMs;
                hitchEvent.presentMs = submitTiming.queuePresentMs;
                hitchEvent.gpuShadowMs = acquireTiming.gpuValid ? acquireTiming.gpuShadowMs : std::numeric_limits<float>::quiet_NaN();
                hitchEvent.gpuForwardMs = acquireTiming.gpuValid ? acquireTiming.gpuForwardMs : std::numeric_limits<float>::quiet_NaN();
                hitchEvent.gpuTotalMs = acquireTiming.gpuValid ? acquireTiming.gpuTotalMs : std::numeric_limits<float>::quiet_NaN();
                queueHitchEvent(hitchEvent);
            }
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
                        snapshot.wallTimeSeconds = frameEndTime;
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

        if (frameCapSeconds > 0.0)
        {
            double frameElapsedSeconds = glfwGetTime() - frameStartTime;
            double remainingSeconds = frameCapSeconds - frameElapsedSeconds;
            if (remainingSeconds > 0.0)
            {
                constexpr double sleepGuardSeconds = 0.0005;
                if (remainingSeconds > sleepGuardSeconds)
                {
                    std::this_thread::sleep_for(std::chrono::duration<double>(remainingSeconds - sleepGuardSeconds));
                }
                while ((glfwGetTime() - frameStartTime) < frameCapSeconds)
                {
                    std::this_thread::yield();
                }
            }
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
