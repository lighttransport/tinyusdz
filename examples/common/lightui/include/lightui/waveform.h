/*
 * lightui/waveform.h — Audio waveform display widget
 *
 * Displays audio sample data as a waveform envelope with playhead, selection,
 * and stereo channel support.  Suitable for NLA / media editing timelines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_WAVEFORM_H
#define LIGHTUI_WAVEFORM_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Waveform widget ---------------------------------------------------- */

typedef struct {
    lui_widget_t widget;

    /* Sample data (app-owned, not copied) */
    const float  *samples;
    int           sample_count;
    int           channels;          /* 1 = mono, 2 = stereo                  */
    int           channel_height;    /* height per channel in pixels           */

    /* Display range (zoom / scroll) */
    int           start_sample;
    int           visible_samples;

    /* Playhead (0.0–1.0 normalised position within view) */
    float         playhead_pos;
    bool          show_playhead;

    /* Selection (0.0–1.0 normalised within view) */
    float         select_start;
    float         select_end;
    bool          has_selection;

    /* Interaction state */
    bool          dragging;
    float         drag_origin;       /* normalised x where drag started       */

    /* Appearance */
    lvg_color_t   bg_color;
    lvg_color_t   wave_color;
    lvg_color_t   wave_fill_color;
    lvg_color_t   playhead_color;
    lvg_color_t   selection_color;   /* semi-transparent                      */
    lvg_color_t   grid_color;
    lvg_color_t   border_color;
    lvg_color_t   center_line_color;
} lui_waveform_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a waveform widget with sensible defaults. */
void lui_waveform_init(lui_waveform_t *wf);

/** Set sample data (pointer is stored, not copied). */
void lui_waveform_set_samples(lui_waveform_t *wf, const float *samples,
                               int count, int channels);

/** Set the visible range (start sample index and number of samples). */
void lui_waveform_set_view(lui_waveform_t *wf, int start, int visible);

/** Set the playhead position (0.0–1.0 normalised). */
void lui_waveform_set_playhead(lui_waveform_t *wf, float pos);

/** Set a selection range (0.0–1.0 normalised). */
void lui_waveform_set_selection(lui_waveform_t *wf, float start, float end);

/** Get the widget node. */
static inline lui_widget_t *lui_waveform_widget(lui_waveform_t *wf) {
    return &wf->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_WAVEFORM_H */
