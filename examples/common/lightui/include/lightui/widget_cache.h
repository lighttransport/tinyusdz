/*
 * lightui/widget_cache.h — Opt-in per-widget pixel cache
 *
 * A cached widget renders its whole subtree once into an off-screen surface;
 * on later frames, while it is clean, the dirty-draw traversal replays those
 * pixels with a fast blit instead of re-rasterising the subtree.  This pays off
 * for complex but rarely-changing widgets (menus, toolbars, panels) that are
 * frequently re-presented because a neighbour keeps changing.
 *
 * Caching is correct only for OPAQUE, self-contained widgets: the cache blit is
 * a straight copy (no alpha), so a transparent widget over a moving background
 * would replay a stale background.  Invalidate the cached widget itself (not its
 * children) when its content changes; lui_widget_invalidate() clears the cache.
 *
 * Cached surfaces live in a process-global pool with a memory budget.  When a
 * new allocation would exceed the budget, surfaces are evicted lowest-priority
 * and least-recently-used first — so a HIGH-priority menu outlives a LOW one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_WIDGET_CACHE_H
#define LIGHTUI_WIDGET_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <lightvg/surface.h>
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Eviction-priority hint.  Higher survives longer under memory pressure. */
typedef enum {
    LUI_CACHE_LOW    = 0,
    LUI_CACHE_NORMAL = 1,
    LUI_CACHE_HIGH   = 2,   /* e.g. frequently shown items like menus */
} lui_cache_priority_t;

struct lui_widget_cache {
    lvg_surface_t       *surface;   /* widget-sized cache (owned), or NULL    */
    int                  w, h;      /* size the surface was built for         */
    lui_cache_priority_t priority;  /* eviction hint                          */
    uint32_t             last_used; /* frame index of last hit/build (LRU)    */
};

/* ---- Application API ----------------------------------------------------- */

/** Enable caching on @w with the given priority.  Lazily allocates the cache
 *  surface on first draw.  Safe to call repeatedly (updates the priority). */
void lui_widget_enable_cache(lui_widget_t *w, lui_cache_priority_t prio);

/** Disable caching on @w and free its cache surface. */
void lui_widget_disable_cache(lui_widget_t *w);

/** Set the global cache memory budget in bytes (default 64 MiB). */
void lui_widget_cache_set_budget(size_t bytes);

/** Query pool usage.  Any out-pointer may be NULL. */
void lui_widget_cache_get_stats(size_t *used_bytes, size_t *budget_bytes,
                                int *live_count);

/* ---- Internal: used by the dirty-draw traversal -------------------------- */

/** Advance the frame counter (called once per dirty-draw pass). */
void     lui_widget_cache_tick(void);
/** Current frame counter. */
uint32_t lui_widget_cache_frame(void);
/** Ensure @w has a @width x @height cache surface (allocating / evicting as
 *  needed).  Returns true if a surface of that size is available. */
bool     lui_widget_cache_ensure(lui_widget_t *w, int width, int height);
/** Mark @w's cache as used this frame (for LRU). */
void     lui_widget_cache_touch(lui_widget_t *w);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_WIDGET_CACHE_H */
