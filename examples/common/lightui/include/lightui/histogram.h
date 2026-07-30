/*
 * lightui/histogram.h — Histogram display widget
 *
 * Displays 256-bin histograms for image channels (R, G, B, luminance).
 * Supports overlay and stacked modes with clipping indicators.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_HISTOGRAM_H
#define LIGHTUI_HISTOGRAM_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Constants ---------------------------------------------------------- */

#define LUI_HIST_BINS          256
#define LUI_HIST_MAX_CHANNELS    4

typedef enum {
    LUI_HIST_CHANNEL_R   = 0,
    LUI_HIST_CHANNEL_G   = 1,
    LUI_HIST_CHANNEL_B   = 2,
    LUI_HIST_CHANNEL_LUM = 3,
} lui_hist_channel_t;

typedef enum {
    LUI_HIST_OVERLAY = 0,   /* channels drawn semi-transparent on top   */
    LUI_HIST_STACKED = 1,   /* channels stacked vertically              */
} lui_hist_mode_t;

/* ---- Histogram widget --------------------------------------------------- */

typedef struct {
    lui_widget_t  widget;

    /* Bin data (normalised counts, 0.0–1.0 per bin) */
    float         bins[LUI_HIST_MAX_CHANNELS][LUI_HIST_BINS];
    bool          channel_visible[LUI_HIST_MAX_CHANNELS];
    int           channel_count;    /* active channel count (1–4)          */

    /* Display mode */
    lui_hist_mode_t mode;

    /* Clipping indicators */
    bool          show_clipping;    /* highlight clipped shadows/highlights */
    float         clip_threshold;   /* bin value above which = clipped (0.9)*/

    /* Appearance */
    lvg_color_t   bg;
    lvg_color_t   channel_color[LUI_HIST_MAX_CHANNELS];
    lvg_color_t   grid_color;
    lvg_color_t   clip_shadow_color;   /* clipped blacks indicator          */
    lvg_color_t   clip_highlight_color;/* clipped whites indicator          */
    lvg_color_t   border_color;
    int           corner_radius;
} lui_histogram_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a histogram widget (default: 4 channels, overlay mode). */
void lui_histogram_init(lui_histogram_t *h);

/**
 * Set bin data for a channel from raw integer counts.
 * Counts are normalised internally to 0.0–1.0 (peak = 1.0).
 */
void lui_histogram_set_data(lui_histogram_t *h, int channel,
                             const int *counts, int num_bins);

/**
 * Compute histogram bins from a surface's pixel data.
 * Fills all 4 channels (R, G, B, luminance).
 */
void lui_histogram_from_surface(lui_histogram_t *h,
                                 const lvg_surface_t *surface);

/** Clear all bin data. */
void lui_histogram_clear(lui_histogram_t *h);

/** Get the widget node. */
static inline lui_widget_t *lui_histogram_widget(lui_histogram_t *h) {
    return &h->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_HISTOGRAM_H */
