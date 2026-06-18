/*
 * lightrt_c.c — clean, self-contained C11 port of LightRT's generic BVH +
 * custom-primitive ray intersection.
 *
 * This is a pure C11 implementation of the lightrt_c.h API. It has NO
 * dependency on the C++ library or its headers: it builds and traverses its
 * own BVH.
 *
 * Design:
 *   - The BVH is single precision (broad phase). Build uses a binned Surface
 *     Area Heuristic (16 bins per axis).
 *   - Traversal is stack-based with front-to-back child ordering, calling the
 *     user's fp64 intersection callback per candidate primitive. The
 *     authoritative fp64 ray and best hit are kept in fp64, so an analytic
 *     surface can be solved at full double precision.
 *
 * NOTE: a scene keeps per-query scratch, so a single lrt_scene must not be
 * intersected from multiple threads concurrently.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightrt_c.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#if defined(LRT_HAVE_SSE2)
#include <emmintrin.h>
#endif

/* ------------------------------------------------------------------------- */
/* Tunables (mirror lightrt's BVHBuildConfig defaults).                      */
/* ------------------------------------------------------------------------- */
#define LRT_MAX_LEAF_SIZE 4u  /* max primitives per leaf */
#define LRT_NUM_BINS      16u /* SAH bins per axis */
#define LRT_TRAVERSAL_COST    1.0f
#define LRT_INTERSECTION_COST 1.0f
#define LRT_STACK_SIZE    64  /* traversal stack depth (>= max tree depth) */

#define LRT_INF_F (3.402823466e+38f) /* ~FLT_MAX, used as +inf sentinel */
#define LRT_INVALID_NODE 0xFFFFFFFFu
#define LRT_DEFAULT_PROGRESS_INTERVAL 4096u
#define LRT_DEFAULT_MAX_BUILD_DEPTH 64u

/* BVH node (full precision). flags: bit0 = leaf, bits1-2 = split axis. */
typedef struct lrt_node {
    float lo[3];
    float hi[3];
    union {
        struct { uint32_t left, right; } inner; /* interior children */
        struct { uint32_t offset, count; } leaf; /* prim-index range */
    } u;
    uint32_t flags;
} lrt_node;

struct lrt_scene {
    unsigned         nprims;
    lrt_bounds_cb    bounds_cb;
    lrt_intersect_cb isect_cb;
    void            *user;

    /* BVH */
    lrt_node *nodes;
    uint32_t  node_count;
    uint32_t  node_cap;
    uint32_t *prim_indices; /* leaf primitive references (length nprims) */
    uint32_t  prim_index_count;
    int       built;

    lrt_options options;
    atomic_int cancel_requested;
    lrt_result last_result;
    char       last_error[128];
    size_t     last_progress_done;
    const char *last_progress_stage;

    /* per-query scratch (authoritative fp64 ray + best fp64 hit) */
    double   org[3], dir[3], tmin, tmax;
    double   best_t, best_u, best_v;
    unsigned best_prim;
};

/* Build-time context: per-primitive fp32 bounds (freed after build). */
typedef struct {
    lrt_scene   *s;
    const float *prim_lo;  /* nprims * 3 */
    const float *prim_hi;  /* nprims * 3 */
    const float *prim_cen; /* nprims * 3 */
} lrt_build_ctx;

/* ------------------------------------------------------------------------- */
/* Small helpers.                                                            */
/* ------------------------------------------------------------------------- */
static inline float lrt_minf(float a, float b) { return a < b ? a : b; }
static inline float lrt_maxf(float a, float b) { return a > b ? a : b; }

static const char *lrt_result_string(lrt_result result) {
    switch (result) {
        case LRT_RESULT_OK: return "ok";
        case LRT_RESULT_INVALID_ARGUMENT: return "invalid argument";
        case LRT_RESULT_OUT_OF_MEMORY: return "out of memory";
        case LRT_RESULT_CANCELED: return "canceled";
        case LRT_RESULT_NOT_BUILT: return "scene is not built";
        case LRT_RESULT_TRAVERSAL_OVERFLOW: return "traversal stack overflow";
        case LRT_RESULT_INVALID_BOUNDS: return "invalid primitive bounds";
        case LRT_RESULT_BUILD_LIMIT: return "BVH build limit reached";
        default: return "unknown error";
    }
}

