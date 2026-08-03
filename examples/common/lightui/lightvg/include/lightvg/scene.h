/*
 * lightvg/scene.h — Ordered layer stack with dirty-region compositing
 *
 * A scene holds an ordered list of layers (back to front).  Compositing
 * paints only the dirty regions of each visible layer onto the target canvas,
 * and can respect a caller-supplied deadline callback for early termination.
 *
 * Dirty propagation model:
 *   1. Application modifies layer content →
 *        lvg_scene_invalidate_layer(scene, idx, rect)
 *      which records the dirty rect in both the layer and the scene.
 *   2. lvg_scene_composite() composites only dirty scene regions.
 *   3. lvg_scene_flush_dirty() clears the scene dirty after presenting.
 *
 * Early-termination / tearing:
 *   If the deadline callback trips mid-composite, the function returns before
 *   all layers are drawn.  The remaining layers stay dirty, and the next
 *   composite will pick up where this one left off.  The partial frame can be
 *   presented immediately when the caller wants lower latency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTVG_SCENE_H
#define LIGHTVG_SCENE_H

#include "layer.h"
#include "canvas.h"
#include "dirty.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LVG_SCENE_MAX_LAYERS 64

typedef struct {
    lvg_layer_t *layers[LVG_SCENE_MAX_LAYERS];
    int          count;
    lvg_dirty_t  dirty;   /* scene-space aggregate dirty */
} lvg_scene_t;

/* Optional deadline callback for interruptible compositing. Return non-zero
 * to stop before compositing the next layer. */
typedef int (*lvg_deadline_fn)(void *userdata);

/* Initialise a scene (stack allocation OK). */
void lvg_scene_init(lvg_scene_t *scene);

/*
 * Append a layer to the scene (drawn last = on top).
 * Returns the layer index, or -1 if the scene is full.
 */
int lvg_scene_add_layer(lvg_scene_t *scene, lvg_layer_t *layer);

/* Remove a layer by index.  Remaining layers shift down. */
void lvg_scene_remove_layer(lvg_scene_t *scene, int index);

/*
 * Invalidate a rect in layer-local coordinates.
 * Marks both the layer and the corresponding scene-space region dirty.
 * Pass rect==NULL to invalidate the entire layer.
 */
void lvg_scene_invalidate_layer(lvg_scene_t *scene, int index,
                                 const lvg_rect_t *rect);

/* Mark the entire scene dirty (forces full re-composite). */
void lvg_scene_invalidate_all(lvg_scene_t *scene);

/*
 * Composite all visible layers back-to-front onto @canvas.
 *
 * Only regions that intersect the scene's dirty list are painted.
 * Before compositing each layer, @deadline_fn is checked; if it returns
 * non-zero, compositing stops and the function returns.
 *
 * @deadline_fn  Deadline callback (may be NULL to disable deadline checking).
 *
 * Returns the number of layers fully composited.  If the return value is
 * less than scene->count, some layers were skipped due to the deadline —
 * the scene's dirty list is NOT cleared for those layers.
 */
int lvg_scene_composite(lvg_scene_t *scene, lvg_canvas_t *canvas,
                        lvg_deadline_fn deadline_fn,
                        void *deadline_userdata);

/*
 * Clear the scene dirty list after the frame has been presented.
 * Only call this after a complete composite (all layers painted).
 */
void lvg_scene_flush_dirty(lvg_scene_t *scene);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTVG_SCENE_H */
