/*
 * lightrt_c_tri.h — C11 triangle-native fp32 BVH for LightRT.
 *
 * Optimized companion to the generic callback API in lightrt_c.h. Where that
 * API serves opaque fp64 primitives through per-primitive callbacks, this one
 * owns the triangles: vertex data is copied and pre-swizzled into SIMD-friendly
 * SoA leaf blocks at build time, the BVH is a wide (4- or 8-ary) tree with
 * cache-line-sized SoA nodes, and traversal kernels are compiled scalar,
 * SSE4 (BVH4) and AVX2 (BVH8) with compile-time dispatch.
 *
 * Queries are stateless and thread-safe: any number of threads may intersect
 * a single lrt_tri_scene concurrently. There are no cancel/progress hooks in
 * the query hot path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTRT_C_TRI_H
#define LIGHTRT_C_TRI_H

#include <stddef.h>
#include <stdint.h>

#include "lightrt_c.h" /* lrt_result */

#ifdef __cplusplus
extern "C" {
#endif

/* Stored in lrt_hit.prim_id when nothing was hit. */
#define LRT_TRI_NO_HIT 0xFFFFFFFFu

typedef struct lrt_tri_scene lrt_tri_scene;

/* Single-precision ray. dir need not be normalized; the reported t is in units
 * of |dir|. tmin/tmax bound the accepted hit interval. 32 bytes. */
typedef struct lrt_ray {
    float org[3];
    float tmin;
    float dir[3];
    float tmax;
} lrt_ray;

/* Closest-hit result. prim_id is the caller's triangle index (i.e. the index
 * into the vertices array passed to lrt_tri_scene_build), or LRT_TRI_NO_HIT.
 * u/v are Moller-Trumbore barycentrics of the hit. 16 bytes. */
typedef struct lrt_hit {
    float t, u, v;
    uint32_t prim_id;
} lrt_hit;

typedef enum lrt_tri_quality {
    LRT_TRI_BUILD_FAST = 0,    /* LBVH (Morton sort + bit splits): fastest build */
    LRT_TRI_BUILD_DEFAULT = 1, /* binned SAH (16 bins) */
    /* Binned SAH + spatial splits (SBVH, Stich et al. 2009): overlapping
     * primitives may be referenced by several leaves with clipped bounds.
     * Tightest tree (helps short/shadow rays most), slowest build, serial. */
    LRT_TRI_BUILD_HQ = 2
} lrt_tri_quality;

typedef enum lrt_tri_layout {
    LRT_TRI_LAYOUT_AUTO = 0, /* widest kernel compiled in */
    LRT_TRI_LAYOUT_BVH4 = 4,
    LRT_TRI_LAYOUT_BVH8 = 8,
    /* 8-wide with 8-bit quantized child bounds (128-byte nodes vs 256):
     * halves node bandwidth at the cost of extra decode ALU; intended for
     * memory-latency-bound incoherent rays on large scenes. */
    LRT_TRI_LAYOUT_BVH8Q = 9,
    /* 8-wide with 4-bit quantized child bounds (96-byte nodes): coarser bounds
     * (a few % more node tests) for ~25% smaller nodes. */
    LRT_TRI_LAYOUT_BVH8_Q4 = 10,
    /* 8-wide with 8-bit-FLOAT (E4M3) child bounds (128-byte nodes, same size as
     * BVH8Q): a tighter bound fit for skewed extents, not a memory win. */
    LRT_TRI_LAYOUT_BVH8_QF8 = 11
} lrt_tri_layout;

typedef struct lrt_tri_build_options {
    lrt_tri_quality quality;
    lrt_tri_layout layout;
    unsigned max_leaf_size; /* triangles per leaf; 0 = default (8) */
    unsigned num_threads;   /* build threads; 0 or 1 = serial */
} lrt_tri_build_options;

/* Build a scene over ntris triangles. vertices = 9*ntris floats laid out as
 * v0.xyz v1.xyz v2.xyz per triangle. The data is copied and re-swizzled; the
 * caller's buffer is not retained. opts may be NULL for defaults. Returns NULL
 * on failure and, when err is non-NULL, stores the reason there. */
lrt_tri_scene *lrt_tri_scene_build(const float *vertices, size_t ntris,
                                   const lrt_tri_build_options *opts,
                                   lrt_result *err);

void lrt_tri_scene_free(lrt_tri_scene *s);

/* Closest hit. Returns 1 and fills *hit on a hit; returns 0 on miss (hit, if
 * non-NULL, gets prim_id = LRT_TRI_NO_HIT). Thread-safe. */
int lrt_tri_intersect1(const lrt_tri_scene *s, const lrt_ray *ray, lrt_hit *hit);

/* Any hit (shadow/occlusion). Returns 1 if anything lies in [tmin, tmax]. */
int lrt_tri_occluded1(const lrt_tri_scene *s, const lrt_ray *ray);

/* How a batch of rays relates spatially; picks the traversal strategy. */
typedef enum lrt_tri_batch_hint {
    /* Library default (currently the incoherent strategy: the coherent case
     * is fast either way, the incoherent case is the painful one). */
    LRT_TRI_BATCH_AUTO = 0,
    /* Nearby rays visiting the same nodes (e.g. primary camera rays):
     * closest-hit runs them as Ray4/Ray8 packets, which amortize node and leaf
     * fetches over the packet (~+25% over per-ray; faster than Embree on the
     * mandelbulb). Any-hit stays per-ray (packets would defeat early-out). */
    LRT_TRI_BATCH_COHERENT = 1,
    /* Unrelated rays (e.g. path-tracing bounces): several rays are kept in
     * flight per thread, one node visit each in turn, so one ray's memory
     * stall overlaps the others' compute. */
    LRT_TRI_BATCH_INCOHERENT = 2
} lrt_tri_batch_hint;

/* Batched variants: amortize per-ray setup over n rays and, for incoherent
 * batches, hide memory latency by interleaving rays. Results are identical
 * to looping the single-ray calls regardless of hint. */
void lrt_tri_intersect1N(const lrt_tri_scene *s, const lrt_ray *rays,
                         lrt_hit *hits, size_t n, lrt_tri_batch_hint hint);
void lrt_tri_occluded1N(const lrt_tri_scene *s, const lrt_ray *rays,
                        uint8_t *occluded, size_t n, lrt_tri_batch_hint hint);

/* --- Coherent ray packets (Ray4 / Ray8) -----------------------------------
 *
 * Fixed-size SoA ray packets traversed together (one node tested against all
 * lanes). Best for COHERENT rays (primary camera, surface->light shadow); for
 * divergent rays prefer lrt_tri_intersect1N(..., LRT_TRI_BATCH_INCOHERENT).
 * Results are identical to looping the single-ray calls. Triangle scenes only;
 * other primitive kinds fall back to a per-ray loop. */
typedef struct lrt_ray4 {
    float orgx[4], orgy[4], orgz[4];
    float dirx[4], diry[4], dirz[4];
    float tmin[4], tmax[4];
} lrt_ray4;
typedef struct lrt_ray8 {
    float orgx[8], orgy[8], orgz[8];
    float dirx[8], diry[8], dirz[8];
    float tmin[8], tmax[8];
} lrt_ray8;
typedef struct lrt_hit4 {
    float t[4], u[4], v[4];
    uint32_t prim_id[4];
} lrt_hit4;
typedef struct lrt_hit8 {
    float t[8], u[8], v[8];
    uint32_t prim_id[8];
} lrt_hit8;

void lrt_tri_intersect4(const lrt_tri_scene *s, const lrt_ray4 *rays,
                        lrt_hit4 *hits);
void lrt_tri_intersect8(const lrt_tri_scene *s, const lrt_ray8 *rays,
                        lrt_hit8 *hits);
void lrt_tri_occluded4(const lrt_tri_scene *s, const lrt_ray4 *rays,
                       uint8_t occluded[4]);
void lrt_tri_occluded8(const lrt_tri_scene *s, const lrt_ray8 *rays,
                       uint8_t occluded[8]);

/* Any-hit with a user filter: the filter accepts (return 1) or rejects (return
 * 0) each geometric hit, so the search continues past rejected hits (e.g.
 * alpha-tested foliage shadows). Triangle scenes only. */
typedef int (*lrt_anyhit_filter)(void *user, uint32_t prim_id, float t, float u,
                                 float v);
int lrt_tri_occluded1_filtered(const lrt_tri_scene *s, const lrt_ray *ray,
                               lrt_anyhit_filter filter, void *user);

typedef struct lrt_tri_stats {
    uint32_t node_count; /* wide nodes */
    uint32_t leaf_count; /* leaf references */
    uint32_t max_depth;  /* wide-tree depth */
    size_t memory_bytes; /* nodes + triangle blocks */
    float sah_cost;      /* SAH cost of the wide tree */
} lrt_tri_stats;

void lrt_tri_scene_stats(const lrt_tri_scene *s, lrt_tri_stats *out);

/* Kernel actually selected for this scene, e.g. "bvh8/avx2", "bvh4/sse4",
 * "bvh4/scalar". Useful to detect a scalar fallback caused by missing
 * compiler SIMD flags. */
const char *lrt_tri_kernel_name(const lrt_tri_scene *s);

/* --- Hair / curve scenes ---------------------------------------------------
 *
 * Axis-aligned boxes around long thin diagonal primitives are mostly empty
 * space, which no triangle BVH (object or spatial splits) can remove. The
 * right tool is a dedicated primitive: each hair is a capsule (line segment
 * swept by a sphere of radius r) and is subdivided at build time into short
 * sub-segments whose boxes are tight, while hits still report the original
 * segment.
 *
 * segments = 6*nsegs floats (p0.xyz p1.xyz per segment). radii = per-segment
 * radius array, or NULL to use constant_radius for all. The scene is queried
 * through the same lrt_tri_* intersection functions; hits report
 * prim_id = segment index, u = parameter along the segment in [0,1], v = 0.
 * opts: layout is forced to BVH4 and quality to DEFAULT; max_leaf_size and
 * num_threads apply as for triangles. */
lrt_tri_scene *lrt_curve_scene_build(const float *segments, const float *radii,
                                     float constant_radius, size_t nsegs,
                                     const lrt_tri_build_options *opts,
                                     lrt_result *err);

/* --- Round-linear (Embree-style) hair / curve scenes ----------------------
 *
 * A higher-fidelity hair primitive than the capsule path above: each pair of
 * consecutive points in a strand becomes a tapered cone (radius r0 -> r1)
 * tangent to its two end spheres, and abutting segments are CSG-clipped at the
 * shared joint so the surface does not double up (a direct port of Embree's
 * round_linear_curve intersector). Varying per-point radius is supported.
 *
 * Input is strand-structured: `points` is 3*npoints xyz; `radius` is npoints
 * (or NULL to use `constant_radius`); each strand i spans points
 * [strand_first[i], strand_first[i] + strand_count[i]) and yields
 * strand_count[i]-1 segments (strands with count < 2 are skipped). The scene is
 * queried through the same lrt_tri_* functions; hits report prim_id = global
 * segment index, u = parameter along the segment in [0,1], v = 0. opts: layout
 * is forced to BVH4; max_leaf_size and num_threads apply as for triangles.
 * Serialization / refit / mmap are not supported for these scenes. */
typedef struct lrt_hair_strands {
    const float *points;          /* 3*npoints (xyz) */
    const float *radius;          /* npoints, or NULL to use constant_radius */
    float constant_radius;        /* used when radius == NULL */
    const uint32_t *strand_first; /* nstrands: first point index of each strand */
    const uint32_t *strand_count; /* nstrands: points per strand (>= 2) */
    size_t nstrands;
    size_t npoints;
} lrt_hair_strands;

lrt_tri_scene *lrt_roundcurve_scene_build(const lrt_hair_strands *strands,
                                          const lrt_tri_build_options *opts,
                                          lrt_result *err);

/* Flat (ribbon) linear curves: each strand segment is drawn as a ray-facing
 * ribbon of width 2*radius (varying r0->r1) — Embree's
 * RTC_GEOMETRY_TYPE_FLAT_LINEAR_CURVE for the linear case. Same lrt_hair_strands
 * input as lrt_roundcurve_scene_build; hits report prim_id = segment index,
 * u = parameter along the segment, v = 0. Queried via the lrt_tri_* functions;
 * no serialization / refit / mmap. */
lrt_tri_scene *lrt_flatcurve_scene_build(const lrt_hair_strands *strands,
                                         const lrt_tri_build_options *opts,
                                         lrt_result *err);

/* Round cubic-Bezier curves: a true higher-order tube intersector (recursive
 * subdivision + 2D Newton, a port of Embree's curve_intersector_sweep.h —
 * RTC_GEOMETRY_TYPE_ROUND_BEZIER_CURVE), not a tessellation. cps = 16*nseg
 * floats: per segment, 4 cubic control points each (x,y,z,radius). Segments are
 * independent cubics (share endpoints for C0 continuity). Higher-order bases
 * (B-spline/Catmull-Rom) are supported by converting their control points to
 * Bezier form before calling this. Hits report prim_id = segment index,
 * u = curve parameter in [0,1], v = 0. */
lrt_tri_scene *lrt_bezcurve_scene_build(const float *cps, size_t nseg,
                                        const lrt_tri_build_options *opts,
                                        lrt_result *err);

/* --- Point primitives ------------------------------------------------------
 *
 * A cloud of point primitives, mirroring Embree's point geometry types:
 *   LRT_POINT_SPHERE         analytic sphere      (RTC_GEOMETRY_TYPE_SPHERE_POINT)
 *   LRT_POINT_DISC           ray-facing disc      (RTC_GEOMETRY_TYPE_DISC_POINT)
 *   LRT_POINT_ORIENTED_DISC  fixed-normal disc    (ORIENTED_DISC_POINT)
 *
 * centers = 3*nprims xyz, radii = nprims. normals = 3*nprims (required only for
 * LRT_POINT_ORIENTED_DISC, else may be NULL). Queried via the lrt_tri_*
 * functions; hits report prim_id = point index, u = v = 0. */
typedef enum lrt_tri_point_type {
    LRT_POINT_SPHERE = 0,
    LRT_POINT_DISC = 1,
    LRT_POINT_ORIENTED_DISC = 2
} lrt_tri_point_type;

lrt_tri_scene *lrt_points_scene_build(const float *centers, const float *radii,
                                      const float *normals, int point_type,
                                      size_t nprims,
                                      const lrt_tri_build_options *opts,
                                      lrt_result *err);

/* --- Custom (user) geometry -----------------------------------------------
 *
 * An efficient fp32 custom-primitive path: the BVH broad phase (wide SoA nodes,
 * SIMD slab tests, octant-ordered traversal) stays in the fast kernel, and the
 * caller's intersector is invoked only for leaf candidates that survive a
 * 4-wide fp32 AABB pretest. Each primitive is described to the builder by an
 * fp32 AABB; the callbacks own the actual surface.
 *
 * Thread-safety: a built scene is immutable and any number of threads may query
 * it concurrently *provided the callbacks are re-entrant* (no unsynchronized
 * shared mutable state). The library never serializes callback invocations.
 */

/* Closest-hit test for one user primitive. Called for every candidate the box
 * pretest did not reject. ray->tmax is the current closest-hit bound; report a
 * hit in [ray->tmin, ray->tmax] by writing t (and optional u,v) and returning
 * 1, else return 0. */
typedef int (*lrt_user_intersect_cb)(const lrt_ray *ray, uint32_t prim_id,
                                      void *user, float *t, float *u, float *v);

/* Optional cheaper any-hit test; NULL => the library reuses the closest-hit
 * callback and ignores t/u/v. Return 1 if the primitive is hit anywhere in
 * [ray->tmin, ray->tmax]. */
typedef int (*lrt_user_occluded_cb)(const lrt_ray *ray, uint32_t prim_id,
                                     void *user);

/* Build a BVH over nprims user primitives. aabbs = 6*nprims floats laid out as
 * lo.xyz hi.xyz per primitive (copied; the caller's buffer is not retained).
 * isect is required; occ may be NULL. user is stored and passed to the
 * callbacks at query time. opts->layout is forced to BVH4 and quality is
 * clamped to FAST/DEFAULT (spatial splits are triangle-specific). Query through
 * the same lrt_tri_intersect1 / lrt_tri_occluded1 (and 1N) functions; hits
 * report prim_id = the primitive index and whatever u,v the callback wrote. */
lrt_tri_scene *lrt_user_scene_build(const float *aabbs, size_t nprims,
                                    lrt_user_intersect_cb isect,
                                    lrt_user_occluded_cb occ, void *user,
                                    const lrt_tri_build_options *opts,
                                    lrt_result *err);

/* --- Built-in analytic sphere primitive (fully SIMD) ----------------------
 *
 * spheres = 4*nprims floats: cx cy cz r per sphere (copied). r <= 0 marks a
 * primitive that never hits. Queried through the same lrt_tri_* functions;
 * hits report prim_id = sphere index, t in units of |dir|, and (u,v) = the
 * spherical coordinates of the surface normal (u in [0,1) longitude, v in
 * [0,1] latitude). opts->layout is forced to BVH4. */
lrt_tri_scene *lrt_sphere_scene_build(const float *spheres, size_t nprims,
                                      const lrt_tri_build_options *opts,
                                      lrt_result *err);

/* --- Quantized triangle scenes (approximate / LOD / preview) ---------------
 *
 * Triangle vertices are stored in a low-precision format to cut memory and
 * bandwidth, for large-scene preview / level-of-detail / approximate ray
 * tracing where geometric precision is not required. BVH4 only; queried through
 * the same lrt_tri_intersect1 / lrt_tri_occluded1 (and 1N). Hits report the
 * original triangle index in prim_id; t/u/v are approximate.
 *
 * Spatial queries (closest_point/knn/region/intersect_n), refit, and
 * serialization are NOT supported on quantized scenes.
 */
typedef enum lrt_qtri_format {
    LRT_QTRI_Q16 = 0, /* 16-bit, scene-global grid; near-lossless (~22 B/tri) */
    LRT_QTRI_Q8 = 1,  /* 8-bit uniform, per-leaf grid                (~20 B/tri) */
    LRT_QTRI_FP8 = 2, /* 8-bit E4M3 float, per-leaf grid             (~20 B/tri) */
    LRT_QTRI_FP4 = 3  /* 4-bit E2M1 float, per-leaf grid             (~16 B/tri) */
} lrt_qtri_format;

/* flags */
#define LRT_QTRI_LOSSY 0u        /* smallest; hits are approximate            */
#define LRT_QTRI_CONSERVATIVE 1u /* decoded triangle encloses the true one:
                                  * a transverse ray that hit the true triangle
                                  * never misses (only spurious near-edge hits) */

/* Build a quantized-triangle scene. vertices = 9*ntris floats (v0 v1 v2 per
 * triangle), copied. opts->layout is forced to BVH4, quality clamped to
 * FAST/DEFAULT. */
lrt_tri_scene *lrt_qtri_scene_build(const float *vertices, size_t ntris,
                                    lrt_qtri_format fmt, unsigned flags,
                                    const lrt_tri_build_options *opts,
                                    lrt_result *err);

/* --- Spatial queries (triangle scenes) ------------------------------------
 *
 * These operate on triangle scenes built with lrt_tri_scene_build. For
 * curve/sphere/user scenes they return 0 / empty. All are stateless and
 * thread-safe, and allocate nothing (results go to caller buffers).
 */

typedef struct lrt_point_hit {
    float dist_sq;  /* squared distance from the query point to the surface */
    float point[3]; /* closest point on the nearest primitive */
    uint32_t prim_id;
} lrt_point_hit;

typedef struct lrt_knn_result {
    float dist_sq;
    uint32_t prim_id;
} lrt_knn_result;

/* Nearest primitive to p (exact point-to-triangle distance). Returns 1 and
 * fills *out, or 0 if the scene is empty / not a triangle scene. */
int lrt_tri_closest_point(const lrt_tri_scene *s, const float p[3],
                          lrt_point_hit *out);

/* The k nearest primitives to p, written sorted nearest-first into out[0..cap).
 * Returns the number written (<= min(k, cap)). */
size_t lrt_tri_knn(const lrt_tri_scene *s, const float p[3], uint32_t k,
                   lrt_knn_result *out, size_t cap);

/* Region queries: write the ids of primitives whose AABB overlaps the region
 * into out (up to cap). Returns the number written; a return value == cap means
 * the result may be truncated (re-query with a larger buffer). On HQ/SBVH
 * builds a primitive may be reported more than once. */
size_t lrt_tri_query_aabb(const lrt_tri_scene *s, const float lo[3],
                          const float hi[3], uint32_t *out, size_t cap);
size_t lrt_tri_query_sphere(const lrt_tri_scene *s, const float center[3],
                            float radius, uint32_t *out, size_t cap);

typedef struct lrt_frustum {
    float planes[6][4]; /* {nx,ny,nz,d}; inside half-space: n.p + d >= 0 */
} lrt_frustum;

/* Extract 6 normalized frustum planes from a row-major matrix m with the
 * convention clip = m * [x y z 1]^T and GL clip-space z in [-1, 1] (e.g.
 * m = projection * view). */
void lrt_frustum_from_matrix(const float m[16], lrt_frustum *out);

size_t lrt_tri_query_frustum(const lrt_tri_scene *s, const lrt_frustum *f,
                             uint32_t *out, size_t cap);

/* Up to max_hits closest hits along the ray, sorted front-to-back by t (for
 * transparency / CSG / volume rendering). Returns the number written; out[0]
 * equals lrt_tri_intersect1 for the same ray. Triangle scenes only. */
size_t lrt_tri_intersect_n(const lrt_tri_scene *s, const lrt_ray *ray,
                           lrt_hit *out, size_t max_hits);

/* --- Sphere tracing for implicit surfaces (signed distance fields) --------
 *
 * lrt_sdf_cb evaluates a signed distance field at p (negative inside the
 * surface). The field must be a conservative distance bound (Lipschitz
 * constant <= 1): the marcher advances by |sdf|, so a field that overestimates
 * distance can tunnel through thin features. Divide the field by its Lipschitz
 * constant if it is not unit-Lipschitz.
 */
typedef float (*lrt_sdf_cb)(const float p[3], void *user);

typedef struct lrt_sdf_params {
    unsigned max_steps; /* 0 => 128 */
    float epsilon;      /* surface hit threshold; relative to t when
                           t_eps_scale > 0. 0 => 1e-4 */
    float over_relax;   /* over-relaxation omega in [1,2); 0/1 => classic
                           sphere tracing (Keinert et al. 2014) */
    float t_eps_scale;  /* eps(t) = epsilon * (1 + t_eps_scale * t); 0 => off */
    float normal_eps;   /* finite-difference step for the hit normal;
                           0 => normal not computed */
} lrt_sdf_params;

typedef struct lrt_sdf_hit {
    float t;        /* ray parameter (units of |dir|) of the hit */
    float p[3];     /* hit position */
    float n[3];     /* surface normal (valid only when params->normal_eps > 0) */
    unsigned iters; /* marching iterations used */
    int hit;        /* 1 on hit, 0 on miss */
} lrt_sdf_hit;

/* March one ray through a single SDF (no BVH). dir need not be normalized; the
 * reported t is in units of |dir|. params may be NULL for defaults. Returns 1
 * and fills *out on a surface hit in [tmin, tmax]. */
int lrt_sdf_sphere_trace(const float org[3], const float dir[3], float tmin,
                         float tmax, lrt_sdf_cb sdf, void *user,
                         const lrt_sdf_params *params, lrt_sdf_hit *out);

/* A BVH-accelerated implicit-surface scene. Each blob has a tight AABB and an
 * SDF callback; the broad phase prunes by AABB and marches only within
 * candidate blobs. blobs[] and params are copied (not retained). For
 * overlapping blobs, have each blob's sdf evaluate the GLOBAL union field
 * min_j f_j(p) — the per-blob AABB still prunes the broad phase, and the
 * nearest hit across candidates is reported. Queried through the same lrt_tri_*
 * functions; hits report prim_id = blob index, t, and u = v = 0. */
typedef struct lrt_sdf_blob {
    float aabb[6]; /* lo.xyz hi.xyz */
    lrt_sdf_cb sdf;
    void *user;
} lrt_sdf_blob;

lrt_tri_scene *lrt_sdf_scene_build(const lrt_sdf_blob *blobs, size_t nblobs,
                                   const lrt_sdf_params *params,
                                   const lrt_tri_build_options *opts,
                                   lrt_result *err);

/* --- Serialization (triangle and curve scenes) ----------------------------
 *
 * Nodes and blocks are position-independent (internal integer indices), so a
 * built scene serializes as a raw "LRTS" blob. User/sphere/SDF scenes hold
 * callbacks and cannot be serialized (save returns LRT_RESULT_INVALID_ARGUMENT).
 * Loaded files are validated (magic/version/endianness + every child reference
 * bounds-checked) before any query can run, so a corrupt file is rejected
 * rather than driving traversal out of bounds.
 */

/* Save to a file / heap buffer (caller frees *buf with free()). */
lrt_result lrt_tri_scene_save(const lrt_tri_scene *s, const char *path);
lrt_result lrt_tri_scene_save_to_memory(const lrt_tri_scene *s, void **buf,
                                        size_t *n);

/* Load (copying, fully owned by the returned scene). Returns NULL on error. */
lrt_tri_scene *lrt_tri_scene_load(const char *path, lrt_result *err);
lrt_tri_scene *lrt_tri_scene_load_from_memory(const void *buf, size_t n,
                                              lrt_result *err);

/* Zero-copy: mmap the file read-only and point the scene's nodes/blocks into
 * the mapping. The returned scene is read-only (refit is rejected) and is
 * released normally by lrt_tri_scene_free (which unmaps). Falls back with
 * LRT_RESULT_NOT_BUILT on platforms without mmap. */
lrt_tri_scene *lrt_tri_scene_open_mmap(const char *path, lrt_result *err);

/* --- Refit (animation) ----------------------------------------------------
 *
 * Update vertex positions in place and recompute node bounds without rebuilding
 * the tree. vertices is 9*ntris floats in the ORIGINAL build order, and ntris
 * must equal the original triangle count. Far cheaper than a rebuild; the tree
 * topology is preserved (a large deformation degrades traversal quality but
 * stays correct). Triangle scenes only; rejects mmapped (read-only) scenes. */
lrt_result lrt_tri_scene_refit(lrt_tri_scene *s, const float *vertices,
                               size_t ntris);

/* --- Instancing / TLAS ----------------------------------------------------
 *
 * A top-level acceleration structure over instances of bottom-level scenes
 * (any lrt_tri_scene). Each instance places a BLAS in the world with a 3x4
 * row-major affine transform (rows of [3x3 linear | translation], so a point
 * maps as p' = M*p + t). The BLAS scenes are NOT copied and must outlive the
 * TLAS. Queries are stateless and thread-safe.
 */
typedef struct lrt_tlas lrt_tlas;

typedef struct lrt_instance {
    uint32_t blas_id;     /* index into the blas[] array passed to build */
    float obj2world[12];  /* row-major 3x4 affine object->world */
    uint32_t instance_id; /* reported back on hit */
    uint32_t mask;        /* visibility bits; instance skipped if (mask & ray_mask)==0 */
} lrt_instance;

typedef struct lrt_tlas_hit {
    float t, u, v;
    uint32_t prim_id; /* triangle/primitive index within the hit BLAS */
    uint32_t inst_id; /* instance_id of the hit instance */
} lrt_tlas_hit;

/* Build a TLAS. Instances with a (near-)singular transform are skipped. */
lrt_tlas *lrt_tlas_build(lrt_tri_scene *const *blas, size_t nblas,
                         const lrt_instance *insts, size_t ninsts,
                         const lrt_tri_build_options *opts, lrt_result *err);
void lrt_tlas_free(lrt_tlas *t);

/* Closest hit / any hit. ray_mask is ANDed with each instance mask (pass
 * 0xFFFFFFFF to test all). Returns 1 on hit. */
int lrt_tlas_intersect1(const lrt_tlas *t, const lrt_ray *ray, uint32_t ray_mask,
                        lrt_tlas_hit *hit);
int lrt_tlas_occluded1(const lrt_tlas *t, const lrt_ray *ray, uint32_t ray_mask);

/* Update instance transforms (same count/blas_ids) and rebuild the small TLAS
 * BVH over the new world bounds. */
lrt_result lrt_tlas_refit(lrt_tlas *t, const lrt_instance *insts, size_t ninsts);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTRT_C_TRI_H */
