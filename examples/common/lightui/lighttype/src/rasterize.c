/*
 * src/fonts/rasterize.c — Analytic-area scanline glyph rasterizer
 *
 * Non-zero winding rule. Handles quadratic (TrueType) and cubic (CFF)
 * Bezier curves. Cubic curves are flattened via de Casteljau subdivision.
 *
 * Algorithm:
 *   1. Scale outline points to pixel coordinates
 *   2. Walk contours, decompose curves into monotone edges
 *   3. For each scanline, accumulate signed coverage deltas per pixel
 *   4. Prefix-sum deltas to get 0-255 coverage values
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lighttype/rasterize.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Edge representation
 * ========================================================================= */

typedef struct {
    float x0, y0, x1, y1;  /* directed edge: y0 < y1 always */
    int   dir;              /* +1 upward (in font coords), -1 downward */
} rast_edge_t;

typedef struct {
    rast_edge_t *edges;
    int          count, cap;
} edge_list_t;

static void edge_list_init(edge_list_t *el)
{
    memset(el, 0, sizeof(*el));
}

static void edge_list_free(edge_list_t *el)
{
    free(el->edges);
}

static void edge_list_add(edge_list_t *el, float x0, float y0,
                           float x1, float y1)
{
    if (y0 == y1) return; /* horizontal — no coverage contribution */

    if (el->count >= el->cap) {
        int nc = el->cap ? el->cap * 2 : 128;
        rast_edge_t *ne = (rast_edge_t *)realloc(el->edges,
                                                   (size_t)nc * sizeof(*ne));
        if (!ne) return;
        el->edges = ne;
        el->cap = nc;
    }

    rast_edge_t *e = &el->edges[el->count++];
    if (y0 < y1) {
        e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1;
        e->dir = 1;
    } else {
        e->x0 = x1; e->y0 = y1; e->x1 = x0; e->y1 = y0;
        e->dir = -1;
    }
}

/* =========================================================================
 * Curve flattening — de Casteljau subdivision
 * ========================================================================= */

#define FLATTEN_TOLERANCE 0.25f

/* Flatten a quadratic Bezier into line segments and add edges */
static void flatten_quad(edge_list_t *el,
                          float x0, float y0,
                          float x1, float y1,
                          float x2, float y2,
                          int depth)
{
    if (depth > 16) {
        edge_list_add(el, x0, y0, x2, y2);
        return;
    }

    /* Midpoint flatness test */
    float mx = (x0 + 2.0f * x1 + x2) * 0.25f;
    float my = (y0 + 2.0f * y1 + y2) * 0.25f;
    float ax = (x0 + x2) * 0.5f;
    float ay = (y0 + y2) * 0.5f;
    float dx = mx - ax;
    float dy = my - ay;
    if (dx * dx + dy * dy <= FLATTEN_TOLERANCE * FLATTEN_TOLERANCE) {
        edge_list_add(el, x0, y0, x2, y2);
        return;
    }

    /* de Casteljau split */
    float x01 = (x0 + x1) * 0.5f;
    float y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f;
    float y12 = (y1 + y2) * 0.5f;
    float xm  = (x01 + x12) * 0.5f;
    float ym  = (y01 + y12) * 0.5f;

    flatten_quad(el, x0, y0, x01, y01, xm, ym, depth + 1);
    flatten_quad(el, xm, ym, x12, y12, x2, y2, depth + 1);
}

/* Flatten a cubic Bezier into line segments and add edges */
static void flatten_cubic(edge_list_t *el,
                           float x0, float y0,
                           float x1, float y1,
                           float x2, float y2,
                           float x3, float y3,
                           int depth)
{
    if (depth > 16) {
        edge_list_add(el, x0, y0, x3, y3);
        return;
    }

    /* Flatness test: max distance of control points from the chord */
    float dx = x3 - x0, dy = y3 - y0;
    float d2 = fabsf((x1 - x3) * dy - (y1 - y3) * dx);
    float d3 = fabsf((x2 - x3) * dy - (y2 - y3) * dx);
    float d = d2 + d3;
    float chord = dx * dx + dy * dy;

    if (d * d <= FLATTEN_TOLERANCE * FLATTEN_TOLERANCE * chord) {
        edge_list_add(el, x0, y0, x3, y3);
        return;
    }

    /* de Casteljau split */
    float x01  = (x0 + x1) * 0.5f;
    float y01  = (y0 + y1) * 0.5f;
    float x12  = (x1 + x2) * 0.5f;
    float y12  = (y1 + y2) * 0.5f;
    float x23  = (x2 + x3) * 0.5f;
    float y23  = (y2 + y3) * 0.5f;
    float x012 = (x01 + x12) * 0.5f;
    float y012 = (y01 + y12) * 0.5f;
    float x123 = (x12 + x23) * 0.5f;
    float y123 = (y12 + y23) * 0.5f;
    float xm   = (x012 + x123) * 0.5f;
    float ym   = (y012 + y123) * 0.5f;

    flatten_cubic(el, x0, y0, x01, y01, x012, y012, xm, ym, depth + 1);
    flatten_cubic(el, xm, ym, x123, y123, x23, y23, x3, y3, depth + 1);
}

