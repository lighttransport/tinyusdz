/*
 * checkbox.c — Checkbox (toggle) widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/checkbox.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int checkbox_measure(const lui_widget_t *w, int *out_w, int *out_h,
                             void *user)
{
    const lui_checkbox_t *cb = (const lui_checkbox_t *)w;
    (void)user;
    *out_w = cb->box_size;
    *out_h = cb->box_size;
    return 0;
}

static void checkbox_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_checkbox_t *cb = (lui_checkbox_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Centre the box within the widget */
    int bx = r.x + (r.width  - cb->box_size) / 2;
    int by = r.y + (r.height - cb->box_size) / 2;
    int bs = cb->box_size;

    int cr = cb->corner_radius;

    /* Box background */
    lvg_color_t fill = cb->checked ? cb->box_checked_color : cb->box_color;
    lvg_canvas_fill_rounded_rect(canvas, bx, by, bs, bs, cr, fill);

    /* 3D bevels (if set) — sunken look for 4Dwm style */
    if (LVG_COLOR_A(cb->bevel_light) > 0 && bs > 2) {
        /* Bottom-right = light (appears raised edge of surrounding surface) */
        lvg_canvas_fill_rect(canvas, bx, by + bs - 1, bs, 1, cb->bevel_light);
        lvg_canvas_fill_rect(canvas, bx + bs - 1, by, 1, bs, cb->bevel_light);
        /* Top-left = shadow (sunken into surface) */
        lvg_canvas_fill_rect(canvas, bx, by, bs, 1, cb->bevel_shadow);
        lvg_canvas_fill_rect(canvas, bx, by, 1, bs, cb->bevel_shadow);
    }

    /* Border */
    lvg_canvas_stroke_rounded_rect(canvas, bx, by, bs, bs, cr,
                                    cb->border_color,
                                    cb->border_width > 0 ? cb->border_width : 1);

    /* Checkmark (two lines forming a ✓) */
    if (cb->checked) {
        int cx = bx + bs / 2;
        int cy = by + bs / 2;
        /* Left stroke: from bottom-left of check to bottom centre */
        lvg_canvas_draw_line(canvas,
                              cx - bs / 4, cy,
                              cx - bs / 8, cy + bs / 4,
                              cb->check_color,
                              cb->check_width > 0 ? cb->check_width : 2);
        /* Right stroke: from bottom centre to top-right */
        lvg_canvas_draw_line(canvas,
                              cx - bs / 8, cy + bs / 4,
                              cx + bs / 4, cy - bs / 4,
                              cb->check_color,
                              cb->check_width > 0 ? cb->check_width : 2);
    }
}

static int checkbox_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_checkbox_t *cb = (lui_checkbox_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        if (lvg_rect_contains_point(&r,
                event->data.mouse_button.x,
                event->data.mouse_button.y)) {
            cb->checked = !cb->checked;
            if (cb->on_toggle)
                cb->on_toggle(cb->checked, cb->on_toggle_user);
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_checkbox_init(lui_checkbox_t *cb, bool checked)
{
    if (!cb) return;

    lui_widget_init(&cb->widget);
    cb->widget.width   = lvg_size_hug(0);
    cb->widget.height  = lvg_size_hug(0);
    cb->widget.measure = checkbox_measure;
    cb->widget.draw    = checkbox_draw;
    cb->widget.on_event = checkbox_event;

    cb->checked           = checked;
    cb->box_size          = 18;
    cb->box_color         = LVG_COLOR_RGB(0x40, 0x44, 0x4B);
    cb->box_checked_color = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    cb->border_color      = LVG_COLOR_RGB(0x6C, 0x70, 0x76);
    cb->check_color       = LVG_COLOR_WHITE;
    cb->bevel_light       = LVG_COLOR_TRANSPARENT;
    cb->bevel_shadow      = LVG_COLOR_TRANSPARENT;
    cb->corner_radius     = 3;
    cb->border_width      = 1;
    cb->check_width       = 2;
    cb->on_toggle       = NULL;
    cb->on_toggle_user  = NULL;
}

void lui_checkbox_set_checked(lui_checkbox_t *cb, bool checked)
{
    if (!cb) return;
    cb->checked = checked;
}
