#include <callandor.h>

#include <iostream>

using namespace std;

int main()
{
    InitGlfwContext();
    InitWindow();

    InitVulkan();

    while(!WindowShouldClose())
    {
        PollEvents();
    }

    CloseWindow();
    CloseGlfwContext();

    return 0;
}
