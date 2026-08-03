/*
 * lightui/minimap.h — Minimap / overview widget
 *
 * A scaled-down bird's-eye view of a large canvas area
 * with a draggable viewport rectangle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_MINIMAP_H
#define LIGHTUI_MINIMAP_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_minimap_pan_fn)(float view_x, float view_y, void *user);

typedef struct {
    lui_widget_t   widget;

    /* World bounds (the full area being overviewed) */
    float          world_x, world_y;
    float          world_w, world_h;

    /* Current visible viewport rectangle (in world coordinates) */
    float          view_x, view_y;
    float          view_w, view_h;

    /* Interaction */
    bool           dragging;
    float          drag_offset_x;
    float          drag_offset_y;

    /* Appearance */
    lvg_color_t    bg;
    lvg_color_t    viewport_fill;
    lvg_color_t    viewport_border;
    lvg_color_t    border_color;
    int            corner_radius;

    /* Callback — called when user drags the viewport */
    lui_minimap_pan_fn on_pan;
    void              *on_pan_user;
} lui_minimap_t;

/* ---- API ---------------------------------------------------------------- */

void lui_minimap_init(lui_minimap_t *mm);
void lui_minimap_set_world(lui_minimap_t *mm,
                            float x, float y, float w, float h);
void lui_minimap_set_viewport(lui_minimap_t *mm,
                               float x, float y, float w, float h);

static inline lui_widget_t *lui_minimap_widget(lui_minimap_t *mm) {
    return &mm->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_MINIMAP_H */
