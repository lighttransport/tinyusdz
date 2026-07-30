/*
 * waveform.c — Audio waveform display widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/waveform.h>
#include <lightvg/canvas.h>

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline float wf_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int wf_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    const lui_waveform_t *wf = (const lui_waveform_t *)w;
    (void)user;
    *out_w = 256;
    *out_h = wf->channel_height * wf->channels;
    return 0;
}

static void wf_draw_channel(const lui_waveform_t *wf, lvg_canvas_t *canvas,
                              int plot_x, int plot_y, int plot_w, int plot_h,
                              int ch)
{
    if (!wf->samples || wf->sample_count <= 0 || wf->visible_samples <= 0)
        return;

    int center_y = plot_y + plot_h / 2;

    /* Center line */
    lvg_canvas_fill_rect(canvas, plot_x, center_y, plot_w, 1,
                          wf->center_line_color);

    /* Draw waveform envelope column by column */
    for (int px = 0; px < plot_w; px++) {
        /* Map pixel column to sample range */
        int s0 = wf->start_sample + (int)((float)px * wf->visible_samples / plot_w);
        int s1 = wf->start_sample + (int)((float)(px + 1) * wf->visible_samples / plot_w);
        if (s0 < 0) s0 = 0;
        if (s1 <= s0) s1 = s0 + 1;
        if (s0 >= wf->sample_count) continue;
        if (s1 > wf->sample_count) s1 = wf->sample_count;

        /* Find min/max in this range for the given channel */
        float vmin =  1.0f;
        float vmax = -1.0f;
        for (int si = s0; si < s1; si++) {
            int idx = si * wf->channels + ch;
            if (idx >= wf->sample_count * wf->channels) break;
            float v = wf->samples[idx];
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
        vmin = wf_clampf(vmin, -1.0f, 1.0f);
        vmax = wf_clampf(vmax, -1.0f, 1.0f);

        /* Map to pixel coordinates (top = +1, bottom = -1) */
        int y_top    = center_y - (int)(vmax * (plot_h / 2));
        int y_bottom = center_y - (int)(vmin * (plot_h / 2));
        if (y_bottom < y_top) { int t = y_top; y_top = y_bottom; y_bottom = t; }
        int h = y_bottom - y_top;
        if (h < 1) h = 1;

        /* Filled area from center */
        lvg_canvas_fill_rect(canvas, plot_x + px, y_top, 1, h,
                              wf->wave_fill_color);

        /* Envelope outline */
        lvg_canvas_fill_rect(canvas, plot_x + px, y_top, 1, 1,
                              wf->wave_color);
        lvg_canvas_fill_rect(canvas, plot_x + px, y_top + h - 1, 1, 1,
                              wf->wave_color);
    }
}

static void wf_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_waveform_t *wf = (lui_waveform_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, wf->bg_color);
    lvg_canvas_stroke_rect(canvas, r.x, r.y, r.width, r.height,
                            wf->border_color, 1);

    int plot_x = r.x + 1;
    int plot_w = r.width - 2;
    int total_h = r.height - 2;

    /* Draw each channel */
    int ch_count = wf->channels;
    if (ch_count < 1) ch_count = 1;
    if (ch_count > 2) ch_count = 2;
    int ch_h = total_h / ch_count;

    for (int ch = 0; ch < ch_count; ch++) {
        int plot_y = r.y + 1 + ch * ch_h;
        wf_draw_channel(wf, canvas, plot_x, plot_y, plot_w, ch_h, ch);

        /* Channel separator */
        if (ch > 0) {
            lvg_canvas_fill_rect(canvas, plot_x, plot_y, plot_w, 1,
                                  wf->grid_color);
        }
    }

    /* Selection overlay */
    if (wf->has_selection) {
        float s0 = wf->select_start < wf->select_end
                  ? wf->select_start : wf->select_end;
        float s1 = wf->select_start < wf->select_end
                  ? wf->select_end : wf->select_start;
        int sx = plot_x + (int)(s0 * plot_w);
        int sw = (int)((s1 - s0) * plot_w);
        if (sw < 1) sw = 1;
        lvg_canvas_fill_rect(canvas, sx, r.y + 1, sw, total_h,
                              wf->selection_color);
    }

    /* Playhead */
    if (wf->show_playhead) {
        int px = plot_x + (int)(wf->playhead_pos * plot_w);
        if (px >= plot_x && px < plot_x + plot_w) {
            lvg_canvas_fill_rect(canvas, px, r.y + 1, 1, total_h,
                                  wf->playhead_color);
        }
    }
}

