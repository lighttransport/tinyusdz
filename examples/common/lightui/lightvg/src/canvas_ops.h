/*
 * lightvg/src/canvas_ops.h — internal pluggable-backend vtable for lvg_canvas_t.
 *
 * This header is NOT part of the public lightui API. Backend implementations
 * (blend2d, thorvg, …) fill in a static const lvg_canvas_ops_t and attach it
 * to a canvas via lvg_canvas_t::ops.
 *
 * Design:
 *   - Every function pointer slot is OPTIONAL. A NULL slot means "fall back
 *     to the built-in software body", which lets backends ship partial
 *     coverage without blocking the demo.
 *   - The public lvg_canvas_* dispatchers in lightvg/src/canvas.c check
 *     cv->ops-><slot>; if non-NULL they delegate, else they run the
 *     software body inline.
 *   - The software backend sets cv->ops = NULL entirely (zero-cost fast
 *     path — a single NULL-check branch).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTVG_SRC_CANVAS_OPS_H
#define LIGHTVG_SRC_CANVAS_OPS_H

#include <lightvg/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lvg_canvas_ops {
    /* ---- Lifecycle ------------------------------------------------------ */
    /* destroy: free backend_state. Called from lvg_canvas_destroy. */
    void (*destroy)(lvg_canvas_t *canvas);

    /* flush: finalise any deferred drawing into cv->surface->pixels.
     * Called by the user via lvg_canvas_flush(). Called automatically at
     * the start of lvg_canvas_destroy() for retained-mode backends. */
    void (*flush)(lvg_canvas_t *canvas);

    /* set_clip: notify backend the clip rect changed. NULL = full surface. */
    void (*set_clip)(lvg_canvas_t *canvas, const lvg_rect_t *clip);

    /* ---- Primitives (NULL => software fallback) ------------------------- */
    void (*clear)(lvg_canvas_t *, lvg_color_t);

    void (*fill_rect)(lvg_canvas_t *, int x, int y, int w, int h,
                      lvg_color_t);
    void (*stroke_rect)(lvg_canvas_t *, int x, int y, int w, int h,
                        lvg_color_t, int stroke_width);

    void (*fill_circle)(lvg_canvas_t *, int cx, int cy, int r, lvg_color_t);
    void (*stroke_circle)(lvg_canvas_t *, int cx, int cy, int r,
                          lvg_color_t, int stroke_width);

    void (*fill_ellipse)(lvg_canvas_t *, int cx, int cy, int rx, int ry,
                         lvg_color_t);
    void (*stroke_ellipse)(lvg_canvas_t *, int cx, int cy, int rx, int ry,
                           lvg_color_t, int stroke_width);

    void (*fill_rounded_rect)(lvg_canvas_t *, int x, int y, int w, int h,
                              int radius, lvg_color_t);
    void (*stroke_rounded_rect)(lvg_canvas_t *, int x, int y, int w, int h,
                                int radius, lvg_color_t, int stroke_width);

    void (*fill_triangle)(lvg_canvas_t *, int x0, int y0, int x1, int y1,
                          int x2, int y2, lvg_color_t);
    void (*fill_polygon)(lvg_canvas_t *, const lvg_point_t *, int count,
                         lvg_color_t);
    void (*stroke_polygon)(lvg_canvas_t *, const lvg_point_t *, int count,
                           lvg_color_t, int stroke_width);
    void (*fill_polygon_ex)(lvg_canvas_t *, const lvg_point_t *, int count,
                            lvg_color_t, lvg_fill_rule_t);

    void (*draw_line)(lvg_canvas_t *, int x0, int y0, int x1, int y1,
                      lvg_color_t, int stroke_width);
    void (*draw_polyline)(lvg_canvas_t *, const lvg_point_t *, int count,
                          lvg_color_t, int stroke_width);

    void (*draw_line_dashed)(lvg_canvas_t *, int x0, int y0, int x1, int y1,
                             lvg_color_t, int stroke_width,
                             const lvg_dash_t *);
    void (*draw_polyline_dashed)(lvg_canvas_t *, const lvg_point_t *,
                                 int count, lvg_color_t, int stroke_width,
                                 const lvg_dash_t *);

    void (*draw_line_aa)(lvg_canvas_t *, float x0, float y0,
                         float x1, float y1, lvg_color_t);

    void (*draw_thick_polyline)(lvg_canvas_t *, const lvg_pointf_t *,
                                int count, lvg_color_t, float width,
                                bool closed);
    void (*draw_styled_polyline)(lvg_canvas_t *, const lvg_pointf_t *,
                                 int count, lvg_color_t, float width,
                                 bool closed,
                                 lvg_line_cap_t, lvg_line_join_t);

    void (*draw_arrow)(lvg_canvas_t *, int x0, int y0, int x1, int y1,
                       lvg_color_t, int stroke_width,
                       lvg_arrow_style_t head, lvg_arrow_style_t tail,
                       int head_size);

    void (*fill_rect_hatched)(lvg_canvas_t *, int x, int y, int w, int h,
                              lvg_color_t bg, lvg_color_t fg,
                              lvg_hatch_style_t, int spacing, int line_width);

    void (*draw_bezier_cubic)(lvg_canvas_t *,
                              float x0, float y0, float x1, float y1,
                              float x2, float y2, float x3, float y3,
                              lvg_color_t, int stroke_width);
    void (*draw_bezier_quad)(lvg_canvas_t *,
                             float x0, float y0, float x1, float y1,
                             float x2, float y2,
                             lvg_color_t, int stroke_width);

    void (*draw_arc)(lvg_canvas_t *, float cx, float cy, float radius,
                     float start_angle, float sweep_angle,
                     lvg_color_t, int stroke_width, lvg_line_cap_t cap);
    void (*fill_pie)(lvg_canvas_t *, float cx, float cy, float radius,
                     float start_angle, float sweep_angle, lvg_color_t);

    void (*fill_rect_gradient)(lvg_canvas_t *, int x, int y, int w, int h,
                               const lvg_canvas_gradient_t *);
    void (*fill_circle_gradient)(lvg_canvas_t *, int cx, int cy, int r,
                                 const lvg_canvas_gradient_t *);

    void (*fill_rect_blended)(lvg_canvas_t *, int x, int y, int w, int h,
                              lvg_color_t, lvg_canvas_blend_mode_t);
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTVG_SRC_CANVAS_OPS_H */
