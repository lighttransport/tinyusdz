/*
 * lightui/breadcrumb.h — Navigation breadcrumb path display
 *
 * A horizontal row of clickable path segments separated by a configurable
 * separator character.  Clicking a segment navigates to that depth level.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_BREADCRUMB_H
#define LIGHTUI_BREADCRUMB_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_BREADCRUMB_MAX  16

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_breadcrumb_click_fn)(int index, void *user);

/* ---- Breadcrumb segment ------------------------------------------------- */

typedef struct {
    char label[64];
} lui_breadcrumb_segment_t;

/* ---- Breadcrumb widget -------------------------------------------------- */

typedef struct {
    lui_widget_t              widget;

    /* Segments */
    lui_breadcrumb_segment_t  segments[LUI_BREADCRUMB_MAX];
    int                       count;

    /* Appearance */
    char                      separator[4];   /* ">" or "/" etc.             */
    int                       hovered;        /* hovered segment (-1 = none) */
    lvg_color_t               text_color;
    lvg_color_t               hover_color;
    lvg_color_t               separator_color;
    lvg_color_t               bg_color;

    /* Callback */
    lui_breadcrumb_click_fn   on_click;
    void                     *on_click_user;
} lui_breadcrumb_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a breadcrumb widget with default appearance. */
void lui_breadcrumb_init(lui_breadcrumb_t *bc);

/** Push a new segment at the end.  Returns 0 on success, -1 if full. */
int lui_breadcrumb_push(lui_breadcrumb_t *bc, const char *label);

/** Remove the last segment.  Returns 0 on success, -1 if empty. */
int lui_breadcrumb_pop(lui_breadcrumb_t *bc);

/** Remove all segments. */
void lui_breadcrumb_clear(lui_breadcrumb_t *bc);

/** Replace the entire path from an array of labels. */
void lui_breadcrumb_set_path(lui_breadcrumb_t *bc,
                              const char *const *labels, int count);

/** Get the widget node. */
static inline lui_widget_t *lui_breadcrumb_widget(lui_breadcrumb_t *bc) {
    return &bc->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_BREADCRUMB_H */