static int wf_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_waveform_t *wf = (lui_waveform_t *)w;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN:
        if (event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            lvg_rect_t r = lui_widget_absolute_rect(w);
            if (lvg_rect_contains_point(&r,
                    event->data.mouse_button.x,
                    event->data.mouse_button.y)) {
                int plot_x = r.x + 1;
                int plot_w = r.width - 2;
                if (plot_w <= 0) return 0;
                float frac = (float)(event->data.mouse_button.x - plot_x)
                           / (float)plot_w;
                frac = wf_clampf(frac, 0.0f, 1.0f);
                wf->playhead_pos = frac;
                wf->show_playhead = true;
                wf->dragging = true;
                wf->drag_origin = frac;
                wf->has_selection = false;
                return 1;
            }
        }
        break;

    case LUI_EVENT_MOUSE_MOVE:
        if (wf->dragging) {
            lvg_rect_t r = lui_widget_absolute_rect(w);
            int plot_x = r.x + 1;
            int plot_w = r.width - 2;
            if (plot_w <= 0) return 0;
            float frac = (float)(event->data.mouse_move.x - plot_x)
                       / (float)plot_w;
            frac = wf_clampf(frac, 0.0f, 1.0f);
            /* Create selection from drag origin to current */
            wf->select_start = wf->drag_origin;
            wf->select_end = frac;
            wf->has_selection = true;
            wf->playhead_pos = frac;
            return 1;
        }
        break;

    case LUI_EVENT_MOUSE_UP:
        if (wf->dragging && event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            wf->dragging = false;
            /* If selection is too small, clear it */
            float diff = wf->select_end - wf->select_start;
            if (diff < 0) diff = -diff;
            if (diff < 0.002f) {
                wf->has_selection = false;
            }
            return 1;
        }
        break;

    default:
        break;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_waveform_init(lui_waveform_t *wf)
{
    if (!wf) return;

    lui_widget_init(&wf->widget);
    wf->widget.width    = lvg_size_fill(1);
    wf->widget.height   = lvg_size_hug(80);
    wf->widget.measure  = wf_measure;
    wf->widget.draw     = wf_draw;
    wf->widget.on_event = wf_event;

    wf->samples         = NULL;
    wf->sample_count    = 0;
    wf->channels        = 1;
    wf->channel_height  = 40;

    wf->start_sample    = 0;
    wf->visible_samples = 0;

    wf->playhead_pos    = 0.0f;
    wf->show_playhead   = false;

    wf->select_start    = 0.0f;
    wf->select_end      = 0.0f;
    wf->has_selection   = false;

    wf->dragging        = false;
    wf->drag_origin     = 0.0f;

    wf->bg_color         = LVG_COLOR_RGB(0x1A, 0x1A, 0x24);
    wf->wave_color       = LVG_COLOR_RGB(0x58, 0xB0, 0xE0);
    wf->wave_fill_color  = LVG_COLOR_RGB(0x30, 0x70, 0xA0);
    wf->playhead_color   = LVG_COLOR_RGB(0xE0, 0xE0, 0x40);
    wf->selection_color  = LVG_COLOR_ARGB(0x40, 0x58, 0xB0, 0xE0);
    wf->grid_color       = LVG_COLOR_RGB(0x30, 0x30, 0x3C);
    wf->border_color     = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    wf->center_line_color = LVG_COLOR_RGB(0x38, 0x38, 0x44);
}

void lui_waveform_set_samples(lui_waveform_t *wf, const float *samples,
                               int count, int channels)
{
    if (!wf) return;
    wf->samples      = samples;
    wf->sample_count = count;
    wf->channels     = channels < 1 ? 1 : (channels > 2 ? 2 : channels);
    if (wf->visible_samples <= 0)
        wf->visible_samples = count;
}

void lui_waveform_set_view(lui_waveform_t *wf, int start, int visible)
{
    if (!wf) return;
    wf->start_sample    = start;
    wf->visible_samples = visible;
}

void lui_waveform_set_playhead(lui_waveform_t *wf, float pos)
{
    if (!wf) return;
    wf->playhead_pos  = wf_clampf(pos, 0.0f, 1.0f);
    wf->show_playhead = true;
}

void lui_waveform_set_selection(lui_waveform_t *wf, float start, float end)
{
    if (!wf) return;
    wf->select_start = wf_clampf(start, 0.0f, 1.0f);
    wf->select_end   = wf_clampf(end, 0.0f, 1.0f);
    wf->has_selection = true;
}
