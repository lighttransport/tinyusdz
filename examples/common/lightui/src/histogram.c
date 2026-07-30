/*
 * histogram.c — Histogram display widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/histogram.h>
#include <lightvg/canvas.h>
#include <lightvg/surface.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int hist_measure(const lui_widget_t *w, int *out_w, int *out_h,
                         void *user)
{
    (void)w; (void)user;
    *out_w = 256;
    *out_h = 128;
    return 0;
}

static void hist_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_histogram_t *h = (lui_histogram_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int cr = h->corner_radius;

    /* Background */
    lvg_canvas_fill_rounded_rect(canvas, r.x, r.y, r.width, r.height, cr,
                                  h->bg);
    lvg_canvas_stroke_rounded_rect(canvas, r.x, r.y, r.width, r.height, cr,
                                    h->border_color, 1);

    /* Clip */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t inner = lvg_rect_make(r.x + 1, r.y + 1,
                                      r.width - 2, r.height - 2);
    lvg_rect_t clip = lvg_rect_intersect(&old_clip, &inner);
    canvas->_clip = clip;

    int plot_x = r.x + 1;
    int plot_y = r.y + 1;
    int plot_w = r.width - 2;
    int plot_h = r.height - 2;

    /* Grid lines (25%, 50%, 75%) */
    for (int g = 1; g <= 3; g++) {
        int gy = plot_y + plot_h - (plot_h * g / 4);
        lvg_canvas_fill_rect(canvas, plot_x, gy, plot_w, 1, h->grid_color);
    }

    /* Draw channels */
    for (int ch = 0; ch < h->channel_count && ch < LUI_HIST_MAX_CHANNELS; ch++) {
        if (!h->channel_visible[ch]) continue;

        lvg_color_t col = h->channel_color[ch];
        /* In overlay mode, make semi-transparent by darkening */
        if (h->mode == LUI_HIST_OVERLAY && h->channel_count > 1) {
            int cr2 = (int)(LVG_COLOR_R(col) * 0.6f);
            int cg = (int)(LVG_COLOR_G(col) * 0.6f);
            int cb = (int)(LVG_COLOR_B(col) * 0.6f);
            col = LVG_COLOR_RGB(cr2, cg, cb);
        }

        float bin_w = (float)plot_w / LUI_HIST_BINS;

        for (int b = 0; b < LUI_HIST_BINS; b++) {
            float v = h->bins[ch][b];
            if (v <= 0.0f) continue;
            if (v > 1.0f) v = 1.0f;

            int bx = plot_x + (int)(b * bin_w);
            int bw = (int)((b + 1) * bin_w) - (int)(b * bin_w);
            if (bw < 1) bw = 1;
            int bh = (int)(v * plot_h);
            if (bh < 1) bh = 1;

            int by;
            if (h->mode == LUI_HIST_STACKED && ch > 0) {
                /* Stack on top of previous channels */
                float prev_total = 0;
                for (int pc = 0; pc < ch; pc++) {
                    if (h->channel_visible[pc])
                        prev_total += h->bins[pc][b];
                }
                if (prev_total > 1.0f) prev_total = 1.0f;
                int prev_h = (int)(prev_total * plot_h);
                by = plot_y + plot_h - prev_h - bh;
            } else {
                by = plot_y + plot_h - bh;
            }

            lvg_canvas_fill_rect(canvas, bx, by, bw, bh, col);
        }
    }

    /* Clipping indicators */
    if (h->show_clipping) {
        for (int ch = 0; ch < h->channel_count && ch < LUI_HIST_MAX_CHANNELS; ch++) {
            if (!h->channel_visible[ch]) continue;
            /* Shadow clipping (bin 0) */
            if (h->bins[ch][0] >= h->clip_threshold) {
                lvg_canvas_fill_rect(canvas, plot_x, plot_y + plot_h - 4,
                                      4, 4, h->clip_shadow_color);
            }
            /* Highlight clipping (bin 255) */
            if (h->bins[ch][LUI_HIST_BINS - 1] >= h->clip_threshold) {
                lvg_canvas_fill_rect(canvas, plot_x + plot_w - 4,
                                      plot_y + plot_h - 4,
                                      4, 4, h->clip_highlight_color);
            }
        }
    }

    canvas->_clip = old_clip;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_histogram_init(lui_histogram_t *h)
{
    if (!h) return;

    lui_widget_init(&h->widget);
    h->widget.width   = lvg_size_fill(1);
    h->widget.height  = lvg_size_hug(128);
    h->widget.measure = hist_measure;
    h->widget.draw    = hist_draw;
    h->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    memset(h->bins, 0, sizeof(h->bins));
    h->channel_count = 4;
    for (int i = 0; i < LUI_HIST_MAX_CHANNELS; i++)
        h->channel_visible[i] = true;

    h->mode = LUI_HIST_OVERLAY;
    h->show_clipping = true;
    h->clip_threshold = 0.9f;

    h->bg                  = LVG_COLOR_RGB(0x1A, 0x1A, 0x24);
    h->channel_color[0]    = LVG_COLOR_RGB(0xE0, 0x40, 0x40); /* R */
    h->channel_color[1]    = LVG_COLOR_RGB(0x40, 0xE0, 0x40); /* G */
    h->channel_color[2]    = LVG_COLOR_RGB(0x40, 0x40, 0xE0); /* B */
    h->channel_color[3]    = LVG_COLOR_RGB(0xC0, 0xC0, 0xC0); /* Lum */
    h->grid_color          = LVG_COLOR_RGB(0x30, 0x30, 0x3C);
    h->clip_shadow_color   = LVG_COLOR_RGB(0x40, 0x40, 0xFF);
    h->clip_highlight_color = LVG_COLOR_RGB(0xFF, 0x40, 0x40);
    h->border_color        = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    h->corner_radius       = 3;
}

