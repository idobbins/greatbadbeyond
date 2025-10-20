#include <callandor.h>

#include <iostream>

using namespace std;

int main()
{
    InitGlfwContext();

    InitWindow();

    auto [w, h] = GetFramebufferSize();
    cout << w << "x" << h << endl;

    while(!WindowShouldClose())
    {
        PollEvents();
    }

    CloseWindow();

    CloseGlfwContext();

    return 0;
}
