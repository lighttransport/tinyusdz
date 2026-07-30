/*
 * progress.c — Progress bar and circular gauge widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/progress.h>
#include <lightvg/canvas.h>

#include <math.h>
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

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int progress_measure(const lui_widget_t *w, int *out_w, int *out_h,
                              void *user)
{
    const lui_progress_t *p = (const lui_progress_t *)w;
    (void)user;
    if (p->style == LUI_PROGRESS_CIRCULAR) {
        *out_w = 48;
        *out_h = 48;
    } else {
        *out_w = 200;
        *out_h = p->bar_height;
    }
    return 0;
}

static void draw_bar(lui_progress_t *p, lvg_canvas_t *canvas, lvg_rect_t r)
{
    int cr = p->corner_radius;
    int bar_y = r.y + (r.height - p->bar_height) / 2;
    int bar_h = p->bar_height;

    /* Track background */
    lvg_canvas_fill_rounded_rect(canvas, r.x, bar_y, r.width, bar_h, cr,
                                  p->bg);

    if (p->indeterminate) {
        /* Animated bouncing indicator */
        int ind_w = r.width / 4;
        if (ind_w < 20) ind_w = 20;
        float pos = p->anim_phase;
        /* Ping-pong: 0..1 maps to 0..max..0 */
        float t = pos * 2.0f;
        if (t > 1.0f) t = 2.0f - t;
        int ind_x = r.x + (int)(t * (float)(r.width - ind_w));
        lvg_canvas_fill_rounded_rect(canvas, ind_x, bar_y, ind_w, bar_h,
                                      cr, p->fill);
    } else if (p->segment_count > 0) {
        /* Stacked segments */
        int fill_x = r.x;
        for (int i = 0; i < p->segment_count; i++) {
            float sv = clampf(p->segments[i].value, 0.0f, 1.0f);
            int sw = (int)(sv * r.width);
            if (sw > 0) {
                lvg_canvas_fill_rounded_rect(canvas, fill_x, bar_y, sw,
                                              bar_h, cr,
                                              p->segments[i].color);
                fill_x += sw;
            }
        }
    } else {
        /* Single value fill */
        float v = clampf(p->value, 0.0f, 1.0f);
        int fill_w = (int)(v * r.width);
        if (fill_w > 0) {
            lvg_canvas_fill_rounded_rect(canvas, r.x, bar_y, fill_w, bar_h,
                                          cr, p->fill);
        }
    }

    /* Border */
    lvg_canvas_stroke_rounded_rect(canvas, r.x, bar_y, r.width, bar_h, cr,
                                    p->border_color,
                                    p->border_width > 0 ? p->border_width : 1);

    /* Percentage text */
    if (p->show_text && !p->indeterminate) {
        float v = clampf(p->value, 0.0f, 1.0f);
        int pct = (int)(v * 100.0f + 0.5f);
        /* Draw "XX%" as character rectangles */
        int len = 0;
        if (pct >= 100) len = 4;
        else if (pct >= 10) len = 3;
        else len = 2;

        int tw = len * 7;
        int tx = r.x + (r.width - tw) / 2;
        int ty = bar_y + (bar_h - 10) / 2;
        for (int c = 0; c < len; c++)
            lvg_canvas_fill_rect(canvas, tx + c * 7, ty, 5, 10, p->text_color);
    }
}

