/*
 * lightui/statusbar.h — Application status bar widget
 *
 * Horizontal bar divided into sections with text, separated by vertical
 * lines.  Sections may have a fixed width or auto-stretch to fill.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_STATUSBAR_H
#define LIGHTUI_STATUSBAR_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_STATUSBAR_MAX_SECTIONS  8

typedef enum {
    LUI_STATUSBAR_ALIGN_LEFT   = 0,
    LUI_STATUSBAR_ALIGN_CENTER = 1,
    LUI_STATUSBAR_ALIGN_RIGHT  = 2,
} lui_statusbar_align_t;

typedef struct {
    char                   text[128];
    int                    text_len;
    int                    width;       /* 0 = auto-stretch                  */
    lui_statusbar_align_t  alignment;
} lui_statusbar_section_t;

typedef struct {
    lui_widget_t             widget;

    lui_statusbar_section_t  sections[LUI_STATUSBAR_MAX_SECTIONS];
    int                      section_count;

    /* Dimensions */
    int                      bar_height;       /* (22)                       */
    int                      separator_width;  /* (1)                        */

    /* Colors */
    lvg_color_t              bg_color;
    lvg_color_t              text_color;
    lvg_color_t              separator_color;
    lvg_color_t              border_color;
} lui_statusbar_t;

/* ---- API ---------------------------------------------------------------- */

void lui_statusbar_init(lui_statusbar_t *sb);
int  lui_statusbar_add_section(lui_statusbar_t *sb, const char *text, int width);
void lui_statusbar_set_text(lui_statusbar_t *sb, int section, const char *text);

static inline lui_widget_t *lui_statusbar_widget(lui_statusbar_t *sb) {
    return &sb->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_STATUSBAR_H */
