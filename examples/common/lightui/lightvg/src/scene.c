/*
 * src/scene.c — Ordered layer stack with dirty-region compositing
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightvg/scene.h>
#include <string.h>

#include "internal/pixel_blend.h"

void lvg_scene_init(lvg_scene_t *scene)
{
    memset(scene, 0, sizeof(*scene));
    lvg_dirty_init(&scene->dirty);
}

int lvg_scene_add_layer(lvg_scene_t *scene, lvg_layer_t *layer)
{
    if (scene->count >= LVG_SCENE_MAX_LAYERS) return -1;
    scene->layers[scene->count] = layer;
    return scene->count++;
}

void lvg_scene_remove_layer(lvg_scene_t *scene, int idx)
{
    if (idx < 0 || idx >= scene->count) return;
    for (int i = idx; i < scene->count - 1; i++)
        scene->layers[i] = scene->layers[i + 1];
    scene->layers[--scene->count] = NULL;
}

void lvg_scene_invalidate_layer(lvg_scene_t *scene, int idx,
                                  const lvg_rect_t *rect)
{
    if (idx < 0 || idx >= scene->count) return;
    lvg_layer_t *layer = scene->layers[idx];

    /* Mark layer content dirty. */
    lvg_layer_invalidate(layer, rect);

    /* Propagate to scene-space dirty (offset by layer position). */
    if (!rect) {
        lvg_dirty_mark_all(&scene->dirty);
    } else {
        lvg_rect_t scene_rect = lvg_rect_make(
            layer->x + rect->x, layer->y + rect->y,
            rect->width, rect->height);
        lvg_dirty_add(&scene->dirty, &scene_rect);
    }
}

void lvg_scene_invalidate_all(lvg_scene_t *scene)
{
    lvg_dirty_mark_all(&scene->dirty);
    for (int i = 0; i < scene->count; i++)
        if (scene->layers[i]) lvg_dirty_mark_all(&scene->layers[i]->dirty);
}

/* ---- Composite callback -------------------------------------------------- */

typedef struct {
    lvg_canvas_t *canvas;
    lvg_layer_t  *layer;
} composite_ctx_t;

static void composite_region(const lvg_rect_t *clip, void *user)
{
    composite_ctx_t *ctx = (composite_ctx_t *)user;
    lvg_canvas_t    *c   = ctx->canvas;
    lvg_layer_t     *l   = ctx->layer;

    lvg_rect_t saved_clip = c->_clip;
    lvg_canvas_set_clip(c, clip);

    if (l->alpha == 255) {
        lvg_canvas_blit(c, l->x, l->y, l->surface, NULL);
    } else {
        /*
         * Semi-transparent layer: the source surface pixels are assumed opaque
         * (alpha = 255 in the pixel data).  We need to composite with layer
         * alpha as a constant multiplier.
         *
         * Implemented by reading each pixel and replacing its stored alpha with
         * layer->alpha before blending.  This is done inline here to avoid a
         * dedicated canvas primitive for this uncommon path.
         */
        const lvg_rect_t *dst_clip = &c->_clip;
        lvg_surface_t    *dst  = c->_surface;
        lvg_surface_t    *src  = l->surface;

        int x0 = l->x > dst_clip->x ? l->x : dst_clip->x;
        int y0 = l->y > dst_clip->y ? l->y : dst_clip->y;
        int x1 = (l->x + src->width)  < (dst_clip->x + dst_clip->width)  ?
                  (l->x + src->width)  : (dst_clip->x + dst_clip->width);
        int y1 = (l->y + src->height) < (dst_clip->y + dst_clip->height) ?
                  (l->y + src->height) : (dst_clip->y + dst_clip->height);

        /* Replace the source alpha with the layer alpha, then SRC_OVER
         * through the shared blender. The destination is always composited
         * onto an opaque backing, so we force alpha=0xFF on write to keep
         * the output opaque (matches the previous scalar formula). */
        for (int row = y0; row < y1; row++) {
            const uint32_t *sp = &src->pixels[(row - l->y) * src->stride + (x0 - l->x)];
            uint32_t       *dp = &dst->pixels[row * dst->stride + x0];
            for (int col = x0; col < x1; col++, sp++, dp++) {
                uint32_t pix = (*sp & 0x00FFFFFFu) |
                               ((uint32_t)l->alpha << 24);
                uint32_t blended = lvg_px_blend_over(*dp | 0xFF000000u, pix);
                *dp = 0xFF000000u | (blended & 0x00FFFFFFu);
            }
        }
    }

    c->_clip = saved_clip;
}

/* ---- Public composite ---------------------------------------------------- */

int lvg_scene_composite(lvg_scene_t *scene, lvg_canvas_t *canvas,
                        lvg_deadline_fn deadline_fn,
                        void *deadline_userdata)
{
    int composited = 0;

    for (int i = 0; i < scene->count; i++) {
        lvg_layer_t *layer = scene->layers[i];
        if (!layer || !layer->visible) { composited++; continue; }

        /* Check deadline before each layer. */
        if (deadline_fn && deadline_fn(deadline_userdata)) break;

        /* Layer's bounding rect in scene coordinates. */
        lvg_rect_t layer_rect = lvg_rect_make(layer->x, layer->y,
                                               layer->surface->width,
                                               layer->surface->height);

        /* Skip if this layer doesn't overlap any scene dirty region. */
        if (!scene->dirty.all && !lvg_dirty_test(&scene->dirty, &layer_rect)) {
            composited++;
            continue;
        }

        /* Composite only the dirty portions of this layer. */
        composite_ctx_t ctx = { canvas, layer };
        lvg_dirty_foreach(&scene->dirty, &layer_rect, composite_region, &ctx);

        composited++;
    }

    return composited;
}

void lvg_scene_flush_dirty(lvg_scene_t *scene)
{
    lvg_dirty_reset(&scene->dirty);
}
