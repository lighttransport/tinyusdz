/*
 * timeline.c — Multi-track NLA timeline widget
 *
 * Renders tracks, clips, ruler, and playhead. Supports dragging clips,
 * moving the playhead, resizing clip edges, and pan/zoom.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/timeline.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

static inline int tl_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float tl_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- Coordinate conversion ---------------------------------------------- */

int lui_timeline_frame_to_x(const lui_timeline_t *tl, int frame)
{
    return (int)((float)(frame - tl->view_start) * tl->pixels_per_frame);
}

int lui_timeline_x_to_frame(const lui_timeline_t *tl, int x)
{
    if (tl->pixels_per_frame <= 0.0f) return 0;
    return tl->view_start + (int)((float)x / tl->pixels_per_frame);
}

/* ---- Ruler drawing ------------------------------------------------------ */

static void draw_ruler(const lui_timeline_t *tl, lvg_canvas_t *canvas,
                         int rx, int ry, int rw, int rh)
{
    lvg_canvas_fill_rect(canvas, rx, ry, rw, rh, tl->ruler_bg);

    /* Tick spacing: aim for ticks every ~60 pixels */
    int frames_per_tick = 1;
    float ppf = tl->pixels_per_frame;
    if (ppf > 0) {
        frames_per_tick = (int)(60.0f / ppf);
        if (frames_per_tick < 1) frames_per_tick = 1;
        /* Snap to nice multiples */
        int nice[] = {1, 2, 5, 10, 15, 24, 25, 30, 48, 50, 60,
                      100, 120, 150, 200, 250, 300, 500, 600, 1000};
        int nice_count = (int)(sizeof(nice) / sizeof(nice[0]));
        for (int i = 0; i < nice_count; i++) {
            if (nice[i] >= frames_per_tick) {
                frames_per_tick = nice[i];
                break;
            }
        }
    }

    int first_frame = tl->view_start - (tl->view_start % frames_per_tick);
    if (first_frame < 0) first_frame = 0;

    for (int f = first_frame; f <= tl->total_frames; f += frames_per_tick) {
        int x = rx + lui_timeline_frame_to_x(tl, f);
        if (x < rx) continue;
        if (x >= rx + rw) break;

        /* Tick mark */
        int tick_h = (f % (frames_per_tick * 5) == 0) ? rh * 2 / 3 : rh / 3;
        lvg_canvas_fill_rect(canvas, x, ry + rh - tick_h, 1, tick_h,
                              tl->ruler_text);
    }

    /* Bottom border */
    lvg_canvas_fill_rect(canvas, rx, ry + rh - 1, rw, 1, tl->track_border);
}

/* ---- Playhead / range drawing ------------------------------------------- */

static void draw_playhead(const lui_timeline_t *tl, lvg_canvas_t *canvas,
                            int area_x, int area_y, int area_w, int area_h)
{
    int px = area_x + lui_timeline_frame_to_x(tl, tl->playhead);
    if (px >= area_x && px < area_x + area_w) {
        lvg_canvas_fill_rect(canvas, px - tl->playhead_width / 2, area_y,
                              tl->playhead_width, area_h,
                              tl->playhead_color);
        /* Playhead triangle at top */
        lvg_canvas_fill_triangle(canvas,
                                  px - 5, area_y,
                                  px + 5, area_y,
                                  px, area_y + 7,
                                  tl->playhead_color);
    }
}

static void draw_range(const lui_timeline_t *tl, lvg_canvas_t *canvas,
                         int area_x, int area_y, int area_w, int area_h)
{
    if (tl->range_start >= tl->range_end) return;

    int x0 = area_x + lui_timeline_frame_to_x(tl, tl->range_start);
    int x1 = area_x + lui_timeline_frame_to_x(tl, tl->range_end);

    x0 = tl_clampi(x0, area_x, area_x + area_w);
    x1 = tl_clampi(x1, area_x, area_x + area_w);

    if (x1 > x0)
        lvg_canvas_fill_rect(canvas, x0, area_y, x1 - x0, area_h,
                              tl->range_color);
}

/* ---- Main draw ---------------------------------------------------------- */

