/*
 * searchbar.c — Search/filter input with icon and results dropdown
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/searchbar.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define BAR_HEIGHT      32
#define BAR_RADIUS       6
#define ICON_AREA       28  /* width reserved for magnifying glass icon  */
#define CLEAR_AREA      24  /* width reserved for clear button           */
#define RESULT_HEIGHT   28  /* height of each result row                 */

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Draw text as 5x10 character rectangles with 7px advance. */
static void draw_text(lvg_canvas_t *canvas, int x, int y,
                       const char *text, int max_x, lvg_color_t color)
{
    for (int i = 0; text[i] && x + 5 <= max_x; i++) {
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, x, y, 5, 10, color);
        x += 7;
    }
}

/* Draw a magnifying glass icon: small circle + diagonal line. */
static void draw_search_icon(lvg_canvas_t *canvas, int cx, int cy,
                               lvg_color_t color)
{
    lvg_canvas_stroke_circle(canvas, cx, cy, 5, color, 1);
    lvg_canvas_draw_line(canvas, cx + 4, cy + 4, cx + 7, cy + 7, color, 1);
}

/* Draw an "x" clear button. */
static void draw_clear_icon(lvg_canvas_t *canvas, int cx, int cy, int size,
                              lvg_color_t color)
{
    int hs = size / 2;
    lvg_canvas_draw_line(canvas, cx - hs, cy - hs, cx + hs, cy + hs,
                          color, 1);
    lvg_canvas_draw_line(canvas, cx + hs, cy - hs, cx - hs, cy + hs,
                          color, 1);
}

/* Get the clear button rect (absolute coordinates). */
static lvg_rect_t clear_button_rect(const lvg_rect_t *bar)
{
    return lvg_rect_make(bar->x + bar->width - CLEAR_AREA,
                          bar->y + 2,
                          CLEAR_AREA - 2,
                          BAR_HEIGHT - 4);
}

/* Get the results dropdown rect (absolute coordinates). */
static lvg_rect_t dropdown_rect(const lui_searchbar_t *sb,
                                 const lvg_rect_t *bar)
{
    return lvg_rect_make(bar->x, bar->y + BAR_HEIGHT + 2,
                          bar->width,
                          sb->result_count * RESULT_HEIGHT + 2);
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int sb_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)w; (void)user;
    *out_w = 240;
    *out_h = BAR_HEIGHT;
    return 0;
}

static void sb_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_searchbar_t *sb = (lui_searchbar_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Bar background */
    lvg_canvas_fill_rounded_rect(canvas, r.x, r.y, r.width, BAR_HEIGHT,
                                  BAR_RADIUS, sb->bg_color);

    /* Border */
    lvg_color_t border = sb->has_focus ? sb->focus_border : sb->border_color;
    lvg_canvas_stroke_rounded_rect(canvas, r.x, r.y, r.width, BAR_HEIGHT,
                                    BAR_RADIUS, border, 1);

    /* Magnifying glass icon */
    int icon_cx = r.x + ICON_AREA / 2;
    int icon_cy = r.y + BAR_HEIGHT / 2;
    draw_search_icon(canvas, icon_cx, icon_cy, sb->icon_color);

    /* Query text or placeholder */
    int tx = r.x + ICON_AREA;
    int ty = r.y + (BAR_HEIGHT - 10) / 2;
    int text_max = r.x + r.width - CLEAR_AREA - 4;

    if (sb->query_len > 0) {
        draw_text(canvas, tx, ty, sb->query, text_max, sb->text_color);
    } else {
        draw_text(canvas, tx, ty, sb->placeholder, text_max,
                  sb->placeholder_color);
    }

    /* Clear button (only when query non-empty) */
    if (sb->query_len > 0) {
        int clear_cx = r.x + r.width - CLEAR_AREA / 2;
        int clear_cy = r.y + BAR_HEIGHT / 2;
        lvg_color_t cc = sb->hovered_clear
                       ? LVG_COLOR_RGB(0xFF, 0xFF, 0xFF) : sb->clear_color;
        draw_clear_icon(canvas, clear_cx, clear_cy, 8, cc);
    }

    /* Results dropdown */
    if (sb->show_results && sb->result_count > 0) {
        lvg_rect_t dr = dropdown_rect(sb, &r);

        lvg_canvas_fill_rounded_rect(canvas, dr.x, dr.y, dr.width, dr.height,
                                      4, sb->result_bg);
        lvg_canvas_stroke_rounded_rect(canvas, dr.x, dr.y, dr.width, dr.height,
                                        4, sb->border_color, 1);

        for (int i = 0; i < sb->result_count; i++) {
            int iy = dr.y + 1 + i * RESULT_HEIGHT;

            /* Hover highlight */
            if (i == sb->hovered_result) {
                lvg_canvas_fill_rect(canvas, dr.x + 1, iy,
                                      dr.width - 2, RESULT_HEIGHT,
                                      sb->result_hover_bg);
            }

            /* Result text */
            int rtx = dr.x + 8;
            int rty = iy + (RESULT_HEIGHT - 10) / 2;
            draw_text(canvas, rtx, rty, sb->results[i],
                      dr.x + dr.width - 8, sb->result_text);
        }
    }
}

