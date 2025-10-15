#pragma once

#include "types.cpp"
#include "assert.cpp"

#include <GLFW/glfw3.h>

#include <array>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

using namespace std;

struct GlfwContext { bool initialized = false; };
struct WindowConfig {
    i32 width = 1280;
    i32 height = 720;
    string title = string("callandor");
    bool resizable = false;
};
struct Window { GLFWwindow* handle = nullptr; };

static void glfwErrorCallback(i32 code, const char* desc) {
    cerr << "[glfw][error " << code << "] " << (desc ? desc : "no description") << '\n';
}

static GlfwContext createGlfwContext() {
    glfwSetErrorCallback(glfwErrorCallback);
    runtime_assert(glfwInit() == GLFW_TRUE, "Failed to initialize GLFW");
    runtime_assert(glfwVulkanSupported() == GLFW_TRUE, "GLFW was not compiled with Vulkan support");
    return {.initialized = true};
}

static void destroyGlfwContext(const GlfwContext& ctx) {
    if (!ctx.initialized) return;
    glfwTerminate();
    glfwSetErrorCallback(nullptr);
}

static void applyWindowHints(const WindowConfig& c) {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, c.resizable ? GLFW_TRUE : GLFW_FALSE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif
}

static Window createWindow(const GlfwContext& ctx, const WindowConfig& c = {}) {
    runtime_assert(ctx.initialized, "GLFW context must be initialized before creating windows");
    applyWindowHints(c);
    GLFWwindow* w = glfwCreateWindow(c.width, c.height, c.title.c_str(), nullptr, nullptr);
    runtime_assert(w != nullptr, "Failed to create GLFW window");
    return {.handle = w};
}

static void destroyWindow(const Window& w) {
    if (w.handle) glfwDestroyWindow(w.handle);
}

static bool windowShouldClose(const Window& w) {
    return !w.handle || glfwWindowShouldClose(w.handle) == GLFW_TRUE;
}

static void pollWindowEvents() { glfwPollEvents(); }

static void getFramebufferSize(const Window& w, i32& width, i32& height) {
    if (!w.handle) { width = height = 0; return; }
    glfwGetFramebufferSize(w.handle, &width, &height);
}

// Mirrors enumerate* style in instance.cpp: cache + span.
static span<const char* const> enumeratePlatformInstanceExtensions() {
    static array<const char*, 8> cache{}; // generous cap
    static u32 count = 0;
    static once_flag once;
    call_once(once, [] {
        u32 n = 0;
        const char** exts = glfwGetRequiredInstanceExtensions(&n);
        runtime_assert(exts && n > 0, "glfwGetRequiredInstanceExtensions failed");
        runtime_assert(n <= cache.size(), "Too many GLFW-required extensions for cache");
        for (u32 i = 0; i < n; ++i) cache[i] = exts[i];
        count = n;
    });
    return {cache.data(), count};
}