void lui_histogram_set_data(lui_histogram_t *h, int channel,
                             const int *counts, int num_bins)
{
    if (!h || channel < 0 || channel >= LUI_HIST_MAX_CHANNELS) return;
    if (!counts || num_bins <= 0) {
        memset(h->bins[channel], 0, sizeof(h->bins[channel]));
        return;
    }

    /* Find max count for normalisation */
    int max_count = 0;
    for (int i = 0; i < num_bins && i < LUI_HIST_BINS; i++)
        if (counts[i] > max_count) max_count = counts[i];

    float scale = max_count > 0 ? 1.0f / (float)max_count : 0.0f;

    memset(h->bins[channel], 0, sizeof(h->bins[channel]));
    for (int i = 0; i < num_bins && i < LUI_HIST_BINS; i++)
        h->bins[channel][i] = (float)counts[i] * scale;
}

void lui_histogram_from_surface(lui_histogram_t *h,
                                 const lvg_surface_t *surface)
{
    if (!h || !surface || !surface->pixels) return;
    if (surface->width <= 0 || surface->height <= 0) return;

    int counts[4][LUI_HIST_BINS];
    memset(counts, 0, sizeof(counts));

    /* Previously `int total`, which wraps negative for ~46k*46k surfaces
     * and silently mis-iterates. Use size_t end-to-end. */
    size_t total = (size_t)surface->width * (size_t)surface->height;
    const uint32_t *px = (const uint32_t *)surface->pixels;

    for (size_t i = 0; i < total; i++) {
        uint32_t c = px[i];
        int r = (int)((c >> 16) & 0xFF);
        int g = (int)((c >> 8) & 0xFF);
        int b = (int)(c & 0xFF);
        int lum = (r * 77 + g * 150 + b * 29) >> 8;  /* BT.601 approx */
        if (lum > 255) lum = 255;

        counts[0][r]++;
        counts[1][g]++;
        counts[2][b]++;
        counts[3][lum]++;
    }

    for (int ch = 0; ch < 4; ch++)
        lui_histogram_set_data(h, ch, counts[ch], LUI_HIST_BINS);
}

void lui_histogram_clear(lui_histogram_t *h)
{
    if (!h) return;
    memset(h->bins, 0, sizeof(h->bins));
}
