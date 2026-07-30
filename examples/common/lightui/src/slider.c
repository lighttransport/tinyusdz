/*
 * slider.c — Horizontal slider widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/slider.h>
#include <stddef.h>

static inline float lui__s_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float lui__slider_fraction(const lui_slider_t *s)
{
    if (s->max_val <= s->min_val) return 0.0f;
    return (s->value - s->min_val) / (s->max_val - s->min_val);
}

static void lui__slider_value_from_x(lui_slider_t *s, int mouse_x)
{
    lvg_rect_t r = lui_widget_absolute_rect(&s->widget);
    int track_left  = r.x + s->thumb_radius;
    int track_right = r.x + r.width - s->thumb_radius;
    int track_w = track_right - track_left;
    if (track_w <= 0) return;

    float frac = (float)(mouse_x - track_left) / (float)track_w;
    frac = lui__s_clampf(frac, 0.0f, 1.0f);

    float new_val = s->min_val + frac * (s->max_val - s->min_val);
    if (new_val != s->value) {
        s->value = new_val;
        if (s->on_change)
            s->on_change(s->value, s->on_change_user);
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int slider_measure(const lui_widget_t *w, int *out_w, int *out_h,
                           void *user)
{
    const lui_slider_t *s = (const lui_slider_t *)w;
    (void)user;
    *out_w = 120;
    *out_h = s->thumb_radius * 2 + 2;
    return 0;
}

static void slider_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_slider_t *s = (lui_slider_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    float frac = lui__slider_fraction(s);
    int track_left  = r.x + s->thumb_radius;
    int track_right = r.x + r.width - s->thumb_radius;
    int track_w = track_right - track_left;
    int track_y = r.y + r.height / 2 - s->track_height / 2;

    /* Track background */
    lvg_canvas_fill_rounded_rect(canvas, track_left, track_y,
                                  track_w, s->track_height,
                                  s->track_height / 2, s->track_color);

    /* Filled portion */
    int fill_w = (int)(frac * track_w);
    if (fill_w > 0) {
        lvg_canvas_fill_rounded_rect(canvas, track_left, track_y,
                                      fill_w, s->track_height,
                                      s->track_height / 2, s->track_fill_color);
    }

    /* Thumb */
    int thumb_x = track_left + (int)(frac * track_w);
    int thumb_y = r.y + r.height / 2;
    lvg_color_t tc = s->dragging ? s->thumb_active : s->thumb_color;
    lvg_canvas_fill_circle(canvas, thumb_x, thumb_y, s->thumb_radius, tc);
}

static int slider_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_slider_t *s = (lui_slider_t *)w;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN:
        if (event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            lvg_rect_t r = lui_widget_absolute_rect(w);
            if (lvg_rect_contains_point(&r,
                    event->data.mouse_button.x,
                    event->data.mouse_button.y)) {
                s->dragging = true;
                lui__slider_value_from_x(s, event->data.mouse_button.x);
                return 1;
            }
        }
        break;

    case LUI_EVENT_MOUSE_MOVE:
        if (s->dragging) {
            lui__slider_value_from_x(s, event->data.mouse_move.x);
            return 1;
        }
        break;

    case LUI_EVENT_MOUSE_UP:
        if (s->dragging && event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            s->dragging = false;
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

void lui_slider_init(lui_slider_t *s, float min_val, float max_val, float value)
{
    if (!s) return;

    lui_widget_init(&s->widget);
    s->widget.width   = lvg_size_hug(120);
    s->widget.height  = lvg_size_hug(0);
    s->widget.measure = slider_measure;
    s->widget.draw    = slider_draw;
    s->widget.on_event = slider_event;

    s->min_val   = min_val;
    s->max_val   = max_val;
    s->value     = lui__s_clampf(value, min_val, max_val);
    s->dragging  = false;

    s->thumb_radius     = 7;
    s->track_height     = 4;
    s->track_color      = LVG_COLOR_RGB(0x40, 0x44, 0x4B);
    s->track_fill_color = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    s->thumb_color      = LVG_COLOR_WHITE;
    s->thumb_active     = LVG_COLOR_RGB(0x89, 0xB4, 0xFA);
    s->on_change      = NULL;
    s->on_change_user = NULL;
}

void lui_slider_set_value(lui_slider_t *s, float value)
{
    if (!s) return;
    s->value = lui__s_clampf(value, s->min_val, s->max_val);
}
