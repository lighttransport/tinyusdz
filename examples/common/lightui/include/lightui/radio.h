/*
 * lightui/radio.h — Radio button group widget
 *
 * A group of mutually exclusive radio buttons.  Exactly one item
 * can be selected at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_RADIO_H
#define LIGHTUI_RADIO_H

#include "layout.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_RADIO_MAX_ITEMS     16
#define LUI_RADIO_MAX_ITEM_LEN  63  /* max bytes per item label (excl. NUL) */

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_radio_change_fn)(int index, void *user);

/* ---- Orientation -------------------------------------------------------- */

typedef enum {
    LUI_RADIO_COLUMN = 0,  /* items stacked vertically   */
    LUI_RADIO_ROW    = 1,  /* items laid out horizontally */
} lui_radio_orient_t;

/* ---- Radio group widget ------------------------------------------------- */

typedef struct {
    lui_widget_t        widget;

    /* Items */
    char                items[LUI_RADIO_MAX_ITEMS][LUI_RADIO_MAX_ITEM_LEN + 1];
    int                 item_count;
    int                 selected;          /* selected index (-1 = none)    */

    /* Layout */
    lui_radio_orient_t  orientation;       /* row or column                 */
    int                 item_spacing;      /* gap between items (default 20)*/
    int                 circle_diameter;   /* outer circle diameter (14)    */
    int                 inner_radius;      /* selected dot radius (4)       */
    int                 label_gap;         /* gap between circle and label  */
    int                 border_width;      /* outer circle stroke width (1) */

    /* Appearance */
    lvg_color_t         circle_color;      /* outer circle fill             */
    lvg_color_t         selected_color;    /* inner dot colour              */
    lvg_color_t         text_color;        /* label text colour             */
    lvg_color_t         border_color;      /* outer circle border           */
    lui_font_t         *font;              /* optional label font, not owned */

    /* Callback */
    lui_radio_change_fn on_change;
    void               *on_change_user;
} lui_radio_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a radio button group with default appearance. */
void lui_radio_init(lui_radio_t *r);

/**
 * Add an item to the radio group.  Returns the item index or -1 on failure.
 * The label is copied internally (max LUI_RADIO_MAX_ITEM_LEN bytes).
 */
int lui_radio_add_item(lui_radio_t *r, const char *label);

/** Set the selected item by index (-1 to deselect). */
void lui_radio_set_selected(lui_radio_t *r, int index);

/** Get the widget node. */
static inline lui_widget_t *lui_radio_widget(lui_radio_t *r) {
    return &r->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_RADIO_H */
