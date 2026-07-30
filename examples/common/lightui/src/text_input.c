/*
 * text_input.c — Single-line text input widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/text_input.h>
#include "utf8_util.h"

#include <stdlib.h>
#include <string.h>

#ifdef LUI_HAVE_FONTS

static char *g_text_input_clipboard = NULL;
static int   g_text_input_clipboard_len = 0;

/* -------------------------------------------------------------------------
 * Internal buffer operations
 * ------------------------------------------------------------------------- */

static void lui__ti_delete_range(lui_text_input_t *ti, int from, int to);
static void lui__ti_scroll_to_cursor(lui_text_input_t *ti);

static int lui__ti_ensure_cap(lui_text_input_t *ti, int needed)
{
    if (needed + 1 <= ti->cap) return 1;
    int new_cap = ti->cap * 2;
    if (new_cap < needed + 1) new_cap = needed + 1;
    char *new_buf = (char *)realloc(ti->buf, new_cap);
    if (!new_buf) return 0;
    ti->buf = new_buf;
    ti->cap = new_cap;
    return 1;
}

static void lui__ti_clear_selection(lui_text_input_t *ti)
{
    ti->selection_anchor = -1;
}

static int lui__ti_has_selection(const lui_text_input_t *ti)
{
    return ti->selection_anchor >= 0 && ti->selection_anchor != ti->cursor;
}

static void lui__ti_selection_range(const lui_text_input_t *ti,
                                    int *out_a, int *out_b)
{
    int a = ti->selection_anchor;
    int b = ti->cursor;
    if (a < 0) a = b;
    if (a > b) {
        int tmp = a;
        a = b;
        b = tmp;
    }
    *out_a = a;
    *out_b = b;
}

static int lui__ti_delete_selection(lui_text_input_t *ti)
{
    if (!lui__ti_has_selection(ti))
        return 0;

    int a, b;
    lui__ti_selection_range(ti, &a, &b);
    lui__ti_delete_range(ti, a, b);
    ti->cursor = a;
    lui__ti_clear_selection(ti);
    return 1;
}

static void lui__ti_insert(lui_text_input_t *ti, const char *text, int text_len)
{
    if (text_len <= 0) return;
    if (lui__ti_has_selection(ti))
        lui__ti_delete_selection(ti);
    if (ti->max_len > 0 && ti->len + text_len > ti->max_len) return;
    if (!lui__ti_ensure_cap(ti, ti->len + text_len)) return;

    /* Shift tail right */
    memmove(ti->buf + ti->cursor + text_len,
            ti->buf + ti->cursor,
            ti->len - ti->cursor);
    memcpy(ti->buf + ti->cursor, text, text_len);
    ti->len += text_len;
    ti->cursor += text_len;
    ti->buf[ti->len] = '\0';
    lui__ti_clear_selection(ti);

    if (ti->on_change)
        ti->on_change(ti->buf, ti->on_change_user);
}

static void lui__ti_delete_range(lui_text_input_t *ti, int from, int to)
{
    if (from >= to || from < 0 || to > ti->len) return;
    int del_len = to - from;
    memmove(ti->buf + from, ti->buf + to, ti->len - to);
    ti->len -= del_len;
    ti->buf[ti->len] = '\0';
    if (ti->cursor > to)
        ti->cursor -= del_len;
    else if (ti->cursor > from)
        ti->cursor = from;

    if (ti->on_change)
        ti->on_change(ti->buf, ti->on_change_user);
}

static int lui__ti_cursor_from_x(lui_text_input_t *ti, int x, const lvg_rect_t *r)
{
    int click_x = x - (r->x + ti->widget.padding.left) + ti->scroll_offset;
    int best = 0;
    int best_dist = click_x < 0 ? -click_x : click_x;
    int pos = 0;

    while (pos < ti->len) {
        int cp_len = lui__utf8_cp_len(ti->buf, pos, ti->len);
        pos += cp_len;
        int px = lui_font_measure_text(ti->font, ti->buf, pos);
        int dist = px - click_x;
        if (dist < 0) dist = -dist;
        if (dist < best_dist) {
            best_dist = dist;
            best = pos;
        }
    }
    return best;
}

static void lui__ti_move_cursor(lui_text_input_t *ti, int new_cursor,
                                bool extend_selection)
{
    if (new_cursor < 0) new_cursor = 0;
    if (new_cursor > ti->len) new_cursor = ti->len;

    if (extend_selection) {
        if (ti->selection_anchor < 0)
            ti->selection_anchor = ti->cursor;
    } else {
        lui__ti_clear_selection(ti);
    }
    ti->cursor = new_cursor;
}

