/*
 * toggle.c — Toggle switch widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/toggle.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline float lui__t_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int toggle_measure(const lui_widget_t *w, int *out_w, int *out_h,
                            void *user)
{
    const lui_toggle_t *t = (const lui_toggle_t *)w;
    (void)user;
    *out_w = t->track_width;
    *out_h = t->track_height;
    return 0;
}

static void toggle_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_toggle_t *t = (lui_toggle_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Centre track within widget */
    int tx = r.x + (r.width  - t->track_width)  / 2;
    int ty = r.y + (r.height - t->track_height) / 2;
    int tw = t->track_width;
    int th = t->track_height;
    int cr = th / 2;  /* pill-shaped = half height */

    /* Interpolate track colour between off and on based on anim_pos */
    float ap = t->anim_pos;
    int tr = (int)((1.0f - ap) * (float)LVG_COLOR_R(t->off_color) +
                   ap * (float)LVG_COLOR_R(t->on_color));
    int tg = (int)((1.0f - ap) * (float)LVG_COLOR_G(t->off_color) +
                   ap * (float)LVG_COLOR_G(t->on_color));
    int tb = (int)((1.0f - ap) * (float)LVG_COLOR_B(t->off_color) +
                   ap * (float)LVG_COLOR_B(t->on_color));
    lvg_color_t track_col = LVG_COLOR_RGB(tr, tg, tb);

    /* Track background (pill shape) */
    lvg_canvas_fill_rounded_rect(canvas, tx, ty, tw, th, cr, track_col);

    /* Thumb position: interpolate from left to right */
    int thumb_min_x = tx + t->thumb_radius + 2;
    int thumb_max_x = tx + tw - t->thumb_radius - 2;
    int thumb_cx = thumb_min_x + (int)(ap * (float)(thumb_max_x - thumb_min_x));
    int thumb_cy = ty + th / 2;

    /* Thumb circle */
    lvg_canvas_fill_circle(canvas, thumb_cx, thumb_cy,
                            t->thumb_radius, t->thumb_color);
}

static bool toggle_animate(lui_widget_t *w, float dt)
{
    lui_toggle_t *t = (lui_toggle_t *)w;
    float target = t->on ? 1.0f : 0.0f;
    float diff = target - t->anim_pos;

    if (diff > 0.001f || diff < -0.001f) {
        /* Animate at ~8x speed (completes in ~125ms) */
        float speed = 8.0f * dt;
        if (speed > 1.0f) speed = 1.0f;
        t->anim_pos += diff * speed;
        t->anim_pos = lui__t_clampf(t->anim_pos, 0.0f, 1.0f);
        return true;
    }

    /* Snap to target and stop animating */
    t->anim_pos = target;
    w->flags &= ~LUI_WIDGET_ANIMATING;
    return false;
}

static int toggle_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_toggle_t *t = (lui_toggle_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        if (lvg_rect_contains_point(&r,
                event->data.mouse_button.x,
                event->data.mouse_button.y)) {
            t->on = !t->on;
            /* Start animation */
            w->flags |= LUI_WIDGET_ANIMATING;
            if (t->on_change)
                t->on_change(t->on, t->on_change_user);
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_toggle_init(lui_toggle_t *t, bool on)
{
    if (!t) return;

    lui_widget_init(&t->widget);
    t->widget.width   = lvg_size_hug(0);
    t->widget.height  = lvg_size_hug(0);
    t->widget.measure = toggle_measure;
    t->widget.draw    = toggle_draw;
    t->widget.on_event = toggle_event;
    t->widget.animate  = toggle_animate;

    t->on           = on;
    t->track_width  = 44;
    t->track_height = 22;
    t->thumb_radius = 8;
    t->anim_pos     = on ? 1.0f : 0.0f;
    t->on_color     = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    t->off_color    = LVG_COLOR_RGB(0x40, 0x44, 0x4B);
    t->thumb_color  = LVG_COLOR_WHITE;
    t->on_change      = NULL;
    t->on_change_user = NULL;
}

void lui_toggle_set(lui_toggle_t *t, bool on)
{
    if (!t) return;
    t->on = on;
    t->anim_pos = on ? 1.0f : 0.0f;
    t->widget.flags &= ~LUI_WIDGET_ANIMATING;
}
