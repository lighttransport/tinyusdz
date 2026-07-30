/*
 * lightui/text_input.h — Single-line text input widget
 *
 * A focusable text field with cursor navigation and editing.
 * Requires LUI_HAVE_FONTS for text measurement and rendering.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TEXT_INPUT_H
#define LIGHTUI_TEXT_INPUT_H

#include "layout.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lui_text_change_fn)(const char *text, void *user);
typedef void (*lui_text_submit_fn)(const char *text, void *user);

typedef struct {
    lui_widget_t     widget;
    char            *buf;              /* owned text buffer (NUL-terminated)  */
    int              len;              /* current byte length (excl. NUL)     */
    int              cap;              /* buffer capacity                     */
    int              cursor;           /* cursor byte offset (0..len)         */
    int              selection_anchor; /* selection anchor byte offset (-1 none) */
    bool             selecting;        /* true while mouse-drag selecting     */
    int              max_len;          /* max byte length (0 = no limit)      */
    int              scroll_offset;    /* horizontal pixel scroll             */
    lui_font_t      *font;            /* font context (not owned)            */
    const char      *placeholder;     /* placeholder text (not owned)        */
    lvg_color_t      text_color;
    lvg_color_t      placeholder_color;
    lvg_color_t      bg_color;
    lvg_color_t      border_color;
    lvg_color_t      border_focus_color;
    lvg_color_t      cursor_color;
    lvg_color_t      selection_color;
    lvg_color_t      bevel_light;       /* top-left bevel (TRANSPARENT=none)   */
    lvg_color_t      bevel_shadow;      /* bottom-right bevel (TRANSPARENT=none) */
    int              cursor_width;     /* cursor bar width (default 2)        */
    int              corner_radius;    /* corner rounding (0=square)          */
    bool             focused;          /* true when this widget has focus      */
    lui_text_change_fn on_change;      /* called after text modification       */
    void              *on_change_user;
    lui_text_submit_fn on_submit;      /* called on Enter key                 */
    void              *on_submit_user;
} lui_text_input_t;

/**
 * Initialise a text input widget.
 * Allocates an internal buffer of @initial_cap bytes (minimum 64).
 * @font is required for text measurement/rendering (not owned).
 */
void lui_text_input_init(lui_text_input_t *ti, lui_font_t *font, int initial_cap);

/** Free the internal text buffer. */
void lui_text_input_destroy(lui_text_input_t *ti);

/** Set the text content programmatically. Does not trigger on_change. */
void lui_text_input_set_text(lui_text_input_t *ti, const char *text);

/** Return true when the input currently has selected text. */
bool lui_text_input_has_selection(const lui_text_input_t *ti);

/** Copy the current selection to the text-input clipboard. */
bool lui_text_input_copy(lui_text_input_t *ti);

/** Cut the current selection to the text-input clipboard. */
bool lui_text_input_cut(lui_text_input_t *ti);

/** Paste from the text-input clipboard at the cursor/selection. */
bool lui_text_input_paste(lui_text_input_t *ti);

/** Get the current text (NUL-terminated, valid until next modification). */
static inline const char *lui_text_input_text(const lui_text_input_t *ti) {
    return ti->buf ? ti->buf : "";
}

/** Get the widget node. */
static inline lui_widget_t *lui_text_input_widget(lui_text_input_t *ti) {
    return &ti->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TEXT_INPUT_H */
