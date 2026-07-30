/*
 * lightui/slider.h — Horizontal slider widget
 *
 * A draggable slider that maps a continuous value between min and max.
 * Integrates with the layout framework via measure, draw, and event callbacks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_SLIDER_H
#define LIGHTUI_SLIDER_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_slider_change_fn)(float value, void *user);

typedef struct {
    lui_widget_t        widget;
    float               value;            /* current value                    */
    float               min_val;          /* range minimum                    */
    float               max_val;          /* range maximum                    */
    bool                dragging;         /* currently being dragged          */
    int                 thumb_radius;     /* thumb circle radius (default 7)  */
    int                 track_height;     /* track bar height (default 4)     */
    lvg_color_t         track_color;      /* track background                 */
    lvg_color_t         track_fill_color; /* filled portion of track          */
    lvg_color_t         thumb_color;      /* thumb at rest                    */
    lvg_color_t         thumb_active;     /* thumb while dragging             */
    lui_slider_change_fn on_change;       /* called when value changes        */
    void               *on_change_user;
} lui_slider_t;

/**
 * Initialise a slider with a value range.
 * Default appearance: 120x24, dark track, blue fill, white thumb.
 */
void lui_slider_init(lui_slider_t *s, float min_val, float max_val, float value);

/** Set the value (clamped to [min, max]). Does not trigger on_change. */
void lui_slider_set_value(lui_slider_t *s, float value);

/** Get the widget node. */
static inline lui_widget_t *lui_slider_widget(lui_slider_t *s) {
    return &s->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_SLIDER_H */
