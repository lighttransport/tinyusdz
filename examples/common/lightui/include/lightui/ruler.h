/*
 * lightui/ruler.h — Measurement ruler widget for editors
 *
 * Displays a measurement ruler similar to those found along the edges
 * of design tools (Photoshop, Illustrator).  Supports horizontal and
 * vertical orientations with major, minor, and micro tick subdivisions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_RULER_H
#define LIGHTUI_RULER_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Orientation -------------------------------------------------------- */

typedef enum {
    LUI_RULER_HORIZONTAL = 0,
    LUI_RULER_VERTICAL   = 1,
} lui_ruler_orientation_t;

/* ---- Ruler widget ------------------------------------------------------- */

typedef struct {
    lui_widget_t             widget;

    lui_ruler_orientation_t  orientation;

    /* World-space mapping */
    float                    origin;          /* scroll offset in world units   */
    float                    scale;           /* pixels per world unit          */
    float                    range_min;       /* minimum world coordinate       */
    float                    range_max;       /* maximum world coordinate       */

    /* Tick intervals */
    float                    major_interval;  /* world units between major ticks */
    int                      minor_divisions; /* subdivisions of a major tick    */
    int                      micro_divisions; /* subdivisions of a minor tick    */

    /* Dimensions */
    int                      ruler_thickness; /* cross-axis size in pixels       */

    /* Cursor indicator */
    float                    cursor_pos;      /* current position in world units */
    bool                     show_cursor;

    /* Colours */
    lvg_color_t              bg_color;
    lvg_color_t              tick_color;
    lvg_color_t              text_color;
    lvg_color_t              cursor_color;
    lvg_color_t              border_color;
} lui_ruler_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a ruler with the given orientation. */
void lui_ruler_init(lui_ruler_t *r, lui_ruler_orientation_t orientation);

/** Set the displayed world-coordinate range. */
void lui_ruler_set_range(lui_ruler_t *r, float min_val, float max_val);

/** Set the scroll origin (world units). */
void lui_ruler_set_origin(lui_ruler_t *r, float origin);

/** Set the zoom scale (pixels per world unit). */
void lui_ruler_set_scale(lui_ruler_t *r, float scale);

/** Set the cursor position (world units) and show it. */
void lui_ruler_set_cursor(lui_ruler_t *r, float pos);

/** Get the widget node. */
static inline lui_widget_t *lui_ruler_widget(lui_ruler_t *r) {
    return &r->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_RULER_H */
