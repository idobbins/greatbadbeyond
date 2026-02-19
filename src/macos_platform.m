#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "platform.h"

static NSWindow* g_win = nil;
void *layer = NULL;
static int g_exit = 0;

int gbbInitWindow(uint32_t w, uint32_t h, const char* title)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        g_win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, (CGFloat)w, (CGFloat)h)
                                             styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        [g_win center];

        NSView* view = [g_win contentView];
        layer = (void *)[CAMetalLayer layer];
        if (!(g_win && view && layer)) return 1;

        [g_win setReleasedWhenClosed:NO];
        [g_win setTitle:title ? [NSString stringWithUTF8String:title] : @""];
        [view setWantsLayer:YES];
        [(CAMetalLayer*)layer setOpaque:YES];
        const CGFloat scale = [g_win backingScaleFactor];
        [(CAMetalLayer*)layer setContentsScale:scale];
        [(CAMetalLayer*)layer setDrawableSize:CGSizeMake((CGFloat)w * scale, (CGFloat)h * scale)];
        [view setLayer:(CAMetalLayer*)layer];
        [g_win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        g_exit = 0;
    }
    return 0;
}

void gbbShutdownWindow(void)
{
    @autoreleasepool {
        [g_win close];
        layer = NULL;
        g_exit = 1;
    }
}

int gbbPumpEventsOnce(void)
{
    @autoreleasepool {
        if (!g_win) return 1;

        NSEvent* event = nil;
        while (!g_exit &&
               (event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]) != nil) {
            const int isEscape = ([event type] == NSEventTypeKeyDown && [event keyCode] == 53);
            g_exit |= isEscape;
            if (!isEscape) [NSApp sendEvent:event];
        }

        g_exit |= ![g_win isVisible];
    }
    return g_exit;
}
