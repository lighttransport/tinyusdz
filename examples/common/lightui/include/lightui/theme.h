/*
 * lightui/theme.h — Widget theme presets
 *
 * A theme collects all colour and dimensional style properties for every
 * widget type into a single struct.  Apply a theme to individual widgets
 * with the lui_theme_apply_*() helpers, or iterate your widget tree.
 *
 * Built-in presets:
 *   lui_theme_flat()  — modern flat dark theme (current default)
 *   lui_theme_4dwm()  — IRIX 4Dwm / Motif-inspired 3D beveled theme
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_THEME_H
#define LIGHTUI_THEME_H

#include <lightvg/types.h>
#include "button.h"
#include "slider.h"
#include "checkbox.h"
#include "scroll.h"
#include "text_input.h"
#include "label.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* --- General / window --- */
    lvg_color_t window_bg;          /* main window background              */
    lvg_color_t panel_bg;           /* panel / container background        */
    lvg_color_t panel_bg_alt;       /* alternating row background          */

    /* --- Text --- */
    lvg_color_t text;               /* primary text colour                 */
    lvg_color_t text_dim;           /* secondary / placeholder text        */

    /* --- Accent --- */
    lvg_color_t accent;             /* primary accent (focus, selection)    */
    lvg_color_t accent_active;      /* accent hover / active variant       */

    /* --- Button --- */
    lui_button_style_t button_style;

    /* --- Slider --- */
    lvg_color_t slider_track;
    lvg_color_t slider_fill;
    lvg_color_t slider_thumb;
    lvg_color_t slider_thumb_active;
    int         slider_track_height;
    int         slider_thumb_radius;

    /* --- Checkbox --- */
    lvg_color_t checkbox_bg;
    lvg_color_t checkbox_checked_bg;
    lvg_color_t checkbox_border;
    lvg_color_t checkbox_mark;
    lvg_color_t checkbox_bevel_light;
    lvg_color_t checkbox_bevel_shadow;
    int         checkbox_size;
    int         checkbox_corner_radius;

    /* --- Text input --- */
    lvg_color_t input_bg;
    lvg_color_t input_text;
    lvg_color_t input_placeholder;
    lvg_color_t input_border;
    lvg_color_t input_border_focus;
    lvg_color_t input_cursor;
    lvg_color_t input_bevel_light;
    lvg_color_t input_bevel_shadow;
    int         input_cursor_width;
    int         input_corner_radius;

    /* --- Scroll container --- */
    lvg_color_t scroll_bg;
    lvg_color_t scrollbar_thumb;
    int         scrollbar_width;

    /* --- Label --- */
    lvg_color_t label_color;
    lvg_color_t label_bg;
} lui_theme_t;

/* ---- Preset constructors ------------------------------------------------ */

/** Modern flat dark theme (matches the existing default colours). */
void lui_theme_flat(lui_theme_t *theme);

/** IRIX 4Dwm / Motif-inspired theme with 3D beveled widgets. */
void lui_theme_4dwm(lui_theme_t *theme);

/* ---- Apply helpers ------------------------------------------------------ */

/** Apply theme colours to a lui_button_style_t. */
void lui_theme_apply_button_style(lui_button_style_t *style,
                                   const lui_theme_t *theme);

/** Apply theme colours and dimensions to a slider. */
void lui_theme_apply_slider(lui_slider_t *s, const lui_theme_t *theme);

/** Apply theme colours and dimensions to a checkbox. */
void lui_theme_apply_checkbox(lui_checkbox_t *cb, const lui_theme_t *theme);

/** Apply theme colours and dimensions to a text input. */
void lui_theme_apply_text_input(lui_text_input_t *ti, const lui_theme_t *theme);

/** Apply theme colours to a scroll container. */
void lui_theme_apply_scroll(lui_scroll_t *s, const lui_theme_t *theme);

/** Apply theme text colour to a label. */
void lui_theme_apply_label(lui_label_t *label, const lui_theme_t *theme);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_THEME_H */
