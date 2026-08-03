/*
 * splitter.c — Draggable panel splitter widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/splitter.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static lvg_rect_t divider_rect(const lui_splitter_t *sp, lvg_rect_t wr)
{
    int ds = sp->divider_size;
    if (sp->direction == LUI_SPLIT_HORIZONTAL) {
        int split_x = wr.x + (int)(sp->ratio * wr.width);
        return lvg_rect_make(split_x - ds / 2, wr.y, ds, wr.height);
    } else {
        int split_y = wr.y + (int)(sp->ratio * wr.height);
        return lvg_rect_make(wr.x, split_y - ds / 2, wr.width, ds);
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int sp_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)w; (void)user;
    *out_w = 400;
    *out_h = 300;
    return 0;
}

static void sp_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_splitter_t *sp = (lui_splitter_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&wr)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, wr.x, wr.y, wr.width, wr.height, sp->bg);

    /* Divider */
    lvg_rect_t dr = divider_rect(sp, wr);
    lvg_color_t dc = sp->dragging ? sp->divider_drag : sp->divider_color;
    lvg_canvas_fill_rect(canvas, dr.x, dr.y, dr.width, dr.height, dc);

    /* Grip indicator */
    if (sp->show_grip) {
        int cx = dr.x + dr.width / 2;
        int cy = dr.y + dr.height / 2;
        if (sp->direction == LUI_SPLIT_HORIZONTAL) {
            for (int g = -2; g <= 2; g++)
                lvg_canvas_fill_rect(canvas, cx - 1, cy + g * 6, 2, 3,
                                      sp->grip_color);
        } else {
            for (int g = -2; g <= 2; g++)
                lvg_canvas_fill_rect(canvas, cx + g * 6, cy - 1, 3, 2,
                                      sp->grip_color);
        }
    }
}

static int sp_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_splitter_t *sp = (lui_splitter_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        lvg_rect_t dr = divider_rect(sp, wr);
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (lvg_rect_contains_point(&dr, mx, my)) {
            sp->dragging = true;
            sp->initial_ratio = sp->ratio;
            return 1;
        }
        return 0;
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE && sp->dragging) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        float new_ratio;
        if (sp->direction == LUI_SPLIT_HORIZONTAL) {
            new_ratio = (float)(mx - wr.x) / (float)wr.width;
        } else {
            new_ratio = (float)(my - wr.y) / (float)wr.height;
        }

        /* Apply min constraints */
        int total = sp->direction == LUI_SPLIT_HORIZONTAL
                  ? wr.width : wr.height;
        float min_first = (float)sp->min_first / (float)total;
        float max_ratio = 1.0f - (float)sp->min_second / (float)total;
        sp->ratio = clampf(new_ratio, min_first, max_ratio);
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_UP &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        if (sp->dragging) {
            sp->dragging = false;
            return 1;
        }
    }

    /* Double-click on divider resets to 50/50 */
    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.clicks >= 2) {
        lvg_rect_t dr = divider_rect(sp, wr);
        if (lvg_rect_contains_point(&dr,
                event->data.mouse_button.x,
                event->data.mouse_button.y)) {
            sp->ratio = 0.5f;
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_splitter_init(lui_splitter_t *sp)
{
    if (!sp) return;

    lui_widget_init(&sp->widget);
    sp->widget.width   = lvg_size_fill(1);
    sp->widget.height  = lvg_size_fill(1);
    sp->widget.measure = sp_measure;
    sp->widget.draw    = sp_draw;
    sp->widget.on_event = sp_event;
    sp->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    sp->direction    = LUI_SPLIT_HORIZONTAL;
    sp->ratio        = 0.5f;
    sp->min_first    = 50;
    sp->min_second   = 50;
    sp->divider_size = 6;
    sp->dragging     = false;
    sp->initial_ratio = 0.5f;
    sp->show_grip    = true;

    sp->bg            = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    sp->divider_color = LVG_COLOR_RGB(0x3A, 0x3E, 0x46);
    sp->divider_hover = LVG_COLOR_RGB(0x4A, 0x4E, 0x56);
    sp->divider_drag  = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    sp->grip_color    = LVG_COLOR_RGB(0x60, 0x64, 0x6C);
}

void lui_splitter_set_ratio(lui_splitter_t *sp, float ratio)
{
    if (!sp) return;
    sp->ratio = clampf(ratio, 0.0f, 1.0f);
}

void lui_splitter_reset(lui_splitter_t *sp)
{
    if (!sp) return;
    sp->ratio = 0.5f;
}

void lui_splitter_get_panels(const lui_splitter_t *sp,
                              lvg_rect_t *first, lvg_rect_t *second)
{
    if (!sp) return;
    lvg_rect_t wr = lui_widget_absolute_rect((lui_widget_t *)&sp->widget);
    int ds = sp->divider_size;

    if (sp->direction == LUI_SPLIT_HORIZONTAL) {
        int split = (int)(sp->ratio * wr.width);
        if (first)
            *first = lvg_rect_make(wr.x, wr.y, split - ds / 2, wr.height);
        if (second)
            *second = lvg_rect_make(wr.x + split + ds / 2, wr.y,
                                     wr.width - split - ds / 2, wr.height);
    } else {
        int split = (int)(sp->ratio * wr.height);
        if (first)
            *first = lvg_rect_make(wr.x, wr.y, wr.width, split - ds / 2);
        if (second)
            *second = lvg_rect_make(wr.x, wr.y + split + ds / 2,
                                     wr.width, wr.height - split - ds / 2);
    }
}