static void timeline_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_timeline_t *tl = (lui_timeline_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, tl->bg);

    int hw = tl->header_width;
    int rh = tl->ruler_height;

    /* Track header area */
    int header_x = r.x;

    /* Track content area */
    int track_x = r.x + hw;
    int track_y = r.y + rh;
    int track_w = r.width - hw;
    int track_h = r.height - rh;

    /* Ruler */
    draw_ruler(tl, canvas, track_x, r.y, track_w, rh);

    /* Track backgrounds */
    int y = track_y;
    for (int t = 0; t < tl->track_count && y < r.y + r.height; t++) {
        int th = tl->tracks[t].height;
        lvg_color_t bg = (t % 2) ? tl->track_bg_alt : tl->track_bg;

        /* Header */
        lvg_canvas_fill_rect(canvas, header_x, y, hw, th, bg);
        lvg_canvas_fill_rect(canvas, header_x + hw - 1, y, 1, th,
                              tl->track_border);

        /* Track row */
        lvg_canvas_fill_rect(canvas, track_x, y, track_w, th, bg);

        /* Track border (bottom) */
        lvg_canvas_fill_rect(canvas, r.x, y + th - 1, r.width, 1,
                              tl->track_border);
        y += th;
    }

    /* Range overlay */
    draw_range(tl, canvas, track_x, track_y, track_w, track_h);

    /* Clips */
    for (int i = 0; i < tl->clip_count; i++) {
        lui_timeline_clip_t *clip = &tl->clips[i];
        if (clip->track < 0 || clip->track >= tl->track_count) continue;

        /* Compute track y offset */
        int clip_y = track_y;
        for (int t = 0; t < clip->track; t++)
            clip_y += tl->tracks[t].height;
        int clip_h = tl->tracks[clip->track].height - 2;

        int cx0 = track_x + lui_timeline_frame_to_x(tl, clip->start);
        int cx1 = track_x + lui_timeline_frame_to_x(tl, clip->start + clip->duration);

        /* Clip to visible area */
        if (cx1 < track_x || cx0 >= track_x + track_w) continue;
        if (cx0 < track_x) cx0 = track_x;
        if (cx1 > track_x + track_w) cx1 = track_x + track_w;

        int cw = cx1 - cx0;
        if (cw < 1) continue;

        /* Clip body */
        lvg_color_t cc = clip->color;
        if (clip->selected) {
            /* Brighten selected clips */
            int cr = LVG_COLOR_R(cc);
            int cg = LVG_COLOR_G(cc);
            int cb = LVG_COLOR_B(cc);
            cr = cr + (255 - cr) / 4;
            cg = cg + (255 - cg) / 4;
            cb = cb + (255 - cb) / 4;
            cc = LVG_COLOR_RGB(cr, cg, cb);
        }

        if (tl->clip_corner_radius > 0)
            lvg_canvas_fill_rounded_rect(canvas, cx0, clip_y + 1, cw, clip_h,
                                          tl->clip_corner_radius, cc);
        else
            lvg_canvas_fill_rect(canvas, cx0, clip_y + 1, cw, clip_h, cc);

        /* Selection outline */
        if (clip->selected) {
            if (tl->clip_corner_radius > 0)
                lvg_canvas_stroke_rounded_rect(canvas, cx0, clip_y + 1,
                                                cw, clip_h,
                                                tl->clip_corner_radius,
                                                tl->selection_color, 2);
            else
                lvg_canvas_stroke_rect(canvas, cx0, clip_y + 1, cw, clip_h,
                                        tl->selection_color, 2);
        }
    }

    /* Playhead */
    draw_playhead(tl, canvas, track_x, r.y, track_w, r.height);
}

/* ---- Event handling ----------------------------------------------------- */

static int find_clip_at(const lui_timeline_t *tl, int frame, int track)
{
    for (int i = 0; i < tl->clip_count; i++) {
        if (tl->clips[i].track == track &&
            frame >= tl->clips[i].start &&
            frame < tl->clips[i].start + tl->clips[i].duration)
            return i;
    }
    return -1;
}

static int track_at_y(const lui_timeline_t *tl, int local_y, int ruler_h)
{
    int y = ruler_h;
    for (int t = 0; t < tl->track_count; t++) {
        if (local_y >= y && local_y < y + tl->tracks[t].height)
            return t;
        y += tl->tracks[t].height;
    }
    return -1;
}

static void emit_event(lui_timeline_t *tl, lui_timeline_event_type_t type,
                         int clip_id, int track_id, int frame)
{
    if (!tl->on_event) return;
    lui_timeline_event_t ev = {0};
    ev.type     = type;
    ev.clip_id  = clip_id;
    ev.track_id = track_id;
    ev.frame    = frame;
    tl->on_event(&ev, tl->on_event_user);
}

