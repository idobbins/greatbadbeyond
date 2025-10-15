#include "types.cpp"
#include "assert.cpp"
#include "defer.cpp"

#include "vulkan/headers.cpp"
#include "vulkan/instance.cpp"

int main()
{
    constexpr InstanceConfig instanceConfig{
#if defined(NDEBUG)
        .enableDebug = false,
#else
        .enableDebug = true,
#endif
    };

    Instance instance = createInstance(instanceConfig);
    defer { destroyInstance(instance); };

    return 0;
}
