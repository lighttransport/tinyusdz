/*
 * platform_cocoa.m — macOS Cocoa platform backend
 *
 * Creates an NSWindow with a custom NSView that blits the pixel buffer to
 * screen via Core Graphics.  HiDPI / Retina is handled by honouring the
 * window's backingScaleFactor and allocating a physical-size pixel buffer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifdef LUI_PLATFORM_COCOA

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

#include "../platform_internal.h"
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
static bool           cocoa_init(void);
static void           cocoa_shutdown(void);
static lui_window_t  *cocoa_window_create(const char *title, int w, int h, uint32_t flags);
static void           cocoa_window_destroy(lui_window_t *win);
static void           cocoa_window_show(lui_window_t *win);
static void           cocoa_window_hide(lui_window_t *win);
static void           cocoa_window_set_title(lui_window_t *win, const char *title);
static void           cocoa_window_get_size(const lui_window_t *win, int *w, int *h);
static void           cocoa_window_get_physical_size(const lui_window_t *win, int *w, int *h);
static lvg_surface_t *cocoa_window_get_surface(lui_window_t *win);
static void           cocoa_window_present(lui_window_t *win);
static bool           cocoa_window_poll_event(lui_window_t *win, lui_event_t *ev);
static bool           cocoa_window_wait_event(lui_window_t *win, lui_event_t *ev);

/* -------------------------------------------------------------------------
 * Platform-specific window struct
 * ------------------------------------------------------------------------- */
typedef struct lui_window_cocoa_s {
    lui_window_t    base;          /* MUST be first */
    NSWindow       *nswindow;
    NSView         *view;
    /* Simple FIFO event queue (ring buffer) */
#define COCOA_EVT_QUEUE_SIZE 64
    lui_event_t     event_queue[COCOA_EVT_QUEUE_SIZE];
    int             evt_head;
    int             evt_tail;
} lui_window_cocoa_t;

/* -------------------------------------------------------------------------
 * LUIView — Custom NSView subclass
 * ------------------------------------------------------------------------- */
@interface LUIView : NSView {
    lui_window_cocoa_t *_win;
}
- (instancetype)initWithWindow:(lui_window_cocoa_t *)win
                         frame:(NSRect)frame;
@end

@implementation LUIView

- (instancetype)initWithWindow:(lui_window_cocoa_t *)win frame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        _win = win;
        /* Accept first responder so we receive key events */
    }
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque               { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    lui_window_cocoa_t *cw = _win;
    if (!cw->base.surface.pixels) return;

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];

    int phys_w = cw->base.surface.width;
    int phys_h = cw->base.surface.height;

    /* Create a CGDataProvider that references our pixel buffer */
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        NULL,
        cw->base.surface.pixels,
        (size_t)(phys_w * phys_h * 4),
        NULL   /* no release callback — we own the buffer */
    );
    if (!provider) return;

    /* kCGBitmapByteOrder32Host + kCGImageAlphaNoneSkipFirst → ARGB on LE */
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = CGImageCreate(
        (size_t)phys_w, (size_t)phys_h,
        8,              /* bitsPerComponent */
        32,             /* bitsPerPixel     */
        (size_t)(phys_w * 4),
        cs,
        kCGBitmapByteOrder32Host | kCGImageAlphaNoneSkipFirst,
        provider,
        NULL,  /* decode */
        false, /* shouldInterpolate */
        kCGRenderingIntentDefault
    );
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);
    if (!image) return;

    NSRect bounds = [self bounds];
    CGContextDrawImage(ctx, NSRectToCGRect(bounds), image);
    CGImageRelease(image);
}

/* --- Keyboard ------------------------------------------------------------ */

static void cocoa_push_event(lui_window_cocoa_t *cw, const lui_event_t *ev)
{
    int next = (cw->evt_tail + 1) % COCOA_EVT_QUEUE_SIZE;
    if (next == cw->evt_head) return;  /* queue full — drop */
    cw->event_queue[cw->evt_tail] = *ev;
    cw->evt_tail = next;
}

