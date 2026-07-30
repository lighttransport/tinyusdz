/*
 * canvas.c — 2D software rasterizer
 *
 * All drawing operations are clipped to canvas->_clip and alpha-composited
 * using the Porter-Duff SRC_OVER operator.
 *
 * Pixel format: 0xAARRGGBB (non-premultiplied alpha), stored as uint32_t
 * in row-major order.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightvg/canvas.h>
#include <lightvg/math.h>

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "canvas_ops.h"
#include "internal/pixel_blend.h"
#include <lightvg/canvas_backend.h>

/*
 * Optional SIMD. Default is portable scalar SWAR; define at compile time
 * (-DLVG_USE_SSE2=1 or -DLVG_USE_NEON=1) to enable the platform
 * SIMD fast paths. Only one may be enabled at a time.
 */
#ifndef LVG_USE_SSE2
#define LVG_USE_SSE2 0
#endif
#ifndef LVG_USE_NEON
#define LVG_USE_NEON 0
#endif

#if LVG_USE_SSE2 && LVG_USE_NEON
#error "LVG_USE_SSE2 and LVG_USE_NEON are mutually exclusive"
#endif

#if LVG_USE_SSE2
#if !defined(__SSE2__) && !defined(_M_X64)
#error "LVG_USE_SSE2 requires an SSE2-capable target (compile with -msse2)"
#endif
#include <emmintrin.h>
#endif

#if LVG_USE_NEON
#if !defined(__ARM_NEON) && !defined(__ARM_NEON__)
#error "LVG_USE_NEON requires an ARM target with NEON"
#endif
#include <arm_neon.h>
#endif

/* Round-to-nearest float→int. lroundf is a libm function call; on x86 the
 * AGG fixed-point conversion of every polygon vertex is hot enough that
 * the call overhead shows up in profiles. cvtss2si rounds to nearest-even
 * by default, which is identical to lroundf for the values we feed it
 * (well-bounded coordinate space, never exact .5 across many vertices). */
static inline int lvg__roundf_to_int(float v)
{
#if LVG_USE_SSE2
    return _mm_cvtss_si32(_mm_set_ss(v));
#else
    return (int)lroundf(v);
#endif
}

/*
 * Pluggable-backend dispatch.
 *
 * Every public lvg_canvas_* primitive below starts with LVG_DISPATCH, which
 * forwards to the corresponding ops slot if one is installed. A NULL slot
 * (the default, and the entire software path) falls through to the software
 * body that follows — zero-cost in the common case (one NULL check).
 */
#define LVG_DISPATCH(name, ...) \
    do { \
        if (!canvas) return; \
        if (canvas->_ops && canvas->_ops->name) { \
            canvas->_ops->name(canvas, __VA_ARGS__); \
            return; \
        } \
    } while (0)

/* Forward decls for the AGG-mode polygon batch (defined later). */
static void lvg__flush_polygon_batch(lvg_canvas_t *cv);
static void lvg__discard_polygon_batch(void);

#define LVG_DISPATCH0(name) \
    do { \
        if (!canvas) return; \
        if (canvas->_ops && canvas->_ops->name) { \
            canvas->_ops->name(canvas); \
            return; \
        } \
    } while (0)

/* Backend init hooks — provided by linked-in backend translation units. */
#ifdef LVG_HAVE_BLEND2D
extern bool lvg_canvas_blend2d_init(lvg_canvas_t *canvas);
#endif
#ifdef LVG_HAVE_THORVG
extern bool lvg_canvas_thorvg_init(lvg_canvas_t *canvas);
#endif
#ifdef LVG_HAVE_SKIA
extern bool lvg_canvas_skia_init(lvg_canvas_t *canvas);
#endif

/* Alias libm names to our libm-free replacements. The rasterizer is written
 * against the standard names; these macros redirect to lightvg/math.h so the
 * compiled TU has no libm dependency. Override any of LVG_SQRTF etc.
 * before including lightvg/math.h to swap in a project-specific implementation. */
