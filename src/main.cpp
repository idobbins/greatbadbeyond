#include <callandor.h>

#include <iostream>

using namespace std;

int main()
{
    CreateGlfwContext();
    CreateWindow();

    CreateVulkan();

    MainLoop();

    DestroyVulkan();

    DestroyWindow();
    DestroyGlfwContext();

    return 0;
}
