/*
 * lightui/accordion.h — Collapsible accordion (expandable panels) widget
 *
 * Vertically stacked sections with clickable headers.  Each section can
 * be expanded or collapsed.  Optional exclusive mode ensures only one
 * section is open at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_ACCORDION_H
#define LIGHTUI_ACCORDION_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_ACCORDION_MAX_SECTIONS  16

typedef struct {
    char          title[64];
    int           title_len;
    bool          expanded;
    int           content_height;     /* height of content area in pixels  */
    lui_widget_t *content;            /* optional child widget             */
} lui_accordion_section_t;

typedef struct {
    lui_widget_t              widget;

    lui_accordion_section_t   sections[LUI_ACCORDION_MAX_SECTIONS];
    int                       section_count;

    /* Behaviour */
    int                       header_height;   /* (28)                     */
    bool                      exclusive;       /* only one open at a time  */

    /* Colors */
    lvg_color_t               header_bg;
    lvg_color_t               header_text;
    lvg_color_t               content_bg;
    lvg_color_t               border_color;
    lvg_color_t               arrow_color;
} lui_accordion_t;

/* ---- API ---------------------------------------------------------------- */

void lui_accordion_init(lui_accordion_t *acc);
int  lui_accordion_add_section(lui_accordion_t *acc, const char *title,
                               int content_height);
void lui_accordion_set_expanded(lui_accordion_t *acc, int index, bool expanded);
void lui_accordion_toggle(lui_accordion_t *acc, int index);

static inline lui_widget_t *lui_accordion_widget(lui_accordion_t *acc) {
    return &acc->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_ACCORDION_H */
