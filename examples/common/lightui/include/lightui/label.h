/*
 * lightui/label.h — Text label widget
 *
 * A label displays a single line of text.  It integrates with the layout
 * framework: it implements the measure callback to report its text extent
 * and the draw callback to render text at its computed position.
 *
 * Requires LUI_HAVE_FONTS (FreeType + HarfBuzz).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_LABEL_H
#define LIGHTUI_LABEL_H

#include "layout.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lui_widget_t  widget;     /* must be first — allows (lui_label_t*)widget */
    const char   *text;       /* UTF-8 text (caller-owned, not copied)       */
    lui_font_t   *font;       /* font context (not owned)                    */
    lvg_color_t   color;      /* text colour                                 */
    lvg_color_t   bg;         /* background colour (TRANSPARENT = none)      */
} lui_label_t;

/**
 * Initialise a label widget.
 * @text must remain valid for the lifetime of the label (not copied).
 */
void lui_label_init(lui_label_t *label,
                    const char *text,
                    lui_font_t *font,
                    lvg_color_t color);

/** Change the displayed text.  Invalidates layout (call lui_layout_compute). */
void lui_label_set_text(lui_label_t *label, const char *text);

/** Change the font.  Invalidates layout. */
void lui_label_set_font(lui_label_t *label, lui_font_t *font);

/** Get the widget node (for adding to the layout tree). */
static inline lui_widget_t *lui_label_widget(lui_label_t *label) {
    return &label->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_LABEL_H */