/* =========================================================================
 * Outline decomposition — walk contours and emit edges
 * ========================================================================= */

static void decompose_outline(const ttf_outline_t *outline, float scale,
                                float off_x, float off_y, edge_list_t *el)
{
    int pt_idx = 0;
    for (int c = 0; c < outline->num_contours; c++) {
        int end = outline->contour_ends[c];
        int start = pt_idx;
        int npts = end - start + 1;

        if (npts < 2) {
            pt_idx = end + 1;
            continue;
        }

        /* Scaled points for this contour */
        float first_x = 0, first_y = 0;
        float cur_x = 0, cur_y = 0;
        int have_first = 0;

        /* Walk the contour points, emitting line/curve segments.
         * For TrueType outlines (on_curve = 0/1):
         *   - on-curve(1): endpoint
         *   - off-curve(0): quadratic control point
         *   - Two consecutive off-curve: implied on-curve midpoint
         *
         * For CFF outlines (on_curve = 1/2):
         *   - on-curve(1): endpoint
         *   - cubic control(2): cubic Bezier control point
         *   - Cubics always come in pairs followed by on-curve
         */

        /* Helper: get scaled point */
        #define SP_X(i) (outline->points[i].x * scale + off_x)
        #define SP_Y(i) (-(outline->points[i].y * scale) + off_y)
        #define SP_ON(i) (outline->points[i].on_curve)

        /* For TrueType: if first point is off-curve, we need special handling */
        if (SP_ON(start) == 1) {
            first_x = cur_x = SP_X(start);
            first_y = cur_y = SP_Y(start);
            have_first = 1;
            pt_idx = start + 1;
        } else if (SP_ON(start) == 2) {
            /* CFF: cubic control — shouldn't start here but handle gracefully */
            first_x = cur_x = SP_X(start);
            first_y = cur_y = SP_Y(start);
            have_first = 1;
            pt_idx = start + 1;
        } else {
            /* TrueType: first point is off-curve.
             * If last point is on-curve, start from there.
             * If both are off-curve, start from their midpoint. */
            if (SP_ON(end) == 1) {
                first_x = cur_x = SP_X(end);
                first_y = cur_y = SP_Y(end);
                have_first = 1;
                pt_idx = start;
                /* Don't skip the first off-curve point */
            } else {
                first_x = cur_x = (SP_X(start) + SP_X(end)) * 0.5f;
                first_y = cur_y = (SP_Y(start) + SP_Y(end)) * 0.5f;
                have_first = 1;
                pt_idx = start;
            }
        }

        if (!have_first) {
            pt_idx = end + 1;
            continue;
        }

        while (pt_idx <= end) {
            int on = SP_ON(pt_idx);
            if (on == 1) {
                /* On-curve point: straight line from current */
                float px = SP_X(pt_idx);
                float py = SP_Y(pt_idx);
                edge_list_add(el, cur_x, cur_y, px, py);
                cur_x = px;
                cur_y = py;
                pt_idx++;
            } else if (on == 0) {
                /* Quadratic control point */
                float cx = SP_X(pt_idx);
                float cy = SP_Y(pt_idx);
                float nx, ny;

                /* Check next point (wrap to start if past end) */
                int next = pt_idx + 1;
                int wrapped = (next > end);
                if (wrapped) next = start;

                if (SP_ON(next) == 1) {
                    /* Next is on-curve: simple quad */
                    nx = SP_X(next);
                    ny = SP_Y(next);
                    flatten_quad(el, cur_x, cur_y, cx, cy, nx, ny, 0);
                    cur_x = nx;
                    cur_y = ny;
                    /* If we wrapped around, we're done with this contour */
                    pt_idx = wrapped ? end + 1 : next + 1;
                } else {
                    /* Next is also off-curve: implied midpoint */
                    nx = (cx + SP_X(next)) * 0.5f;
                    ny = (cy + SP_Y(next)) * 0.5f;
                    flatten_quad(el, cur_x, cur_y, cx, cy, nx, ny, 0);
                    cur_x = nx;
                    cur_y = ny;
                    /* If we wrapped around, we still consumed our last point */
                    if (wrapped)
                        pt_idx = end + 1;
                    else
                        pt_idx++;
                }
            } else if (on == 2) {
                /* Cubic control point (CFF) — expect two cubics then on-curve */
                float c1x = SP_X(pt_idx);
                float c1y = SP_Y(pt_idx);
                int n1 = pt_idx + 1;
                int n2 = pt_idx + 2;
                if (n1 > end) n1 = start + (n1 - end - 1);
                if (n2 > end) n2 = start + (n2 - end - 1);

                float c2x = SP_X(n1);
                float c2y = SP_Y(n1);
                float ex = SP_X(n2);
                float ey = SP_Y(n2);

                flatten_cubic(el, cur_x, cur_y, c1x, c1y, c2x, c2y, ex, ey, 0);
                cur_x = ex;
                cur_y = ey;
                pt_idx += 3;
                /* If we went past end, we're done */
                if (pt_idx > end + 1) pt_idx = end + 1;
            } else {
                pt_idx++;
            }
        }

        /* Close the contour */
        if (cur_x != first_x || cur_y != first_y)
            edge_list_add(el, cur_x, cur_y, first_x, first_y);

        #undef SP_X
        #undef SP_Y
        #undef SP_ON

        pt_idx = end + 1;
    }
}