static void draw_circular(lui_progress_t *p, lvg_canvas_t *canvas,
                            lvg_rect_t r)
{
    int sz = r.width < r.height ? r.width : r.height;
    int cx = r.x + r.width / 2;
    int cy = r.y + r.height / 2;
    int outer_r = sz / 2 - 1;
    int inner_r = outer_r - p->arc_width;
    if (inner_r < 4) inner_r = 4;

    /* Track ring (full circle background) */
    lvg_canvas_stroke_circle(canvas, cx, cy, outer_r, p->bg, p->arc_width);

    /* Fill arc — approximate by drawing small filled rects along the arc */
    float v = clampf(p->indeterminate ? 0.25f : p->value, 0.0f, 1.0f);
    float start_angle = p->indeterminate ? p->anim_phase * 6.2831853f : -1.5707963f;
    float sweep = v * 6.2831853f;
    int steps = (int)(sweep * (float)outer_r * 0.5f);
    if (steps < 4) steps = 4;
    if (steps > 200) steps = 200;

    for (int s = 0; s <= steps; s++) {
        float angle = start_angle + sweep * (float)s / (float)steps;
        float ca = cosf(angle);
        float sa = sinf(angle);
        int mid_r = (outer_r + inner_r) / 2;
        int px = cx + (int)(ca * mid_r);
        int py = cy + (int)(sa * mid_r);
        int dot = p->arc_width;
        lvg_canvas_fill_rect(canvas, px - dot / 2, py - dot / 2,
                              dot, dot, p->fill);
    }

    /* Percentage text in center */
    if (p->show_text && !p->indeterminate) {
        int pct = (int)(clampf(p->value, 0.0f, 1.0f) * 100.0f + 0.5f);
        int len = 0;
        if (pct >= 100) len = 4;
        else if (pct >= 10) len = 3;
        else len = 2;

        int tw = len * 7;
        int tx = cx - tw / 2;
        int ty = cy - 5;
        for (int c = 0; c < len; c++)
            lvg_canvas_fill_rect(canvas, tx + c * 7, ty, 5, 10, p->text_color);
    }
}

static void progress_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_progress_t *p = (lui_progress_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    if (p->style == LUI_PROGRESS_CIRCULAR)
        draw_circular(p, canvas, r);
    else
        draw_bar(p, canvas, r);
}

static bool progress_animate(lui_widget_t *w, float dt)
{
    lui_progress_t *p = (lui_progress_t *)w;
    if (!p->indeterminate) return false;
    p->anim_phase += dt * 0.8f;
    if (p->anim_phase >= 1.0f)
        p->anim_phase -= 1.0f;
    return true;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_progress_init(lui_progress_t *p)
{
    if (!p) return;

    lui_widget_init(&p->widget);
    p->widget.width   = lvg_size_fill(1);
    p->widget.height  = lvg_size_hug(0);
    p->widget.measure = progress_measure;
    p->widget.draw    = progress_draw;
    p->widget.animate = progress_animate;
    p->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    p->value          = 0.0f;
    p->indeterminate  = false;
    p->segment_count  = 0;
    p->style          = LUI_PROGRESS_BAR;
    p->bar_height     = 12;
    p->arc_width      = 6;
    p->corner_radius  = 4;
    p->border_width   = 1;
    p->anim_phase     = 0.0f;

    p->bg             = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    p->fill           = LVG_COLOR_RGB(0x44, 0x8E, 0xD0);
    p->border_color   = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    p->text_color     = LVG_COLOR_RGB(0xE0, 0xE3, 0xE7);
    p->show_text      = false;
}

void lui_progress_set_value(lui_progress_t *p, float value)
{
    if (!p) return;
    float v = clampf(value, 0.0f, 1.0f);
    if (v != p->value) {
        p->value = v;
        lui_widget_invalidate(&p->widget);
    }
}

int lui_progress_add_segment(lui_progress_t *p, float value,
                              lvg_color_t color)
{
    if (!p || p->segment_count >= LUI_PROGRESS_MAX_SEGMENTS) return -1;
    int idx = p->segment_count;
    p->segments[idx].value = clampf(value, 0.0f, 1.0f);
    p->segments[idx].color = color;
    p->segment_count++;
    return idx;
}

void lui_progress_set_segment(lui_progress_t *p, int index, float value)
{
    if (!p || index < 0 || index >= p->segment_count) return;
    p->segments[index].value = clampf(value, 0.0f, 1.0f);
}

void lui_progress_clear_segments(lui_progress_t *p)
{
    if (!p) return;
    p->segment_count = 0;
}

bool lui_progress_animate(lui_progress_t *p, float dt)
{
    if (!p || !p->indeterminate) return false;
    /* Ensure the ANIMATING flag is set for tree-based animation */
    p->widget.flags |= LUI_WIDGET_ANIMATING;
    p->anim_phase += dt * 0.8f;  /* ~0.8 Hz cycle */
    if (p->anim_phase >= 1.0f)
        p->anim_phase -= 1.0f;
    lui_widget_invalidate(&p->widget);
    return true;
}
