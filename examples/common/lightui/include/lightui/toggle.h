/*
 * lightui/toggle.h — Toggle switch widget
 *
 * A pill-shaped on/off toggle switch with a sliding circle thumb.
 * Supports smooth animated transitions between states.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TOGGLE_H
#define LIGHTUI_TOGGLE_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_toggle_change_fn)(bool on, void *user);

typedef struct {
    lui_widget_t          widget;
    bool                  on;              /* current state                  */
    int                   track_width;     /* pill track width (default 44) */
    int                   track_height;    /* pill track height (default 22)*/
    int                   thumb_radius;    /* thumb circle radius (default 8)*/
    float                 anim_pos;        /* animation position 0.0–1.0   */
    lvg_color_t           on_color;        /* track fill when on            */
    lvg_color_t           off_color;       /* track fill when off           */
    lvg_color_t           thumb_color;     /* thumb circle colour           */
    lui_toggle_change_fn  on_change;       /* called on state change        */
    void                 *on_change_user;
} lui_toggle_t;

/**
 * Initialise a toggle switch.
 * Default: 44x22 track, 8px thumb radius, blue on / dark off, white thumb.
 */
void lui_toggle_init(lui_toggle_t *t, bool on);

/** Set the on/off state.  Does not trigger on_change. */
void lui_toggle_set(lui_toggle_t *t, bool on);

/** Get the widget node. */
static inline lui_widget_t *lui_toggle_widget(lui_toggle_t *t) {
    return &t->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TOGGLE_H */
