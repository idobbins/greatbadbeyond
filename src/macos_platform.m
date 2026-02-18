#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#define VK_ENABLE_BETA_EXTENSIONS
#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>

#include "platform.h"

static NSWindow* g_win = nil;
static CAMetalLayer* g_layer = nil;
static int g_exit = 0;
static const char* k_instanceExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
};

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

        NSView* view = [g_win contentView];
        g_layer = [CAMetalLayer layer];
        if (!(g_win && view && g_layer)) return 1;

        [g_win setReleasedWhenClosed:NO];
        [g_win setTitle:title ? [NSString stringWithUTF8String:title] : @""];
        [view setWantsLayer:YES];
        [g_layer setOpaque:YES];
        const CGFloat scale = [g_win backingScaleFactor];
        [g_layer setContentsScale:scale];
        [g_layer setDrawableSize:CGSizeMake((CGFloat)w * scale, (CGFloat)h * scale)];
        [view setLayer:g_layer];
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

void gbbGetInstanceExtensions(const char*** extensions, uint32_t* extensionCount, VkInstanceCreateFlags* instanceFlags)
{
    *extensions = k_instanceExtensions;
    *extensionCount = (uint32_t)(sizeof(k_instanceExtensions) / sizeof(k_instanceExtensions[0]));
    *instanceFlags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
}

VkResult gbbCreateSurface(VkInstance inst, VkSurfaceKHR* surf)
{
    if (!g_layer) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkCreateMetalSurfaceEXT fn = (PFN_vkCreateMetalSurfaceEXT)vkGetInstanceProcAddr(inst, "vkCreateMetalSurfaceEXT");
    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkMetalSurfaceCreateInfoEXT ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    ci.pLayer = g_layer;
    return fn(inst, &ci, NULL, surf);
}
