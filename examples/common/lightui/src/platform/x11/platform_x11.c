/*
 * platform_x11.c — Linux X11 platform backend
 *
 * Uses XImage for efficient software-render presentation.
 * Events are mapped to lui_event_t via XNextEvent / XPending.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifdef LUI_PLATFORM_X11

#include "../platform_internal.h"
#include "../../internal/lui_log.h"

#include "lui_x11.h"
#define LUI_X11_IMPLEMENTATION
#include "lui_x11_loader.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Platform-specific window struct
 * ------------------------------------------------------------------------- */
typedef struct {
    lui_window_t  base;          /* MUST be first — enables safe casting */

    Display      *display;
    int           screen;
    Window        xwindow;
    GC            gc;
    Visual       *visual;
    int           depth;

    XImage       *ximage;        /* wraps base.surface.pixels            */

    /* WM_DELETE_WINDOW atom for close-button detection */
    Atom          wm_delete_window;

    /* Previous mouse position for delta computation */
    int           last_mouse_x;
    int           last_mouse_y;

    bool          has_pending_text;
    lui_event_t   pending_text;
} lui_window_x11_t;

/* Multi-click run state. X11 has no notion of a double-click, so the run is
 * tracked here (see the ButtonPress handler). Process-global rather than
 * per-window: a click run cannot span windows anyway. */
static lui_mouse_button_t x11_click_button = LUI_MOUSE_LEFT;
static unsigned long      x11_click_time   = 0;
static int                x11_click_x      = 0;
static int                x11_click_y      = 0;
static int                x11_click_count  = 0;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
static bool           x11_init(void);
static void           x11_shutdown(void);
static lui_window_t  *x11_window_create(const char *title, int w, int h, uint32_t flags);
static void           x11_window_destroy(lui_window_t *win);
static void           x11_window_show(lui_window_t *win);
static void           x11_window_hide(lui_window_t *win);
static void           x11_window_set_title(lui_window_t *win, const char *title);
static void           x11_window_get_size(const lui_window_t *win, int *w, int *h);
static void           x11_window_get_physical_size(const lui_window_t *win, int *w, int *h);
static lvg_surface_t *x11_window_get_surface(lui_window_t *win);
static void           x11_window_present(lui_window_t *win);
static void           x11_window_present_rect(lui_window_t *win, const lvg_rect_t *dirty);
static bool           x11_window_poll_event(lui_window_t *win, lui_event_t *ev);
static bool           x11_window_wait_event(lui_window_t *win, lui_event_t *ev);

/* -------------------------------------------------------------------------
 * Global display (one per process, shared across windows)
 * ------------------------------------------------------------------------- */
static Display *g_display = NULL;
static int      g_screen  = 0;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int x11_host_byte_order(void)
{
    const uint32_t value = 1u;
    return (*(const unsigned char *)&value != 0u) ? LSBFirst : MSBFirst;
}

/* Allocate (or re-allocate) the XImage that wraps the pixel buffer. */
static bool x11_create_ximage(lui_window_x11_t *w)
{
    int phys_w = (int)(w->base.logical_w * w->base.dpi_scale);
    int phys_h = (int)(w->base.logical_h * w->base.dpi_scale);

    /* (Re-)allocate the pixel buffer */
    if (!lvg_surface_resize(&w->base.surface, phys_w, phys_h))
        return false;
    /* Propagate the window's DPI scale into the surface so callers can read it. */
    w->base.surface.dpi_scale = w->base.dpi_scale;

    if (w->ximage) {
        /* Detach the old XImage without freeing the data pointer we own */
        w->ximage->data = NULL;
        XDestroyImage(w->ximage);
        w->ximage = NULL;
    }

    w->ximage = XCreateImage(
        w->display,
        w->visual,
        (unsigned int)w->depth,
        ZPixmap,
        0,
        (char *)w->base.surface.pixels,
        (unsigned int)phys_w,
        (unsigned int)phys_h,
        32,
        phys_w * 4
    );
    if (!w->ximage) return false;

    /* Set byte order to match the host so X11 doesn't byte-swap. */
    w->ximage->byte_order = x11_host_byte_order();
    return true;
}

