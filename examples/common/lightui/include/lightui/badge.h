/*
 * lightui/badge.h — Small status badge / tag label
 *
 * A compact indicator that can display a coloured dot, a numeric count,
 * or a short text label inside a rounded pill shape.  Commonly attached
 * as a child widget for notification counts on buttons or icons.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_BADGE_H
#define LIGHTUI_BADGE_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Badge style -------------------------------------------------------- */

typedef enum {
    LUI_BADGE_DOT   = 0,   /* small filled circle (8px)           */
    LUI_BADGE_COUNT = 1,   /* rounded pill with number            */
    LUI_BADGE_LABEL = 2,   /* rounded pill with arbitrary text    */
} lui_badge_style_t;

/* ---- Badge widget ------------------------------------------------------- */

typedef struct {
    lui_widget_t      widget;

    char              text[32];
    lui_badge_style_t style;

    /* Appearance */
    lvg_color_t       bg_color;
    lvg_color_t       text_color;
} lui_badge_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a badge widget with the given style. */
void lui_badge_init(lui_badge_t *badge, lui_badge_style_t style);

/** Set the badge text (for COUNT or LABEL styles). */
void lui_badge_set_text(lui_badge_t *badge, const char *text);

/** Set a numeric count (formats the number as text). */
void lui_badge_set_count(lui_badge_t *badge, int n);

/** Get the widget node. */
static inline lui_widget_t *lui_badge_widget(lui_badge_t *badge) {
    return &badge->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_BADGE_H */
