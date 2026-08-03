/*
 * lightui/progress.h — Progress bar and circular gauge widget
 *
 * Supports determinate (0–100%), indeterminate (animated),
 * and stacked multi-segment progress bars.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_PROGRESS_H
#define LIGHTUI_PROGRESS_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Constants ---------------------------------------------------------- */

#define LUI_PROGRESS_MAX_SEGMENTS  8

typedef enum {
    LUI_PROGRESS_BAR      = 0,  /* horizontal bar                       */
    LUI_PROGRESS_CIRCULAR = 1,  /* circular gauge (arc)                  */
} lui_progress_style_t;

/* ---- Segment (for stacked progress) ------------------------------------- */

typedef struct {
    float       value;      /* 0.0–1.0 fraction of this segment         */
    lvg_color_t color;      /* segment fill color                       */
} lui_progress_segment_t;

/* ---- Progress widget ---------------------------------------------------- */

typedef struct {
    lui_widget_t           widget;

    /* Value */
    float                  value;          /* overall progress 0.0–1.0    */
    bool                   indeterminate;  /* animated bouncing indicator  */

    /* Segments (for stacked display) */
    lui_progress_segment_t segments[LUI_PROGRESS_MAX_SEGMENTS];
    int                    segment_count;  /* 0 = use single value        */

    /* Style */
    lui_progress_style_t   style;
    int                    bar_height;     /* bar mode height (12)        */
    int                    arc_width;      /* circular arc stroke (6)     */
    int                    corner_radius;  /* bar corner rounding (4)     */
    int                    border_width;   /* bar border stroke (1)       */

    /* Indeterminate animation state */
    float                  anim_phase;     /* 0.0–1.0 animation phase     */

    /* Appearance */
    lvg_color_t            bg;             /* track background            */
    lvg_color_t            fill;           /* single-value fill color     */
    lvg_color_t            border_color;
    lvg_color_t            text_color;     /* percentage text color        */
    bool                   show_text;      /* show "XX%" label            */
} lui_progress_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a progress widget (default: horizontal bar). */
void lui_progress_init(lui_progress_t *p);

/** Set the progress value (0.0–1.0, clamped). */
void lui_progress_set_value(lui_progress_t *p, float value);

/**
 * Add a segment for stacked display. Returns segment index or -1.
 * When segments are used, they override the single `value` display.
 */
int lui_progress_add_segment(lui_progress_t *p, float value,
                              lvg_color_t color);

/** Set a segment's value. */
void lui_progress_set_segment(lui_progress_t *p, int index, float value);

/** Clear all segments (revert to single-value mode). */
void lui_progress_clear_segments(lui_progress_t *p);

/**
 * Advance indeterminate animation by dt seconds.
 * Call from your frame loop. Returns true if display changed.
 */
bool lui_progress_animate(lui_progress_t *p, float dt);

/** Get the widget node. */
static inline lui_widget_t *lui_progress_widget(lui_progress_t *p) {
    return &p->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_PROGRESS_H */
