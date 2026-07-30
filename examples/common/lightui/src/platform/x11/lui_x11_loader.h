/*
 * lui_x11_loader.h — Runtime loader for libX11.so via dlopen/dlsym.
 *
 * Define LUI_X11_IMPLEMENTATION in exactly one translation unit before
 * including this header to emit the implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUI_X11_LOADER_H
#define LUI_X11_LOADER_H

#include "lui_x11.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lui_x11_loader_s {
    void *handle;

    Display *(*XOpenDisplay)(const char *);
    int      (*XCloseDisplay)(Display *);

    int           (*XDefaultScreen)(Display *);
    Window        (*XRootWindow)(Display *, int);
    Visual       *(*XDefaultVisual)(Display *, int);
    int           (*XDefaultDepth)(Display *, int);
    unsigned long (*XBlackPixel)(Display *, int);
    int           (*XDisplayWidth)(Display *, int);
    int           (*XDisplayWidthMM)(Display *, int);
    char         *(*XGetDefault)(Display *, const char *, const char *);

    Window (*XCreateWindow)(Display *, Window, int, int, unsigned int,
                            unsigned int, unsigned int, int, unsigned int,
                            Visual *, unsigned long, XSetWindowAttributes *);
    int    (*XMapWindow)(Display *, Window);
    int    (*XUnmapWindow)(Display *, Window);
    int    (*XDestroyWindow)(Display *, Window);
    int    (*XStoreName)(Display *, Window, const char *);

    Atom   (*XInternAtom)(Display *, const char *, Bool);
    Status (*XSetWMProtocols)(Display *, Window, Atom *, int);

    XSizeHints *(*XAllocSizeHints)(void);
    int         (*XSetWMNormalHints)(Display *, Window, XSizeHints *);
    int         (*XFree)(void *);

    GC      (*XCreateGC)(Display *, Drawable, unsigned long, void *);
    int     (*XFreeGC)(Display *, GC);
    XImage *(*XCreateImage)(Display *, Visual *, unsigned int, int, int,
                            char *, unsigned int, unsigned int, int, int);
    int     (*XPutImage)(Display *, Drawable, GC, XImage *, int, int,
                         int, int, unsigned int, unsigned int);

    int    (*XPending)(Display *);
    int    (*XNextEvent)(Display *, XEvent *);
    Bool   (*XCheckTypedWindowEvent)(Display *, Window, int, XEvent *);
    KeySym (*XLookupKeysym)(XKeyEvent *, int);

    int    (*XFlush)(Display *);
} lui_x11_loader_t;

static lui_x11_loader_t g_lui_x11;

static int  lui_x11_load(lui_x11_loader_t *x11);
static void lui_x11_unload(lui_x11_loader_t *x11);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LUI_X11_LOADER_H */

#ifdef LUI_X11_IMPLEMENTATION
#undef LUI_X11_IMPLEMENTATION

