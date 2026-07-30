/*
 * lightui/menu.h — Context menu / popup menu widget
 *
 * A popup menu supporting normal items, separators, checkboxes, and submenus.
 * Items can have shortcut text and enabled/disabled state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_MENU_H
#define LIGHTUI_MENU_H

#include "layout.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_MENU_MAX_ITEMS       32
#define LUI_MENU_MAX_LABEL_LEN   63  /* max bytes per label (excl. NUL) */
#define LUI_MENU_MAX_SHORTCUT_LEN 15 /* max bytes per shortcut (excl. NUL) */

/* ---- Item type ---------------------------------------------------------- */

typedef enum {
    LUI_MENU_ITEM_NORMAL    = 0,
    LUI_MENU_ITEM_SEPARATOR = 1,
    LUI_MENU_ITEM_CHECKBOX  = 2,
    LUI_MENU_ITEM_SUBMENU   = 3,
} lui_menu_item_type_t;

/* ---- Forward declaration ------------------------------------------------ */

typedef struct lui_menu lui_menu_t;

/* ---- Menu item ---------------------------------------------------------- */

typedef struct {
    lui_menu_item_type_t type;
    char                 label[LUI_MENU_MAX_LABEL_LEN + 1];
    char                 shortcut[LUI_MENU_MAX_SHORTCUT_LEN + 1];
    bool                 enabled;
    bool                 checked;
    lui_menu_t          *submenu;    /* non-NULL for SUBMENU items */
} lui_menu_item_t;

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_menu_select_fn)(int index, void *user);

/* ---- Menu widget -------------------------------------------------------- */

struct lui_menu {
    lui_widget_t      widget;

    /* Items */
    lui_menu_item_t   items[LUI_MENU_MAX_ITEMS];
    int               item_count;

    /* Interaction state */
    bool              open;          /* menu is visible                     */
    int               hovered;       /* hovered item index (-1 = none)      */
    int               popup_x;       /* screen X where menu was opened      */
    int               popup_y;       /* screen Y where menu was opened      */

    /* Appearance */
    int               item_height;   /* height of each item row (24)        */
    int               separator_height; /* height of separator row (9)      */
    int               padding_x;     /* horizontal padding (8)              */
    lvg_color_t       bg_color;      /* menu background                     */
    lvg_color_t       hover_color;   /* hovered item highlight              */
    lvg_color_t       text_color;    /* normal text color                   */
    lvg_color_t       disabled_color;/* disabled text color                 */
    lvg_color_t       separator_color;/* separator line color               */
    lvg_color_t       border_color;  /* border color                        */
    lui_font_t       *font;          /* optional text font, not owned       */

    /* Callback */
    lui_menu_select_fn on_item_selected;
    void              *on_item_selected_user;
};

/* ---- API ---------------------------------------------------------------- */

/** Initialise a menu widget with default appearance. */
void lui_menu_init(lui_menu_t *menu);

/**
 * Add a normal item.  Returns the item index or -1 on failure.
 * @shortcut may be NULL.
 */
int lui_menu_add_item(lui_menu_t *menu, const char *label,
                      const char *shortcut);

/** Add a separator.  Returns the item index or -1 on failure. */
int lui_menu_add_separator(lui_menu_t *menu);

/** Add a checkbox item.  Returns the item index or -1 on failure. */
int lui_menu_add_checkbox(lui_menu_t *menu, const char *label, bool checked);

/** Set the enabled state of an item. */
void lui_menu_set_enabled(lui_menu_t *menu, int index, bool enabled);

/** Set the checked state of a checkbox item. */
void lui_menu_set_checked(lui_menu_t *menu, int index, bool checked);

/** Show the menu at screen position (@x, @y). */
void lui_menu_popup(lui_menu_t *menu, int x, int y);

/** Close the menu. */
void lui_menu_close(lui_menu_t *menu);

/** Return true if the menu is currently open. */
bool lui_menu_is_open(const lui_menu_t *menu);

/** Get the widget node. */
static inline lui_widget_t *lui_menu_widget(lui_menu_t *menu) {
    return &menu->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_MENU_H */
