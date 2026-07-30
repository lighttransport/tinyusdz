/*
 * lightui/window.h — Platform window management
 *
 * Windows are created as opaque handles; all operations go through the
 * functions below so the caller never needs to know the underlying OS type.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_WINDOW_H
#define LIGHTUI_WINDOW_H

#include <lightvg/surface.h>
#include "event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Window creation flags
 * ------------------------------------------------------------------------- */
typedef enum {
    LUI_WINDOW_RESIZABLE   = 1 << 0,  /* user can resize the window       */
    LUI_WINDOW_BORDERLESS  = 1 << 1,  /* no title bar / OS decorations    */
    LUI_WINDOW_HIDDEN      = 1 << 2,  /* start hidden (show explicitly)   */
    LUI_WINDOW_HDPI        = 1 << 3,  /* opt-in to HiDPI / Retina         */
} lui_window_flags_t;

/* -------------------------------------------------------------------------
 * Opaque window handle
 * ------------------------------------------------------------------------- */
typedef struct lui_window_s lui_window_t;

/**
 * Create a platform window.
 *
 * @param title   UTF-8 window title.
 * @param width   Logical width in pixels.
 * @param height  Logical height in pixels.
 * @param flags   Bitmask of lui_window_flags_t.
 * @return        New window handle, or NULL on failure.
 */
lui_window_t *lui_window_create(const char *title,
                                 int width, int height,
                                 uint32_t flags);

/**
 * Destroy a window and release all OS resources.
 * The pointer is invalid after this call.  Passing NULL is a no-op.
 */
void lui_window_destroy(lui_window_t *window);

/** Show a hidden window. */
void lui_window_show(lui_window_t *window);

/** Hide a visible window. */
void lui_window_hide(lui_window_t *window);

/** Update the window title (UTF-8). */
void lui_window_set_title(lui_window_t *window, const char *title);

/**
 * Query the logical content-area size (in logical pixels, not physical
 * backing-store pixels).
 */
void lui_window_get_size(const lui_window_t *window,
                          int *width, int *height);

/**
 * Query the physical (backing-store) pixel dimensions.
 * On HiDPI displays this is larger than the logical size by dpi_scale.
 */
void lui_window_get_physical_size(const lui_window_t *window,
                                   int *width, int *height);

/**
 * Obtain the surface to draw the next frame into.
 *
 * The returned pointer is valid until the next call to lui_window_present().
 * Do NOT free it.
 */
lvg_surface_t *lui_window_get_surface(lui_window_t *window);

/**
 * Flush the rendered surface to the screen.
 * After this call the previously-obtained surface should no longer be used.
 */
void lui_window_present(lui_window_t *window);

/**
 * Flush only the @dirty region of the rendered surface to the screen.
 *
 * @dirty is in physical (backing-store) surface coordinates.  Passing NULL or
 * an empty rect presents the whole surface (identical to lui_window_present).
 * On platforms without a partial-present path this falls back to a full
 * present, so it is always safe to call.
 */
void lui_window_present_rect(lui_window_t *window, const lvg_rect_t *dirty);

/**
 * Non-blocking event poll.
 *
 * Returns true and fills *event when an event is pending.
 * Returns false when the queue is empty.
 */
bool lui_window_poll_event(lui_window_t *window, lui_event_t *event);

/**
 * Blocking event wait.
 *
 * Blocks until at least one event is available, then fills *event and returns
 * true.  Returns false only on an unrecoverable error.
 */
bool lui_window_wait_event(lui_window_t *window, lui_event_t *event);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_WINDOW_H */
