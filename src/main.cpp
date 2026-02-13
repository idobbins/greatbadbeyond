#include <greadbadbeyond.h>

auto main() -> int
{
    CreateGlfwContext();
    CreateWindow();
    CreateCamera();
    CreateManifestBlob();
    CreateVulkan();

    MainLoop();

    DestroyVulkan();
    DestroyManifestBlob();
    DestroyCamera();
    DestroyWindow();
    DestroyGlfwContext();

    return 0;
}
