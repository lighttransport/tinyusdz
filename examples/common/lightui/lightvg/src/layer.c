/*
 * lightvg/src/layer.c — Cached off-screen surface layer
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightvg/layer.h>
#include <stdlib.h>
#include <string.h>

lvg_layer_t *lvg_layer_create(int width, int height)
{
    lvg_layer_t *l = (lvg_layer_t *)calloc(1, sizeof(*l));
    if (!l) return NULL;

    l->surface = lvg_surface_create(width, height);
    if (!l->surface) { free(l); return NULL; }

    l->alpha   = 255;
    l->visible = 1;
    lvg_dirty_init(&l->dirty);
    lvg_dirty_mark_all(&l->dirty);   /* new layer: everything needs drawing */
    return l;
}

void lvg_layer_destroy(lvg_layer_t *l)
{
    if (!l) return;
    lvg_surface_destroy(l->surface);
    free(l);
}

int lvg_layer_resize(lvg_layer_t *l, int width, int height)
{
    if (!lvg_surface_resize(l->surface, width, height)) return 0;
    lvg_dirty_mark_all(&l->dirty);
    return 1;
}

int lvg_layer_needs_redraw(const lvg_layer_t *l)
{
    return lvg_dirty_any(&l->dirty);
}

void lvg_layer_invalidate(lvg_layer_t *l, const lvg_rect_t *rect)
{
    if (!rect) {
        lvg_dirty_mark_all(&l->dirty);
    } else {
        lvg_dirty_add(&l->dirty, rect);
    }
}

lvg_canvas_t lvg_layer_begin_draw(lvg_layer_t *l, int clip_to_dirty)
{
    lvg_canvas_t canvas;
    lvg_canvas_init(&canvas, l->surface);

    if (clip_to_dirty && !l->dirty.all && l->dirty.count > 0) {
        lvg_rect_t bounds = lvg_rect_make(0, 0,
                                          l->surface->width,
                                          l->surface->height);
        lvg_rect_t db = lvg_dirty_bounds(&l->dirty, &bounds);
        if (!lvg_rect_is_empty(&db))
            lvg_canvas_set_clip(&canvas, &db);
    }
    return canvas;
}

void lvg_layer_end_draw(lvg_layer_t *l)
{
    lvg_dirty_reset(&l->dirty);
}
