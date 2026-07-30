/*
 * lightui/combo.h — Combo box (dropdown) widget
 *
 * A dropdown selector that shows a list of string items.
 * Click to open/close the dropdown; select an item to close.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_COMBO_H
#define LIGHTUI_COMBO_H

#include "layout.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_COMBO_MAX_ITEMS     64
#define LUI_COMBO_MAX_ITEM_LEN  63  /* max bytes per item label (excl. NUL) */

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_combo_change_fn)(int index, const char *item, void *user);

/* ---- Combo box widget --------------------------------------------------- */

typedef struct {
    lui_widget_t  widget;

    /* Items */
    char          items[LUI_COMBO_MAX_ITEMS][LUI_COMBO_MAX_ITEM_LEN + 1];
    int           item_count;
    int           selected;        /* selected item index (-1 = none)     */

    /* Interaction state */
    bool          open;            /* dropdown is visible                 */
    int           hovered;         /* hovered item index (-1 = none)      */
    int           max_visible;     /* max items visible in dropdown (8)   */
    int           scroll_offset;   /* first visible item index            */

    /* Appearance */
    int           item_height;     /* height of each dropdown row (24)    */
    int           corner_radius;   /* corner rounding (3)                 */
    int           text_padding;    /* left text inset (8)                 */
    int           arrow_size;      /* dropdown arrow size (8)             */
    int           arrow_area_width;/* right arrow area width (24)         */
    int           border_width;    /* border stroke width (1)             */
    lvg_color_t   bg_color;        /* main button background              */
    lvg_color_t   border_color;    /* border color                        */
    lvg_color_t   text_color;      /* text color                          */
    lvg_color_t   arrow_color;     /* dropdown arrow color                */
    lvg_color_t   drop_bg;         /* dropdown list background            */
    lvg_color_t   drop_hover;      /* hovered item highlight              */
    lvg_color_t   drop_border;     /* dropdown border                     */
    lvg_color_t   bevel_light;     /* 3D bevel highlight                  */
    lvg_color_t   bevel_shadow;    /* 3D bevel shadow                     */
    lui_font_t   *font;            /* optional font for labels, not owned */

    /* Callback */
    lui_combo_change_fn on_change;
    void               *on_change_user;
} lui_combo_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a combo box widget with default appearance. */
void lui_combo_init(lui_combo_t *cb);

/**
 * Add an item to the combo box.  Returns the item index or -1 on failure.
 * The label is copied internally (max LUI_COMBO_MAX_ITEM_LEN bytes).
 */
int lui_combo_add_item(lui_combo_t *cb, const char *label);

/** Remove all items. */
void lui_combo_clear(lui_combo_t *cb);

/** Set the selected item by index (-1 to deselect). */
void lui_combo_set_selected(lui_combo_t *cb, int index);

/** Get the label of the selected item (or NULL if none). */
const char *lui_combo_selected_text(const lui_combo_t *cb);

/** Absolute dropdown bounds for the current layout. Empty if there are no items. */
lvg_rect_t lui_combo_dropdown_rect(const lui_combo_t *cb);

/** Draw only the dropdown overlay. Useful after the main tree draw for z-order. */
void lui_combo_draw_dropdown(lui_combo_t *cb, lvg_canvas_t *canvas);

/** Get the widget node. */
static inline lui_widget_t *lui_combo_widget(lui_combo_t *cb) {
    return &cb->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_COMBO_H */
