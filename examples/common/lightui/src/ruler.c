/*
 * ruler.c — Measurement ruler widget for editors
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/ruler.h>
#include <lightvg/canvas.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define RULER_CHAR_W    5
#define RULER_CHAR_H   10
#define RULER_CHAR_ADV  7
#define RULER_DEFAULT_THICKNESS 24

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static float ruler_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Convert world coordinate to pixel offset within the ruler. */
static int ruler_world_to_pixel(const lui_ruler_t *r, float world)
{
    return (int)((world - r->origin) * r->scale);
}

/*
 * Compute a nice major interval for the current scale.
 * Adapts the label interval to avoid overlapping at different zoom levels.
 */
static float ruler_adapted_interval(const lui_ruler_t *r)
{
    float base = r->major_interval;
    if (r->scale <= 0.0f) return base;

    /* Minimum pixel gap between major labels (approximate label width) */
    float min_px = 50.0f;
    float interval_px = base * r->scale;

    /* If labels would be too close, multiply the interval */
    float mult = 1.0f;
    while (interval_px * mult < min_px) {
        mult *= 2.0f;
    }
    /* If labels are very far apart, subdivide */
    while (interval_px * mult > min_px * 8.0f && mult > 1.0f) {
        mult *= 0.5f;
    }

    return base * mult;
}

/* Draw a single character rectangle (5x10 block). */
static void ruler_draw_char(lvg_canvas_t *canvas, int x, int y,
                             lvg_color_t color)
{
    lvg_canvas_fill_rect(canvas, x, y, RULER_CHAR_W, RULER_CHAR_H, color);
}

/* Draw a number label at pixel position. */
static void ruler_draw_label(lvg_canvas_t *canvas, int x, int y,
                              float value, lvg_color_t color)
{
    char buf[16];
    /* Use integer display for round numbers, one decimal otherwise */
    if (value == (int)value) {
        snprintf(buf, sizeof(buf), "%d", (int)value);
    } else {
        snprintf(buf, sizeof(buf), "%.1f", value);
    }
    int len = (int)strlen(buf);
    for (int i = 0; i < len; i++) {
        ruler_draw_char(canvas, x + i * RULER_CHAR_ADV, y, color);
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int ruler_measure(const lui_widget_t *w, int *out_w, int *out_h,
                          void *user)
{
    const lui_ruler_t *r = (const lui_ruler_t *)w;
    (void)user;

    if (r->orientation == LUI_RULER_HORIZONTAL) {
        *out_w = 200;
        *out_h = r->ruler_thickness;
    } else {
        *out_w = r->ruler_thickness;
        *out_h = 200;
    }
    return 0;
}

static void ruler_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_ruler_t *r = (lui_ruler_t *)w;
    lvg_rect_t rect = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&rect)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, rect.x, rect.y,
                          rect.width, rect.height, r->bg_color);

    /* Border along the inner edge */
    if (r->orientation == LUI_RULER_HORIZONTAL) {
        lvg_canvas_fill_rect(canvas, rect.x, rect.y + rect.height - 1,
                              rect.width, 1, r->border_color);
    } else {
        lvg_canvas_fill_rect(canvas, rect.x + rect.width - 1, rect.y,
                              1, rect.height, r->border_color);
    }

    float major = ruler_adapted_interval(r);
    if (major <= 0.0f) return;

    float minor_step = major / (float)(r->minor_divisions > 0 ? r->minor_divisions : 1);
    float micro_step = minor_step / (float)(r->micro_divisions > 0 ? r->micro_divisions : 1);

    int thickness = r->ruler_thickness;

    /* Determine visible world range */
    float vis_start, vis_end;
    if (r->orientation == LUI_RULER_HORIZONTAL) {
        vis_start = r->origin;
        vis_end   = r->origin + (float)rect.width / r->scale;
    } else {
        vis_start = r->origin;
        vis_end   = r->origin + (float)rect.height / r->scale;
    }

    /* Clamp to user range */
    if (vis_start < r->range_min) vis_start = r->range_min;
    if (vis_end   > r->range_max) vis_end   = r->range_max;

    /* Draw micro ticks */
    if (micro_step > 0.0f && micro_step * r->scale >= 3.0f) {
        float start = ((int)(vis_start / micro_step)) * micro_step;
        for (float t = start; t <= vis_end; t += micro_step) {
            int px = ruler_world_to_pixel(r, t);
            int tick_len = thickness / 4;
            if (r->orientation == LUI_RULER_HORIZONTAL) {
                int sx = rect.x + px;
                if (sx < rect.x || sx >= rect.x + rect.width) continue;
                lvg_canvas_draw_line(canvas, sx, rect.y,
                                      sx, rect.y + tick_len,
                                      r->tick_color, 1);
            } else {
                int sy = rect.y + px;
                if (sy < rect.y || sy >= rect.y + rect.height) continue;
                lvg_canvas_draw_line(canvas, rect.x, sy,
                                      rect.x + tick_len, sy,
                                      r->tick_color, 1);
            }
        }
    }

    /* Draw minor ticks */
    if (minor_step > 0.0f && minor_step * r->scale >= 3.0f) {
        float start = ((int)(vis_start / minor_step)) * minor_step;
        for (float t = start; t <= vis_end; t += minor_step) {
            int px = ruler_world_to_pixel(r, t);
            int tick_len = thickness / 2;
            if (r->orientation == LUI_RULER_HORIZONTAL) {
                int sx = rect.x + px;
                if (sx < rect.x || sx >= rect.x + rect.width) continue;
                lvg_canvas_draw_line(canvas, sx, rect.y,
                                      sx, rect.y + tick_len,
                                      r->tick_color, 1);
            } else {
                int sy = rect.y + px;
                if (sy < rect.y || sy >= rect.y + rect.height) continue;
                lvg_canvas_draw_line(canvas, rect.x, sy,
                                      rect.x + tick_len, sy,
                                      r->tick_color, 1);
            }
        }
    }

    /* Draw major ticks + labels */
    {
        float start = ((int)(vis_start / major)) * major;
        for (float t = start; t <= vis_end; t += major) {
            int px = ruler_world_to_pixel(r, t);
            if (r->orientation == LUI_RULER_HORIZONTAL) {
                int sx = rect.x + px;
                if (sx < rect.x || sx >= rect.x + rect.width) continue;
                /* Full-height tick */
                lvg_canvas_draw_line(canvas, sx, rect.y,
                                      sx, rect.y + thickness - 1,
                                      r->tick_color, 1);
                /* Label just below the tick */
                ruler_draw_label(canvas, sx + 2,
                                  rect.y + thickness - RULER_CHAR_H - 2,
                                  t, r->text_color);
            } else {
                int sy = rect.y + px;
                if (sy < rect.y || sy >= rect.y + rect.height) continue;
                /* Full-width tick */
                lvg_canvas_draw_line(canvas, rect.x, sy,
                                      rect.x + thickness - 1, sy,
                                      r->tick_color, 1);
                /* Label to the right of the tick */
                ruler_draw_label(canvas,
                                  rect.x + thickness - RULER_CHAR_ADV * 3 - 2,
                                  sy + 2,
                                  t, r->text_color);
            }
        }
    }

    /* Cursor indicator */
    if (r->show_cursor) {
        float cpos = ruler_clampf(r->cursor_pos, r->range_min, r->range_max);
        int cpx = ruler_world_to_pixel(r, cpos);
        if (r->orientation == LUI_RULER_HORIZONTAL) {
            int sx = rect.x + cpx;
            if (sx >= rect.x && sx < rect.x + rect.width) {
                lvg_canvas_draw_line(canvas, sx, rect.y,
                                      sx, rect.y + thickness,
                                      r->cursor_color, 2);
            }
        } else {
            int sy = rect.y + cpx;
            if (sy >= rect.y && sy < rect.y + rect.height) {
                lvg_canvas_draw_line(canvas, rect.x, sy,
                                      rect.x + thickness, sy,
                                      r->cursor_color, 2);
            }
        }
    }
}

