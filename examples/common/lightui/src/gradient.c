/*
 * gradient.c — Gradient editor widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/gradient.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Sort stops by position (insertion sort — max 32 elements). */
static void sort_stops(lui_gradient_t *g)
{
    for (int i = 1; i < g->stop_count; i++) {
        lui_gradient_stop_t tmp = g->stops[i];
        int j = i - 1;
        while (j >= 0 && g->stops[j].position > tmp.position) {
            g->stops[j + 1] = g->stops[j];
            j--;
        }
        g->stops[j + 1] = tmp;
    }
}

/* Color lerp now lives in <lightvg/types.h> as lvg_color_lerp. */

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int grad_measure(const lui_widget_t *w, int *out_w, int *out_h,
                          void *user)
{
    const lui_gradient_t *g = (const lui_gradient_t *)w;
    (void)user;
    *out_w = 200;
    *out_h = g->bar_height + g->stop_size + 4;
    return 0;
}

static void grad_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_gradient_t *g = (lui_gradient_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int bar_y = r.y;
    int bar_h = g->bar_height;
    int bar_w = r.width;

    /* Checker pattern under gradient (for transparency preview) */
    int check_sz = 6;
    for (int cy = bar_y; cy < bar_y + bar_h; cy += check_sz) {
        for (int cx = r.x; cx < r.x + bar_w; cx += check_sz) {
            bool dark = ((cx / check_sz) + (cy / check_sz)) & 1;
            int cw = check_sz; if (cx + cw > r.x + bar_w) cw = r.x + bar_w - cx;
            int ch = check_sz; if (cy + ch > bar_y + bar_h) ch = bar_y + bar_h - cy;
            lvg_canvas_fill_rect(canvas, cx, cy, cw, ch,
                                  dark ? g->checker_b : g->checker_a);
        }
    }

    /* Draw gradient bar column by column */
    for (int x = 0; x < bar_w; x++) {
        float t = (float)x / (float)(bar_w > 1 ? bar_w - 1 : 1);
        lvg_color_t c = lui_gradient_evaluate(g, t);
        lvg_canvas_fill_rect(canvas, r.x + x, bar_y, 1, bar_h, c);
    }

    /* Border */
    lvg_canvas_stroke_rect(canvas, r.x, bar_y, bar_w, bar_h,
                            g->border_color, 1);

    /* Stop handles */
    int handle_y = bar_y + bar_h + 2;
    int hs = g->stop_size;
    for (int i = 0; i < g->stop_count; i++) {
        int hx = r.x + (int)(g->stops[i].position * (bar_w - 1)) - hs / 2;
        lvg_canvas_fill_rect(canvas, hx, handle_y, hs, hs,
                              g->stops[i].color);
        lvg_color_t bdr = (i == g->selected_stop)
                        ? g->selected_border : g->stop_border;
        lvg_canvas_stroke_rect(canvas, hx, handle_y, hs, hs, bdr, 1);
    }
}