- (void)keyDown:(NSEvent *)event
{
    lui_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type           = LUI_EVENT_KEY_DOWN;
    ev.data.key.key      = (int)[event keyCode];
    ev.data.key.scancode = (uint32_t)[event keyCode];
    ev.data.key.repeat   = ([event isARepeat] == YES);
    NSUInteger mods = [event modifierFlags];
    if (mods & NSEventModifierFlagShift)   ev.data.key.mods |= LUI_MOD_SHIFT;
    if (mods & NSEventModifierFlagControl) ev.data.key.mods |= LUI_MOD_CTRL;
    if (mods & NSEventModifierFlagOption)  ev.data.key.mods |= LUI_MOD_ALT;
    if (mods & NSEventModifierFlagCommand) ev.data.key.mods |= LUI_MOD_SUPER;
    cocoa_push_event(_win, &ev);

    NSString *chars = [event characters];
    if (chars && [chars length] > 0) {
        lui_event_t tev;
        memset(&tev, 0, sizeof(tev));
        tev.type = LUI_EVENT_TEXT_INPUT;
        const char *utf8 = [chars UTF8String];
        strncpy(tev.data.text.text, utf8, sizeof(tev.data.text.text) - 1);
        cocoa_push_event(_win, &tev);
    }
}

- (void)keyUp:(NSEvent *)event
{
    lui_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type           = LUI_EVENT_KEY_UP;
    ev.data.key.key      = (int)[event keyCode];
    ev.data.key.scancode = (uint32_t)[event keyCode];
    cocoa_push_event(_win, &ev);
}

/* --- Mouse --------------------------------------------------------------- */

- (void)mouseMoved:(NSEvent *)event      { [self handleMotion:event]; }
- (void)mouseDragged:(NSEvent *)event    { [self handleMotion:event]; }
- (void)rightMouseDragged:(NSEvent *)ev  { [self handleMotion:ev]; }
- (void)otherMouseDragged:(NSEvent *)ev  { [self handleMotion:ev]; }

- (void)handleMotion:(NSEvent *)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    lui_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type              = LUI_EVENT_MOUSE_MOVE;
    ev.data.mouse_move.x  = (int)pt.x;
    ev.data.mouse_move.y  = (int)([self bounds].size.height - pt.y);
    ev.data.mouse_move.dx = (int)[event deltaX];
    ev.data.mouse_move.dy = (int)[event deltaY];
    cocoa_push_event(_win, &ev);
}

- (void)mouseDown:(NSEvent *)event
{
    [self handleButton:event type:LUI_EVENT_MOUSE_DOWN btn:LUI_MOUSE_LEFT];
}
- (void)mouseUp:(NSEvent *)event
{
    [self handleButton:event type:LUI_EVENT_MOUSE_UP btn:LUI_MOUSE_LEFT];
}
- (void)rightMouseDown:(NSEvent *)event
{
    [self handleButton:event type:LUI_EVENT_MOUSE_DOWN btn:LUI_MOUSE_RIGHT];
}
- (void)rightMouseUp:(NSEvent *)event
{
    [self handleButton:event type:LUI_EVENT_MOUSE_UP btn:LUI_MOUSE_RIGHT];
}
- (void)otherMouseDown:(NSEvent *)event
{
    [self handleButton:event type:LUI_EVENT_MOUSE_DOWN btn:LUI_MOUSE_MIDDLE];
}
- (void)otherMouseUp:(NSEvent *)event
{
    [self handleButton:event type:LUI_EVENT_MOUSE_UP btn:LUI_MOUSE_MIDDLE];
}

- (void)handleButton:(NSEvent *)event
                type:(lui_event_type_t)evtype
                 btn:(lui_mouse_button_t)btn
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    lui_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type                    = evtype;
    ev.data.mouse_button.x      = (int)pt.x;
    ev.data.mouse_button.y      = (int)([self bounds].size.height - pt.y);
    ev.data.mouse_button.button = btn;
    ev.data.mouse_button.clicks = (int)[event clickCount];
    cocoa_push_event(_win, &ev);
}

- (void)scrollWheel:(NSEvent *)event
{
    lui_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type              = LUI_EVENT_SCROLL;
    ev.data.scroll.delta_x = (float)[event scrollingDeltaX];
    ev.data.scroll.delta_y = (float)[event scrollingDeltaY];
    cocoa_push_event(_win, &ev);
}

@end /* LUIView */

/* -------------------------------------------------------------------------
 * LUIWindowDelegate
 * ------------------------------------------------------------------------- */
