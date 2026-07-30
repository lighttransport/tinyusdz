/*
 * lightui/searchbar.h — Search/filter input with icon and results dropdown
 *
 * Rounded search bar with magnifying glass icon, clear button,
 * and an optional dropdown list of results.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_SEARCHBAR_H
#define LIGHTUI_SEARCHBAR_H

#include "layout.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_SEARCH_MAX_RESULTS  32

/* ---- Callbacks ---------------------------------------------------------- */

typedef void (*lui_searchbar_search_fn)(const char *query, void *user);
typedef void (*lui_searchbar_select_fn)(int index, void *user);

/* ---- Search bar widget -------------------------------------------------- */

typedef struct {
    lui_widget_t  widget;

    /* Query */
    char          query[128];
    int           query_len;
    char          placeholder[64];

    /* State */
    bool          has_focus;
    bool          hovered_clear;     /* mouse over clear button           */

    /* Results */
    char          results[LUI_SEARCH_MAX_RESULTS][64];
    int           result_count;
    int           hovered_result;    /* hovered result index (-1 = none)  */
    bool          show_results;      /* dropdown visible                  */

    /* Colors */
    lvg_color_t   bg_color;
    lvg_color_t   text_color;
    lvg_color_t   placeholder_color;
    lvg_color_t   border_color;
    lvg_color_t   focus_border;
    lvg_color_t   icon_color;
    lvg_color_t   clear_color;
    lvg_color_t   result_bg;
    lvg_color_t   result_hover_bg;
    lvg_color_t   result_text;

    /* Callbacks */
    lui_searchbar_search_fn on_search;
    void                   *on_search_user;
    lui_searchbar_select_fn on_select;
    void                   *on_select_user;
} lui_searchbar_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a search bar widget with default appearance. */
void lui_searchbar_init(lui_searchbar_t *sb);

/** Set the query text programmatically. */
void lui_searchbar_set_query(lui_searchbar_t *sb, const char *text);

/** Clear the query text. */
void lui_searchbar_clear_query(lui_searchbar_t *sb);

/**
 * Add a result to the dropdown list.  Returns the index or -1 on failure.
 * The text is copied internally (max 63 bytes).
 */
int lui_searchbar_add_result(lui_searchbar_t *sb, const char *text);

/** Remove all results. */
void lui_searchbar_clear_results(lui_searchbar_t *sb);

/** Show or hide the results dropdown. */
void lui_searchbar_show_results(lui_searchbar_t *sb, bool show);

/** Get the widget node. */
static inline lui_widget_t *lui_searchbar_widget(lui_searchbar_t *sb) {
    return &sb->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_SEARCHBAR_H */
