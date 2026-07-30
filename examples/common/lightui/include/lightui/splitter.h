/*
 * lightui/splitter.h — Draggable panel splitter widget
 *
 * A divider between two child regions that can be dragged to resize.
 * Supports horizontal (left|right) and vertical (top|bottom) splits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_SPLITTER_H
#define LIGHTUI_SPLITTER_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LUI_SPLIT_HORIZONTAL = 0,  /* divider is vertical bar (left|right) */
    LUI_SPLIT_VERTICAL   = 1,  /* divider is horizontal bar (top|bottom) */
} lui_split_dir_t;

typedef struct {
    lui_widget_t   widget;

    /* Split configuration */
    lui_split_dir_t direction;
    float           ratio;         /* 0.0–1.0 split position (default 0.5) */
    int             min_first;     /* minimum size of first panel (px)     */
    int             min_second;    /* minimum size of second panel (px)    */
    int             divider_size;  /* divider thickness (default 6)        */

    /* Interaction */
    bool            dragging;      /* currently dragging the divider       */
    float           initial_ratio; /* ratio at drag start                  */

    /* Appearance */
    lvg_color_t     bg;
    lvg_color_t     divider_color;
    lvg_color_t     divider_hover;
    lvg_color_t     divider_drag;
    lvg_color_t     grip_color;    /* grip dots/lines on divider           */
    bool            show_grip;     /* draw grip indicator (default true)   */
} lui_splitter_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a splitter widget (default: horizontal, 50/50). */
void lui_splitter_init(lui_splitter_t *sp);

/** Set the split ratio (0.0–1.0). */
void lui_splitter_set_ratio(lui_splitter_t *sp, float ratio);

/** Reset to 50/50 split. */
void lui_splitter_reset(lui_splitter_t *sp);

/**
 * Get the rects for the two panels in absolute coordinates.
 * Call after lui_layout_compute().
 */
void lui_splitter_get_panels(const lui_splitter_t *sp,
                              lvg_rect_t *first, lvg_rect_t *second);

/** Get the widget node. */
static inline lui_widget_t *lui_splitter_widget(lui_splitter_t *sp) {
    return &sp->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_SPLITTER_H */