static void x11_sync_configured_size(lui_window_x11_t *xw)
{
    XEvent xe;

    /* tinyusdz-local patch: this must never block.
     *
     * The original loop called XNextEvent until it saw a ConfigureNotify. That
     * assumes a reparenting window manager, which always reconfigures a newly
     * mapped window. With NO window manager -- Xvfb, a bare X session, a kiosk
     * -- the window is created at exactly the requested size and is never
     * reconfigured, so no ConfigureNotify is ever sent and the call hangs
     * forever before the app can draw its first frame.
     *
     * Adopting the server's configured size is an optimization (the window
     * already has the size we asked for), not a correctness requirement.
     * XCheckTypedWindowEvent is non-blocking: it takes a ConfigureNotify if one
     * has already arrived and returns immediately otherwise. Nothing is lost
     * when no WM is present -- the window already has the requested size -- and
     * a later reconfigure still reaches the app as a normal resize event
     * (StructureNotifyMask is selected). */
    XFlush(xw->display);

    while (XCheckTypedWindowEvent(xw->display, xw->xwindow, ConfigureNotify,
                                  &xe)) {
        float s = xw->base.dpi_scale;
        int new_log_w = (int)(xe.xconfigure.width  / s + 0.5f);
        int new_log_h = (int)(xe.xconfigure.height / s + 0.5f);
        if (new_log_w > 0 && new_log_h > 0 &&
            (new_log_w != xw->base.logical_w ||
             new_log_h != xw->base.logical_h)) {
            xw->base.logical_w = new_log_w;
            xw->base.logical_h = new_log_h;
            x11_create_ximage(xw);
        }
        return;
    }
}

/* Map an X11 KeySym to our internal key code (basic set). */
static int x11_map_keysym(KeySym ks)
{
    /* For now return the KeySym directly; callers should treat this as opaque
     * until a full keymap is implemented. */
    return (int)ks;
}

static int x11_keysym_to_text(KeySym ks, unsigned int state, char out[8])
{
    memset(out, 0, 8);
    if (ks < 32 || ks >= 127)
        return 0;

    char ch = (char)ks;
    bool shift = (state & ShiftMask) != 0;
    if (shift) {
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        } else {
            switch (ch) {
            case '1': ch = '!'; break;
            case '2': ch = '@'; break;
            case '3': ch = '#'; break;
            case '4': ch = '$'; break;
            case '5': ch = '%'; break;
            case '6': ch = '^'; break;
            case '7': ch = '&'; break;
            case '8': ch = '*'; break;
            case '9': ch = '('; break;
            case '0': ch = ')'; break;
            case '-': ch = '_'; break;
            case '=': ch = '+'; break;
            case '[': ch = '{'; break;
            case ']': ch = '}'; break;
            case '\\': ch = '|'; break;
            case ';': ch = ':'; break;
            case '\'': ch = '"'; break;
            case ',': ch = '<'; break;
            case '.': ch = '>'; break;
            case '/': ch = '?'; break;
            case '`': ch = '~'; break;
            default: break;
            }
        }
    }

    out[0] = ch;
    return 1;
}

/* Translate a raw XEvent into a lui_event_t.
 * Returns false if the event should be discarded. */
