/*
 * plot.c — Plot / chart widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/plot.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Character-rectangle text drawing: each char is a 5x10 filled rect,
   spaced 7px apart horizontally.  Returns the total width drawn. */
static int draw_text_rects(lvg_canvas_t *canvas, int x, int y,
                           const char *text, lvg_color_t color)
{
    if (!text) return 0;
    int tx = x;
    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, tx, y, 5, 10, color);
        tx += 7;
    }
    return tx - x;
}

/* Measure width of text in character rectangles (7px per char). */
static int text_width(const char *text)
{
    if (!text) return 0;
    return (int)strlen(text) * 7;
}

/* Compute a "nice" tick step for an axis range. */
static float nice_step(float range, int max_ticks)
{
    if (range <= 0.0f || max_ticks < 1) return 1.0f;
    float rough = range / (float)max_ticks;
    float mag = powf(10.0f, floorf(log10f(rough)));
    float norm = rough / mag;
    float step;
    if (norm < 1.5f)      step = 1.0f * mag;
    else if (norm < 3.5f) step = 2.0f * mag;
    else if (norm < 7.5f) step = 5.0f * mag;
    else                   step = 10.0f * mag;
    return step;
}

/* Map a data value to pixel coordinate in the plot area. */
static int map_x(float val, float xmin, float xmax, int px_left, int px_width)
{
    if (xmax <= xmin) return px_left;
    float t = (val - xmin) / (xmax - xmin);
    return px_left + (int)(t * (float)px_width);
}

static int map_y(float val, float ymin, float ymax, int px_top, int px_height)
{
    if (ymax <= ymin) return px_top;
    float t = (val - ymin) / (ymax - ymin);
    return px_top + px_height - (int)(t * (float)px_height);
}

/* Inverse map: pixel -> data value. */
static float unmap_x(int px, float xmin, float xmax, int px_left, int px_width)
{
    if (px_width <= 0) return xmin;
    float t = (float)(px - px_left) / (float)px_width;
    return xmin + t * (xmax - xmin);
}

static float unmap_y(int py, float ymin, float ymax, int px_top, int px_height)
{
    if (px_height <= 0) return ymin;
    float t = (float)(px_top + px_height - py) / (float)px_height;
    return ymin + t * (ymax - ymin);
}

/* Compute effective axis ranges, applying auto-range from visible data. */
static void compute_ranges(const lui_plot_t *plot,
                           float *xmin, float *xmax,
                           float *ymin, float *ymax)
{
    bool need_x = plot->x_range.auto_range;
    bool need_y = plot->y_range.auto_range;

    if (!need_x) {
        *xmin = plot->x_range.min;
        *xmax = plot->x_range.max;
    }
    if (!need_y) {
        *ymin = plot->y_range.min;
        *ymax = plot->y_range.max;
    }

    if (need_x || need_y) {
        float ax0 = 0.0f, ax1 = 1.0f, ay0 = 0.0f, ay1 = 1.0f;
        bool first = true;
        for (int s = 0; s < plot->series_count; s++) {
            const lui_plot_series_t *ser = &plot->series[s];
            if (!ser->visible || ser->point_count == 0) continue;
            for (int i = 0; i < ser->point_count; i++) {
                float px = ser->points[i].x;
                float py = ser->points[i].y;
                if (first) {
                    ax0 = ax1 = px;
                    ay0 = ay1 = py;
                    first = false;
                } else {
                    if (px < ax0) ax0 = px;
                    if (px > ax1) ax1 = px;
                    if (py < ay0) ay0 = py;
                    if (py > ay1) ay1 = py;
                }
            }
        }
        /* Add 5% margin */
        float xpad = (ax1 - ax0) * 0.05f;
        float ypad = (ay1 - ay0) * 0.05f;
        if (xpad == 0.0f) xpad = 0.5f;
        if (ypad == 0.0f) ypad = 0.5f;
        if (need_x) { *xmin = ax0 - xpad; *xmax = ax1 + xpad; }
        if (need_y) { *ymin = ay0 - ypad; *ymax = ay1 + ypad; }
    }

    /* Ensure non-degenerate */
    if (*xmax <= *xmin) *xmax = *xmin + 1.0f;
    if (*ymax <= *ymin) *ymax = *ymin + 1.0f;
}

