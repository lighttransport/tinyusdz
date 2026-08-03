/*
 * lightui/dopesheet.h — Dopesheet / keyframe editor widget
 *
 * A dopesheet displays keyframes as diamonds on horizontal tracks,
 * supporting selection, drag-to-move, and range selection.
 * Designed for animation editors alongside the timeline widget.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_DOPESHEET_H
#define LIGHTUI_DOPESHEET_H

#include "alloc.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

/* Default capacities used by lui_dopesheet_init().
 * Use lui_dopesheet_init_ex() to pick smaller (or larger) sizes. */
#define LUI_DOPESHEET_MAX_CHANNELS    64
#define LUI_DOPESHEET_MAX_KEYFRAMES  512
#define LUI_DOPESHEET_MAX_LABEL       31  /* excl. NUL */

/* ---- Keyframe ----------------------------------------------------------- */

typedef struct {
    int   frame;       /* frame number                              */
    bool  selected;    /* selection state                           */
} lui_keyframe_t;

/* ---- Channel (track) ---------------------------------------------------- */

typedef struct {
    char            label[LUI_DOPESHEET_MAX_LABEL + 1];
    lui_keyframe_t  keys[LUI_DOPESHEET_MAX_KEYFRAMES];
    int             key_count;
    bool            expanded;   /* true = visible                    */
    lvg_color_t     color;      /* channel accent color               */
} lui_dopesheet_channel_t;

/* ---- Dopesheet widget --------------------------------------------------- */

typedef struct {
    lui_widget_t widget;

    /* Channels — heap-allocated, capacity = max_channels. */
    lui_dopesheet_channel_t *channels;
    int                     channel_count;
    int                     max_channels;

    /* Allocator paired with `channels` — used by destroy. */
    lui_alloc_fn       alloc_fn;
    lui_free_fn        free_fn;
    void              *alloc_user;

    /* View state */
    int   view_start;         /* first visible frame                  */
    float pixels_per_frame;   /* zoom level (default 6.0)             */
    int   total_frames;       /* total frame count                    */

    /* Interaction state */
    int   hovered_channel;    /* -1 = none                            */
    int   hovered_key;        /* -1 = none                            */
    bool  dragging;           /* dragging selected keyframes          */
    int   drag_start_frame;   /* frame at drag start                  */
    int   drag_delta;         /* frame delta accumulated              */
    bool  box_selecting;      /* rubber band selection active          */
    int   box_x0, box_y0;    /* start of box select (pixels)         */
    int   box_x1, box_y1;    /* current end of box select            */

    /* Layout */
    int   header_width;       /* channel label column width (100)     */
    int   row_height;         /* height per channel row (22)          */
    int   key_size;           /* diamond size in pixels (8)           */
    int   ruler_height;       /* frame ruler height (20)              */

    /* Colors */
    lvg_color_t bg;
    lvg_color_t ruler_bg;
    lvg_color_t ruler_text;
    lvg_color_t row_bg;
    lvg_color_t row_bg_alt;
    lvg_color_t grid_color;
    lvg_color_t key_color;
    lvg_color_t key_selected;
    lvg_color_t key_hovered;
    lvg_color_t selection_box;
    lvg_color_t text_color;
    lvg_color_t border_color;

    /* Callback */
    void (*on_change)(void *user);
    void *on_change_user;
} lui_dopesheet_t;

/* ---- API ---------------------------------------------------------------- */

/**
 * Initialise a dopesheet with default capacity using malloc/free.
 * Pair every successful init with lui_dopesheet_destroy().
 */
bool lui_dopesheet_init(lui_dopesheet_t *ds);

/** Initialise with caller-supplied capacity / allocator (NULL/NULL = default). */
bool lui_dopesheet_init_ex(lui_dopesheet_t *ds, int max_channels,
                            lui_alloc_fn alloc_fn,
                            lui_free_fn  free_fn,
                            void        *alloc_user);

/** Free heap arrays owned by `ds`. */
void lui_dopesheet_destroy(lui_dopesheet_t *ds);

/**
 * Add a channel.  Returns channel index or -1 on failure.
 * @label  Channel name (copied internally).
 * @color  Accent color for this channel.
 */
int lui_dopesheet_add_channel(lui_dopesheet_t *ds, const char *label,
                               lvg_color_t color);

/**
 * Add a keyframe to a channel.  Returns keyframe index or -1 on failure.
 * @channel  Channel index.
 * @frame    Frame number.
 */
int lui_dopesheet_add_key(lui_dopesheet_t *ds, int channel, int frame);

/** Remove a keyframe from a channel. */
void lui_dopesheet_remove_key(lui_dopesheet_t *ds, int channel, int key_index);

/** Remove all keyframes in a channel. */
void lui_dopesheet_clear_channel(lui_dopesheet_t *ds, int channel);

/** Set the view range. */
void lui_dopesheet_set_view(lui_dopesheet_t *ds, int start_frame,
                             float pixels_per_frame);

/** Select all keyframes in a frame range across all channels. */
void lui_dopesheet_select_range(lui_dopesheet_t *ds, int frame_start,
                                 int frame_end);

/** Deselect all keyframes. */
void lui_dopesheet_deselect_all(lui_dopesheet_t *ds);

/** Convert frame to pixel X position. */
int lui_dopesheet_frame_to_x(const lui_dopesheet_t *ds, int frame);

/** Convert pixel X to frame number. */
int lui_dopesheet_x_to_frame(const lui_dopesheet_t *ds, int x);

/** Get the widget pointer. */
static inline lui_widget_t *lui_dopesheet_widget(lui_dopesheet_t *ds) {
    return &ds->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_DOPESHEET_H */
