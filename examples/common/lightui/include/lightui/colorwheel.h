/*
 * lightui/colorwheel.h — Color wheel widget for color grading/correction
 *
 * An HSV color wheel with a hue ring and saturation/value triangle (or disc).
 * Suitable for photo/video color grading workflows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_COLORWHEEL_H
#define LIGHTUI_COLORWHEEL_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_colorwheel_change_fn)(float h, float s, float v, void *user);

typedef struct {
    lui_widget_t widget;

    /* Current color in HSV */
    float hue;         /* 0..360 degrees                    */
    float saturation;  /* 0..1                              */
    float value;       /* 0..1 (brightness)                 */

    /* Geometry */
    int   ring_width;  /* width of the hue ring (default 16)*/
    int   size;        /* diameter of the wheel (default 160)*/

    /* Interaction state */
    int   drag_mode;   /* 0=none, 1=hue ring, 2=SV area     */

    /* Appearance */
    lvg_color_t bg;           /* background behind the wheel       */
    lvg_color_t indicator;    /* selection indicator color          */
    int         indicator_r;  /* indicator circle radius (default 5)*/

    /* Callback */
    lui_colorwheel_change_fn on_change;
    void                    *on_change_user;
} lui_colorwheel_t;

/**
 * Initialise a color wheel widget.
 * Default: 160x160 HSV wheel with 16px hue ring.
 */
void lui_colorwheel_init(lui_colorwheel_t *cw);

/** Set the color (HSV). Does not trigger on_change. */
void lui_colorwheel_set_hsv(lui_colorwheel_t *cw, float h, float s, float v);

/** Get the current color as packed ARGB. */
lvg_color_t lui_colorwheel_get_rgb(const lui_colorwheel_t *cw);

/** Set color from RGB. Does not trigger on_change. */
void lui_colorwheel_set_rgb(lui_colorwheel_t *cw, lvg_color_t rgb);

/** Convert HSV (h:0-360, s:0-1, v:0-1) to ARGB color. */
lvg_color_t lui_hsv_to_rgb(float h, float s, float v);

/** Convert ARGB color to HSV. */
void lui_rgb_to_hsv(lvg_color_t rgb, float *h, float *s, float *v);

static inline lui_widget_t *lui_colorwheel_widget(lui_colorwheel_t *cw) {
    return &cw->widget;
}

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_COLORWHEEL_H */
