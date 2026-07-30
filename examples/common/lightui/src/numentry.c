/*
 * numentry.c — Numeric entry widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/numentry.h>
#include <lightvg/canvas.h>

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define NUMENTRY_BUTTON_WIDTH  28   /* width of each +/− button  */
#define NUMENTRY_CHAR_W         5
#define NUMENTRY_CHAR_H        10
#define NUMENTRY_CHAR_ADV       7

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline double lui__n_clampd(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static lvg_rect_t numentry_minus_button_rect(const lui_numentry_t *n,
                                             const lvg_rect_t *r)
{
    int bw = n->button_width > 0 ? n->button_width : NUMENTRY_BUTTON_WIDTH;
    return lvg_rect_make(r->x, r->y, bw, r->height);
}

static lvg_rect_t numentry_plus_button_rect(const lui_numentry_t *n,
                                            const lvg_rect_t *r)
{
    int bw = n->button_width > 0 ? n->button_width : NUMENTRY_BUTTON_WIDTH;
    return lvg_rect_make(r->x + r->width - bw, r->y, bw, r->height);
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int numentry_measure(const lui_widget_t *w, int *out_w, int *out_h,
                              void *user)
{
    (void)w; (void)user;
    *out_w = 120;
    *out_h = 28;
    return 0;
}

static void numentry_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_numentry_t *n = (lui_numentry_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Overall background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, n->bg_color);

    /* Border */
    lvg_canvas_stroke_rect(canvas, r.x, r.y, r.width, r.height,
                            n->border_color, 1);

    /* Minus button (left) */
    lvg_rect_t mb = numentry_minus_button_rect(n, &r);
    lvg_color_t mb_col = (n->hover_button == 0)
                       ? n->button_hover_color : n->button_color;
    lvg_canvas_fill_rect(canvas, mb.x, mb.y, mb.width, mb.height, mb_col);
    lvg_canvas_stroke_rect(canvas, mb.x, mb.y, mb.width, mb.height,
                            n->border_color, 1);

    /* Draw "−" (horizontal bar) */
    {
        int bar_w = 8;
        int bar_h = 2;
        int bx = mb.x + (mb.width - bar_w) / 2;
        int by = mb.y + (mb.height - bar_h) / 2;
        lvg_canvas_fill_rect(canvas, bx, by, bar_w, bar_h, n->text_color);
    }

    /* Plus button (right) */
    lvg_rect_t pb = numentry_plus_button_rect(n, &r);
    lvg_color_t pb_col = (n->hover_button == 1)
                       ? n->button_hover_color : n->button_color;
    lvg_canvas_fill_rect(canvas, pb.x, pb.y, pb.width, pb.height, pb_col);
    lvg_canvas_stroke_rect(canvas, pb.x, pb.y, pb.width, pb.height,
                            n->border_color, 1);

    /* Draw "+" (horizontal + vertical bars) */
    {
        int bar_w = 8;
        int bar_h = 2;
        int bx = pb.x + (pb.width - bar_w) / 2;
        int by = pb.y + (pb.height - bar_h) / 2;
        lvg_canvas_fill_rect(canvas, bx, by, bar_w, bar_h, n->text_color);
        /* Vertical bar */
        int vx = pb.x + (pb.width - bar_h) / 2;
        int vy = pb.y + (pb.height - bar_w) / 2;
        lvg_canvas_fill_rect(canvas, vx, vy, bar_h, bar_w, n->text_color);
    }

    /* Value text in centre */
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", n->precision, n->value);
    int text_len = (int)strlen(buf);
#ifdef LUI_HAVE_FONTS
    int text_w = n->font ? lui_font_measure_text(n->font, buf, -1)
                         : text_len * NUMENTRY_CHAR_ADV;
#else
    int text_w = text_len * NUMENTRY_CHAR_ADV;
#endif

    /* Centre area (between buttons) */
    int button_w = n->button_width > 0 ? n->button_width : NUMENTRY_BUTTON_WIDTH;
    int centre_x = r.x + button_w;
    int centre_w = r.width - 2 * button_w;
    int tx = centre_x + (centre_w - text_w) / 2;
#ifdef LUI_HAVE_FONTS
    if (n->font) {
        int ty = r.y + lui_font_ascent(n->font) +
                 (r.height - lui_font_line_height(n->font)) / 2;
        lui_canvas_draw_text(canvas, tx, ty, buf, -1, n->font, n->text_color);
    } else {
#endif
        int ty = r.y + (r.height - NUMENTRY_CHAR_H) / 2;
        for (int i = 0; i < text_len && tx < r.x + r.width - button_w; i++) {
            lvg_canvas_fill_rect(canvas, tx, ty,
                                  NUMENTRY_CHAR_W, NUMENTRY_CHAR_H, n->text_color);
            tx += NUMENTRY_CHAR_ADV;
        }
#ifdef LUI_HAVE_FONTS
    }
#endif
}

static int numentry_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_numentry_t *n = (lui_numentry_t *)w;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN:
        if (event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            lvg_rect_t r = lui_widget_absolute_rect(w);
            int mx = event->data.mouse_button.x;
            int my = event->data.mouse_button.y;

            if (!lvg_rect_contains_point(&r, mx, my))
                return 0;

            lvg_rect_t mb = numentry_minus_button_rect(n, &r);
            lvg_rect_t pb = numentry_plus_button_rect(n, &r);

            if (lvg_rect_contains_point(&mb, mx, my)) {
                double new_val = lui__n_clampd(n->value - n->step,
                                                n->min_val, n->max_val);
                if (new_val != n->value) {
                    n->value = new_val;
                    if (n->on_change)
                        n->on_change(n->value, n->on_change_user);
                }
                return 1;
            }

            if (lvg_rect_contains_point(&pb, mx, my)) {
                double new_val = lui__n_clampd(n->value + n->step,
                                                n->min_val, n->max_val);
                if (new_val != n->value) {
                    n->value = new_val;
                    if (n->on_change)
                        n->on_change(n->value, n->on_change_user);
                }
                return 1;
            }

            return 1;  /* consumed (click in centre area) */
        }
        break;

    case LUI_EVENT_MOUSE_MOVE: {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        int old_hover = n->hover_button;

        if (!lvg_rect_contains_point(&r, mx, my)) {
            n->hover_button = -1;
        } else {
            lvg_rect_t mb = numentry_minus_button_rect(n, &r);
            lvg_rect_t pb = numentry_plus_button_rect(n, &r);
            if (lvg_rect_contains_point(&mb, mx, my))
                n->hover_button = 0;
            else if (lvg_rect_contains_point(&pb, mx, my))
                n->hover_button = 1;
            else
                n->hover_button = -1;
        }
        if (n->hover_button != old_hover)
            return 1;
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

void lui_numentry_init(lui_numentry_t *n, double min_val, double max_val,
                       double value, double step)
{
    if (!n) return;

    lui_widget_init(&n->widget);
    n->widget.width   = lvg_size_hug(120);
    n->widget.height  = lvg_size_hug(0);
    n->widget.measure = numentry_measure;
    n->widget.draw    = numentry_draw;
    n->widget.on_event = numentry_event;

    n->min_val   = min_val;
    n->max_val   = max_val;
    n->value     = lui__n_clampd(value, min_val, max_val);
    n->step      = step;
    n->precision = 2;

    n->bg_color           = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    n->text_color         = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    n->border_color       = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    n->button_color       = LVG_COLOR_RGB(0x3A, 0x3D, 0x42);
    n->button_hover_color = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    n->hover_button       = -1;
    n->button_width       = NUMENTRY_BUTTON_WIDTH;
    n->font               = NULL;

    n->on_change      = NULL;
    n->on_change_user = NULL;
}

void lui_numentry_set_value(lui_numentry_t *n, double value)
{
    if (!n) return;
    n->value = lui__n_clampd(value, n->min_val, n->max_val);
}
