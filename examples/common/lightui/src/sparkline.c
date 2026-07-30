/*
 * sparkline.c — Tiny inline chart widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/sparkline.h>
#include <lightvg/canvas.h>

#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline float sp_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void sp_compute_range(const lui_sparkline_t *sp, float *out_min,
                               float *out_max)
{
    float lo = sp->min_val;
    float hi = sp->max_val;

    /* Auto-range if both are zero */
    if (lo == 0.0f && hi == 0.0f && sp->value_count > 0) {
        lo = sp->values[0];
        hi = sp->values[0];
        for (int i = 1; i < sp->value_count; i++) {
            if (sp->values[i] < lo) lo = sp->values[i];
            if (sp->values[i] > hi) hi = sp->values[i];
        }
        /* Ensure non-zero range */
        if (hi <= lo) {
            hi = lo + 1.0f;
        }
    }

    if (hi <= lo) hi = lo + 1.0f;

    *out_min = lo;
    *out_max = hi;
}

/* Map a value to a Y pixel (top = max, bottom = min) */
static inline int sp_val_to_y(float v, float lo, float hi,
                                int plot_y, int plot_h)
{
    float frac = (v - lo) / (hi - lo);
    frac = sp_clampf(frac, 0.0f, 1.0f);
    return plot_y + plot_h - 1 - (int)(frac * (plot_h - 1));
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int sp_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)w; (void)user;
    *out_w = 80;
    *out_h = 20;
    return 0;
}