@interface LUIWindowDelegate : NSObject<NSWindowDelegate> {
    lui_window_cocoa_t *_win;
}
- (instancetype)initWithWindow:(lui_window_cocoa_t *)win;
@end

@implementation LUIWindowDelegate

- (instancetype)initWithWindow:(lui_window_cocoa_t *)win
{
    self = [super init];
    if (self) _win = win;
    return self;
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    lui_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = LUI_EVENT_QUIT;
    _win->base.should_close = true;
    /* Push a static helper; cocoa_push_event is defined on LUIView but we
     * replicate the logic here inline. */
    int next = (_win->evt_tail + 1) % COCOA_EVT_QUEUE_SIZE;
    if (next != _win->evt_head) {
        _win->event_queue[_win->evt_tail] = ev;
        _win->evt_tail = next;
    }
    return NO;  /* we manage lifetime ourselves */
}

- (void)windowDidResize:(NSNotification *)notification
{
    NSWindow *nswin = (NSWindow *)[notification object];
    NSRect content = [nswin contentRectForFrameRect:[nswin frame]];
    CGFloat scale  = [nswin backingScaleFactor];

    _win->base.logical_w  = (int)content.size.width;
    _win->base.logical_h  = (int)content.size.height;
    _win->base.dpi_scale  = (float)scale;

    int phys_w = (int)(content.size.width  * scale);
    int phys_h = (int)(content.size.height * scale);
    lvg_surface_resize(&_win->base.surface, phys_w, phys_h);

    lui_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type                = LUI_EVENT_WINDOW_RESIZE;
    ev.data.resize.width   = _win->base.logical_w;
    ev.data.resize.height  = _win->base.logical_h;
    ev.data.resize.dpi_scale = _win->base.dpi_scale;
    int next = (_win->evt_tail + 1) % COCOA_EVT_QUEUE_SIZE;
    if (next != _win->evt_head) {
        _win->event_queue[_win->evt_tail] = ev;
        _win->evt_tail = next;
    }
}

@end /* LUIWindowDelegate */

/* -------------------------------------------------------------------------
 * Platform ops implementation
 * ------------------------------------------------------------------------- */

static bool cocoa_init(void)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    return true;
}

static void cocoa_shutdown(void)
{
    /* Nothing to tear down; NSApplication is a singleton. */
}

static lui_window_t *cocoa_window_create(const char *title,
                                          int w, int h,
                                          uint32_t flags)
{
    lui_window_cocoa_t *cw =
        (lui_window_cocoa_t *)calloc(1, sizeof(lui_window_cocoa_t));
    if (!cw) return NULL;

    cw->base.logical_w   = w;
    cw->base.logical_h   = h;
    cw->base.dpi_scale   = 1.0f;
    cw->base.should_close = false;

    NSRect frame = NSMakeRect(0, 0, (CGFloat)w, (CGFloat)h);

    NSWindowStyleMask style = NSWindowStyleMaskTitled |
                               NSWindowStyleMaskClosable |
                               NSWindowStyleMaskMiniaturizable;
    if (flags & LUI_WINDOW_RESIZABLE)  style |= NSWindowStyleMaskResizable;
    if (flags & LUI_WINDOW_BORDERLESS) style = NSWindowStyleMaskBorderless;

    cw->nswindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:style
                    backing:NSBackingStoreBuffered
                      defer:NO];
    if (!cw->nswindow) { free(cw); return NULL; }

    /* HiDPI */
    if (flags & LUI_WINDOW_HDPI) {
        CGFloat scale = [cw->nswindow backingScaleFactor];
        cw->base.dpi_scale = (float)scale;
    }

    int phys_w = (int)(w * cw->base.dpi_scale);
    int phys_h = (int)(h * cw->base.dpi_scale);
    if (!lvg_surface_resize(&cw->base.surface, phys_w, phys_h)) {
        free(cw);
        return NULL;
    }

    NSRect viewFrame = NSMakeRect(0, 0, (CGFloat)w, (CGFloat)h);
    cw->view = [[LUIView alloc] initWithWindow:cw frame:viewFrame];
    [cw->nswindow setContentView:cw->view];

    LUIWindowDelegate *delegate =
        [[LUIWindowDelegate alloc] initWithWindow:cw];
    [cw->nswindow setDelegate:delegate];

    if (title) [cw->nswindow setTitle:[NSString stringWithUTF8String:title]];
    [cw->nswindow center];

    if (!(flags & LUI_WINDOW_HIDDEN)) {
        [cw->nswindow makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }

    return (lui_window_t *)cw;
}

