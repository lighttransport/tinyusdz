/*
 * lighttype/types.h — Basic types for LightType font library
 *
 * Layout-compatible with LightVG's lvg_color_t, lvg_rect_t for
 * zero-cost bridging.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTTYPE_TYPES_H
#define LIGHTTYPE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Color — packed 32-bit ARGB (0xAARRGGBB) ---------------------------- */

typedef uint32_t ltt_color_t;

#define LTT_COLOR_ARGB(a, r, g, b) \
    (((uint32_t)((a) & 0xFF) << 24) | \
     ((uint32_t)((r) & 0xFF) << 16) | \
     ((uint32_t)((g) & 0xFF) <<  8) | \
      (uint32_t)((b) & 0xFF))
#define LTT_COLOR_RGB(r, g, b)  LTT_COLOR_ARGB(0xFF, (r), (g), (b))

#define LTT_COLOR_A(c)  (((c) >> 24) & 0xFF)
#define LTT_COLOR_R(c)  (((c) >> 16) & 0xFF)
#define LTT_COLOR_G(c)  (((c) >>  8) & 0xFF)
#define LTT_COLOR_B(c)  (((c)      ) & 0xFF)

#define LTT_COLOR_BLACK        LTT_COLOR_RGB(0x00, 0x00, 0x00)
#define LTT_COLOR_WHITE        LTT_COLOR_RGB(0xFF, 0xFF, 0xFF)
#define LTT_COLOR_TRANSPARENT  LTT_COLOR_ARGB(0x00, 0x00, 0x00, 0x00)

/* ---- Rectangle ---------------------------------------------------------- */

typedef struct {
    int x, y;
    int width, height;
} ltt_rect_t;

/* ---- Render target (replaces canvas dependency) ------------------------- */

/**
 * A render target for glyph blitting.
 *
 * LightType draws glyphs into this pixel buffer with sRGB-correct
 * linear blending.  Callers construct an ltt_target_t from whatever
 * pixel buffer they use (e.g. a LightUI surface, an SDL surface, a
 * raw malloc'd buffer, etc.).
 *
 * pixels  — ARGB (0xAARRGGBB) row-major pixel buffer.
 * stride  — row stride in uint32_t units (>= width).
 * clip    — clipping rectangle in pixel coordinates.
 */
typedef struct {
    uint32_t  *pixels;
    int        width, height;
    int        stride;
    ltt_rect_t clip;
} ltt_target_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTTYPE_TYPES_H */
