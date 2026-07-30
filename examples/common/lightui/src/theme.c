/*
 * theme.c — Widget theme presets and apply helpers
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/theme.h>

/* =========================================================================
 * Flat theme — modern dark, matches existing widget defaults
 * ========================================================================= */

void lui_theme_flat(lui_theme_t *t)
{
    if (!t) return;

    /* General */
    t->window_bg    = LVG_COLOR_RGB(0x35, 0x39, 0x3F);
    t->panel_bg     = LVG_COLOR_RGB(0x30, 0x34, 0x3A);
    t->panel_bg_alt = LVG_COLOR_RGB(0x38, 0x3C, 0x42);

    /* Text */
    t->text     = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    t->text_dim = LVG_COLOR_RGB(0x6C, 0x70, 0x76);

    /* Accent */
    t->accent        = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    t->accent_active = LVG_COLOR_RGB(0x89, 0xB4, 0xFA);

    /* Button */
    t->button_style.face           = LVG_COLOR_RGB(0x65, 0x6A, 0x71);
    t->button_style.face_pressed   = LVG_COLOR_RGB(0x57, 0x6A, 0x7D);
    t->button_style.face_highlight = LVG_COLOR_RGB(0x74, 0x82, 0x92);
    t->button_style.border         = LVG_COLOR_RGB(0x22, 0x25, 0x29);
    t->button_style.edge_light     = LVG_COLOR_RGB(0x91, 0x98, 0xA1);
    t->button_style.edge_shadow    = LVG_COLOR_RGB(0x3B, 0x40, 0x47);
    t->button_style.accent         = LVG_COLOR_RGB(0x88, 0xB2, 0xDA);

    /* Slider */
    t->slider_track        = LVG_COLOR_RGB(0x40, 0x44, 0x4B);
    t->slider_fill         = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    t->slider_thumb        = LVG_COLOR_WHITE;
    t->slider_thumb_active = LVG_COLOR_RGB(0x89, 0xB4, 0xFA);
    t->slider_track_height = 4;
    t->slider_thumb_radius = 7;

    /* Checkbox */
    t->checkbox_bg           = LVG_COLOR_RGB(0x40, 0x44, 0x4B);
    t->checkbox_checked_bg   = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    t->checkbox_border       = LVG_COLOR_RGB(0x6C, 0x70, 0x76);
    t->checkbox_mark         = LVG_COLOR_WHITE;
    t->checkbox_bevel_light  = LVG_COLOR_TRANSPARENT;
    t->checkbox_bevel_shadow = LVG_COLOR_TRANSPARENT;
    t->checkbox_size         = 18;
    t->checkbox_corner_radius = 3;

    /* Text input */
    t->input_bg           = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    t->input_text         = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    t->input_placeholder  = LVG_COLOR_RGB(0x6C, 0x70, 0x76);
    t->input_border       = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    t->input_border_focus = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    t->input_cursor       = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    t->input_bevel_light  = LVG_COLOR_TRANSPARENT;
    t->input_bevel_shadow = LVG_COLOR_TRANSPARENT;
    t->input_cursor_width = 2;
    t->input_corner_radius = 3;

    /* Scroll */
    t->scroll_bg       = LVG_COLOR_TRANSPARENT;
    t->scrollbar_thumb = LVG_COLOR_ARGB(0x80, 0xA0, 0xA0, 0xA0);
    t->scrollbar_width = 8;

    /* Label */
    t->label_color = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    t->label_bg    = LVG_COLOR_TRANSPARENT;
}

/* =========================================================================
 * IRIX 4Dwm / Motif theme — modernised dark variant
 *
 * Retains the square, beveled character of the SGI "Indigo Magic" desktop
 * but re-imagined as a dark theme with:
 *   - Dark charcoal base palette
 *   - Thin, subtle bevels (shallow colour difference)
 *   - Muted SGI purple-blue accent
 *   - Light text on dark backgrounds
 * ========================================================================= */