static int grad_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_gradient_t *g = (lui_gradient_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        int handle_y = r.y + g->bar_height + 2;
        int hs = g->stop_size;

        /* Check stop handles */
        for (int i = 0; i < g->stop_count; i++) {
            int hx = r.x + (int)(g->stops[i].position * (r.width - 1)) - hs / 2;
            if (mx >= hx && mx < hx + hs && my >= handle_y && my < handle_y + hs) {
                g->selected_stop = i;
                g->dragging = true;
                g->drag_stop = i;
                return 1;
            }
        }

        /* Click on bar: add new stop */
        if (my >= r.y && my < r.y + g->bar_height &&
            mx >= r.x && mx < r.x + r.width) {
            float t = (float)(mx - r.x) / (float)(r.width > 1 ? r.width - 1 : 1);
            lvg_color_t c = lui_gradient_evaluate(g, t);
            int idx = lui_gradient_add_stop(g, t, c);
            if (idx >= 0) {
                g->selected_stop = idx;
                if (g->on_change) g->on_change(g->on_change_user);
            }
            return 1;
        }
        return 0;
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE && g->dragging) {
        int mx = event->data.mouse_move.x;
        float t = (float)(mx - r.x) / (float)(r.width > 1 ? r.width - 1 : 1);
        g->stops[g->drag_stop].position = clampf(t, 0.0f, 1.0f);
        sort_stops(g);
        /* Update drag_stop index after sort */
        for (int i = 0; i < g->stop_count; i++) {
            if (i == g->selected_stop) { g->drag_stop = i; break; }
        }
        if (g->on_change) g->on_change(g->on_change_user);
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_UP) {
        g->dragging = false;
        return 0;
    }

    /* Delete selected stop with Delete/Backspace */
    if (event->type == LUI_EVENT_KEY_DOWN &&
        (event->data.key.key == LUI_KEY_DELETE ||
         event->data.key.key == LUI_KEY_BACKSPACE)) {
        if (g->selected_stop >= 0 && g->stop_count > 2) {
            lui_gradient_remove_stop(g, g->selected_stop);
            g->selected_stop = -1;
            if (g->on_change) g->on_change(g->on_change_user);
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_gradient_init(lui_gradient_t *g)
{
    if (!g) return;

    lui_widget_init(&g->widget);
    g->widget.width   = lvg_size_fill(1);
    g->widget.height  = lvg_size_hug(0);
    g->widget.measure = grad_measure;
    g->widget.draw    = grad_draw;
    g->widget.on_event = grad_event;
    g->widget.flags   = LUI_WIDGET_FOCUSABLE;

    g->stop_count    = 0;
    g->selected_stop = -1;
    g->dragging      = false;
    g->drag_stop     = -1;
    g->bar_height    = 24;
    g->stop_size     = 10;
    g->corner_radius = 0;

    g->bg              = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    g->border_color    = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    g->stop_border     = LVG_COLOR_RGB(0x80, 0x84, 0x8A);
    g->selected_border = LVG_COLOR_RGB(0xFF, 0xFF, 0x00);
    g->checker_a       = LVG_COLOR_RGB(0x40, 0x40, 0x40);
    g->checker_b       = LVG_COLOR_RGB(0x30, 0x30, 0x30);

    g->on_change      = NULL;
    g->on_change_user = NULL;

    /* Default: black to white gradient */
    lui_gradient_add_stop(g, 0.0f, LVG_COLOR_BLACK);
    lui_gradient_add_stop(g, 1.0f, LVG_COLOR_WHITE);
}

int lui_gradient_add_stop(lui_gradient_t *g, float position, lvg_color_t color)
{
    if (!g || g->stop_count >= LUI_GRADIENT_MAX_STOPS) return -1;
    int idx = g->stop_count;
    g->stops[idx].position = clampf(position, 0.0f, 1.0f);
    g->stops[idx].color = color;
    g->stop_count++;
    sort_stops(g);
    return idx;
}

void lui_gradient_remove_stop(lui_gradient_t *g, int index)
{
    if (!g || index < 0 || index >= g->stop_count) return;
    for (int i = index; i < g->stop_count - 1; i++)
        g->stops[i] = g->stops[i + 1];
    g->stop_count--;
    if (g->selected_stop == index) g->selected_stop = -1;
    else if (g->selected_stop > index) g->selected_stop--;
}

void lui_gradient_set_stop(lui_gradient_t *g, int index,
                            float position, lvg_color_t color)
{
    if (!g || index < 0 || index >= g->stop_count) return;
    g->stops[index].position = clampf(position, 0.0f, 1.0f);
    g->stops[index].color = color;
    sort_stops(g);
}

lvg_color_t lui_gradient_evaluate(const lui_gradient_t *g, float t)
{
    if (!g || g->stop_count == 0) return LVG_COLOR_BLACK;
    t = clampf(t, 0.0f, 1.0f);

    if (g->stop_count == 1) return g->stops[0].color;

    /* Find surrounding stops */
    if (t <= g->stops[0].position) return g->stops[0].color;
    if (t >= g->stops[g->stop_count - 1].position)
        return g->stops[g->stop_count - 1].color;

    for (int i = 0; i < g->stop_count - 1; i++) {
        if (t >= g->stops[i].position && t <= g->stops[i + 1].position) {
            float range = g->stops[i + 1].position - g->stops[i].position;
            float local_t = range > 0 ? (t - g->stops[i].position) / range : 0;
            return lvg_color_lerp(g->stops[i].color, g->stops[i + 1].color,
                               local_t);
        }
    }
    return g->stops[g->stop_count - 1].color;
}

void lui_gradient_clear(lui_gradient_t *g)
{
    if (!g) return;
    g->stop_count = 0;
    g->selected_stop = -1;
}
