/*
 * lightui/toolbar.h — Toolbar widget
 *
 * A horizontal or vertical strip of icon buttons with separators
 * and toggle/radio groups.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TOOLBAR_H
#define LIGHTUI_TOOLBAR_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_TOOLBAR_MAX_ITEMS  32
#define LUI_TOOLBAR_MAX_LABEL  15  /* excl. NUL */

/* ---- Item types --------------------------------------------------------- */

typedef enum {
    LUI_TB_BUTTON    = 0,   /* momentary push button                    */
    LUI_TB_TOGGLE    = 1,   /* latching on/off toggle                   */
    LUI_TB_RADIO     = 2,   /* mutually exclusive within a group        */
    LUI_TB_SEPARATOR = 3,   /* visual divider (no interaction)          */
} lui_tb_item_type_t;

typedef struct {
    int                id;           /* unique item ID                   */
    lui_tb_item_type_t type;
    char               label[LUI_TOOLBAR_MAX_LABEL + 1];
    lvg_color_t        icon_color;   /* icon indicator color             */
    bool               active;       /* toggle/radio active state        */
    bool               enabled;      /* greyed out if false              */
    int                group;        /* radio group (0 = no group)       */
} lui_tb_item_t;

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_toolbar_click_fn)(int item_id, bool active, void *user);

/* ---- Toolbar widget ----------------------------------------------------- */

typedef struct {
    lui_widget_t      widget;

    /* Items */
    lui_tb_item_t     items[LUI_TOOLBAR_MAX_ITEMS];
    int               item_count;
    int               next_id;

    /* Layout */
    bool              vertical;       /* true = vertical strip            */
    int               button_size;    /* button width/height (28)         */
    int               spacing;        /* gap between items (2)            */
    int               separator_size; /* separator width/height (8)       */

    /* Interaction */
    int               hovered_id;     /* -1 = none                        */
    int               pressed_id;     /* -1 = none                        */

    /* Appearance */
    lvg_color_t       bg;
    lvg_color_t       button_bg;
    lvg_color_t       button_hover;
    lvg_color_t       button_active;
    lvg_color_t       button_pressed;
    lvg_color_t       border_color;
    lvg_color_t       icon_default;
    lvg_color_t       disabled_color;
    lvg_color_t       separator_color;
    int               corner_radius;

    /* Callback */
    lui_toolbar_click_fn on_click;
    void                *on_click_user;
} lui_toolbar_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a toolbar widget (default: horizontal). */
void lui_toolbar_init(lui_toolbar_t *tb);

/**
 * Add a button/toggle/radio item. Returns item ID (> 0) or -1.
 * For RADIO items, set group > 0 to define mutual exclusion groups.
 */
int lui_toolbar_add_item(lui_toolbar_t *tb, lui_tb_item_type_t type,
                          const char *label, int group);

/** Add a separator. Returns item ID. */
int lui_toolbar_add_separator(lui_toolbar_t *tb);

/** Set an item's active state (for toggle/radio). */
void lui_toolbar_set_active(lui_toolbar_t *tb, int item_id, bool active);

/** Set an item's enabled state. */
void lui_toolbar_set_enabled(lui_toolbar_t *tb, int item_id, bool enabled);

/** Get an item by ID. Returns NULL if not found. */
lui_tb_item_t *lui_toolbar_get_item(lui_toolbar_t *tb, int item_id);

/** Get the widget node. */
static inline lui_widget_t *lui_toolbar_widget(lui_toolbar_t *tb) {
    return &tb->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TOOLBAR_H */
