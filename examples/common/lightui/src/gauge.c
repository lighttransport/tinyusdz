/*
 * gauge.c -- Circular gauge / meter widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/gauge.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline float g_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Draw a string as 5x10 filled rectangles with 7px advance. */
static void g_draw_text(lvg_canvas_t *canvas, int x, int y,
                        const char *text, lvg_color_t color)
{
    if (!text) return;
    for (int i = 0; text[i]; i++) {
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, x, y, 5, 10, color);
        x += 7;
    }
}

/* Measure text width in the 5x10 / 7px-advance system. */
static int g_text_width(const char *text)
{
    if (!text) return 0;
    int len = (int)strlen(text);
    if (len == 0) return 0;
    return len * 7 - 2;  /* last char has no trailing gap */
}

/* Draw text centered at (cx, cy). */
static void g_draw_text_centered(lvg_canvas_t *canvas, int cx, int cy,
                                 const char *text, lvg_color_t color)
{
    int tw = g_text_width(text);
    g_draw_text(canvas, cx - tw / 2, cy - 5, text, color);
}

/* Simple float-to-string for display. */
static void g_fmt_value(char *buf, int sz, float v)
{
    int whole = (int)v;
    int frac  = (int)((v - (float)whole) * 10.0f);
    if (frac < 0) frac = -frac;

    int pos = 0;
    if (v < 0.0f && whole == 0) {
        buf[pos++] = '-';
    }
    if (whole < 0) { buf[pos++] = '-'; whole = -whole; }

    char tmp[16];
    int n = 0;
    if (whole == 0) { tmp[n++] = '0'; }
    else { while (whole > 0 && n < 14) { tmp[n++] = (char)('0' + whole % 10); whole /= 10; } }
    for (int i = n - 1; i >= 0 && pos < sz - 3; i--) buf[pos++] = tmp[i];
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + frac);
    buf[pos] = '\0';
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int gauge_measure(const lui_widget_t *w, int *out_w, int *out_h,
                         void *user)
{
    const lui_gauge_t *g = (const lui_gauge_t *)w;
    (void)user;
    *out_w = g->size;
    *out_h = g->size;
    return 0;
}

