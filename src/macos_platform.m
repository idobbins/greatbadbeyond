#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <mach/mach_time.h>
#include <stdint.h>

#include "platform.h"

static NSWindow* window_handle = nil;
void *surface_layer = NULL;
static uint32_t should_quit = 0u;
static uint8_t key_states[GBB_KEY_COUNT] = {0u};
static float mouse_delta_x = 0.0f;
static float mouse_delta_y = 0.0f;

static int gbbMapMacKeyCode(unsigned short keyCode, uint32_t* key)
{
    switch (keyCode)
    {
        case 13: *key = GBB_KEY_W; return 1;      // W
        case 0: *key = GBB_KEY_A; return 1;       // A
        case 1: *key = GBB_KEY_S; return 1;       // S
        case 2: *key = GBB_KEY_D; return 1;       // D
        case 12: *key = GBB_KEY_Q; return 1;      // Q
        case 14: *key = GBB_KEY_E; return 1;      // E
        case 123: *key = GBB_KEY_LEFT; return 1;  // Left Arrow
        case 124: *key = GBB_KEY_RIGHT; return 1; // Right Arrow
        case 126: *key = GBB_KEY_UP; return 1;    // Up Arrow
        case 125: *key = GBB_KEY_DOWN; return 1;  // Down Arrow
        case 56:
        case 60:
            *key = GBB_KEY_SHIFT;
            return 1;
        default:
            break;
    }
    return 0;
}

int gbbInitWindow(uint32_t width, uint32_t height, const char* title)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    window_handle = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, (CGFloat)width, (CGFloat)height)
                                         styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    [window_handle center];

    NSView* view = [window_handle contentView];
    surface_layer = (void *)[CAMetalLayer layer];
    if (!(window_handle && view && surface_layer)) return 1;

    [window_handle setReleasedWhenClosed:NO];
    [window_handle setAcceptsMouseMovedEvents:YES];
    [window_handle setTitle:title ? [NSString stringWithUTF8String:title] : @""];
    [view setWantsLayer:YES];
    [(CAMetalLayer*)surface_layer setOpaque:YES];
    const CGFloat scale = [window_handle backingScaleFactor];
    [(CAMetalLayer*)surface_layer setContentsScale:scale];
    [(CAMetalLayer*)surface_layer setDrawableSize:CGSizeMake((CGFloat)width * scale, (CGFloat)height * scale)];
    [view setLayer:(CAMetalLayer*)surface_layer];
    [window_handle makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    should_quit = 0u;
    return 0;
}

void gbbShutdownWindow(void)
{
    should_quit = 1u;
}

int gbbPumpEventsOnce(void)
{
    @autoreleasepool {
        NSEvent* event = nil;
        if (!window_handle) return 1;

        while (!should_quit &&
               (event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]) != nil) {
            const uint32_t is_key_down = (uint32_t)([event type] == NSEventTypeKeyDown);
            const uint32_t is_key_up = (uint32_t)([event type] == NSEventTypeKeyUp);
            uint32_t key = 0u;
            if ((is_key_down || is_key_up) && (gbbMapMacKeyCode([event keyCode], &key) != 0))
            {
                key_states[key] = is_key_down ? 1u : 0u;
            }
            const uint32_t is_escape = (uint32_t)(is_key_down && [event keyCode] == 53);
            should_quit |= is_escape;
            if (!is_escape) [NSApp sendEvent:event];

            const NSEventType type = [event type];
            if ((type == NSEventTypeMouseMoved) ||
                (type == NSEventTypeLeftMouseDragged) ||
                (type == NSEventTypeRightMouseDragged) ||
                (type == NSEventTypeOtherMouseDragged))
            {
                mouse_delta_x += (float)[event deltaX];
                mouse_delta_y += (float)(-[event deltaY]);
            }
        }

        should_quit |= (uint32_t)(![window_handle isVisible]);
    }
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
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0)
    {
        mach_timebase_info(&timebase);
    }
    const uint64_t time = mach_absolute_time();
    return time * timebase.numer / timebase.denom;
}
