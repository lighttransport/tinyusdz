/*
 * lightui/numentry.h — Numeric entry widget
 *
 * A number input field with increment/decrement buttons on each side.
 * Click the "−" or "+" buttons to step the value.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_NUMENTRY_H
#define LIGHTUI_NUMENTRY_H

#include "layout.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_numentry_change_fn)(double value, void *user);

typedef struct {
    lui_widget_t          widget;
    double                value;            /* current value               */
    double                min_val;          /* range minimum               */
    double                max_val;          /* range maximum               */
    double                step;             /* increment/decrement step    */
    int                   precision;        /* decimal places for display  */
    lvg_color_t           bg_color;         /* centre field background     */
    lvg_color_t           text_color;       /* value text colour           */
    lvg_color_t           border_color;     /* overall border              */
    lvg_color_t           button_color;     /* +/− button background       */
    lvg_color_t           button_hover_color; /* +/− button hover          */
    int                   hover_button;     /* -1=none, 0=minus, 1=plus    */
    int                   button_width;     /* +/- button width (default 28) */
    lui_font_t           *font;             /* optional value font, not owned */
    lui_numentry_change_fn on_change;       /* called when value changes   */
    void                  *on_change_user;
} lui_numentry_t;

/**
 * Initialise a numeric entry widget.
 * Default: 120x28, 2 decimal places, dark appearance.
 */
void lui_numentry_init(lui_numentry_t *n, double min_val, double max_val,
                       double value, double step);

/** Set the value (clamped to [min, max]).  Does not trigger on_change. */
void lui_numentry_set_value(lui_numentry_t *n, double value);

/** Get the widget node. */
static inline lui_widget_t *lui_numentry_widget(lui_numentry_t *n) {
    return &n->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_NUMENTRY_H */