static bool x11_translate_event(lui_window_x11_t *xw,
                                  const XEvent *xe,
                                  lui_event_t *out)
{
    memset(out, 0, sizeof(*out));

    switch (xe->type) {
    case ClientMessage:
        if ((Atom)xe->xclient.data.l[0] == xw->wm_delete_window) {
            out->type            = LUI_EVENT_QUIT;
            xw->base.should_close = true;
            return true;
        }
        return false;

    case Expose:
        if (xe->xexpose.count == 0) {
            out->type = LUI_EVENT_WINDOW_EXPOSE;
            return true;
        }
        return false;

    case ConfigureNotify: {
        /* ConfigureNotify reports physical pixels (window is physical-sized). */
        float s = xw->base.dpi_scale;
        int new_log_w = (int)(xe->xconfigure.width  / s + 0.5f);
        int new_log_h = (int)(xe->xconfigure.height / s + 0.5f);
        if (new_log_w != xw->base.logical_w || new_log_h != xw->base.logical_h) {
            xw->base.logical_w = new_log_w;
            xw->base.logical_h = new_log_h;
            /* Rebuild the XImage for the new physical size */
            x11_create_ximage(xw);
            out->type                  = LUI_EVENT_WINDOW_RESIZE;
            out->data.resize.width     = new_log_w;
            out->data.resize.height    = new_log_h;
            out->data.resize.dpi_scale = s;
            return true;
        }
        return false;
    }

    case FocusIn:
        out->type = LUI_EVENT_WINDOW_FOCUS_IN;
        return true;

    case FocusOut:
        out->type = LUI_EVENT_WINDOW_FOCUS_OUT;
        return true;

    case KeyPress:
    case KeyRelease: {
        KeySym ks = XLookupKeysym((XKeyEvent *)&xe->xkey, 0);
        if (ks == NoSymbol)
            ks = (KeySym)xe->xkey.keycode;

        uint32_t mods = 0;
        unsigned int state = xe->xkey.state;
        if (state & ShiftMask)   mods |= LUI_MOD_SHIFT;
        if (state & ControlMask) mods |= LUI_MOD_CTRL;
        if (state & Mod1Mask)    mods |= LUI_MOD_ALT;
        if (state & Mod4Mask)    mods |= LUI_MOD_SUPER;
        if (state & LockMask)    mods |= LUI_MOD_CAPS;
        if (state & Mod2Mask)    mods |= LUI_MOD_NUM;

        if (xe->type == KeyPress) {
            out->type           = LUI_EVENT_KEY_DOWN;
            out->data.key.key      = x11_map_keysym(ks);
            out->data.key.scancode = xe->xkey.keycode;
            out->data.key.mods     = mods;
            out->data.key.repeat   = false; /* TODO: detect repeat */
            if (!(mods & (LUI_MOD_CTRL | LUI_MOD_ALT | LUI_MOD_SUPER)) &&
                x11_keysym_to_text(ks, state, xw->pending_text.data.text.text)) {
                xw->pending_text.type = LUI_EVENT_TEXT_INPUT;
                xw->has_pending_text = true;
            }
        } else {
            out->type           = LUI_EVENT_KEY_UP;
            out->data.key.key      = x11_map_keysym(ks);
            out->data.key.scancode = xe->xkey.keycode;
            out->data.key.mods     = mods;
        }
        return true;
    }

    case MotionNotify: {
        /*
         * Report in physical pixel coordinates — canvas, layout, and
         * hit-testing all operate on the physical-pixel surface directly.
         */
        int x = xe->xmotion.x;
        int y = xe->xmotion.y;
        out->type              = LUI_EVENT_MOUSE_MOVE;
        out->data.mouse_move.x  = x;
        out->data.mouse_move.y  = y;
        out->data.mouse_move.dx = x - xw->last_mouse_x;
        out->data.mouse_move.dy = y - xw->last_mouse_y;
        xw->last_mouse_x = x;
        xw->last_mouse_y = y;
        return true;
    }

    case ButtonPress:
    case ButtonRelease: {
        int btn = xe->xbutton.button;
        /* X11 buttons 4/5 are scroll wheel */
        if (btn == 4 || btn == 5) {
            if (xe->type == ButtonPress) {
                out->type              = LUI_EVENT_SCROLL;
                out->data.scroll.x      = xe->xbutton.x;
                out->data.scroll.y      = xe->xbutton.y;
                out->data.scroll.delta_y = (btn == 4) ? -1.0f : 1.0f;
            }
            return xe->type == ButtonPress;
        }
        lui_mouse_button_t mb;
        if      (btn == 1) mb = LUI_MOUSE_LEFT;
        else if (btn == 2) mb = LUI_MOUSE_MIDDLE;
        else               mb = LUI_MOUSE_RIGHT;

        out->type                     = (xe->type == ButtonPress)
                                         ? LUI_EVENT_MOUSE_DOWN
                                         : LUI_EVENT_MOUSE_UP;
        out->data.mouse_button.x       = xe->xbutton.x;
        out->data.mouse_button.y       = xe->xbutton.y;
        out->data.mouse_button.button  = mb;

        /* Multi-click counting. X11 reports every press independently, so the
         * count has to be synthesized here or lui_event_t::clicks is stuck at
         * 1 and no application can ever see a double-click. Same button, close
         * in time and in space, extends the run. */
        if (xe->type == ButtonPress) {
            static const unsigned long kMultiClickMs = 400;
            static const int kMultiClickSlopPx = 4;
            unsigned long t = xe->xbutton.time;
            int dx = xe->xbutton.x - x11_click_x;
            int dy = xe->xbutton.y - x11_click_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (mb == x11_click_button &&
                t >= x11_click_time &&
                (t - x11_click_time) <= kMultiClickMs &&
                dx <= kMultiClickSlopPx && dy <= kMultiClickSlopPx) {
                x11_click_count++;
            } else {
                x11_click_count = 1;
            }
            x11_click_button = mb;
            x11_click_time   = t;
            x11_click_x      = xe->xbutton.x;
            x11_click_y      = xe->xbutton.y;
            out->data.mouse_button.clicks = x11_click_count;
        } else {
            /* A release belongs to the press that opened the run. */
            out->data.mouse_button.clicks = x11_click_count > 0
                                          ? x11_click_count : 1;
        }
        return true;
    }

    default:
        return false;
    }
}