/* Format a float value into a short label buffer.  Returns pointer to buf. */
static char *fmt_float(char *buf, int buf_size, float val)
{
    /* Simple formatting: use integer if close enough, else 1 or 2 decimals */
    float av = fabsf(val);
    if (av < 1e-6f) {
        buf[0] = '0'; buf[1] = '\0';
    } else if (fabsf(val - (float)(int)val) < 1e-4f) {
        int iv = (int)val;
        int neg = iv < 0;
        if (neg) iv = -iv;
        int pos = 0;
        char tmp[32];
        if (iv == 0) { tmp[pos++] = '0'; }
        else { while (iv > 0 && pos < 30) { tmp[pos++] = (char)('0' + iv % 10); iv /= 10; } }
        int out = 0;
        if (neg && out < buf_size - 1) buf[out++] = '-';
        for (int i = pos - 1; i >= 0 && out < buf_size - 1; i--)
            buf[out++] = tmp[i];
        buf[out] = '\0';
    } else {
        /* Manual formatting with 1 decimal */
        int neg = val < 0.0f;
        float a = fabsf(val);
        int whole = (int)a;
        int frac = (int)((a - (float)whole) * 10.0f + 0.5f);
        if (frac >= 10) { whole++; frac = 0; }
        int out = 0;
        char tmp[32];
        int pos = 0;
        if (whole == 0) { tmp[pos++] = '0'; }
        else { int w = whole; while (w > 0 && pos < 28) { tmp[pos++] = (char)('0' + w % 10); w /= 10; } }
        if (neg && out < buf_size - 1) buf[out++] = '-';
        for (int i = pos - 1; i >= 0 && out < buf_size - 1; i--)
            buf[out++] = tmp[i];
        if (out < buf_size - 1) buf[out++] = '.';
        if (out < buf_size - 1) buf[out++] = (char)('0' + frac);
        buf[out] = '\0';
    }
    return buf;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int plot_measure(const lui_widget_t *w, int *out_w, int *out_h,
                        void *user)
{
    (void)w; (void)user;
    *out_w = 400;
    *out_h = 300;
    return 0;
}

static void plot_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_plot_t *plot = (lui_plot_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Save / set clip */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t clip = lvg_rect_intersect(&old_clip, &r);
    canvas->_clip = clip;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, plot->bg);

    /* Plot area bounds */
    int pa_x = r.x + plot->margin_left;
    int pa_y = r.y + plot->margin_top;
    int pa_w = r.width - plot->margin_left - plot->margin_right;
    int pa_h = r.height - plot->margin_top - plot->margin_bottom;
    if (pa_w < 10 || pa_h < 10) { canvas->_clip = old_clip; return; }

    /* Plot area background */
    lvg_canvas_fill_rect(canvas, pa_x, pa_y, pa_w, pa_h, plot->plot_bg);

    /* Compute axis ranges */
    float xmin, xmax, ymin, ymax;
    compute_ranges(plot, &xmin, &xmax, &ymin, &ymax);

    /* ---- Grid lines ---- */

    /* Major grid */
    float x_step = nice_step(xmax - xmin, pa_w / 80);
    float y_step = nice_step(ymax - ymin, pa_h / 50);

    /* Minor grid (5 subdivisions) */
    float x_minor = x_step / 5.0f;
    float y_minor = y_step / 5.0f;

    /* Draw minor grid */
    {
        float v = floorf(xmin / x_minor) * x_minor;
        for (; v <= xmax; v += x_minor) {
            int px = map_x(v, xmin, xmax, pa_x, pa_w);
            if (px >= pa_x && px <= pa_x + pa_w)
                lvg_canvas_fill_rect(canvas, px, pa_y, 1, pa_h, plot->grid_minor);
        }
    }
    {
        float v = floorf(ymin / y_minor) * y_minor;
        for (; v <= ymax; v += y_minor) {
            int py = map_y(v, ymin, ymax, pa_y, pa_h);
            if (py >= pa_y && py <= pa_y + pa_h)
                lvg_canvas_fill_rect(canvas, pa_x, py, pa_w, 1, plot->grid_minor);
        }
    }

    /* Draw major grid + axis labels */
    {
        float v = floorf(xmin / x_step) * x_step;
        for (; v <= xmax; v += x_step) {
            int px = map_x(v, xmin, xmax, pa_x, pa_w);
            if (px >= pa_x && px <= pa_x + pa_w)
                lvg_canvas_fill_rect(canvas, px, pa_y, 1, pa_h, plot->grid_major);
            /* X axis label below plot */
            char buf[32];
            fmt_float(buf, (int)sizeof(buf), v);
            int tw = text_width(buf);
            int lx = px - tw / 2;
            int ly = pa_y + pa_h + 4;
            if (ly + 10 <= r.y + r.height)
                draw_text_rects(canvas, lx, ly, buf, plot->text_color);
        }
    }
    {
        float v = floorf(ymin / y_step) * y_step;
        for (; v <= ymax; v += y_step) {
            int py = map_y(v, ymin, ymax, pa_y, pa_h);
            if (py >= pa_y && py <= pa_y + pa_h)
                lvg_canvas_fill_rect(canvas, pa_x, py, pa_w, 1, plot->grid_major);
            /* Y axis label left of plot */
            char buf[32];
            fmt_float(buf, (int)sizeof(buf), v);
            int tw = text_width(buf);
            int lx = pa_x - tw - 4;
            int ly = py - 5;
            if (lx >= r.x)
                draw_text_rects(canvas, lx, ly, buf, plot->text_color);
        }
    }

    /* Axis border */
    lvg_canvas_stroke_rect(canvas, pa_x, pa_y, pa_w, pa_h,
                           plot->axis_color, 1);

    /* ---- Axis titles ---- */
    if (plot->x_title) {
        int tw = text_width(plot->x_title);
        int tx = pa_x + (pa_w - tw) / 2;
        int ty = pa_y + pa_h + 18;
        if (ty + 10 <= r.y + r.height)
            draw_text_rects(canvas, tx, ty, plot->x_title, plot->text_color);
    }
    if (plot->y_title) {
        /* Draw Y title vertically (stacked chars) to the left */
        int len = (int)strlen(plot->y_title);
        int total_h = len * 14;
        int ty = pa_y + (pa_h - total_h) / 2;
        int tx = r.x + 2;
        for (int i = 0; i < len; i++) {
            char ch[2] = { plot->y_title[i], '\0' };
            draw_text_rects(canvas, tx, ty + i * 14, ch, plot->text_color);
        }
    }

    /* ---- Clip to plot area for data drawing ---- */
    lvg_rect_t pa_rect = { pa_x, pa_y, pa_w, pa_h };
    lvg_rect_t pa_clip = lvg_rect_intersect(&clip, &pa_rect);
    canvas->_clip = pa_clip;

    /* ---- Draw series ---- */
    for (int s = 0; s < plot->series_count; s++) {
        const lui_plot_series_t *ser = &plot->series[s];
        if (!ser->visible || ser->point_count == 0) continue;

        switch (ser->type) {
        case LUI_PLOT_LINE: {
            if (ser->point_count < 2) break;
            /* Draw as connected line segments */
            for (int i = 0; i < ser->point_count - 1; i++) {
                int x0 = map_x(ser->points[i].x, xmin, xmax, pa_x, pa_w);
                int y0 = map_y(ser->points[i].y, ymin, ymax, pa_y, pa_h);
                int x1 = map_x(ser->points[i + 1].x, xmin, xmax, pa_x, pa_w);
                int y1 = map_y(ser->points[i + 1].y, ymin, ymax, pa_y, pa_h);
                lvg_canvas_draw_line(canvas, x0, y0, x1, y1, ser->color, 2);
            }
            break;
        }
        case LUI_PLOT_SCATTER: {
            for (int i = 0; i < ser->point_count; i++) {
                int cx = map_x(ser->points[i].x, xmin, xmax, pa_x, pa_w);
                int cy = map_y(ser->points[i].y, ymin, ymax, pa_y, pa_h);
                lvg_canvas_fill_circle(canvas, cx, cy, 3, ser->color);
            }
            break;
        }
        case LUI_PLOT_BAR: {
            int n = ser->point_count;
            if (n == 0) break;
            /* Bar width based on data spacing and series count */
            float bar_data_w = (xmax - xmin) / (float)(n > 1 ? n : 1) * 0.7f;
            int bar_px_w = (int)(bar_data_w / (xmax - xmin) * (float)pa_w);
            if (bar_px_w < 2) bar_px_w = 2;
            if (bar_px_w > pa_w / 2) bar_px_w = pa_w / 2;
            int baseline = map_y(0.0f, ymin, ymax, pa_y, pa_h);
            if (baseline < pa_y) baseline = pa_y;
            if (baseline > pa_y + pa_h) baseline = pa_y + pa_h;
            for (int i = 0; i < n; i++) {
                int cx = map_x(ser->points[i].x, xmin, xmax, pa_x, pa_w);
                int top = map_y(ser->points[i].y, ymin, ymax, pa_y, pa_h);
                int bx = cx - bar_px_w / 2;
                int by, bh;
                if (top < baseline) {
                    by = top; bh = baseline - top;
                } else {
                    by = baseline; bh = top - baseline;
                }
                if (bh < 1) bh = 1;
                lvg_canvas_fill_rect(canvas, bx, by, bar_px_w, bh, ser->color);
            }
            break;
        }
        case LUI_PLOT_AREA_FILL: {
            if (ser->point_count < 2) break;
            /* Draw filled area from baseline to data as vertical strips */
            int baseline = map_y(0.0f, ymin, ymax, pa_y, pa_h);
            if (baseline < pa_y) baseline = pa_y;
            if (baseline > pa_y + pa_h) baseline = pa_y + pa_h;
            /* Semi-transparent fill */
            lvg_color_t fill_col = LVG_COLOR_ARGB(0x40,
                LVG_COLOR_R(ser->color),
                LVG_COLOR_G(ser->color),
                LVG_COLOR_B(ser->color));
            for (int i = 0; i < ser->point_count - 1; i++) {
                int x0 = map_x(ser->points[i].x, xmin, xmax, pa_x, pa_w);
                int y0 = map_y(ser->points[i].y, ymin, ymax, pa_y, pa_h);
                int x1 = map_x(ser->points[i + 1].x, xmin, xmax, pa_x, pa_w);
                int y1 = map_y(ser->points[i + 1].y, ymin, ymax, pa_y, pa_h);
                /* Fill vertical strips between the two points */
                int sx = x0 < x1 ? x0 : x1;
                int ex = x0 < x1 ? x1 : x0;
                if (sx < pa_x) sx = pa_x;
                if (ex > pa_x + pa_w) ex = pa_x + pa_w;
                for (int px = sx; px <= ex; px++) {
                    float t = (ex > sx) ? (float)(px - x0) / (float)(x1 - x0) : 0.0f;
                    int py = y0 + (int)(t * (float)(y1 - y0));
                    int top_y = py < baseline ? py : baseline;
                    int bot_y = py < baseline ? baseline : py;
                    if (bot_y - top_y > 0)
                        lvg_canvas_fill_rect(canvas, px, top_y, 1,
                                             bot_y - top_y, fill_col);
                }
                /* Outline on top */
                lvg_canvas_draw_line(canvas, x0, y0, x1, y1, ser->color, 1);
            }
            break;
        }
        } /* switch */
    }

    /* ---- Crosshair cursor ---- */
    if (plot->show_crosshair) {
        int cx = plot->cursor_x;
        int cy = plot->cursor_y;
        if (cx >= pa_x && cx <= pa_x + pa_w &&
            cy >= pa_y && cy <= pa_y + pa_h) {
            /* Vertical line */
            lvg_canvas_fill_rect(canvas, cx, pa_y, 1, pa_h,
                                 plot->crosshair_color);
            /* Horizontal line */
            lvg_canvas_fill_rect(canvas, pa_x, cy, pa_w, 1,
                                 plot->crosshair_color);
            /* Coordinate label */
            float dx = unmap_x(cx, xmin, xmax, pa_x, pa_w);
            float dy = unmap_y(cy, ymin, ymax, pa_y, pa_h);
            char xbuf[32], ybuf[32];
            fmt_float(xbuf, (int)sizeof(xbuf), dx);
            fmt_float(ybuf, (int)sizeof(ybuf), dy);
            /* Build "x, y" label */
            char coord[80];
            int ci = 0;
            for (int i = 0; xbuf[i] && ci < 76; i++) coord[ci++] = xbuf[i];
            coord[ci++] = ','; coord[ci++] = ' ';
            for (int i = 0; ybuf[i] && ci < 78; i++) coord[ci++] = ybuf[i];
            coord[ci] = '\0';
            int tw = text_width(coord);
            /* Position label near cursor, offset to stay in view */
            int lx = cx + 8;
            int ly = cy - 14;
            if (lx + tw > pa_x + pa_w) lx = cx - tw - 8;
            if (ly < pa_y) ly = cy + 4;
            /* Background box */
            lvg_canvas_fill_rect(canvas, lx - 2, ly - 2,
                                 tw + 4, 14, plot->bg);
            draw_text_rects(canvas, lx, ly, coord, plot->crosshair_color);
        }
    }

    /* Restore clip to full widget for legend */
    canvas->_clip = clip;

    /* ---- Legend ---- */
    if (plot->show_legend && plot->series_count > 0) {
        int lw = 0;
        for (int s = 0; s < plot->series_count; s++) {
            int tw = text_width(plot->series[s].label);
            if (tw + 20 > lw) lw = tw + 20;  /* 12 swatch + 8 padding */
        }
        int lh = plot->series_count * 16 + 8;
        lw += 12;  /* extra padding */
        int lx = pa_x + pa_w - lw - 4;
        int ly = pa_y + 4;

        /* Legend background */
        lvg_canvas_fill_rounded_rect(canvas, lx, ly, lw, lh, 3,
                                     plot->legend_bg);
        lvg_canvas_stroke_rounded_rect(canvas, lx, ly, lw, lh, 3,
                                       plot->axis_color, 1);

        for (int s = 0; s < plot->series_count; s++) {
            const lui_plot_series_t *ser = &plot->series[s];
            int ey = ly + 4 + s * 16;
            /* Color swatch */
            lvg_color_t sc = ser->visible ? ser->color
                           : LVG_COLOR_RGB(0x50, 0x50, 0x50);
            lvg_canvas_fill_rect(canvas, lx + 6, ey + 2, 8, 8, sc);
            /* Label */
            lvg_color_t tc = ser->visible ? plot->text_color
                           : LVG_COLOR_RGB(0x60, 0x60, 0x60);
            draw_text_rects(canvas, lx + 20, ey + 1, ser->label, tc);
        }
    }

    canvas->_clip = old_clip;
}

