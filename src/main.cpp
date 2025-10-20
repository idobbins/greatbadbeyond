#include <callandor.h>

#include <iostream>

using namespace std;

int main()
{
    InitGlfwContext();
    InitWindow();

    VulkanConfig config = {};

#ifndef NDEBUG
    config.debug = true;
#else
    config.debug = false;
#endif

#if defined(__APPLE__)
    config.portability = true;
#else
    config.portability = false;
#endif

    InitVulkan(config);

    while (!WindowShouldClose())
    {
        PollEvents();
    }
    CloseVulkan(config);

    CloseWindow();
    CloseGlfwContext();

    return 0;
}
