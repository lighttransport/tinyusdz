/*
 * src/fonts/rasterize.h — Glyph outline to bitmap rasterizer
 *
 * Analytic-area scanline rasterizer using non-zero winding rule.
 * Handles quadratic Bezier (TrueType) and cubic Bezier (CFF) outlines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTTYPE_RASTERIZE_H
#define LIGHTTYPE_RASTERIZE_H

#include <lighttype/ttf_parse.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *pixels;       /* row-major coverage bitmap (caller frees) */
    int      width;
    int      height;
    int      bearing_x;    /* left side bearing in pixels */
    int      bearing_y;    /* top bearing (baseline to top of bitmap) */
} rast_bitmap_t;

/*
 * Rasterize a glyph outline at the given scale.
 * scale = pixel_size / units_per_em.
 *
 * On success, fills out bm with an allocated coverage bitmap.
 * Caller must free bm->pixels with free().
 *
 * Returns 0 on success, -1 on failure (empty glyph, OOM).
 */
int rast_glyph(const ttf_outline_t *outline, float scale, rast_bitmap_t *bm);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTTYPE_RASTERIZE_H */