void lui_theme_4dwm(lui_theme_t *t)
{
    if (!t) return;

    /*
     * Modernised dark 4Dwm palette:
     *   Base:         #3A3A3A  (dark charcoal)
     *   Light bevel:  #484848  (subtle lift, ~14 above base)
     *   Dark bevel:   #2C2C2C  (subtle shadow, ~14 below base)
     *   Border:       #2A2A2A
     *   Text:         #C8C8C8  (soft white)
     *   Accent:       #6868A0  (muted SGI purple-blue)
     *   Input field:  #303030  (slightly darker than base)
     */

    /* General */
    t->window_bg    = LVG_COLOR_RGB(0x3A, 0x3A, 0x3A);
    t->panel_bg     = LVG_COLOR_RGB(0x34, 0x34, 0x34);
    t->panel_bg_alt = LVG_COLOR_RGB(0x3E, 0x3E, 0x3E);

    /* Text — light on dark */
    t->text     = LVG_COLOR_RGB(0xC8, 0xC8, 0xC8);
    t->text_dim = LVG_COLOR_RGB(0x70, 0x70, 0x70);

    /* Accent — muted SGI purple-blue */
    t->accent        = LVG_COLOR_RGB(0x68, 0x68, 0xA0);
    t->accent_active = LVG_COLOR_RGB(0x7C, 0x7C, 0xB4);

    /* Button — thin bevels, shallow colour steps */
    t->button_style.face           = LVG_COLOR_RGB(0x44, 0x44, 0x44);
    t->button_style.face_pressed   = LVG_COLOR_RGB(0x38, 0x38, 0x38);
    t->button_style.face_highlight = LVG_COLOR_RGB(0x58, 0x58, 0x80);
    t->button_style.border         = LVG_COLOR_RGB(0x2A, 0x2A, 0x2A);
    t->button_style.edge_light     = LVG_COLOR_RGB(0x52, 0x52, 0x52);
    t->button_style.edge_shadow    = LVG_COLOR_RGB(0x30, 0x30, 0x30);
    t->button_style.accent         = LVG_COLOR_RGB(0x7C, 0x7C, 0xB4);

    /* Slider */
    t->slider_track        = LVG_COLOR_RGB(0x2E, 0x2E, 0x2E);
    t->slider_fill         = LVG_COLOR_RGB(0x68, 0x68, 0xA0);
    t->slider_thumb        = LVG_COLOR_RGB(0x58, 0x58, 0x58);
    t->slider_thumb_active = LVG_COLOR_RGB(0x68, 0x68, 0x68);
    t->slider_track_height = 4;
    t->slider_thumb_radius = 6;

    /* Checkbox — square, thin sunken bevel */
    t->checkbox_bg           = LVG_COLOR_RGB(0x2E, 0x2E, 0x2E);
    t->checkbox_checked_bg   = LVG_COLOR_RGB(0x68, 0x68, 0xA0);
    t->checkbox_border       = LVG_COLOR_RGB(0x2A, 0x2A, 0x2A);
    t->checkbox_mark         = LVG_COLOR_RGB(0xD8, 0xD8, 0xD8);
    t->checkbox_bevel_light  = LVG_COLOR_RGB(0x48, 0x48, 0x48);
    t->checkbox_bevel_shadow = LVG_COLOR_RGB(0x24, 0x24, 0x24);
    t->checkbox_size         = 16;
    t->checkbox_corner_radius = 0;  /* square */

    /* Text input — sunken field, dark background */
    t->input_bg           = LVG_COLOR_RGB(0x30, 0x30, 0x30);
    t->input_text         = LVG_COLOR_RGB(0xC8, 0xC8, 0xC8);
    t->input_placeholder  = LVG_COLOR_RGB(0x60, 0x60, 0x60);
    t->input_border       = LVG_COLOR_RGB(0x2A, 0x2A, 0x2A);
    t->input_border_focus = LVG_COLOR_RGB(0x68, 0x68, 0xA0);
    t->input_cursor       = LVG_COLOR_RGB(0x68, 0x68, 0xA0);
    t->input_bevel_light  = LVG_COLOR_RGB(0x48, 0x48, 0x48);
    t->input_bevel_shadow = LVG_COLOR_RGB(0x24, 0x24, 0x24);
    t->input_cursor_width = 2;
    t->input_corner_radius = 0;  /* square */

    /* Scroll — slightly wider, subtle thumb */
    t->scroll_bg       = LVG_COLOR_RGB(0x30, 0x30, 0x30);
    t->scrollbar_thumb = LVG_COLOR_RGB(0x50, 0x50, 0x50);
    t->scrollbar_width = 10;

    /* Label — light text on transparent */
    t->label_color = LVG_COLOR_RGB(0xC8, 0xC8, 0xC8);
    t->label_bg    = LVG_COLOR_TRANSPARENT;
}

/* =========================================================================
 * Apply helpers
 * ========================================================================= */

void lui_theme_apply_button_style(lui_button_style_t *style,
                                   const lui_theme_t *theme)
{
    if (!style || !theme) return;
    *style = theme->button_style;
}

void lui_theme_apply_slider(lui_slider_t *s, const lui_theme_t *theme)
{
    if (!s || !theme) return;
    s->track_color      = theme->slider_track;
    s->track_fill_color = theme->slider_fill;
    s->thumb_color      = theme->slider_thumb;
    s->thumb_active     = theme->slider_thumb_active;
    s->track_height     = theme->slider_track_height;
    s->thumb_radius     = theme->slider_thumb_radius;
}

void lui_theme_apply_checkbox(lui_checkbox_t *cb, const lui_theme_t *theme)
{
    if (!cb || !theme) return;
    cb->box_color         = theme->checkbox_bg;
    cb->box_checked_color = theme->checkbox_checked_bg;
    cb->border_color      = theme->checkbox_border;
    cb->check_color       = theme->checkbox_mark;
    cb->bevel_light       = theme->checkbox_bevel_light;
    cb->bevel_shadow      = theme->checkbox_bevel_shadow;
    cb->box_size          = theme->checkbox_size;
    cb->corner_radius     = theme->checkbox_corner_radius;
}

void lui_theme_apply_text_input(lui_text_input_t *ti, const lui_theme_t *theme)
{
    if (!ti || !theme) return;
    ti->text_color        = theme->input_text;
    ti->placeholder_color = theme->input_placeholder;
    ti->bg_color          = theme->input_bg;
    ti->border_color      = theme->input_border;
    ti->border_focus_color = theme->input_border_focus;
    ti->cursor_color      = theme->input_cursor;
    ti->bevel_light       = theme->input_bevel_light;
    ti->bevel_shadow      = theme->input_bevel_shadow;
    ti->cursor_width      = theme->input_cursor_width;
    ti->corner_radius     = theme->input_corner_radius;
}

void lui_theme_apply_scroll(lui_scroll_t *s, const lui_theme_t *theme)
{
    if (!s || !theme) return;
    s->bg              = theme->scroll_bg;
    s->scrollbar_color = theme->scrollbar_thumb;
    s->scrollbar_width = theme->scrollbar_width;
}

void lui_theme_apply_label(lui_label_t *label, const lui_theme_t *theme)
{
    if (!label || !theme) return;
    label->color = theme->label_color;
    label->bg    = theme->label_bg;
}
