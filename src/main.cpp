#include <callandor.h>
#include <runtime.h>

#include <GLFW/glfw3.h>

namespace
{
void ConfigureWindow(Config<Window> &config)
{
    config.width = 1280;
    config.height = 720;
    config.title = "callandor";
    config.resizable = false;
}

bool ShouldQuit(Window &window)
{
    if (ShouldClose(window)) {
        return true;
    }

    if (IsKeyPressed(window, GLFW_KEY_ESCAPE)) {
        return true;
    }

    return false;
}
} // namespace

int main()
{
    Config<Window> config{};
    ConfigureWindow(config);

    Window window = Create(config);
    Assert(IsReady(window), "Window creation failed");

    while (IsReady(window)) {
        Poll(window);

        if (ShouldQuit(window)) {
            break;
        }
    }

    Destroy(window);
    return 0;
}