static void cocoa_window_destroy(lui_window_t *win)
{
    lui_window_cocoa_t *cw = (lui_window_cocoa_t *)win;
    if (!cw) return;
    [cw->nswindow close];
    free(cw->base.surface.pixels);
    cw->base.surface.pixels = NULL;
    free(cw);
}

static void cocoa_window_show(lui_window_t *win)
{
    lui_window_cocoa_t *cw = (lui_window_cocoa_t *)win;
    [cw->nswindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

static void cocoa_window_hide(lui_window_t *win)
{
    lui_window_cocoa_t *cw = (lui_window_cocoa_t *)win;
    [cw->nswindow orderOut:nil];
}

static void cocoa_window_set_title(lui_window_t *win, const char *title)
{
    lui_window_cocoa_t *cw = (lui_window_cocoa_t *)win;
    if (title)
        [cw->nswindow setTitle:[NSString stringWithUTF8String:title]];
}

static void cocoa_window_get_size(const lui_window_t *win, int *w, int *h)
{
    if (w) *w = win->logical_w;
    if (h) *h = win->logical_h;
}

static void cocoa_window_get_physical_size(const lui_window_t *win,
                                            int *w, int *h)
{
    if (w) *w = win->surface.width;
    if (h) *h = win->surface.height;
}

static lvg_surface_t *cocoa_window_get_surface(lui_window_t *win)
{
    return &win->surface;
}

static void cocoa_window_present(lui_window_t *win)
{
    lui_window_cocoa_t *cw = (lui_window_cocoa_t *)win;
    [cw->view setNeedsDisplay:YES];
    /* Flush the run loop briefly to push the redraw */
    NSDate *until = [NSDate dateWithTimeIntervalSinceNow:0];
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:until];
}

static bool cocoa_window_poll_event(lui_window_t *win, lui_event_t *ev)
{
    lui_window_cocoa_t *cw = (lui_window_cocoa_t *)win;

    /* Drain pending NSEvents */
    for (;;) {
        NSEvent *nse = [NSApp nextEventMatchingMask:NSEventMaskAny
                                          untilDate:[NSDate distantPast]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
        if (!nse) break;
        [NSApp sendEvent:nse];
    }

    /* Pop from our queue */
    if (cw->evt_head == cw->evt_tail) return false;
    *ev = cw->event_queue[cw->evt_head];
    cw->evt_head = (cw->evt_head + 1) % COCOA_EVT_QUEUE_SIZE;
    return true;
}

static bool cocoa_window_wait_event(lui_window_t *win, lui_event_t *ev)
{
    lui_window_cocoa_t *cw = (lui_window_cocoa_t *)win;
    for (;;) {
        /* Block until an NSEvent arrives */
        NSEvent *nse = [NSApp nextEventMatchingMask:NSEventMaskAny
                                          untilDate:[NSDate distantFuture]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
        if (nse) [NSApp sendEvent:nse];

        if (cw->evt_head != cw->evt_tail) {
            *ev = cw->event_queue[cw->evt_head];
            cw->evt_head = (cw->evt_head + 1) % COCOA_EVT_QUEUE_SIZE;
            return true;
        }
    }
}

/* -------------------------------------------------------------------------
 * Platform ops table
 * ------------------------------------------------------------------------- */
const lui_platform_ops_t lui_platform_ops = {
    .init                    = cocoa_init,
    .shutdown                = cocoa_shutdown,
    .window_create           = cocoa_window_create,
    .window_destroy          = cocoa_window_destroy,
    .window_show             = cocoa_window_show,
    .window_hide             = cocoa_window_hide,
    .window_set_title        = cocoa_window_set_title,
    .window_get_size         = cocoa_window_get_size,
    .window_get_physical_size = cocoa_window_get_physical_size,
    .window_get_surface      = cocoa_window_get_surface,
    .window_present          = cocoa_window_present,
    .window_poll_event       = cocoa_window_poll_event,
    .window_wait_event       = cocoa_window_wait_event,
};

#endif /* LUI_PLATFORM_COCOA */
