/*
 * lightui/spinner.h — Animated spinner / loading indicator widget
 *
 * A spinning loading indicator with configurable appearance.
 * Supports arc-style spinner and dot-style spinner.
 * Automatically animates when ANIMATING flag is set.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_SPINNER_H
#define LIGHTUI_SPINNER_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Spinner style ------------------------------------------------------ */

typedef enum {
    LUI_SPINNER_ARC  = 0,   /* rotating arc (default)                     */
    LUI_SPINNER_DOTS = 1,   /* orbiting dots with fade                    */
} lui_spinner_style_t;

/* ---- Spinner widget ----------------------------------------------------- */

typedef struct {
    lui_widget_t        widget;

    /* Animation state */
    float               phase;          /* 0.0–1.0 rotation phase          */
    float               speed;          /* rotations per second (1.2)      */
    bool                spinning;       /* true = actively animating       */

    /* Style */
    lui_spinner_style_t style;
    int                 size;           /* overall size in pixels (24)     */
    int                 thickness;      /* arc stroke width (3)            */
    float               arc_length;     /* fraction of circle for arc (0.7)*/
    int                 dot_count;      /* number of dots (8)              */
    int                 dot_radius;     /* dot size in pixels (3)          */

    /* Colors */
    lvg_color_t         color;          /* spinner primary color           */
    lvg_color_t         trail_color;    /* trail / faded color             */
    lvg_color_t         bg;             /* background (0 = transparent)    */
} lui_spinner_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a spinner widget (default: arc style, spinning). */
void lui_spinner_init(lui_spinner_t *sp);

/** Start spinning. Sets ANIMATING flag. */
void lui_spinner_start(lui_spinner_t *sp);

/** Stop spinning. Clears ANIMATING flag. */
void lui_spinner_stop(lui_spinner_t *sp);

/** Check if currently spinning. */
static inline bool lui_spinner_is_spinning(const lui_spinner_t *sp) {
    return sp ? sp->spinning : false;
}

/** Get the widget node. */
static inline lui_widget_t *lui_spinner_widget(lui_spinner_t *sp) {
    return &sp->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_SPINNER_H */