/* -------------------------------------------------------------------------
 * Platform ops implementation
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * DPI detection
 *
 * Priority:
 *  1. Xft.dpi X resource  (set by most desktop environments)
 *  2. Physical screen size reported by the X server
 *
 * dpi_scale = dpi / 96, rounded to the nearest 0.25 increment,
 * clamped to [1.0, 4.0].
 * ------------------------------------------------------------------------- */
static float x11_detect_dpi_scale(void)
{
    float dpi = 96.0f;

    /* Method 1: Xft.dpi */
    const char *xft = XGetDefault(g_display, "Xft", "dpi");
    if (xft) {
        float v = 0.0f;
        if (sscanf(xft, "%f", &v) == 1 && v > 0.0f)
            dpi = v;
    } else {
        /* Method 2: physical screen dimensions */
        int    mm  = DisplayWidthMM(g_display, g_screen);
        int    px  = DisplayWidth(g_display, g_screen);
        if (mm > 0)
            dpi = (float)px * 25.4f / (float)mm;
    }

    float scale = dpi / 96.0f;

    /* Round to nearest 0.25 */
    scale = (float)((int)(scale * 4.0f + 0.5f)) / 4.0f;

    if (scale < 1.0f) scale = 1.0f;
    if (scale > 4.0f) scale = 4.0f;

    /* Many X11 setups report generic 96 DPI even on 4K panels.  Treat a
     * 3840px-wide desktop as at least 2x when the caller opts into HDPI. */
    if (DisplayWidth(g_display, g_screen) >= 3840 && scale < 2.0f)
        scale = 2.0f;

    return scale;
}

static bool x11_init(void)
{
    if (lui_x11_load(&g_lui_x11) != 0) {
        LUI_LOG_ERR("x11: cannot load libX11.so");
        return false;
    }

    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        LUI_LOG_ERR("x11: cannot open display");
        lui_x11_unload(&g_lui_x11);
        return false;
    }
    g_screen = DefaultScreen(g_display);
    return true;
}

static void x11_shutdown(void)
{
    if (g_display) {
        XCloseDisplay(g_display);
        g_display = NULL;
    }
    lui_x11_unload(&g_lui_x11);
}

