#include <callandor.h>

#include <iostream>

using namespace std;

int main()
{
    InitGlfwContext();
    InitWindow();

#ifdef NDEBUG
    InitVulkan();
#else
    InitVulkan(true);
#endif

    while(!WindowShouldClose())
    {
        PollEvents();
    }
#ifdef NDEBUG
    CloseVulkan();
#else
    CloseVulkan(true);
#endif

    CloseWindow();
    CloseGlfwContext();

    return 0;
}
