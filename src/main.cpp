#include <callandor.h>

#include <iostream>

using namespace std;

int main()
{
    InitGlfwContext();

    auto exts = GetWindowVulkanExtensions();

    for (auto ext : exts)
    {
        cout << ext << endl;
    }

    CloseGlfwContext();

    return 0;
}
