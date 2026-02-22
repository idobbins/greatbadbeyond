#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdint.h>

#include "platform.h"

static NSWindow* window_handle = nil;
void *surface_layer = NULL;
static uint32_t should_quit = 0u;

int gbbInitWindow(uint32_t width, uint32_t height, const char* title)
{
    @autoreleasepool {
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
    }
    return 0;
}

void gbbShutdownWindow(void)
{
    @autoreleasepool {
        [window_handle close];
        window_handle = nil;
        surface_layer = NULL;
        should_quit = 1u;
    }
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
            const uint32_t is_escape = (uint32_t)([event type] == NSEventTypeKeyDown && [event keyCode] == 53);
            should_quit |= is_escape;
            if (!is_escape) [NSApp sendEvent:event];
        }

        should_quit |= (uint32_t)(![window_handle isVisible]);
    }
    return (int)should_quit;
}