static int plot_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_plot_t *plot = (lui_plot_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    /* Plot area */
    int pa_x = r.x + plot->margin_left;
    int pa_y = r.y + plot->margin_top;
    int pa_w = r.width - plot->margin_left - plot->margin_right;
    int pa_h = r.height - plot->margin_top - plot->margin_bottom;

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;

        /* Update crosshair */
        lvg_rect_t pa = { pa_x, pa_y, pa_w, pa_h };
        if (lvg_rect_contains_point(&pa, mx, my)) {
            plot->show_crosshair = true;
            plot->cursor_x = mx;
            plot->cursor_y = my;
        } else {
            plot->show_crosshair = false;
        }

        /* Pan */
        if (plot->panning) {
            float xmin = plot->pan_origin_xmin;
            float xmax = plot->pan_origin_xmax;
            float ymin = plot->pan_origin_ymin;
            float ymax = plot->pan_origin_ymax;
            float dx_data = 0.0f, dy_data = 0.0f;
            if (pa_w > 0)
                dx_data = (float)(plot->pan_start_x - mx) / (float)pa_w
                        * (xmax - xmin);
            if (pa_h > 0)
                dy_data = (float)(my - plot->pan_start_y) / (float)pa_h
                        * (ymax - ymin);
            plot->x_range.min = xmin + dx_data;
            plot->x_range.max = xmax + dx_data;
            plot->x_range.auto_range = false;
            plot->y_range.min = ymin + dy_data;
            plot->y_range.max = ymax + dy_data;
            plot->y_range.auto_range = false;
            return 1;
        }

        return lvg_rect_contains_point(&r, mx, my) ? 1 : 0;
    }

    if (event->type == LUI_EVENT_MOUSE_DOWN) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        if (!lvg_rect_contains_point(&r, mx, my)) return 0;

        /* Legend click: toggle series visibility */
        if (plot->show_legend && event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            int lw = 0;
            for (int s = 0; s < plot->series_count; s++) {
                int tw = text_width(plot->series[s].label);
                if (tw + 20 > lw) lw = tw + 20;
            }
            lw += 12;
            int lx = pa_x + pa_w - lw - 4;
            int ly = pa_y + 4;
            int lh = plot->series_count * 16 + 8;
            lvg_rect_t legend_r = { lx, ly, lw, lh };
            if (lvg_rect_contains_point(&legend_r, mx, my)) {
                int row = (my - ly - 4) / 16;
                if (row >= 0 && row < plot->series_count) {
                    plot->series[row].visible = !plot->series[row].visible;
                    return 1;
                }
            }
        }

        /* Middle-button: start pan */
        if (event->data.mouse_button.button == LUI_MOUSE_MIDDLE) {
            float ex_min, ex_max, ey_min, ey_max;
            compute_ranges(plot, &ex_min, &ex_max, &ey_min, &ey_max);
            plot->panning = true;
            plot->pan_start_x = mx;
            plot->pan_start_y = my;
            plot->pan_origin_xmin = ex_min;
            plot->pan_origin_xmax = ex_max;
            plot->pan_origin_ymin = ey_min;
            plot->pan_origin_ymax = ey_max;
            /* Snapshot current ranges to manual so panning works */
            plot->x_range.min = ex_min;
            plot->x_range.max = ex_max;
            plot->x_range.auto_range = false;
            plot->y_range.min = ey_min;
            plot->y_range.max = ey_max;
            plot->y_range.auto_range = false;
            return 1;
        }

        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_UP) {
        if (event->data.mouse_button.button == LUI_MOUSE_MIDDLE && plot->panning) {
            plot->panning = false;
            return 1;
        }
    }

    if (event->type == LUI_EVENT_SCROLL) {
        int mx = event->data.scroll.x;
        int my = event->data.scroll.y;
        lvg_rect_t pa = { pa_x, pa_y, pa_w, pa_h };
        if (!lvg_rect_contains_point(&pa, mx, my)) return 0;

        /* Zoom centered on cursor */
        float ex_min, ex_max, ey_min, ey_max;
        compute_ranges(plot, &ex_min, &ex_max, &ey_min, &ey_max);

        float factor = (event->data.scroll.delta_y > 0) ? 1.15f : (1.0f / 1.15f);

        /* Cursor position in data space */
        float cx = unmap_x(mx, ex_min, ex_max, pa_x, pa_w);
        float cy = unmap_y(my, ey_min, ey_max, pa_y, pa_h);

        /* Scale around cursor */
        float new_xmin = cx - (cx - ex_min) * factor;
        float new_xmax = cx + (ex_max - cx) * factor;
        float new_ymin = cy - (cy - ey_min) * factor;
        float new_ymax = cy + (ey_max - cy) * factor;

        plot->x_range.min = new_xmin;
        plot->x_range.max = new_xmax;
        plot->x_range.auto_range = false;
        plot->y_range.min = new_ymin;
        plot->y_range.max = new_ymax;
        plot->y_range.auto_range = false;
        return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

static void *plot_default_alloc(void *u, size_t n) { (void)u; return malloc(n); }
static void  plot_default_free(void *u, void *p)    { (void)u; free(p); }

bool lui_plot_init_ex(lui_plot_t *plot, int max_series,
                       lui_alloc_fn alloc_fn,
                       lui_free_fn  free_fn,
                       void        *alloc_user)
{
    if (!plot || max_series <= 0) return false;
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = plot_default_alloc;
        free_fn    = plot_default_free;
        alloc_user = NULL;
    }

    size_t bytes = (size_t)max_series * sizeof(lui_plot_series_t);
    if (bytes / sizeof(lui_plot_series_t) != (size_t)max_series) return false;
    lui_plot_series_t *series = (lui_plot_series_t *)alloc_fn(alloc_user, bytes);
    if (!series) return false;
    memset(series, 0, bytes);

    memset(plot, 0, sizeof(*plot));
    plot->series     = series;
    plot->max_series = max_series;
    plot->alloc_fn   = alloc_fn;
    plot->free_fn    = free_fn;
    plot->alloc_user = alloc_user;

    lui_widget_init(&plot->widget);
    plot->widget.width    = lvg_size_fill(1);
    plot->widget.height   = lvg_size_fill(1);
    plot->widget.measure  = plot_measure;
    plot->widget.draw     = plot_draw;
    plot->widget.on_event = plot_event;
    plot->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN | LUI_WIDGET_FOCUSABLE;

    plot->series_count = 0;

    plot->x_range.min = 0.0f;
    plot->x_range.max = 0.0f;
    plot->x_range.auto_range = true;
    plot->y_range.min = 0.0f;
    plot->y_range.max = 0.0f;
    plot->y_range.auto_range = true;

    plot->x_title = NULL;
    plot->y_title = NULL;

    plot->show_crosshair = false;
    plot->cursor_x = 0;
    plot->cursor_y = 0;
    plot->panning = false;
    plot->show_legend = true;

    /* Margins: space for axis labels and titles */
    plot->margin_left   = 60;
    plot->margin_right  = 20;
    plot->margin_top    = 20;
    plot->margin_bottom = 40;

    /* Dark theme colors */
    plot->bg              = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    plot->plot_bg         = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    plot->grid_major      = LVG_COLOR_RGB(0x30, 0x30, 0x40);
    plot->grid_minor      = LVG_COLOR_RGB(0x26, 0x26, 0x34);
    plot->axis_color      = LVG_COLOR_RGB(0x60, 0x60, 0x70);
    plot->text_color      = LVG_COLOR_RGB(0xA0, 0xA4, 0xAA);
    plot->crosshair_color = LVG_COLOR_ARGB(0xB0, 0xFF, 0xFF, 0x60);
    plot->legend_bg       = LVG_COLOR_ARGB(0xD0, 0x1E, 0x1E, 0x2E);

    return true;
}

