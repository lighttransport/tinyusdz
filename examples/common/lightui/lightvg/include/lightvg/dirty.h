/*
 * lightvg/dirty.h — Dirty rectangle tracker
 *
 * Tracks up to LVG_DIRTY_MAX_RECTS independent dirty regions.  When the list
 * overflows all existing rects are merged into their bounding box (one rect)
 * before the new one is appended.  This keeps memory bounded while
 * over-approximating the dirty area conservatively.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTVG_DIRTY_H
#define LIGHTVG_DIRTY_H

#include "types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LVG_DIRTY_MAX_RECTS 8

typedef struct {
    lvg_rect_t rects[LVG_DIRTY_MAX_RECTS];
    int        count;  /* number of valid rects (0 when clean or all==true) */
    bool       all;    /* entire area is dirty — skip per-rect tests        */
} lvg_dirty_t;

/* Initialise to clean state. */
void lvg_dirty_init(lvg_dirty_t *d);

/* Reset to clean state (no dirty regions). */
void lvg_dirty_reset(lvg_dirty_t *d);

/* Add a dirty region.  Empty rects are ignored. */
void lvg_dirty_add(lvg_dirty_t *d, const lvg_rect_t *r);

/* Mark the entire area dirty.  Clears the rect list. */
void lvg_dirty_mark_all(lvg_dirty_t *d);

/* Returns true if @r overlaps any dirty region (or all==true). */
bool lvg_dirty_test(const lvg_dirty_t *d, const lvg_rect_t *r);

/* Returns true if there is any dirty region at all. */
bool lvg_dirty_any(const lvg_dirty_t *d);

/*
 * Bounding box of all dirty rects.
 * If all==true, @fallback is returned (caller's total area).
 * Returns empty rect when clean.
 */
lvg_rect_t lvg_dirty_bounds(const lvg_dirty_t *d, const lvg_rect_t *fallback);

/*
 * For each dirty rect that intersects @bounds, compute the intersection
 * and invoke @fn(clip, user).  If all==true, @fn is called once with @bounds.
 */
typedef void (*lvg_dirty_clip_fn)(const lvg_rect_t *clip, void *user);
void lvg_dirty_foreach(const lvg_dirty_t *d, const lvg_rect_t *bounds,
                       lvg_dirty_clip_fn fn, void *user);

/*
 * Translate all dirty rects by (dx, dy).
 * Used when converting layer-local dirty coords to scene coords.
 */
void lvg_dirty_translate(lvg_dirty_t *dst, const lvg_dirty_t *src, int dx, int dy);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTVG_DIRTY_H */
