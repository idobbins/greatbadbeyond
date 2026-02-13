#include <greadbadbeyond.h>

auto main() -> int
{
    CreateGlfwContext();
    CreateWindow();
    CreateCamera();
    CreateVulkan();

    MainLoop();

    DestroyVulkan();
    DestroyCamera();
    DestroyWindow();
    DestroyGlfwContext();

    return 0;
}
