/*
 * minimap.c — Minimap / overview widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/minimap.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Map world coordinates to widget pixel coordinates. */
static void world_to_widget(const lui_minimap_t *mm, lvg_rect_t wr,
                              float wx, float wy, int *px, int *py)
{
    float sx = mm->world_w > 0 ? (float)wr.width / mm->world_w : 1.0f;
    float sy = mm->world_h > 0 ? (float)wr.height / mm->world_h : 1.0f;
    float scale = sx < sy ? sx : sy;
    float ox = wr.x + (wr.width - mm->world_w * scale) * 0.5f;
    float oy = wr.y + (wr.height - mm->world_h * scale) * 0.5f;
    if (px) *px = (int)(ox + (wx - mm->world_x) * scale);
    if (py) *py = (int)(oy + (wy - mm->world_y) * scale);
}

static void widget_to_world(const lui_minimap_t *mm, lvg_rect_t wr,
                              int px, int py, float *wx, float *wy)
{
    float sx = mm->world_w > 0 ? (float)wr.width / mm->world_w : 1.0f;
    float sy = mm->world_h > 0 ? (float)wr.height / mm->world_h : 1.0f;
    float scale = sx < sy ? sx : sy;
    float ox = wr.x + (wr.width - mm->world_w * scale) * 0.5f;
    float oy = wr.y + (wr.height - mm->world_h * scale) * 0.5f;
    if (wx) *wx = mm->world_x + (px - ox) / scale;
    if (wy) *wy = mm->world_y + (py - oy) / scale;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int mm_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)w; (void)user;
    *out_w = 150;
    *out_h = 100;
    return 0;
}

static void mm_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_minimap_t *mm = (lui_minimap_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rounded_rect(canvas, r.x, r.y, r.width, r.height,
                                  mm->corner_radius, mm->bg);

    /* Viewport rectangle */
    int vx0, vy0, vx1, vy1;
    world_to_widget(mm, r, mm->view_x, mm->view_y, &vx0, &vy0);
    world_to_widget(mm, r, mm->view_x + mm->view_w,
                     mm->view_y + mm->view_h, &vx1, &vy1);
    int vw = vx1 - vx0;
    int vh = vy1 - vy0;
    if (vw < 4) vw = 4;
    if (vh < 4) vh = 4;

    lvg_canvas_fill_rect(canvas, vx0, vy0, vw, vh, mm->viewport_fill);
    lvg_canvas_stroke_rect(canvas, vx0, vy0, vw, vh,
                            mm->viewport_border, 1);

    /* Border */
    lvg_canvas_stroke_rounded_rect(canvas, r.x, r.y, r.width, r.height,
                                    mm->corner_radius, mm->border_color, 1);
}

static int mm_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_minimap_t *mm = (lui_minimap_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        if (lvg_rect_contains_point(&r,
                event->data.mouse_button.x,
                event->data.mouse_button.y)) {
            mm->dragging = true;
            float wx, wy;
            widget_to_world(mm, r, event->data.mouse_button.x,
                             event->data.mouse_button.y, &wx, &wy);
            mm->drag_offset_x = wx - mm->view_x - mm->view_w * 0.5f;
            mm->drag_offset_y = wy - mm->view_y - mm->view_h * 0.5f;

            /* Snap center to click if outside current viewport */
            float dist_x = wx - (mm->view_x + mm->view_w * 0.5f);
            float dist_y = wy - (mm->view_y + mm->view_h * 0.5f);
            if (dist_x < -mm->view_w * 0.5f || dist_x > mm->view_w * 0.5f ||
                dist_y < -mm->view_h * 0.5f || dist_y > mm->view_h * 0.5f) {
                mm->view_x = wx - mm->view_w * 0.5f;
                mm->view_y = wy - mm->view_h * 0.5f;
                mm->drag_offset_x = 0;
                mm->drag_offset_y = 0;
                if (mm->on_pan)
                    mm->on_pan(mm->view_x, mm->view_y, mm->on_pan_user);
            }
            return 1;
        }
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE && mm->dragging) {
        float wx, wy;
        widget_to_world(mm, r, event->data.mouse_move.x,
                         event->data.mouse_move.y, &wx, &wy);
        mm->view_x = wx - mm->drag_offset_x - mm->view_w * 0.5f;
        mm->view_y = wy - mm->drag_offset_y - mm->view_h * 0.5f;

        /* Clamp to world bounds */
        mm->view_x = clampf(mm->view_x, mm->world_x,
                              mm->world_x + mm->world_w - mm->view_w);
        mm->view_y = clampf(mm->view_y, mm->world_y,
                              mm->world_y + mm->world_h - mm->view_h);

        if (mm->on_pan)
            mm->on_pan(mm->view_x, mm->view_y, mm->on_pan_user);
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_UP) {
        mm->dragging = false;
        return 0;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_minimap_init(lui_minimap_t *mm)
{
    if (!mm) return;

    lui_widget_init(&mm->widget);
    mm->widget.width   = lvg_size_hug(150);
    mm->widget.height  = lvg_size_hug(100);
    mm->widget.measure = mm_measure;
    mm->widget.draw    = mm_draw;
    mm->widget.on_event = mm_event;
    mm->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    mm->world_x = 0; mm->world_y = 0;
    mm->world_w = 1000; mm->world_h = 1000;
    mm->view_x = 0; mm->view_y = 0;
    mm->view_w = 200; mm->view_h = 200;
    mm->dragging = false;
    mm->drag_offset_x = 0;
    mm->drag_offset_y = 0;

    mm->bg              = LVG_COLOR_RGB(0x1A, 0x1A, 0x24);
    mm->viewport_fill   = LVG_COLOR_ARGB(0x30, 0x58, 0x9C, 0xE0);
    mm->viewport_border = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    mm->border_color    = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    mm->corner_radius   = 3;

    mm->on_pan      = NULL;
    mm->on_pan_user = NULL;
}

void lui_minimap_set_world(lui_minimap_t *mm,
                            float x, float y, float w, float h)
{
    if (!mm) return;
    mm->world_x = x; mm->world_y = y;
    mm->world_w = w; mm->world_h = h;
}

void lui_minimap_set_viewport(lui_minimap_t *mm,
                               float x, float y, float w, float h)
{
    if (!mm) return;
    mm->view_x = x; mm->view_y = y;
    mm->view_w = w; mm->view_h = h;
}