static int timeline_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_timeline_t *tl = (lui_timeline_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    int hw = tl->header_width;
    int rh = tl->ruler_height;
    int track_x = r.x + hw;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN: {
        if (event->data.mouse_button.button == LUI_MOUSE_MIDDLE) {
            /* Middle-click: start panning */
            tl->drag_mode = 5;
            tl->drag_start_x = event->data.mouse_button.x;
            tl->drag_view_start = tl->view_start;
            return 1;
        }
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;

        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        int local_x = mx - track_x;
        int local_y = my - r.y;

        /* Click on ruler: move playhead */
        if (local_y < rh && local_x >= 0) {
            int frame = lui_timeline_x_to_frame(tl, local_x);
            tl->playhead = tl_clampi(frame, 0, tl->total_frames);
            tl->drag_mode = 1;
            emit_event(tl, LUI_TL_EVENT_PLAYHEAD_MOVED, -1, -1, tl->playhead);
            return 1;
        }

        /* Click on track area */
        if (local_x >= 0 && local_y >= rh) {
            int track = track_at_y(tl, local_y, rh);
            int frame = lui_timeline_x_to_frame(tl, local_x);

            int clip_idx = (track >= 0) ? find_clip_at(tl, frame, track) : -1;

            if (clip_idx >= 0) {
                lui_timeline_clip_t *clip = &tl->clips[clip_idx];

                /* Check if near edge for resize */
                int clip_x0 = lui_timeline_frame_to_x(tl, clip->start);
                int clip_x1 = lui_timeline_frame_to_x(tl, clip->start + clip->duration);

                if (local_x - clip_x0 < 6) {
                    tl->drag_mode = 3; /* resize left */
                    tl->drag_clip = clip_idx;
                } else if (clip_x1 - local_x < 6) {
                    tl->drag_mode = 4; /* resize right */
                    tl->drag_clip = clip_idx;
                } else {
                    tl->drag_mode = 2; /* move */
                    tl->drag_clip = clip_idx;
                    tl->drag_offset = frame - clip->start;
                }

                /* Select/deselect */
                clip->selected = !clip->selected;
                emit_event(tl, LUI_TL_EVENT_CLIP_SELECTED, clip->id,
                           clip->track, frame);

                /* Double click */
                if (event->data.mouse_button.clicks >= 2) {
                    emit_event(tl, LUI_TL_EVENT_CLIP_DOUBLE_CLICK, clip->id,
                               clip->track, frame);
                }

                return 1;
            }

            /* Click on empty area: deselect all, move playhead */
            for (int i = 0; i < tl->clip_count; i++)
                tl->clips[i].selected = false;
            tl->playhead = tl_clampi(frame, 0, tl->total_frames);
            tl->drag_mode = 1;
            emit_event(tl, LUI_TL_EVENT_PLAYHEAD_MOVED, -1, -1, tl->playhead);
            return 1;
        }
        break;
    }

    case LUI_EVENT_MOUSE_MOVE: {
        int mx = event->data.mouse_move.x;
        int local_x = mx - track_x;

        switch (tl->drag_mode) {
        case 1: { /* playhead */
            int frame = lui_timeline_x_to_frame(tl, local_x);
            tl->playhead = tl_clampi(frame, 0, tl->total_frames);
            emit_event(tl, LUI_TL_EVENT_PLAYHEAD_MOVED, -1, -1, tl->playhead);
            return 1;
        }
        case 2: { /* clip move */
            int frame = lui_timeline_x_to_frame(tl, local_x);
            int new_start = frame - tl->drag_offset;
            if (new_start < 0) new_start = 0;
            tl->clips[tl->drag_clip].start = new_start;
            emit_event(tl, LUI_TL_EVENT_CLIP_MOVED,
                       tl->clips[tl->drag_clip].id,
                       tl->clips[tl->drag_clip].track, new_start);
            return 1;
        }
        case 3: { /* resize left edge */
            int frame = lui_timeline_x_to_frame(tl, local_x);
            if (frame < 0) frame = 0;
            lui_timeline_clip_t *clip = &tl->clips[tl->drag_clip];
            int end = clip->start + clip->duration;
            if (frame >= end) frame = end - 1;
            clip->duration = end - frame;
            clip->start = frame;
            emit_event(tl, LUI_TL_EVENT_CLIP_RESIZED, clip->id,
                       clip->track, frame);
            return 1;
        }
        case 4: { /* resize right edge */
            int frame = lui_timeline_x_to_frame(tl, local_x);
            lui_timeline_clip_t *clip = &tl->clips[tl->drag_clip];
            int new_dur = frame - clip->start;
            if (new_dur < 1) new_dur = 1;
            clip->duration = new_dur;
            emit_event(tl, LUI_TL_EVENT_CLIP_RESIZED, clip->id,
                       clip->track, frame);
            return 1;
        }
        case 5: { /* pan view */
            int dx = mx - tl->drag_start_x;
            int frame_dx = (int)((float)dx / tl->pixels_per_frame);
            tl->view_start = tl->drag_view_start - frame_dx;
            if (tl->view_start < 0) tl->view_start = 0;
            return 1;
        }
        }
        break;
    }

    case LUI_EVENT_MOUSE_UP:
        if (tl->drag_mode != 0) {
            tl->drag_mode = 0;
            tl->drag_clip = -1;
            return 1;
        }
        break;

    case LUI_EVENT_SCROLL: {
        /* Scroll wheel zooms */
        int mx = event->data.scroll.x;
        int local_x = mx - track_x;
        int center = lui_timeline_x_to_frame(tl, local_x);

        if (event->data.scroll.delta_y < 0)
            lui_timeline_zoom(tl, 1.2f, center);
        else if (event->data.scroll.delta_y > 0)
            lui_timeline_zoom(tl, 1.0f / 1.2f, center);
        return 1;
    }

    default:
        break;
    }

    return 0;
}

