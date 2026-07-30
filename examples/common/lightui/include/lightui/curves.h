/*
 * lightui/curves.h — Curves editor widget for tone/color correction
 *
 * A spline-based curves widget with draggable control points.
 * Supports multiple channels (master, R, G, B) for photo/video grading.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_CURVES_H
#define LIGHTUI_CURVES_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_CURVES_MAX_POINTS 16

typedef enum {
    LUI_CURVES_MASTER = 0,
    LUI_CURVES_RED    = 1,
    LUI_CURVES_GREEN  = 2,
    LUI_CURVES_BLUE   = 3,
    LUI_CURVES_CHANNEL_COUNT = 4,
} lui_curves_channel_t;

typedef struct {
    float x;  /* 0..1 input (shadow..highlight) */
    float y;  /* 0..1 output */
} lui_curve_point_t;

typedef void (*lui_curves_change_fn)(lui_curves_channel_t channel, void *user);

typedef struct {
    lui_widget_t widget;

    /* Per-channel control points */
    lui_curve_point_t points[LUI_CURVES_CHANNEL_COUNT][LUI_CURVES_MAX_POINTS];
    int               point_count[LUI_CURVES_CHANNEL_COUNT];

    /* Active channel */
    lui_curves_channel_t active_channel;

    /* Interaction state */
    int drag_point;  /* index of point being dragged, -1 = none */
    int hover_point; /* index of hovered point, -1 = none */

    /* Appearance */
    lvg_color_t bg;
    lvg_color_t grid_color;
    lvg_color_t curve_colors[LUI_CURVES_CHANNEL_COUNT];
    lvg_color_t point_color;
    lvg_color_t point_active;
    int         point_radius;    /* control point radius (default 4) */
    int         grid_divisions;  /* grid lines per axis (default 4)  */
    int         curve_width;     /* curve line width (default 2)     */

    /* Histogram overlay (optional, user-provided) */
    const float *histogram;      /* 256 normalized values, or NULL   */
    lvg_color_t  histogram_color;

    /* Callback */
    lui_curves_change_fn on_change;
    void                *on_change_user;
} lui_curves_t;

/**
 * Initialise a curves widget.
 * Default: 256x256, identity curve (diagonal), master channel active.
 */
void lui_curves_init(lui_curves_t *c);

/** Reset a channel to identity (diagonal line). */
void lui_curves_reset_channel(lui_curves_t *c, lui_curves_channel_t ch);

/** Reset all channels to identity. */
void lui_curves_reset_all(lui_curves_t *c);

/** Set the active channel. */
void lui_curves_set_channel(lui_curves_t *c, lui_curves_channel_t ch);

/**
 * Add a control point to the active channel.
 * Returns the index of the new point, or -1 if at max capacity.
 */
int lui_curves_add_point(lui_curves_t *c, float x, float y);

/** Remove a control point by index from the active channel. */
void lui_curves_remove_point(lui_curves_t *c, int index);

/**
 * Evaluate the curve at input x (0..1) for the given channel.
 * Returns the output y (0..1).
 */
float lui_curves_evaluate(const lui_curves_t *c, lui_curves_channel_t ch,
                           float x);

/**
 * Build a 256-entry LUT from the curve for the given channel.
 * out[i] = curve(i/255.0) * 255, clamped to [0,255].
 */
void lui_curves_build_lut(const lui_curves_t *c, lui_curves_channel_t ch,
                            uint8_t *out);

static inline lui_widget_t *lui_curves_widget(lui_curves_t *c) {
    return &c->widget;
}

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_CURVES_H */