static void gauge_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_gauge_t *g = (lui_gauge_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int dim = r.width < r.height ? r.width : r.height;
    int cx = r.x + r.width / 2;
    int cy = r.y + r.height / 2;
    int outer_r = dim / 2 - 2;
    int inner_r = outer_r - g->thickness;
    if (inner_r < 1) inner_r = 1;

    /* Background circle */
    lvg_canvas_fill_circle(canvas, cx, cy, outer_r, g->bg_color);
    lvg_canvas_stroke_circle(canvas, cx, cy, outer_r, g->border_color, 1);

    /* Arc track and filled arc.
     * We draw pixel-by-pixel in the arc band, checking angles. */
    float start_rad = g->start_angle * (float)M_PI / 180.0f;
    float sweep_rad = g->sweep_angle * (float)M_PI / 180.0f;
    float fill_frac = g->value;

    lvg_surface_t *surf = canvas->_surface;
    lvg_rect_t clip = canvas->_clip;

    for (int py = r.y; py < r.y + r.height; py++) {
        if (py < clip.y || py >= clip.y + clip.height) continue;
        for (int px = r.x; px < r.x + r.width; px++) {
            if (px < clip.x || px >= clip.x + clip.width) continue;

            int dx = px - cx;
            int dy = py - cy;
            int dist2 = dx * dx + dy * dy;
            int dist = (int)(sqrtf((float)dist2) + 0.5f);

            if (dist < inner_r || dist > outer_r) continue;

            /* Compute angle of this pixel relative to start */
            float angle = atan2f((float)dy, (float)dx);
            float rel = angle - start_rad;

            /* Normalise rel to [0, 2*PI) */
            while (rel < 0.0f) rel += 2.0f * (float)M_PI;
            while (rel >= 2.0f * (float)M_PI) rel -= 2.0f * (float)M_PI;

            /* Check if within the sweep (handle CW sweep) */
            float abs_sweep = sweep_rad < 0.0f ? -sweep_rad : sweep_rad;
            float norm_rel = (sweep_rad >= 0.0f) ? rel
                             : (2.0f * (float)M_PI - rel);
            if (norm_rel > abs_sweep) continue;

            /* Fraction along the arc */
            float arc_frac = norm_rel / abs_sweep;

            lvg_color_t col;
            if (arc_frac <= fill_frac) {
                /* Filled portion - pick colour based on thresholds */
                if (fill_frac >= g->danger_threshold)
                    col = g->danger_color;
                else if (fill_frac >= g->warning_threshold)
                    col = g->warning_color;
                else
                    col = g->fill_color;
            } else {
                col = g->track_color;
            }

            /* Alpha-blend manually would be complex; just write */
            if (py >= 0 && py < surf->height && px >= 0 && px < surf->width)
                surf->pixels[py * surf->stride + px] = col;
        }
    }

    /* Needle line from center toward the current value angle */
    {
        float val_angle = start_rad + fill_frac * sweep_rad;
        int needle_len = inner_r - 4;
        if (needle_len < 2) needle_len = 2;
        int nx = cx + (int)(cosf(val_angle) * (float)needle_len);
        int ny = cy + (int)(sinf(val_angle) * (float)needle_len);
        lvg_canvas_draw_line(canvas, cx, cy, nx, ny, g->needle_color, 2);
    }

    /* Center dot */
    lvg_canvas_fill_circle(canvas, cx, cy, 3, g->needle_color);

    /* Value text centered */
    {
        float display_val = g->min_val + g->value * (g->max_val - g->min_val);
        char buf[32];
        g_fmt_value(buf, sizeof(buf), display_val);
        /* Append unit */
        if (g->unit[0]) {
            int len = (int)strlen(buf);
            if (len < (int)sizeof(buf) - 2) {
                int ulen = (int)strlen(g->unit);
                for (int i = 0; i < ulen && len < (int)sizeof(buf) - 1; i++)
                    buf[len++] = g->unit[i];
                buf[len] = '\0';
            }
        }
        g_draw_text_centered(canvas, cx, cy + inner_r / 3, buf, g->text_color);
    }

    /* Label below value */
    if (g->label[0]) {
        g_draw_text_centered(canvas, cx, cy + inner_r / 3 + 14,
                             g->label, g->text_color);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_gauge_init(lui_gauge_t *g, float min_val, float max_val, int size)
{
    if (!g) return;

    lui_widget_init(&g->widget);
    g->widget.width    = lvg_size_hug(size);
    g->widget.height   = lvg_size_hug(size);
    g->widget.measure  = gauge_measure;
    g->widget.draw     = gauge_draw;
    /* No event handler -- read-only */

    g->value   = 0.0f;
    g->min_val = min_val;
    g->max_val = max_val;
    g->size    = size;

    g->label[0] = '\0';
    g->unit[0]  = '\0';

    /* Arc starts at bottom-left (150 deg) and sweeps 240 deg clockwise */
    g->start_angle = 150.0f;
    g->sweep_angle = 240.0f;
    g->thickness   = 12;

    g->bg_color     = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    g->track_color  = LVG_COLOR_RGB(0x40, 0x44, 0x4B);
    g->fill_color   = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    g->text_color   = LVG_COLOR_RGB(0xCD, 0xD6, 0xF4);
    g->border_color = LVG_COLOR_RGB(0x58, 0x5B, 0x70);
    g->needle_color = LVG_COLOR_WHITE;

    g->warning_threshold = 0.7f;
    g->danger_threshold  = 0.9f;
    g->warning_color     = LVG_COLOR_RGB(0xF9, 0xE2, 0xAF);
    g->danger_color      = LVG_COLOR_RGB(0xF3, 0x8B, 0xA8);
}

void lui_gauge_set_value(lui_gauge_t *g, float value)
{
    if (!g) return;
    /* Normalise to 0..1 */
    if (g->max_val > g->min_val)
        g->value = g_clampf((value - g->min_val) / (g->max_val - g->min_val),
                            0.0f, 1.0f);
    else
        g->value = 0.0f;
}
