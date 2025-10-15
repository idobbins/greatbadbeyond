#include "types.cpp"
#include "assert.cpp"
#include "defer.cpp"

#include "glfw/window.cpp"

#include "vulkan/headers.cpp"
#include "vulkan/instance.cpp"

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

    while (!windowShouldClose(window)) {
        pollWindowEvents();
    }

    return EXIT_SUCCESS;
}