static uint32_t lrt_max_build_depth(const lrt_scene *s) {
    return (s && s->options.max_build_depth != 0)
        ? s->options.max_build_depth
        : LRT_DEFAULT_MAX_BUILD_DEPTH;
}

static uint32_t lrt_max_leaf_size(const lrt_scene *s) {
    return (s && s->options.max_leaf_size != 0)
        ? s->options.max_leaf_size
        : LRT_MAX_LEAF_SIZE;
}

static void lrt_set_result(lrt_scene *s, lrt_result result, const char *message) {
    if (!s) return;
    s->last_result = result;
    if (!message) message = lrt_result_string(result);
    strncpy(s->last_error, message, sizeof(s->last_error) - 1u);
    s->last_error[sizeof(s->last_error) - 1u] = '\0';
}

static int lrt_is_finite3(const double v[3]) {
    return v && isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static int lrt_valid_ray(const double org[3], const double dir[3],
                         double tmin, double tmax) {
    if (!lrt_is_finite3(org) || !lrt_is_finite3(dir)) return 0;
    if (!isfinite(tmin) || !isfinite(tmax) || tmax < tmin) return 0;
    return dir[0] != 0.0 || dir[1] != 0.0 || dir[2] != 0.0;
}

static int lrt_valid_aabb(const lrt_aabb *a) {
    if (!a) return 0;
    for (int k = 0; k < 3; k++) {
        if (!isfinite(a->lo[k]) || !isfinite(a->hi[k]) || a->lo[k] > a->hi[k]) {
            return 0;
        }
    }
    return 1;
}

static int lrt_should_cancel(lrt_scene *s) {
    if (!s) return 1;
    if (atomic_load_explicit(&s->cancel_requested, memory_order_relaxed)) {
        if (s->last_result == LRT_RESULT_OK) {
            lrt_set_result(s, LRT_RESULT_CANCELED, NULL);
        }
        return 1;
    }
    if (s->options.cancel_cb && s->options.cancel_cb(s->options.user)) {
        atomic_store_explicit(&s->cancel_requested, 1, memory_order_relaxed);
        lrt_set_result(s, LRT_RESULT_CANCELED, NULL);
        return 1;
    }
    return 0;
}

static void lrt_reset_progress(lrt_scene *s) {
    if (!s) return;
    s->last_progress_done = (size_t)-1;
    s->last_progress_stage = NULL;
}

static void lrt_report_progress(lrt_scene *s, const char *stage,
                                size_t done, size_t total, int force) {
    if (!s || !s->options.progress_cb) return;
    size_t interval = s->options.progress_interval;
    if (interval == 0) interval = LRT_DEFAULT_PROGRESS_INTERVAL;
    if (!force && s->last_progress_stage == stage &&
        s->last_progress_done != (size_t)-1 &&
        done < s->last_progress_done + interval) {
        return;
    }

    double fraction = 0.0;
    if (total != 0) {
        fraction = (double)done / (double)total;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
    }

    lrt_progress progress;
    progress.stage = stage;
    progress.done = done;
    progress.total = total;
    progress.fraction = fraction;
    s->last_progress_stage = stage;
    s->last_progress_done = done;
    s->options.progress_cb(&progress, s->options.user);
}

static inline float lrt_surface_area(const float lo[3], const float hi[3]) {
    float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

static inline void lrt_box_reset(float lo[3], float hi[3]) {
    lo[0] = lo[1] = lo[2] = LRT_INF_F;
    hi[0] = hi[1] = hi[2] = -LRT_INF_F;
}

static inline void lrt_box_expand(float lo[3], float hi[3],
                                  const float plo[3], const float phi[3]) {
    for (int a = 0; a < 3; a++) {
        lo[a] = lrt_minf(lo[a], plo[a]);
        hi[a] = lrt_maxf(hi[a], phi[a]);
    }
}

/* Longest axis of a box (matches AABB::longestAxis tie-breaking). */
static inline int lrt_longest_axis(const float lo[3], const float hi[3]) {
    float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    if (dx > dy && dx > dz) return 0;
    if (dy > dz) return 1;
    return 2;
}

/* ------------------------------------------------------------------------- */
/* Build (binned SAH).                                                       */
/* ------------------------------------------------------------------------- */

static uint32_t lrt_make_leaf(lrt_scene *s, uint32_t node_idx,
                              const uint32_t *indices, uint32_t num) {
    if (s->prim_index_count > s->nprims || num > s->nprims - s->prim_index_count) {
        lrt_set_result(s, LRT_RESULT_OUT_OF_MEMORY, "primitive index storage overflow");
        return LRT_INVALID_NODE;
    }
    uint32_t offset = s->prim_index_count;
    for (uint32_t i = 0; i < num; i++) {
        s->prim_indices[s->prim_index_count++] = indices[i];
    }
    lrt_node *n = &s->nodes[node_idx];
    n->u.leaf.offset = offset;
    n->u.leaf.count = num;
    n->flags |= 0x1u;
    return node_idx;
}

/* Recursively build a subtree over indices[0..num). Returns the node index.
 * The nodes array is preallocated to 2*nprims, so it never reallocates and
 * indices held across recursion stay valid. */
static uint32_t lrt_build_recursive(lrt_build_ctx *c, uint32_t *indices,
                                    uint32_t num, uint32_t depth) {
    lrt_scene *s = c->s;
    const float *plo = c->prim_lo;
    const float *phi = c->prim_hi;
    const float *pcen = c->prim_cen;

    if (lrt_should_cancel(s)) return LRT_INVALID_NODE;
    if (depth > lrt_max_build_depth(s)) {
        lrt_set_result(s, LRT_RESULT_BUILD_LIMIT, "BVH max build depth reached");
        return LRT_INVALID_NODE;
    }
    if (s->node_count >= s->node_cap) {
        lrt_set_result(s, LRT_RESULT_OUT_OF_MEMORY, "BVH node storage overflow");
        return LRT_INVALID_NODE;
    }

    /* Node bounds + centroid bounds. */
    float nlo[3], nhi[3], clo[3], chi[3];
    lrt_box_reset(nlo, nhi);
    lrt_box_reset(clo, chi);
    for (uint32_t i = 0; i < num; i++) {
        uint32_t p = indices[i];
        const float *lo_p = &plo[p * 3];
        const float *hi_p = &phi[p * 3];
        const float *cen_p = &pcen[p * 3];

        nlo[0] = lrt_minf(nlo[0], lo_p[0]);
        nlo[1] = lrt_minf(nlo[1], lo_p[1]);
        nlo[2] = lrt_minf(nlo[2], lo_p[2]);

        nhi[0] = lrt_maxf(nhi[0], hi_p[0]);
        nhi[1] = lrt_maxf(nhi[1], hi_p[1]);
        nhi[2] = lrt_maxf(nhi[2], hi_p[2]);

        clo[0] = lrt_minf(clo[0], cen_p[0]);
        clo[1] = lrt_minf(clo[1], cen_p[1]);
        clo[2] = lrt_minf(clo[2], cen_p[2]);

        chi[0] = lrt_maxf(chi[0], cen_p[0]);
        chi[1] = lrt_maxf(chi[1], cen_p[1]);
        chi[2] = lrt_maxf(chi[2], cen_p[2]);
    }

    uint32_t node_idx = s->node_count++;
    lrt_report_progress(s, "build", (size_t)s->nprims + s->node_count,
                        (size_t)s->nprims + s->node_cap, 0);
    lrt_node *node = &s->nodes[node_idx];
    node->lo[0] = nlo[0]; node->lo[1] = nlo[1]; node->lo[2] = nlo[2];
    node->hi[0] = nhi[0]; node->hi[1] = nhi[1]; node->hi[2] = nhi[2];
    node->flags = 0;

    uint32_t max_leaf_size = lrt_max_leaf_size(s);
    if (max_leaf_size == 0) {
        lrt_set_result(s, LRT_RESULT_BUILD_LIMIT, "BVH max leaf size must be non-zero");
        return LRT_INVALID_NODE;
    }

    if (num <= max_leaf_size) {
        return lrt_make_leaf(s, node_idx, indices, num);
    }

    /* Binned SAH split search over all three axes. */
    int   best_axis = lrt_longest_axis(clo, chi);
    float best_pos = 0.0f;
    float best_cost = LRT_INF_F;
    float parent_area = lrt_surface_area(nlo, nhi);

    for (int axis = 0; axis < 3; axis++) {
        float amin = clo[axis], amax = chi[axis];
        if (amax - amin < 1e-6f) continue; /* degenerate on this axis */

        float bin_size = (amax - amin) / (float)LRT_NUM_BINS;

        uint32_t bin_count[LRT_NUM_BINS];
        float    bin_lo[LRT_NUM_BINS][3], bin_hi[LRT_NUM_BINS][3];
        for (uint32_t b = 0; b < LRT_NUM_BINS; b++) {
            bin_count[b] = 0;
            lrt_box_reset(bin_lo[b], bin_hi[b]);
        }

        for (uint32_t i = 0; i < num; i++) {
            uint32_t p = indices[i];
            float cen = pcen[p * 3 + axis];
            uint32_t b = (uint32_t)((cen - amin) / bin_size);
            if (b >= LRT_NUM_BINS) b = LRT_NUM_BINS - 1;
            bin_count[b]++;

            const float *lo_p = &plo[p * 3];
            const float *hi_p = &phi[p * 3];
            float *blo = bin_lo[b];
            float *bhi = bin_hi[b];

            blo[0] = lrt_minf(blo[0], lo_p[0]);
            blo[1] = lrt_minf(blo[1], lo_p[1]);
            blo[2] = lrt_minf(blo[2], lo_p[2]);

            bhi[0] = lrt_maxf(bhi[0], hi_p[0]);
            bhi[1] = lrt_maxf(bhi[1], hi_p[1]);
            bhi[2] = lrt_maxf(bhi[2], hi_p[2]);
        }

        /* Left prefix sweep. */
        float    left_lo[LRT_NUM_BINS][3], left_hi[LRT_NUM_BINS][3];
        uint32_t left_cnt[LRT_NUM_BINS];
        float run_lo[3], run_hi[3];
        uint32_t run_cnt = 0;
        lrt_box_reset(run_lo, run_hi);
        for (uint32_t b = 0; b < LRT_NUM_BINS; b++) {
            run_lo[0] = lrt_minf(run_lo[0], bin_lo[b][0]);
            run_lo[1] = lrt_minf(run_lo[1], bin_lo[b][1]);
            run_lo[2] = lrt_minf(run_lo[2], bin_lo[b][2]);

            run_hi[0] = lrt_maxf(run_hi[0], bin_hi[b][0]);
            run_hi[1] = lrt_maxf(run_hi[1], bin_hi[b][1]);
            run_hi[2] = lrt_maxf(run_hi[2], bin_hi[b][2]);

            run_cnt += bin_count[b];

            left_lo[b][0] = run_lo[0];
            left_lo[b][1] = run_lo[1];
            left_lo[b][2] = run_lo[2];

            left_hi[b][0] = run_hi[0];
            left_hi[b][1] = run_hi[1];
            left_hi[b][2] = run_hi[2];

            left_cnt[b] = run_cnt;
        }

        /* Right suffix sweep, evaluating each split plane. */
        lrt_box_reset(run_lo, run_hi);
        run_cnt = 0;
        for (uint32_t b = LRT_NUM_BINS - 1; b > 0; b--) {
            run_lo[0] = lrt_minf(run_lo[0], bin_lo[b][0]);
            run_lo[1] = lrt_minf(run_lo[1], bin_lo[b][1]);
            run_lo[2] = lrt_minf(run_lo[2], bin_lo[b][2]);

            run_hi[0] = lrt_maxf(run_hi[0], bin_hi[b][0]);
            run_hi[1] = lrt_maxf(run_hi[1], bin_hi[b][1]);
            run_hi[2] = lrt_maxf(run_hi[2], bin_hi[b][2]);

            run_cnt += bin_count[b];

            uint32_t left_count = left_cnt[b - 1];
            if (left_count == 0 || run_cnt == 0) continue;

            float left_area = lrt_surface_area(left_lo[b - 1], left_hi[b - 1]);
            float right_area = lrt_surface_area(run_lo, run_hi);
            float cost = LRT_TRAVERSAL_COST +
                         LRT_INTERSECTION_COST *
                             ((float)left_count * left_area +
                              (float)run_cnt * right_area) / parent_area;
            if (cost < best_cost) {
                best_cost = cost;
                best_axis = axis;
                best_pos = amin + (float)b * bin_size;
            }
        }
    }

    /* If splitting is not worthwhile (or impossible: e.g. all centroids
     * coincide), make an oversized leaf rather than failing the build.
     * Traversal handles any leaf size; prim_indices has room for exactly
     * nprims entries across all leaves. */
    float leaf_cost = LRT_INTERSECTION_COST * (float)num;
    if (best_cost >= leaf_cost) {
        return lrt_make_leaf(s, node_idx, indices, num);
    }

    /* Partition indices around best_pos on best_axis. */
    uint32_t mid = 0;
    for (uint32_t i = 0; i < num; i++) {
        uint32_t p = indices[i];
        float cen = pcen[p * 3 + best_axis];
        if (cen < best_pos) {
            uint32_t tmp = indices[i];
            indices[i] = indices[mid];
            indices[mid] = tmp;
            mid++;
        }
    }
    if (mid == 0 || mid == num) mid = num / 2; /* fallback: median by index */

    uint32_t left = lrt_build_recursive(c, indices, mid, depth + 1u);
    if (left == LRT_INVALID_NODE) return LRT_INVALID_NODE;
    uint32_t right = lrt_build_recursive(c, indices + mid, num - mid, depth + 1u);
    if (right == LRT_INVALID_NODE) return LRT_INVALID_NODE;

    /* Re-fetch: recursion appended nodes (array does not realloc). */
    node = &s->nodes[node_idx];
    node->u.inner.left = left;
    node->u.inner.right = right;
    node->flags = ((uint32_t)best_axis & 0x3u) << 1; /* clear leaf bit */
    return node_idx;
}

/* ------------------------------------------------------------------------- */
/* Traversal.                                                                */
/* ------------------------------------------------------------------------- */

/* Slab test (ported from AABB::intersect). Returns 1 and the near distance on
 * hit. invd[a] may be +/-inf for zero direction components; that is handled. */
static inline int lrt_aabb_intersect(const float lo[3], const float hi[3],
                                     const float org[3], const float invd[3],
                                     float tmin, float tmax, float *tmin_out) {
#if defined(LRT_HAVE_SSE2)
    /* SSE2 slab test.  lo/hi/org/invd are 3‑element arrays, so we must NOT
     * use _mm_loadu_ps (which reads 4 floats and would go out‑of‑bounds on
     * the struct members).  Instead we build the vectors from 3 scalars,
     * keeping the 4th lane neutral (0 for differences, 1 for invd so the
     * multiply yields 0). */
    __m128 lo_vec  = _mm_set_ps(0.f, lo[2],  lo[1],  lo[0]);
    __m128 hi_vec  = _mm_set_ps(0.f, hi[2],  hi[1],  hi[0]);
    __m128 org_vec = _mm_set_ps(0.f, org[2], org[1], org[0]);
    __m128 inv_vec = _mm_set_ps(1.f, invd[2],invd[1],invd[0]);

    __m128 t0 = _mm_mul_ps(_mm_sub_ps(lo_vec, org_vec), inv_vec);
    __m128 t1 = _mm_mul_ps(_mm_sub_ps(hi_vec, org_vec), inv_vec);
    __m128 tmin_vec = _mm_min_ps(t0, t1);
    __m128 tmax_vec = _mm_max_ps(t0, t1);

    /* Horizontal reduce: tmin = max(tmin, tmin_vec[0..2]),
     *                    tmax = min(tmax, tmax_vec[0..2]). */
    _Alignas(16) float tmin_vals[4];
    _Alignas(16) float tmax_vals[4];
    _mm_store_ps(tmin_vals, tmin_vec);
    _mm_store_ps(tmax_vals, tmax_vec);
    if (tmin_vals[0] > tmin) tmin = tmin_vals[0];
    if (tmin_vals[1] > tmin) tmin = tmin_vals[1];
    if (tmin_vals[2] > tmin) tmin = tmin_vals[2];
    if (tmax_vals[0] < tmax) tmax = tmax_vals[0];
    if (tmax_vals[1] < tmax) tmax = tmax_vals[1];
    if (tmax_vals[2] < tmax) tmax = tmax_vals[2];
#else
    for (int a = 0; a < 3; a++) {
        float t0a = (lo[a] - org[a]) * invd[a];
        float t1a = (hi[a] - org[a]) * invd[a];
        if (t0a > t1a) { float tmp = t0a; t0a = t1a; t1a = tmp; }
        if (t0a > tmin) tmin = t0a;
        if (t1a < tmax) tmax = t1a;
    }
#endif
    if (tmax < tmin) return 0;
    *tmin_out = tmin;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Public API.                                                               */
/* ------------------------------------------------------------------------- */

lrt_scene *lrt_scene_create(unsigned nprims, lrt_bounds_cb bounds_cb,
                            lrt_intersect_cb isect_cb, void *user) {
    if (!bounds_cb || !isect_cb) return NULL;
    lrt_scene *s = (lrt_scene *)calloc(1, sizeof(lrt_scene));
    if (!s) return NULL;
    s->nprims = nprims;
    s->bounds_cb = bounds_cb;
    s->isect_cb = isect_cb;
    s->user = user;
    s->best_prim = LRT_NO_HIT;
    atomic_init(&s->cancel_requested, 0);
    lrt_set_result(s, LRT_RESULT_OK, NULL);
    lrt_reset_progress(s);
    return s;
}

int lrt_scene_set_options(lrt_scene *s, const lrt_options *options) {
    if (!s) return 0;
    if (options) {
        s->options = *options;
    } else {
        memset(&s->options, 0, sizeof(s->options));
    }
    lrt_set_result(s, LRT_RESULT_OK, NULL);
    return 1;
}

int lrt_scene_build(lrt_scene *s) {
    if (!s || s->nprims == 0) {
        lrt_set_result(s, LRT_RESULT_INVALID_ARGUMENT, "scene is null or has no primitives");
        return 0;
    }
    lrt_set_result(s, LRT_RESULT_OK, NULL);
    lrt_reset_progress(s);

    size_t nprims_size = (size_t)s->nprims;
    if (s->nprims > UINT32_MAX / 2u ||
        nprims_size > SIZE_MAX / (3u * sizeof(float)) ||
        nprims_size > SIZE_MAX / sizeof(uint32_t) ||
        nprims_size > SIZE_MAX / (2u * sizeof(lrt_node)) ||
        nprims_size > SIZE_MAX / 3u) {
        lrt_set_result(s, LRT_RESULT_OUT_OF_MEMORY, "scene is too large");
        return 0;
    }
    /* Worst-case node count for a binary tree with <= nprims leaves. */
    size_t node_cap = (size_t)s->nprims * 2u;
    if (node_cap < 1) node_cap = 1;
    size_t build_total = nprims_size + node_cap;
    lrt_report_progress(s, "build", 0, build_total, 1);
    if (lrt_should_cancel(s)) return 0;

    /* Per-primitive fp32 bounds + centroids (build-time only). */
    size_t float3_bytes = (size_t)s->nprims * 3 * sizeof(float);
    float *prim_lo  = (float *)malloc(float3_bytes);
    float *prim_hi  = (float *)malloc(float3_bytes);
    float *prim_cen = (float *)malloc(float3_bytes);
    uint32_t *indices = (uint32_t *)malloc((size_t)s->nprims * sizeof(uint32_t));

    lrt_node *nodes = (lrt_node *)malloc(node_cap * sizeof(lrt_node));
    uint32_t *prim_indices = (uint32_t *)malloc((size_t)s->nprims * sizeof(uint32_t));

    if (!prim_lo || !prim_hi || !prim_cen || !indices || !nodes || !prim_indices) {
        free(prim_lo); free(prim_hi); free(prim_cen); free(indices);
        free(nodes); free(prim_indices);
        lrt_set_result(s, LRT_RESULT_OUT_OF_MEMORY, NULL);
        return 0;
    }

    for (unsigned i = 0; i < s->nprims; i++) {
        if (lrt_should_cancel(s)) {
            free(prim_lo); free(prim_hi); free(prim_cen); free(indices);
            free(nodes); free(prim_indices);
            return 0;
        }
        lrt_aabb a = s->bounds_cb(i, s->user);
        if (!lrt_valid_aabb(&a)) {
            free(prim_lo); free(prim_hi); free(prim_cen); free(indices);
            free(nodes); free(prim_indices);
            lrt_set_result(s, LRT_RESULT_INVALID_BOUNDS, "bounds callback returned invalid AABB");
            return 0;
        }
        for (int k = 0; k < 3; k++) {
            prim_lo[i * 3 + k]  = (float)a.lo[k];
            prim_hi[i * 3 + k]  = (float)a.hi[k];
            prim_cen[i * 3 + k] = 0.5f * ((float)a.lo[k] + (float)a.hi[k]);
        }
        indices[i] = i;
        lrt_report_progress(s, "build", (size_t)i + 1u, build_total, 0);
    }

    /* Free any previous build, then attach fresh storage. */
    free(s->nodes);
    free(s->prim_indices);
    s->nodes = nodes;
    s->node_count = 0;
    s->node_cap = (uint32_t)node_cap;
    s->prim_indices = prim_indices;
    s->prim_index_count = 0;
    s->built = 0;

    lrt_build_ctx ctx = { s, prim_lo, prim_hi, prim_cen };
    uint32_t root = lrt_build_recursive(&ctx, indices, s->nprims, 0);
    if (root == LRT_INVALID_NODE) {
        free(prim_lo);
        free(prim_hi);
        free(prim_cen);
        free(indices);
        free(s->nodes);
        free(s->prim_indices);
        s->nodes = NULL;
        s->prim_indices = NULL;
        s->node_count = 0;
        s->node_cap = 0;
        s->prim_index_count = 0;
        s->built = 0;
        return 0;
    }
    s->built = 1;
    lrt_report_progress(s, "build", build_total, build_total, 1);
    lrt_set_result(s, LRT_RESULT_OK, NULL);

    free(prim_lo);
    free(prim_hi);
    free(prim_cen);
    free(indices);
    return 1;
}

unsigned lrt_scene_intersect(lrt_scene *s, const double org[3],
                             const double dir[3], double tmin, double tmax,
                             double *t, double *u, double *v) {
    if (!s) return LRT_NO_HIT;
    lrt_set_result(s, LRT_RESULT_OK, NULL);
    lrt_reset_progress(s);
    if (!s->built || s->node_count == 0) {
        lrt_set_result(s, LRT_RESULT_NOT_BUILT, NULL);
        return LRT_NO_HIT;
    }
    if (!lrt_valid_ray(org, dir, tmin, tmax)) {
        lrt_set_result(s, LRT_RESULT_INVALID_ARGUMENT, "invalid ray or range");
        return LRT_NO_HIT;
    }
    lrt_report_progress(s, "intersect", 0, s->node_count, 1);
    if (lrt_should_cancel(s)) return LRT_NO_HIT;

    for (int k = 0; k < 3; k++) { s->org[k] = org[k]; s->dir[k] = dir[k]; }
    s->tmin = tmin;
    s->tmax = tmax;
    s->best_t = DBL_MAX;
    s->best_u = 0.0;
    s->best_v = 0.0;
    s->best_prim = LRT_NO_HIT;

    /* fp32 ray for broad-phase node tests. */
    float forg[3], fdir[3], finvd[3];
    for (int k = 0; k < 3; k++) {
        forg[k] = (float)org[k];
        fdir[k] = (float)dir[k];
        finvd[k] = 1.0f / fdir[k]; /* +/-inf for 0 dir; slab test handles it */
    }
    float ftmin = (tmin > 0.0) ? (float)tmin : 0.0f;
    float ftmax = (float)tmax;

    uint32_t stack[LRT_STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0; /* root */
    size_t visited_nodes = 0;
    size_t steps = 0;
    size_t check_interval = s->options.progress_interval;
    if (check_interval == 0) check_interval = LRT_DEFAULT_PROGRESS_INTERVAL;
    size_t next_check = check_interval;
    while (sp > 0) {
        uint32_t node_idx = stack[--sp];
        const lrt_node *n = &s->nodes[node_idx];
        visited_nodes++;
        steps++;
        if (steps >= next_check) {
            if (lrt_should_cancel(s)) return LRT_NO_HIT;
            lrt_report_progress(s, "intersect", visited_nodes, s->node_count, 0);
            next_check = steps + check_interval;
        }

        float box_tmin;
        if (!lrt_aabb_intersect(n->lo, n->hi, forg, finvd, ftmin, ftmax,
                                &box_tmin)) {
            continue;
        }
        /* Cull against the closest hit so far (fp32 broad-phase bound). */
        float cull = (s->best_prim != LRT_NO_HIT) ? (float)s->best_t : ftmax;
        if (box_tmin > cull) continue;

        if (n->flags & 0x1u) { /* leaf */
            uint32_t offset = n->u.leaf.offset;
            uint32_t count = n->u.leaf.count;
            for (uint32_t i = 0; i < count; i++) {
                steps++;
                if (steps >= next_check) {
                    if (lrt_should_cancel(s)) return LRT_NO_HIT;
                    lrt_report_progress(s, "intersect", visited_nodes, s->node_count, 0);
                    next_check = steps + check_interval;
                }
                uint32_t prim = s->prim_indices[offset + i];
                double td = 0.0, ud = 0.0, vd = 0.0;
                double cb_tmax = s->best_t < s->tmax ? s->best_t : s->tmax;
                if (s->isect_cb(s->org, s->dir, s->tmin, cb_tmax, prim,
                                s->user, &td, &ud, &vd) &&
                    isfinite(td) && td >= s->tmin && td <= cb_tmax &&
                    td < s->best_t) {
                    s->best_t = td;
                    s->best_u = ud;
                    s->best_v = vd;
                    s->best_prim = prim;
                }
            }
        } else { /* interior: push far child first so near is visited first */
            uint32_t left = n->u.inner.left;
            uint32_t right = n->u.inner.right;
            uint32_t axis = (n->flags >> 1) & 0x3u;
            if (sp + 2 > LRT_STACK_SIZE) {
                lrt_set_result(s, LRT_RESULT_TRAVERSAL_OVERFLOW, NULL);
                return LRT_NO_HIT;
            }
            if (fdir[axis] >= 0.0f) {
                stack[sp++] = right; /* far */
                stack[sp++] = left;  /* near */
            } else {
                stack[sp++] = left;
                stack[sp++] = right;
            }
        }
    }

    if (s->best_prim != LRT_NO_HIT) {
        if (t) *t = s->best_t;
        if (u) *u = s->best_u;
        if (v) *v = s->best_v;
    }
    lrt_report_progress(s, "intersect", s->node_count, s->node_count, 1);
    lrt_set_result(s, LRT_RESULT_OK, NULL);
    return s->best_prim;
}

void lrt_scene_request_cancel(lrt_scene *s) {
    if (!s) return;
    atomic_store_explicit(&s->cancel_requested, 1, memory_order_relaxed);
}

void lrt_scene_clear_cancel(lrt_scene *s) {
    if (!s) return;
    atomic_store_explicit(&s->cancel_requested, 0, memory_order_relaxed);
    lrt_set_result(s, LRT_RESULT_OK, NULL);
}

int lrt_scene_cancel_requested(const lrt_scene *s) {
    if (!s) return 0;
    return atomic_load_explicit(&s->cancel_requested, memory_order_relaxed) ? 1 : 0;
}

lrt_result lrt_scene_last_result(const lrt_scene *s) {
    if (!s) return LRT_RESULT_INVALID_ARGUMENT;
    return s->last_result;
}

const char *lrt_scene_last_error(const lrt_scene *s) {
    if (!s) return "invalid argument";
    return s->last_error[0] ? s->last_error : lrt_result_string(s->last_result);
}

void lrt_scene_free(lrt_scene *s) {
    if (!s) return;
    free(s->nodes);
    free(s->prim_indices);
    free(s);
}

const char *lrt_backend_name(void) {
    return "LightRT C11 (pure-C binned-SAH BVH, fp32 BVH, fp64 callback)";
}