static bool lui__ti_clipboard_set(const char *text, int len)
{
    if (len < 0)
        len = text ? (int)strlen(text) : 0;

    char *copy = (char *)malloc((size_t)len + 1);
    if (!copy)
        return false;

    if (len > 0 && text)
        memcpy(copy, text, (size_t)len);
    copy[len] = '\0';

    free(g_text_input_clipboard);
    g_text_input_clipboard = copy;
    g_text_input_clipboard_len = len;
    return true;
}

static bool lui__ti_copy_selection(lui_text_input_t *ti)
{
    if (!ti || !lui__ti_has_selection(ti))
        return false;

    int a, b;
    lui__ti_selection_range(ti, &a, &b);
    return lui__ti_clipboard_set(ti->buf + a, b - a);
}

static bool lui__ti_cut_selection(lui_text_input_t *ti)
{
    if (!lui__ti_copy_selection(ti))
        return false;

    lui__ti_delete_selection(ti);
    lui__ti_scroll_to_cursor(ti);
    return true;
}

static bool lui__ti_paste_clipboard(lui_text_input_t *ti)
{
    if (!ti || !g_text_input_clipboard || g_text_input_clipboard_len <= 0)
        return false;

    lui__ti_insert(ti, g_text_input_clipboard, g_text_input_clipboard_len);
    lui__ti_scroll_to_cursor(ti);
    return true;
}

/* -------------------------------------------------------------------------
 * Scroll management — keep cursor visible
 * ------------------------------------------------------------------------- */

static void lui__ti_scroll_to_cursor(lui_text_input_t *ti)
{
    if (!ti->font) return;
    int cursor_px = lui_font_measure_text(ti->font, ti->buf, ti->cursor);
    int visible_w = ti->widget.computed.width
                    - ti->widget.padding.left - ti->widget.padding.right;
    if (visible_w <= 0) visible_w = 1;

    if (cursor_px - ti->scroll_offset > visible_w)
        ti->scroll_offset = cursor_px - visible_w;
    if (cursor_px - ti->scroll_offset < 0)
        ti->scroll_offset = cursor_px;
    if (ti->scroll_offset < 0)
        ti->scroll_offset = 0;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int ti_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    const lui_text_input_t *ti = (const lui_text_input_t *)w;
    (void)user;
    *out_w = 200;  /* default preferred width */
    *out_h = ti->font ? lui_font_line_height(ti->font) + 8 : 24;
    return 0;
}

static void ti_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_text_input_t *ti = (lui_text_input_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r) || !ti->font) return;

    int cr = ti->corner_radius;

    /* Background */
    lvg_canvas_fill_rounded_rect(canvas, r.x, r.y, r.width, r.height, cr,
                                  ti->bg_color);

    /* 3D bevels (if set) — sunken look for 4Dwm style */
    if (LVG_COLOR_A(ti->bevel_light) > 0 && r.width > 2 && r.height > 2) {
        /* Bottom-right = light */
        lvg_canvas_fill_rect(canvas, r.x, r.y + r.height - 1, r.width, 1,
                              ti->bevel_light);
        lvg_canvas_fill_rect(canvas, r.x + r.width - 1, r.y, 1, r.height,
                              ti->bevel_light);
        /* Top-left = shadow (sunken) */
        lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, 1, ti->bevel_shadow);
        lvg_canvas_fill_rect(canvas, r.x, r.y, 1, r.height, ti->bevel_shadow);
    }

    /* Border */
    lvg_color_t border = ti->focused ? ti->border_focus_color : ti->border_color;
    lvg_canvas_stroke_rounded_rect(canvas, r.x, r.y, r.width, r.height, cr,
                                    border, 1);

    /* Text area (inset by padding) */
    int text_x = r.x + w->padding.left;
    int text_y = r.y + w->padding.top;
    int text_area_w = r.width - w->padding.left - w->padding.right;
    int baseline_y = text_y + lui_font_ascent(ti->font);

    /* Clip text to the text area */
    lvg_rect_t text_clip = lvg_rect_make(text_x, r.y, text_area_w, r.height);
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t clipped = lvg_rect_intersect(&old_clip, &text_clip);
    canvas->_clip = clipped;

    if (ti->len > 0) {
        if (lui__ti_has_selection(ti)) {
            int a, b;
            lui__ti_selection_range(ti, &a, &b);
            int sx0 = text_x + lui_font_measure_text(ti->font, ti->buf, a)
                      - ti->scroll_offset;
            int sx1 = text_x + lui_font_measure_text(ti->font, ti->buf, b)
                      - ti->scroll_offset;
            int sy = text_y + 1;
            int sh = lui_font_line_height(ti->font) - 2;
            if (sx1 > sx0)
                lvg_canvas_fill_rect(canvas, sx0, sy, sx1 - sx0, sh,
                                      ti->selection_color);
        }
        lui_canvas_draw_text(canvas, text_x - ti->scroll_offset, baseline_y,
                              ti->buf, ti->len, ti->font, ti->text_color);
    } else if (ti->placeholder && !ti->focused) {
        lui_canvas_draw_text(canvas, text_x, baseline_y,
                              ti->placeholder, -1, ti->font,
                              ti->placeholder_color);
    }

    /* Cursor */
    if (ti->focused) {
        int cursor_px = lui_font_measure_text(ti->font, ti->buf, ti->cursor);
        int cx = text_x + cursor_px - ti->scroll_offset;
        int cy = text_y + 1;
        int ch = lui_font_line_height(ti->font) - 2;
        lvg_canvas_fill_rect(canvas, cx, cy, ti->cursor_width, ch,
                              ti->cursor_color);
    }

    canvas->_clip = old_clip;
}