#include <dlfcn.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_X11_LOAD_SYM(x11, name)                                      \
    do {                                                                  \
        *((void **)&(x11)->name) = dlsym((x11)->handle, #name);           \
        if (!(x11)->name) goto fail;                                      \
    } while (0)

static int lui_x11_load(lui_x11_loader_t *x11)
{
    memset(x11, 0, sizeof(*x11));

    x11->handle = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!x11->handle)
        x11->handle = dlopen("libX11.so", RTLD_LAZY | RTLD_LOCAL);
    if (!x11->handle)
        return -1;

    LUI_X11_LOAD_SYM(x11, XOpenDisplay);
    LUI_X11_LOAD_SYM(x11, XCloseDisplay);
    LUI_X11_LOAD_SYM(x11, XDefaultScreen);
    LUI_X11_LOAD_SYM(x11, XRootWindow);
    LUI_X11_LOAD_SYM(x11, XDefaultVisual);
    LUI_X11_LOAD_SYM(x11, XDefaultDepth);
    LUI_X11_LOAD_SYM(x11, XBlackPixel);
    LUI_X11_LOAD_SYM(x11, XDisplayWidth);
    LUI_X11_LOAD_SYM(x11, XDisplayWidthMM);
    LUI_X11_LOAD_SYM(x11, XGetDefault);
    LUI_X11_LOAD_SYM(x11, XCreateWindow);
    LUI_X11_LOAD_SYM(x11, XMapWindow);
    LUI_X11_LOAD_SYM(x11, XUnmapWindow);
    LUI_X11_LOAD_SYM(x11, XDestroyWindow);
    LUI_X11_LOAD_SYM(x11, XStoreName);
    LUI_X11_LOAD_SYM(x11, XInternAtom);
    LUI_X11_LOAD_SYM(x11, XSetWMProtocols);
    LUI_X11_LOAD_SYM(x11, XAllocSizeHints);
    LUI_X11_LOAD_SYM(x11, XSetWMNormalHints);
    LUI_X11_LOAD_SYM(x11, XFree);
    LUI_X11_LOAD_SYM(x11, XCreateGC);
    LUI_X11_LOAD_SYM(x11, XFreeGC);
    LUI_X11_LOAD_SYM(x11, XCreateImage);
    LUI_X11_LOAD_SYM(x11, XPutImage);
    LUI_X11_LOAD_SYM(x11, XPending);
    LUI_X11_LOAD_SYM(x11, XNextEvent);
    LUI_X11_LOAD_SYM(x11, XCheckTypedWindowEvent);
    LUI_X11_LOAD_SYM(x11, XLookupKeysym);
    LUI_X11_LOAD_SYM(x11, XFlush);

    return 0;

fail:
    lui_x11_unload(x11);
    return -1;
}

#undef LUI_X11_LOAD_SYM

static void lui_x11_unload(lui_x11_loader_t *x11)
{
    if (x11->handle) {
        dlclose(x11->handle);
        x11->handle = NULL;
    }
    memset(x11, 0, sizeof(*x11));
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LUI_X11_IMPLEMENTATION */

#ifndef LUI_X11_MACROS_DEFINED
#define LUI_X11_MACROS_DEFINED

#define XOpenDisplay      g_lui_x11.XOpenDisplay
#define XCloseDisplay     g_lui_x11.XCloseDisplay
#define XGetDefault       g_lui_x11.XGetDefault
#define XCreateWindow     g_lui_x11.XCreateWindow
#define XMapWindow        g_lui_x11.XMapWindow
#define XUnmapWindow      g_lui_x11.XUnmapWindow
#define XDestroyWindow    g_lui_x11.XDestroyWindow
#define XStoreName        g_lui_x11.XStoreName
#define XInternAtom       g_lui_x11.XInternAtom
#define XSetWMProtocols   g_lui_x11.XSetWMProtocols
#define XAllocSizeHints   g_lui_x11.XAllocSizeHints
#define XSetWMNormalHints g_lui_x11.XSetWMNormalHints
#define XFree             g_lui_x11.XFree
#define XCreateGC         g_lui_x11.XCreateGC
#define XFreeGC           g_lui_x11.XFreeGC
#define XCreateImage      g_lui_x11.XCreateImage
#define XPutImage         g_lui_x11.XPutImage
#define XPending          g_lui_x11.XPending
#define XNextEvent        g_lui_x11.XNextEvent
#define XCheckTypedWindowEvent g_lui_x11.XCheckTypedWindowEvent
#define XLookupKeysym     g_lui_x11.XLookupKeysym
#define XFlush            g_lui_x11.XFlush

#define DefaultScreen(d)    g_lui_x11.XDefaultScreen(d)
#define RootWindow(d, s)    g_lui_x11.XRootWindow(d, s)
#define DefaultVisual(d, s) g_lui_x11.XDefaultVisual(d, s)
#define DefaultDepth(d, s)  g_lui_x11.XDefaultDepth(d, s)
#define BlackPixel(d, s)    g_lui_x11.XBlackPixel(d, s)
#define DisplayWidth(d, s)  g_lui_x11.XDisplayWidth(d, s)
#define DisplayWidthMM(d, s) g_lui_x11.XDisplayWidthMM(d, s)

#endif /* LUI_X11_MACROS_DEFINED */
