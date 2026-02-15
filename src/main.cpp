#include <greadbadbeyond.h>

auto main() -> int
{
    CreateGlfwContext();
    CreateWindow();
    CreateCamera();
    CreateManifestBlob();
    CreateVulkan();
    ResetFrameTiming();

    MainLoop();

    DestroyVulkan();
    DestroyManifestBlob();
    DestroyCamera();
    DestroyWindow();
    DestroyGlfwContext();

    return 0;
}
