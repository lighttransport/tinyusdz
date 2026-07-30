/*
 * lui_x11.h — Minimal X11 ABI declarations for the lightui X11 backend.
 *
 * These declarations are intentionally limited to the Xlib surface used by
 * platform_x11.c so lightui can build without libx11 development headers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUI_X11_H
#define LUI_X11_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long XID;
typedef XID           Window;
typedef XID           Drawable;
typedef XID           Pixmap;
typedef XID           Colormap;
typedef XID           Cursor;
typedef unsigned long Atom;
typedef unsigned long VisualID;
typedef unsigned long Time;
typedef unsigned long KeySym;
typedef int           Bool;
typedef int           Status;

typedef struct _XDisplay Display;
typedef struct _XGC     *GC;
typedef struct _Visual   Visual;

typedef struct {
    Pixmap        background_pixmap;
    unsigned long background_pixel;
    Pixmap        border_pixmap;
    unsigned long border_pixel;
    int           bit_gravity;
    int           win_gravity;
    int           backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    Bool          save_under;
    long          event_mask;
    long          do_not_propagate_mask;
    Bool          override_redirect;
    Colormap      colormap;
    Cursor        cursor;
} XSetWindowAttributes;

typedef struct _XImage {
    int            width, height;
    int            xoffset;
    int            format;
    char          *data;
    int            byte_order;
    int            bitmap_unit;
    int            bitmap_bit_order;
    int            bitmap_pad;
    int            depth;
    int            bytes_per_line;
    int            bits_per_pixel;
    unsigned long  red_mask;
    unsigned long  green_mask;
    unsigned long  blue_mask;
    char          *obdata;
    struct {
        struct _XImage *(*create_image)(
            Display *, Visual *, unsigned int, int, int,
            char *, unsigned int, unsigned int, int, int);
        int (*destroy_image)(struct _XImage *);
        unsigned long (*get_pixel)(struct _XImage *, int, int);
        int (*put_pixel)(struct _XImage *, int, int, unsigned long);
        struct _XImage *(*sub_image)(
            struct _XImage *, int, int, unsigned int, unsigned int);
        int (*add_pixel)(struct _XImage *, long);
    } f;
} XImage;

#define XDestroyImage(ximage) ((*((ximage)->f.destroy_image))((ximage)))

typedef struct {
    int            type;
    unsigned long  serial;
    Bool           send_event;
    Display       *display;
    Window         window;
} XAnyEvent;

typedef struct {
    int            type;
    unsigned long  serial;
    Bool           send_event;
    Display       *display;
    Window         window;
    Window         root;
    Window         subwindow;
    Time           time;
    int            x, y;
    int            x_root, y_root;
    unsigned int   state;
    unsigned int   keycode;
    Bool           same_screen;
} XKeyEvent;

typedef struct {
    int            type;
    unsigned long  serial;
    Bool           send_event;
    Display       *display;
    Window         window;
    Window         root;
    Window         subwindow;
    Time           time;
    int            x, y;
    int            x_root, y_root;
    unsigned int   state;
    unsigned int   button;
    Bool           same_screen;
} XButtonEvent;

typedef struct {
    int            type;
    unsigned long  serial;
    Bool           send_event;
    Display       *display;
    Window         window;
    Window         root;
    Window         subwindow;
    Time           time;
    int            x, y;
    int            x_root, y_root;
    unsigned int   state;
    char           is_hint;
    Bool           same_screen;
} XMotionEvent;

typedef struct {
    int            type;
    unsigned long  serial;
    Bool           send_event;
    Display       *display;
    Window         window;
    int            x, y;
    int            width, height;
    int            count;
} XExposeEvent;

typedef struct {
    int            type;
    unsigned long  serial;
    Bool           send_event;
    Display       *display;
    Window         event;
    Window         window;
    int            x, y;
    int            width, height;
    int            border_width;
    Window         above;
    Bool           override_redirect;
} XConfigureEvent;

typedef struct {
    int            type;
    unsigned long  serial;
    Bool           send_event;
    Display       *display;
    Window         window;
    Atom           message_type;
    int            format;
    union {
        char  b[20];
        short s[10];
        long  l[5];
    } data;
} XClientMessageEvent;

typedef union _XEvent {
    int                 type;
    XAnyEvent           xany;
    XKeyEvent           xkey;
    XButtonEvent        xbutton;
    XMotionEvent        xmotion;
    XExposeEvent        xexpose;
    XConfigureEvent     xconfigure;
    XClientMessageEvent xclient;
    long                pad[24];
} XEvent;

typedef struct {
    int x;
    int y;
} XPoint;

typedef struct {
    long   flags;
    int    x, y;
    int    width, height;
    int    min_width, min_height;
    int    max_width, max_height;
    int    width_inc, height_inc;
    XPoint min_aspect;
    XPoint max_aspect;
    int    base_width, base_height;
    int    win_gravity;
} XSizeHints;

#define KeyPressMask        (1L << 0)
#define KeyReleaseMask      (1L << 1)
#define ButtonPressMask     (1L << 2)
#define ButtonReleaseMask   (1L << 3)
#define PointerMotionMask   (1L << 6)
#define ExposureMask        (1L << 15)
#define StructureNotifyMask (1L << 17)
#define FocusChangeMask     (1L << 21)

#define ShiftMask           (1 << 0)
#define LockMask            (1 << 1)
#define ControlMask         (1 << 2)
#define Mod1Mask            (1 << 3)
#define Mod2Mask            (1 << 4)
#define Mod4Mask            (1 << 6)

#define CWBackPixel         (1L << 1)
#define CWBitGravity        (1L << 4)
#define CWBackingStore      (1L << 6)
#define CWEventMask         (1L << 11)

#define KeyPress            2
#define KeyRelease          3
#define ButtonPress         4
#define ButtonRelease       5
#define MotionNotify        6
#define FocusIn             9
#define FocusOut            10
#define Expose              12
#define ConfigureNotify     22
#define ClientMessage       33

#define StaticGravity       10
#define Always              2

#define ZPixmap             2

#define LSBFirst            0
#define MSBFirst            1

#define InputOutput         1

#define Button1             1
#define Button2             2
#define Button3             3
#define Button4             4
#define Button5             5

#define PMinSize            (1L << 4)
#define PMaxSize            (1L << 5)

#define NoSymbol            0L

#ifndef True
#  define True 1
#endif
#ifndef False
#  define False 0
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LUI_X11_H */
