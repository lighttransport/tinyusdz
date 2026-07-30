/*
 * lightvg/layer.h — Cached off-screen surface layer
 *
 * A layer is an off-screen pixel buffer with position, opacity, and a dirty
 * tracker.  Applications draw into layers; the scene composites them.
 *
 * Typical use:
 *
 *   // Setup (once)
 *   lvg_layer_t *bg    = lvg_layer_create(800, 600);
 *   lvg_layer_t *text  = lvg_layer_create(800, 600);
 *   lvg_layer_t *anim  = lvg_layer_create(800, 600);
 *
 *   // Each frame: only redraw what changed
 *   if (lvg_layer_needs_redraw(text)) {
 *       lvg_canvas_t c = lvg_layer_begin_draw(text, NULL);  // clip to dirty
 *       draw_text_content(&c);
 *       lvg_layer_end_draw(text);
 *   }
 *   // anim is always dirty (animation)
 *   {
 *       lvg_canvas_t c = lvg_layer_begin_draw(anim, NULL);
 *       draw_animation_frame(&c);
 *       lvg_layer_end_draw(anim);
 *   }
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTVG_LAYER_H
#define LIGHTVG_LAYER_H

#include "surface.h"
#include "canvas.h"
#include "dirty.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lvg_surface_t *surface;    /* off-screen pixel buffer (owned)            */
    int            x, y;       /* position in parent/scene coordinates       */
    uint8_t        alpha;      /* layer opacity: 0 = invisible, 255 = opaque */
    int            visible;    /* 0 = skip during composite                  */
    lvg_dirty_t    dirty;      /* regions needing redraw within this layer   */
} lvg_layer_t;

/* Allocate a layer with a pixel buffer of the given size. */
lvg_layer_t *lvg_layer_create(int width, int height);

/* Free a layer and its pixel buffer. */
void lvg_layer_destroy(lvg_layer_t *layer);

/* Resize the layer's pixel buffer. Content is undefined after resize. */
int  lvg_layer_resize(lvg_layer_t *layer, int width, int height);

/* Returns 1 if any dirty region is pending. */
int  lvg_layer_needs_redraw(const lvg_layer_t *layer);

/*
 * Mark a region of the layer dirty (needs redraw by the application).
 * Pass NULL to mark the entire layer dirty.
 */
void lvg_layer_invalidate(lvg_layer_t *layer, const lvg_rect_t *rect);

/*
 * Begin drawing into the layer.
 *
 * Returns a canvas backed by the layer surface.  If @clip_to_dirty is
 * non-zero the canvas clip is set to the dirty bounding box, restricting
 * draw calls to just the region that needs updating.  Pass 0 to get a
 * full-surface clip (useful when the entire layer is rebuilt each frame).
 *
 * Call lvg_layer_end_draw() after drawing to mark the layer clean.
 */
lvg_canvas_t lvg_layer_begin_draw(lvg_layer_t *layer, int clip_to_dirty);

/*
 * Mark the layer clean after drawing.  The dirty list is cleared.
 */
void lvg_layer_end_draw(lvg_layer_t *layer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTVG_LAYER_H */