static lui_window_t *x11_window_create(const char *title,
                                        int w, int h,
                                        uint32_t flags)
{
    if (!g_display) return NULL;

    lui_window_x11_t *xw =
        (lui_window_x11_t *)calloc(1, sizeof(lui_window_x11_t));
    if (!xw) return NULL;

    /* Initialise base */
    xw->base.logical_w   = w;
    xw->base.logical_h   = h;
    xw->base.dpi_scale   = (flags & LUI_WINDOW_HDPI) ? x11_detect_dpi_scale() : 1.0f;
    xw->base.should_close = false;
    xw->display = g_display;
    xw->screen  = g_screen;

    /* Choose visual — default TrueColor visual */
    xw->visual = DefaultVisual(g_display, g_screen);
    xw->depth  = DefaultDepth(g_display, g_screen);

    /* Create the X11 window */
    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.background_pixel = BlackPixel(g_display, g_screen);
    swa.event_mask = (ExposureMask | KeyPressMask | KeyReleaseMask |
                      ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | StructureNotifyMask |
                      FocusChangeMask);
    swa.bit_gravity  = StaticGravity;
    swa.backing_store = Always;

    /* Create the window at physical pixels so XPutImage blits 1:1. */
    int phys_w = (int)(w * xw->base.dpi_scale + 0.5f);
    int phys_h = (int)(h * xw->base.dpi_scale + 0.5f);

    xw->xwindow = XCreateWindow(
        g_display,
        RootWindow(g_display, g_screen),
        0, 0,
        (unsigned int)phys_w, (unsigned int)phys_h,
        0,
        xw->depth,
        InputOutput,
        xw->visual,
        CWBackPixel | CWEventMask | CWBitGravity | CWBackingStore,
        &swa
    );
    if (!xw->xwindow) {
        free(xw);
        return NULL;
    }

    /* Window hints */
    XSizeHints *sh = XAllocSizeHints();
    if (sh) {
        sh->flags = PMinSize;
        sh->min_width = sh->min_height = 1;
        if (!(flags & LUI_WINDOW_RESIZABLE)) {
            sh->flags    |= PMaxSize | PMinSize;
            sh->max_width  = sh->min_width  = phys_w;
            sh->max_height = sh->min_height = phys_h;
        }
        XSetWMNormalHints(g_display, xw->xwindow, sh);
        XFree(sh);
    }

    /* Title */
    XStoreName(g_display, xw->xwindow, title ? title : "");

    /* WM_DELETE_WINDOW protocol */
    xw->wm_delete_window = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, xw->xwindow, &xw->wm_delete_window, 1);

    /* GC */
    xw->gc = XCreateGC(g_display, xw->xwindow, 0, NULL);

    /* Allocate pixel buffer and XImage */
    if (!x11_create_ximage(xw)) {
        XDestroyWindow(g_display, xw->xwindow);
        free(xw);
        return NULL;
    }

    /* Show unless HIDDEN flag is set */
    if (!(flags & LUI_WINDOW_HIDDEN))
        XMapWindow(g_display, xw->xwindow);

    XFlush(g_display);
    if (!(flags & LUI_WINDOW_HIDDEN))
        x11_sync_configured_size(xw);
    return (lui_window_t *)xw;
}

static void x11_window_destroy(lui_window_t *win)
{
    lui_window_x11_t *xw = (lui_window_x11_t *)win;
    if (!xw) return;

    if (xw->ximage) {
        xw->ximage->data = NULL;
        XDestroyImage(xw->ximage);
    }
    /* Free the pixel buffer (owned by the surface, not the XImage) */
    free(xw->base.surface.pixels);
    xw->base.surface.pixels = NULL;

    if (xw->gc)      XFreeGC(xw->display, xw->gc);
    if (xw->xwindow) XDestroyWindow(xw->display, xw->xwindow);
    XFlush(xw->display);
    free(xw);
}

static void x11_window_show(lui_window_t *win)
{
    lui_window_x11_t *xw = (lui_window_x11_t *)win;
    XMapWindow(xw->display, xw->xwindow);
    XFlush(xw->display);
}

static void x11_window_hide(lui_window_t *win)
{
    lui_window_x11_t *xw = (lui_window_x11_t *)win;
    XUnmapWindow(xw->display, xw->xwindow);
    XFlush(xw->display);
}

static void x11_window_set_title(lui_window_t *win, const char *title)
{
    lui_window_x11_t *xw = (lui_window_x11_t *)win;
    XStoreName(xw->display, xw->xwindow, title ? title : "");
    XFlush(xw->display);
}

