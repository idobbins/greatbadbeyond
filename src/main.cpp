#include <callandor.h>

#include <iostream>

using namespace std;

int main()
{
    CreateGlfwContext();
    CreateWindow();

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

    CreateVulkan(config);

    while (!WindowShouldClose())
    {
        PollEvents();
    }
    DestroyVulkan(config);

    DestroyWindow();
    DestroyGlfwContext();

    return 0;
}
