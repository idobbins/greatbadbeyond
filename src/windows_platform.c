#include <windows.h>

#include "platform.h"

static const char* WINDOW_CLASS_NAME = "greatbadbeyond_window_class";

static HINSTANCE g_instance = NULL;
static HWND g_window = NULL;
static int g_exit = 0;

void *hwnd = NULL;

static LRESULT CALLBACK gbbWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CLOSE)
    {
        g_exit = 1;
        DestroyWindow(window);
        return 0;
    }

    if (message == WM_DESTROY)
    {
        g_exit = 1;
        PostQuitMessage(0);
        return 0;
    }

    if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        g_exit = 1;
        DestroyWindow(window);
        return 0;
    }

    return DefWindowProcA(window, message, wParam, lParam);
}

int gbbInitWindow(uint32_t width, uint32_t height, const char* title)
{
    g_instance = GetModuleHandleA(NULL);
    if (!g_instance) return 1;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = gbbWindowProc;
    wc.hInstance = g_instance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect = {0, 0, (LONG)width, (LONG)height};
    if (!AdjustWindowRect(&rect, style, FALSE)) return 1;

    g_window = CreateWindowExA(0, WINDOW_CLASS_NAME, title ? title : "",
                               style, CW_USEDEFAULT, CW_USEDEFAULT,
                               rect.right - rect.left, rect.bottom - rect.top,
                               NULL, NULL, g_instance, NULL);
    if (!g_window) return 1;

    ShowWindow(g_window, SW_SHOWNORMAL);
    UpdateWindow(g_window);

    hwnd = (void *)g_window;
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
    while (!g_exit && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            g_exit = 1;
        }
        else
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }

    if (!g_window || !IsWindow(g_window)) g_exit = 1;
    return g_exit;
}
