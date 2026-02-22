#include <windows.h>
#include "platform.h"

static const char* const WINDOW_CLASS_NAME = "greatbadbeyond_window_class";

static const uint32_t MAX_PUMP_EVENTS_PER_CALL = 64u;
static HINSTANCE instance_handle = NULL;
void *window_handle = NULL;
static uint32_t should_quit = 0u;

static LRESULT CALLBACK gbbWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CLOSE:
        {
            should_quit = 1u;
            DestroyWindow(window);
            return 0;
        }
        case WM_DESTROY:
        {
            should_quit = 1u;
            PostQuitMessage(0);
            return 0;
        }
        case WM_KEYDOWN:
        {
            const uint32_t is_escape = (wParam == VK_ESCAPE) ? 1u : 0u;
            should_quit |= is_escape;
            if (is_escape != 0u)
            {
                DestroyWindow(window);
                return 0;
            }
            break;
        }
        default:
            break;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

int gbbInitWindow(uint32_t width, uint32_t height, const char* title)
{
    const char* const title_text = title ? title : "";
    const DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect = {0, 0, (LONG)width, (LONG)height};
    WNDCLASSA window_class = {0};

    instance_handle = GetModuleHandleA(NULL);
    if (!instance_handle) return 1;

    window_class.lpfnWndProc   = gbbWindowProc;
    window_class.hInstance     = instance_handle;
    window_class.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassA(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    if (!AdjustWindowRect(&rect, window_style, FALSE)) return 1;

    window_handle = (void*)CreateWindowExA(0, WINDOW_CLASS_NAME, title_text,
                                           window_style, CW_USEDEFAULT, CW_USEDEFAULT,
                                           rect.right - rect.left, rect.bottom - rect.top,
                                           NULL, NULL, instance_handle, NULL);
    if (!window_handle) return 1;

    ShowWindow((HWND)window_handle, SW_SHOWNORMAL);
    UpdateWindow((HWND)window_handle);

    should_quit = 0u;
    return 0;
}

void gbbShutdownWindow(void)
{
    if (window_handle)
    {
        DestroyWindow((HWND)window_handle);
        window_handle = NULL;
    }
    if (instance_handle)
    {
        UnregisterClassA(WINDOW_CLASS_NAME, instance_handle);
        instance_handle = NULL;
    }
    should_quit = 1u;
}

int gbbPumpEventsOnce(void)
{
    const uint32_t exit_mask = (uint32_t)(!window_handle || !IsWindow((HWND)window_handle));
    MSG event = {0};

    for (uint32_t event_index = 0u;
         (event_index < MAX_PUMP_EVENTS_PER_CALL) && (should_quit == 0u) && PeekMessageA(&event, NULL, 0u, 0u, PM_REMOVE);
         event_index += 1u)
    {
        if (event.message == WM_QUIT)
        {
            should_quit = 1u;
        }
        else
        {
            TranslateMessage(&event);
            DispatchMessageA(&event);
        }
    }

    should_quit |= exit_mask;

    return (int)should_quit;
}