/* =========================================================================
 * Scanline rasterization — coverage accumulation
 * ========================================================================= */

static int edge_cmp_y(const void *a, const void *b)
{
    const rast_edge_t *ea = (const rast_edge_t *)a;
    const rast_edge_t *eb = (const rast_edge_t *)b;
    if (ea->y0 < eb->y0) return -1;
    if (ea->y0 > eb->y0) return  1;
    return 0;
}

static void rasterize_edges(const edge_list_t *el, int w, int h,
                              uint8_t *pixels)
{
    if (el->count == 0) return;

    /* Sort edges by y_min */
    rast_edge_t *edges = (rast_edge_t *)malloc((size_t)el->count * sizeof(rast_edge_t));
    if (!edges) return;
    memcpy(edges, el->edges, (size_t)el->count * sizeof(rast_edge_t));
    qsort(edges, (size_t)el->count, sizeof(rast_edge_t), edge_cmp_y);

    /* Allocate a row of coverage deltas (+2 for right-side overflow) */
    float *deltas = (float *)calloc((size_t)(w + 2), sizeof(float));
    if (!deltas) { free(edges); return; }

    int ei_start = 0; /* first edge that might still be active */

    for (int y = 0; y < h; y++) {
        float y_top = (float)y;
        float y_bot = (float)(y + 1);

        memset(deltas, 0, (size_t)(w + 2) * sizeof(float));

        for (int e = ei_start; e < el->count; e++) {
            const rast_edge_t *edge = &edges[e];

            /* Advance start pointer past finished edges */
            if (edge->y1 <= y_top) {
                if (e == ei_start) ei_start++;
                continue;
            }
            /* Edges are sorted by y0; all remaining start after this row */
            if (edge->y0 >= y_bot) break;

            /* Clip edge to this scanline row */
            float ey0 = edge->y0 < y_top ? y_top : edge->y0;
            float ey1 = edge->y1 > y_bot ? y_bot : edge->y1;
            float dy  = ey1 - ey0;
            if (dy <= 0.0f) continue;

            /* Compute x at clipped y positions using linear interpolation */
            float edge_dy = edge->y1 - edge->y0;
            float t0 = (ey0 - edge->y0) / edge_dy;
            float t1 = (ey1 - edge->y0) / edge_dy;
            float ex0 = edge->x0 + t0 * (edge->x1 - edge->x0);
            float ex1 = edge->x0 + t1 * (edge->x1 - edge->x0);

            int dir = edge->dir;

            /* Which pixel columns does this edge segment span? */
            float x_lo = ex0 < ex1 ? ex0 : ex1;
            float x_hi = ex0 > ex1 ? ex0 : ex1;
            int ixl = (int)floorf(x_lo);
            int ixr = (int)floorf(x_hi);

            if (ixl == ixr) {
                /* Edge is within a single pixel column */
                if (ixl >= 0 && ixl < w) {
                    float xmid = (ex0 + ex1) * 0.5f;
                    float sign = (float)dir * dy;
                    float area = (xmid - (float)ixl) * sign;
                    deltas[ixl]     += sign - area;
                    deltas[ixl + 1] += area;
                } else if (ixr < 0) {
                    /* Entirely left of bitmap — full winding to pixel 0 */
                    deltas[0] += (float)dir * dy;
                }
            } else {
                /* Edge spans multiple pixel columns.
                 * Walk column boundaries in the direction the edge actually
                 * traverses (top-to-bottom in y, following dxdy in x). */
                float dxdy = (ex1 - ex0) / dy;

                int col = (int)floorf(ex0);
                /* When going left and exactly on a column boundary,
                 * the edge is about to enter the column to the left. */
                if (dxdy < 0.0f && ex0 == (float)col)
                    col--;

                float y_prev = ey0;
                float x_prev = ex0;
                int max_iter = ixr - ixl + 2;

                for (int iter = 0; iter <= max_iter; iter++) {
                    /* Find the y where the edge exits this column */
                    float y_exit;
                    int next_col;

                    if (dxdy > 0.0f) {
                        /* Going right: exits at right boundary (col + 1) */
                        float bnd = (float)(col + 1);
                        y_exit   = ey0 + (bnd - ex0) / dxdy;
                        next_col = col + 1;
                    } else if (dxdy < 0.0f) {
                        /* Going left: exits at left boundary (col) */
                        float bnd = (float)col;
                        y_exit   = ey0 + (bnd - ex0) / dxdy;
                        next_col = col - 1;
                    } else {
                        /* Vertical edge — stays in this column */
                        y_exit   = ey1;
                        next_col = col;
                    }

                    if (y_exit > ey1) y_exit = ey1;

                    float sub_dy = y_exit - y_prev;
                    if (sub_dy > 0.0f) {
                        float x_exit = x_prev + dxdy * sub_dy;
                        if (col >= 0 && col < w) {
                            float xmid = (x_prev + x_exit) * 0.5f;
                            float sign = (float)dir * sub_dy;
                            float area = (xmid - (float)col) * sign;
                            deltas[col]     += sign - area;
                            deltas[col + 1] += area;
                        } else if (col < 0) {
                            deltas[0] += (float)dir * sub_dy;
                        }
                        y_prev = y_exit;
                        x_prev = x_exit;
                    }

                    if (y_exit >= ey1) break;
                    col = next_col;
                }
            }
        }

        /* Prefix-sum the deltas and convert to 0-255 coverage */
        float accum = 0.0f;
        uint8_t *row = pixels + y * w;
        for (int x = 0; x < w; x++) {
            accum += deltas[x];
            float cov = fabsf(accum);
            if (cov > 1.0f) cov = 1.0f;
            row[x] = (uint8_t)(cov * 255.0f + 0.5f);
        }
    }

    free(deltas);
    free(edges);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

int rast_glyph(const ttf_outline_t *outline, float scale, rast_bitmap_t *bm)
{
    if (!outline || outline->num_points == 0 || outline->num_contours == 0) {
        memset(bm, 0, sizeof(*bm));
        return -1;
    }

    /* Compute scaled bounding box.
     * No extra padding needed (matching FreeType's tight bbox). */
    float x_min = (float)outline->x_min * scale;
    float y_min = -(float)outline->y_max * scale;  /* flip Y */
    float x_max = (float)outline->x_max * scale;
    float y_max = -(float)outline->y_min * scale;  /* flip Y */

    int ix0 = (int)floorf(x_min);
    int iy0 = (int)floorf(y_min);
    int ix1 = (int)ceilf(x_max);
    int iy1 = (int)ceilf(y_max);

    int w = ix1 - ix0;
    int h = iy1 - iy0;
    if (w <= 0 || h <= 0) {
        memset(bm, 0, sizeof(*bm));
        return -1;
    }

    /* Decompose outline into edges */
    edge_list_t el;
    edge_list_init(&el);
    decompose_outline(outline, scale, -(float)ix0, -(float)iy0, &el);

    if (el.count == 0) {
        edge_list_free(&el);
        memset(bm, 0, sizeof(*bm));
        return -1;
    }

    /* Allocate pixel buffer */
    bm->pixels = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
    if (!bm->pixels) {
        edge_list_free(&el);
        memset(bm, 0, sizeof(*bm));
        return -1;
    }

    bm->width     = w;
    bm->height    = h;
    bm->bearing_x = ix0;
    bm->bearing_y = -iy0; /* positive = above baseline */

    /* Rasterize */
    rasterize_edges(&el, w, h, bm->pixels);

    edge_list_free(&el);
    return 0;
}
