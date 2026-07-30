/*
 * lightui/scroll.h — Scrollable container widget
 *
 * A scroll container clips its children to a viewport and allows the user
 * to scroll the content using mouse wheel / trackpad events.
 *
 * Usage:
 *   lui_scroll_t scroll;
 *   lui_scroll_init(&scroll);
 *   scroll.widget.width  = lvg_size_fill(1);
 *   scroll.widget.height = lvg_size_fill(1);
 *
 *   // Add children to scroll.content (not scroll.widget directly)
 *   lui_widget_add_child(&scroll.content, &my_label.widget);
 *
 *   // Add scroll widget to the tree
 *   lui_widget_add_child(&parent, &scroll.widget);
 *
 *   // After lui_layout_compute, call:
 *   lui_scroll_update(&scroll);
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_SCROLL_H
#define LIGHTUI_SCROLL_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lui_widget_t  widget;           /* the viewport (add to tree)           */
    lui_widget_t  content;          /* content pane (add children here)     */
    int           scroll_x;        /* current horizontal scroll offset     */
    int           scroll_y;        /* current vertical scroll offset       */
    int           content_width;   /* total content width (after layout)   */
    int           content_height;  /* total content height (after layout)  */
    int           scroll_speed;    /* pixels per scroll tick (default 32)  */
    lvg_color_t   bg;              /* background colour                    */
    lvg_color_t   scrollbar_color; /* scrollbar thumb colour               */
    int           scrollbar_width; /* scrollbar track width (default 8)    */
} lui_scroll_t;

/**
 * Initialise a scroll container.
 * The viewport uses FILL sizing; the content pane uses HUG (grows to fit).
 */
void lui_scroll_init(lui_scroll_t *s);

/**
 * Call after lui_layout_compute() to sync content dimensions and
 * clamp the scroll position.
 */
void lui_scroll_update(lui_scroll_t *s);

/** Set the scroll position (clamped to valid range). */
void lui_scroll_set(lui_scroll_t *s, int x, int y);

/** Scroll by a delta (positive = scroll down/right). */
void lui_scroll_by(lui_scroll_t *s, int dx, int dy);

/** Get the widget node for adding to the layout tree. */
static inline lui_widget_t *lui_scroll_widget(lui_scroll_t *s) {
    return &s->widget;
}

/** Get the content node for adding children. */
static inline lui_widget_t *lui_scroll_content(lui_scroll_t *s) {
    return &s->content;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_SCROLL_H */
