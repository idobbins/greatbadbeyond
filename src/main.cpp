#include <callandor.h>

#include <iostream>

using namespace std;

int main()
{
    InitGlfwContext();

    InitWindow();

    while(!WindowShouldClose())
    {
        PollEvents();
    }

    CloseWindow();

    CloseGlfwContext();

    return 0;
}
