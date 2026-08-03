/*
 * lightui/tabs.h — Tab strip widget
 *
 * Horizontal tab bar with closable, reorderable tabs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TABS_H
#define LIGHTUI_TABS_H

#include "layout.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_TABS_MAX       32
#define LUI_TAB_MAX_LABEL  31  /* excl. NUL */

typedef void (*lui_tab_change_fn)(int tab_id, void *user);
typedef void (*lui_tab_close_fn)(int tab_id, void *user);

typedef struct {
    int           id;
    char          label[LUI_TAB_MAX_LABEL + 1];
    lvg_color_t   color;        /* accent color (0 = default)           */
    bool          closable;     /* show close button                    */
} lui_tab_t;

typedef struct {
    lui_widget_t    widget;

    lui_tab_t       tabs[LUI_TABS_MAX];
    int             tab_count;
    int             next_id;
    int             active_id;    /* currently active tab (-1 = none)   */
    int             hovered_id;
    int             close_hovered_id; /* tab whose close btn is hovered */

    /* Appearance */
    int             tab_height;   /* height of tab bar (28)             */
    int             tab_min_width;/* minimum tab width (60)             */
    int             tab_max_width;/* maximum tab width (180)            */
    int             tab_padding;  /* horizontal padding per tab (12)    */
    int             close_size;   /* close button size (12)             */
    int             corner_radius;

    lvg_color_t     bg;
    lvg_color_t     tab_bg;
    lvg_color_t     tab_active_bg;
    lvg_color_t     tab_hover_bg;
    lvg_color_t     text_color;
    lvg_color_t     text_active;
    lvg_color_t     close_color;
    lvg_color_t     close_hover;
    lvg_color_t     border_color;
    lui_font_t     *font;          /* optional label font, not owned      */

    /* Callbacks */
    lui_tab_change_fn on_change;
    void             *on_change_user;
    lui_tab_close_fn  on_close;
    void             *on_close_user;
} lui_tabs_t;

/* ---- API ---------------------------------------------------------------- */

void lui_tabs_init(lui_tabs_t *tabs);
int  lui_tabs_add(lui_tabs_t *tabs, const char *label, bool closable);
void lui_tabs_remove(lui_tabs_t *tabs, int tab_id);
void lui_tabs_set_active(lui_tabs_t *tabs, int tab_id);
int  lui_tabs_get_active(const lui_tabs_t *tabs);
lui_tab_t *lui_tabs_get(lui_tabs_t *tabs, int tab_id);

static inline lui_widget_t *lui_tabs_widget(lui_tabs_t *tabs) {
    return &tabs->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TABS_H */
