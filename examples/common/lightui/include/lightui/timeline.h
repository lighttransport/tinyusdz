/*
 * lightui/timeline.h — Timeline / NLA (Non-Linear Animation) widget
 *
 * A multi-track timeline for video editing, animation, and audio work.
 * Supports clips on tracks, a playhead, zoom/pan, and drag-to-move/resize.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TIMELINE_H
#define LIGHTUI_TIMELINE_H

#include "alloc.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default capacities used by lui_timeline_init().
 * Use lui_timeline_init_ex() to pick different sizes. */
#define LUI_TIMELINE_MAX_TRACKS     32
#define LUI_TIMELINE_MAX_CLIPS     256

typedef struct {
    int          id;          /* unique clip ID                      */
    int          track;       /* track index                         */
    int          start;       /* start frame                         */
    int          duration;    /* duration in frames                  */
    lvg_color_t  color;       /* clip color                          */
    const char  *label;       /* clip name (not owned)               */
    bool         selected;    /* selection state                     */
} lui_timeline_clip_t;

typedef struct {
    int          id;          /* unique track ID                     */
    const char  *label;       /* track name (not owned)              */
    bool         muted;       /* muted/disabled state                */
    bool         locked;      /* lock against editing                */
    lvg_color_t  color;       /* track header/label color            */
    int          height;      /* track row height (default 32)       */
} lui_timeline_track_t;

typedef enum {
    LUI_TL_EVENT_NONE = 0,
    LUI_TL_EVENT_PLAYHEAD_MOVED,
    LUI_TL_EVENT_CLIP_MOVED,
    LUI_TL_EVENT_CLIP_RESIZED,
    LUI_TL_EVENT_CLIP_SELECTED,
    LUI_TL_EVENT_CLIP_DOUBLE_CLICK,
    LUI_TL_EVENT_RANGE_CHANGED,
} lui_timeline_event_type_t;

typedef struct {
    lui_timeline_event_type_t type;
    int clip_id;     /* relevant clip, or -1 */
    int track_id;    /* relevant track, or -1 */
    int frame;       /* frame number (for playhead) */
} lui_timeline_event_t;

typedef void (*lui_timeline_event_fn)(const lui_timeline_event_t *event,
                                       void *user);

typedef struct {
    lui_widget_t widget;

    /* Tracks and clips — heap-allocated. */
    lui_timeline_track_t *tracks;
    int                   track_count;
    int                   max_tracks;
    lui_timeline_clip_t  *clips;
    int                   clip_count;
    int                   max_clips;

    /* Allocator paired with the heap arrays — used by destroy. */
    lui_alloc_fn          alloc_fn;
    lui_free_fn           free_fn;
    void                 *alloc_user;

    /* Playback */
    int playhead;         /* current frame                          */
    int range_start;      /* in/out range start frame               */
    int range_end;        /* in/out range end frame                 */
    int total_frames;     /* total timeline length in frames        */
    float fps;            /* frames per second (for ruler labels)   */

    /* View / zoom */
    int   view_start;     /* first visible frame                    */
    float pixels_per_frame; /* zoom level (default 4.0)             */

    /* Interaction state */
    int   drag_mode;      /* 0=none, 1=playhead, 2=clip move,
                             3=clip resize left, 4=clip resize right,
                             5=pan view                             */
    int   drag_clip;      /* clip index being dragged               */
    int   drag_offset;    /* mouse offset from clip start           */
    int   drag_start_x;   /* mouse x at drag start (for panning)   */
    int   drag_view_start;/* view_start at drag start               */

    /* Layout geometry */
    int   header_width;   /* track label column width (default 80)  */
    int   ruler_height;   /* top ruler height (default 24)          */

    /* Appearance */
    lvg_color_t bg;
    lvg_color_t ruler_bg;
    lvg_color_t ruler_text;
    lvg_color_t track_bg;
    lvg_color_t track_bg_alt;
    lvg_color_t track_border;
    lvg_color_t playhead_color;
    lvg_color_t selection_color;
    lvg_color_t range_color;
    int         playhead_width;   /* default 2 */
    int         clip_corner_radius; /* default 3 */

    /* Callback */
    lui_timeline_event_fn on_event;
    void                 *on_event_user;
} lui_timeline_t;

/**
 * Initialise a timeline with default capacities (LUI_TIMELINE_MAX_TRACKS /
 * LUI_TIMELINE_MAX_CLIPS) using malloc/free. Default: 640x200, 30fps,
 * 1000 frames, 4px/frame zoom. Pair every successful init with destroy.
 */
bool lui_timeline_init(lui_timeline_t *tl);

/** Initialise with caller-supplied capacities / allocator (NULL/NULL = default). */
bool lui_timeline_init_ex(lui_timeline_t *tl,
                           int max_tracks, int max_clips,
                           lui_alloc_fn alloc_fn,
                           lui_free_fn  free_fn,
                           void        *alloc_user);

/** Free heap arrays owned by `tl`. */
void lui_timeline_destroy(lui_timeline_t *tl);

/** Add a track. Returns track index or -1 on failure. */
int lui_timeline_add_track(lui_timeline_t *tl, int id, const char *label);

/** Add a clip. Returns clip index or -1 on failure. */
int lui_timeline_add_clip(lui_timeline_t *tl, int id, int track,
                            int start, int duration, lvg_color_t color,
                            const char *label);

/** Remove a clip by its ID. */
void lui_timeline_remove_clip(lui_timeline_t *tl, int id);

/** Set the playhead position. */
void lui_timeline_set_playhead(lui_timeline_t *tl, int frame);

/** Set the visible frame range (zoom to fit). */
void lui_timeline_set_view(lui_timeline_t *tl, int start_frame,
                             float pixels_per_frame);

/** Zoom in/out centred on a given frame. factor > 1 zooms in. */
void lui_timeline_zoom(lui_timeline_t *tl, float factor, int center_frame);

/** Convert a frame number to pixel X within the timeline area. */
int lui_timeline_frame_to_x(const lui_timeline_t *tl, int frame);

/** Convert pixel X to frame number. */
int lui_timeline_x_to_frame(const lui_timeline_t *tl, int x);

static inline lui_widget_t *lui_timeline_widget(lui_timeline_t *tl) {
    return &tl->widget;
}

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_TIMELINE_H */
