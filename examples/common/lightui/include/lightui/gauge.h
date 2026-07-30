/*
 * lightui/gauge.h -- Circular gauge / meter widget
 *
 * A read-only circular gauge for monitoring values such as CPU load or
 * temperature.  Draws an arc track with a filled portion whose colour
 * changes at configurable warning and danger thresholds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_GAUGE_H
#define LIGHTUI_GAUGE_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lui_widget_t  widget;
    float         value;           /* normalised 0..1                    */
    float         min_val;         /* display minimum                    */
    float         max_val;         /* display maximum                    */
    char          label[32];       /* bottom label text                  */
    char          unit[16];        /* unit suffix (e.g. "%", "C")        */
    int           size;            /* diameter in pixels                 */

    /* arc geometry */
    float         start_angle;     /* degrees, 0 = right, CW positive   */
    float         sweep_angle;     /* arc sweep in degrees (default 240) */
    int           thickness;       /* arc band thickness in pixels       */

    /* colours */
    lvg_color_t   bg_color;
    lvg_color_t   track_color;
    lvg_color_t   fill_color;
    lvg_color_t   text_color;
    lvg_color_t   border_color;
    lvg_color_t   needle_color;

    /* thresholds */
    float         warning_threshold;  /* fraction 0..1, default 0.7     */
    float         danger_threshold;   /* fraction 0..1, default 0.9     */
    lvg_color_t   warning_color;
    lvg_color_t   danger_color;
} lui_gauge_t;

/** Initialise a gauge with a display range and diameter. */
void lui_gauge_init(lui_gauge_t *g, float min_val, float max_val, int size);

/** Set the current value (clamped to [min, max]). */
void lui_gauge_set_value(lui_gauge_t *g, float value);

/** Get the widget node. */
static inline lui_widget_t *lui_gauge_widget(lui_gauge_t *g) {
    return &g->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_GAUGE_H */
