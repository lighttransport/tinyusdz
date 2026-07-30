/*
 * lightui/draglist.h — Reorderable drag list widget
 *
 * A vertical list of labelled items that can be reordered by dragging.
 * Mouse-down starts a drag, mouse-move updates the insertion point, and
 * mouse-up completes the reorder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_DRAGLIST_H
#define LIGHTUI_DRAGLIST_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_DRAGLIST_MAX  32

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_draglist_reorder_fn)(int from, int to, void *user);

/* ---- Drag list item ----------------------------------------------------- */

typedef struct {
    char label[64];
} lui_draglist_item_t;

/* ---- Drag list widget --------------------------------------------------- */

typedef struct {
    lui_widget_t          widget;

    /* Items */
    lui_draglist_item_t   items[LUI_DRAGLIST_MAX];
    int                   count;

    /* Interaction */
    int                   selected;       /* selected index (-1 = none)      */
    int                   drag_index;     /* item being dragged (-1 = none)  */
    int                   drag_y;         /* current mouse y during drag     */
    int                   drag_start_y;   /* mouse y at drag start           */
    int                   insert_index;   /* insertion target (-1 = none)    */

    /* Appearance */
    int                   item_height;    /* height of each item (default 28)*/
    lvg_color_t           bg_color;
    lvg_color_t           item_bg;
    lvg_color_t           item_selected_bg;
    lvg_color_t           item_hover_bg;
    lvg_color_t           text_color;
    lvg_color_t           border_color;
    lvg_color_t           drag_indicator_color;

    /* Callback */
    lui_draglist_reorder_fn on_reorder;
    void                   *on_reorder_user;
} lui_draglist_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a drag list widget with default appearance. */
void lui_draglist_init(lui_draglist_t *dl);

/** Add an item.  Returns the index or -1 if full. */
int lui_draglist_add_item(lui_draglist_t *dl, const char *label);

/** Remove an item by index.  Returns 0 on success, -1 on failure. */
int lui_draglist_remove_item(lui_draglist_t *dl, int index);

/** Remove all items. */
void lui_draglist_clear(lui_draglist_t *dl);

/** Get the widget node. */
static inline lui_widget_t *lui_draglist_widget(lui_draglist_t *dl) {
    return &dl->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_DRAGLIST_H */
