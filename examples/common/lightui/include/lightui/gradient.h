/*
 * lightui/gradient.h — Gradient editor widget
 *
 * Edit color gradients with draggable color stops.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_GRADIENT_H
#define LIGHTUI_GRADIENT_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_GRADIENT_MAX_STOPS  32

typedef struct {
    float       position;   /* 0.0–1.0 along the gradient              */
    lvg_color_t color;      /* color at this stop                      */
} lui_gradient_stop_t;

typedef void (*lui_gradient_change_fn)(void *user);

typedef struct {
    lui_widget_t         widget;

    /* Stops */
    lui_gradient_stop_t  stops[LUI_GRADIENT_MAX_STOPS];
    int                  stop_count;
    int                  selected_stop; /* -1 = none                    */

    /* Interaction */
    bool                 dragging;      /* dragging a stop              */
    int                  drag_stop;     /* index of dragged stop        */

    /* Appearance */
    int                  bar_height;    /* gradient bar height (24)     */
    int                  stop_size;     /* stop handle size (10)        */
    int                  corner_radius;
    lvg_color_t          bg;
    lvg_color_t          border_color;
    lvg_color_t          stop_border;
    lvg_color_t          selected_border;
    lvg_color_t          checker_a;     /* for transparency preview     */
    lvg_color_t          checker_b;

    /* Callback */
    lui_gradient_change_fn on_change;
    void                  *on_change_user;
} lui_gradient_t;

/* ---- API ---------------------------------------------------------------- */

void lui_gradient_init(lui_gradient_t *g);
int  lui_gradient_add_stop(lui_gradient_t *g, float position, lvg_color_t color);
void lui_gradient_remove_stop(lui_gradient_t *g, int index);
void lui_gradient_set_stop(lui_gradient_t *g, int index,
                            float position, lvg_color_t color);
lvg_color_t lui_gradient_evaluate(const lui_gradient_t *g, float t);
void lui_gradient_clear(lui_gradient_t *g);

static inline lui_widget_t *lui_gradient_widget(lui_gradient_t *g) {
    return &g->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_GRADIENT_H */
