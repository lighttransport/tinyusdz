/*
 * rating.c — Star rating input widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/rating.h>
#include <lightvg/canvas.h>
#include <stddef.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int rating_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/*
 * Draw a single star as two overlapping equilateral-ish triangles
 * (Star of David shape) for visual clarity.
 *
 * Triangle 1: pointing up   (base at bottom)
 * Triangle 2: pointing down (base at top)
 */
static void rating_draw_star_filled(lvg_canvas_t *canvas,
                                     int cx, int cy, int size,
                                     lvg_color_t color)
{
    int half = size / 2;
    int h = (int)(half * 0.866f); /* sqrt(3)/2 */

    /* Upward triangle */
    lvg_canvas_fill_triangle(canvas,
                              cx, cy - half,
                              cx - h, cy + half / 2,
                              cx + h, cy + half / 2,
                              color);

    /* Downward triangle */
    lvg_canvas_fill_triangle(canvas,
                              cx, cy + half,
                              cx - h, cy - half / 2,
                              cx + h, cy - half / 2,
                              color);
}

static void rating_draw_star_outline(lvg_canvas_t *canvas,
                                      int cx, int cy, int size,
                                      lvg_color_t color)
{
    int half = size / 2;
    int h = (int)(half * 0.866f);

    /* Upward triangle outline */
    lvg_canvas_draw_line(canvas, cx, cy - half,
                          cx - h, cy + half / 2, color, 1);
    lvg_canvas_draw_line(canvas, cx - h, cy + half / 2,
                          cx + h, cy + half / 2, color, 1);
    lvg_canvas_draw_line(canvas, cx + h, cy + half / 2,
                          cx, cy - half, color, 1);

    /* Downward triangle outline */
    lvg_canvas_draw_line(canvas, cx, cy + half,
                          cx - h, cy - half / 2, color, 1);
    lvg_canvas_draw_line(canvas, cx - h, cy - half / 2,
                          cx + h, cy - half / 2, color, 1);
    lvg_canvas_draw_line(canvas, cx + h, cy - half / 2,
                          cx, cy + half, color, 1);
}

/* Get the bounding rect of star at index, in absolute coordinates. */
static lvg_rect_t rating_star_rect(const lui_rating_t *r,
                                    const lvg_rect_t *abs,
                                    int index)
{
    int cell = r->star_size + r->spacing;
    int x = abs->x + index * cell;
    int y = abs->y + (abs->height - r->star_size) / 2;
    return lvg_rect_make(x, y, r->star_size, r->star_size);
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int rating_measure(const lui_widget_t *w, int *out_w, int *out_h,
                           void *user)
{
    const lui_rating_t *r = (const lui_rating_t *)w;
    (void)user;

    int n = r->max_stars > 0 ? r->max_stars : 1;
    *out_w = n * r->star_size + (n - 1) * r->spacing;
    *out_h = r->star_size;
    return 0;
}

static void rating_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_rating_t *r = (lui_rating_t *)w;
    lvg_rect_t abs = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&abs)) return;

    int n = r->max_stars;
    int display_value = r->value;

    /* If hovering, show preview up to hovered star */
    if (r->hovered_star >= 0) {
        display_value = r->hovered_star + 1;
    }

    for (int i = 0; i < n; i++) {
        lvg_rect_t sr = rating_star_rect(r, &abs, i);
        int cx = sr.x + sr.width / 2;
        int cy = sr.y + sr.height / 2;

        if (i < display_value) {
            /* Filled star */
            lvg_color_t col = (r->hovered_star >= 0 && i >= r->value)
                              ? r->hover_color : r->filled_color;
            rating_draw_star_filled(canvas, cx, cy, r->star_size, col);
        } else {
            /* Empty star (outline only) */
            rating_draw_star_outline(canvas, cx, cy, r->star_size,
                                      r->empty_color);
        }
    }
}

/* Determine which star index a mouse x falls on (-1 if none). */
static int rating_hit_star(const lui_rating_t *r, const lvg_rect_t *abs,
                            int mx, int my)
{
    for (int i = 0; i < r->max_stars; i++) {
        lvg_rect_t sr = rating_star_rect(r, abs, i);
        if (lvg_rect_contains_point(&sr, mx, my)) {
            return i;
        }
    }
    return -1;
}

static int rating_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_rating_t *r = (lui_rating_t *)w;
    lvg_rect_t abs = lui_widget_absolute_rect(w);

    switch (event->type) {
    case LUI_EVENT_MOUSE_MOVE: {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        if (lvg_rect_contains_point(&abs, mx, my)) {
            r->hovered_star = rating_hit_star(r, &abs, mx, my);
        } else {
            r->hovered_star = -1;
        }
        return 0; /* don't consume — allow bubbling */
    }

    case LUI_EVENT_MOUSE_DOWN: {
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (!lvg_rect_contains_point(&abs, mx, my)) break;

        int star = rating_hit_star(r, &abs, mx, my);
        if (star >= 0) {
            int new_val = star + 1;
            /* Click on current value clears it */
            if (new_val == r->value) new_val = 0;
            r->value = new_val;
            if (r->on_change) {
                r->on_change(r->value, r->on_change_user);
            }
            return 1;
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

void lui_rating_init(lui_rating_t *r, int max_stars)
{
    if (!r) return;

    lui_widget_init(&r->widget);
    r->widget.width    = lvg_size_hug(0);
    r->widget.height   = lvg_size_hug(0);
    r->widget.measure  = rating_measure;
    r->widget.draw     = rating_draw;
    r->widget.on_event = rating_event;

    r->value        = 0;
    r->max_stars    = max_stars > 0 ? max_stars : 5;
    r->hovered_star = -1;
    r->allow_half   = false;
    r->half_value   = 0.0f;

    r->star_size = 20;
    r->spacing   = 4;

    r->filled_color = LVG_COLOR_RGB(0xFF, 0xD7, 0x00); /* gold */
    r->empty_color  = LVG_COLOR_RGB(0x60, 0x64, 0x6C); /* gray */
    r->hover_color  = LVG_COLOR_RGB(0xFF, 0xE0, 0x80); /* light gold */
    r->border_color = LVG_COLOR_RGB(0x80, 0x80, 0x80);

    r->on_change      = NULL;
    r->on_change_user = NULL;
}

void lui_rating_set_value(lui_rating_t *r, int value)
{
    if (!r) return;
    r->value = rating_clamp(value, 0, r->max_stars);
}

int lui_rating_get_value(const lui_rating_t *r)
{
    if (!r) return 0;
    return r->value;
}
