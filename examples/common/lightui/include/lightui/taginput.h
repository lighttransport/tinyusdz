/*
 * lightui/taginput.h — Multi-tag entry widget
 *
 * Displays a horizontal flow of tag pills with close buttons.
 * Tags can be added/removed via the API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TAGINPUT_H
#define LIGHTUI_TAGINPUT_H

#include "layout.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_TAGINPUT_MAX  32

/* ---- Tag ---------------------------------------------------------------- */

typedef struct {
    char text[32];
} lui_tag_t;

/* ---- Callbacks ---------------------------------------------------------- */

typedef void (*lui_taginput_add_fn)(const char *tag, void *user);
typedef void (*lui_taginput_remove_fn)(int index, void *user);

/* ---- Tag input widget --------------------------------------------------- */

typedef struct {
    lui_widget_t  widget;

    /* Tags */
    lui_tag_t     tags[LUI_TAGINPUT_MAX];
    int           tag_count;

    /* Input buffer (for display; actual text entry managed by app) */
    char          input_buf[32];
    int           input_len;
    int           cursor;

    /* Layout tracking */
    int           tag_rows;          /* number of rows tags wrap into     */

    /* Interaction state */
    int           hovered_close;     /* index of hovered close button (-1) */

    /* Colors */
    lvg_color_t   bg_color;
    lvg_color_t   tag_bg;
    lvg_color_t   tag_text;
    lvg_color_t   tag_close_color;
    lvg_color_t   input_text;
    lvg_color_t   border_color;
    lvg_color_t   focus_border;

    /* Callbacks */
    lui_taginput_add_fn    on_add;
    void                  *on_add_user;
    lui_taginput_remove_fn on_remove;
    void                  *on_remove_user;
} lui_taginput_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a tag input widget with default appearance. */
void lui_taginput_init(lui_taginput_t *ti);

/**
 * Add a tag.  Returns the tag index or -1 on failure.
 * The text is copied internally (max 31 bytes).
 */
int lui_taginput_add_tag(lui_taginput_t *ti, const char *text);

/** Remove a tag by index. */
void lui_taginput_remove_tag(lui_taginput_t *ti, int index);

/** Remove all tags. */
void lui_taginput_clear(lui_taginput_t *ti);

/** Get the current number of tags. */
int lui_taginput_tag_count(const lui_taginput_t *ti);

/** Get the widget node. */
static inline lui_widget_t *lui_taginput_widget(lui_taginput_t *ti) {
    return &ti->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TAGINPUT_H */
