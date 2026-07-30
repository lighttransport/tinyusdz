/*
 * lightui/popover.h — Floating content panel anchored to a position
 *
 * A popover displays a rounded panel with an arrow pointing toward its
 * anchor point.  It wraps an arbitrary content widget.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_POPOVER_H
#define LIGHTUI_POPOVER_H

#include "layout.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Position ----------------------------------------------------------- */

typedef enum {
    LUI_POPOVER_ABOVE = 0,
    LUI_POPOVER_BELOW = 1,
    LUI_POPOVER_LEFT  = 2,
    LUI_POPOVER_RIGHT = 3,
} lui_popover_position_t;

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_popover_dismiss_fn)(void *user);

/* ---- Popover widget ----------------------------------------------------- */

typedef struct {
    lui_widget_t  widget;

    /* State */
    bool          visible;
    int           anchor_x;
    int           anchor_y;
    int           content_width;
    int           content_height;

    /* Position */
    lui_popover_position_t position;

    /* Content */
    lui_widget_t *content;           /* wrapped content widget (not owned) */

    /* Dimensions */
    int           corner_radius;     /* (6)                                */
    int           arrow_size;        /* (8)                                */
    int           padding;           /* inner padding (8)                  */

    /* Colors */
    lvg_color_t   bg_color;
    lvg_color_t   border_color;
    lvg_color_t   arrow_color;
    lvg_color_t   shadow_color;

    /* Callback */
    lui_popover_dismiss_fn on_dismiss;
    void                  *on_dismiss_user;
} lui_popover_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a popover widget with default appearance. */
void lui_popover_init(lui_popover_t *po);

/** Show the popover at the given anchor position. */
void lui_popover_show(lui_popover_t *po, int anchor_x, int anchor_y,
                       lui_popover_position_t position);

/** Hide the popover. */
void lui_popover_hide(lui_popover_t *po);

/** Set the content widget displayed inside the popover. */
void lui_popover_set_content(lui_popover_t *po, lui_widget_t *content);

/** Returns true if the popover is currently visible. */
bool lui_popover_is_visible(const lui_popover_t *po);

/** Get the widget node. */
static inline lui_widget_t *lui_popover_widget(lui_popover_t *po) {
    return &po->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_POPOVER_H */
