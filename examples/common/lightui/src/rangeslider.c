/*
 * rangeslider.c -- Dual-handle range slider widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/rangeslider.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline float rs_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Map a value to pixel x within the track. */
static int rs_val_to_x(const lui_rangeslider_t *rs, const lvg_rect_t *r,
                        float val)
{
    int track_left  = r->x + rs->thumb_radius;
    int track_right = r->x + r->width - rs->thumb_radius;
    int track_w = track_right - track_left;
    if (track_w <= 0) return track_left;
    float frac = (val - rs->min_val) / (rs->max_val - rs->min_val);
    return track_left + (int)(frac * (float)track_w);
}

/* Map a pixel x to a value. */
static float rs_x_to_val(const lui_rangeslider_t *rs, const lvg_rect_t *r,
                          int x)
{
    int track_left  = r->x + rs->thumb_radius;
    int track_right = r->x + r->width - rs->thumb_radius;
    int track_w = track_right - track_left;
    if (track_w <= 0) return rs->min_val;
    float frac = (float)(x - track_left) / (float)track_w;
    frac = rs_clampf(frac, 0.0f, 1.0f);
    return rs->min_val + frac * (rs->max_val - rs->min_val);
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int rangeslider_measure(const lui_widget_t *w, int *out_w, int *out_h,
                               void *user)
{
    const lui_rangeslider_t *rs = (const lui_rangeslider_t *)w;
    (void)user;
    *out_w = 160;
    *out_h = rs->thumb_radius * 2 + 2;
    return 0;
}

static void rangeslider_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_rangeslider_t *rs = (lui_rangeslider_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int track_left  = r.x + rs->thumb_radius;
    int track_right = r.x + r.width - rs->thumb_radius;
    int track_w = track_right - track_left;
    int track_y = r.y + r.height / 2 - rs->track_height / 2;

    /* Full track background */
    lvg_canvas_fill_rounded_rect(canvas, track_left, track_y,
                                  track_w, rs->track_height,
                                  rs->track_height / 2, rs->track_color);

    /* Range fill between low and high */
    int low_x  = rs_val_to_x(rs, &r, rs->low);
    int high_x = rs_val_to_x(rs, &r, rs->high);
    if (high_x > low_x) {
        lvg_canvas_fill_rounded_rect(canvas, low_x, track_y,
                                      high_x - low_x, rs->track_height,
                                      rs->track_height / 2, rs->range_color);
    }

    /* Low thumb */
    {
        lvg_color_t tc = rs->dragging_low ? rs->thumb_active : rs->thumb_color;
        int thumb_cy = r.y + r.height / 2;
        lvg_canvas_fill_circle(canvas, low_x, thumb_cy, rs->thumb_radius, tc);
        lvg_canvas_stroke_circle(canvas, low_x, thumb_cy, rs->thumb_radius,
                                  rs->border_color, 1);
    }

    /* High thumb */
    {
        lvg_color_t tc = rs->dragging_high ? rs->thumb_active : rs->thumb_color;
        int thumb_cy = r.y + r.height / 2;
        lvg_canvas_fill_circle(canvas, high_x, thumb_cy, rs->thumb_radius, tc);
        lvg_canvas_stroke_circle(canvas, high_x, thumb_cy, rs->thumb_radius,
                                  rs->border_color, 1);
    }
}

static void rs_enforce_constraints(lui_rangeslider_t *rs)
{
    rs->low  = rs_clampf(rs->low, rs->min_val, rs->max_val - rs->min_span);
    rs->high = rs_clampf(rs->high, rs->min_val + rs->min_span, rs->max_val);
    if (rs->high - rs->low < rs->min_span) {
        rs->high = rs->low + rs->min_span;
        if (rs->high > rs->max_val) {
            rs->high = rs->max_val;
            rs->low  = rs->max_val - rs->min_span;
        }
    }
}

static int rangeslider_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_rangeslider_t *rs = (lui_rangeslider_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN: {
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (!lvg_rect_contains_point(&r, mx, my)) break;

        int low_x  = rs_val_to_x(rs, &r, rs->low);
        int high_x = rs_val_to_x(rs, &r, rs->high);
        int tr = rs->thumb_radius;

        /* Check if clicking on low thumb */
        int dl = (mx - low_x) * (mx - low_x) +
                 (my - (r.y + r.height / 2)) * (my - (r.y + r.height / 2));
        if (dl <= tr * tr) {
            rs->dragging_low = true;
            return 1;
        }

        /* Check if clicking on high thumb */
        int dh = (mx - high_x) * (mx - high_x) +
                 (my - (r.y + r.height / 2)) * (my - (r.y + r.height / 2));
        if (dh <= tr * tr) {
            rs->dragging_high = true;
            return 1;
        }

        /* Check if clicking between the two thumbs (range drag) */
        if (mx > low_x && mx < high_x) {
            rs->dragging_range  = true;
            rs->drag_start_x    = mx;
            rs->drag_start_low  = rs->low;
            rs->drag_start_high = rs->high;
            return 1;
        }
        break;
    }

    case LUI_EVENT_MOUSE_MOVE: {
        int mx = event->data.mouse_move.x;

        if (rs->dragging_low) {
            float new_low = rs_x_to_val(rs, &r, mx);
            new_low = rs_clampf(new_low, rs->min_val,
                                rs->high - rs->min_span);
            if (new_low != rs->low) {
                rs->low = new_low;
                if (rs->on_change)
                    rs->on_change(rs->low, rs->high, rs->on_change_user);
            }
            return 1;
        }

        if (rs->dragging_high) {
            float new_high = rs_x_to_val(rs, &r, mx);
            new_high = rs_clampf(new_high, rs->low + rs->min_span,
                                 rs->max_val);
            if (new_high != rs->high) {
                rs->high = new_high;
                if (rs->on_change)
                    rs->on_change(rs->low, rs->high, rs->on_change_user);
            }
            return 1;
        }

        if (rs->dragging_range) {
            float dx_val = rs_x_to_val(rs, &r, mx)
                         - rs_x_to_val(rs, &r, rs->drag_start_x);
            float new_low  = rs->drag_start_low + dx_val;
            float new_high = rs->drag_start_high + dx_val;
            float span = rs->drag_start_high - rs->drag_start_low;

            if (new_low < rs->min_val) {
                new_low  = rs->min_val;
                new_high = rs->min_val + span;
            }
            if (new_high > rs->max_val) {
                new_high = rs->max_val;
                new_low  = rs->max_val - span;
            }

            if (new_low != rs->low || new_high != rs->high) {
                rs->low  = new_low;
                rs->high = new_high;
                if (rs->on_change)
                    rs->on_change(rs->low, rs->high, rs->on_change_user);
            }
            return 1;
        }
        break;
    }

    case LUI_EVENT_MOUSE_UP:
        if (rs->dragging_low || rs->dragging_high || rs->dragging_range) {
            rs->dragging_low   = false;
            rs->dragging_high  = false;
            rs->dragging_range = false;
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

void lui_rangeslider_init(lui_rangeslider_t *rs, float min_val, float max_val,
                          float low, float high)
{
    if (!rs) return;

    lui_widget_init(&rs->widget);
    rs->widget.width    = lvg_size_hug(160);
    rs->widget.height   = lvg_size_hug(0);
    rs->widget.measure  = rangeslider_measure;
    rs->widget.draw     = rangeslider_draw;
    rs->widget.on_event = rangeslider_event;

    rs->min_val  = min_val;
    rs->max_val  = max_val;
    rs->min_span = 0.0f;
    rs->low      = rs_clampf(low, min_val, max_val);
    rs->high     = rs_clampf(high, min_val, max_val);
    if (rs->high < rs->low) rs->high = rs->low;

    rs->dragging_low   = false;
    rs->dragging_high  = false;
    rs->dragging_range = false;
    rs->drag_start_x    = 0;
    rs->drag_start_low  = 0.0f;
    rs->drag_start_high = 0.0f;

    rs->thumb_radius = 7;
    rs->track_height = 4;
    rs->track_color  = LVG_COLOR_RGB(0x40, 0x44, 0x4B);
    rs->range_color  = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    rs->thumb_color  = LVG_COLOR_WHITE;
    rs->thumb_active = LVG_COLOR_RGB(0x89, 0xB4, 0xFA);
    rs->border_color = LVG_COLOR_RGB(0x58, 0x5B, 0x70);

    rs->on_change      = NULL;
    rs->on_change_user = NULL;
}

void lui_rangeslider_set_range(lui_rangeslider_t *rs, float low, float high)
{
    if (!rs) return;
    rs->low  = rs_clampf(low, rs->min_val, rs->max_val);
    rs->high = rs_clampf(high, rs->min_val, rs->max_val);
    if (rs->high < rs->low) rs->high = rs->low;
    rs_enforce_constraints(rs);
}