static int ti_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_text_input_t *ti = (lui_text_input_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        if (lvg_rect_contains_point(&r,
                event->data.mouse_button.x,
                event->data.mouse_button.y)) {
            ti->focused = true;

            /* Place cursor at click position */
            int pos = ti->font ? lui__ti_cursor_from_x(ti,
                         event->data.mouse_button.x, &r) : ti->len;

            if (event->data.mouse_button.button == LUI_MOUSE_RIGHT) {
                int a, b;
                bool keep_selection = false;
                if (lui__ti_has_selection(ti)) {
                    lui__ti_selection_range(ti, &a, &b);
                    keep_selection = pos >= a && pos <= b;
                }
                if (!keep_selection)
                    lui__ti_move_cursor(ti, pos, false);
                ti->selecting = false;
            } else if (event->data.mouse_button.button != LUI_MOUSE_LEFT) {
                ti->selecting = false;
            } else if (event->data.mouse_button.clicks >= 2) {
                ti->selection_anchor = 0;
                ti->cursor = ti->len;
                ti->selecting = false;
            } else {
                ti->selecting = true;
                lui__ti_move_cursor(ti, pos, false);
                ti->selection_anchor = pos;
            }
            return 1;
        } else {
            ti->focused = false;
            ti->selecting = false;
            lui__ti_clear_selection(ti);
        }
        return 0;
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE && ti->selecting) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        if (ti->selection_anchor < 0)
            ti->selection_anchor = ti->cursor;
        ti->cursor = ti->font
            ? lui__ti_cursor_from_x(ti, event->data.mouse_move.x, &r)
            : ti->len;
        lui__ti_scroll_to_cursor(ti);
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_UP && ti->selecting) {
        ti->selecting = false;
        return 1;
    }

    if (!ti->focused) return 0;

    if (event->type == LUI_EVENT_TEXT_INPUT) {
        int input_len = (int)strlen(event->data.text.text);
        if (input_len > 0) {
            lui__ti_insert(ti, event->data.text.text, input_len);
            lui__ti_scroll_to_cursor(ti);
        }
        return 1;
    }

    if (event->type == LUI_EVENT_KEY_DOWN) {
        bool shift = (event->data.key.mods & LUI_MOD_SHIFT) != 0;
        bool ctrl = (event->data.key.mods & LUI_MOD_CTRL) != 0;
        int key = event->data.key.key;

        if (ctrl && (key == 'c' || key == 'C')) {
            lui__ti_copy_selection(ti);
            return 1;
        }
        if (ctrl && (key == 'x' || key == 'X')) {
            lui__ti_cut_selection(ti);
            return 1;
        }
        if ((ctrl && (key == 'v' || key == 'V')) ||
            (shift && key == LUI_KEY_INSERT)) {
            lui__ti_paste_clipboard(ti);
            return 1;
        }

        switch (event->data.key.key) {
        case LUI_KEY_BACKSPACE:
            if (lui__ti_delete_selection(ti)) {
                lui__ti_scroll_to_cursor(ti);
            } else if (ti->cursor > 0) {
                int prev = lui__utf8_prev(ti->buf, ti->cursor);
                lui__ti_delete_range(ti, prev, ti->cursor);
                lui__ti_clear_selection(ti);
                lui__ti_scroll_to_cursor(ti);
            }
            return 1;

        case LUI_KEY_DELETE:
            if (lui__ti_delete_selection(ti)) {
                lui__ti_scroll_to_cursor(ti);
            } else if (ti->cursor < ti->len) {
                int cp_len = lui__utf8_cp_len(ti->buf, ti->cursor, ti->len);
                lui__ti_delete_range(ti, ti->cursor, ti->cursor + cp_len);
                lui__ti_clear_selection(ti);
            }
            return 1;

        case LUI_KEY_LEFT:
            if (ti->cursor > 0) {
                lui__ti_move_cursor(ti, lui__utf8_prev(ti->buf, ti->cursor),
                                    shift);
                lui__ti_scroll_to_cursor(ti);
            }
            return 1;

        case LUI_KEY_RIGHT:
            if (ti->cursor < ti->len) {
                int next = ti->cursor +
                           lui__utf8_cp_len(ti->buf, ti->cursor, ti->len);
                lui__ti_move_cursor(ti, next, shift);
                lui__ti_scroll_to_cursor(ti);
            }
            return 1;

        case LUI_KEY_HOME:
            lui__ti_move_cursor(ti, 0, shift);
            lui__ti_scroll_to_cursor(ti);
            return 1;

        case LUI_KEY_END:
            lui__ti_move_cursor(ti, ti->len, shift);
            lui__ti_scroll_to_cursor(ti);
            return 1;

        case LUI_KEY_RETURN:
            if (ti->on_submit)
                ti->on_submit(ti->buf, ti->on_submit_user);
            return 1;

        default:
            break;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_text_input_init(lui_text_input_t *ti, lui_font_t *font, int initial_cap)
{
    if (!ti) return;

    lui_widget_init(&ti->widget);
    ti->widget.width   = lvg_size_hug(200);
    ti->widget.height  = lvg_size_hug(0);
    ti->widget.padding = lui_edges_xy(6, 4);
    ti->widget.measure = ti_measure;
    ti->widget.draw    = ti_draw;
    ti->widget.on_event = ti_event;
    ti->widget.flags   = LUI_WIDGET_FOCUSABLE;

    if (initial_cap < 64) initial_cap = 64;
    ti->buf = (char *)calloc(initial_cap, 1);
    ti->len = 0;
    ti->cap = ti->buf ? initial_cap : 0;
    ti->cursor = 0;
    ti->selection_anchor = -1;
    ti->selecting = false;
    ti->max_len = 0;
    ti->scroll_offset = 0;
    ti->font = font;
    ti->placeholder = NULL;
    ti->focused = false;
    ti->cursor_width = 2;

    ti->text_color        = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    ti->placeholder_color = LVG_COLOR_RGB(0x6C, 0x70, 0x76);
    ti->bg_color          = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    ti->border_color      = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    ti->border_focus_color = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    ti->cursor_color      = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    ti->selection_color   = LVG_COLOR_ARGB(0x80, 0x58, 0x9C, 0xE0);
    ti->bevel_light       = LVG_COLOR_TRANSPARENT;
    ti->bevel_shadow      = LVG_COLOR_TRANSPARENT;
    ti->corner_radius     = 3;

    ti->on_change      = NULL;
    ti->on_change_user = NULL;
    ti->on_submit      = NULL;
    ti->on_submit_user = NULL;
}

void lui_text_input_destroy(lui_text_input_t *ti)
{
    if (!ti) return;
    free(ti->buf);
    ti->buf = NULL;
    ti->len = 0;
    ti->cap = 0;
}

void lui_text_input_set_text(lui_text_input_t *ti, const char *text)
{
    if (!ti || !ti->buf) return;
    int new_len = text ? (int)strlen(text) : 0;
    if (ti->max_len > 0 && new_len > ti->max_len)
        new_len = ti->max_len;
    if (!lui__ti_ensure_cap(ti, new_len)) return;
    if (new_len > 0)
        memcpy(ti->buf, text, new_len);
    ti->buf[new_len] = '\0';
    ti->len = new_len;
    if (ti->cursor > ti->len)
        ti->cursor = ti->len;
    lui__ti_clear_selection(ti);
    ti->selecting = false;
    ti->scroll_offset = 0;
}

bool lui_text_input_has_selection(const lui_text_input_t *ti)
{
    return ti && lui__ti_has_selection(ti);
}

bool lui_text_input_copy(lui_text_input_t *ti)
{
    return lui__ti_copy_selection(ti);
}

bool lui_text_input_cut(lui_text_input_t *ti)
{
    return lui__ti_cut_selection(ti);
}

bool lui_text_input_paste(lui_text_input_t *ti)
{
    return lui__ti_paste_clipboard(ti);
}

#else /* !LUI_HAVE_FONTS */

void lui_text_input_init(lui_text_input_t *ti, lui_font_t *font, int initial_cap)
{
    (void)ti; (void)font; (void)initial_cap;
}
void lui_text_input_destroy(lui_text_input_t *ti) { (void)ti; }
void lui_text_input_set_text(lui_text_input_t *ti, const char *text)
{
    (void)ti; (void)text;
}
bool lui_text_input_has_selection(const lui_text_input_t *ti)
{
    (void)ti;
    return false;
}
bool lui_text_input_copy(lui_text_input_t *ti)
{
    (void)ti;
    return false;
}
bool lui_text_input_cut(lui_text_input_t *ti)
{
    (void)ti;
    return false;
}
bool lui_text_input_paste(lui_text_input_t *ti)
{
    (void)ti;
    return false;
}

#endif /* LUI_HAVE_FONTS */
