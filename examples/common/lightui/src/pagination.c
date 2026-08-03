/*
 * pagination.c — Page navigation control widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/pagination.h>
#include <lightvg/canvas.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define PG_CHAR_W    5
#define PG_CHAR_H   10
#define PG_CHAR_ADV  7

#define PG_PAGE_PREV  (-1)
#define PG_PAGE_NEXT  (-2)
#define PG_PAGE_ELLIP (-3)

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/*
 * Build the array of page numbers to display.
 * Returns the count.  Negative values are special tokens (prev, next, ellipsis).
 */
static int pg_build_items(const lui_pagination_t *pg, int *items, int max_items)
{
    int n = 0;
    int total = pg->total_pages;
    int cur   = pg->current_page;
    int maxb  = pg->max_visible_buttons;

    items[n++] = PG_PAGE_PREV;  /* prev arrow */

    if (total <= maxb) {
        /* Show all pages */
        for (int p = 1; p <= total && n < max_items - 1; p++)
            items[n++] = p;
    } else {
        /* Always show first page */
        items[n++] = 1;

        /* Compute window around current page */
        int wing = (maxb - 3) / 2;   /* pages on each side of current */
        int lo = cur - wing;
        int hi = cur + wing;

        /* Adjust if near edges */
        if (lo <= 2) {
            lo = 2;
            hi = lo + (maxb - 3);
        }
        if (hi >= total) {
            hi = total - 1;
            lo = hi - (maxb - 3);
            if (lo < 2) lo = 2;
        }

        /* Left ellipsis */
        if (lo > 2 && n < max_items - 1)
            items[n++] = PG_PAGE_ELLIP;

        /* Page numbers in window */
        for (int p = lo; p <= hi && n < max_items - 1; p++)
            items[n++] = p;

        /* Right ellipsis */
        if (hi < total - 1 && n < max_items - 1)
            items[n++] = PG_PAGE_ELLIP;

        /* Always show last page */
        if (n < max_items - 1)
            items[n++] = total;
    }

    items[n++] = PG_PAGE_NEXT;  /* next arrow */
    return n;
}

static inline int pg_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int pg_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    const lui_pagination_t *pg = (const lui_pagination_t *)w;
    (void)user;

    /* Estimate: prev + max_visible + next + spacing */
    int count = pg->max_visible_buttons + 4;  /* +arrows +ellipses */
    *out_w = count * (pg->button_size + pg->spacing);
    *out_h = pg->button_size;
    return 0;
}

static void pg_draw_text_centered(lvg_canvas_t *canvas, int bx, int by,
                                    int bw, int bh, const char *text,
                                    lvg_color_t color)
{
    int tlen = (int)strlen(text);
    int tw = tlen * PG_CHAR_ADV;
    int tx = bx + (bw - tw) / 2;
    int ty = by + (bh - PG_CHAR_H) / 2;
    for (int i = 0; i < tlen; i++) {
        lvg_canvas_fill_rect(canvas, tx, ty, PG_CHAR_W, PG_CHAR_H, color);
        tx += PG_CHAR_ADV;
    }
}

static void pg_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_pagination_t *pg = (lui_pagination_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int items[32];
    int count = pg_build_items(pg, items, 32);

    int bs = pg->button_size;
    int sp = pg->spacing;
    int total_w = count * bs + (count - 1) * sp;
    int sx = r.x + (r.width - total_w) / 2;
    int sy = r.y + (r.height - bs) / 2;

    for (int i = 0; i < count; i++) {
        int bx = sx + i * (bs + sp);
        int item = items[i];

        lvg_color_t bg;
        lvg_color_t fg;
        char label[8];

        if (item == PG_PAGE_PREV) {
            bool disabled = (pg->current_page <= 1);
            bg = pg->button_bg;
            fg = disabled ? pg->disabled_color : pg->arrow_color;
            label[0] = '<'; label[1] = '\0';
        } else if (item == PG_PAGE_NEXT) {
            bool disabled = (pg->current_page >= pg->total_pages);
            bg = pg->button_bg;
            fg = disabled ? pg->disabled_color : pg->arrow_color;
            label[0] = '>'; label[1] = '\0';
        } else if (item == PG_PAGE_ELLIP) {
            bg = LVG_COLOR_TRANSPARENT;
            fg = pg->text_color;
            memcpy(label, "...", 4);
        } else {
            /* Page number */
            if (item == pg->current_page) {
                bg = pg->button_active_bg;
                fg = pg->active_text;
            } else if (item == pg->hover_page) {
                bg = pg->button_hover_bg;
                fg = pg->text_color;
            } else {
                bg = pg->button_bg;
                fg = pg->text_color;
            }
            snprintf(label, sizeof(label), "%d", item);
        }

        /* Draw button background */
        if (LVG_COLOR_A(bg) > 0) {
            lvg_canvas_fill_rounded_rect(canvas, bx, sy, bs, bs, 4, bg);
        }

        /* Draw label */
        pg_draw_text_centered(canvas, bx, sy, bs, bs, label, fg);
    }
}