static int ruler_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_ruler_t *r = (lui_ruler_t *)w;

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        lvg_rect_t rect = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        if (lvg_rect_contains_point(&rect, mx, my)) {
            float world;
            if (r->orientation == LUI_RULER_HORIZONTAL) {
                world = r->origin + (float)(mx - rect.x) / r->scale;
            } else {
                world = r->origin + (float)(my - rect.y) / r->scale;
            }
            r->cursor_pos = world;
            r->show_cursor = true;
            return 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_ruler_init(lui_ruler_t *r, lui_ruler_orientation_t orientation)
{
    if (!r) return;

    lui_widget_init(&r->widget);
    r->widget.measure  = ruler_measure;
    r->widget.draw     = ruler_draw;
    r->widget.on_event = ruler_event;

    r->orientation = orientation;
    r->origin      = 0.0f;
    r->scale       = 1.0f;
    r->range_min   = 0.0f;
    r->range_max   = 1000.0f;

    r->major_interval  = 100.0f;
    r->minor_divisions = 5;
    r->micro_divisions = 2;

    r->ruler_thickness = RULER_DEFAULT_THICKNESS;

    r->cursor_pos  = 0.0f;
    r->show_cursor = false;

    r->bg_color     = LVG_COLOR_RGB(0x2B, 0x2D, 0x30);
    r->tick_color   = LVG_COLOR_RGB(0xA0, 0xA0, 0xA0);
    r->text_color   = LVG_COLOR_RGB(0xC0, 0xC0, 0xC0);
    r->cursor_color = LVG_COLOR_RGB(0xE0, 0x50, 0x50);
    r->border_color = LVG_COLOR_RGB(0x50, 0x52, 0x56);

    if (orientation == LUI_RULER_HORIZONTAL) {
        r->widget.width  = lvg_size_fill(1);
        r->widget.height = lvg_size_fixed(RULER_DEFAULT_THICKNESS);
    } else {
        r->widget.width  = lvg_size_fixed(RULER_DEFAULT_THICKNESS);
        r->widget.height = lvg_size_fill(1);
    }
}

void lui_ruler_set_range(lui_ruler_t *r, float min_val, float max_val)
{
    if (!r) return;
    r->range_min = min_val;
    r->range_max = max_val;
}

void lui_ruler_set_origin(lui_ruler_t *r, float origin)
{
    if (!r) return;
    r->origin = origin;
}

void lui_ruler_set_scale(lui_ruler_t *r, float scale)
{
    if (!r) return;
    if (scale > 0.0f) r->scale = scale;
}

void lui_ruler_set_cursor(lui_ruler_t *r, float pos)
{
    if (!r) return;
    r->cursor_pos  = pos;
    r->show_cursor = true;
}