#define sqrtf(x)   LVG_SQRTF(x)
#define sinf(x)    LVG_SINF(x)
#define cosf(x)    LVG_COSF(x)
#define fabsf(x)   LVG_FABSF(x)
#define floorf(x)  LVG_FLOORF(x)
#define ceilf(x)   LVG_CEILF(x)
#define roundf(x)  LVG_ROUNDF(x)
#define sqrt(x)    LVG_SQRT(x)
#define sin(x)     LVG_SIN(x)
#define floor(x)   LVG_FLOOR(x)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/* Integer square-root (Newton–Raphson, non-negative input). */
static int lvg__isqrt(int n)
{
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/*
 * SRC_OVER primitives (lvg_px_blend_over, lvg_px_div255, LVG_RB_{MASK,HALF})
 * live in src/internal/pixel_blend.h so the scene compositor and glyph
 * blender share the same formulas.
 */

/*
 * Write a single pixel at (x, y) after clip and alpha-blend checks.
 * Inlined here; callers that do their own clip/bounds checks bypass this.
 */
static inline void lvg__set_pixel(lvg_canvas_t *c, int x, int y, uint32_t col)
{
    const lvg_rect_t *clip = &c->_clip;
    if (x < clip->x || y < clip->y ||
        x >= clip->x + clip->width ||
        y >= clip->y + clip->height)
        return;
    uint32_t *p = &c->_surface->pixels[y * c->_surface->stride + x];
    *p = lvg_px_blend_over(*p, col);
}

static inline int lvg__clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline double lvg__clamp_double(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint32_t lvg__pack_sample(double pr, double pg, double pb, double pa)
{
    double r, g, b;

    if (pa <= 0.0) return 0;
    if (pa > 1.0) pa = 1.0;

    r = pr / pa;
    g = pg / pa;
    b = pb / pa;

    if (r < 0.0) r = 0.0; else if (r > 255.0) r = 255.0;
    if (g < 0.0) g = 0.0; else if (g > 255.0) g = 255.0;
    if (b < 0.0) b = 0.0; else if (b > 255.0) b = 255.0;

    return ((uint32_t)(pa * 255.0 + 0.5) << 24) |
           ((uint32_t)(r + 0.5) << 16) |
           ((uint32_t)(g + 0.5) <<  8) |
            (uint32_t)(b + 0.5);
}

static inline void lvg__sample_accumulate(const lvg_surface_t *src, int x, int y,
                                          double weight,
                                          double *sum_pr, double *sum_pg,
                                          double *sum_pb, double *sum_pa,
                                          double *sum_w)
{
    uint32_t p = src->pixels[y * src->stride + x];
    double a = (double)LVG_COLOR_A(p) / 255.0;

    *sum_pr += weight * (double)LVG_COLOR_R(p) * a;
    *sum_pg += weight * (double)LVG_COLOR_G(p) * a;
    *sum_pb += weight * (double)LVG_COLOR_B(p) * a;
    *sum_pa += weight * a;
    *sum_w  += weight;
}

static uint32_t lvg__sample_nearest(const lvg_surface_t *src,
                                    int sx, int sy, int sw, int sh,
                                    double u, double v)
{
    int x, y;

    if (sw <= 0 || sh <= 0) return 0;

    x = lvg__clamp_int((int)floor(u + 0.5), sx, sx + sw - 1);
    y = lvg__clamp_int((int)floor(v + 0.5), sy, sy + sh - 1);
    return src->pixels[y * src->stride + x];
}

static uint32_t lvg__sample_bilinear(const lvg_surface_t *src,
                                     int sx, int sy, int sw, int sh,
                                     double u, double v)
{
    double sum_pr = 0.0, sum_pg = 0.0, sum_pb = 0.0, sum_pa = 0.0, sum_w = 0.0;
    int x0, x1, y0, y1;
    double tx, ty;

    if (sw <= 0 || sh <= 0) return 0;

    u = lvg__clamp_double(u, (double)sx, (double)(sx + sw - 1));
    v = lvg__clamp_double(v, (double)sy, (double)(sy + sh - 1));

    x0 = (int)floor(u);
    y0 = (int)floor(v);
    x1 = x0 + 1;
    y1 = y0 + 1;
    if (x1 >= sx + sw) x1 = sx + sw - 1;
    if (y1 >= sy + sh) y1 = sy + sh - 1;

    tx = u - (double)x0;
    ty = v - (double)y0;

    lvg__sample_accumulate(src, x0, y0, (1.0 - tx) * (1.0 - ty),
                           &sum_pr, &sum_pg, &sum_pb, &sum_pa, &sum_w);
    lvg__sample_accumulate(src, x1, y0, tx * (1.0 - ty),
                           &sum_pr, &sum_pg, &sum_pb, &sum_pa, &sum_w);
    lvg__sample_accumulate(src, x0, y1, (1.0 - tx) * ty,
                           &sum_pr, &sum_pg, &sum_pb, &sum_pa, &sum_w);
    lvg__sample_accumulate(src, x1, y1, tx * ty,
                           &sum_pr, &sum_pg, &sum_pb, &sum_pa, &sum_w);

    if (sum_w > 0.0) {
        sum_pr /= sum_w;
        sum_pg /= sum_w;
        sum_pb /= sum_w;
        sum_pa /= sum_w;
    }

    return lvg__pack_sample(sum_pr, sum_pg, sum_pb, sum_pa);
}

static double lvg__sinc(double x)
{
    if (x == 0.0) return 1.0;
    x *= M_PI;
    return sin(x) / x;
}

static double lvg__lanczos3(double x)
{
    if (x <= -3.0 || x >= 3.0) return 0.0;
    return lvg__sinc(x) * lvg__sinc(x / 3.0);
}

static uint32_t lvg__sample_lanczos3(const lvg_surface_t *src,
                                     int sx, int sy, int sw, int sh,
                                     double u, double v)
{
    double sum_pr = 0.0, sum_pg = 0.0, sum_pb = 0.0, sum_pa = 0.0, sum_w = 0.0;
    int ix, iy;

    if (sw <= 0 || sh <= 0) return 0;

    for (iy = (int)floor(v) - 2; iy <= (int)floor(v) + 3; iy++) {
        double wy = lvg__lanczos3(v - (double)iy);
        int cy;
        if (wy == 0.0) continue;
        cy = lvg__clamp_int(iy, sy, sy + sh - 1);

        for (ix = (int)floor(u) - 2; ix <= (int)floor(u) + 3; ix++) {
            double wx = lvg__lanczos3(u - (double)ix);
            double w = wx * wy;
            int cx;
            if (w == 0.0) continue;
            cx = lvg__clamp_int(ix, sx, sx + sw - 1);
            lvg__sample_accumulate(src, cx, cy, w,
                                   &sum_pr, &sum_pg, &sum_pb, &sum_pa, &sum_w);
        }
    }

    if (sum_w == 0.0) return 0;

    sum_pr /= sum_w;
    sum_pg /= sum_w;
    sum_pb /= sum_w;
    sum_pa /= sum_w;

    return lvg__pack_sample(sum_pr, sum_pg, sum_pb, sum_pa);
}

/* -------------------------------------------------------------------------
 * Context
 * ------------------------------------------------------------------------- */

void lvg_canvas_init(lvg_canvas_t *canvas, lvg_surface_t *surface)
{
    canvas->_surface = surface;
    canvas->_ops = NULL;           /* software fast path */
    canvas->_backend_state = NULL;
    canvas->_aa_mode = LVG_CANVAS_AA_NORMAL;
    lvg_canvas_reset_clip(canvas);
}

void lvg_canvas_set_aa_mode(lvg_canvas_t *canvas, lvg_canvas_aa_mode_t mode)
{
    if (!canvas) return;
    canvas->_aa_mode = (int)mode;
}

lvg_canvas_aa_mode_t lvg_canvas_get_aa_mode(const lvg_canvas_t *canvas)
{
    if (!canvas) return LVG_CANVAS_AA_NORMAL;
    return (lvg_canvas_aa_mode_t)canvas->_aa_mode;
}

bool lvg_canvas_backend_available(lvg_canvas_backend_t backend)
{
    switch ((int)backend) {
    case LVG_CANVAS_BACKEND_SOFTWARE: return true;
    case LVG_CANVAS_BACKEND_BLEND2D:
#ifdef LVG_HAVE_BLEND2D
        return true;
#else
        return false;
#endif
    case LVG_CANVAS_BACKEND_THORVG:
#ifdef LVG_HAVE_THORVG
        return true;
#else
        return false;
#endif
    case LVG_CANVAS_BACKEND_SKIA:
#ifdef LVG_HAVE_SKIA
        return true;
#else
        return false;
#endif
    }
    /* Custom slot? Available iff a descriptor is registered there. */
    return lvg_canvas_backend_describe((int)backend) != NULL;
}

const char *lvg_canvas_backend_name(lvg_canvas_backend_t backend)
{
    switch ((int)backend) {
    case LVG_CANVAS_BACKEND_SOFTWARE: return "software";
    case LVG_CANVAS_BACKEND_BLEND2D:  return "blend2d";
    case LVG_CANVAS_BACKEND_THORVG:   return "thorvg";
    case LVG_CANVAS_BACKEND_SKIA:     return "skia";
    }
    const lvg_canvas_backend_desc_t *desc = lvg_canvas_backend_describe((int)backend);
    return desc ? desc->name : "unknown";
}

bool lvg_canvas_init_backend(lvg_canvas_t *canvas, lvg_surface_t *surface,
                             lvg_canvas_backend_t backend)
{
    lvg_canvas_init(canvas, surface);
    switch ((int)backend) {
    case LVG_CANVAS_BACKEND_SOFTWARE:
        return true;
    case LVG_CANVAS_BACKEND_BLEND2D:
#ifdef LVG_HAVE_BLEND2D
        return lvg_canvas_blend2d_init(canvas);
#else
        return false;
#endif
    case LVG_CANVAS_BACKEND_THORVG:
#ifdef LVG_HAVE_THORVG
        return lvg_canvas_thorvg_init(canvas);
#else
        return false;
#endif
    case LVG_CANVAS_BACKEND_SKIA:
#ifdef LVG_HAVE_SKIA
        return lvg_canvas_skia_init(canvas);
#else
        return false;
#endif
    }
    /* Custom slot — dispatch through the registry. */
    const lvg_canvas_backend_desc_t *desc = lvg_canvas_backend_describe((int)backend);
    return desc ? desc->init(canvas) : false;
}

void lvg_canvas_flush(lvg_canvas_t *canvas)
{
    if (canvas->_ops && canvas->_ops->flush)
        canvas->_ops->flush(canvas);
    else
        lvg__flush_polygon_batch(canvas);
}

void lvg_canvas_destroy(lvg_canvas_t *canvas)
{
    if (canvas->_ops && canvas->_ops->destroy)
        canvas->_ops->destroy(canvas);
    else
        lvg__flush_polygon_batch(canvas);
    canvas->_ops = NULL;
    canvas->_backend_state = NULL;
}

void lvg_canvas_set_clip(lvg_canvas_t *canvas, const lvg_rect_t *clip)
{
    /* Drain any pending fills under the previous clip first. */
    if (!canvas->_ops) lvg__flush_polygon_batch(canvas);
    lvg_rect_t surf = lvg_rect_make(0, 0,
                                    canvas->_surface->width,
                                    canvas->_surface->height);
    if (!clip) {
        canvas->_clip = surf;
    } else {
        canvas->_clip = lvg_rect_intersect(&surf, clip);
    }
    if (canvas->_ops && canvas->_ops->set_clip)
        canvas->_ops->set_clip(canvas, clip);
}

void lvg_canvas_reset_clip(lvg_canvas_t *canvas)
{
    if (!canvas->_ops) lvg__flush_polygon_batch(canvas);
    canvas->_clip = lvg_rect_make(0, 0,
                                  canvas->_surface->width,
                                  canvas->_surface->height);
    if (canvas->_ops && canvas->_ops->set_clip)
        canvas->_ops->set_clip(canvas, NULL);
}

/* -------------------------------------------------------------------------
 * Fill
 * ------------------------------------------------------------------------- */

void lvg_canvas_clear(lvg_canvas_t *canvas, lvg_color_t color)
{
    LVG_DISPATCH(clear, color);
    /* Pending batched polygons would be overwritten by clear anyway —
     * drop them rather than rasterize. */
    if (!canvas->_ops) lvg__discard_polygon_batch();
    lvg_canvas_fill_rect(canvas, 0, 0,
                          canvas->_surface->width,
                          canvas->_surface->height,
                          color);
}

void lvg_canvas_fill_rect(lvg_canvas_t *canvas,
                           int x, int y, int w, int h,
                           lvg_color_t color)
{
    LVG_DISPATCH(fill_rect, x, y, w, h, color);
    if (w <= 0 || h <= 0) return;

    /* Clip to canvas clip rect */
    const lvg_rect_t *clip = &canvas->_clip;
    int x0 = x < clip->x ? clip->x : x;
    int y0 = y < clip->y ? clip->y : y;
    int x1 = (x + w) > (clip->x + clip->width)  ? (clip->x + clip->width)  : (x + w);
    int y1 = (y + h) > (clip->y + clip->height) ? (clip->y + clip->height) : (y + h);
    if (x0 >= x1 || y0 >= y1) return;

    lvg_surface_t *s  = canvas->_surface;
    uint32_t       sa = (color >> 24) & 0xFF;

    if (sa == 255) {
        /* Fast path: opaque fill — no blending needed */
        const int span = x1 - x0;
        for (int row = y0; row < y1; row++) {
            uint32_t *p = &s->pixels[row * s->stride + x0];
            for (int col = 0; col < span; col++)
                p[col] = color;
        }
    } else if (sa > 0) {
        /* Translucent fill delegates to the shared row blender. */
        const int span = x1 - x0;
        for (int row = y0; row < y1; row++) {
            lvg_px_blend_over_constant_row(&s->pixels[row * s->stride + x0],
                                            color, span);
        }
    }
    /* sa == 0: fully transparent — no-op */
}

void lvg_canvas_stroke_rect(lvg_canvas_t *canvas,
                              int x, int y, int w, int h,
                              lvg_color_t color, int stroke_width)
{
    LVG_DISPATCH(stroke_rect, x, y, w, h, color, stroke_width);
    if (w <= 0 || h <= 0 || stroke_width <= 0) return;
    int sw = stroke_width;

    /* Top edge */
    lvg_canvas_fill_rect(canvas, x, y, w, sw, color);
    /* Bottom edge */
    lvg_canvas_fill_rect(canvas, x, y + h - sw, w, sw, color);
    /* Left edge (avoiding overlap with top/bottom) */
    lvg_canvas_fill_rect(canvas, x, y + sw, sw, h - 2 * sw, color);
    /* Right edge */
    lvg_canvas_fill_rect(canvas, x + w - sw, y + sw, sw, h - 2 * sw, color);
}

void lvg_canvas_fill_circle(lvg_canvas_t *canvas,
                              int cx, int cy, int r,
                              lvg_color_t color)
{
    LVG_DISPATCH(fill_circle, cx, cy, r, color);
    if (r <= 0) return;
    /* Scanline fill using integer square root */
    for (int dy = -r; dy <= r; dy++) {
        int dx = lvg__isqrt(r * r - dy * dy);
        lvg_canvas_fill_rect(canvas, cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

void lvg_canvas_draw_line(lvg_canvas_t *canvas,
                           int x0, int y0, int x1, int y1,
                           lvg_color_t color, int stroke_width)
{
    LVG_DISPATCH(draw_line, x0, y0, x1, y1, color, stroke_width);
    if (stroke_width <= 0) stroke_width = 1;

    if (stroke_width == 1) {
        /* Hairline: single-pixel Bresenham. */
        int dx  =  abs(x1 - x0);
        int dy  = -abs(y1 - y0);
        int sx  = x0 < x1 ? 1 : -1;
        int sy  = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            lvg__set_pixel(canvas, x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
        return;
    }

    /* Axis-aligned fast paths keep integer pixel counts exact. */
    int half = stroke_width / 2;
    if (y0 == y1) {
        int x_start = x0 < x1 ? x0 : x1;
        int len_x = abs(x1 - x0);
        if (len_x == 0) len_x = stroke_width;
        lvg_canvas_fill_rect(canvas, x_start, y0 - half,
                              len_x, stroke_width, color);
        return;
    }
    if (x0 == x1) {
        int y_start = y0 < y1 ? y0 : y1;
        int len_y = abs(y1 - y0);
        if (len_y == 0) len_y = stroke_width;
        lvg_canvas_fill_rect(canvas, x0 - half, y_start,
                              stroke_width, len_y, color);
        return;
    }

    /* Diagonal thick line: expand perpendicular to the line direction into
     * a quad, then fill with two triangles. The previous implementation
     * stamped an axis-aligned square at each Bresenham step, which
     * produced ~W·cos θ perpendicular thickness for diagonal lines —
     * visibly thinner than horizontals of the same stroke width, and
     * visible on every line-based primitive that composes short segments
     * (beziers, arcs, diagonal hatches). */
    float fx0 = (float)x0, fy0 = (float)y0;
    float fx1 = (float)x1, fy1 = (float)y1;
    float lx = fx1 - fx0, ly = fy1 - fy0;
    float len = sqrtf(lx * lx + ly * ly);
    float half_w = (float)stroke_width * 0.5f;
    float nx = -ly / len, ny = lx / len;
    float ox = nx * half_w, oy = ny * half_w;

    int ax = (int)(fx0 + ox + 0.5f), ay = (int)(fy0 + oy + 0.5f);
    int bx = (int)(fx1 + ox + 0.5f), by = (int)(fy1 + oy + 0.5f);
    int cx = (int)(fx1 - ox + 0.5f), cy = (int)(fy1 - oy + 0.5f);
    int dx = (int)(fx0 - ox + 0.5f), dy = (int)(fy0 - oy + 0.5f);

    lvg_canvas_fill_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    lvg_canvas_fill_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

void lvg_canvas_draw_polyline(lvg_canvas_t *canvas,
                               const lvg_point_t *points, int count,
                               lvg_color_t color, int stroke_width)
{
    LVG_DISPATCH(draw_polyline, points, count, color, stroke_width);
    if (!points || count < 2) return;
    for (int i = 0; i < count - 1; i++) {
        lvg_canvas_draw_line(canvas,
                              points[i].x, points[i].y,
                              points[i + 1].x, points[i + 1].y,
                              color, stroke_width);
    }
}

/* -------------------------------------------------------------------------
 * Circles & Ellipses
 * ------------------------------------------------------------------------- */

void lvg_canvas_stroke_circle(lvg_canvas_t *canvas,
                                int cx, int cy, int r,
                                lvg_color_t color, int stroke_width)
{
    LVG_DISPATCH(stroke_circle, cx, cy, r, color, stroke_width);
    if (r <= 0 || stroke_width <= 0) return;

    int outer = r;
    int inner = r - stroke_width;
    if (inner < 0) inner = 0;

    int outer2 = outer * outer;
    int inner2 = inner * inner;

    for (int dy = -outer; dy <= outer; dy++) {
        int dy2 = dy * dy;
        int dx_outer = lvg__isqrt(outer2 - dy2);
        if (dy2 <= inner2) {
            int dx_inner = lvg__isqrt(inner2 - dy2);
            /* Left arc */
            lvg_canvas_fill_rect(canvas, cx - dx_outer, cy + dy,
                                  dx_outer - dx_inner, 1, color);
            /* Right arc */
            lvg_canvas_fill_rect(canvas, cx + dx_inner + 1, cy + dy,
                                  dx_outer - dx_inner, 1, color);
        } else {
            /* Full span (near top/bottom of circle) */
            lvg_canvas_fill_rect(canvas, cx - dx_outer, cy + dy,
                                  2 * dx_outer + 1, 1, color);
        }
    }
}

void lvg_canvas_fill_ellipse(lvg_canvas_t *canvas,
                               int cx, int cy, int rx, int ry,
                               lvg_color_t color)
{
    LVG_DISPATCH(fill_ellipse, cx, cy, rx, ry, color);
    if (rx <= 0 || ry <= 0) return;

    for (int dy = -ry; dy <= ry; dy++) {
        /* x^2/rx^2 + y^2/ry^2 <= 1  =>  x <= rx * sqrt(1 - y^2/ry^2) */
        int dx = (int)(rx * sqrt(1.0 - (double)(dy * dy) / ((double)ry * ry)));
        lvg_canvas_fill_rect(canvas, cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

void lvg_canvas_stroke_ellipse(lvg_canvas_t *canvas,
                                 int cx, int cy, int rx, int ry,
                                 lvg_color_t color, int stroke_width)
{
    LVG_DISPATCH(stroke_ellipse, cx, cy, rx, ry, color, stroke_width);
    if (rx <= 0 || ry <= 0 || stroke_width <= 0) return;

    int orx = rx, ory = ry;
    int irx = rx - stroke_width;
    int iry = ry - stroke_width;
    if (irx < 0) irx = 0;
    if (iry < 0) iry = 0;

    int max_y = ory;
    for (int dy = -max_y; dy <= max_y; dy++) {
        double fy = (double)dy;
        int dx_outer = (int)(orx * sqrt(1.0 - (fy * fy) / ((double)ory * ory)));

        if (irx > 0 && iry > 0 && abs(dy) <= iry) {
            int dx_inner = (int)(irx * sqrt(1.0 - (fy * fy) / ((double)iry * iry)));
            lvg_canvas_fill_rect(canvas, cx - dx_outer, cy + dy,
                                  dx_outer - dx_inner, 1, color);
            lvg_canvas_fill_rect(canvas, cx + dx_inner + 1, cy + dy,
                                  dx_outer - dx_inner, 1, color);
        } else {
            lvg_canvas_fill_rect(canvas, cx - dx_outer, cy + dy,
                                  2 * dx_outer + 1, 1, color);
        }
    }
}

/* -------------------------------------------------------------------------
 * Rounded rectangles
 * ------------------------------------------------------------------------- */

void lvg_canvas_fill_rounded_rect(lvg_canvas_t *canvas,
                                    int x, int y, int w, int h,
                                    int radius, lvg_color_t color)
{
    LVG_DISPATCH(fill_rounded_rect, x, y, w, h, radius, color);
    if (w <= 0 || h <= 0) return;
    if (radius <= 0) {
        lvg_canvas_fill_rect(canvas, x, y, w, h, color);
        return;
    }

    /* Clamp radius to half the smallest dimension */
    int max_r = (w < h ? w : h) / 2;
    if (radius > max_r) radius = max_r;

    int r2 = radius * radius;

    /* Top rounded region */
    for (int dy = 0; dy < radius; dy++) {
        int dx = lvg__isqrt(r2 - (radius - dy) * (radius - dy));
        int left  = x + radius - dx;
        int right = x + w - radius + dx;
        lvg_canvas_fill_rect(canvas, left, y + dy, right - left, 1, color);
    }

    /* Middle rectangular region */
    if (h - 2 * radius > 0)
        lvg_canvas_fill_rect(canvas, x, y + radius, w, h - 2 * radius, color);

    /* Bottom rounded region.
     * Bottom-left corner center is at (x+r, y+h-r-1); each loop row
     * (y+h-r+dy) is (dy+1) pixels below the center, so the horizontal
     * extent is sqrt(r^2 - (dy+1)^2). The row closest to the middle
     * (dy=0) is WIDEST, the bottommost row (dy=r-1) is NARROWEST.
     * The previous formula reused (radius-dy)^2 from the top-arc loop,
     * which inverted the arc and produced a visible "crack" mid-rect. */
    for (int dy = 0; dy < radius; dy++) {
        int d = dy + 1;
        int dx = lvg__isqrt(r2 - d * d);
        int left  = x + radius - dx;
        int right = x + w - radius + dx;
        lvg_canvas_fill_rect(canvas, left, y + h - radius + dy, right - left, 1, color);
    }
}

static bool lvg__rounded_rect_span(float x, float y, float w, float h,
                                   float radius, int py, int *x0, int *x1)
{
    if (w <= 0.0f || h <= 0.0f) return false;

    float cy = (float)py + 0.5f;
    if (cy < y || cy >= y + h) return false;

    float max_r = (w < h ? w : h) * 0.5f;
    if (radius < 0.0f) radius = 0.0f;
    if (radius > max_r) radius = max_r;

    float inset = 0.0f;
    if (radius > 0.0f && cy < y + radius) {
        float dy = (y + radius) - cy;
        inset = radius - sqrtf(radius * radius - dy * dy);
    } else if (radius > 0.0f && cy >= y + h - radius) {
        float dy = cy - (y + h - radius);
        inset = radius - sqrtf(radius * radius - dy * dy);
    }

    int lo = (int)ceilf(x + inset);
    int hi = (int)floorf(x + w - inset);
    if (hi <= lo) return false;

    *x0 = lo;
    *x1 = hi;
    return true;
}

void lvg_canvas_stroke_rounded_rect(lvg_canvas_t *canvas,
                                      int x, int y, int w, int h,
                                      int radius, lvg_color_t color,
                                      int stroke_width)
{
    LVG_DISPATCH(stroke_rounded_rect, x, y, w, h, radius, color, stroke_width);
    if (w <= 0 || h <= 0 || stroke_width <= 0) return;
    if (radius <= 0) {
        lvg_canvas_stroke_rect(canvas, x, y, w, h, color, stroke_width);
        return;
    }

    int max_r = (w < h ? w : h) / 2;
    if (radius > max_r) radius = max_r;

    int sw = stroke_width;
    float half = (float)sw * 0.5f;
    float outer_r = (float)radius + half;
    float inner_r = (float)radius - half;
    if (inner_r < 0.0f) inner_r = 0.0f;

    float ix = (float)x + (float)sw;
    float iy = (float)y + (float)sw;
    float iw = (float)w - 2.0f * (float)sw;
    float ih = (float)h - 2.0f * (float)sw;
    bool has_inner = iw > 0.0f && ih > 0.0f;

    if (!has_inner) {
        lvg_canvas_fill_rect(canvas, x, y, w, h, color);
        return;
    }

    for (int py = y; py < y + h; py++) {
        int ox0, ox1;
        if (!lvg__rounded_rect_span((float)x, (float)y, (float)w, (float)h,
                                    outer_r, py, &ox0, &ox1))
            continue;

        int ix0, ix1;
        if (!lvg__rounded_rect_span(ix, iy, iw, ih, inner_r, py, &ix0, &ix1)) {
            lvg_canvas_fill_rect(canvas, ox0, py, ox1 - ox0, 1, color);
            continue;
        }

        if (ix0 > ox0)
            lvg_canvas_fill_rect(canvas, ox0, py, ix0 - ox0, 1, color);
        if (ox1 > ix1)
            lvg_canvas_fill_rect(canvas, ix1, py, ox1 - ix1, 1, color);
    }
}

/* -------------------------------------------------------------------------
 * Triangles & Polygons
 * ------------------------------------------------------------------------- */

void lvg_canvas_fill_triangle(lvg_canvas_t *canvas,
                                int x0, int y0,
                                int x1, int y1,
                                int x2, int y2,
                                lvg_color_t color)
{
    LVG_DISPATCH(fill_triangle, x0, y0, x1, y1, x2, y2, color);
    /* Sort vertices by y-coordinate: y0 <= y1 <= y2 */
    if (y0 > y1) { int t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y0 > y2) { int t; t = x0; x0 = x2; x2 = t; t = y0; y0 = y2; y2 = t; }
    if (y1 > y2) { int t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }

    if (y0 == y2) return; /* degenerate */

    for (int y = y0; y <= y2; y++) {
        /* Edge 0→2 is always active; second edge switches at y1 */
        int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb;
        if (y < y1) {
            if (y1 == y0)
                xb = x0;
            else
                xb = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        } else {
            if (y2 == y1)
                xb = x1;
            else
                xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        }

        if (xa > xb) { int t = xa; xa = xb; xb = t; }
        lvg_canvas_fill_rect(canvas, xa, y, xb - xa + 1, 1, color);
    }
}

void lvg_canvas_fill_polygon(lvg_canvas_t *canvas,
                               const lvg_point_t *points, int count,
                               lvg_color_t color)
{
    LVG_DISPATCH(fill_polygon, points, count, color);
    if (!points || count < 3) return;

    /* Find y range */
    int y_min = points[0].y, y_max = points[0].y;
    for (int i = 1; i < count; i++) {
        if (points[i].y < y_min) y_min = points[i].y;
        if (points[i].y > y_max) y_max = points[i].y;
    }

    /* Scanline fill using edge intersection */
    for (int y = y_min; y <= y_max; y++) {
        /* Collect x-intersections with all edges */
        int xs[64]; /* enough for reasonable polygons */
        int nx = 0;

        for (int i = 0; i < count && nx < 64; i++) {
            int j = (i + 1) % count;
            int yi = points[i].y, yj = points[j].y;
            if (yi == yj) continue; /* horizontal edge */

            int y_lo = yi < yj ? yi : yj;
            int y_hi = yi > yj ? yi : yj;
            if (y < y_lo || y >= y_hi) continue;

            int xi = points[i].x, xj = points[j].x;
            xs[nx++] = xi + (xj - xi) * (y - yi) / (yj - yi);
        }

        /* Sort intersections */
        for (int i = 0; i < nx - 1; i++)
            for (int j = i + 1; j < nx; j++)
                if (xs[i] > xs[j]) { int t = xs[i]; xs[i] = xs[j]; xs[j] = t; }

        /* Fill between pairs */
        for (int i = 0; i + 1 < nx; i += 2)
            lvg_canvas_fill_rect(canvas, xs[i], y, xs[i + 1] - xs[i] + 1, 1, color);
    }
}

/* -------------------------------------------------------------------------
 * Stroke polygon
 * ------------------------------------------------------------------------- */

void lvg_canvas_stroke_polygon(lvg_canvas_t *canvas,
                                 const lvg_point_t *points, int count,
                                 lvg_color_t color, int stroke_width)
{
    LVG_DISPATCH(stroke_polygon, points, count, color, stroke_width);
    if (!points || count < 2) return;
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        lvg_canvas_draw_line(canvas,
                              points[i].x, points[i].y,
                              points[j].x, points[j].y,
                              color, stroke_width);
    }
}

/* -------------------------------------------------------------------------
 * Dashed / dotted lines
 * ------------------------------------------------------------------------- */

const int lvg_dash_solid[]   = {0};
const int lvg_dash_dashed[]  = {6, 4};
const int lvg_dash_dotted[]  = {2, 3};
const int lvg_dash_dashdot[] = {6, 3, 2, 3};

void lvg_canvas_draw_line_dashed(lvg_canvas_t *canvas,
                                   int x0, int y0, int x1, int y1,
                                   lvg_color_t color, int stroke_width,
                                   const lvg_dash_t *dash)
{
    LVG_DISPATCH(draw_line_dashed, x0, y0, x1, y1, color, stroke_width, dash);
    /* Fallback: solid line */
    if (!dash || dash->count < 2) {
        lvg_canvas_draw_line(canvas, x0, y0, x1, y1, color, stroke_width);
        return;
    }

    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;

    float ux = dx / len;
    float uy = dy / len;

    float pos = 0.0f;
    int pat_idx = 0;
    float pat_remaining = (float)dash->pattern[0];

    /* Apply offset */
    float off = (float)dash->offset;
    while (off > 0.0f) {
        if (off >= pat_remaining) {
            off -= pat_remaining;
            pat_idx = (pat_idx + 1) % dash->count;
            pat_remaining = (float)dash->pattern[pat_idx];
        } else {
            pat_remaining -= off;
            off = 0.0f;
        }
    }

    while (pos < len) {
        float seg = pat_remaining;
        if (pos + seg > len) seg = len - pos;

        /* Even indices = draw, odd = skip */
        if ((pat_idx & 1) == 0) {
            int sx = x0 + (int)(pos * ux + 0.5f);
            int sy = y0 + (int)(pos * uy + 0.5f);
            int ex = x0 + (int)((pos + seg) * ux + 0.5f);
            int ey = y0 + (int)((pos + seg) * uy + 0.5f);
            lvg_canvas_draw_line(canvas, sx, sy, ex, ey, color, stroke_width);
        }

        pos += seg;
        pat_remaining -= seg;
        if (pat_remaining <= 0.0f) {
            pat_idx = (pat_idx + 1) % dash->count;
            pat_remaining = (float)dash->pattern[pat_idx];
        }
    }
}

void lvg_canvas_draw_polyline_dashed(lvg_canvas_t *canvas,
                                       const lvg_point_t *points, int count,
                                       lvg_color_t color, int stroke_width,
                                       const lvg_dash_t *dash)
{
    LVG_DISPATCH(draw_polyline_dashed, points, count, color, stroke_width, dash);
    if (!points || count < 2) return;

    /* For polyline dashing, we need continuous dash state across segments */
    if (!dash || dash->count < 2) {
        lvg_canvas_draw_polyline(canvas, points, count, color, stroke_width);
        return;
    }

    /* Compute total length for continuous dashing */
    int pat_idx = 0;
    float pat_remaining = (float)dash->pattern[0];

    /* Apply offset */
    float off = (float)dash->offset;
    while (off > 0.0f) {
        if (off >= pat_remaining) {
            off -= pat_remaining;
            pat_idx = (pat_idx + 1) % dash->count;
            pat_remaining = (float)dash->pattern[pat_idx];
        } else {
            pat_remaining -= off;
            off = 0.0f;
        }
    }

    for (int i = 0; i < count - 1; i++) {
        float sx = (float)points[i].x;
        float sy = (float)points[i].y;
        float ex = (float)points[i + 1].x;
        float ey = (float)points[i + 1].y;
        float dx = ex - sx;
        float dy = ey - sy;
        float seg_len = sqrtf(dx * dx + dy * dy);
        if (seg_len < 0.5f) continue;

        float ux = dx / seg_len;
        float uy = dy / seg_len;
        float pos = 0.0f;

        while (pos < seg_len) {
            float seg = pat_remaining;
            if (pos + seg > seg_len) seg = seg_len - pos;

            if ((pat_idx & 1) == 0) {
                int lx0 = (int)(sx + pos * ux + 0.5f);
                int ly0 = (int)(sy + pos * uy + 0.5f);
                int lx1 = (int)(sx + (pos + seg) * ux + 0.5f);
                int ly1 = (int)(sy + (pos + seg) * uy + 0.5f);
                lvg_canvas_draw_line(canvas, lx0, ly0, lx1, ly1,
                                      color, stroke_width);
            }

            pos += seg;
            pat_remaining -= seg;
            if (pat_remaining <= 0.0f) {
                pat_idx = (pat_idx + 1) % dash->count;
                pat_remaining = (float)dash->pattern[pat_idx];
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Anti-aliased lines (Xiaolin Wu)
 * ------------------------------------------------------------------------- */

static inline float lvg__fpart(float x) { return x - floorf(x); }
static inline float lvg__rfpart(float x) { return 1.0f - lvg__fpart(x); }

static inline void lvg__aa_pixel(lvg_canvas_t *c, int x, int y,
                                  lvg_color_t color, float brightness)
{
    if (brightness <= 0.0f) return;
    if (brightness > 1.0f) brightness = 1.0f;

    uint32_t a = LVG_COLOR_A(color);
    a = (uint32_t)(a * brightness + 0.5f);
    uint32_t blended = LVG_COLOR_ARGB(a, LVG_COLOR_R(color),
                                       LVG_COLOR_G(color),
                                       LVG_COLOR_B(color));
    lvg__set_pixel(c, x, y, blended);
}

/*
 * Analytic anti-aliased polygon fill (sub-scanline area coverage).
 *
 * Generalised to N independent quad contours: @quads holds 4*nquad
 * points, with quads[4i .. 4i+3] forming each individual quad. We
 * accumulate ALL contours into a single coverage buffer and composite
 * once per pixel — so where adjacent stroke quads overlap at a vertex,
 * the union is filled exactly via non-zero winding, with no
 * double-blending and no offset-polygon spikes from miter clamping.
 *
 * For each output pixel row [y, y+1), we step LVG__AA_SUB sub-scanlines
 * uniformly across the row. At each sub-scanline we compute exact
 * fractional x-positions where every edge crosses, sort by x, walk with
 * a running winding counter (non-zero rule), and accumulate each
 * non-zero span's intersection with each pixel column. The final
 * coverage is `accumulated / LVG__AA_SUB` ∈ [0, 1] — the same approach
 * blend2d/thorvg use for stroke fills.
 */
/* 8 sub-scanlines per row: ~1/8-pixel effective vertical AA resolution.
 * Closer match to blend2d/AGG output at high zoom (4x). The per-row
 * crossings recompute is the main cost — each sub-row only does a single
 * multiply-add per active edge plus an insertion sort, both small. */
#define LVG__AA_SUB 8

/* Pre-computed per-edge metadata. Built once per polygon, then walked
 * many times across rows / sub-scanlines. Storing slope (dx_per_dy)
 * lets us compute crossings as a single multiply-add instead of the
 * full edge interpolation per sub-scanline. xmin_e/xmax_e let us cull
 * edges whose entire x-range falls outside the canvas clip region —
 * very common when an SVG path is zoomed past the viewport. */
typedef struct {
    float ymin, ymax;     /* edge y span (ymin < ymax)                  */
    float x_at_ymin;      /* x at the lower-y endpoint                  */
    float dx_per_dy;      /* x slope                                    */
    float xmin_e, xmax_e; /* edge's x bbox over its y span              */
    int8_t w;             /* +1 if drawn down (y increases), -1 else    */
} lvg__edge_t;

typedef struct { float x; int8_t w; } lvg__cross_t;

static void lvg__fill_polygon_aa(lvg_canvas_t *cv,
                                  const lvg_pointf_t *pts, int count,
                                  lvg_color_t color,
                                  lvg_fill_rule_t rule,
                                  const int *clens, int nclen)
{
    if (!cv || !pts || count < 3) return;

    /* Bounding box (float). */
    float fxmin = pts[0].x, fxmax = pts[0].x;
    float fymin = pts[0].y, fymax = pts[0].y;
    for (int i = 1; i < count; i++) {
        if (pts[i].x < fxmin) fxmin = pts[i].x;
        if (pts[i].x > fxmax) fxmax = pts[i].x;
        if (pts[i].y < fymin) fymin = pts[i].y;
        if (pts[i].y > fymax) fymax = pts[i].y;
    }

    int xmin = (int)floorf(fxmin);
    int xmax = (int)ceilf (fxmax);
    int ymin = (int)floorf(fymin);
    int ymax = (int)ceilf (fymax);

    const lvg_rect_t *clip = &cv->_clip;
    int cx0 = clip->x, cy0 = clip->y;
    int cx1 = clip->x + clip->width;
    int cy1 = clip->y + clip->height;
    if (xmin < cx0) xmin = cx0;
    if (ymin < cy0) ymin = cy0;
    if (xmax > cx1) xmax = cx1;
    if (ymax > cy1) ymax = cy1;
    if (xmax <= xmin || ymax <= ymin) return;

    int row_w = xmax - xmin;
    float *cov = (float *)calloc((size_t)row_w, sizeof(float));
    if (!cov) return;

    /* ---- Build edge table once. Reject horizontal edges and edges
     *      fully outside the clipped y-range. ---------------------- */
    enum { LVG__AA_EDGE_STACK = 128 };
    lvg__edge_t edge_stack[LVG__AA_EDGE_STACK];
    lvg__edge_t *edges = edge_stack;
    lvg__edge_t *edges_heap = NULL;
    if (count > LVG__AA_EDGE_STACK) {
        edges_heap = (lvg__edge_t *)malloc((size_t)count * sizeof(*edges));
        if (edges_heap) edges = edges_heap;
        else { free(cov); return; }
    }

    /* Forward-declare heaps so the early-out 'goto done' can free them. */
    lvg__cross_t *cross_heap = NULL;
    int *active_heap = NULL;

    int n_edges = 0;
    float fy_clip_lo = (float)ymin;
    float fy_clip_hi = (float)ymax;
    /* Walk each closed contour independently so the wrap-around edge connects
     * a contour's last vertex to its OWN first vertex (not the next
     * contour's), keeping the winding of every contour self-balanced. */
    int n_contours = (clens && nclen > 0) ? nclen : 1;
    int cbase = 0;
    for (int ci = 0; ci < n_contours; ci++) {
        int clen = (clens && nclen > 0) ? clens[ci] : count;
        for (int k = 0; k < clen; k++) {
        int i = cbase + k;
        int j = cbase + ((k + 1) == clen ? 0 : k + 1);
        float y0 = pts[i].y, y1 = pts[j].y;
        if (y0 == y1) continue;                                /* horizontal */
        float x0 = pts[i].x, x1 = pts[j].x;
        lvg__edge_t *e = &edges[n_edges];
        if (y0 < y1) {
            e->ymin = y0; e->ymax = y1;
            e->x_at_ymin = x0;
            e->dx_per_dy = (x1 - x0) / (y1 - y0);
            e->w = +1;
        } else {
            e->ymin = y1; e->ymax = y0;
            e->x_at_ymin = x1;
            e->dx_per_dy = (x0 - x1) / (y0 - y1);
            e->w = -1;
        }
        if (e->ymax <= fy_clip_lo || e->ymin >= fy_clip_hi)
            continue;                                          /* off-canvas y */
        e->xmin_e = (x0 < x1) ? x0 : x1;
        e->xmax_e = (x0 < x1) ? x1 : x0;
        /* Don't drop edges based on x range. Right-of-clip edges still
         * contribute to the winding count for spans that start inside
         * the clip and "end" at the right edge — dropping them leaves
         * non-zero winding unbalanced, and the rasterizer never closes
         * the span, so polygons with their right side extending past
         * the canvas lose all their fill. Same reasoning we already
         * apply to left-of-clip edges. */
        n_edges++;
        }
        cbase += clen;
    }
    if (n_edges == 0) goto done;

    /* Sort edges by ymin (insertion sort — N is small after horizontal &
     * off-canvas culling). Lets us walk an Active Edge Table linearly:
     * advance an `add_cursor` to lazily add edges entering each row,
     * compact actives in place when their ymax falls behind. This turns
     * the per-row edge scan from O(n_edges) into amortised O(active). */
    for (int i = 1; i < n_edges; i++) {
        lvg__edge_t key = edges[i];
        int k = i - 1;
        while (k >= 0 && edges[k].ymin > key.ymin) {
            edges[k + 1] = edges[k];
            k--;
        }
        edges[k + 1] = key;
    }

    /* Crossings buffer: at most n_edges per sub-scanline. If n_edges
     * exceeds the stack capacity we MUST malloc — falling back to the
     * undersized stack would silently truncate the inner crossing loop,
     * which under non-zero winding leaves the running counter unbalanced
     * and produces dropped-row "white stripe" artefacts. Same for the
     * active-edge index buffer below. On OOM, abort the fill cleanly
     * rather than corrupt memory. */
    enum { LVG__AA_CROSS_STACK = 128 };
    lvg__cross_t cross_stack[LVG__AA_CROSS_STACK];
    lvg__cross_t *cross = cross_stack;
    int cross_cap = LVG__AA_CROSS_STACK;
    if (n_edges > LVG__AA_CROSS_STACK) {
        cross_heap = (lvg__cross_t *)malloc(
            (size_t)n_edges * sizeof(*cross_heap));
        if (!cross_heap) goto done;
        cross = cross_heap;
        cross_cap = n_edges;
    }

    /* Active-edge index buffer: indices into edges[] of edges whose y
     * range intersects the current row. Re-built per row from a single
     * O(n_edges) scan. */
    int active_stack[LVG__AA_EDGE_STACK];
    int *active = active_stack;
    if (n_edges > LVG__AA_EDGE_STACK) {
        active_heap = (int *)malloc((size_t)n_edges * sizeof(int));
        if (!active_heap) goto done;
        active = active_heap;
    }

    const float inv_sub = 1.0f / (float)LVG__AA_SUB;
    bool is_evenodd = (rule == LVG_FILL_RULE_EVENODD);
    int  add_cursor = 0;
    int  n_active = 0;

    for (int y = ymin; y < ymax; y++) {
        float yrow_lo = (float)y;
        float yrow_hi = (float)(y + 1);

        /* Drop edges whose ymax fell behind the current row. */
        int w = 0;
        for (int r = 0; r < n_active; r++) {
            if (edges[active[r]].ymax > yrow_lo) active[w++] = active[r];
        }
        n_active = w;

        /* Add edges entering at this row (sorted by ymin → linear walk). */
        while (add_cursor < n_edges
               && edges[add_cursor].ymin < yrow_hi) {
            if (edges[add_cursor].ymax > yrow_lo)
                active[n_active++] = add_cursor;
            add_cursor++;
        }
        if (n_active == 0) continue;

        /* cov[] is already zero coming into each row — we zero the
         * touched range during composite at the end, avoiding a full
         * row_w memset per row (huge bandwidth saving for tall thin
         * polygons clipped to the screen, where row_w is large but
         * actual coverage spans are small). */
        int row_lo = INT_MAX, row_hi = INT_MIN;  /* tight composite range */

        for (int sy = 0; sy < LVG__AA_SUB; sy++) {
            float yf = yrow_lo + ((float)sy + 0.5f) * inv_sub;

            int nc = 0;
            for (int ai = 0; ai < n_active && nc < cross_cap; ai++) {
                const lvg__edge_t *e = &edges[active[ai]];
                if (yf < e->ymin || yf >= e->ymax) continue;
                cross[nc].x = e->x_at_ymin + (yf - e->ymin) * e->dx_per_dy;
                cross[nc].w = e->w;
                nc++;
            }
            if (nc < 2) continue;

            /* Insertion sort by x — small N (≈ stroke outline crossings). */
            for (int i = 1; i < nc; i++) {
                lvg__cross_t key = cross[i];
                int k = i - 1;
                while (k >= 0 && cross[k].x > key.x) {
                    cross[k + 1] = cross[k];
                    k--;
                }
                cross[k + 1] = key;
            }

            /* Hoist the rule branch: two specialised inner loops.
             * Inside each, split each span into partial-pixel ends + a
             * tight `cov[x] += inv_sub` middle loop — for the typical
             * "many full-coverage pixels in the middle" case this avoids
             * 5 floats of work per pixel and lets the compiler vectorise
             * the inner add. */
            float xmin_f = (float)xmin;
            float xmax_f = (float)xmax;
#define LVG__AA_EMIT_SPAN(XA, XB)                                           \
    do {                                                                    \
        float _xa = (XA), _xb = (XB);                                       \
        if (_xb > _xa) {                                                    \
            if (_xa < xmin_f) _xa = xmin_f;                                 \
            if (_xb > xmax_f) _xb = xmax_f;                                 \
            int _ix0 = (int)floorf(_xa);                                    \
            int _ix1 = (int)floorf(_xb);                                    \
            if (_ix1 >= _ix0 && _ix0 < xmax && _ix1 >= xmin) {              \
                if (_ix0 < xmin) _ix0 = xmin;                               \
                if (_ix1 > xmax - 1) _ix1 = xmax - 1;                       \
                if (_ix0 < row_lo) row_lo = _ix0;                           \
                if (_ix1 > row_hi) row_hi = _ix1;                           \
                if (_ix0 == _ix1) {                                         \
                    cov[_ix0 - xmin] += (_xb - _xa) * inv_sub;              \
                } else {                                                    \
                    cov[_ix0 - xmin] += ((float)(_ix0 + 1) - _xa) * inv_sub;\
                    for (int _x = _ix0 + 1; _x < _ix1; _x++)                \
                        cov[_x - xmin] += inv_sub;                          \
                    cov[_ix1 - xmin] += (_xb - (float)_ix1) * inv_sub;      \
                }                                                           \
            }                                                               \
        }                                                                   \
    } while (0)

            if (is_evenodd) {
                for (int i = 0; i + 1 < nc; i += 2) {
                    LVG__AA_EMIT_SPAN(cross[i].x, cross[i + 1].x);
                }
            } else {
                int winding = 0;
                float span_x = 0.0f;
                for (int i = 0; i < nc; i++) {
                    int prev_w = winding;
                    winding += cross[i].w;
                    if (prev_w == 0 && winding != 0) {
                        span_x = cross[i].x;
                    } else if (prev_w != 0 && winding == 0) {
                        LVG__AA_EMIT_SPAN(span_x, cross[i].x);
                    }
                }
            }
#undef LVG__AA_EMIT_SPAN
        }

        if (row_hi >= row_lo) {
            /* Run-length composite. Polygon interiors typically have many
             * consecutive full-coverage (c >= 1) pixels and partial pixels
             * only at edges; batching full-coverage runs into a single
             * lvg_px_blend_over_constant_row call lets the SSE2 fast path
             * blend 4 pixels per iteration instead of one. Partial-
             * coverage pixels still go through the scalar blender. */
            int xa = row_lo - xmin;
            int xb = row_hi - xmin + 1;
            uint32_t *dst_row = &cv->_surface->pixels[y * cv->_surface->stride];
            uint32_t a_full = LVG_COLOR_A(color);
            uint32_t r_col  = LVG_COLOR_R(color);
            uint32_t g_col  = LVG_COLOR_G(color);
            uint32_t b_col  = LVG_COLOR_B(color);
            int x = xa;
            while (x < xb) {
                float c = cov[x];
                cov[x] = 0.0f;
                if (c <= 0.0f) { x++; continue; }
                if (c >= 1.0f) {
                    /* Find run of full-coverage pixels. */
                    int j = x + 1;
                    while (j < xb && cov[j] >= 1.0f) {
                        cov[j] = 0.0f;
                        j++;
                    }
                    uint32_t src = LVG_COLOR_ARGB(a_full, r_col, g_col, b_col);
                    lvg_px_blend_over_constant_row(&dst_row[xmin + x],
                                                    src, j - x);
                    x = j;
                } else {
                    uint32_t a = (uint32_t)(a_full * c + 0.5f);
                    if (a) {
                        uint32_t src = LVG_COLOR_ARGB(a, r_col, g_col, b_col);
                        dst_row[xmin + x] = lvg_px_blend_over(
                            dst_row[xmin + x], src);
                    }
                    x++;
                }
            }
        }
    }

done:
    free(active_heap);
    free(cross_heap);
    free(edges_heap);
    free(cov);
}


/* AGG-style alpha calculation. `area_value` is (cover<<9) - cell_area or
 * (cover<<9) for a uniform gap span. Returns 0..255. */
static int lvg__agg_alpha(int area_value, lvg_fill_rule_t rule)
{
    int v = area_value >> 9;
    if (v < 0) v = -v;
    if (rule == LVG_FILL_RULE_EVENODD) {
        v &= 511;
        if (v > 256) v = 512 - v;
        if (v > 255) v = 255;
    } else {
        if (v > 255) v = 255;
    }
    return v;
}

/* =========================================================================
 * Dense per-row rasterizer (replacement for the cell-based AGG path).
 *
 * The cell-based path emits one cell per (row, pixel-column) the edge
 * crosses, then sorts cells per row before sweeping. At zoom (long
 * edges → many cells per row) the per-cell flush, allocation, and
 * insertion sort all cost real time even after the per-row bucket
 * restructuring.
 *
 * This rasterizer drops the cell intermediate. For each output pixel
 * row we walk the active edge table once, accumulating each edge's
 * (cover, area) contribution directly into per-pixel arrays indexed
 * by x. Sweep walks those arrays left-to-right, computes alpha from
 * the running winding cover, and composites — uniform spans go to
 * the SSE2 constant-row blender; partial-coverage cell pixels go
 * through the scalar SRC_OVER blender.
 *
 * Key simplifications vs cells:
 *   - No cell allocation, no per-cell flush, no per-row sort.
 *   - Multiple edges hitting the same (row, x) just += into the same
 *     array slots — no merge step, no duplicate cells.
 *   - cover_arr and area_arr are reused across rows; only the
 *     [dirty_lo, dirty_hi] range needs zeroing between rows.
 * ========================================================================= */

typedef struct {
    int    ymin_sub;    /* subpixel y of upper endpoint                  */
    int    ymax_sub;    /* subpixel y of lower endpoint                  */
    int    x_at_ymin;   /* subpixel x at ymin_sub                        */
    int    dx;          /* signed subpixel dx (x2 - x1 with y1 < y2)      */
    int    dy;          /* positive subpixel dy = ymax_sub - ymin_sub     */
    double dx_per_dy;   /* (double)dx / dy — pre-computed for fast x(y)  */
    int8_t winding;     /* +1 if drawn down, -1 if up                     */
} lvg__dense_edge_t;

static int lvg__dense_cmp_edge_ymin(const void *a, const void *b)
{
    const lvg__dense_edge_t *ea = (const lvg__dense_edge_t *)a;
    const lvg__dense_edge_t *eb = (const lvg__dense_edge_t *)b;
    if (ea->ymin_sub != eb->ymin_sub) return ea->ymin_sub - eb->ymin_sub;
    return 0;
}

/* =========================================================================
 * Cross-polygon batched / strip-based rasterizer (AGG mode only).
 *
 * fill_polygon_dense calls in AGG mode enqueue an edge list + colour into
 * a thread-local batch. canvas_flush / canvas_clear / canvas_set_clip /
 * canvas_set_aa_mode / canvas_destroy drain the batch by walking the
 * canvas clip in 64-row horizontal strips: for each strip, every cmd
 * whose y-range overlaps the strip is rasterized in submission order,
 * sharing a single cover/area buffer sized to the clip width. The
 * shared buffer + surface strip stay hot in L1/L2 across all cmds in
 * the strip — this is the win over rasterizing each polygon
 * end-to-end.
 *
 * Caveat: in AGG mode, mixing fill_polygon_ex with other software
 * primitives (text, lines, blits, …) without a manual flush in between
 * can change z-order. The bench/SVG path issues only fills then
 * flushes, which is the supported pattern.
 * ========================================================================= */
typedef struct {
    int             edge_offset;  /* into lvg__batch_edges */
    int             edge_count;
    lvg_color_t     color;
    lvg_fill_rule_t rule;
    lvg_rect_t      clip;
    int             row_min;      /* y range, surface coords, clipped */
    int             row_max;
    int             xbb_min;      /* x range, surface coords, clipped */
    int             xbb_max;
} lvg__poly_cmd_t;

#if defined(_MSC_VER)
#define LVG__TLS __declspec(thread)
#else
#define LVG__TLS __thread
#endif
static LVG__TLS lvg__poly_cmd_t   *lvg__batch_cmds      = NULL;
static LVG__TLS int                lvg__batch_cmd_n     = 0;
static LVG__TLS int                lvg__batch_cmd_cap   = 0;
static LVG__TLS lvg__dense_edge_t *lvg__batch_edges     = NULL;
static LVG__TLS int                lvg__batch_edge_n    = 0;
static LVG__TLS int                lvg__batch_edge_cap  = 0;
static LVG__TLS int               *lvg__strip_cover     = NULL;
static LVG__TLS int               *lvg__strip_area      = NULL;
static LVG__TLS int                lvg__strip_xy_cap    = 0;
static LVG__TLS int               *lvg__strip_active    = NULL;
static LVG__TLS int                lvg__strip_active_cap = 0;

static void lvg__discard_polygon_batch(void)
{
    lvg__batch_cmd_n  = 0;
    lvg__batch_edge_n = 0;
}

/* Accumulate one edge's contribution within the pixel row [row_y_lo,
 * row_y_lo+256] (subpixel) into cover_arr / area_arr. The pattern is the
 * same render_hline math as the cell-based path, but writes go directly
 * to arrays indexed by (pixel x - xbb_min). */
static inline void lvg__dense_emit_edge_row(
    const lvg__dense_edge_t *e, int row_y_lo,
    int *cover_arr, int *area_arr,
    int xbb_min, int bbox_w,
    int *dirty_lo, int *dirty_hi,
    int *row_pre_cover)
{
    int row_y_hi = row_y_lo + 256;
    int y_top = e->ymin_sub > row_y_lo ? e->ymin_sub : row_y_lo;
    int y_bot = e->ymax_sub < row_y_hi ? e->ymax_sub : row_y_hi;
    if (y_top >= y_bot) return;

    /* x at y_top and y_bot. The 64-bit integer divide previously used
     * here was a major hotspot at zoom; precompute slope as double and
     * use a single fmul + cvt per endpoint. Coordinates fit comfortably
     * in double's 53-bit mantissa. */
    int x_top, x_bot;
    if (y_top == e->ymin_sub) {
        x_top = e->x_at_ymin;
    } else {
        x_top = e->x_at_ymin
              + (int)((double)(y_top - e->ymin_sub) * e->dx_per_dy);
    }
    if (y_bot == e->ymax_sub) {
        x_bot = e->x_at_ymin + e->dx;
    } else {
        x_bot = e->x_at_ymin
              + (int)((double)(y_bot - e->ymin_sub) * e->dx_per_dy);
    }

    /* Local subpixel y in row [0..256]. */
    int y1 = y_top - row_y_lo;
    int y2 = y_bot - row_y_lo;
    int sign = e->winding;
    int sdy = sign * (y2 - y1);  /* signed dy contribution (cover unit) */

    int ex1 = x_top >> 8;
    int ex2 = x_bot >> 8;
    int fx1 = x_top & 255;
    int fx2 = x_bot & 255;

/* Cells with x left of the bbox accumulate to row_pre_cover (used
 * as the starting running-cover during sweep so polygons extending
 * past the clip still close their winding). Cells with x right of
 * bbox are dropped — they don't affect any in-clip pixel coverage. */
#define LVG__DENSE_EMIT(EX, COVER, AREA) do {                               \
    int _xx = (EX) - xbb_min;                                               \
    if (_xx < 0) {                                                          \
        *row_pre_cover += (COVER);                                          \
    } else if (_xx < bbox_w) {                                              \
        cover_arr[_xx] += (COVER);                                          \
        area_arr[_xx]  += (AREA);                                           \
        if (_xx < *dirty_lo) *dirty_lo = _xx;                               \
        if (_xx > *dirty_hi) *dirty_hi = _xx;                               \
    }                                                                       \
} while (0)

    if (y1 == y2) return;

    if (ex1 == ex2) {
        LVG__DENSE_EMIT(ex1, sdy, sign * (fx1 + fx2) * (y2 - y1));
        return;
    }

    /* Multi-cell walk — port of render_hline's fixed-point step but
     * writing directly to dense arrays with the edge's winding sign. */
    int p, first, dx, incr, lift, mod, rem, delta;

    p = (256 - fx1) * (y2 - y1);
    first = 256;
    incr = 1;
    dx = x_bot - x_top;
    if (dx < 0) {
        p = fx1 * (y2 - y1);
        first = 0;
        incr = -1;
        dx = -dx;
    }

    delta = p / dx;
    mod = p % dx;
    if (mod < 0) { delta--; mod += dx; }

    /* First cell. */
    LVG__DENSE_EMIT(ex1, sign * delta, sign * (fx1 + first) * delta);

    ex1 += incr;
    int y = y1 + delta;

    if (ex1 != ex2) {
        p = 256 * (y2 - y + delta);
        lift = p / dx;
        rem = p % dx;
        if (rem < 0) { lift--; rem += dx; }
        mod -= dx;

        while (ex1 != ex2) {
            delta = lift;
            mod += rem;
            if (mod >= 0) { mod -= dx; delta++; }
            if (delta) {
                LVG__DENSE_EMIT(ex1, sign * delta, sign * 256 * delta);
                y += delta;
            }
            ex1 += incr;
        }
    }

    /* Last cell. */
    delta = y2 - y;
    if (delta) {
        LVG__DENSE_EMIT(ex1, sign * delta, sign * (fx2 + 256 - first) * delta);
    }

#undef LVG__DENSE_EMIT
}

/* Per-strip rasterize: process rows [strip_y0, strip_y1) of one cmd,
 * writing into the shared cover/area buffer (sized to clip width but
 * indexed by abs_x - cmd->xbb_min, so different cmds in the same strip
 * use overlapping but per-cmd-bounded slots that are kept zeroed by
 * the dirty-range memset between rows / at cmd end).
 *
 * `edges_base` is passed explicitly (rather than read from
 * lvg__batch_edges TLS) so worker threads can rasterize using the
 * batch built on the main thread. */
static void lvg__rasterize_cmd_strip(lvg_canvas_t *cv,
                                      const lvg__poly_cmd_t *cmd,
                                      const lvg__dense_edge_t *edges_base,
                                      int strip_y0, int strip_y1,
                                      int *cover_arr, int *area_arr,
                                      int *active)
{
    int row_min = cmd->row_min;
    int row_max = cmd->row_max;
    if (strip_y0 > row_min) row_min = strip_y0;
    if (strip_y1 < row_max) row_max = strip_y1;
    if (row_max <= row_min) return;

    const lvg__dense_edge_t *edges = &edges_base[cmd->edge_offset];
    int n_edges = cmd->edge_count;
    int xbb_min = cmd->xbb_min;
    int xbb_max = cmd->xbb_max;
    int bbox_w  = xbb_max - xbb_min;
    int cx0 = cmd->clip.x;
    int cx1 = cmd->clip.x + cmd->clip.width;
    lvg_color_t      color = cmd->color;
    lvg_fill_rule_t  rule  = cmd->rule;

    uint32_t a_full = LVG_COLOR_A(color);
    uint32_t r_col  = LVG_COLOR_R(color);
    uint32_t g_col  = LVG_COLOR_G(color);
    uint32_t b_col  = LVG_COLOR_B(color);

    int row_start_sub = row_min << 8;

    /* Pre-populate AET with edges that straddle row_start (their ymin
     * is before this strip's first row). add_cursor advances past
     * those; subsequent additions happen inside the row loop. */
    int n_active = 0;
    int add_cursor = 0;
    /* Strict <: edges with ymin_sub == row_start_sub are added by the
     * per-row "add new" loop below (matching the < row_y_hi test).
     * Adding them here too would double-insert them into active. */
    while (add_cursor < n_edges
           && edges[add_cursor].ymin_sub < row_start_sub) {
        if (edges[add_cursor].ymax_sub > row_start_sub) {
            active[n_active++] = add_cursor;
        }
        add_cursor++;
    }

    int dirty_lo = INT_MAX, dirty_hi = INT_MIN;

    for (int row = row_min; row < row_max; row++) {
        int row_y_lo = row << 8;
        int row_y_hi = row_y_lo + 256;

        /* Drop expired edges. */
        int w = 0;
        for (int i = 0; i < n_active; i++) {
            if (edges[active[i]].ymax_sub > row_y_lo) active[w++] = active[i];
        }
        n_active = w;

        /* Add edges entering this row. */
        while (add_cursor < n_edges
               && edges[add_cursor].ymin_sub < row_y_hi) {
            if (edges[add_cursor].ymax_sub > row_y_lo)
                active[n_active++] = add_cursor;
            add_cursor++;
        }
        if (n_active == 0) continue;

        /* Reset previous row's dirty range — memset hits SIMD inside
         * libc, which is faster than a scalar loop the compiler may
         * not auto-vectorize. */
        if (dirty_hi >= dirty_lo) {
            size_t span = (size_t)(dirty_hi - dirty_lo + 1) * sizeof(int);
            memset(&cover_arr[dirty_lo], 0, span);
            memset(&area_arr[dirty_lo],  0, span);
            dirty_lo = INT_MAX; dirty_hi = INT_MIN;
        }

        /* Accumulate each active edge's contribution for this row.
         * Cover from cells left of xbb_min lands in row_pre_cover and
         * seeds the running cover at sweep start. */
        int row_pre_cover = 0;
        for (int ai = 0; ai < n_active; ai++) {
            const lvg__dense_edge_t *e = &edges[active[ai]];
            lvg__dense_emit_edge_row(e, row_y_lo,
                                      cover_arr, area_arr,
                                      xbb_min, bbox_w,
                                      &dirty_lo, &dirty_hi,
                                      &row_pre_cover);
        }

        uint32_t *dst_row = &cv->_surface->pixels[row * cv->_surface->stride];

        /* If row_pre_cover yields a non-empty alpha, the row gets a
         * left-side gap fill from cx0 to the first cell. */
        if (dirty_hi < dirty_lo) {
            /* No in-bbox cells. If pre-cover yields a non-zero alpha,
             * the entire row is filled. */
            int span_alpha = lvg__agg_alpha(row_pre_cover << 9, rule);
            if (span_alpha) {
                uint32_t a = (uint32_t)((a_full * (uint32_t)span_alpha
                                         + 127) / 255);
                if (a) {
                    uint32_t src = LVG_COLOR_ARGB(a, r_col, g_col, b_col);
                    lvg_px_blend_over_constant_row(&dst_row[cx0], src,
                                                    cx1 - cx0);
                }
            }
            continue;
        }

        /* Sweep dirty range, composite into dst[row]. */
        int cover = row_pre_cover;
        /* Left-side gap from cx0 to the first dirty cell, with the
         * running cover seeded by row_pre_cover. */
        if (dirty_lo > 0) {
            int span_alpha = lvg__agg_alpha(cover << 9, rule);
            if (span_alpha) {
                int xa = cx0;
                int xb = xbb_min + dirty_lo;
                if (xb > cx1) xb = cx1;
                if (xb > xa) {
                    uint32_t a = (uint32_t)((a_full * (uint32_t)span_alpha
                                             + 127) / 255);
                    if (a) {
                        uint32_t src = LVG_COLOR_ARGB(a, r_col, g_col, b_col);
                        lvg_px_blend_over_constant_row(&dst_row[xa],
                                                        src, xb - xa);
                    }
                }
            }
        }
        int x = dirty_lo;
        while (x <= dirty_hi) {
            int cell_cover = cover_arr[x];
            int cell_area  = area_arr[x];
            cover += cell_cover;

            int abs_x = xbb_min + x;
            if (cell_area || cell_cover) {
                /* Pixel cell with both cover and area: emit as a
                 * cell-style partial-coverage pixel. */
                int alpha = lvg__agg_alpha((cover << 9) - cell_area, rule);
                if (alpha && abs_x >= cx0 && abs_x < cx1) {
                    uint32_t a = (uint32_t)((a_full * (uint32_t)alpha + 127)
                                            / 255);
                    if (a) {
                        uint32_t src = LVG_COLOR_ARGB(a, r_col, g_col, b_col);
                        dst_row[abs_x] = lvg_px_blend_over(
                            dst_row[abs_x], src);
                    }
                }
                x++;
            } else {
                x++;
            }

            /* Find run of empty cells (gap span) until next non-empty.
             * Polygon interiors are dominated by uniform cover spans,
             * so the SIMD path that checks 8 lanes per iteration cuts
             * sweep time substantially at zoom. */
            int j = x;
#if LVG_USE_SSE2
            const __m128i zero128 = _mm_setzero_si128();
            while (j + 8 <= dirty_hi + 1) {
                __m128i c0 = _mm_loadu_si128((const __m128i *)&cover_arr[j]);
                __m128i c1 = _mm_loadu_si128((const __m128i *)&cover_arr[j + 4]);
                __m128i a0 = _mm_loadu_si128((const __m128i *)&area_arr[j]);
                __m128i a1 = _mm_loadu_si128((const __m128i *)&area_arr[j + 4]);
                __m128i any = _mm_or_si128(_mm_or_si128(c0, c1),
                                            _mm_or_si128(a0, a1));
                __m128i eq0 = _mm_cmpeq_epi32(any, zero128);
                if (_mm_movemask_epi8(eq0) != 0xFFFF) break;
                j += 8;
            }
#endif
            while (j <= dirty_hi && cover_arr[j] == 0 && area_arr[j] == 0)
                j++;
            if (j > x) {
                int span_alpha = lvg__agg_alpha(cover << 9, rule);
                if (span_alpha) {
                    int xa = xbb_min + x;
                    int xb = xbb_min + j;
                    if (xa < cx0) xa = cx0;
                    if (xb > cx1) xb = cx1;
                    if (xb > xa) {
                        uint32_t a = (uint32_t)((a_full
                                                 * (uint32_t)span_alpha
                                                 + 127) / 255);
                        if (a) {
                            uint32_t src = LVG_COLOR_ARGB(a, r_col, g_col,
                                                          b_col);
                            lvg_px_blend_over_constant_row(&dst_row[xa],
                                                            src, xb - xa);
                        }
                    }
                }
                x = j;
            }
        }

        /* Right-side gap. After the last dirty cell, running cover may
         * still be non-zero — that's the contribution from edges
         * extending right of bbox (whose cells we dropped). Fill the
         * remainder of the row using the final running cover. */
        if (cover != 0) {
            int span_alpha = lvg__agg_alpha(cover << 9, rule);
            if (span_alpha) {
                int xa = xbb_min + dirty_hi + 1;
                int xb = cx1;
                if (xa < cx0) xa = cx0;
                if (xb > xa) {
                    uint32_t a = (uint32_t)((a_full * (uint32_t)span_alpha
                                             + 127) / 255);
                    if (a) {
                        uint32_t src = LVG_COLOR_ARGB(a, r_col, g_col, b_col);
                        lvg_px_blend_over_constant_row(&dst_row[xa], src,
                                                        xb - xa);
                    }
                }
            }
        }
    }

    /* Final dirty-range reset to keep the shared buffer zeroed for
     * the next cmd in this strip. */
    if (dirty_hi >= dirty_lo) {
        size_t span = (size_t)(dirty_hi - dirty_lo + 1) * sizeof(int);
        memset(&cover_arr[dirty_lo], 0, span);
        memset(&area_arr[dirty_lo],  0, span);
    }
}

/* Enqueue: build edges, sort, append cmd to the batch. The actual
 * rasterization happens during flush. */
static void lvg__fill_polygon_dense(lvg_canvas_t *cv,
                                     const lvg_pointf_t *pts, int count,
                                     lvg_color_t color,
                                     lvg_fill_rule_t rule)
{
    if (!cv || !pts || count < 3) return;
    const lvg_rect_t *clip = &cv->_clip;
    int cx0 = clip->x, cy0 = clip->y;
    int cx1 = clip->x + clip->width;
    int cy1 = clip->y + clip->height;
    if (cx1 <= cx0 || cy1 <= cy0) return;

    /* Polygon bbox. */
    float fxmin = pts[0].x, fxmax = pts[0].x;
    float fymin = pts[0].y, fymax = pts[0].y;
    for (int i = 1; i < count; i++) {
        if (pts[i].x < fxmin) fxmin = pts[i].x;
        if (pts[i].x > fxmax) fxmax = pts[i].x;
        if (pts[i].y < fymin) fymin = pts[i].y;
        if (pts[i].y > fymax) fymax = pts[i].y;
    }

    int row_min = (int)floorf(fymin);
    int row_max = (int)floorf(fymax) + 1;
    if (row_min < cy0) row_min = cy0;
    if (row_max > cy1) row_max = cy1;
    if (row_max <= row_min) return;

    int xbb_min = (int)floorf(fxmin) - 1;
    int xbb_max = (int)floorf(fxmax) + 2;
    if (xbb_min < cx0) xbb_min = cx0;
    if (xbb_max > cx1) xbb_max = cx1;
    if (xbb_max <= xbb_min) return;

    /* Grow edge arena to hold up to `count` more edges. */
    if (lvg__batch_edge_n + count > lvg__batch_edge_cap) {
        int new_cap = lvg__batch_edge_cap ? lvg__batch_edge_cap : 256;
        while (lvg__batch_edge_n + count > new_cap) new_cap *= 2;
        lvg__dense_edge_t *p = (lvg__dense_edge_t *)realloc(
            lvg__batch_edges, (size_t)new_cap * sizeof(*p));
        if (!p) return;
        lvg__batch_edges = p;
        lvg__batch_edge_cap = new_cap;
    }
    lvg__dense_edge_t *edges = &lvg__batch_edges[lvg__batch_edge_n];

    int n_edges = 0;
    int row_min_sub = row_min << 8;
    int row_max_sub = row_max << 8;
    for (int i = 0; i < count; i++) {
        int j = (i + 1) == count ? 0 : i + 1;
        int x1 = lvg__roundf_to_int(pts[i].x * 256.0f);
        int y1 = lvg__roundf_to_int(pts[i].y * 256.0f);
        int x2 = lvg__roundf_to_int(pts[j].x * 256.0f);
        int y2 = lvg__roundf_to_int(pts[j].y * 256.0f);
        if (y1 == y2) continue;
        lvg__dense_edge_t *e = &edges[n_edges];
        if (y1 < y2) {
            e->ymin_sub = y1; e->ymax_sub = y2;
            e->x_at_ymin = x1; e->dx = x2 - x1;
            e->dy = y2 - y1; e->winding = 1;
        } else {
            e->ymin_sub = y2; e->ymax_sub = y1;
            e->x_at_ymin = x2; e->dx = x1 - x2;
            e->dy = y1 - y2; e->winding = -1;
        }
        if (e->ymax_sub <= row_min_sub) continue;
        if (e->ymin_sub >= row_max_sub) continue;
        e->dx_per_dy = (double)e->dx / (double)e->dy;
        n_edges++;
    }
    if (n_edges == 0) return;

    /* Sort edges by ymin_sub. */
    if (n_edges <= 32) {
        for (int i = 1; i < n_edges; i++) {
            lvg__dense_edge_t key = edges[i];
            int k = i - 1;
            while (k >= 0 && edges[k].ymin_sub > key.ymin_sub) {
                edges[k + 1] = edges[k]; k--;
            }
            edges[k + 1] = key;
        }
    } else {
        qsort(edges, (size_t)n_edges, sizeof(*edges),
              lvg__dense_cmp_edge_ymin);
    }

    /* Grow cmd buffer. */
    if (lvg__batch_cmd_n >= lvg__batch_cmd_cap) {
        int new_cap = lvg__batch_cmd_cap ? lvg__batch_cmd_cap * 2 : 64;
        lvg__poly_cmd_t *p = (lvg__poly_cmd_t *)realloc(
            lvg__batch_cmds, (size_t)new_cap * sizeof(*p));
        if (!p) return;
        lvg__batch_cmds = p;
        lvg__batch_cmd_cap = new_cap;
    }

    lvg__poly_cmd_t *cmd = &lvg__batch_cmds[lvg__batch_cmd_n++];
    cmd->edge_offset = lvg__batch_edge_n;
    cmd->edge_count  = n_edges;
    cmd->color       = color;
    cmd->rule        = rule;
    cmd->clip        = *clip;
    cmd->row_min     = row_min;
    cmd->row_max     = row_max;
    cmd->xbb_min     = xbb_min;
    cmd->xbb_max     = xbb_max;
    lvg__batch_edge_n += n_edges;
}

/* =========================================================================
 * Persistent worker pool for parallel strip rasterization.
 *
 * Created lazily on first multi-strip flush; workers spin on a condvar.
 * Per dispatch: main fills shared work descriptor + bumps a generation
 * counter, broadcasts cv_work, then waits on cv_done. Workers claim
 * strips via an atomic counter (work-stealing), each rasterizing every
 * cmd whose y range overlaps that strip into its own per-thread
 * cover/area/active scratch (TLS, persisted across dispatches by the
 * worker thread).
 *
 * Strip outputs are disjoint surface row ranges, so no synchronisation
 * is needed during rasterization itself.
 * ========================================================================= */
#if LVG_HAVE_PTHREADS
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#define LVG__POOL_MAX 8

typedef struct {
    pthread_t                thread;
    int                      wid;
} lvg__pool_worker_t;

static struct {
    pthread_mutex_t          mu;
    pthread_cond_t           cv_work;
    pthread_cond_t           cv_done;
    int                      n_workers;
    int                      quit;
    uint64_t                 gen_avail;
    int                      n_done;
    /* Shared work descriptor (read-only for workers during a dispatch). */
    lvg_canvas_t            *cv;
    lvg__poly_cmd_t         *cmds;
    int                      n_cmds;
    const lvg__dense_edge_t *edges;
    int                      gy_min;
    int                      gy_max;
    int                      strip_h;
    int                      max_bbox_w;
    int                      max_edges;
    /* Atomic strip claim counter, reset to 0 each dispatch. */
    atomic_int               next_strip;
    int                      total_strips;
    lvg__pool_worker_t       workers[LVG__POOL_MAX];
} lvg__pool;

static int lvg__pool_inited = 0;
static pthread_once_t lvg__pool_once_init = PTHREAD_ONCE_INIT;

static void *lvg__pool_worker_main(void *arg)
{
    int wid = (int)(intptr_t)arg;
    /* Per-worker scratch — TLS is zero-initialised on thread start, so
     * subsequent grow paths see cap == 0 and realloc from NULL. */
    int *cover = NULL;
    int *area  = NULL;
    int *active = NULL;
    int  xy_cap = 0;
    int  active_cap = 0;
    uint64_t gen_done = 0;

    pthread_mutex_lock(&lvg__pool.mu);
    while (1) {
        while (!lvg__pool.quit && gen_done == lvg__pool.gen_avail)
            pthread_cond_wait(&lvg__pool.cv_work, &lvg__pool.mu);
        if (lvg__pool.quit) break;
        uint64_t my_gen = lvg__pool.gen_avail;
        int max_bbox_w = lvg__pool.max_bbox_w;
        int max_edges  = lvg__pool.max_edges;
        pthread_mutex_unlock(&lvg__pool.mu);

        /* Grow per-thread scratch. */
        if (max_bbox_w > xy_cap) {
            int new_cap = xy_cap ? xy_cap : 256;
            while (new_cap < max_bbox_w) new_cap *= 2;
            int *nc = (int *)realloc(cover, (size_t)new_cap * sizeof(int));
            int *na = (int *)realloc(area,  (size_t)new_cap * sizeof(int));
            if (nc && na) {
                memset(nc + xy_cap, 0, (size_t)(new_cap - xy_cap) * sizeof(int));
                memset(na + xy_cap, 0, (size_t)(new_cap - xy_cap) * sizeof(int));
                cover = nc; area = na; xy_cap = new_cap;
            } else { free(nc); free(na); cover = area = NULL; xy_cap = 0; }
        }
        if (max_edges > active_cap) {
            int new_cap = active_cap ? active_cap : 64;
            while (new_cap < max_edges) new_cap *= 2;
            int *p = (int *)realloc(active, (size_t)new_cap * sizeof(int));
            if (p) { active = p; active_cap = new_cap; }
        }

        /* Work-steal strips. */
        if (cover && area && active) {
            for (;;) {
                int si = atomic_fetch_add_explicit(&lvg__pool.next_strip, 1,
                                                    memory_order_relaxed);
                if (si >= lvg__pool.total_strips) break;
                int strip_y0 = lvg__pool.gy_min + si * lvg__pool.strip_h;
                int strip_y1 = strip_y0 + lvg__pool.strip_h;
                if (strip_y1 > lvg__pool.gy_max) strip_y1 = lvg__pool.gy_max;
                for (int i = 0; i < lvg__pool.n_cmds; i++) {
                    const lvg__poly_cmd_t *c = &lvg__pool.cmds[i];
                    if (c->row_max <= strip_y0) continue;
                    if (c->row_min >= strip_y1) continue;
                    lvg__rasterize_cmd_strip(lvg__pool.cv, c,
                                              lvg__pool.edges,
                                              strip_y0, strip_y1,
                                              cover, area, active);
                }
            }
        }

        gen_done = my_gen;
        pthread_mutex_lock(&lvg__pool.mu);
        lvg__pool.n_done++;
        if (lvg__pool.n_done >= lvg__pool.n_workers)
            pthread_cond_signal(&lvg__pool.cv_done);
    }
    pthread_mutex_unlock(&lvg__pool.mu);

    free(cover); free(area); free(active);
    (void)wid;
    return NULL;
}

static void lvg__pool_init(void)
{
    int hw = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (hw < 1) hw = 1;
    if (hw > LVG__POOL_MAX) hw = LVG__POOL_MAX;
    /* Override via env for benchmarking. */
    const char *env = getenv("LVG_RASTER_THREADS");
    if (env) {
        int v = atoi(env);
        if (v >= 1 && v <= LVG__POOL_MAX) hw = v;
    }
    pthread_mutex_init(&lvg__pool.mu, NULL);
    pthread_cond_init(&lvg__pool.cv_work, NULL);
    pthread_cond_init(&lvg__pool.cv_done, NULL);
    lvg__pool.n_workers = hw;
    lvg__pool.gen_avail = 0;
    lvg__pool.n_done = 0;
    lvg__pool.quit = 0;
    atomic_init(&lvg__pool.next_strip, 0);
    for (int i = 0; i < hw; i++) {
        lvg__pool.workers[i].wid = i;
        pthread_create(&lvg__pool.workers[i].thread, NULL,
                       lvg__pool_worker_main, (void *)(intptr_t)i);
    }
    lvg__pool_inited = 1;
}

static void lvg__pool_ensure(void)
{
    pthread_once(&lvg__pool_once_init, lvg__pool_init);
}

/* Returns 1 if the dispatch ran (workers handled the strips), 0 if the
 * caller should fall back to serial. */
static int lvg__pool_dispatch(lvg_canvas_t *cv,
                               lvg__poly_cmd_t *cmds, int n_cmds,
                               const lvg__dense_edge_t *edges,
                               int gy_min, int gy_max, int strip_h,
                               int max_bbox_w, int max_edges,
                               int total_strips)
{
    lvg__pool_ensure();
    if (!lvg__pool_inited || lvg__pool.n_workers <= 1) return 0;

    pthread_mutex_lock(&lvg__pool.mu);
    lvg__pool.cv = cv;
    lvg__pool.cmds = cmds;
    lvg__pool.n_cmds = n_cmds;
    lvg__pool.edges = edges;
    lvg__pool.gy_min = gy_min;
    lvg__pool.gy_max = gy_max;
    lvg__pool.strip_h = strip_h;
    lvg__pool.max_bbox_w = max_bbox_w;
    lvg__pool.max_edges = max_edges;
    lvg__pool.total_strips = total_strips;
    atomic_store_explicit(&lvg__pool.next_strip, 0, memory_order_relaxed);
    lvg__pool.n_done = 0;
    lvg__pool.gen_avail++;
    pthread_cond_broadcast(&lvg__pool.cv_work);
    while (lvg__pool.n_done < lvg__pool.n_workers)
        pthread_cond_wait(&lvg__pool.cv_done, &lvg__pool.mu);
    pthread_mutex_unlock(&lvg__pool.mu);
    return 1;
}
#endif /* LVG_HAVE_PTHREADS */

/* Strip-based flush: walk the canvas in 64-row horizontal strips; for
 * each strip rasterize every cmd whose y range overlaps. The shared
 * cover/area buffers stay hot in L1 across all cmds in the strip, and
 * the surface strip stays hot in L2. */
static void lvg__flush_polygon_batch(lvg_canvas_t *cv)
{
    if (!cv || lvg__batch_cmd_n == 0) return;

    int n_cmds = lvg__batch_cmd_n;
    lvg__poly_cmd_t *cmds = lvg__batch_cmds;

    /* Global y range (for strip iteration) and max bbox_w (for buffer
     * sizing — we share cover/area across cmds). */
    int gy_min = INT_MAX, gy_max = INT_MIN;
    int max_bbox_w = 0;
    int max_edges = 0;
    for (int i = 0; i < n_cmds; i++) {
        const lvg__poly_cmd_t *c = &cmds[i];
        if (c->row_min < gy_min) gy_min = c->row_min;
        if (c->row_max > gy_max) gy_max = c->row_max;
        int bw = c->xbb_max - c->xbb_min;
        if (bw > max_bbox_w) max_bbox_w = bw;
        if (c->edge_count > max_edges) max_edges = c->edge_count;
    }
    if (gy_max <= gy_min || max_bbox_w == 0) {
        lvg__batch_cmd_n = 0; lvg__batch_edge_n = 0;
        return;
    }

    /* Grow shared cover/area buffer to hold max_bbox_w; tail is zeroed
     * on growth. The dirty-range memset invariant keeps the live
     * prefix zero between cmds. */
    if (max_bbox_w > lvg__strip_xy_cap) {
        int new_cap = lvg__strip_xy_cap ? lvg__strip_xy_cap : 256;
        while (new_cap < max_bbox_w) new_cap *= 2;
        int *nc = (int *)realloc(lvg__strip_cover,
                                  (size_t)new_cap * sizeof(int));
        int *na = (int *)realloc(lvg__strip_area,
                                  (size_t)new_cap * sizeof(int));
        if (!nc || !na) { free(nc); free(na);
            lvg__batch_cmd_n = 0; lvg__batch_edge_n = 0; return; }
        memset(nc + lvg__strip_xy_cap, 0,
               (size_t)(new_cap - lvg__strip_xy_cap) * sizeof(int));
        memset(na + lvg__strip_xy_cap, 0,
               (size_t)(new_cap - lvg__strip_xy_cap) * sizeof(int));
        lvg__strip_cover = nc;
        lvg__strip_area  = na;
        lvg__strip_xy_cap = new_cap;
    }
    if (max_edges > lvg__strip_active_cap) {
        int new_cap = lvg__strip_active_cap ? lvg__strip_active_cap : 64;
        while (new_cap < max_edges) new_cap *= 2;
        int *p = (int *)realloc(lvg__strip_active,
                                 (size_t)new_cap * sizeof(int));
        if (!p) { lvg__batch_cmd_n = 0; lvg__batch_edge_n = 0; return; }
        lvg__strip_active = p;
        lvg__strip_active_cap = new_cap;
    }

    /* STRIP_H trades per-cmd-per-strip overhead (smaller strips: more
     * AET re-builds for cmds spanning multiple strips) against cache
     * pressure (larger: exceeds L2). 64 rows works well in parallel:
     * gives enough strips to feed N workers, surface strip stays in
     * L2, and cmds that span multiple strips re-build AET cheaply. */
    const int STRIP_H = 64;
#if LVG_HAVE_PTHREADS
    int total_strips = (gy_max - gy_min + STRIP_H - 1) / STRIP_H;
#endif

#if LVG_HAVE_PTHREADS
    /* Parallel dispatch via the persistent worker pool — work-stealing
     * over strips. Strip outputs are disjoint surface row ranges, so
     * no cross-thread synchronisation is needed during rasterization. */
    if (total_strips >= 2 && lvg__pool_dispatch(cv, cmds, n_cmds,
                                                 lvg__batch_edges,
                                                 gy_min, gy_max, STRIP_H,
                                                 max_bbox_w, max_edges,
                                                 total_strips)) {
        lvg__batch_cmd_n = 0;
        lvg__batch_edge_n = 0;
        return;
    }
#endif

    /* Serial fallback. */
    for (int strip_y0 = gy_min; strip_y0 < gy_max; strip_y0 += STRIP_H) {
        int strip_y1 = strip_y0 + STRIP_H;
        if (strip_y1 > gy_max) strip_y1 = gy_max;
        for (int i = 0; i < n_cmds; i++) {
            const lvg__poly_cmd_t *c = &cmds[i];
            if (c->row_max <= strip_y0) continue;
            if (c->row_min >= strip_y1) continue;
            lvg__rasterize_cmd_strip(cv, c, lvg__batch_edges,
                                      strip_y0, strip_y1,
                                      lvg__strip_cover, lvg__strip_area,
                                      lvg__strip_active);
        }
    }

    lvg__batch_cmd_n = 0;
    lvg__batch_edge_n = 0;
}


void lvg_canvas_draw_line_aa(lvg_canvas_t *canvas,
                               float x0, float y0, float x1, float y1,
                               lvg_color_t color)
{
    LVG_DISPATCH(draw_line_aa, x0, y0, x1, y1, color);
    if (!canvas) return;

    float dx = x1 - x0;
    float dy = y1 - y0;
    int steep = fabsf(dy) > fabsf(dx);

    if (steep) {
        float t;
        t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        float t;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }

    dx = x1 - x0;
    dy = y1 - y0;
    float gradient = (dx == 0.0f) ? 1.0f : dy / dx;

    /* First endpoint */
    float xend = roundf(x0);
    float yend = y0 + gradient * (xend - x0);
    float xgap = lvg__rfpart(x0 + 0.5f);
    int xpxl1 = (int)xend;
    int ypxl1 = (int)floorf(yend);

    if (steep) {
        lvg__aa_pixel(canvas, ypxl1,     xpxl1, color, lvg__rfpart(yend) * xgap);
        lvg__aa_pixel(canvas, ypxl1 + 1, xpxl1, color, lvg__fpart(yend) * xgap);
    } else {
        lvg__aa_pixel(canvas, xpxl1, ypxl1,     color, lvg__rfpart(yend) * xgap);
        lvg__aa_pixel(canvas, xpxl1, ypxl1 + 1, color, lvg__fpart(yend) * xgap);
    }

    float intery = yend + gradient;

    /* Second endpoint */
    xend = roundf(x1);
    yend = y1 + gradient * (xend - x1);
    xgap = lvg__fpart(x1 + 0.5f);
    int xpxl2 = (int)xend;
    int ypxl2 = (int)floorf(yend);

    if (steep) {
        lvg__aa_pixel(canvas, ypxl2,     xpxl2, color, lvg__rfpart(yend) * xgap);
        lvg__aa_pixel(canvas, ypxl2 + 1, xpxl2, color, lvg__fpart(yend) * xgap);
    } else {
        lvg__aa_pixel(canvas, xpxl2, ypxl2,     color, lvg__rfpart(yend) * xgap);
        lvg__aa_pixel(canvas, xpxl2, ypxl2 + 1, color, lvg__fpart(yend) * xgap);
    }

    /* Main loop */
    for (int x = xpxl1 + 1; x < xpxl2; x++) {
        int iy = (int)floorf(intery);
        float frac = lvg__fpart(intery);
        if (steep) {
            lvg__aa_pixel(canvas, iy,     x, color, 1.0f - frac);
            lvg__aa_pixel(canvas, iy + 1, x, color, frac);
        } else {
            lvg__aa_pixel(canvas, x, iy,     color, 1.0f - frac);
            lvg__aa_pixel(canvas, x, iy + 1, color, frac);
        }
        intery += gradient;
    }
}

void lvg_canvas_draw_polyline_aa(lvg_canvas_t *canvas,
                                   const float *xy_pairs, int point_count,
                                   lvg_color_t color)
{
    if (!xy_pairs || point_count < 2) return;
    for (int i = 0; i < point_count - 1; i++) {
        lvg_canvas_draw_line_aa(canvas,
                                 xy_pairs[i * 2], xy_pairs[i * 2 + 1],
                                 xy_pairs[(i + 1) * 2], xy_pairs[(i + 1) * 2 + 1],
                                 color);
    }
}

/* -------------------------------------------------------------------------
 * Float-coordinate thick polyline (constant pixel width)
 * ------------------------------------------------------------------------- */

/*
 * For each pair of consecutive segments, compute the miter offset at the join.
 * The polyline is expanded into a quad strip and filled with triangles.
 * This gives constant pixel width regardless of world-to-screen zoom.
 */

static void lvg__thick_segment(lvg_canvas_t *canvas,
                                float x0, float y0, float x1, float y1,
                                float nx0, float ny0, float nx1, float ny1,
                                lvg_color_t color)
{
    /* Quad: (x0±n0, y0±n0) to (x1±n1, y1±n1) */
    int ax = (int)(x0 + nx0 + 0.5f), ay = (int)(y0 + ny0 + 0.5f);
    int bx = (int)(x1 + nx1 + 0.5f), by = (int)(y1 + ny1 + 0.5f);
    int cx = (int)(x1 - nx1 + 0.5f), cy = (int)(y1 - ny1 + 0.5f);
    int dx = (int)(x0 - nx0 + 0.5f), dy = (int)(y0 - ny0 + 0.5f);

    lvg_canvas_fill_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    lvg_canvas_fill_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

void lvg_canvas_draw_thick_polyline(lvg_canvas_t *canvas,
                                      const lvg_pointf_t *points, int count,
                                      lvg_color_t color, float width,
                                      bool closed)
{
    LVG_DISPATCH(draw_thick_polyline, points, count, color, width, closed);
    if (!canvas || !points || count < 2) return;
    if (width < 0.5f) width = 0.5f;
    float half_w = width * 0.5f;

    /* Maximum miter length (prevent spikes at sharp angles) */
    float miter_limit = width * 2.0f;

    /* Per-vertex normals. Stack-allocate for the common small case; spill
     * to malloc for long subpaths (a complex SVG <path> can flatten to
     * thousands of points after Bézier subdivision, especially when
     * zoomed in — silently truncating creates broken-stroke artifacts). */
    enum { LVG__POLYLINE_STACK = 512 };
    float nx_stack[LVG__POLYLINE_STACK], ny_stack[LVG__POLYLINE_STACK];
    float *nx = nx_stack, *ny = ny_stack;
    float *nx_heap = NULL;
    if (count > LVG__POLYLINE_STACK) {
        nx_heap = (float *)malloc((size_t)count * 2 * sizeof(float));
        if (nx_heap) {
            nx = nx_heap;
            ny = nx_heap + count;
        } else {
            count = LVG__POLYLINE_STACK;
        }
    }

    for (int i = 0; i < count; i++) {
        /* Compute tangent directions of adjacent segments */
        int prev_i, next_i;
        if (closed) {
            prev_i = (i - 1 + count) % count;
            next_i = (i + 1) % count;
        } else {
            prev_i = i > 0 ? i - 1 : i;
            next_i = i < count - 1 ? i + 1 : i;
        }

        float dx1 = points[i].x - points[prev_i].x;
        float dy1 = points[i].y - points[prev_i].y;
        float dx2 = points[next_i].x - points[i].x;
        float dy2 = points[next_i].y - points[i].y;

        float len1 = sqrtf(dx1 * dx1 + dy1 * dy1);
        float len2 = sqrtf(dx2 * dx2 + dy2 * dy2);

        /* Normals perpendicular to each segment (rotate 90 degrees) */
        float n1x = 0, n1y = 0, n2x = 0, n2y = 0;
        if (len1 > 0.0001f) { n1x = -dy1 / len1; n1y = dx1 / len1; }
        if (len2 > 0.0001f) { n2x = -dy2 / len2; n2y = dx2 / len2; }

        /* Average normal for miter join */
        float mnx, mny;
        if (i == prev_i || len1 < 0.0001f) {
            mnx = n2x; mny = n2y;
        } else if (i == next_i || len2 < 0.0001f) {
            mnx = n1x; mny = n1y;
        } else {
            mnx = (n1x + n2x) * 0.5f;
            mny = (n1y + n2y) * 0.5f;
            float ml = sqrtf(mnx * mnx + mny * mny);
            if (ml > 0.0001f) {
                /* Scale to produce correct width at the miter */
                float dot = mnx * n1x + mny * n1y;
                if (dot > 0.0001f) {
                    float scale = 1.0f / dot;
                    /* Clamp miter */
                    if (scale * ml * half_w > miter_limit) {
                        mnx = n1x; mny = n1y;
                    } else {
                        mnx *= scale;
                        mny *= scale;
                    }
                }
            } else {
                mnx = n1x; mny = n1y;
            }
        }

        nx[i] = mnx * half_w;
        ny[i] = mny * half_w;
    }

    /* Draw segments as quad strips */
    int seg_count = closed ? count : count - 1;
    for (int i = 0; i < seg_count; i++) {
        int j = (i + 1) % count;
        lvg__thick_segment(canvas,
                           points[i].x, points[i].y,
                           points[j].x, points[j].y,
                           nx[i], ny[i], nx[j], ny[j],
                           color);
    }

    free(nx_heap);
}

/* -------------------------------------------------------------------------
 * Arrow heads
 * ------------------------------------------------------------------------- */

static void lvg__draw_arrowhead(lvg_canvas_t *canvas,
                                 int tip_x, int tip_y,
                                 float dx, float dy, float len,
                                 lvg_color_t color, int stroke_width,
                                 lvg_arrow_style_t style, int size)
{
    if (style == LVG_ARROW_NONE || len < 0.5f) return;
    float ux = dx / len;
    float uy = dy / len;
    /* Perpendicular */
    float px = -uy;
    float py =  ux;

    float half = (float)size * 0.4f;

    if (style == LVG_ARROW_FILLED) {
        int bx = tip_x - (int)(ux * size);
        int by = tip_y - (int)(uy * size);
        int lx = bx + (int)(px * half);
        int ly = by + (int)(py * half);
        int rx = bx - (int)(px * half);
        int ry = by - (int)(py * half);
        lvg_canvas_fill_triangle(canvas, tip_x, tip_y, lx, ly, rx, ry, color);
    } else if (style == LVG_ARROW_OPEN) {
        int lx = tip_x - (int)(ux * size) + (int)(px * half);
        int ly = tip_y - (int)(uy * size) + (int)(py * half);
        int rx = tip_x - (int)(ux * size) - (int)(px * half);
        int ry = tip_y - (int)(uy * size) - (int)(py * half);
        lvg_canvas_draw_line(canvas, lx, ly, tip_x, tip_y, color, stroke_width);
        lvg_canvas_draw_line(canvas, rx, ry, tip_x, tip_y, color, stroke_width);
    } else if (style == LVG_ARROW_DIAMOND) {
        int fwd  = size / 2;
        int back = size / 2;
        int mx  = tip_x - (int)(ux * fwd);
        int my  = tip_y - (int)(uy * fwd);
        int far_x = tip_x - (int)(ux * (fwd + back));
        int far_y = tip_y - (int)(uy * (fwd + back));
        int lx = mx + (int)(px * half);
        int ly = my + (int)(py * half);
        int rx = mx - (int)(px * half);
        int ry = my - (int)(py * half);
        lvg_canvas_fill_triangle(canvas, tip_x, tip_y, lx, ly, rx, ry, color);
        lvg_canvas_fill_triangle(canvas, far_x, far_y, lx, ly, rx, ry, color);
    }
}

void lvg_canvas_draw_arrow(lvg_canvas_t *canvas,
                             int x0, int y0, int x1, int y1,
                             lvg_color_t color, int stroke_width,
                             lvg_arrow_style_t head_style,
                             lvg_arrow_style_t tail_style,
                             int head_size)
{
    LVG_DISPATCH(draw_arrow, x0, y0, x1, y1, color, stroke_width,
                 head_style, tail_style, head_size);
    if (!canvas) return;
    if (head_size <= 0) head_size = 10;

    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);

    /* Draw shaft */
    lvg_canvas_draw_line(canvas, x0, y0, x1, y1, color, stroke_width);

    /* Head at (x1, y1): direction is (dx, dy) */
    lvg__draw_arrowhead(canvas, x1, y1, dx, dy, len,
                         color, stroke_width, head_style, head_size);

    /* Tail at (x0, y0): direction is (-dx, -dy) */
    lvg__draw_arrowhead(canvas, x0, y0, -dx, -dy, len,
                         color, stroke_width, tail_style, head_size);
}

/* -------------------------------------------------------------------------
 * Pattern / hatch fill
 * ------------------------------------------------------------------------- */

void lvg_canvas_fill_rect_hatched(lvg_canvas_t *canvas,
                                    int x, int y, int w, int h,
                                    lvg_color_t bg_color,
                                    lvg_color_t fg_color,
                                    lvg_hatch_style_t style,
                                    int spacing, int line_width)
{
    LVG_DISPATCH(fill_rect_hatched, x, y, w, h, bg_color, fg_color,
                 style, spacing, line_width);
    if (!canvas || w <= 0 || h <= 0) return;
    if (spacing < 2) spacing = 6;
    if (line_width < 1) line_width = 1;

    /* Background fill */
    if (LVG_COLOR_A(bg_color) > 0)
        lvg_canvas_fill_rect(canvas, x, y, w, h, bg_color);

    /* Save clip */
    lvg_rect_t saved_clip = canvas->_clip;
    lvg_rect_t hatch_clip = lvg_rect_make(x, y, w, h);
    hatch_clip = lvg_rect_intersect(&hatch_clip, &saved_clip);
    lvg_canvas_set_clip(canvas, &hatch_clip);

    switch (style) {
    case LVG_HATCH_HORIZONTAL:
        for (int hy = y; hy < y + h; hy += spacing)
            lvg_canvas_fill_rect(canvas, x, hy, w, line_width, fg_color);
        break;

    case LVG_HATCH_VERTICAL:
        for (int hx = x; hx < x + w; hx += spacing)
            lvg_canvas_fill_rect(canvas, hx, y, line_width, h, fg_color);
        break;

    case LVG_HATCH_FORWARD_DIAG: /* / */
        for (int d = -(h); d < w + h; d += spacing)
            lvg_canvas_draw_line(canvas, x + d, y + h, x + d + h, y,
                                  fg_color, line_width);
        break;

    case LVG_HATCH_BACK_DIAG: /* \ */
        for (int d = -(h); d < w + h; d += spacing)
            lvg_canvas_draw_line(canvas, x + d, y, x + d + h, y + h,
                                  fg_color, line_width);
        break;

    case LVG_HATCH_CROSS:
        for (int hy = y; hy < y + h; hy += spacing)
            lvg_canvas_fill_rect(canvas, x, hy, w, line_width, fg_color);
        for (int hx = x; hx < x + w; hx += spacing)
            lvg_canvas_fill_rect(canvas, hx, y, line_width, h, fg_color);
        break;

    case LVG_HATCH_DIAG_CROSS:
        for (int d = -(h); d < w + h; d += spacing) {
            lvg_canvas_draw_line(canvas, x + d, y + h, x + d + h, y,
                                  fg_color, line_width);
            lvg_canvas_draw_line(canvas, x + d, y, x + d + h, y + h,
                                  fg_color, line_width);
        }
        break;
    }

    /* Restore clip — must go through lvg_canvas_set_clip so that pluggable
     * backends (blend2d, thorvg, …) get the notification. A direct struct
     * write would leave the backend's private clip stuck on hatch_clip. */
    lvg_canvas_set_clip(canvas, &saved_clip);
}

void lvg_canvas_fill_polygon_hatched(lvg_canvas_t *canvas,
                                       const lvg_point_t *points, int count,
                                       lvg_color_t bg_color,
                                       lvg_color_t fg_color,
                                       lvg_hatch_style_t style,
                                       int spacing, int line_width)
{
    if (!canvas || !points || count < 3) return;

    /* Fill background polygon */
    if (LVG_COLOR_A(bg_color) > 0)
        lvg_canvas_fill_polygon(canvas, points, count, bg_color);

    /* Find bounding box */
    int xmin = points[0].x, xmax = points[0].x;
    int ymin = points[0].y, ymax = points[0].y;
    for (int i = 1; i < count; i++) {
        if (points[i].x < xmin) xmin = points[i].x;
        if (points[i].x > xmax) xmax = points[i].x;
        if (points[i].y < ymin) ymin = points[i].y;
        if (points[i].y > ymax) ymax = points[i].y;
    }

    /*
     * For each hatch line, clip it against the polygon edges using scanline
     * intersection.  For simplicity, draw the hatch as rect then re-fill the
     * polygon to mask the outside.  A proper implementation would clip each
     * hatch line against polygon edges.
     *
     * Since we have the polygon fill, we use a stencil approach:
     * 1. Draw hatch in the bounding box
     * 2. Clear outside with transparent (not possible with blending)
     *
     * Instead, we scanline-clip each hatch line row-by-row against the polygon.
     */
    if (spacing < 2) spacing = 6;
    if (line_width < 1) line_width = 1;

    /* Helper: for a given y, find x-intersections with polygon and fill
       only inside those spans. We reuse the polygon scanline intersection. */
    for (int y = ymin; y <= ymax; y++) {
        /* Collect x-intersections */
        int xs[64];
        int nx = 0;
        for (int i = 0; i < count && nx < 64; i++) {
            int j = (i + 1) % count;
            int yi = points[i].y, yj = points[j].y;
            if (yi == yj) continue;
            int y_lo = yi < yj ? yi : yj;
            int y_hi = yi > yj ? yi : yj;
            if (y < y_lo || y >= y_hi) continue;
            int xi = points[i].x, xj = points[j].x;
            xs[nx++] = xi + (xj - xi) * (y - yi) / (yj - yi);
        }
        /* Sort */
        for (int i = 0; i < nx - 1; i++)
            for (int j = i + 1; j < nx; j++)
                if (xs[i] > xs[j]) { int t = xs[i]; xs[i] = xs[j]; xs[j] = t; }

        /* Check if this row has a hatch line */
        int should_draw = 0;
        switch (style) {
        case LVG_HATCH_HORIZONTAL:
            should_draw = ((y - ymin) % spacing) < line_width;
            break;
        case LVG_HATCH_VERTICAL:
            /* Handled per-pixel below */
            should_draw = 1;
            break;
        case LVG_HATCH_CROSS:
            should_draw = 1;
            break;
        default:
            should_draw = 1;
            break;
        }

        if (!should_draw) continue;

        /* Draw inside polygon spans */
        for (int s = 0; s + 1 < nx; s += 2) {
            int x_start = xs[s];
            int x_end   = xs[s + 1];

            switch (style) {
            case LVG_HATCH_HORIZONTAL:
                lvg_canvas_fill_rect(canvas, x_start, y,
                                      x_end - x_start + 1, 1, fg_color);
                break;
            case LVG_HATCH_VERTICAL:
                for (int hx = x_start; hx <= x_end; hx++) {
                    if (((hx - xmin) % spacing) < line_width)
                        lvg_canvas_fill_rect(canvas, hx, y, 1, 1, fg_color);
                }
                break;
            case LVG_HATCH_CROSS:
                for (int hx = x_start; hx <= x_end; hx++) {
                    int is_h = ((y - ymin) % spacing) < line_width;
                    int is_v = ((hx - xmin) % spacing) < line_width;
                    if (is_h || is_v)
                        lvg_canvas_fill_rect(canvas, hx, y, 1, 1, fg_color);
                }
                break;
            case LVG_HATCH_FORWARD_DIAG:
                for (int hx = x_start; hx <= x_end; hx++) {
                    if ((((hx - xmin) + (y - ymin)) % spacing) < line_width)
                        lvg_canvas_fill_rect(canvas, hx, y, 1, 1, fg_color);
                }
                break;
            case LVG_HATCH_BACK_DIAG:
                for (int hx = x_start; hx <= x_end; hx++) {
                    if ((((hx - xmin) - (y - ymin) + spacing * 1000) % spacing) < line_width)
                        lvg_canvas_fill_rect(canvas, hx, y, 1, 1, fg_color);
                }
                break;
            case LVG_HATCH_DIAG_CROSS:
                for (int hx = x_start; hx <= x_end; hx++) {
                    int d1 = ((hx - xmin) + (y - ymin)) % spacing;
                    int d2 = ((hx - xmin) - (y - ymin) + spacing * 1000) % spacing;
                    if (d1 < line_width || d2 < line_width)
                        lvg_canvas_fill_rect(canvas, hx, y, 1, 1, fg_color);
                }
                break;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Blit / composite
 * ------------------------------------------------------------------------- */

void lvg_canvas_blit(lvg_canvas_t *canvas, int dst_x, int dst_y,
                      const lvg_surface_t *src, const lvg_rect_t *src_rect)
{
    if (!src || !src->pixels) return;

    /* Determine source region */
    int sx = 0, sy = 0;
    int sw = src->width, sh = src->height;
    if (src_rect) {
        sx = src_rect->x; sy = src_rect->y;
        sw = src_rect->width; sh = src_rect->height;
    }
    if (sw <= 0 || sh <= 0) return;

    /* Clip destination against canvas clip */
    const lvg_rect_t *clip = &canvas->_clip;

    int cx0 = dst_x < clip->x ? clip->x : dst_x;
    int cy0 = dst_y < clip->y ? clip->y : dst_y;
    int cx1 = (dst_x + sw) > (clip->x + clip->width)  ? (clip->x + clip->width)  : (dst_x + sw);
    int cy1 = (dst_y + sh) > (clip->y + clip->height) ? (clip->y + clip->height) : (dst_y + sh);
    if (cx0 >= cx1 || cy0 >= cy1) return;

    /* Adjust source start to match clipped destination */
    sx += cx0 - dst_x;
    sy += cy0 - dst_y;

    int copy_w = cx1 - cx0;
    int copy_h = cy1 - cy0;

    lvg_surface_t *dst = canvas->_surface;
    for (int row = 0; row < copy_h; row++) {
        const uint32_t *s = &src->pixels[(sy + row) * src->stride + sx];
        uint32_t       *d = &dst->pixels[(cy0 + row) * dst->stride + cx0];
        for (int col = 0; col < copy_w; col++)
            *d++ = *s++;
    }
}

void lvg_canvas_blend(lvg_canvas_t *canvas, int dst_x, int dst_y,
                       const lvg_surface_t *src, const lvg_rect_t *src_rect)
{
    if (!src || !src->pixels) return;

    int sx = 0, sy = 0;
    int sw = src->width, sh = src->height;
    if (src_rect) {
        sx = src_rect->x; sy = src_rect->y;
        sw = src_rect->width; sh = src_rect->height;
    }
    if (sw <= 0 || sh <= 0) return;

    const lvg_rect_t *clip = &canvas->_clip;
    int cx0 = dst_x < clip->x ? clip->x : dst_x;
    int cy0 = dst_y < clip->y ? clip->y : dst_y;
    int cx1 = (dst_x + sw) > (clip->x + clip->width)  ? (clip->x + clip->width)  : (dst_x + sw);
    int cy1 = (dst_y + sh) > (clip->y + clip->height) ? (clip->y + clip->height) : (dst_y + sh);
    if (cx0 >= cx1 || cy0 >= cy1) return;

    sx += cx0 - dst_x;
    sy += cy0 - dst_y;

    int copy_w = cx1 - cx0;
    int copy_h = cy1 - cy0;

    lvg_surface_t *d = canvas->_surface;
    for (int row = 0; row < copy_h; row++) {
        const uint32_t *sp = &src->pixels[(sy + row) * src->stride + sx];
        uint32_t       *dp = &d->pixels[(cy0 + row) * d->stride + cx0];
        for (int col = 0; col < copy_w; col++) {
            *dp = lvg_px_blend_over(*dp, *sp);
            dp++; sp++;
        }
    }
}

void lvg_canvas_draw_image(lvg_canvas_t *canvas,
                            int dst_x, int dst_y, int dst_w, int dst_h,
                            const lvg_surface_t *src, const lvg_rect_t *src_rect,
                            lvg_image_filter_t filter)
{
    int sx = 0, sy = 0;
    int sw, sh;
    int x0, y0, x1, y1;
    double scale_x, scale_y;
    int x, y;

    if (!canvas || !canvas->_surface || !src || !src->pixels) return;
    if (dst_w <= 0 || dst_h <= 0) return;

    if (src_rect) {
        lvg_rect_t bounds = lvg_rect_make(0, 0, src->width, src->height);
        lvg_rect_t clipped = lvg_rect_intersect(&bounds, src_rect);
        if (lvg_rect_is_empty(&clipped)) return;
        sx = clipped.x;
        sy = clipped.y;
        sw = clipped.width;
        sh = clipped.height;
    } else {
        sw = src->width;
        sh = src->height;
    }

    x0 = dst_x < canvas->_clip.x ? canvas->_clip.x : dst_x;
    y0 = dst_y < canvas->_clip.y ? canvas->_clip.y : dst_y;
    x1 = (dst_x + dst_w) > (canvas->_clip.x + canvas->_clip.width)
           ? (canvas->_clip.x + canvas->_clip.width)
           : (dst_x + dst_w);
    y1 = (dst_y + dst_h) > (canvas->_clip.y + canvas->_clip.height)
           ? (canvas->_clip.y + canvas->_clip.height)
           : (dst_y + dst_h);
    if (x0 >= x1 || y0 >= y1) return;

    scale_x = (double)sw / (double)dst_w;
    scale_y = (double)sh / (double)dst_h;

    for (y = y0; y < y1; y++) {
        double v = (double)sy + (((double)(y - dst_y) + 0.5) * scale_y - 0.5);
        uint32_t *dp = &canvas->_surface->pixels[y * canvas->_surface->stride + x0];

        for (x = x0; x < x1; x++, dp++) {
            double u = (double)sx + (((double)(x - dst_x) + 0.5) * scale_x - 0.5);
            uint32_t sample;

            switch (filter) {
            case LVG_IMAGE_FILTER_NEAREST:
                sample = lvg__sample_nearest(src, sx, sy, sw, sh, u, v);
                break;
            case LVG_IMAGE_FILTER_LANCZOS3:
                sample = lvg__sample_lanczos3(src, sx, sy, sw, sh, u, v);
                break;
            case LVG_IMAGE_FILTER_BILINEAR:
            default:
                sample = lvg__sample_bilinear(src, sx, sy, sw, sh, u, v);
                break;
            }

            *dp = lvg_px_blend_over(*dp, sample);
        }
    }
}

/* =========================================================================
 * Bezier curves
 * ========================================================================= */

/* Flatten a cubic Bezier into line segments using De Casteljau subdivision.
 * Recursion depth is bounded by flatness threshold. */
static void lvg__bezier_cubic_recursive(lvg_canvas_t *canvas,
    float x0, float y0, float x1, float y1,
    float x2, float y2, float x3, float y3,
    lvg_color_t color, int stroke_width, int depth)
{
    /* Flatness test: if control points are close to the line (x0,y0)→(x3,y3) */
    float dx = x3 - x0, dy = y3 - y0;
    float d1 = fabsf((x1 - x3) * dy - (y1 - y3) * dx);
    float d2 = fabsf((x2 - x3) * dy - (y2 - y3) * dx);
    float len2 = dx * dx + dy * dy;

    if (depth > 10 || (d1 + d2) * (d1 + d2) < 0.25f * len2) {
        lvg_canvas_draw_line(canvas, (int)(x0 + 0.5f), (int)(y0 + 0.5f),
                              (int)(x3 + 0.5f), (int)(y3 + 0.5f),
                              color, stroke_width);
        return;
    }

    /* Subdivide at t = 0.5 */
    float m01x = (x0+x1)*0.5f, m01y = (y0+y1)*0.5f;
    float m12x = (x1+x2)*0.5f, m12y = (y1+y2)*0.5f;
    float m23x = (x2+x3)*0.5f, m23y = (y2+y3)*0.5f;
    float m012x = (m01x+m12x)*0.5f, m012y = (m01y+m12y)*0.5f;
    float m123x = (m12x+m23x)*0.5f, m123y = (m12y+m23y)*0.5f;
    float mx = (m012x+m123x)*0.5f, my = (m012y+m123y)*0.5f;

    lvg__bezier_cubic_recursive(canvas, x0, y0, m01x, m01y, m012x, m012y, mx, my,
                                color, stroke_width, depth + 1);
    lvg__bezier_cubic_recursive(canvas, mx, my, m123x, m123y, m23x, m23y, x3, y3,
                                color, stroke_width, depth + 1);
}

void lvg_canvas_draw_bezier_cubic(lvg_canvas_t *canvas,
                                     float x0, float y0,
                                     float x1, float y1,
                                     float x2, float y2,
                                     float x3, float y3,
                                     lvg_color_t color, int stroke_width)
{
    LVG_DISPATCH(draw_bezier_cubic, x0, y0, x1, y1, x2, y2, x3, y3,
                 color, stroke_width);
    if (!canvas) return;
    lvg__bezier_cubic_recursive(canvas, x0, y0, x1, y1, x2, y2, x3, y3,
                                color, stroke_width, 0);
}

void lvg_canvas_draw_bezier_quad(lvg_canvas_t *canvas,
                                    float x0, float y0,
                                    float x1, float y1,
                                    float x2, float y2,
                                    lvg_color_t color, int stroke_width)
{
    LVG_DISPATCH(draw_bezier_quad, x0, y0, x1, y1, x2, y2, color, stroke_width);
    if (!canvas) return;
    /* Convert quadratic to cubic: CP1 = P0 + 2/3*(P1-P0), CP2 = P2 + 2/3*(P1-P2) */
    float cx1 = x0 + (2.0f / 3.0f) * (x1 - x0);
    float cy1 = y0 + (2.0f / 3.0f) * (y1 - y0);
    float cx2 = x2 + (2.0f / 3.0f) * (x1 - x2);
    float cy2 = y2 + (2.0f / 3.0f) * (y1 - y2);
    lvg_canvas_draw_bezier_cubic(canvas, x0, y0, cx1, cy1, cx2, cy2, x2, y2,
                                  color, stroke_width);
}

/* Flatten cubic Bezier into an array of points for filling */
static void lvg__bezier_flatten(float x0, float y0, float x1, float y1,
                                 float x2, float y2, float x3, float y3,
                                 lvg_point_t *pts, int *count, int max_pts,
                                 int depth)
{
    float dx = x3 - x0, dy = y3 - y0;
    float d1 = fabsf((x1 - x3) * dy - (y1 - y3) * dx);
    float d2 = fabsf((x2 - x3) * dy - (y2 - y3) * dx);
    float len2 = dx * dx + dy * dy;

    if (depth > 10 || (d1 + d2) * (d1 + d2) < 0.25f * len2) {
        if (*count < max_pts) {
            pts[*count].x = (int)(x3 + 0.5f);
            pts[*count].y = (int)(y3 + 0.5f);
            (*count)++;
        }
        return;
    }

    float m01x = (x0+x1)*0.5f, m01y = (y0+y1)*0.5f;
    float m12x = (x1+x2)*0.5f, m12y = (y1+y2)*0.5f;
    float m23x = (x2+x3)*0.5f, m23y = (y2+y3)*0.5f;
    float m012x = (m01x+m12x)*0.5f, m012y = (m01y+m12y)*0.5f;
    float m123x = (m12x+m23x)*0.5f, m123y = (m12y+m23y)*0.5f;
    float mx = (m012x+m123x)*0.5f, my = (m012y+m123y)*0.5f;

    lvg__bezier_flatten(x0, y0, m01x, m01y, m012x, m012y, mx, my,
                        pts, count, max_pts, depth + 1);
    lvg__bezier_flatten(mx, my, m123x, m123y, m23x, m23y, x3, y3,
                        pts, count, max_pts, depth + 1);
}

void lvg_canvas_fill_bezier_cubic(lvg_canvas_t *canvas,
                                     float x0, float y0,
                                     float x1, float y1,
                                     float x2, float y2,
                                     float x3, float y3,
                                     lvg_color_t color)
{
    if (!canvas) return;
    lvg_point_t pts[256];
    int count = 0;
    pts[0].x = (int)(x0 + 0.5f);
    pts[0].y = (int)(y0 + 0.5f);
    count = 1;
    lvg__bezier_flatten(x0, y0, x1, y1, x2, y2, x3, y3, pts, &count, 256, 0);
    if (count >= 3)
        lvg_canvas_fill_polygon(canvas, pts, count, color);
}

/* =========================================================================
 * Arcs
 * ========================================================================= */

void lvg_canvas_draw_arc(lvg_canvas_t *canvas,
                           float cx, float cy, float radius,
                           float start_angle, float sweep_angle,
                           lvg_color_t color, int stroke_width,
                           lvg_line_cap_t cap)
{
    LVG_DISPATCH(draw_arc, cx, cy, radius, start_angle, sweep_angle,
                 color, stroke_width, cap);
    if (!canvas || radius <= 0.0f || sweep_angle == 0.0f) return;
    if (stroke_width <= 0) stroke_width = 1;

    /* Approximate the stroked arc as one annular polygon. This avoids seams
     * from independent chord strokes and keeps endpoint caps explicit. */
    int steps = (int)(fabsf(sweep_angle) * radius * 0.2f + 0.5f);
    if (steps < 4) steps = 4;
    if (steps > 127) steps = 127;

    float half_w = (float)stroke_width * 0.5f;
    float outer_r = radius + half_w;
    float inner_r = radius - half_w;
    if (inner_r < 0.0f) inner_r = 0.0f;

    lvg_point_t pts[256];
    int n = 0;
    float dt = sweep_angle / (float)steps;
    for (int i = 0; i <= steps; i++) {
        float t = start_angle + dt * (float)i;
        pts[n].x = (int)(cx + outer_r * cosf(t) + 0.5f);
        pts[n].y = (int)(cy + outer_r * sinf(t) + 0.5f);
        n++;
    }
    for (int i = steps; i >= 0; i--) {
        float t = start_angle + dt * (float)i;
        pts[n].x = (int)(cx + inner_r * cosf(t) + 0.5f);
        pts[n].y = (int)(cy + inner_r * sinf(t) + 0.5f);
        n++;
    }
    lvg_canvas_fill_polygon(canvas, pts, n, color);

    if (cap == LVG_LINE_CAP_ROUND) {
        float a0 = start_angle;
        float a1 = start_angle + sweep_angle;
        lvg_canvas_fill_circle(canvas,
            (int)(cx + radius * cosf(a0) + 0.5f),
            (int)(cy + radius * sinf(a0) + 0.5f),
            (int)(half_w + 0.5f), color);
        lvg_canvas_fill_circle(canvas,
            (int)(cx + radius * cosf(a1) + 0.5f),
            (int)(cy + radius * sinf(a1) + 0.5f),
            (int)(half_w + 0.5f), color);
    } else if (cap == LVG_LINE_CAP_SQUARE) {
        float sign = sweep_angle >= 0.0f ? 1.0f : -1.0f;
        float angles[2] = { start_angle, start_angle + sweep_angle };
        float dirs[2] = { -1.0f, 1.0f };
        for (int ci = 0; ci < 2; ci++) {
            float a = angles[ci];
            float nx = cosf(a), ny = sinf(a);
            float tx = sign * -sinf(a), ty = sign * cosf(a);
            float ex = dirs[ci] * half_w;
            lvg_point_t q[4] = {
                { (int)(cx + (radius + half_w) * nx + 0.5f),
                  (int)(cy + (radius + half_w) * ny + 0.5f) },
                { (int)(cx + inner_r * nx + 0.5f),
                  (int)(cy + inner_r * ny + 0.5f) },
                { (int)(cx + inner_r * nx + tx * ex + 0.5f),
                  (int)(cy + inner_r * ny + ty * ex + 0.5f) },
                { (int)(cx + (radius + half_w) * nx + tx * ex + 0.5f),
                  (int)(cy + (radius + half_w) * ny + ty * ex + 0.5f) },
            };
            lvg_canvas_fill_polygon(canvas, q, 4, color);
        }
    }
}

void lvg_canvas_fill_pie(lvg_canvas_t *canvas,
                            float cx, float cy, float radius,
                            float start_angle, float sweep_angle,
                            lvg_color_t color)
{
    LVG_DISPATCH(fill_pie, cx, cy, radius, start_angle, sweep_angle, color);
    if (!canvas || radius <= 0.0f || sweep_angle == 0.0f) return;

    int steps = (int)(fabsf(sweep_angle) * radius * 0.2f + 0.5f);
    if (steps < 4) steps = 4;
    if (steps > 254) steps = 254;

    lvg_point_t pts[258]; /* center + arc points */
    int n = 0;

    /* Center point */
    pts[n].x = (int)(cx + 0.5f);
    pts[n].y = (int)(cy + 0.5f);
    n++;

    float dt = sweep_angle / (float)steps;
    for (int i = 0; i <= steps; i++) {
        float t = start_angle + dt * (float)i;
        pts[n].x = (int)(cx + radius * cosf(t) + 0.5f);
        pts[n].y = (int)(cy + radius * sinf(t) + 0.5f);
        n++;
    }

    /* Fill as one polygon. A triangle fan redraws shared edges and can leave
     * radial cracks, especially when a retained AA backend handles triangles
     * as separate shapes. */
    lvg_canvas_fill_polygon(canvas, pts, n, color);
}

/* =========================================================================
 * Gradient fills
 * ========================================================================= */

static lvg_color_t lvg__gradient_sample(const lvg_canvas_gradient_t *g, float t)
{
    if (t <= 0.0f || g->stop_count < 1) return g->stops[0].color;
    if (t >= 1.0f || g->stop_count < 2) return g->stops[g->stop_count - 1].color;

    /* Find the two stops surrounding t */
    int i;
    for (i = 0; i < g->stop_count - 1; i++) {
        if (t < g->stops[i + 1].position) break;
    }
    if (i >= g->stop_count - 1) return g->stops[g->stop_count - 1].color;

    float p0 = g->stops[i].position;
    float p1 = g->stops[i + 1].position;
    float f = (p1 > p0) ? (t - p0) / (p1 - p0) : 0.0f;

    uint32_t c0 = g->stops[i].color;
    uint32_t c1 = g->stops[i + 1].color;

    uint32_t a = (uint32_t)(((c0 >> 24) & 0xFF) * (1.0f - f) + ((c1 >> 24) & 0xFF) * f + 0.5f);
    uint32_t r = (uint32_t)(((c0 >> 16) & 0xFF) * (1.0f - f) + ((c1 >> 16) & 0xFF) * f + 0.5f);
    uint32_t g2 = (uint32_t)(((c0 >> 8) & 0xFF) * (1.0f - f) + ((c1 >> 8) & 0xFF) * f + 0.5f);
    uint32_t b = (uint32_t)((c0 & 0xFF) * (1.0f - f) + (c1 & 0xFF) * f + 0.5f);

    return (a << 24) | (r << 16) | (g2 << 8) | b;
}

/*
 * Build a 256-entry LUT covering t in [0, 1] so per-pixel gradient
 * sampling collapses to one indexed load. The stop-count + linear-search
 * cost is paid once per fill_* call instead of once per pixel, which for
 * large fills (scale 2x: >1 Mpx per fill) is effectively free.
 */
#define LVG_GRADIENT_LUT_SIZE 256
static void lvg__gradient_build_lut(const lvg_canvas_gradient_t *g,
                                    lvg_color_t lut[LVG_GRADIENT_LUT_SIZE])
{
    for (int i = 0; i < LVG_GRADIENT_LUT_SIZE; i++) {
        float t = (float)i * (1.0f / (float)(LVG_GRADIENT_LUT_SIZE - 1));
        lut[i] = lvg__gradient_sample(g, t);
    }
}

static inline lvg_color_t lvg__gradient_lut_sample(const lvg_color_t *lut, float t)
{
    if (t <= 0.0f) return lut[0];
    if (t >= 1.0f) return lut[LVG_GRADIENT_LUT_SIZE - 1];
    int idx = (int)(t * (float)(LVG_GRADIENT_LUT_SIZE - 1) + 0.5f);
    return lut[idx];
}

void lvg_canvas_fill_rect_gradient(lvg_canvas_t *canvas,
                                      int x, int y, int w, int h,
                                      const lvg_canvas_gradient_t *grad)
{
    LVG_DISPATCH(fill_rect_gradient, x, y, w, h, grad);
    if (!canvas || !grad || w <= 0 || h <= 0 || grad->stop_count < 1) return;

    lvg_rect_t r = lvg_rect_make(x, y, w, h);
    r = lvg_rect_intersect(&r, &canvas->_clip);
    if (lvg_rect_is_empty(&r)) return;

    int x0 = r.x, y0 = r.y;
    int x1 = r.x + r.width, y1 = r.y + r.height;

    lvg_color_t lut[LVG_GRADIENT_LUT_SIZE];
    lvg__gradient_build_lut(grad, lut);

    if (grad->type == LVG_CANVAS_GRADIENT_LINEAR) {
        /* Precompute per-pixel step so t becomes a running add instead of
         * a MAD+div per pixel. */
        float gdx = grad->x1 - grad->x0;
        float gdy = grad->y1 - grad->y0;
        float len2 = gdx * gdx + gdy * gdy;
        float inv_len2 = (len2 < 0.0001f) ? 0.0f : 1.0f / len2;
        float dt_dx = gdx * inv_len2;
        float dt_dy = gdy * inv_len2;
        float t_row = ((x0 - grad->x0) * gdx + (y0 - grad->y0) * gdy) * inv_len2;
        for (int py = y0; py < y1; py++, t_row += dt_dy) {
            uint32_t *row = canvas->_surface->pixels + py * canvas->_surface->stride;
            float t = t_row;
            for (int px = x0; px < x1; px++, t += dt_dx) {
                lvg_color_t c = lvg__gradient_lut_sample(lut, t);
                row[px] = lvg_px_blend_over(row[px], c);
            }
        }
    } else {
        float inv_r = (grad->r > 0.0f) ? 1.0f / grad->r : 0.0f;
        for (int py = y0; py < y1; py++) {
            uint32_t *row = canvas->_surface->pixels + py * canvas->_surface->stride;
            float dy = (float)py - grad->cy;
            float dy2 = dy * dy;
            for (int px = x0; px < x1; px++) {
                float dx = (float)px - grad->cx;
                float t = sqrtf(dx * dx + dy2) * inv_r;
                lvg_color_t c = lvg__gradient_lut_sample(lut, t);
                row[px] = lvg_px_blend_over(row[px], c);
            }
        }
    }
}

void lvg_canvas_fill_circle_gradient(lvg_canvas_t *canvas,
                                        int cx, int cy, int r,
                                        const lvg_canvas_gradient_t *grad)
{
    LVG_DISPATCH(fill_circle_gradient, cx, cy, r, grad);
    if (!canvas || !grad || r <= 0) return;

    /* Hoist loop-invariant gradient geometry out of the per-pixel hot loop. */
    const bool is_linear = (grad->type == LVG_CANVAS_GRADIENT_LINEAR);
    float gdx = 0.0f, gdy = 0.0f, inv_len2 = 0.0f;
    float dt_dx = 0.0f;
    float inv_r = 0.0f;
    if (is_linear) {
        gdx = grad->x1 - grad->x0;
        gdy = grad->y1 - grad->y0;
        float len2 = gdx * gdx + gdy * gdy;
        inv_len2 = (len2 < 0.0001f) ? 0.0f : 1.0f / len2;
        dt_dx = gdx * inv_len2;
    } else {
        inv_r = (grad->r > 0.0f) ? 1.0f / grad->r : 0.0f;
    }

    lvg_color_t lut[LVG_GRADIENT_LUT_SIZE];
    lvg__gradient_build_lut(grad, lut);

    for (int dy = -r; dy <= r; dy++) {
        int dx_range = lvg__isqrt(r * r - dy * dy);
        int row_y = cy + dy;
        int px_lo = cx - dx_range;
        int px_hi = cx + dx_range;

        if (is_linear) {
            float t = ((px_lo - grad->x0) * gdx + (row_y - grad->y0) * gdy) * inv_len2;
            for (int px = px_lo; px <= px_hi; px++, t += dt_dx) {
                lvg_color_t c = lvg__gradient_lut_sample(lut, t);
                lvg__set_pixel(canvas, px, row_y, c);
            }
        } else {
            float ddy = (float)row_y - grad->cy;
            float ddy2 = ddy * ddy;
            for (int px = px_lo; px <= px_hi; px++) {
                float ddx = (float)px - grad->cx;
                float t = sqrtf(ddx * ddx + ddy2) * inv_r;
                lvg_color_t c = lvg__gradient_lut_sample(lut, t);
                lvg__set_pixel(canvas, px, row_y, c);
            }
        }
    }
}

/* =========================================================================
 * Blend / composite modes
 * ========================================================================= */

static inline uint32_t lvg__clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint32_t)v); }

static inline uint32_t lvg__blend_mode(uint32_t dst, uint32_t src, lvg_canvas_blend_mode_t mode)
{
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0) return dst;

    uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t da = (dst >> 24) & 0xFF;

    uint32_t rr, rg, rb;

    switch (mode) {
    case LVG_CANVAS_BLEND_SRC_OVER:
        return lvg_px_blend_over(dst, src);

    case LVG_CANVAS_BLEND_MULTIPLY:
        rr = (sr * dr + 127) / 255;
        rg = (sg * dg + 127) / 255;
        rb = (sb * db + 127) / 255;
        break;

    case LVG_CANVAS_BLEND_SCREEN:
        rr = sr + dr - (sr * dr + 127) / 255;
        rg = sg + dg - (sg * dg + 127) / 255;
        rb = sb + db - (sb * db + 127) / 255;
        break;

    case LVG_CANVAS_BLEND_OVERLAY:
        rr = (dr < 128) ? (2 * sr * dr + 127) / 255 : 255 - (2 * (255-sr) * (255-dr) + 127) / 255;
        rg = (dg < 128) ? (2 * sg * dg + 127) / 255 : 255 - (2 * (255-sg) * (255-dg) + 127) / 255;
        rb = (db < 128) ? (2 * sb * db + 127) / 255 : 255 - (2 * (255-sb) * (255-db) + 127) / 255;
        break;

    case LVG_CANVAS_BLEND_DARKEN:
        rr = sr < dr ? sr : dr;
        rg = sg < dg ? sg : dg;
        rb = sb < db ? sb : db;
        break;

    case LVG_CANVAS_BLEND_LIGHTEN:
        rr = sr > dr ? sr : dr;
        rg = sg > dg ? sg : dg;
        rb = sb > db ? sb : db;
        break;

    case LVG_CANVAS_BLEND_DIFFERENCE:
        rr = (uint32_t)abs((int)sr - (int)dr);
        rg = (uint32_t)abs((int)sg - (int)dg);
        rb = (uint32_t)abs((int)sb - (int)db);
        break;

    case LVG_CANVAS_BLEND_EXCLUSION:
        rr = sr + dr - (2 * sr * dr + 127) / 255;
        rg = sg + dg - (2 * sg * dg + 127) / 255;
        rb = sb + db - (2 * sb * db + 127) / 255;
        break;

    case LVG_CANVAS_BLEND_PLUS:
        rr = lvg__clamp8((int)sr + (int)dr);
        rg = lvg__clamp8((int)sg + (int)dg);
        rb = lvg__clamp8((int)sb + (int)db);
        break;

    default:
        return lvg_px_blend_over(dst, src);
    }

    /* Apply source alpha */
    uint32_t inv_sa = 255 - sa;
    uint32_t out_r = (rr * sa + dr * inv_sa + 127) / 255;
    uint32_t out_g = (rg * sa + dg * inv_sa + 127) / 255;
    uint32_t out_b = (rb * sa + db * inv_sa + 127) / 255;
    uint32_t out_a = sa + (da * inv_sa + 127) / 255;

    return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

void lvg_canvas_fill_rect_blended(lvg_canvas_t *canvas,
                                     int x, int y, int w, int h,
                                     lvg_color_t color,
                                     lvg_canvas_blend_mode_t mode)
{
    LVG_DISPATCH(fill_rect_blended, x, y, w, h, color, mode);
    if (!canvas || w <= 0 || h <= 0) return;
    if (mode == LVG_CANVAS_BLEND_SRC_OVER) {
        lvg_canvas_fill_rect(canvas, x, y, w, h, color);
        return;
    }

    lvg_rect_t r = lvg_rect_make(x, y, w, h);
    r = lvg_rect_intersect(&r, &canvas->_clip);
    if (lvg_rect_is_empty(&r)) return;

    int x0 = r.x, y0 = r.y;
    int x1 = r.x + r.width, y1 = r.y + r.height;

    for (int py = y0; py < y1; py++) {
        uint32_t *row = canvas->_surface->pixels + py * canvas->_surface->stride;
        for (int px = x0; px < x1; px++) {
            row[px] = lvg__blend_mode(row[px], color, mode);
        }
    }
}

/* =========================================================================
 * Line cap & join styles
 * ========================================================================= */

static void lvg__draw_round_cap(lvg_canvas_t *canvas,
                                  float cx, float cy, float half_w,
                                  lvg_color_t color)
{
    int r = (int)(half_w + 0.5f);
    lvg_canvas_fill_circle(canvas, (int)(cx + 0.5f), (int)(cy + 0.5f), r, color);
}

static void lvg__draw_square_cap(lvg_canvas_t *canvas,
                                   float px, float py, float ux, float uy,
                                   float half_w, lvg_color_t color)
{
    /* Extend the line end by half_w in the line direction */
    float ex = px + ux * half_w;
    float ey = py + uy * half_w;
    float nx = -uy * half_w;
    float ny = ux * half_w;
    int ax = (int)(px + nx + 0.5f), ay = (int)(py + ny + 0.5f);
    int bx = (int)(ex + nx + 0.5f), by = (int)(ey + ny + 0.5f);
    int cx2 = (int)(ex - nx + 0.5f), cy2 = (int)(ey - ny + 0.5f);
    int dx2 = (int)(px - nx + 0.5f), dy2 = (int)(py - ny + 0.5f);
    lvg_canvas_fill_triangle(canvas, ax, ay, bx, by, cx2, cy2, color);
    lvg_canvas_fill_triangle(canvas, ax, ay, cx2, cy2, dx2, dy2, color);
}

void lvg_canvas_draw_styled_polyline(lvg_canvas_t *canvas,
                                        const lvg_pointf_t *points, int count,
                                        lvg_color_t color, float width,
                                        bool closed,
                                        lvg_line_cap_t cap,
                                        lvg_line_join_t join)
{
    LVG_DISPATCH(draw_styled_polyline, points, count, color, width, closed,
                 cap, join);
    if (!canvas || !points || count < 2) return;
    if (width < 0.5f) width = 0.5f;
    float half_w = width * 0.5f;
    float miter_limit = width * 2.0f;

    enum { LVG__STYLED_POLYLINE_STACK = 512 };
    float nx_stack[LVG__STYLED_POLYLINE_STACK];
    float ny_stack[LVG__STYLED_POLYLINE_STACK];
    float *nx = nx_stack, *ny = ny_stack;
    float *nx_heap = NULL;
    if (count > LVG__STYLED_POLYLINE_STACK) {
        nx_heap = (float *)malloc((size_t)count * 2 * sizeof(float));
        if (nx_heap) {
            nx = nx_heap;
            ny = nx_heap + count;
        } else {
            count = LVG__STYLED_POLYLINE_STACK;
        }
    }

    for (int i = 0; i < count; i++) {
        int prev_i, next_i;
        if (closed) {
            prev_i = (i - 1 + count) % count;
            next_i = (i + 1) % count;
        } else {
            prev_i = i > 0 ? i - 1 : i;
            next_i = i < count - 1 ? i + 1 : i;
        }

        float dx1 = points[i].x - points[prev_i].x;
        float dy1 = points[i].y - points[prev_i].y;
        float dx2 = points[next_i].x - points[i].x;
        float dy2 = points[next_i].y - points[i].y;

        float len1 = sqrtf(dx1*dx1 + dy1*dy1);
        float len2 = sqrtf(dx2*dx2 + dy2*dy2);

        float n1x = 0, n1y = 0, n2x = 0, n2y = 0;
        if (len1 > 0.0001f) { n1x = -dy1/len1; n1y = dx1/len1; }
        if (len2 > 0.0001f) { n2x = -dy2/len2; n2y = dx2/len2; }

        float mnx, mny;
        if (i == prev_i || len1 < 0.0001f) {
            mnx = n2x; mny = n2y;
        } else if (i == next_i || len2 < 0.0001f) {
            mnx = n1x; mny = n1y;
        } else {
            if (join == LVG_LINE_JOIN_BEVEL) {
                /* Use incoming segment normal — bevel will be handled in segment drawing */
                mnx = n1x; mny = n1y;
            } else {
                mnx = (n1x + n2x) * 0.5f;
                mny = (n1y + n2y) * 0.5f;
                float ml = sqrtf(mnx*mnx + mny*mny);
                if (ml > 0.0001f) {
                    float dot = mnx*n1x + mny*n1y;
                    if (dot > 0.0001f) {
                        float scale = 1.0f / dot;
                        if (join == LVG_LINE_JOIN_MITER && scale * ml * half_w > miter_limit) {
                            mnx = n1x; mny = n1y;
                        } else if (join == LVG_LINE_JOIN_ROUND) {
                            /* Clamp miter, round join is drawn separately */
                            if (scale * ml * half_w > miter_limit) {
                                mnx = n1x; mny = n1y;
                            } else {
                                mnx *= scale; mny *= scale;
                            }
                        } else {
                            mnx *= scale; mny *= scale;
                        }
                    }
                } else {
                    mnx = n1x; mny = n1y;
                }
            }
        }

        nx[i] = mnx * half_w;
        ny[i] = mny * half_w;
    }

    /* Thin strokes (≤ ~3 px) take an analytic-AA fast path: render the
     * stroke as the union of per-segment oriented rectangles, fed to
     * lvg__fill_quads_aa which does sub-scanline coverage with non-zero
     * winding into a single coverage buffer. Each segment uses its OWN
     * perpendicular normal (independent of neighbours), so there are no
     * miter spikes and no offset-polygon self-intersection artifacts —
     * adjacent segments simply overlap into a clean bevel join via the
     * union, matching the constant-width quality of blend2d/thorvg.
     * Thicker strokes keep the two-triangle path (binary-fill jaggies
     * are far less visible at width and per-segment cost matters more). */
    /* Always use the analytic-AA offset-polygon path. Previously we
     * fell through to a per-segment integer-rounded fill_triangle path
     * for width > 3 to save cycles, but at zoom (where stroke widths
     * commonly exceed 3 px) that produced visibly polygonal,
     * non-AA strokes — segment quads were rounded to int endpoints and
     * the join triangles left aliased seams. The offset-polygon AA
     * path has none of those artifacts and matches blend2d quality. */
    bool use_aa_thin = true;
    if (use_aa_thin) {
        /* Build a SINGLE closed offset polygon — the classic stroker
         * reduction (stroke = fill of offset outline) used by blend2d /
         * cairo / AGG. At smooth-curve vertices we collapse adjacent
         * normals into a MITER point (single offset, perpendicular
         * distance = half_w to both segments) so a finely-flattened
         * Bézier stays smooth instead of accumulating one bevel-step
         * per chord; at sharp turns we fall back to BEVEL (two distinct
         * offset points + a flat join edge) to avoid miter spikes that
         * would extend far beyond the stroke and cause visible
         * artifacts. This both improves visible quality on smooth
         * curves and roughly halves the outline-point count, which is
         * the dominant cost in the AA fill rasteriser. */
        int seg_count = closed ? count : (count - 1);
        if (seg_count <= 0) {
            free(nx_heap);
            return;
        }

        /* Per-segment perpendicular offset, scaled to half_w. */
        enum { LVG__OFFS_STACK = 1024 };
        float ox_stack[LVG__OFFS_STACK], oy_stack[LVG__OFFS_STACK];
        float *ox = ox_stack, *oy = oy_stack;
        float *off_heap = NULL;
        if (seg_count > LVG__OFFS_STACK) {
            off_heap = (float *)malloc((size_t)seg_count * 2 * sizeof(float));
            if (off_heap) { ox = off_heap; oy = off_heap + seg_count; }
            else { free(nx_heap); return; }
        }
        for (int i = 0; i < seg_count; i++) {
            int j = (i + 1) % count;
            float sdx = points[j].x - points[i].x;
            float sdy = points[j].y - points[i].y;
            float slen = sqrtf(sdx * sdx + sdy * sdy);
            if (slen > 1e-4f) {
                ox[i] = -sdy / slen * half_w;
                oy[i] =  sdx / slen * half_w;
            } else {
                ox[i] = oy[i] = 0.0f;
            }
        }

        /* Worst-case outline size: 2 points per side for every interior
         * join (= 4 * (joins)) + 2 endpoint points (one per side, if
         * open). For closed polylines, every vertex is a join. */
        int join_count = closed ? seg_count : (seg_count - 1);
        if (join_count < 0) join_count = 0;
        int max_outline = 2 * (join_count * 2 + 2 + (closed ? 0 : 0));
        if (closed) max_outline = 4 * seg_count; /* every vertex possibly bevels */
        else        max_outline = 4 * seg_count; /* generous upper bound */

        enum { LVG__OUTLINE_STACK = 1024 };
        lvg_pointf_t outline_stack[LVG__OUTLINE_STACK];
        lvg_pointf_t *outline = outline_stack;
        lvg_pointf_t *outline_heap = NULL;
        if (max_outline > LVG__OUTLINE_STACK) {
            outline_heap = (lvg_pointf_t *)malloc(
                (size_t)max_outline * sizeof(*outline));
            if (outline_heap) outline = outline_heap;
            else { free(off_heap); free(nx_heap); return; }
        }

        /* Miter limit: bevel when miter_length/width exceeds miter_limit.
         *   (1 + cos(theta)) >= 2 / miter_limit²
         * where theta is the angle between adjacent unit-normals.
         * The blend2d backend uses BL_STROKE_JOIN_MITER_CLIP which
         * preserves the miter apex (just truncates the spike) rather
         * than falling back to a flat bevel — that's why blend2d shows
         * sharp mane tips where a strict miter-then-bevel renderer
         * cuts them off. We approximate that behaviour by raising the
         * fall-back-to-bevel threshold to miter_limit=10 (= 0.02);
         * sharp content (interior angles ≥ ~6°) keeps its apex, which
         * visually matches blend2d for typical SVG line art. */
        const float inv_hw2 = 1.0f / (half_w * half_w);
        const float miter_thresh = 0.02f;

        int oi = 0;

        /* Forward chain (outer side: + offset). For each segment, emit
         * the START corner if no preceding miter swallowed it, then
         * always emit the END corner of this segment. At the end-of-
         * segment vertex (shared with the next segment's start), pick
         * miter or bevel: miter writes ONE shared offset that both this
         * segment's end and the next segment's start use; bevel writes
         * this segment's end here and lets the next iteration write the
         * next segment's start separately. */
        bool prev_was_miter = false;
        float miter_x = 0.0f, miter_y = 0.0f;  /* miter point at start of i */
        for (int i = 0; i < seg_count; i++) {
            int j = (i + 1) % count;
            /* START of segment i. */
            if (prev_was_miter) {
                outline[oi].x = miter_x;
                outline[oi].y = miter_y;
                oi++;
                prev_was_miter = false;
            } else if (closed || i > 0) {
                /* Bevel: emit segment-i start corner using its own offset. */
                outline[oi].x = points[i].x + ox[i];
                outline[oi].y = points[i].y + oy[i];
                oi++;
            } else {
                /* Open polyline first segment: just emit start. */
                outline[oi].x = points[i].x + ox[i];
                outline[oi].y = points[i].y + oy[i];
                oi++;
            }

            /* END of segment i — but maybe collapse with segment i+1's
             * start via a miter. Only relevant when there IS a next
             * segment (i.e., interior vertex). */
            bool has_next = closed || (i + 1 < seg_count);
            if (!has_next) {
                outline[oi].x = points[j].x + ox[i];
                outline[oi].y = points[j].y + oy[i];
                oi++;
                continue;
            }
            int k = (i + 1) % seg_count;
            int kj = (k + 1) % count;
            float sdx_i = points[j].x  - points[i].x;
            float sdy_i = points[j].y  - points[i].y;
            float sdx_k = points[kj].x - points[k].x;
            float sdy_k = points[kj].y - points[k].y;
            /* +offset chain is on the OUTER (convex) side of the join
             * when the visual turn is CCW in screen y-down coords
             * (cross < 0). Miter the outer chain, bevel the inner
             * chain — the standard cairo/AGG split. The inner side's
             * miter would fold the polygon back on itself and produce
             * spurs after non-zero fill. */
            float cross = sdx_i * sdy_k - sdy_i * sdx_k;
            bool plus_is_outer = (cross < 0.0f);
            /* d = dot of unit normals → cos(angle) between segments. */
            float d = (ox[i] * ox[k] + oy[i] * oy[k]) * inv_hw2;
            float denom = 1.0f + d;
            if (plus_is_outer && denom > miter_thresh) {
                /* Miter on outer side. Clamp magnitude to
                 * miter_limit·half_w (blend2d MITER_CLIP behaviour) so
                 * sharper-than-miter-limit angles don't grow infinite
                 * spikes. miter_thresh = 2/miter_limit² → max |s|² =
                 * 2·hw²/miter_thresh. */
                float sx = (ox[i] + ox[k]) / denom;
                float sy = (oy[i] + oy[k]) / denom;
                float s_mag2 = sx * sx + sy * sy;
                float max_mag2 = 2.0f * half_w * half_w / miter_thresh;
                if (s_mag2 > max_mag2) {
                    float scale = sqrtf(max_mag2 / s_mag2);
                    sx *= scale; sy *= scale;
                }
                outline[oi].x = points[j].x + sx;
                outline[oi].y = points[j].y + sy;
                oi++;
                prev_was_miter = true;
                miter_x = points[j].x + sx;
                miter_y = points[j].y + sy;
            } else {
                /* Inner side or angle below miter-limit: bevel — emit
                 * end-of-i now, next iteration emits start-of-k. */
                outline[oi].x = points[j].x + ox[i];
                outline[oi].y = points[j].y + oy[i];
                oi++;
            }
        }

        /* Backward chain (inner side: - offset). Walk segments in
         * reverse with the same miter / bevel logic. */
        prev_was_miter = false;
        for (int i = seg_count - 1; i >= 0; i--) {
            int j = (i + 1) % count;
            /* START of this iteration = end of segment i (inner). */
            if (prev_was_miter) {
                outline[oi].x = miter_x;
                outline[oi].y = miter_y;
                oi++;
                prev_was_miter = false;
            } else if (closed || i < seg_count - 1) {
                outline[oi].x = points[j].x - ox[i];
                outline[oi].y = points[j].y - oy[i];
                oi++;
            } else {
                outline[oi].x = points[j].x - ox[i];
                outline[oi].y = points[j].y - oy[i];
                oi++;
            }

            /* END of this iteration = start of segment i (inner). The
             * next iteration handles segment (i-1); the shared vertex
             * between i and (i-1) is points[i]. */
            bool has_next = closed || (i > 0);
            if (!has_next) {
                outline[oi].x = points[i].x - ox[i];
                outline[oi].y = points[i].y - oy[i];
                oi++;
                continue;
            }
            int k = closed ? ((i - 1 + seg_count) % seg_count) : (i - 1);
            int kp = (k + 1) % count;
            float sdx_k = points[kp].x - points[k].x;
            float sdy_k = points[kp].y - points[k].y;
            float sdx_i = points[(i+1)%count].x - points[i].x;
            float sdy_i = points[(i+1)%count].y - points[i].y;
            /* -offset chain is OUTER when visual turn is CW (cross > 0
             * in screen y-down). Mirror of the forward-chain logic. */
            float cross = sdx_k * sdy_i - sdy_k * sdx_i;
            bool minus_is_outer = (cross > 0.0f);
            float d = (ox[i] * ox[k] + oy[i] * oy[k]) * inv_hw2;
            float denom = 1.0f + d;
            if (minus_is_outer && denom > miter_thresh) {
                float sx = (ox[i] + ox[k]) / denom;
                float sy = (oy[i] + oy[k]) / denom;
                float s_mag2 = sx * sx + sy * sy;
                float max_mag2 = 2.0f * half_w * half_w / miter_thresh;
                if (s_mag2 > max_mag2) {
                    float scale = sqrtf(max_mag2 / s_mag2);
                    sx *= scale; sy *= scale;
                }
                outline[oi].x = points[i].x - sx;
                outline[oi].y = points[i].y - sy;
                oi++;
                prev_was_miter = true;
                miter_x = points[i].x - sx;
                miter_y = points[i].y - sy;
            } else {
                outline[oi].x = points[i].x - ox[i];
                outline[oi].y = points[i].y - oy[i];
                oi++;
            }
        }

        if (canvas->_aa_mode == LVG_CANVAS_AA_AGG)
            lvg__fill_polygon_dense(canvas, outline, oi, color,
                                  LVG_FILL_RULE_NONZERO);
        else
            lvg__fill_polygon_aa(canvas, outline, oi, color,
                                 LVG_FILL_RULE_NONZERO, NULL, 0);

        /* The AA outline above represents the centreline stroke body. Open
         * polylines still need their requested endpoint caps; otherwise round
         * and square caps silently degrade to butt caps on the AA path. */
        if (!closed) {
            if (cap == LVG_LINE_CAP_ROUND) {
                lvg__draw_round_cap(canvas, points[0].x, points[0].y,
                                    half_w, color);
                lvg__draw_round_cap(canvas, points[count - 1].x,
                                    points[count - 1].y, half_w, color);
            } else if (cap == LVG_LINE_CAP_SQUARE) {
                float dx0 = points[1].x - points[0].x;
                float dy0 = points[1].y - points[0].y;
                float len0 = sqrtf(dx0 * dx0 + dy0 * dy0);
                if (len0 > 0.0001f) {
                    lvg__draw_square_cap(canvas, points[0].x, points[0].y,
                                         -dx0 / len0, -dy0 / len0,
                                         half_w, color);
                }

                float dxe = points[count - 1].x - points[count - 2].x;
                float dye = points[count - 1].y - points[count - 2].y;
                float lene = sqrtf(dxe * dxe + dye * dye);
                if (lene > 0.0001f) {
                    lvg__draw_square_cap(canvas, points[count - 1].x,
                                         points[count - 1].y,
                                         dxe / lene, dye / lene,
                                         half_w, color);
                }
            }
        }

        free(outline_heap);
        free(off_heap);
        free(nx_heap);
        return;
    }

    /* Draw segments.
     * For BEVEL, each segment uses its OWN normal at both endpoints — the
     * per-vertex averaged normal (nx[i]/ny[i]) would skew the segment
     * starting at a middle vertex, because it carries the previous
     * segment's normal. The outer-side gap at each bevel join is closed
     * by a triangle in the bevel-fill pass below. */
    int seg_count = closed ? count : count - 1;
    for (int i = 0; i < seg_count; i++) {
        int j = (i + 1) % count;
        float oax, oay, obx, oby;   /* offset from point i and j */
        if (join == LVG_LINE_JOIN_BEVEL) {
            float sdx = points[j].x - points[i].x;
            float sdy = points[j].y - points[i].y;
            float slen = sqrtf(sdx * sdx + sdy * sdy);
            if (slen > 0.0001f) {
                oax = obx = -sdy / slen * half_w;
                oay = oby =  sdx / slen * half_w;
            } else {
                oax = obx = nx[i]; oay = oby = ny[i];
            }
        } else {
            oax = nx[i]; oay = ny[i];
            obx = nx[j]; oby = ny[j];
        }
        int ax  = (int)(points[i].x + oax + 0.5f);
        int ay  = (int)(points[i].y + oay + 0.5f);
        int bx  = (int)(points[j].x + obx + 0.5f);
        int by  = (int)(points[j].y + oby + 0.5f);
        int cx2 = (int)(points[j].x - obx + 0.5f);
        int cy2 = (int)(points[j].y - oby + 0.5f);
        int dx2 = (int)(points[i].x - oax + 0.5f);
        int dy2 = (int)(points[i].y - oay + 0.5f);
        lvg_canvas_fill_triangle(canvas, ax, ay, bx, by, cx2, cy2, color);
        lvg_canvas_fill_triangle(canvas, ax, ay, cx2, cy2, dx2, dy2, color);
    }

    /* Bevel join fills — close the outer-side gap at each interior vertex.
     * (Thin AA path returned earlier; this only runs for thicker strokes.) */
    if (join == LVG_LINE_JOIN_BEVEL) {
        int start_v = closed ? 0 : 1;
        int end_v   = closed ? count : count - 1;
        for (int i = start_v; i < end_v; i++) {
            int prev = closed ? ((i - 1 + count) % count) : (i - 1);
            int next = closed ? ((i + 1) % count) : (i + 1);

            float idx = points[i].x    - points[prev].x;
            float idy = points[i].y    - points[prev].y;
            float odx = points[next].x - points[i].x;
            float ody = points[next].y - points[i].y;
            float ilen = sqrtf(idx * idx + idy * idy);
            float olen = sqrtf(odx * odx + ody * ody);
            if (ilen < 0.0001f || olen < 0.0001f) continue;

            float nix = -idy / ilen * half_w, niy = idx / ilen * half_w;
            float nox = -ody / olen * half_w, noy = odx / olen * half_w;

            /* Cross of incoming × outgoing picks the outer (convex) side. */
            float cross = idx * ody - idy * odx;
            float sign  = cross < 0.0f ? 1.0f : -1.0f;

            int vx = (int)(points[i].x + 0.5f);
            int vy = (int)(points[i].y + 0.5f);
            int ax = (int)(points[i].x + sign * nix + 0.5f);
            int ay = (int)(points[i].y + sign * niy + 0.5f);
            int bx = (int)(points[i].x + sign * nox + 0.5f);
            int by = (int)(points[i].y + sign * noy + 0.5f);
            lvg_canvas_fill_triangle(canvas, vx, vy, ax, ay, bx, by, color);
        }
    }

    /* Draw round joins */
    if (join == LVG_LINE_JOIN_ROUND) {
        int start = closed ? 0 : 1;
        int end = closed ? count : count - 1;
        for (int i = start; i < end; i++) {
            lvg__draw_round_cap(canvas, points[i].x, points[i].y, half_w, color);
        }
    }

    /* Draw end caps (only for open polylines) */
    if (!closed) {
        if (cap == LVG_LINE_CAP_ROUND) {
            lvg__draw_round_cap(canvas, points[0].x, points[0].y, half_w, color);
            lvg__draw_round_cap(canvas, points[count-1].x, points[count-1].y, half_w, color);
        } else if (cap == LVG_LINE_CAP_SQUARE) {
            /* Start cap */
            float dx0 = points[1].x - points[0].x;
            float dy0 = points[1].y - points[0].y;
            float len0 = sqrtf(dx0*dx0 + dy0*dy0);
            if (len0 > 0.0001f) {
                lvg__draw_square_cap(canvas, points[0].x, points[0].y,
                                     -dx0/len0, -dy0/len0, half_w, color);
            }
            /* End cap */
            float dxe = points[count-1].x - points[count-2].x;
            float dye = points[count-1].y - points[count-2].y;
            float lene = sqrtf(dxe*dxe + dye*dye);
            if (lene > 0.0001f) {
                lvg__draw_square_cap(canvas, points[count-1].x, points[count-1].y,
                                     dxe/lene, dye/lene, half_w, color);
            }
        }
    }

    free(nx_heap);
}

/* =========================================================================
 * Fill rules (even-odd and non-zero winding for arbitrary polygons)
 * ========================================================================= */

void lvg_canvas_fill_polygonf_ex(lvg_canvas_t *canvas,
                                    const lvg_pointf_t *points, int count,
                                    lvg_color_t color,
                                    lvg_fill_rule_t rule)
{
    if (!canvas || !points || count < 3) return;
    /* Backend present: round to int and dispatch to backend's fill, since
     * backend implementations expose their own AA. Sub-pixel precision is
     * lost on the cast but is recovered by the backend's internal
     * rasteriser if it accepts float input via its native API.) */
    if (canvas->_ops && canvas->_ops->fill_polygon_ex) {
        enum { LVG__POLYF_STACK = 256 };
        lvg_point_t ipts_stack[LVG__POLYF_STACK];
        lvg_point_t *ipts = ipts_stack;
        lvg_point_t *ipts_heap = NULL;
        if (count > LVG__POLYF_STACK) {
            ipts_heap = (lvg_point_t *)malloc(
                (size_t)count * sizeof(*ipts_heap));
            if (ipts_heap) ipts = ipts_heap;
            else count = LVG__POLYF_STACK;
        }
        for (int i = 0; i < count; i++) {
            ipts[i].x = (int)lroundf(points[i].x);
            ipts[i].y = (int)lroundf(points[i].y);
        }
        canvas->_ops->fill_polygon_ex(canvas, ipts, count, color, rule);
        free(ipts_heap);
        return;
    }
    /* Software path: dispatch by AA mode. */
    if (canvas->_aa_mode == LVG_CANVAS_AA_AGG)
        lvg__fill_polygon_dense(canvas, points, count, color, rule);
    else
        lvg__fill_polygon_aa(canvas, points, count, color, rule, NULL, 0);
}

void lvg_canvas_fill_polygonsf_ex(lvg_canvas_t *canvas,
                                     const lvg_pointf_t *points, int count,
                                     const int *contour_lengths, int n_contours,
                                     lvg_color_t color,
                                     lvg_fill_rule_t rule)
{
    if (!canvas || !points || count < 3) return;
    /* Validate the contour partition: lengths must be positive and sum to
     * exactly `count`, otherwise the edge builder would read past `points`.
     * On any mismatch fall back to treating the whole array as one contour. */
    if (contour_lengths && n_contours > 0) {
        long total = 0;
        int valid = 1;
        for (int i = 0; i < n_contours; i++) {
            if (contour_lengths[i] <= 0) { valid = 0; break; }
            total += contour_lengths[i];
        }
        if (!valid || total != count) {
            contour_lengths = NULL;
            n_contours = 0;
        }
    }
    /* Multi-contour fill always uses the analytic-AA software rasteriser so a
     * single edge table spans every contour (required for correct hole/winding
     * evaluation). The integer backend / AGG paths only fill one contour at a
     * time, which fills holes solid. */
    lvg__fill_polygon_aa(canvas, points, count, color, rule,
                         contour_lengths, n_contours);
}

void lvg_canvas_fill_polygon_ex(lvg_canvas_t *canvas,
                                   const lvg_point_t *points, int count,
                                   lvg_color_t color,
                                   lvg_fill_rule_t rule)
{
    LVG_DISPATCH(fill_polygon_ex, points, count, color, rule);
    if (!canvas || !points || count < 3) return;

    /* Find y range */
    int ymin = points[0].y, ymax = points[0].y;
    for (int i = 1; i < count; i++) {
        if (points[i].y < ymin) ymin = points[i].y;
        if (points[i].y > ymax) ymax = points[i].y;
    }

    /* Per-scanline crossing buffers. A scanline can cross at most every
     * polygon edge once, so size for `count`. Stack for small polys, heap
     * for huge ones (silent truncation at 256 used to corrupt complex
     * SVG fills). */
    enum { LVG__FILL_POLY_STACK = 256 };
    int x_buf_stack[LVG__FILL_POLY_STACK];
    int w_buf_stack[LVG__FILL_POLY_STACK];
    int *x_buf = x_buf_stack;
    int *w_buf = w_buf_stack;
    int *xw_heap = NULL;
    int x_cap = LVG__FILL_POLY_STACK;
    if (count > LVG__FILL_POLY_STACK) {
        xw_heap = (int *)malloc((size_t)count * 2 * sizeof(int));
        if (xw_heap) {
            x_buf = xw_heap;
            w_buf = xw_heap + count;
            x_cap = count;
        }
    }

    /* Scanline rasterization */
    for (int y = ymin; y <= ymax; y++) {
        int x_count = 0;

        for (int i = 0; i < count && x_count < x_cap; i++) {
            int j = (i + 1) % count;
            int y0 = points[i].y, y1 = points[j].y;
            if (y0 == y1) continue;

            int x0 = points[i].x, x1 = points[j].x;

            /* Check if scanline intersects this edge */
            if ((y0 <= y && y < y1) || (y1 <= y && y < y0)) {
                int ix = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
                x_buf[x_count] = ix;
                w_buf[x_count] = (y0 < y1) ? 1 : -1;
                x_count++;
            }
        }

        /* Sort x intersections (insertion sort for small count) */
        for (int i = 1; i < x_count; i++) {
            int key = x_buf[i], kw = w_buf[i];
            int j = i - 1;
            while (j >= 0 && x_buf[j] > key) {
                x_buf[j + 1] = x_buf[j];
                w_buf[j + 1] = w_buf[j];
                j--;
            }
            x_buf[j + 1] = key;
            w_buf[j + 1] = kw;
        }

        /* Fill spans based on fill rule */
        if (rule == LVG_FILL_RULE_EVENODD) {
            /* Fill between pairs of crossings */
            for (int i = 0; i + 1 < x_count; i += 2) {
                lvg_canvas_fill_rect(canvas, x_buf[i], y,
                                      x_buf[i + 1] - x_buf[i] + 1, 1, color);
            }
        } else {
            /* Non-zero winding rule */
            int winding = 0;
            int span_start = 0;
            for (int i = 0; i < x_count; i++) {
                int prev_winding = winding;
                winding += w_buf[i];
                if (prev_winding == 0 && winding != 0) {
                    /* Entering filled region */
                    span_start = x_buf[i];
                } else if (prev_winding != 0 && winding == 0) {
                    /* Leaving filled region — fill from entry to here */
                    lvg_canvas_fill_rect(canvas, span_start, y,
                                          x_buf[i] - span_start + 1, 1, color);
                }
            }
        }
    }

    free(xw_heap);
}
