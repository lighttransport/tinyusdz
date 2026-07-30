/*
 * lightui/tooltip.h — Tooltip popup widget
 *
 * Delayed popup with text that appears on hover.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TOOLTIP_H
#define LIGHTUI_TOOLTIP_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_TOOLTIP_MAX_TEXT  127  /* excl. NUL */

typedef struct {
    lui_widget_t  widget;

    /* Content */
    char          text[LUI_TOOLTIP_MAX_TEXT + 1];
    int           text_len;

    /* State */
    bool          visible;       /* tooltip is currently shown           */
    float         hover_time;    /* accumulated hover time (seconds)     */
    float         delay;         /* delay before showing (default 0.5s)  */
    int           target_x;     /* position to show at                   */
    int           target_y;

    /* Appearance */
    int           padding;       /* internal padding (6)                 */
    int           corner_radius; /* (4)                                  */
    int           offset_y;      /* vertical offset from target (16)     */
    lvg_color_t   bg;
    lvg_color_t   text_color;
    lvg_color_t   border_color;
} lui_tooltip_t;

/* ---- API ---------------------------------------------------------------- */

void lui_tooltip_init(lui_tooltip_t *tt);
void lui_tooltip_set_text(lui_tooltip_t *tt, const char *text);
void lui_tooltip_show(lui_tooltip_t *tt, int x, int y);
void lui_tooltip_hide(lui_tooltip_t *tt);

/** Call each frame with dt. If hovering, advances timer and shows. */
bool lui_tooltip_update(lui_tooltip_t *tt, float dt, bool hovering,
                         int mx, int my);

static inline lui_widget_t *lui_tooltip_widget(lui_tooltip_t *tt) {
    return &tt->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TOOLTIP_H */
