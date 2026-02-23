#include <windows.h>
#include "platform.h"

static const char* const WINDOW_CLASS_NAME = "greatbadbeyond_window_class";

static const uint32_t MAX_PUMP_EVENTS_PER_CALL = 64u;
static HINSTANCE instance_handle = NULL;
void *window_handle = NULL;
static uint32_t should_quit = 0u;
static uint8_t key_states[GBB_KEY_COUNT] = {0u};
static float mouse_delta_x = 0.0f;
static float mouse_delta_y = 0.0f;
static int32_t mouse_last_x = 0;
static int32_t mouse_last_y = 0;
static uint32_t mouse_has_last = 0u;
static uint32_t mouse_tracking = 0u;

static int gbbMapVirtualKey(WPARAM wParam, uint32_t* key)
{
    switch (wParam)
    {
        case 'W': *key = GBB_KEY_W; return 1;
        case 'A': *key = GBB_KEY_A; return 1;
        case 'S': *key = GBB_KEY_S; return 1;
        case 'D': *key = GBB_KEY_D; return 1;
        case 'Q': *key = GBB_KEY_Q; return 1;
        case 'E': *key = GBB_KEY_E; return 1;
        case VK_LEFT: *key = GBB_KEY_LEFT; return 1;
        case VK_RIGHT: *key = GBB_KEY_RIGHT; return 1;
        case VK_UP: *key = GBB_KEY_UP; return 1;
        case VK_DOWN: *key = GBB_KEY_DOWN; return 1;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            *key = GBB_KEY_SHIFT;
            return 1;
        default:
            break;
    }
    return 0;
}

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
            uint32_t key = 0u;
            if (gbbMapVirtualKey(wParam, &key) != 0) key_states[key] = 1u;
            should_quit |= is_escape;
            if (is_escape != 0u)
            {
                DestroyWindow(window);
                return 0;
            }
            break;
        }
        case WM_KEYUP:
        {
            uint32_t key = 0u;
            if (gbbMapVirtualKey(wParam, &key) != 0) key_states[key] = 0u;
            break;
        }
        case WM_MOUSEMOVE:
        {
            const int32_t x = GET_X_LPARAM(lParam);
            const int32_t y = GET_Y_LPARAM(lParam);
            if (mouse_has_last != 0u)
            {
                mouse_delta_x += (float)(x - mouse_last_x);
                mouse_delta_y += (float)(y - mouse_last_y);
            }
            mouse_last_x = x;
            mouse_last_y = y;
            mouse_has_last = 1u;

            if (mouse_tracking == 0u)
            {
                TRACKMOUSEEVENT track = {
                    .cbSize = sizeof(TRACKMOUSEEVENT),
                    .dwFlags = TME_LEAVE,
                    .hwndTrack = window,
                    .dwHoverTime = HOVER_DEFAULT
                };
                TrackMouseEvent(&track);
                mouse_tracking = 1u;
            }
            break;
        }
        case WM_MOUSELEAVE:
        {
            mouse_tracking = 0u;
            mouse_has_last = 0u;
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
    mouse_delta_x = 0.0f;
    mouse_delta_y = 0.0f;
    mouse_has_last = 0u;
    mouse_tracking = 0u;
    return 0;
}

void gbbShutdownWindow(void)
{
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

int gbbIsKeyDown(uint32_t key)
{
    if (key >= GBB_KEY_COUNT) return 0;
    return (int)key_states[key];
}

void gbbConsumeMouseDelta(float* delta_x, float* delta_y)
{
    if (delta_x) *delta_x = mouse_delta_x;
    if (delta_y) *delta_y = mouse_delta_y;
    mouse_delta_x = 0.0f;
    mouse_delta_y = 0.0f;
}

uint64_t gbbGetTimeNs(void)
{
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart * 1000000000ULL / freq.QuadPart);
}
