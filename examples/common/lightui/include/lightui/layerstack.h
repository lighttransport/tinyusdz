/*
 * lightui/layerstack.h -- Layer stack panel widget
 *
 * A Photoshop-style layer list with visibility, lock, opacity, and blend mode
 * controls per layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_LAYERSTACK_H
#define LIGHTUI_LAYERSTACK_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_LAYERSTACK_MAX 32

typedef enum {
    LUI_BLEND_NORMAL,
    LUI_BLEND_MULTIPLY,
    LUI_BLEND_ADD,
    LUI_BLEND_SCREEN,
} lui_blend_mode_t;

/* Callback actions */
#define LUI_LAYER_SELECT      0
#define LUI_LAYER_TOGGLE_VIS  1
#define LUI_LAYER_TOGGLE_LOCK 2
#define LUI_LAYER_REORDER     3

typedef struct {
    char             name[48];
    bool             visible;
    bool             locked;
    float            opacity;          /* 0..1                          */
    lui_blend_mode_t blend_mode;
    bool             selected;
    lvg_color_t      thumbnail_color;  /* preview swatch colour         */
} lui_layer_t;

typedef void (*lui_layerstack_change_fn)(int layer, int action, void *user);

typedef struct {
    lui_widget_t              widget;
    lui_layer_t               layers[LUI_LAYERSTACK_MAX];
    int                       layer_count;
    int                       active_layer;    /* -1 = none             */
    int                       item_height;     /* default 36            */
    int                       scroll_offset;   /* vertical scroll px    */

    /* colours */
    lvg_color_t               bg_color;
    lvg_color_t               item_bg;
    lvg_color_t               item_selected_bg;
    lvg_color_t               text_color;
    lvg_color_t               border_color;
    lvg_color_t               eye_color;
    lvg_color_t               lock_color;

    lui_layerstack_change_fn  on_change;
    void                     *on_change_user;
} lui_layerstack_t;

/** Initialise a layer stack with default appearance. */
void lui_layerstack_init(lui_layerstack_t *ls);

/** Add a layer with the given name. Returns layer index or -1 on overflow. */
int lui_layerstack_add(lui_layerstack_t *ls, const char *name);

/** Remove layer at index. */
void lui_layerstack_remove(lui_layerstack_t *ls, int index);

/** Set the active (selected) layer. */
void lui_layerstack_set_active(lui_layerstack_t *ls, int index);

/** Move layer from one position to another. */
void lui_layerstack_move(lui_layerstack_t *ls, int from, int to);

/** Get the widget node. */
static inline lui_widget_t *lui_layerstack_widget(lui_layerstack_t *ls) {
    return &ls->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_LAYERSTACK_H */