static int sb_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_searchbar_t *sb = (lui_searchbar_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    lvg_rect_t bar = lvg_rect_make(r.x, r.y, r.width, BAR_HEIGHT);

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        sb->hovered_clear = false;
        sb->hovered_result = -1;

        /* Clear button hover */
        if (sb->query_len > 0) {
            lvg_rect_t cr = clear_button_rect(&bar);
            if (lvg_rect_contains_point(&cr, mx, my))
                sb->hovered_clear = true;
        }

        /* Results hover */
        if (sb->show_results && sb->result_count > 0) {
            lvg_rect_t dr = dropdown_rect(sb, &r);
            if (lvg_rect_contains_point(&dr, mx, my)) {
                int rel = (my - dr.y - 1) / RESULT_HEIGHT;
                if (rel >= 0 && rel < sb->result_count)
                    sb->hovered_result = rel;
            }
        }
        return lvg_rect_contains_point(&bar, mx, my) ? 1 : 0;
    }

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        /* Clear button click */
        if (sb->query_len > 0) {
            lvg_rect_t cr = clear_button_rect(&bar);
            if (lvg_rect_contains_point(&cr, mx, my)) {
                lui_searchbar_clear_query(sb);
                if (sb->on_search)
                    sb->on_search(sb->query, sb->on_search_user);
                return 1;
            }
        }

        /* Result click */
        if (sb->show_results && sb->result_count > 0) {
            lvg_rect_t dr = dropdown_rect(sb, &r);
            if (lvg_rect_contains_point(&dr, mx, my)) {
                int rel = (my - dr.y - 1) / RESULT_HEIGHT;
                if (rel >= 0 && rel < sb->result_count) {
                    if (sb->on_select)
                        sb->on_select(rel, sb->on_select_user);
                }
                return 1;
            }
        }

        /* Click on bar gives focus */
        if (lvg_rect_contains_point(&bar, mx, my)) {
            sb->has_focus = true;
            return 1;
        }

        /* Click outside */
        sb->has_focus = false;
        sb->show_results = false;
        return 0;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_searchbar_init(lui_searchbar_t *sb)
{
    if (!sb) return;

    lui_widget_init(&sb->widget);
    sb->widget.width    = lvg_size_hug(240);
    sb->widget.height   = lvg_size_hug(0);
    sb->widget.measure  = sb_measure;
    sb->widget.draw     = sb_draw;
    sb->widget.on_event = sb_event;
    sb->widget.flags    = LUI_WIDGET_FOCUSABLE | LUI_WIDGET_DRAWS_CHILDREN;

    sb->query[0]        = '\0';
    sb->query_len       = 0;
    memcpy(sb->placeholder, "Search...", 10);
    sb->has_focus       = false;
    sb->hovered_clear   = false;

    sb->result_count    = 0;
    sb->hovered_result  = -1;
    sb->show_results    = false;

    sb->bg_color        = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    sb->text_color      = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    sb->placeholder_color = LVG_COLOR_RGB(0x70, 0x74, 0x7A);
    sb->border_color    = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    sb->focus_border    = LVG_COLOR_RGB(0x58, 0x8E, 0xCC);
    sb->icon_color      = LVG_COLOR_RGB(0x90, 0x94, 0x9A);
    sb->clear_color     = LVG_COLOR_RGB(0xA0, 0xA4, 0xAA);
    sb->result_bg       = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    sb->result_hover_bg = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    sb->result_text     = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);

    sb->on_search       = NULL;
    sb->on_search_user  = NULL;
    sb->on_select       = NULL;
    sb->on_select_user  = NULL;
}

void lui_searchbar_set_query(lui_searchbar_t *sb, const char *text)
{
    if (!sb || !text) return;
    int len = (int)strlen(text);
    if (len > 127) len = 127;
    memcpy(sb->query, text, len);
    sb->query[len] = '\0';
    sb->query_len = len;
}

void lui_searchbar_clear_query(lui_searchbar_t *sb)
{
    if (!sb) return;
    sb->query[0]  = '\0';
    sb->query_len = 0;
}

int lui_searchbar_add_result(lui_searchbar_t *sb, const char *text)
{
    if (!sb || !text || sb->result_count >= LUI_SEARCH_MAX_RESULTS)
        return -1;

    int idx = sb->result_count;
    int len = (int)strlen(text);
    if (len > 63) len = 63;
    memcpy(sb->results[idx], text, len);
    sb->results[idx][len] = '\0';
    sb->result_count++;
    return idx;
}

void lui_searchbar_clear_results(lui_searchbar_t *sb)
{
    if (!sb) return;
    sb->result_count   = 0;
    sb->hovered_result = -1;
}

void lui_searchbar_show_results(lui_searchbar_t *sb, bool show)
{
    if (!sb) return;
    sb->show_results = show;
}
