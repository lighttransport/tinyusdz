/*
 * lightvg/types.h — Primitive types used by the VG rasterizer
 *
 * Self-contained: depends only on <stdint.h> / <stdbool.h>.
 * Suitable for reuse outside lightui (drop lightvg/src/ + include/lightvg/ in
 * another project and this header is the only dependency).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTVG_TYPES_H
#define LIGHTVG_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Color — packed 32-bit ARGB (0xAARRGGBB)
 * ------------------------------------------------------------------------- */
typedef uint32_t lvg_color_t;

/* Construction */
#define LVG_COLOR_ARGB(a, r, g, b) \
    (((uint32_t)((a) & 0xFF) << 24) | \
     ((uint32_t)((r) & 0xFF) << 16) | \
     ((uint32_t)((g) & 0xFF) <<  8) | \
      (uint32_t)((b) & 0xFF))
#define LVG_COLOR_RGB(r, g, b)  LVG_COLOR_ARGB(0xFF, (r), (g), (b))

/* Component extraction */
#define LVG_COLOR_A(c)  (((c) >> 24) & 0xFF)
#define LVG_COLOR_R(c)  (((c) >> 16) & 0xFF)
#define LVG_COLOR_G(c)  (((c) >>  8) & 0xFF)
#define LVG_COLOR_B(c)  (((c)      ) & 0xFF)

/* Linear interpolation between two ARGB colours by weight @t in [0,1].
 * Out-of-range t is clamped to the endpoint; NaN t falls back to @a. */
static inline lvg_color_t lvg_color_lerp(lvg_color_t a, lvg_color_t b, float t)
{
    if (!(t > 0.0f)) return a;        /* covers t<=0 and t==NaN */
    if (t >= 1.0f)   return b;
    int ra = (int)LVG_COLOR_R(a), ga = (int)LVG_COLOR_G(a),
        ba = (int)LVG_COLOR_B(a), aa = (int)LVG_COLOR_A(a);
    int rb = (int)LVG_COLOR_R(b), gb = (int)LVG_COLOR_G(b),
        bb = (int)LVG_COLOR_B(b), ab = (int)LVG_COLOR_A(b);
    int r  = ra + (int)((rb - ra) * t);
    int g  = ga + (int)((gb - ga) * t);
    int bl = ba + (int)((bb - ba) * t);
    int al = aa + (int)((ab - aa) * t);
    return LVG_COLOR_ARGB(al, r, g, bl);
}

/* Named colours */
#define LVG_COLOR_TRANSPARENT  LVG_COLOR_ARGB(0x00, 0x00, 0x00, 0x00)
#define LVG_COLOR_BLACK        LVG_COLOR_RGB(0x00, 0x00, 0x00)
#define LVG_COLOR_WHITE        LVG_COLOR_RGB(0xFF, 0xFF, 0xFF)
#define LVG_COLOR_RED          LVG_COLOR_RGB(0xFF, 0x00, 0x00)
#define LVG_COLOR_GREEN        LVG_COLOR_RGB(0x00, 0xFF, 0x00)
#define LVG_COLOR_BLUE         LVG_COLOR_RGB(0x00, 0x00, 0xFF)
#define LVG_COLOR_YELLOW       LVG_COLOR_RGB(0xFF, 0xFF, 0x00)
#define LVG_COLOR_CYAN         LVG_COLOR_RGB(0x00, 0xFF, 0xFF)
#define LVG_COLOR_MAGENTA      LVG_COLOR_RGB(0xFF, 0x00, 0xFF)
#define LVG_COLOR_GRAY         LVG_COLOR_RGB(0x80, 0x80, 0x80)
#define LVG_COLOR_DARK_GRAY    LVG_COLOR_RGB(0x40, 0x40, 0x40)
#define LVG_COLOR_LIGHT_GRAY   LVG_COLOR_RGB(0xC0, 0xC0, 0xC0)

/* -------------------------------------------------------------------------
 * lvg_result_t — status code for APIs that can fail
 *
 * Convention (rolled out opportunistically; existing int-returning
 * functions keep their 0/-1 protocol):
 *   - Constructors return NULL on failure.
 *   - "Do X, might fail" returns lvg_result_t (LVG_OK == 0 on success).
 *   - "Query state / did X happen" stays bool.
 * ------------------------------------------------------------------------- */
typedef enum {
    LVG_OK               =  0,
    LVG_ERR_INVALID      = -1,   /* NULL / out-of-range / wrong-kind arg  */
    LVG_ERR_NOMEM        = -2,   /* allocation failure                    */
    LVG_ERR_UNSUPPORTED  = -3,   /* feature disabled at compile time      */
    LVG_ERR_IO           = -4,   /* write/read callback reported failure  */
} lvg_result_t;

/* -------------------------------------------------------------------------
 * lvg_point_t — 2D integer point
 * ------------------------------------------------------------------------- */
typedef struct {
    int x, y;
} lvg_point_t;

/* -------------------------------------------------------------------------
 * lvg_size_t — 2D integer size (non-negative)
 * ------------------------------------------------------------------------- */
typedef struct {
    int width, height;
} lvg_size_t;

/* -------------------------------------------------------------------------
 * lvg_rect_t — 2D integer rectangle (origin + size)
 * ------------------------------------------------------------------------- */
typedef struct {
    int x, y;
    int width, height;
} lvg_rect_t;

/* Rectangle helpers (static inline so header-only with no LTO cost) */
static inline lvg_rect_t lvg_rect_make(int x, int y, int w, int h) {
    lvg_rect_t r;
    r.x = x; r.y = y; r.width = w; r.height = h;
    return r;
}

static inline bool lvg_rect_is_empty(const lvg_rect_t *r) {
    return r->width <= 0 || r->height <= 0;
}

static inline bool lvg_rect_contains_point(const lvg_rect_t *r, int x, int y) {
    return x >= r->x && y >= r->y &&
           x < r->x + r->width && y < r->y + r->height;
}

static inline bool lvg_rect_overlaps(const lvg_rect_t *a, const lvg_rect_t *b) {
    return a->x < b->x + b->width  && a->x + a->width  > b->x &&
           a->y < b->y + b->height && a->y + a->height > b->y;
}

static inline lvg_rect_t lvg_rect_intersect(const lvg_rect_t *a,
                                             const lvg_rect_t *b) {
    lvg_rect_t r;
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = (a->x + a->width)  < (b->x + b->width)  ? (a->x + a->width)  : (b->x + b->width);
    int y2 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);
    r.x = x1; r.y = y1;
    r.width  = x2 > x1 ? x2 - x1 : 0;
    r.height = y2 > y1 ? y2 - y1 : 0;
    return r;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTVG_TYPES_H */