static int pg_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_pagination_t *pg = (lui_pagination_t *)w;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN:
        if (event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            lvg_rect_t r = lui_widget_absolute_rect(w);
            if (!lvg_rect_contains_point(&r,
                    event->data.mouse_button.x,
                    event->data.mouse_button.y))
                return 0;

            /* Determine which button was clicked */
            int items[32];
            int count = pg_build_items(pg, items, 32);
            int bs = pg->button_size;
            int sp = pg->spacing;
            int total_w = count * bs + (count - 1) * sp;
            int sx = r.x + (r.width - total_w) / 2;
            int sy = r.y + (r.height - bs) / 2;
            int mx = event->data.mouse_button.x;
            int my = event->data.mouse_button.y;

            for (int i = 0; i < count; i++) {
                int bx = sx + i * (bs + sp);
                lvg_rect_t btn = lvg_rect_make(bx, sy, bs, bs);
                if (lvg_rect_contains_point(&btn, mx, my)) {
                    int item = items[i];
                    int new_page = pg->current_page;

                    if (item == PG_PAGE_PREV) {
                        if (pg->current_page > 1)
                            new_page = pg->current_page - 1;
                    } else if (item == PG_PAGE_NEXT) {
                        if (pg->current_page < pg->total_pages)
                            new_page = pg->current_page + 1;
                    } else if (item == PG_PAGE_ELLIP) {
                        break;  /* ellipsis not clickable */
                    } else {
                        new_page = item;
                    }

                    if (new_page != pg->current_page) {
                        pg->current_page = new_page;
                        if (pg->on_change)
                            pg->on_change(new_page, pg->on_change_user);
                    }
                    return 1;
                }
            }
        }
        break;

    case LUI_EVENT_MOUSE_MOVE: {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        int items[32];
        int count = pg_build_items(pg, items, 32);
        int bs = pg->button_size;
        int sp = pg->spacing;
        int total_w = count * bs + (count - 1) * sp;
        int sx = r.x + (r.width - total_w) / 2;
        int sy = r.y + (r.height - bs) / 2;
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;

        pg->hover_page = 0;
        for (int i = 0; i < count; i++) {
            int bx = sx + i * (bs + sp);
            lvg_rect_t btn = lvg_rect_make(bx, sy, bs, bs);
            if (lvg_rect_contains_point(&btn, mx, my)) {
                int item = items[i];
                if (item > 0 && item != pg->current_page) {
                    pg->hover_page = item;
                } else if (item == PG_PAGE_PREV) {
                    pg->hover_page = PG_PAGE_PREV;
                } else if (item == PG_PAGE_NEXT) {
                    pg->hover_page = PG_PAGE_NEXT;
                }
                break;
            }
        }
        break;
    }

    default:
        break;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_pagination_init(lui_pagination_t *pg, int total_pages)
{
    if (!pg) return;

    lui_widget_init(&pg->widget);
    pg->widget.width    = lvg_size_hug(200);
    pg->widget.height   = lvg_size_hug(28);
    pg->widget.measure  = pg_measure;
    pg->widget.draw     = pg_draw;
    pg->widget.on_event = pg_event;

    pg->current_page         = 1;
    pg->total_pages          = total_pages > 0 ? total_pages : 1;
    pg->max_visible_buttons  = 7;
    pg->button_size          = 28;
    pg->spacing              = 4;
    pg->hover_page           = 0;

    pg->bg_color          = LVG_COLOR_TRANSPARENT;
    pg->button_bg         = LVG_COLOR_RGB(0x2A, 0x2D, 0x34);
    pg->button_active_bg  = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    pg->button_hover_bg   = LVG_COLOR_RGB(0x3A, 0x3F, 0x4A);
    pg->text_color        = LVG_COLOR_RGB(0xC0, 0xC4, 0xCC);
    pg->active_text       = LVG_COLOR_WHITE;
    pg->arrow_color       = LVG_COLOR_RGB(0xC0, 0xC4, 0xCC);
    pg->disabled_color    = LVG_COLOR_RGB(0x50, 0x54, 0x5C);
    pg->border_color      = LVG_COLOR_RGB(0x40, 0x44, 0x4C);

    pg->on_change      = NULL;
    pg->on_change_user = NULL;
}

void lui_pagination_set_page(lui_pagination_t *pg, int page)
{
    if (!pg) return;
    pg->current_page = pg_clamp(page, 1, pg->total_pages);
}

void lui_pagination_set_total(lui_pagination_t *pg, int total)
{
    if (!pg) return;
    pg->total_pages = total > 0 ? total : 1;
    if (pg->current_page > pg->total_pages)
        pg->current_page = pg->total_pages;
}
