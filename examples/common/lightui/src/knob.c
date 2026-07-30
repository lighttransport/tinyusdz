/*
 * knob.c — Rotary knob (dial) widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/knob.h>
#include <lightvg/canvas.h>

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline float lui__k_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Normalise value to 0.0–1.0 within [min, max]. */
static float knob_fraction(const lui_knob_t *k)
{
    if (k->max_val <= k->min_val) return 0.0f;
    return (k->value - k->min_val) / (k->max_val - k->min_val);
}

/*
 * Arc angles.  The knob arc spans 270 degrees:
 *   start = 135 degrees (lower-left)
 *   end   = 405 degrees (lower-right, i.e. 135 + 270)
 * Angles in radians, measured clockwise from 3-o'clock.
 */
#define KNOB_ARC_START  (135.0f * (float)M_PI / 180.0f)
#define KNOB_ARC_SWEEP  (270.0f * (float)M_PI / 180.0f)

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int knob_measure(const lui_widget_t *w, int *out_w, int *out_h,
                          void *user)
{
    const lui_knob_t *k = (const lui_knob_t *)w;
    (void)user;
    *out_w = k->size;
    *out_h = k->size;
    return 0;
}

static void knob_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_knob_t *k = (lui_knob_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int sz = r.width < r.height ? r.width : r.height;
    int cx = r.x + r.width  / 2;
    int cy = r.y + r.height / 2;
    int radius = sz / 2 - 2;
    if (radius < 4) radius = 4;

    /* Arc track (270-degree arc of small rects) */
    int arc_r = radius;
    int steps = (int)((float)arc_r * 2.0f);
    if (steps < 24) steps = 24;
    if (steps > 120) steps = 120;

    float frac = knob_fraction(k);

    for (int s = 0; s <= steps; s++) {
        float t = (float)s / (float)steps;
        float angle = KNOB_ARC_START + KNOB_ARC_SWEEP * t;
        int px = cx + (int)(cosf(angle) * (float)arc_r);
        int py = cy + (int)(sinf(angle) * (float)arc_r);
        /* Colour: filled portion vs track */
        lvg_color_t c = (t <= frac) ? k->indicator_color : k->track_color;
        lvg_canvas_fill_rect(canvas, px - 1, py - 1, 2, 2, c);
    }

    /* Knob body (filled circle, slightly smaller than track) */
    int body_r = radius - 4;
    if (body_r < 3) body_r = 3;
    lvg_canvas_fill_circle(canvas, cx, cy, body_r, k->knob_color);
    lvg_canvas_stroke_circle(canvas, cx, cy, body_r, k->border_color, 1);

    /* Value indicator line */
    float val_angle = KNOB_ARC_START + KNOB_ARC_SWEEP * frac;
    int ind_inner = body_r / 3;
    int ind_outer = body_r - 2;
    int x0 = cx + (int)(cosf(val_angle) * (float)ind_inner);
    int y0 = cy + (int)(sinf(val_angle) * (float)ind_inner);
    int x1 = cx + (int)(cosf(val_angle) * (float)ind_outer);
    int y1 = cy + (int)(sinf(val_angle) * (float)ind_outer);
    lvg_canvas_draw_line(canvas, x0, y0, x1, y1, k->indicator_color, 2);
}

static int knob_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_knob_t *k = (lui_knob_t *)w;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN:
        if (event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            lvg_rect_t r = lui_widget_absolute_rect(w);
            if (lvg_rect_contains_point(&r,
                    event->data.mouse_button.x,
                    event->data.mouse_button.y)) {
                k->dragging = true;
                k->drag_start_y = event->data.mouse_button.y;
                k->drag_start_val = knob_fraction(k);
                return 1;
            }
        }
        break;

    case LUI_EVENT_MOUSE_MOVE:
        if (k->dragging) {
            /* Vertical drag: up increases, down decreases.
             * 200 pixels of drag = full range. */
            int delta_y = k->drag_start_y - event->data.mouse_move.y;
            float delta_frac = (float)delta_y / 200.0f;
            float new_frac = lui__k_clampf(k->drag_start_val + delta_frac,
                                            0.0f, 1.0f);
            float new_val = k->min_val + new_frac * (k->max_val - k->min_val);
            if (new_val != k->value) {
                k->value = new_val;
                if (k->on_change)
                    k->on_change(k->value, k->on_change_user);
            }
            return 1;
        }
        break;

    case LUI_EVENT_MOUSE_UP:
        if (k->dragging && event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            k->dragging = false;
            return 1;
        }
        break;

    default:
        break;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_knob_init(lui_knob_t *k, float min_val, float max_val, float value)
{
    if (!k) return;

    lui_widget_init(&k->widget);
    k->widget.width   = lvg_size_hug(0);
    k->widget.height  = lvg_size_hug(0);
    k->widget.measure = knob_measure;
    k->widget.draw    = knob_draw;
    k->widget.on_event = knob_event;

    k->min_val   = min_val;
    k->max_val   = max_val;
    k->value     = lui__k_clampf(value, min_val, max_val);
    k->size      = 48;
    k->dragging  = false;
    k->drag_start_y   = 0;
    k->drag_start_val = 0.0f;

    k->knob_color      = LVG_COLOR_RGB(0x3A, 0x3D, 0x42);
    k->indicator_color  = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    k->track_color      = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    k->border_color     = LVG_COLOR_RGB(0x6C, 0x70, 0x76);

    k->on_change      = NULL;
    k->on_change_user = NULL;
}

void lui_knob_set_value(lui_knob_t *k, float value)
{
    if (!k) return;
    k->value = lui__k_clampf(value, k->min_val, k->max_val);
}