static void sp_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_sparkline_t *sp = (lui_sparkline_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, sp->bg_color);

    if (sp->value_count <= 0) return;

    int plot_x = r.x + 1;
    int plot_y = r.y + 1;
    int plot_w = r.width - 2;
    int plot_h = r.height - 2;
    if (plot_w <= 0 || plot_h <= 0) return;

    float lo, hi;
    sp_compute_range(sp, &lo, &hi);

    /* Reference line (dashed) */
    if (sp->show_reference) {
        int ref_y = sp_val_to_y(sp->reference_value, lo, hi, plot_y, plot_h);
        /* Draw dashed line: 3px on, 3px off */
        for (int x = plot_x; x < plot_x + plot_w; x += 6) {
            int seg = x + 3;
            if (seg > plot_x + plot_w) seg = plot_x + plot_w;
            lvg_canvas_fill_rect(canvas, x, ref_y, seg - x, 1,
                                  sp->reference_color);
        }
    }

    if (sp->style == LUI_SPARKLINE_BAR) {
        /* Bar chart */
        float bar_w_f = (float)plot_w / sp->value_count;
        int ref_y = plot_y + plot_h - 1;
        if (sp->show_reference)
            ref_y = sp_val_to_y(sp->reference_value, lo, hi, plot_y, plot_h);

        for (int i = 0; i < sp->value_count; i++) {
            int bx = plot_x + (int)(i * bar_w_f);
            int bw = (int)((i + 1) * bar_w_f) - (int)(i * bar_w_f);
            if (bw < 1) bw = 1;

            int vy = sp_val_to_y(sp->values[i], lo, hi, plot_y, plot_h);
            lvg_color_t col = sp->line_color;

            if (sp->show_reference &&
                (sp->positive_color != 0 || sp->negative_color != 0)) {
                if (sp->values[i] >= sp->reference_value && sp->positive_color != 0)
                    col = sp->positive_color;
                else if (sp->values[i] < sp->reference_value && sp->negative_color != 0)
                    col = sp->negative_color;
            }

            int y_top, bar_h;
            if (vy <= ref_y) {
                y_top = vy;
                bar_h = ref_y - vy;
            } else {
                y_top = ref_y;
                bar_h = vy - ref_y;
            }
            if (bar_h < 1) bar_h = 1;
            lvg_canvas_fill_rect(canvas, bx, y_top, bw, bar_h, col);
        }
    } else {
        /* LINE or AREA style */
        /* Compute pixel Y for each data point */
        int n = sp->value_count;
        /* Use stack allocation for small counts; cap at reasonable size */
        int y_vals[LUI_SPARKLINE_MAX];
        int x_vals[LUI_SPARKLINE_MAX];

        for (int i = 0; i < n && i < LUI_SPARKLINE_MAX; i++) {
            x_vals[i] = plot_x + (int)((float)i * (plot_w - 1) / (n > 1 ? n - 1 : 1));
            y_vals[i] = sp_val_to_y(sp->values[i], lo, hi, plot_y, plot_h);
        }

        /* Area fill */
        if (sp->style == LUI_SPARKLINE_AREA || sp->show_area) {
            int base_y = plot_y + plot_h - 1;
            if (sp->show_reference)
                base_y = sp_val_to_y(sp->reference_value, lo, hi, plot_y, plot_h);

            for (int i = 0; i < n - 1; i++) {
                int x0 = x_vals[i];
                int x1 = x_vals[i + 1];
                for (int px = x0; px <= x1 && px < plot_x + plot_w; px++) {
                    /* Interpolate Y */
                    float t = (x1 > x0) ? (float)(px - x0) / (float)(x1 - x0) : 0.0f;
                    int iy = y_vals[i] + (int)(t * (y_vals[i + 1] - y_vals[i]));

                    int fy_top, fy_h;
                    if (iy <= base_y) {
                        fy_top = iy;
                        fy_h = base_y - iy;
                    } else {
                        fy_top = base_y;
                        fy_h = iy - base_y;
                    }
                    if (fy_h < 1) fy_h = 1;

                    lvg_color_t acol = sp->area_color;
                    if (sp->show_reference &&
                        (sp->positive_color != 0 || sp->negative_color != 0)) {
                        /* Determine color based on whether above/below reference */
                        float t_val = sp->values[i] + t * (sp->values[i + 1] - sp->values[i]);
                        if (t_val >= sp->reference_value && sp->positive_color != 0)
                            acol = LVG_COLOR_ARGB(0x40,
                                LVG_COLOR_R(sp->positive_color),
                                LVG_COLOR_G(sp->positive_color),
                                LVG_COLOR_B(sp->positive_color));
                        else if (t_val < sp->reference_value && sp->negative_color != 0)
                            acol = LVG_COLOR_ARGB(0x40,
                                LVG_COLOR_R(sp->negative_color),
                                LVG_COLOR_G(sp->negative_color),
                                LVG_COLOR_B(sp->negative_color));
                    }

                    lvg_canvas_fill_rect(canvas, px, fy_top, 1, fy_h, acol);
                }
            }
        }

        /* Line segments */
        for (int i = 0; i < n - 1; i++) {
            lvg_color_t col = sp->line_color;
            if (sp->show_reference &&
                (sp->positive_color != 0 || sp->negative_color != 0)) {
                float avg = (sp->values[i] + sp->values[i + 1]) * 0.5f;
                if (avg >= sp->reference_value && sp->positive_color != 0)
                    col = sp->positive_color;
                else if (avg < sp->reference_value && sp->negative_color != 0)
                    col = sp->negative_color;
            }
            lvg_canvas_draw_line(canvas, x_vals[i], y_vals[i],
                                  x_vals[i + 1], y_vals[i + 1], col, 1);
        }

        /* Endpoint dot */
        if (sp->show_endpoint && n > 0) {
            int ex = x_vals[n - 1];
            int ey = y_vals[n - 1];
            lvg_canvas_fill_circle(canvas, ex, ey, 2, sp->endpoint_color);
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_sparkline_init(lui_sparkline_t *sp, lui_sparkline_style_t style)
{
    if (!sp) return;

    lui_widget_init(&sp->widget);
    sp->widget.width    = lvg_size_hug(80);
    sp->widget.height   = lvg_size_hug(20);
    sp->widget.measure  = sp_measure;
    sp->widget.draw     = sp_draw;
    sp->widget.on_event = NULL;

    memset(sp->values, 0, sizeof(sp->values));
    sp->value_count = 0;

    sp->min_val = 0.0f;
    sp->max_val = 0.0f;

    sp->style         = style;
    sp->show_area     = (style == LUI_SPARKLINE_AREA);
    sp->show_endpoint = true;

    sp->reference_value = 0.0f;
    sp->show_reference  = false;

    sp->line_color      = LVG_COLOR_RGB(0x58, 0xB0, 0xE0);
    sp->area_color      = LVG_COLOR_ARGB(0x30, 0x58, 0xB0, 0xE0);
    sp->endpoint_color  = LVG_COLOR_RGB(0xE0, 0xE4, 0xEC);
    sp->bg_color        = LVG_COLOR_TRANSPARENT;
    sp->reference_color = LVG_COLOR_RGB(0x60, 0x64, 0x6C);
    sp->positive_color  = 0;  /* 0 = not set, use line_color */
    sp->negative_color  = 0;
}

void lui_sparkline_set_data(lui_sparkline_t *sp, const float *values, int count)
{
    if (!sp || !values) return;
    if (count > LUI_SPARKLINE_MAX) count = LUI_SPARKLINE_MAX;
    if (count < 0) count = 0;
    memcpy(sp->values, values, count * sizeof(float));
    sp->value_count = count;
}

void lui_sparkline_set_range(lui_sparkline_t *sp, float min_val, float max_val)
{
    if (!sp) return;
    sp->min_val = min_val;
    sp->max_val = max_val;
}

void lui_sparkline_set_reference(lui_sparkline_t *sp, float value)
{
    if (!sp) return;
    sp->reference_value = value;
    sp->show_reference  = true;
}

void lui_sparkline_push(lui_sparkline_t *sp, float value)
{
    if (!sp) return;

    if (sp->value_count < LUI_SPARKLINE_MAX) {
        sp->values[sp->value_count++] = value;
    } else {
        /* Shift left and append */
        memmove(sp->values, sp->values + 1,
                (LUI_SPARKLINE_MAX - 1) * sizeof(float));
        sp->values[LUI_SPARKLINE_MAX - 1] = value;
    }
    lui_widget_invalidate(&sp->widget);
}
