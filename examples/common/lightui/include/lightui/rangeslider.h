/*
 * lightui/rangeslider.h -- Dual-handle range slider widget
 *
 * A horizontal slider with two thumbs for selecting a value range.
 * Supports dragging individual handles or the range bar between them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_RANGESLIDER_H
#define LIGHTUI_RANGESLIDER_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_rangeslider_change_fn)(float low, float high, void *user);

typedef struct {
    lui_widget_t               widget;
    float                      low;            /* current low value         */
    float                      high;           /* current high value        */
    float                      min_val;        /* range minimum             */
    float                      max_val;        /* range maximum             */
    float                      min_span;       /* min distance low..high    */

    /* drag state */
    bool                       dragging_low;
    bool                       dragging_high;
    bool                       dragging_range;
    int                        drag_start_x;
    float                      drag_start_low;
    float                      drag_start_high;

    /* appearance */
    int                        thumb_radius;   /* default 7                 */
    int                        track_height;   /* default 4                 */
    lvg_color_t                track_color;
    lvg_color_t                range_color;
    lvg_color_t                thumb_color;
    lvg_color_t                thumb_active;
    lvg_color_t                border_color;

    lui_rangeslider_change_fn  on_change;
    void                      *on_change_user;
} lui_rangeslider_t;

/** Initialise a range slider. */
void lui_rangeslider_init(lui_rangeslider_t *rs, float min_val, float max_val,
                          float low, float high);

/** Set the low/high range (clamped, does not trigger callback). */
void lui_rangeslider_set_range(lui_rangeslider_t *rs, float low, float high);

/** Get the widget node. */
static inline lui_widget_t *lui_rangeslider_widget(lui_rangeslider_t *rs) {
    return &rs->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_RANGESLIDER_H */