static void x11_window_get_size(const lui_window_t *win, int *w, int *h)
{
    if (w) *w = win->logical_w;
    if (h) *h = win->logical_h;
}

static void x11_window_get_physical_size(const lui_window_t *win,
                                          int *w, int *h)
{
    if (w) *w = win->surface.width;
    if (h) *h = win->surface.height;
}

static lvg_surface_t *x11_window_get_surface(lui_window_t *win)
{
    return &win->surface;
}

static void x11_window_present(lui_window_t *win)
{
    lui_window_x11_t *xw = (lui_window_x11_t *)win;
    if (!xw->ximage) return;

    /* Update the XImage data pointer in case the buffer was reallocated */
    xw->ximage->data = (char *)xw->base.surface.pixels;

    int phys_w = xw->base.surface.width;
    int phys_h = xw->base.surface.height;

    XPutImage(xw->display, xw->xwindow, xw->gc,
              xw->ximage,
              0, 0, 0, 0,
              (unsigned int)phys_w,
              (unsigned int)phys_h);
    XFlush(xw->display);
}

static void x11_window_present_rect(lui_window_t *win, const lvg_rect_t *dirty)
{
    lui_window_x11_t *xw = (lui_window_x11_t *)win;
    if (!xw->ximage || !dirty || dirty->width <= 0 || dirty->height <= 0) {
        x11_window_present(win);
        return;
    }

    int x0 = dirty->x;
    int y0 = dirty->y;
    int x1 = dirty->x + dirty->width;
    int y1 = dirty->y + dirty->height;
    int phys_w = xw->base.surface.width;
    int phys_h = xw->base.surface.height;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > phys_w) x1 = phys_w;
    if (y1 > phys_h) y1 = phys_h;
    if (x1 <= x0 || y1 <= y0)
        return;

    /* Update the XImage data pointer in case the buffer was reallocated. */
    xw->ximage->data = (char *)xw->base.surface.pixels;

    XPutImage(xw->display, xw->xwindow, xw->gc,
              xw->ximage,
              x0, y0, x0, y0,
              (unsigned int)(x1 - x0),
              (unsigned int)(y1 - y0));
    XFlush(xw->display);
}

static bool x11_window_poll_event(lui_window_t *win, lui_event_t *ev)
{
    lui_window_x11_t *xw = (lui_window_x11_t *)win;
    if (xw->has_pending_text) {
        *ev = xw->pending_text;
        xw->has_pending_text = false;
        return true;
    }
    while (XPending(xw->display)) {
        XEvent xe;
        XNextEvent(xw->display, &xe);
        if (xe.type == MotionNotify) {
            XEvent latest = xe;
            while (XCheckTypedWindowEvent(xw->display, xw->xwindow,
                                          MotionNotify, &latest))
                xe = latest;
        }
        if (x11_translate_event(xw, &xe, ev))
            return true;
    }
    return false;
}

static bool x11_window_wait_event(lui_window_t *win, lui_event_t *ev)
{
    lui_window_x11_t *xw = (lui_window_x11_t *)win;
    for (;;) {
        XEvent xe;
        XNextEvent(xw->display, &xe);
        if (x11_translate_event(xw, &xe, ev))
            return true;
    }
}

/* -------------------------------------------------------------------------
 * Platform ops table
 * ------------------------------------------------------------------------- */
const lui_platform_ops_t lui_platform_ops = {
    .init                    = x11_init,
    .shutdown                = x11_shutdown,
    .window_create           = x11_window_create,
    .window_destroy          = x11_window_destroy,
    .window_show             = x11_window_show,
    .window_hide             = x11_window_hide,
    .window_set_title        = x11_window_set_title,
    .window_get_size         = x11_window_get_size,
    .window_get_physical_size = x11_window_get_physical_size,
    .window_get_surface      = x11_window_get_surface,
    .window_present          = x11_window_present,
    .window_present_rect     = x11_window_present_rect,
    .window_poll_event       = x11_window_poll_event,
    .window_wait_event       = x11_window_wait_event,
};

#endif /* LUI_PLATFORM_X11 */