static int timeline_measure(const lui_widget_t *w, int *out_w, int *out_h,
                              void *user)
{
    const lui_timeline_t *tl = (const lui_timeline_t *)w;
    (void)user;
    *out_w = 640;

    /* Height = ruler + all tracks */
    int h = tl->ruler_height;
    for (int i = 0; i < tl->track_count; i++)
        h += tl->tracks[i].height;
    if (h < tl->ruler_height + 64) h = tl->ruler_height + 64;
    *out_h = h;
    return 0;
}

/* ---- Public API --------------------------------------------------------- */

static void *tl_default_alloc(void *u, size_t n) { (void)u; return malloc(n); }
static void  tl_default_free(void *u, void *p)    { (void)u; free(p); }

bool lui_timeline_init_ex(lui_timeline_t *tl,
                           int max_tracks, int max_clips,
                           lui_alloc_fn alloc_fn,
                           lui_free_fn  free_fn,
                           void        *alloc_user)
{
    if (!tl || max_tracks <= 0 || max_clips <= 0) return false;
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = tl_default_alloc;
        free_fn    = tl_default_free;
        alloc_user = NULL;
    }

    size_t tbytes = (size_t)max_tracks * sizeof(lui_timeline_track_t);
    if (tbytes / sizeof(lui_timeline_track_t) != (size_t)max_tracks) return false;
    size_t cbytes = (size_t)max_clips * sizeof(lui_timeline_clip_t);
    if (cbytes / sizeof(lui_timeline_clip_t) != (size_t)max_clips) return false;

    lui_timeline_track_t *tracks = (lui_timeline_track_t *)alloc_fn(alloc_user, tbytes);
    if (!tracks) return false;
    lui_timeline_clip_t  *clips  = (lui_timeline_clip_t  *)alloc_fn(alloc_user, cbytes);
    if (!clips) { free_fn(alloc_user, tracks); return false; }
    memset(tracks, 0, tbytes);
    memset(clips,  0, cbytes);

    memset(tl, 0, sizeof(*tl));
    tl->tracks     = tracks;
    tl->max_tracks = max_tracks;
    tl->clips      = clips;
    tl->max_clips  = max_clips;
    tl->alloc_fn   = alloc_fn;
    tl->free_fn    = free_fn;
    tl->alloc_user = alloc_user;

    lui_widget_init(&tl->widget);
    tl->widget.width    = lvg_size_fill(1);
    tl->widget.height   = lvg_size_hug(200);
    tl->widget.measure  = timeline_measure;
    tl->widget.draw     = timeline_draw;
    tl->widget.on_event = timeline_event;

    tl->track_count   = 0;
    tl->clip_count    = 0;
    tl->playhead      = 0;
    tl->range_start   = 0;
    tl->range_end     = 0;
    tl->total_frames  = 1000;
    tl->fps           = 30.0f;
    tl->view_start    = 0;
    tl->pixels_per_frame = 4.0f;

    tl->drag_mode   = 0;
    tl->drag_clip   = -1;

    tl->header_width  = 80;
    tl->ruler_height  = 24;

    tl->bg              = LVG_COLOR_RGB(0x1A, 0x1A, 0x1A);
    tl->ruler_bg        = LVG_COLOR_RGB(0x28, 0x28, 0x28);
    tl->ruler_text      = LVG_COLOR_RGB(0x90, 0x90, 0x90);
    tl->track_bg        = LVG_COLOR_RGB(0x22, 0x22, 0x22);
    tl->track_bg_alt    = LVG_COLOR_RGB(0x26, 0x26, 0x26);
    tl->track_border    = LVG_COLOR_RGB(0x3A, 0x3A, 0x3A);
    tl->playhead_color  = LVG_COLOR_RGB(0xE0, 0x40, 0x40);
    tl->selection_color = LVG_COLOR_RGB(0xFF, 0xCC, 0x00);
    tl->range_color     = LVG_COLOR_ARGB(0x20, 0x60, 0xA0, 0xFF);
    tl->playhead_width  = 2;
    tl->clip_corner_radius = 3;

    tl->on_event      = NULL;
    tl->on_event_user = NULL;

    return true;
}

