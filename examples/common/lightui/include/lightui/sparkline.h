/*
 * lightui/sparkline.h — Tiny inline chart widget
 *
 * Compact sparkline for showing trends in tables, status bars, or dashboards.
 * Supports line, area, and bar styles with optional reference line.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_SPARKLINE_H
#define LIGHTUI_SPARKLINE_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Constants ---------------------------------------------------------- */

#define LUI_SPARKLINE_MAX  128

/* ---- Types -------------------------------------------------------------- */

typedef enum {
    LUI_SPARKLINE_LINE = 0,
    LUI_SPARKLINE_BAR  = 1,
    LUI_SPARKLINE_AREA = 2,
} lui_sparkline_style_t;

/* ---- Sparkline widget --------------------------------------------------- */

typedef struct {
    lui_widget_t widget;

    /* Data */
    float         values[LUI_SPARKLINE_MAX];
    int           value_count;

    /* Range (0 = auto-range from data) */
    float         min_val;
    float         max_val;

    /* Display options */
    lui_sparkline_style_t style;
    bool          show_area;           /* fill under line (LINE mode)         */
    bool          show_endpoint;       /* dot at last value                   */

    /* Reference line */
    float         reference_value;
    bool          show_reference;

    /* Appearance */
    lvg_color_t   line_color;
    lvg_color_t   area_color;          /* semi-transparent fill               */
    lvg_color_t   endpoint_color;
    lvg_color_t   bg_color;
    lvg_color_t   reference_color;
    lvg_color_t   positive_color;      /* above reference                     */
    lvg_color_t   negative_color;      /* below reference                     */
} lui_sparkline_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a sparkline widget with the given style. */
void lui_sparkline_init(lui_sparkline_t *sp, lui_sparkline_style_t style);

/** Set data from an array of floats (copied into internal buffer). */
void lui_sparkline_set_data(lui_sparkline_t *sp, const float *values, int count);

/** Set the value range (pass 0, 0 for auto-range). */
void lui_sparkline_set_range(lui_sparkline_t *sp, float min_val, float max_val);

/** Set the reference line value and make it visible. */
void lui_sparkline_set_reference(lui_sparkline_t *sp, float value);

/** Append a value; shifts left if the buffer is full. */
void lui_sparkline_push(lui_sparkline_t *sp, float value);

/** Get the widget node. */
static inline lui_widget_t *lui_sparkline_widget(lui_sparkline_t *sp) {
    return &sp->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_SPARKLINE_H */