bool lui_plot_init(lui_plot_t *plot)
{
    return lui_plot_init_ex(plot, LUI_PLOT_MAX_SERIES, NULL, NULL, NULL);
}

void lui_plot_destroy(lui_plot_t *plot)
{
    if (!plot) return;
    if (plot->free_fn && plot->series)
        plot->free_fn(plot->alloc_user, plot->series);
    plot->series       = NULL;
    plot->series_count = 0;
    plot->max_series   = 0;
}

int lui_plot_add_series(lui_plot_t *plot, const char *label,
                        lui_plot_type_t type, lvg_color_t color)
{
    if (!plot || !label || plot->series_count >= plot->max_series)
        return -1;

    int idx = plot->series_count;
    lui_plot_series_t *ser = &plot->series[idx];

    int len = (int)strlen(label);
    if (len > LUI_PLOT_MAX_LABEL) len = LUI_PLOT_MAX_LABEL;
    memcpy(ser->label, label, (size_t)len);
    ser->label[len] = '\0';

    ser->type = type;
    ser->color = color;
    ser->point_count = 0;
    ser->visible = true;

    plot->series_count++;
    return idx;
}

void lui_plot_set_data(lui_plot_t *plot, int series,
                       const lui_plot_point_t *points, int count)
{
    if (!plot || series < 0 || series >= plot->series_count || !points)
        return;

    if (count > LUI_PLOT_MAX_POINTS) count = LUI_PLOT_MAX_POINTS;
    if (count < 0) count = 0;

    memcpy(plot->series[series].points, points,
           (size_t)count * sizeof(lui_plot_point_t));
    plot->series[series].point_count = count;
}

void lui_plot_clear_series(lui_plot_t *plot, int series)
{
    if (!plot || series < 0 || series >= plot->series_count)
        return;
    plot->series[series].point_count = 0;
}

void lui_plot_set_x_range(lui_plot_t *plot, float min, float max)
{
    if (!plot) return;
    if (min == 0.0f && max == 0.0f) {
        plot->x_range.auto_range = true;
    } else {
        plot->x_range.min = min;
        plot->x_range.max = max;
        plot->x_range.auto_range = false;
    }
}

void lui_plot_set_y_range(lui_plot_t *plot, float min, float max)
{
    if (!plot) return;
    if (min == 0.0f && max == 0.0f) {
        plot->y_range.auto_range = true;
    } else {
        plot->y_range.min = min;
        plot->y_range.max = max;
        plot->y_range.auto_range = false;
    }
}

void lui_plot_fit_data(lui_plot_t *plot)
{
    if (!plot) return;
    plot->x_range.auto_range = true;
    plot->y_range.auto_range = true;
}
