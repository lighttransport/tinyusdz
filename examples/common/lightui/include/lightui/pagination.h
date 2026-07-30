/*
 * lightui/pagination.h — Page navigation control widget
 *
 * Displays prev/next arrows and page number buttons with ellipsis
 * for navigating paged content.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_PAGINATION_H
#define LIGHTUI_PAGINATION_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Types -------------------------------------------------------------- */

typedef void (*lui_pagination_change_fn)(int page, void *user);

/* ---- Pagination widget -------------------------------------------------- */

typedef struct {
    lui_widget_t widget;

    int           current_page;          /* 1-based                           */
    int           total_pages;
    int           max_visible_buttons;   /* default 7                         */

    /* Dimensions */
    int           button_size;           /* default 28                        */
    int           spacing;               /* default 4                         */

    /* Interaction state */
    int           hover_page;            /* 0 = none, -1 = prev, -2 = next   */

    /* Appearance */
    lvg_color_t   bg_color;
    lvg_color_t   button_bg;
    lvg_color_t   button_active_bg;
    lvg_color_t   button_hover_bg;
    lvg_color_t   text_color;
    lvg_color_t   active_text;
    lvg_color_t   arrow_color;
    lvg_color_t   disabled_color;
    lvg_color_t   border_color;

    /* Callback */
    lui_pagination_change_fn on_change;
    void                    *on_change_user;
} lui_pagination_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a pagination widget with the given total number of pages. */
void lui_pagination_init(lui_pagination_t *pg, int total_pages);

/** Set the current page (1-based, clamped). */
void lui_pagination_set_page(lui_pagination_t *pg, int page);

/** Set the total number of pages (resets to page 1 if current > total). */
void lui_pagination_set_total(lui_pagination_t *pg, int total);

/** Get the widget node. */
static inline lui_widget_t *lui_pagination_widget(lui_pagination_t *pg) {
    return &pg->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_PAGINATION_H */
