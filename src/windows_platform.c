#include <windows.h>
#include "platform.h"

static const char* const WINDOW_CLASS_NAME = "greatbadbeyond_window_class";

static HINSTANCE g_instance = NULL;
static HWND g_window = NULL;
static int g_exit = 0;
static void* hwnd = NULL;

static LRESULT CALLBACK gbbWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CLOSE:
            g_exit = 1;
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            g_exit = 1;
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)  // single, predictable, non-hot-path branch
            {
                g_exit = 1;
                DestroyWindow(window);
                return 0;
            }
            break;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

int gbbInitWindow(uint32_t width, uint32_t height, const char* title)
{
    g_instance = GetModuleHandleA(NULL);
    if (!g_instance) return 1;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = gbbWindowProc;
    wc.hInstance     = g_instance;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect = {0, 0, (LONG)width, (LONG)height};
    if (!AdjustWindowRect(&rect, style, FALSE))
        return 1;

    const char* title_str = title ? title : "";
    g_window = CreateWindowExA(0, WINDOW_CLASS_NAME, title_str,
                               style, CW_USEDEFAULT, CW_USEDEFAULT,
                               rect.right - rect.left, rect.bottom - rect.top,
                               NULL, NULL, g_instance, NULL);
    if (!g_window) return 1;

    ShowWindow(g_window, SW_SHOWNORMAL);
    UpdateWindow(g_window);

    hwnd = (void*)g_window;
    g_exit = 0;
    return 0;
}

void gbbShutdownWindow(void)
{
    if (g_window)
    {
        DestroyWindow(g_window);
        g_window = NULL;
    }
    if (g_instance)
    {
        UnregisterClassA(WINDOW_CLASS_NAME, g_instance);
        g_instance = NULL;
    }
    hwnd = NULL;
    g_exit = 1;
}

int gbbPumpEventsOnce(void)
{
    MSG message = {0};

    for (int i = 0; i < 64 && !g_exit && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE); i++)
    {
        switch (message.message)
        {
            case WM_QUIT:
                g_exit = 1;
                break;

            default:
                TranslateMessage(&message);
                DispatchMessageA(&message);
                break;
        }
    }

    g_exit |= (!g_window || !IsWindow(g_window));

    return g_exit;
}
