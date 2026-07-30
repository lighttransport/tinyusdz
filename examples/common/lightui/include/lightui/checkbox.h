/*
 * lightui/checkbox.h — Checkbox (toggle) widget
 *
 * A clickable checkbox with checked/unchecked state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_CHECKBOX_H
#define LIGHTUI_CHECKBOX_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_checkbox_toggle_fn)(bool checked, void *user);

typedef struct {
    lui_widget_t          widget;
    bool                  checked;           /* current state                 */
    int                   box_size;          /* side length of the box        */
    lvg_color_t           box_color;         /* box fill (unchecked)          */
    lvg_color_t           box_checked_color; /* box fill (checked)            */
    lvg_color_t           border_color;      /* box border                    */
    lvg_color_t           check_color;       /* checkmark colour              */
    lvg_color_t           bevel_light;       /* top-left bevel (TRANSPARENT=none)  */
    lvg_color_t           bevel_shadow;      /* bottom-right bevel (TRANSPARENT=none) */
    int                   corner_radius;     /* corner rounding (0=square)    */
    int                   border_width;      /* border stroke width (1)       */
    int                   check_width;       /* checkmark stroke width (2)    */
    lui_checkbox_toggle_fn on_toggle;        /* called on state change        */
    void                  *on_toggle_user;
} lui_checkbox_t;

/**
 * Initialise a checkbox.
 * Default: 18x18 box, dark fill, blue when checked, white checkmark.
 */
void lui_checkbox_init(lui_checkbox_t *cb, bool checked);

/** Toggle the state.  Does not trigger on_toggle. */
void lui_checkbox_set_checked(lui_checkbox_t *cb, bool checked);

/** Get the widget node. */
static inline lui_widget_t *lui_checkbox_widget(lui_checkbox_t *cb) {
    return &cb->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_CHECKBOX_H */