bool lui_timeline_init(lui_timeline_t *tl)
{
    return lui_timeline_init_ex(tl,
                                 LUI_TIMELINE_MAX_TRACKS,
                                 LUI_TIMELINE_MAX_CLIPS,
                                 NULL, NULL, NULL);
}

void lui_timeline_destroy(lui_timeline_t *tl)
{
    if (!tl) return;
    if (tl->free_fn) {
        if (tl->tracks) tl->free_fn(tl->alloc_user, tl->tracks);
        if (tl->clips)  tl->free_fn(tl->alloc_user, tl->clips);
    }
    tl->tracks      = NULL;
    tl->clips       = NULL;
    tl->track_count = 0;
    tl->clip_count  = 0;
    tl->max_tracks  = 0;
    tl->max_clips   = 0;
}

int lui_timeline_add_track(lui_timeline_t *tl, int id, const char *label)
{
    if (!tl || tl->track_count >= tl->max_tracks) return -1;
    int idx = tl->track_count;
    lui_timeline_track_t *t = &tl->tracks[idx];
    memset(t, 0, sizeof(*t));
    t->id     = id;
    t->label  = label;
    t->muted  = false;
    t->locked = false;
    t->color  = LVG_COLOR_RGB(0x80, 0x80, 0x80);
    t->height = 32;
    tl->track_count++;
    return idx;
}

int lui_timeline_add_clip(lui_timeline_t *tl, int id, int track,
                            int start, int duration, lvg_color_t color,
                            const char *label)
{
    if (!tl || tl->clip_count >= tl->max_clips) return -1;
    int idx = tl->clip_count;
    lui_timeline_clip_t *c = &tl->clips[idx];
    memset(c, 0, sizeof(*c));
    c->id       = id;
    c->track    = track;
    c->start    = start;
    c->duration = duration;
    c->color    = color;
    c->label    = label;
    c->selected = false;
    tl->clip_count++;
    return idx;
}

void lui_timeline_remove_clip(lui_timeline_t *tl, int id)
{
    if (!tl) return;
    for (int i = 0; i < tl->clip_count; i++) {
        if (tl->clips[i].id == id) {
            for (int j = i; j < tl->clip_count - 1; j++)
                tl->clips[j] = tl->clips[j + 1];
            tl->clip_count--;
            return;
        }
    }
}

void lui_timeline_set_playhead(lui_timeline_t *tl, int frame)
{
    if (!tl) return;
    tl->playhead = tl_clampi(frame, 0, tl->total_frames);
}

void lui_timeline_set_view(lui_timeline_t *tl, int start_frame,
                             float pixels_per_frame)
{
    if (!tl) return;
    tl->view_start = start_frame >= 0 ? start_frame : 0;
    tl->pixels_per_frame = tl_clampf(pixels_per_frame, 0.1f, 100.0f);
}

void lui_timeline_zoom(lui_timeline_t *tl, float factor, int center_frame)
{
    if (!tl || factor <= 0.0f) return;

    float old_ppf = tl->pixels_per_frame;
    float new_ppf = tl_clampf(old_ppf * factor, 0.1f, 100.0f);
    tl->pixels_per_frame = new_ppf;

    /* Adjust view_start to keep center_frame at the same screen position */
    float old_x = (float)(center_frame - tl->view_start) * old_ppf;
    int new_start = center_frame - (int)(old_x / new_ppf);
    tl->view_start = new_start >= 0 ? new_start : 0;
}
