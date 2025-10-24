#include <callandor.h>

auto main() -> int
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
