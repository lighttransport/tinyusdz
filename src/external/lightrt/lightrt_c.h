/*
 * lightrt_c.h — C11 binding for LightRT's generic BVH + custom-primitive ray
 * intersection.
 *
 * Lets C code drive LightRT's BVH while supplying its own primitive bounds and
 * intersection in *double precision*. A scene is a BVH over `nprims` opaque
 * primitives; the user provides a bounds callback (for building) and an
 * intersection callback (invoked per candidate primitive during traversal).
 *
 * The BVH itself is single precision (broad phase); the intersection callback
 * receives the original fp64 ray, so an analytic surface can be solved at full
 * double precision (e.g. sub-nm OPL for aspheric lens tracing).
 *
 * NOTE: a scene keeps per-query scratch, so a single lrt_scene must not be
 * intersected from multiple threads concurrently. Use one scene per thread (or
 * per surface) for parallel queries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTRT_C_H
#define LIGHTRT_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returned by lrt_scene_intersect when nothing was hit. */
#define LRT_NO_HIT 0xFFFFFFFFu

typedef struct lrt_scene lrt_scene;

typedef enum lrt_result {
    LRT_RESULT_OK = 0,
    LRT_RESULT_INVALID_ARGUMENT = 1,
    LRT_RESULT_OUT_OF_MEMORY = 2,
    LRT_RESULT_CANCELED = 3,
    LRT_RESULT_NOT_BUILT = 4,
    LRT_RESULT_TRAVERSAL_OVERFLOW = 5,
    LRT_RESULT_INVALID_BOUNDS = 6,
    LRT_RESULT_BUILD_LIMIT = 7
} lrt_result;

/* Axis-aligned bounding box in world (fp64) coordinates. */
typedef struct { double lo[3]; double hi[3]; } lrt_aabb;

/* Bounds of primitive `prim`. Called during lrt_scene_build. */
typedef lrt_aabb (*lrt_bounds_cb)(unsigned prim, void *user);

/* Intersect the fp64 ray with primitive `prim`. On hit, write the ray parameter
 * to *t (and optional surface params to *u,*v) and return 1; else return 0.
 * Only hits with tmin <= t <= tmax should be reported. */
typedef int (*lrt_intersect_cb)(const double org[3], const double dir[3],
                                double tmin, double tmax, unsigned prim,
                                void *user, double *t, double *u, double *v);

/* Return non-zero to cancel the current build or intersection query. */
typedef int (*lrt_cancel_cb)(void *user);

typedef struct lrt_progress {
    const char *stage;  /* "build", "intersect", etc.; valid during callback */
    size_t done;
    size_t total;       /* 0 when no useful total is known */
    double fraction;    /* clamped to [0,1] when total is known */
} lrt_progress;

typedef void (*lrt_progress_cb)(const lrt_progress *progress, void *user);

typedef struct lrt_options {
    lrt_cancel_cb cancel_cb;
    lrt_progress_cb progress_cb;
    void *user;

    /* Callback cadence in work units. 0 selects a conservative default. */
    size_t progress_interval;

    /* Build safety limits. 0 selects backend defaults. */
    unsigned max_build_depth;
    unsigned max_leaf_size;
} lrt_options;

/* Create a scene over `nprims` primitives. Callbacks + user are retained. */
lrt_scene *lrt_scene_create(unsigned nprims, lrt_bounds_cb bounds_cb,
                            lrt_intersect_cb isect_cb, void *user);

/* Copy optional cancel/progress callbacks into the scene. Pass NULL to clear. */
int lrt_scene_set_options(lrt_scene *s, const lrt_options *options);

/* Build the BVH. Returns 1 on success, 0 on failure. */
int lrt_scene_build(lrt_scene *s);

/* Closest-hit query. Returns the hit primitive id, or LRT_NO_HIT. Writes the
 * fp64 ray parameter / surface params to *t,*u,*v when non-NULL on hit. */
unsigned lrt_scene_intersect(lrt_scene *s, const double org[3], const double dir[3],
                             double tmin, double tmax,
                             double *t, double *u, double *v);

/* Cooperative cancellation. A requested cancel stays set until cleared. */
void lrt_scene_request_cancel(lrt_scene *s);
void lrt_scene_clear_cancel(lrt_scene *s);
int lrt_scene_cancel_requested(const lrt_scene *s);

/* Status for the last API call on this scene. The returned string is owned by
 * the scene and remains valid until the next API call or lrt_scene_free. */
lrt_result lrt_scene_last_result(const lrt_scene *s);
const char *lrt_scene_last_error(const lrt_scene *s);

void lrt_scene_free(lrt_scene *s);

/* Human-readable backend description. */
const char *lrt_backend_name(void);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTRT_C_H */
