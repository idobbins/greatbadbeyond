#include "types.cpp"
#include "assert.cpp"
#include "defer.cpp"
#include "vulkan/headers.cpp"
#include "glfw/window.cpp"
#include "vulkan/instance.cpp"
#include "vulkan/device.cpp"

int main()
{
    GlfwContext glfw = createGlfwContext();
    defer { destroyGlfwContext(glfw); };

    Window window = createWindow(glfw);
    defer { destroyWindow(window); };

    constexpr InstanceConfig instanceConfig{
#if defined(NDEBUG)
        .enableDebug = false,
#else
        .enableDebug = true,
#endif
    };

    const auto [instance, debugMessenger] = createInstance(instanceConfig);
    defer { destroyInstance(instance); };
    defer { destroyDebugMessenger(instance, debugMessenger); };

    VkSurfaceKHR surface = createSurface(instance, window);
    defer { destroySurface(instance, surface); };

    Device device = createDevice(instance, surface);
    defer { destroyDevice(device.logical); };

    while (!windowShouldClose(window)) {
        pollWindowEvents();
    }

    return EXIT_SUCCESS;
}
