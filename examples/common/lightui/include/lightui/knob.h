/*
 * lightui/knob.h — Rotary knob (dial) widget
 *
 * A circular knob for continuous parameter control.  Drag vertically
 * to change the value.  Renders an arc track and a line indicator.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_KNOB_H
#define LIGHTUI_KNOB_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_knob_change_fn)(float value, void *user);

typedef struct {
    lui_widget_t        widget;
    float               value;            /* current value (0.0–1.0 norm)  */
    float               min_val;          /* range minimum                 */
    float               max_val;          /* range maximum                 */
    int                 size;             /* knob diameter in pixels       */
    bool                dragging;         /* currently being dragged       */
    int                 drag_start_y;     /* mouse y at drag start         */
    float               drag_start_val;   /* normalised value at drag start*/
    lvg_color_t         knob_color;       /* knob body fill                */
    lvg_color_t         indicator_color;  /* value indicator line           */
    lvg_color_t         track_color;      /* arc track colour              */
    lvg_color_t         border_color;     /* knob border                   */
    lui_knob_change_fn  on_change;        /* called when value changes     */
    void               *on_change_user;
} lui_knob_t;

/**
 * Initialise a knob with a value range.
 * Default: 48px diameter, dark body, blue indicator, grey track.
 */
void lui_knob_init(lui_knob_t *k, float min_val, float max_val, float value);

/** Set the value (clamped to [min, max]).  Does not trigger on_change. */
void lui_knob_set_value(lui_knob_t *k, float value);

/** Get the widget node. */
static inline lui_widget_t *lui_knob_widget(lui_knob_t *k) {
    return &k->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_KNOB_H */
