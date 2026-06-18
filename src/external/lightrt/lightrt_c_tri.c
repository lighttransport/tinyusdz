/*
 * lightrt_c_tri.c - C11 triangle-native fp32 wide BVH (implementation).
 *
 * Pipeline:
 *   1. Precompute per-triangle AABBs + centroids (one pass, arena allocated).
 *   2. Build a binary BVH with binned SAH (16 bins, all three axes accumulated
 *      in a single pass over the primitives of each node).
 *   3. Collapse the binary tree into a wide BVH (4- or 8-ary) with SoA child
 *      bounds, emitted in DFS order. Leaf triangles are swizzled into 4-wide
 *      SoA blocks (lrt_tri4) with precomputed edges; the prim-index
 *      indirection of the binary tree dies here.
 *
 * Traversal: one ray vs all children of a wide node per step (scalar, SSE4 for
 * BVH4, AVX2 for BVH8), ordered stack with deferred tnear culling, 4-wide SoA
 * Moller-Trumbore per leaf block.
 *
 * The slab test uses (bound - org) * invd with infinite invd components
 * replaced by +/-FLT_MAX at ray setup, which cannot produce NaN (0 * FLT_MAX
 * is 0), so the SIMD min/max slab needs no NaN handling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1 /* madvise + MADV_HUGEPAGE despite _POSIX_C_SOURCE */
#endif

#include "lightrt_c_tri.h"

#include <float.h>
#include <math.h>
#include <stdio.h> /* serialization (fopen/fread/fwrite) */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define LRT_TRI_HAS_MMAP 1
#else
#define LRT_TRI_HAS_MMAP 0
#endif

/* ------------------------------------------------------------------------- */
/* SIMD detection (compile-time; scalar fallback always available).          */
/* ------------------------------------------------------------------------- */
#if defined(__AVX2__) && defined(__FMA__)
#define LRT_TRI_HAS_AVX2 1
#else
#define LRT_TRI_HAS_AVX2 0
#endif
#if defined(__SSE4_1__) || LRT_TRI_HAS_AVX2
#define LRT_TRI_HAS_SSE4 1
#else
#define LRT_TRI_HAS_SSE4 0
#endif

#if LRT_TRI_HAS_SSE4
#include <immintrin.h>
#endif

/* ------------------------------------------------------------------------- */
/* Tunables.                                                                 */
/* ------------------------------------------------------------------------- */
#define TRI_NUM_BINS 16u
#define TRI_DEFAULT_LEAF 60u
#define TRI_MAX_LEAF 60u /* hard cap from the leaf-ref encoding (15 blocks) */
#define TRI_TRAV_COST 1.0f
#define TRI_ISECT_COST 1.0f
#define TRI_MAX_DEPTH 96u
#define TRI_MEDIAN_DEPTH 48u /* force median splits beyond this depth */
#define TRI_ISECT_DET_EPS_SCALE 1e-8f
#define TRI_ISECT_DET_EPS_MIN 1e-12f
#define TRI_ISECT_DET_EPS_MAX 1e-6f
/* Wide-tree depth <= binary depth (TRI_MAX_DEPTH); each level leaves at most
 * width-1 entries on the stack -> 96*7 = 672 worst case for BVH8. */
#define TRI_STACK_SIZE 1024
#define TRI_INF_F 3.402823466e+38f

/* Child reference: bit31 = leaf. Inner: bits30..0 = wide-node index.
 * Leaf: bits30..4 = first lrt_tri4 block index, bits3..0 = block count. */
#define TRI_REF_LEAF_BIT 0x80000000u
#define TRI_REF_IS_LEAF(r) (((r) & TRI_REF_LEAF_BIT) != 0u)
#define TRI_REF_NODE(r) ((r) & 0x7FFFFFFFu)
#define TRI_REF_BLOCK(r) (((r) & 0x7FFFFFFFu) >> 4)
#define TRI_REF_NBLOCKS(r) ((r) & 0xFu)
#define TRI_MAKE_LEAF_REF(block, nblocks) \
    (TRI_REF_LEAF_BIT | ((uint32_t)(block) << 4) | (uint32_t)(nblocks))
#define TRI_MAKE_NODE_REF(idx) ((uint32_t)(idx))

/* ------------------------------------------------------------------------- */
/* Data structures.                                                          */
/* ------------------------------------------------------------------------- */

/* 4-wide node: SoA child bounds, 128 bytes = 2 cache lines.
 * perm[octant] packs an approximate front-to-back slot order (2 bits per
 * position, nearest first) for rays whose direction-sign octant is `octant`;
 * traversal iterates it instead of sorting child tnear values per node. */
typedef struct lrt_bvh4_node {
    float lo_x[4], lo_y[4], lo_z[4];
    float hi_x[4], hi_y[4], hi_z[4];
    uint32_t child[4]; /* child refs; empty slots: 0 with inverted bounds */
    uint32_t nchildren;
    uint8_t perm[8]; /* perm[octant]: slot at position p = (perm >> 2p) & 3 */
    uint32_t _pad;
} lrt_bvh4_node;

/* 8-wide node: 256 bytes = 4 cache lines. */
typedef struct lrt_bvh8_node {
    float lo_x[8], lo_y[8], lo_z[8];
    float hi_x[8], hi_y[8], hi_z[8];
    uint32_t child[8];
    uint32_t nchildren;
    uint32_t _pad[7];
} lrt_bvh8_node;

/* Quantized 8-wide node: child bounds as 8-bit offsets on a node-local grid
 * (org + q * scale, quantization rounded outward so decoded boxes always
 * contain the true ones). 128 bytes = 2 cache lines. */
typedef struct lrt_bvh8q_node {
    float org[3];
    float scale[3];
    uint32_t nchildren;
    uint8_t qlo_x[8], qlo_y[8], qlo_z[8]; /* empty slots: qlo=255 */
    uint8_t qhi_x[8], qhi_y[8], qhi_z[8]; /* empty slots: qhi=0   */
    uint32_t child[8];
    uint32_t _pad[5];
} lrt_bvh8q_node;

/* 4-bit quantized 8-wide node (96 bytes): child bounds as 4-bit offsets, two
 * children packed per byte (child c -> q*[a][c>>1], (c&1)?high:low nibble).
 * Empty slots: qlo nibble 15, qhi nibble 0 (decode inverted). */
typedef struct lrt_bvh8q4_node {
    float org[3];
    float scale[3];
    uint32_t nchildren;
    uint8_t qlo[3][4]; /* [axis][packed]; 8 nibbles per axis */
    uint8_t qhi[3][4];
    uint32_t child[8];
    uint32_t _pad[3];
} lrt_bvh8q4_node;
_Static_assert(sizeof(lrt_bvh8q4_node) == 96, "lrt_bvh8q4_node");

/* Triangle blocks: SoA with precomputed edges, sized to the traversal SIMD
 * width (4 for BVH4/SSE, 8 for BVH8/AVX2). Both share the same generic float
 * layout - 9 arrays of `width` floats (v0xyz, e1xyz, e2xyz) followed by
 * `width` uint32 prim ids - so scalar code can address either width. Padding
 * lanes carry prim_id = LRT_TRI_NO_HIT and all-zero vertices (zero det ->
 * never hit). lrt_tri4 = 160 bytes, lrt_tri8 = 320 bytes. */
typedef struct lrt_tri4 {
    float v0x[4], v0y[4], v0z[4];
    float e1x[4], e1y[4], e1z[4];
    float e2x[4], e2y[4], e2z[4];
    uint32_t prim_id[4];
} lrt_tri4;

typedef struct lrt_tri8 {
    float v0x[8], v0y[8], v0z[8];
    float e1x[8], e1y[8], e1z[8];
    float e2x[8], e2y[8], e2z[8];
    uint32_t prim_id[8];
} lrt_tri8;

/* Capsule (hair sub-segment) leaf block: same 160-byte footprint as lrt_tri4,
 * so curve scenes reuse the triangle block allocator and leaf-ref encoding.
 * u0/u1 map a hit back to the parent segment's [0,1] parameter range.
 * Padding lanes: prim = LRT_TRI_NO_HIT, rad = 0 (never hits). */
typedef struct lrt_crv4 {
    float p0x[4], p0y[4], p0z[4];
    float dx[4], dy[4], dz[4]; /* p1 - p0 */
    float rad[4];
    float u0[4], u1[4];
    uint32_t prim[4];
} lrt_crv4;

/* User-geometry leaf block (160 bytes = tri_block_size(4)): per-lane primitive
 * AABB for a 4-wide SIMD box pretest before the (possibly expensive) callback,
 * plus the original prim id. Padding lanes: prim = LRT_TRI_NO_HIT, box inverted
 * (lo=+INF, hi=-INF) so the slab pretest never selects them. */
typedef struct lrt_user4 {
    float lo_x[4], lo_y[4], lo_z[4];
    float hi_x[4], hi_y[4], hi_z[4];
    uint32_t prim[4];
    uint32_t _pad[12];
} lrt_user4;

/* Analytic sphere leaf block (160 bytes = tri_block_size(4)): center + radius
 * per lane. Padding lanes: r = 0, prim = LRT_TRI_NO_HIT (never hit). */
typedef struct lrt_sph4 {
    float cx[4], cy[4], cz[4], r[4];
    uint32_t prim[4];
    uint32_t _pad[20];
} lrt_sph4;

/* New leaf blocks must match the triangle block stride so they reuse the
 * leaf allocator, the leaf-ref encoding, and tri_block_floats addressing. */
_Static_assert(sizeof(lrt_user4) == 160, "lrt_user4 must equal tri_block_size(4)");
_Static_assert(sizeof(lrt_sph4) == 160, "lrt_sph4 must equal tri_block_size(4)");

/* Round-linear curve leaf block (Embree round_linear_curve port): per lane a
 * tapered cone v0=(p0,r0) -> v1=(p1,r1) tangent to its two end spheres, plus the
 * strand neighbors vL,vR (xyz+radius) used to CSG-clip joints. A neighbor with
 * px == +INF marks a strand endpoint (no neighbor). Unlike the capsule leaf this
 * carries its own (larger) stride and is addressed via block_stride, like the
 * quantized leaves. Padding lanes: prim = LRT_TRI_NO_HIT, r0=r1=0 (never hit). */
typedef struct lrt_rlc4 {
    float p0x[4], p0y[4], p0z[4], r0[4];
    float p1x[4], p1y[4], p1z[4], r1[4];
    float pLx[4], pLy[4], pLz[4], rL[4];
    float pRx[4], pRy[4], pRz[4], rR[4];
    uint32_t prim[4];
} lrt_rlc4;
_Static_assert(sizeof(lrt_rlc4) == 272, "lrt_rlc4");

/* Point leaf block (sphere/disc/oriented-disc): center + radius + normal (only
 * used by ORIENTED_DISC) per lane. 160 bytes = tri_block_size(4), so it reuses
 * the default leaf allocator + tri_block_floats addressing. Padding lanes: r=0,
 * prim = LRT_TRI_NO_HIT (never hit). */
typedef struct lrt_point4 {
    float cx[4], cy[4], cz[4], r[4];
    float nx[4], ny[4], nz[4];
    uint32_t prim[4];
    uint32_t _pad[8];
} lrt_point4;
_Static_assert(sizeof(lrt_point4) == 160, "lrt_point4 must equal tri_block_size(4)");

/* Flat (ribbon) linear curve leaf block: a segment v0=(p0,r0) -> v1=(p1,r1)
 * rendered as a ray-facing ribbon quad. Own 144-byte stride (addressed via
 * block_stride like lrt_rlc4). Padding lanes: r0=r1=0, prim = LRT_TRI_NO_HIT. */
typedef struct lrt_flat4 {
    float p0x[4], p0y[4], p0z[4], r0[4];
    float p1x[4], p1y[4], p1z[4], r1[4];
    uint32_t prim[4];
} lrt_flat4;
_Static_assert(sizeof(lrt_flat4) == 144, "lrt_flat4");

/* Round cubic-Bezier curve leaf block: 4 curves, each 4 control points (xyz +
 * radius). Own 272-byte stride (addressed via block_stride). Padding lanes:
 * all radii 0, prim = LRT_TRI_NO_HIT. */
typedef struct lrt_bez4 {
    float b0x[4], b0y[4], b0z[4], b0r[4];
    float b1x[4], b1y[4], b1z[4], b1r[4];
    float b2x[4], b2y[4], b2z[4], b2r[4];
    float b3x[4], b3y[4], b3z[4], b3r[4];
    uint32_t prim[4];
} lrt_bez4;
_Static_assert(sizeof(lrt_bez4) == 272, "lrt_bez4");

/* Quantized triangle leaf blocks (4 tris each; store quantized absolute
 * v0,v1,v2 -> decode -> e1=v1-v0, e2=v2-v0 -> Moller-Trumbore). These have their
 * own (smaller, format-specific) stride; the scene carries block_stride and the
 * qtri kernels address blocks by it instead of tri_block_size(). */
typedef struct lrt_qtri16 { /* 16-bit, scene-global grid. 88 bytes. */
    uint16_t qv0x[4], qv0y[4], qv0z[4];
    uint16_t qv1x[4], qv1y[4], qv1z[4];
    uint16_t qv2x[4], qv2y[4], qv2z[4];
    uint32_t prim_id[4];
} lrt_qtri16;
_Static_assert(sizeof(lrt_qtri16) == 88, "lrt_qtri16");

typedef struct lrt_qtri8 { /* 8-bit (q8 uniform / fp8 E4M3), per-block grid. 80 bytes. */
    float org[3], scale[3];
    uint8_t qv0x[4], qv0y[4], qv0z[4];
    uint8_t qv1x[4], qv1y[4], qv1z[4];
    uint8_t qv2x[4], qv2y[4], qv2z[4];
    uint32_t prim_id[4];
    uint32_t _pad;
} lrt_qtri8;
_Static_assert(sizeof(lrt_qtri8) == 80, "lrt_qtri8");

typedef struct lrt_qtri4 { /* 4-bit E2M1, per-block grid (org=center, scale=halfext). 64 bytes. */
    float org[3], scale[3];
    uint8_t nib[18]; /* 9 coords x 4 nibble-lanes (2 lanes/byte) */
    uint8_t _pad6[6];
    uint32_t prim_id[4];
} lrt_qtri4;
_Static_assert(sizeof(lrt_qtri4) == 64, "lrt_qtri4");

static inline uint32_t lrt_qtri_block_stride(int fmt) {
    switch (fmt) {
        case LRT_QTRI_Q16: return (uint32_t)sizeof(lrt_qtri16);
        case LRT_QTRI_FP4: return (uint32_t)sizeof(lrt_qtri4);
        default: return (uint32_t)sizeof(lrt_qtri8); /* Q8, FP8 */
    }
}

/* Byte-typed block addressing by explicit stride (for quantized leaves). */
static inline void *tri_block_ptr(void *blocks, uint32_t idx, uint32_t stride) {
    return (char *)blocks + (size_t)idx * stride;
}

/* Primitive kind stored in lrt_tri_scene.prim_kind / selected by the builders. */
enum {
    TRI_PRIM_TRI = 0,   /* triangle leaves (lrt_tri4 / lrt_tri8)        */
    TRI_PRIM_CURVE = 1, /* capsule leaves (lrt_crv4); legacy s->curve   */
    TRI_PRIM_USER = 2,  /* user callback leaves (lrt_user4)             */
    TRI_PRIM_SPHERE = 3,/* analytic sphere leaves (lrt_sph4)            */
    TRI_PRIM_QTRI = 4,  /* quantized triangle leaves (lrt_qtri*)        */
    TRI_PRIM_RLCURVE = 5,/* round-linear curve leaves (lrt_rlc4)        */
    TRI_PRIM_POINT = 6, /* point leaves (lrt_point4): sphere/disc/odisc */
    TRI_PRIM_FLATCURVE = 7, /* flat (ribbon) linear curve leaves (lrt_flat4) */
    TRI_PRIM_BEZCURVE = 8 /* round cubic Bezier curve leaves (lrt_bez4) */
};

/* Point-primitive kind (lrt_tri_scene.point_type), mirroring Embree's point
 * geometry types. Public values in lrt_tri_point_type (lightrt_c_tri.h). */
enum {
    TRI_POINT_SPHERE = 0,       /* analytic sphere (RTC_GEOMETRY_TYPE_SPHERE_POINT) */
    TRI_POINT_DISC = 1,         /* ray-facing disc (RTC_GEOMETRY_TYPE_DISC_POINT) */
    TRI_POINT_ORIENTED_DISC = 2 /* fixed-normal disc (ORIENTED_DISC_POINT) */
};

/* Build-time sub-segment (after hair subdivision). */
typedef struct tri_subseg {
    float p0[3], p1[3];
    float r, u0, u1;
    uint32_t prim;
} tri_subseg;

/* Build-time round-linear curve segment: endpoints + radii + strand neighbors.
 * pL[0]/pR[0] == +INF marks a strand start/end (no neighbor to clip against). */
typedef struct tri_rlcseg {
    float p0[3], r0;
    float p1[3], r1;
    float pL[3], rL;
    float pR[3], rR;
    uint32_t prim;
} tri_rlcseg;

struct lrt_tri_scene {
    int layout;    /* traversal width: 4 or 8 */
    int quantized; /* nodes8q used instead of nodes8 */
    int curve;     /* leaf blocks are lrt_crv4 capsules, not triangles */
    int prim_kind; /* TRI_PRIM_*: discriminates the leaf type / dispatch */
    int point_type; /* TRI_POINT_* (only for prim_kind == TRI_PRIM_POINT) */
    uint32_t root;
    uint32_t original_ntris; /* original triangle count (for refit validation) */
    lrt_bvh4_node *nodes4;
    lrt_bvh8_node *nodes8;
    lrt_bvh8q_node *nodes8q;
    lrt_bvh8q4_node *nodes8q4;
    int qnode; /* node-bound quant: 0=linear8(BVH8Q), 1=fp8(E4M3), 2=q4 */
    uint32_t node_count;
    void *blocks; /* lrt_tri4[] when layout==4, lrt_tri8[] when layout==8 */
    uint32_t block_count;
    uint32_t block_stride; /* bytes per leaf block (varies for quantized leaves) */
    lrt_tri_stats stats;
    const char *kernel_name;
    /* Quantized-triangle leaves (prim_kind == TRI_PRIM_QTRI). */
    int qfmt;           /* lrt_qtri_format */
    uint32_t qflags;    /* LRT_QTRI_* */
    float qgrid_org[3]; /* scene-global grid (q16 only) */
    float qgrid_scale[3];
    /* Custom-geometry callbacks (prim_kind == TRI_PRIM_USER, incl. SDF). */
    lrt_user_intersect_cb user_isect;
    lrt_user_occluded_cb user_occ; /* NULL => reuse user_isect */
    void *user_ptr;
    void *owned_user; /* heap table freed by scene_free (e.g. SDF), else NULL */
    /* Serialization / mmap ownership (Workstream E). */
    int mem_mapped;   /* 1 => nodes/blocks alias map_base; do not free them */
    void *map_base;
    size_t map_size;
    /* Object-space scene AABB (root bounds), for TLAS + serialization. */
    float root_lo[3], root_hi[3];
};

static inline size_t tri_block_size(int width) {
    return width == 4 ? sizeof(lrt_tri4) : sizeof(lrt_tri8);
}

/* Generic view of a block: 9 float arrays then the prim_id array. */
static inline const float *tri_block_floats(const void *blocks, uint32_t idx,
                                            int width) {
    return (const float *)((const char *)blocks +
                           (size_t)idx * tri_block_size(width));
}

/* Binary build node (arena, freed after collapse). */
typedef struct tri_bnode {
    float lo[3], hi[3];
    uint32_t a, b;  /* inner: left/right node indices; leaf: a = index offset */
    uint32_t count; /* 0 = inner, >0 = leaf primitive count */
} tri_bnode;

/* Build context. For parallel builds each worker gets a copy with a private
 * [node_next, node_end) slice of the shared bnodes arena and a disjoint range
 * of the shared indices array, so no synchronization is needed. */
typedef struct tri_build_ctx {
    const float *verts; /* caller soup, 9*ntris (NULL for curve scenes) */
    const tri_subseg *subsegs; /* curve sub-segments (NULL for triangles) */
    const tri_rlcseg *rlcsegs; /* round-linear curve segs (TRI_PRIM_RLCURVE) */
    size_t ntris;
    float *plo;      /* 3*ntris */
    float *phi;      /* 3*ntris */
    float *cen;      /* 3*ntris */
    uint32_t *indices;
    tri_bnode *bnodes;  /* shared arena */
    uint32_t node_next; /* this worker's allocation cursor */
    uint32_t node_end;  /* end of this worker's slice */
    uint32_t max_leaf;
    uint32_t block_shift; /* log2 of the leaf SIMD width (2 or 3) */
    lrt_tri_quality quality;
    int failed;
    /* intra-node parallelism for the big nodes near the root (serial part of
     * the subtree-parallel build); 0/1 = off */
    unsigned par_threads;
    uint32_t *par_scratch; /* ntris u32 scatter buffer (when par_threads > 1) */
    /* LBVH (LRT_TRI_BUILD_FAST): Morton-sorted keys, (morton30 << 32) | prim,
     * index-aligned with the sorted indices[]. NULL for SAH builds. */
    const uint64_t *lbvh_keys;
    uint32_t lbvh_leaf; /* LBVH leaf size (fixed; no SAH leaf decision) */
    /* Alternative leaf sources (NULL for triangle/curve builds). emit_kind
     * selects the tri_emit_leaf branch. */
    const float *user_aabbs; /* 6*nprims: lo.xyz hi.xyz (TRI_PRIM_USER) */
    const float *spheres;    /* 4*nprims: cx cy cz r     (TRI_PRIM_SPHERE) */
    const float *points_xyz; /* 3*nprims centers (TRI_PRIM_POINT) */
    const float *points_r;   /* nprims radii    (TRI_PRIM_POINT) */
    const float *points_n;   /* 3*nprims normals or NULL (TRI_PRIM_POINT) */
    const float *bez_cps;    /* 16*nseg ORIGINAL Bezier CPs (TRI_PRIM_BEZCURVE) */
    uint32_t bez_split;      /* build-time de Casteljau pieces per cubic (>=1) */
    int emit_kind;           /* TRI_PRIM_* */
} tri_build_ctx;

/* ------------------------------------------------------------------------- */
/* Helpers.                                                                  */
/* ------------------------------------------------------------------------- */

#if defined(__linux__)
#include <sys/mman.h>
#endif

/* Threshold above which allocations are 2MB-aligned and madvised for
 * transparent huge pages. Incoherent rays walk the BVH randomly; with 4KB
 * pages a multi-MB structure overwhelms the dTLB, with 2MB pages it fits. */
#define TRI_HUGE_PAGE_SIZE (2u * 1024u * 1024u)

static void *tri_aligned_alloc(size_t align, size_t size) {
#if defined(__linux__) && defined(MADV_HUGEPAGE) && !defined(LRT_TRI_NO_HUGEPAGE)
    if (size >= TRI_HUGE_PAGE_SIZE) {
        size_t a = TRI_HUGE_PAGE_SIZE;
        size_t sz = (size + a - 1u) & ~(a - 1u);
        void *p = aligned_alloc(a, sz);
        if (p) {
            (void)madvise(p, sz, MADV_HUGEPAGE);
            return p;
        }
        /* fall through to the normal path on failure */
    }
#endif
    size = (size + align - 1u) & ~(align - 1u);
#if defined(_MSC_VER)
    return _aligned_malloc(size, align);
#else
    return aligned_alloc(align, size);
#endif
}

static void tri_aligned_free(void *p) {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    free(p);
#endif
}

static inline float tri_minf(float a, float b) { return a < b ? a : b; }
static inline float tri_maxf(float a, float b) { return a > b ? a : b; }

static inline float tri_surface_area(const float lo[3], const float hi[3]) {
    float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    if (dx < 0.0f || dy < 0.0f || dz < 0.0f) return 0.0f;
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

static inline void tri_box_reset(float lo[3], float hi[3]) {
    lo[0] = lo[1] = lo[2] = TRI_INF_F;
    hi[0] = hi[1] = hi[2] = -TRI_INF_F;
}

static inline void tri_box_expand(float lo[3], float hi[3], const float plo[3],
                                  const float phi[3]) {
    for (int a = 0; a < 3; a++) {
        lo[a] = tri_minf(lo[a], plo[a]);
        hi[a] = tri_maxf(hi[a], phi[a]);
    }
}

static inline int tri_longest_axis(const float lo[3], const float hi[3]) {
    float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    if (dx > dy && dx > dz) return 0;
    if (dy > dz) return 1;
    return 2;
}

/* Leaves are intersected one SIMD block at a time, so SAH counts blocks of
 * the traversal width, not triangles (Embree's blockSize model). */
static inline float tri_sah_blocks(uint32_t n, uint32_t block_shift) {
    return (float)((n + (1u << block_shift) - 1u) >> block_shift);
}

/* SAH bin (file scope: shared by the serial and parallel binning paths). */
typedef struct tri_bin {
    float lo[3], hi[3];
    uint32_t count;
} tri_bin;

/* ------------------------------------------------------------------------- */
/* Parallel-for helper (C11 threads).                                        */
/* ------------------------------------------------------------------------- */
#if !defined(__STDC_NO_THREADS__)
#include <threads.h>

#define TRI_PAR_NODE_MIN (1u << 16) /* intra-node parallelism above this */
#define TRI_PAR_MAX_THREADS 32u
/* Binning/partition passes are memory-bound and spawn threads per call;
 * beyond ~8 helpers they cost more than they return (measured on a 16-core
 * Threadripper 1950X: 8 threads beat 16/32 for the intra-node passes). */
#define TRI_PAR_NODE_THREADS 8u

typedef struct tri_pfor_job {
    void (*fn)(void *arg, unsigned chunk, uint32_t begin, uint32_t end);
    void *arg;
    uint32_t n;
    unsigned nchunks;
    atomic_uint next;
} tri_pfor_job;

static int tri_pfor_worker(void *p) {
    tri_pfor_job *job = (tri_pfor_job *)p;
    for (;;) {
        unsigned c =
            atomic_fetch_add_explicit(&job->next, 1u, memory_order_relaxed);
        if (c >= job->nchunks) return 0;
        uint32_t begin = (uint32_t)(((uint64_t)job->n * c) / job->nchunks);
        uint32_t end = (uint32_t)(((uint64_t)job->n * (c + 1u)) / job->nchunks);
        job->fn(job->arg, c, begin, end);
    }
}

/* Run fn over [0,n) split into `threads` contiguous chunks. Chunk boundaries
 * depend only on (n, threads), so results are deterministic. */
static void tri_parallel_for(uint32_t n, unsigned threads,
                             void (*fn)(void *, unsigned, uint32_t, uint32_t),
                             void *arg) {
    if (threads > TRI_PAR_MAX_THREADS) threads = TRI_PAR_MAX_THREADS;
    if (threads < 1) threads = 1;
    tri_pfor_job job = {fn, arg, n, threads, 0};
    atomic_init(&job.next, 0u);
    thrd_t tids[TRI_PAR_MAX_THREADS];
    unsigned spawned = 0;
    for (unsigned i = 0; i + 1 < threads; i++) {
        if (thrd_create(&tids[i], tri_pfor_worker, &job) != thrd_success) break;
        spawned++;
    }
    tri_pfor_worker(&job); /* the calling thread participates */
    for (unsigned i = 0; i < spawned; i++) thrd_join(tids[i], NULL);
}
#endif /* !__STDC_NO_THREADS__ */

/* ------------------------------------------------------------------------- */
/* Binary SAH build.                                                         */
/* ------------------------------------------------------------------------- */

#if !defined(__STDC_NO_THREADS__)
/* Parallel 3-axis binning over indices[first+begin, first+end) per chunk. */
typedef struct tri_bin_job {
    const tri_build_ctx *c;
    uint32_t first;
    const float *amin;
    const float *ascale;
    const int *axis_ok;
    tri_bin (*bins)[3][TRI_NUM_BINS]; /* one set per chunk */
} tri_bin_job;

static void tri_bin_chunk(void *arg, unsigned chunk, uint32_t begin,
                          uint32_t end) {
    tri_bin_job *j = (tri_bin_job *)arg;
    const tri_build_ctx *c = j->c;
    tri_bin(*bins)[TRI_NUM_BINS] = j->bins[chunk];
    for (int a = 0; a < 3; a++) {
        for (uint32_t b = 0; b < TRI_NUM_BINS; b++) {
            tri_box_reset(bins[a][b].lo, bins[a][b].hi);
            bins[a][b].count = 0;
        }
    }
    for (uint32_t i = begin; i < end; i++) {
        uint32_t p = c->indices[j->first + i];
        const float *pl = &c->plo[(size_t)p * 3];
        const float *ph = &c->phi[(size_t)p * 3];
        const float *pc = &c->cen[(size_t)p * 3];
        for (int a = 0; a < 3; a++) {
            if (!j->axis_ok[a]) continue;
            uint32_t b = (uint32_t)((pc[a] - j->amin[a]) * j->ascale[a]);
            if (b >= TRI_NUM_BINS) b = TRI_NUM_BINS - 1;
            tri_box_expand(bins[a][b].lo, bins[a][b].hi, pl, ph);
            bins[a][b].count++;
        }
    }
}

/* Parallel two-pass partition: count per chunk, prefix-sum, scatter into the
 * scratch buffer, copy back. */
typedef struct tri_part_job {
    tri_build_ctx *c;
    uint32_t first;
    int axis;
    float pos;
    uint32_t nleft[TRI_PAR_MAX_THREADS];
    uint32_t left_off[TRI_PAR_MAX_THREADS];
    uint32_t right_off[TRI_PAR_MAX_THREADS];
    uint32_t mid;
} tri_part_job;

static void tri_part_count(void *arg, unsigned chunk, uint32_t begin,
                           uint32_t end) {
    tri_part_job *j = (tri_part_job *)arg;
    const tri_build_ctx *c = j->c;
    uint32_t nl = 0;
    for (uint32_t i = begin; i < end; i++) {
        uint32_t p = c->indices[j->first + i];
        nl += c->cen[(size_t)p * 3 + j->axis] < j->pos;
    }
    j->nleft[chunk] = nl;
}

static void tri_part_scatter(void *arg, unsigned chunk, uint32_t begin,
                             uint32_t end) {
    tri_part_job *j = (tri_part_job *)arg;
    const tri_build_ctx *c = j->c;
    uint32_t *out = c->par_scratch + j->first;
    uint32_t lo = j->left_off[chunk];
    uint32_t ro = j->mid + j->right_off[chunk];
    for (uint32_t i = begin; i < end; i++) {
        uint32_t p = c->indices[j->first + i];
        if (c->cen[(size_t)p * 3 + j->axis] < j->pos) {
            out[lo++] = p;
        } else {
            out[ro++] = p;
        }
    }
}

static void tri_part_copyback(void *arg, unsigned chunk, uint32_t begin,
                              uint32_t end) {
    tri_part_job *j = (tri_part_job *)arg;
    (void)chunk;
    memcpy(j->c->indices + j->first + begin, j->c->par_scratch + j->first + begin,
           (size_t)(end - begin) * sizeof(uint32_t));
}
#endif /* !__STDC_NO_THREADS__ */

/* Per-triangle AABB + centroid precompute. Returns non-zero on non-finite
 * input. Parallelized by chunks when threads > 1. */
typedef struct tri_precompute_job {
    tri_build_ctx *c;
    atomic_int bad;
} tri_precompute_job;

static void tri_precompute_chunk_impl(tri_build_ctx *c, uint32_t begin,
                                      uint32_t end, int *bad_out) {
    int bad = 0;
    for (uint32_t i = begin; i < end; i++) {
        const float *v = &c->verts[(size_t)i * 9];
        for (int a = 0; a < 3; a++) {
            /* check the vertices, not min/max results: minf/maxf comparisons
             * are false for NaN and would swallow it */
            if (!isfinite(v[a]) || !isfinite(v[3 + a]) || !isfinite(v[6 + a])) {
                bad = 1;
            }
            float lo = tri_minf(v[a], tri_minf(v[3 + a], v[6 + a]));
            float hi = tri_maxf(v[a], tri_maxf(v[3 + a], v[6 + a]));
            c->plo[(size_t)i * 3 + a] = lo;
            c->phi[(size_t)i * 3 + a] = hi;
            c->cen[(size_t)i * 3 + a] = 0.5f * (lo + hi);
        }
        c->indices[i] = i;
    }
    if (bad) *bad_out = 1;
}

#if !defined(__STDC_NO_THREADS__)
static void tri_precompute_chunk(void *arg, unsigned chunk, uint32_t begin,
                                 uint32_t end) {
    (void)chunk;
    tri_precompute_job *j = (tri_precompute_job *)arg;
    int bad = 0;
    tri_precompute_chunk_impl(j->c, begin, end, &bad);
    if (bad) atomic_store_explicit(&j->bad, 1, memory_order_relaxed);
}
#endif

static int tri_precompute(tri_build_ctx *c, unsigned threads) {
#if !defined(__STDC_NO_THREADS__)
    if (threads > 1 && c->ntris >= TRI_PAR_NODE_MIN) {
        tri_precompute_job j;
        j.c = c;
        atomic_init(&j.bad, 0);
        tri_parallel_for((uint32_t)c->ntris, threads, tri_precompute_chunk, &j);
        return atomic_load(&j.bad);
    }
#else
    (void)threads;
#endif
    int bad = 0;
    tri_precompute_chunk_impl(c, 0, (uint32_t)c->ntris, &bad);
    return bad;
}

/* Partition indices[first, first+num) by centroid[axis] < pos; returns the
 * left-side count. Uses the parallel path for large nodes when enabled. */
static uint32_t tri_partition(tri_build_ctx *c, uint32_t first, uint32_t num,
                              int axis, float pos) {
#if !defined(__STDC_NO_THREADS__)
    if (c->par_threads > 1 && c->par_scratch && num >= TRI_PAR_NODE_MIN) {
        tri_part_job j;
        j.c = c;
        j.first = first;
        j.axis = axis;
        j.pos = pos;
        unsigned threads = c->par_threads;
        if (threads > TRI_PAR_NODE_THREADS) threads = TRI_PAR_NODE_THREADS;
        tri_parallel_for(num, threads, tri_part_count, &j);
        uint32_t mid = 0, racc = 0;
        for (unsigned t = 0; t < threads; t++) mid += j.nleft[t];
        uint32_t lacc = 0;
        for (unsigned t = 0; t < threads; t++) {
            uint32_t begin = (uint32_t)(((uint64_t)num * t) / threads);
            uint32_t end = (uint32_t)(((uint64_t)num * (t + 1u)) / threads);
            j.left_off[t] = lacc;
            j.right_off[t] = racc;
            lacc += j.nleft[t];
            racc += (end - begin) - j.nleft[t];
        }
        j.mid = mid;
        tri_parallel_for(num, threads, tri_part_scatter, &j);
        tri_parallel_for(num, threads, tri_part_copyback, &j);
        return mid;
    }
#endif
    uint32_t mid = 0;
    for (uint32_t i = 0; i < num; i++) {
        uint32_t p = c->indices[first + i];
        if (c->cen[(size_t)p * 3 + axis] < pos) {
            uint32_t tmp = c->indices[first + i];
            c->indices[first + i] = c->indices[first + mid];
            c->indices[first + mid] = tmp;
            mid++;
        }
    }
    return mid;
}

/* Allocate and fill one binary node over indices[first, first+num): computes
 * bounds, makes it a leaf, or picks a split and partitions the range. Returns
 * the node index; *out_mid > 0 iff the node is inner (split position). */
static uint32_t tri_build_node(tri_build_ctx *c, uint32_t first, uint32_t num,
                               uint32_t depth, uint32_t *out_mid) {
    *out_mid = 0;
    if (c->failed) return 0;
    if (c->node_next >= c->node_end) {
        c->failed = 1;
        return 0;
    }

    const float *plo = c->plo;
    const float *phi = c->phi;
    const float *cen = c->cen;
    uint32_t *indices = c->indices;

    /* Node bounds + centroid bounds in one pass. */
    float nlo[3], nhi[3], clo[3], chi[3];
    tri_box_reset(nlo, nhi);
    tri_box_reset(clo, chi);
    for (uint32_t i = 0; i < num; i++) {
        uint32_t p = indices[first + i];
        tri_box_expand(nlo, nhi, &plo[(size_t)p * 3], &phi[(size_t)p * 3]);
        for (int a = 0; a < 3; a++) {
            float ce = cen[(size_t)p * 3 + a];
            clo[a] = tri_minf(clo[a], ce);
            chi[a] = tri_maxf(chi[a], ce);
        }
    }

    uint32_t node_idx = c->node_next++;
    tri_bnode *node = &c->bnodes[node_idx];
    for (int a = 0; a < 3; a++) {
        node->lo[a] = nlo[a];
        node->hi[a] = nhi[a];
    }

    /* One block (or the user's cap, if tighter) is always a leaf; larger
     * ranges up to max_leaf become leaves only when SAH favors it below. */
    uint32_t block_width = 1u << c->block_shift;
    uint32_t always_leaf = c->max_leaf < block_width ? c->max_leaf : block_width;
    if (num <= always_leaf) {
        node->a = first;
        node->b = 0;
        node->count = num;
        return node_idx;
    }

    int best_axis = -1;
    float best_pos = 0.0f;
    int use_median = (c->quality == LRT_TRI_BUILD_FAST) ||
                     depth >= TRI_MEDIAN_DEPTH;

    if (!use_median) {
        /* Binned SAH: one pass over the primitives bins all three axes. */
        tri_bin bins[3][TRI_NUM_BINS];
        float amin[3], ascale[3];
        int axis_ok[3];
        for (int a = 0; a < 3; a++) {
            amin[a] = clo[a];
            float ext = chi[a] - clo[a];
            axis_ok[a] = ext > 1e-6f;
            ascale[a] = axis_ok[a] ? (float)TRI_NUM_BINS / ext : 0.0f;
            for (uint32_t b = 0; b < TRI_NUM_BINS; b++) {
                tri_box_reset(bins[a][b].lo, bins[a][b].hi);
                bins[a][b].count = 0;
            }
        }

#if !defined(__STDC_NO_THREADS__)
        if (c->par_threads > 1 && num >= TRI_PAR_NODE_MIN) {
            unsigned threads = c->par_threads;
            if (threads > TRI_PAR_NODE_THREADS) threads = TRI_PAR_NODE_THREADS;
            /* only reached from the single-threaded frontier expansion
             * (worker contexts run with par_threads == 0) */
            static tri_bin chunk_bins[TRI_PAR_MAX_THREADS][3][TRI_NUM_BINS];
            tri_bin_job j = {c, first, amin, ascale, axis_ok, chunk_bins};
            tri_parallel_for(num, threads, tri_bin_chunk, &j);
            for (unsigned t = 0; t < threads; t++) {
                for (int a = 0; a < 3; a++) {
                    for (uint32_t b = 0; b < TRI_NUM_BINS; b++) {
                        tri_box_expand(bins[a][b].lo, bins[a][b].hi,
                                       chunk_bins[t][a][b].lo,
                                       chunk_bins[t][a][b].hi);
                        bins[a][b].count += chunk_bins[t][a][b].count;
                    }
                }
            }
        } else
#endif
        {
            for (uint32_t i = 0; i < num; i++) {
                uint32_t p = indices[first + i];
                const float *pl = &plo[(size_t)p * 3];
                const float *ph = &phi[(size_t)p * 3];
                const float *pc = &cen[(size_t)p * 3];
                for (int a = 0; a < 3; a++) {
                    if (!axis_ok[a]) continue;
                    uint32_t b = (uint32_t)((pc[a] - amin[a]) * ascale[a]);
                    if (b >= TRI_NUM_BINS) b = TRI_NUM_BINS - 1;
                    tri_box_expand(bins[a][b].lo, bins[a][b].hi, pl, ph);
                    bins[a][b].count++;
                }
            }
        }

        float parent_area = tri_surface_area(nlo, nhi);
        if (parent_area <= 0.0f) parent_area = 1.0f;
        float best_cost = TRI_INF_F;

        for (int a = 0; a < 3; a++) {
            if (!axis_ok[a]) continue;
            /* Left prefix sweep. */
            float left_area[TRI_NUM_BINS];
            uint32_t left_cnt[TRI_NUM_BINS];
            float run_lo[3], run_hi[3];
            uint32_t run = 0;
            tri_box_reset(run_lo, run_hi);
            for (uint32_t b = 0; b < TRI_NUM_BINS; b++) {
                tri_box_expand(run_lo, run_hi, bins[a][b].lo, bins[a][b].hi);
                run += bins[a][b].count;
                left_area[b] = tri_surface_area(run_lo, run_hi);
                left_cnt[b] = run;
            }
            /* Right suffix sweep, evaluating each split plane. */
            tri_box_reset(run_lo, run_hi);
            run = 0;
            for (uint32_t b = TRI_NUM_BINS - 1; b > 0; b--) {
                tri_box_expand(run_lo, run_hi, bins[a][b].lo, bins[a][b].hi);
                run += bins[a][b].count;
                uint32_t lc = left_cnt[b - 1];
                if (lc == 0 || run == 0) continue;
                float cost = TRI_TRAV_COST +
                             TRI_ISECT_COST *
                                 (tri_sah_blocks(lc, c->block_shift) *
                                      left_area[b - 1] +
                                  tri_sah_blocks(run, c->block_shift) *
                                      tri_surface_area(run_lo, run_hi)) /
                                 parent_area;
                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = a;
                    best_pos = amin[a] + (float)b / ascale[a];
                }
            }
        }

        /* If no profitable split exists, prefer a leaf when small enough. */
        float leaf_cost = TRI_ISECT_COST * tri_sah_blocks(num, c->block_shift);
        if (best_axis < 0 || best_cost >= leaf_cost) {
            if (num <= c->max_leaf) {
                node->a = first;
                node->b = 0;
                node->count = num;
                return node_idx;
            }
            use_median = 1; /* must keep splitting; fall back to median */
        }
    }

    uint32_t mid;
    if (use_median || best_axis < 0) {
        /* Object-median split on the longest centroid axis. A centroid-
         * threshold partition at the midpoint approximates the median; exact
         * balance is restored by the num/2 fallback below. */
        int axis = tri_longest_axis(clo, chi);
        mid = tri_partition(c, first, num, axis, 0.5f * (clo[axis] + chi[axis]));
    } else {
        mid = tri_partition(c, first, num, best_axis, best_pos);
    }
    if (mid == 0 || mid == num) mid = num / 2; /* fallback: median by index */

    if (depth + 1 >= TRI_MAX_DEPTH) {
        /* Pathological recursion: clamp into a (possibly oversized) chain of
         * leaves by splitting evenly; depth grows by log2 only. */
        mid = num / 2;
    }

    node->count = 0;
    *out_mid = mid;
    return node_idx;
}

static uint32_t tri_build_recursive(tri_build_ctx *c, uint32_t first,
                                    uint32_t num, uint32_t depth) {
    uint32_t mid;
    uint32_t node_idx = tri_build_node(c, first, num, depth, &mid);
    if (c->failed || mid == 0) return node_idx; /* leaf (or failure) */

    uint32_t left = tri_build_recursive(c, first, mid, depth + 1);
    uint32_t right = tri_build_recursive(c, first + mid, num - mid, depth + 1);
    if (c->failed) return 0;

    tri_bnode *node = &c->bnodes[node_idx];
    node->a = left;
    node->b = right;
    return node_idx;
}

/* ---- LBVH (Morton) fast build ---------------------------------------------
 *
 * LRT_TRI_BUILD_FAST: quantize centroids to 30-bit Morton codes, radix-sort
 * (morton << 32) | prim keys, then build the binary tree by splitting each
 * sorted range at its highest differing Morton bit (binary search - no
 * binning, no partitioning). Bounds are unioned bottom-up. ~O(N) build with
 * SAH quality typically within ~10-20% of the binned builder.
 */

/* Spread the low 10 bits of v so each lands at every 3rd bit position. */
static inline uint32_t tri_morton_expand10(uint32_t v) {
    v &= 0x3FFu;
    v = (v | (v << 16)) & 0x030000FFu;
    v = (v | (v << 8)) & 0x0300F00Fu;
    v = (v | (v << 4)) & 0x030C30C3u;
    v = (v | (v << 2)) & 0x09249249u;
    return v;
}

/* LSD radix sort of n keys by their high-32 Morton bits (bytes 4..7). The
 * low-32 prim id makes keys unique, so the result is fully deterministic.
 * Returns the array holding the sorted keys (keys or tmp). */
static uint64_t *tri_radix_sort_keys(uint64_t *keys, uint64_t *tmp, size_t n) {
    uint64_t *src = keys, *dst = tmp;
    for (int byte = 4; byte < 8; byte++) {
        uint32_t shift = (uint32_t)byte * 8u;
        size_t count[256];
        memset(count, 0, sizeof(count));
        for (size_t i = 0; i < n; i++) count[(src[i] >> shift) & 0xFFu]++;
        /* skip passes where every key shares the digit (e.g. byte 7 has only
         * 6 used bits, and small scenes leave high bytes constant) */
        int trivial = 0;
        for (int d = 0; d < 256; d++) {
            if (count[d] == n) {
                trivial = 1;
                break;
            }
            if (count[d] != 0) break;
        }
        if (trivial) continue;
        size_t offset[256];
        size_t sum = 0;
        for (int d = 0; d < 256; d++) {
            offset[d] = sum;
            sum += count[d];
        }
        for (size_t i = 0; i < n; i++) {
            dst[offset[(src[i] >> shift) & 0xFFu]++] = src[i];
        }
        uint64_t *t = src;
        src = dst;
        dst = t;
    }
    return src;
}

/* First index in (0, num) where the highest differing Morton bit flips, or
 * num/2 when all codes in the range are equal. */
static uint32_t tri_lbvh_find_split(const uint64_t *keys, uint32_t first,
                                    uint32_t num) {
    uint64_t kf = keys[first] >> 32;
    uint64_t kl = keys[first + num - 1] >> 32;
    uint64_t x = kf ^ kl;
    if (x == 0) return num / 2; /* identical codes: median */
    int bit = 63 - __builtin_clzll(x);
    /* sorted: a prefix has the bit clear, the suffix has it set */
    uint32_t lo = 0, hi = num - 1;
    while (hi - lo > 1) {
        uint32_t m = lo + (hi - lo) / 2;
        if (((keys[first + m] >> 32) >> bit) & 1u) {
            hi = m;
        } else {
            lo = m;
        }
    }
    return hi;
}

/* Build a subtree over the Morton-sorted range [first, first+num); returns
 * the node index and writes the subtree bounds to lo_out/hi_out. */
static uint32_t tri_lbvh_range(tri_build_ctx *c, uint32_t first, uint32_t num,
                               float lo_out[3], float hi_out[3]) {
    if (c->failed) return 0;
    if (c->node_next >= c->node_end) {
        c->failed = 1;
        return 0;
    }
    uint32_t node_idx = c->node_next++;
    tri_bnode *node = &c->bnodes[node_idx];

    if (num <= c->lbvh_leaf) {
        float lo[3], hi[3];
        tri_box_reset(lo, hi);
        for (uint32_t i = 0; i < num; i++) {
            uint32_t p = c->indices[first + i];
            tri_box_expand(lo, hi, &c->plo[(size_t)p * 3], &c->phi[(size_t)p * 3]);
        }
        for (int a = 0; a < 3; a++) {
            node->lo[a] = lo_out[a] = lo[a];
            node->hi[a] = hi_out[a] = hi[a];
        }
        node->a = first;
        node->b = 0;
        node->count = num;
        return node_idx;
    }

    uint32_t mid = tri_lbvh_find_split(c->lbvh_keys, first, num);
    float llo[3], lhi[3], rlo[3], rhi[3];
    uint32_t left = tri_lbvh_range(c, first, mid, llo, lhi);
    uint32_t right = tri_lbvh_range(c, first + mid, num - mid, rlo, rhi);
    if (c->failed) return 0;

    node = &c->bnodes[node_idx];
    for (int a = 0; a < 3; a++) {
        node->lo[a] = lo_out[a] = tri_minf(llo[a], rlo[a]);
        node->hi[a] = hi_out[a] = tri_maxf(lhi[a], rhi[a]);
    }
    node->a = left;
    node->b = right;
    node->count = 0;
    return node_idx;
}

/* Quantize centroids to a 1024^3 grid over the centroid bounds and emit
 * (morton30 << 32) | prim keys. */
typedef struct tri_morton_job {
    const tri_build_ctx *c;
    uint64_t *keys;
    float base[3];
    float scale[3];
} tri_morton_job;

static void tri_morton_chunk_impl(const tri_morton_job *j, uint32_t begin,
                                  uint32_t end) {
    const float *cen = j->c->cen;
    for (uint32_t i = begin; i < end; i++) {
        uint32_t q[3];
        for (int a = 0; a < 3; a++) {
            float v = (cen[(size_t)i * 3 + a] - j->base[a]) * j->scale[a];
            int32_t qi = (int32_t)v;
            if (qi < 0) qi = 0;
            if (qi > 1023) qi = 1023;
            q[a] = (uint32_t)qi;
        }
        uint32_t morton = (tri_morton_expand10(q[0]) << 2) |
                          (tri_morton_expand10(q[1]) << 1) |
                          tri_morton_expand10(q[2]);
        j->keys[i] = ((uint64_t)morton << 32) | (uint64_t)i;
    }
}

#if !defined(__STDC_NO_THREADS__)
static void tri_morton_chunk(void *arg, unsigned chunk, uint32_t begin,
                             uint32_t end) {
    (void)chunk;
    tri_morton_chunk_impl((const tri_morton_job *)arg, begin, end);
}
#endif

static void tri_morton_encode(const tri_build_ctx *c, uint64_t *keys,
                              unsigned threads) {
    tri_morton_job j;
    j.c = c;
    j.keys = keys;
    /* centroid bounds (cheap serial reduction) */
    float clo[3], chi[3];
    tri_box_reset(clo, chi);
    for (size_t i = 0; i < c->ntris; i++) {
        for (int a = 0; a < 3; a++) {
            float v = c->cen[i * 3 + a];
            clo[a] = tri_minf(clo[a], v);
            chi[a] = tri_maxf(chi[a], v);
        }
    }
    for (int a = 0; a < 3; a++) {
        float ext = chi[a] - clo[a];
        j.base[a] = clo[a];
        j.scale[a] = ext > 0.0f ? 1024.0f / ext : 0.0f;
    }
#if !defined(__STDC_NO_THREADS__)
    if (threads > 1 && c->ntris >= TRI_PAR_NODE_MIN) {
        tri_parallel_for((uint32_t)c->ntris, threads, tri_morton_chunk, &j);
        return;
    }
#else
    (void)threads;
#endif
    tri_morton_chunk_impl(&j, 0, (uint32_t)c->ntris);
}

/* ---- Parallel binary build (C11 threads) ---------------------------------
 *
 * A serial frontier expansion splits the root task until there are enough
 * subtree tasks to feed the workers, then each task is built independently:
 * disjoint indices range, private slice of the bnodes arena (a subtree over k
 * prims needs at most 2k-1 nodes), and a unique parent link to store its root
 * into. Workers pull tasks with an atomic cursor.
 */
typedef struct tri_build_task {
    uint32_t first, num, depth;
    uint32_t *parent_slot; /* &bnodes[parent].a or .b (unique per task) */
} tri_build_task;

#if !defined(__STDC_NO_THREADS__)
typedef struct tri_build_pool {
    const tri_build_ctx *proto;
    tri_build_task *tasks;
    uint32_t ntasks;
    atomic_uint next_task;
    /* per-task arena slices, precomputed */
    uint32_t *slice_base;
    atomic_int failed;
    int lbvh; /* tasks run tri_lbvh_range instead of tri_build_recursive */
} tri_build_pool;

static int tri_build_worker(void *arg) {
    tri_build_pool *pool = (tri_build_pool *)arg;
    for (;;) {
        uint32_t t = atomic_fetch_add_explicit(&pool->next_task, 1u,
                                               memory_order_relaxed);
        if (t >= pool->ntasks) break;
        const tri_build_task *task = &pool->tasks[t];
        tri_build_ctx c = *pool->proto;
        c.par_threads = 0; /* subtree tasks are already parallel; no nesting */
        c.node_next = pool->slice_base[t];
        c.node_end = pool->slice_base[t] + 2u * task->num;
        uint32_t root;
        if (pool->lbvh) {
            float lo[3], hi[3];
            root = tri_lbvh_range(&c, task->first, task->num, lo, hi);
        } else {
            root = tri_build_recursive(&c, task->first, task->num, task->depth);
        }
        if (c.failed) {
            atomic_store_explicit(&pool->failed, 1, memory_order_relaxed);
            break;
        }
        *task->parent_slot = root;
    }
    return 0;
}
#endif /* !__STDC_NO_THREADS__ */

/* Build the binary tree over all primitives, using up to num_threads workers.
 * Returns the root node index, or fails via c->failed. */
static uint32_t tri_build_binary(tri_build_ctx *c, unsigned num_threads) {
#if !defined(__STDC_NO_THREADS__)
    if (num_threads > 1 && c->ntris >= 4096) {
        /* Serial frontier expansion: always expand the largest task. */
        enum { MAX_TASKS = 256 };
        tri_build_task tasks[MAX_TASKS];
        uint32_t root_slot = LRT_TRI_NO_HIT; /* root task writes here */
        uint32_t ntasks = 0;
        unsigned target = num_threads * 8u;
        if (target > MAX_TASKS) target = MAX_TASKS;

        tasks[ntasks++] = (tri_build_task){0, (uint32_t)c->ntris, 0, &root_slot};

        while (ntasks > 0 && ntasks < target) {
            /* pick the largest pending task that is splittable */
            uint32_t big = 0;
            for (uint32_t i = 1; i < ntasks; i++) {
                if (tasks[i].num > tasks[big].num) big = i;
            }
            if (tasks[big].num <= c->max_leaf * 4u) break;

            tri_build_task task = tasks[big];
            uint32_t mid;
            uint32_t node_idx =
                tri_build_node(c, task.first, task.num, task.depth, &mid);
            if (c->failed) return 0;
            *task.parent_slot = node_idx;
            if (mid == 0) {
                /* became a leaf: drop the task */
                tasks[big] = tasks[--ntasks];
                continue;
            }
            tri_bnode *node = &c->bnodes[node_idx];
            tasks[big] = (tri_build_task){task.first, mid, task.depth + 1,
                                          &node->a};
            tasks[ntasks++] = (tri_build_task){task.first + mid, task.num - mid,
                                               task.depth + 1, &node->b};
        }

        /* Largest-first order so the biggest subtree never starts last
         * (longest-processing-time schedule). */
        for (uint32_t i = 1; i < ntasks; i++) {
            tri_build_task key = tasks[i];
            uint32_t k = i;
            while (k > 0 && tasks[k - 1].num < key.num) {
                tasks[k] = tasks[k - 1];
                k--;
            }
            tasks[k] = key;
        }

        /* Assign each task a private arena slice of 2*num nodes. */
        uint32_t slice_base[MAX_TASKS];
        uint32_t cursor = c->node_next;
        int fits = 1;
        for (uint32_t i = 0; i < ntasks; i++) {
            slice_base[i] = cursor;
            if (2u * tasks[i].num > c->node_end - cursor) {
                fits = 0;
                break;
            }
            cursor += 2u * tasks[i].num;
        }

        if (fits && ntasks > 1) {
            tri_build_pool pool;
            pool.proto = c;
            pool.tasks = tasks;
            pool.ntasks = ntasks;
            atomic_init(&pool.next_task, 0u);
            pool.slice_base = slice_base;
            atomic_init(&pool.failed, 0);
            pool.lbvh = 0;

            thrd_t tids[64];
            unsigned nthr = num_threads < 64u ? num_threads : 64u;
            if (nthr > ntasks) nthr = ntasks;
            unsigned spawned = 0;
            for (unsigned i = 0; i + 1 < nthr; i++) {
                if (thrd_create(&tids[i], tri_build_worker, &pool) ==
                    thrd_success) {
                    spawned++;
                } else {
                    break;
                }
            }
            tri_build_worker(&pool); /* this thread works too */
            for (unsigned i = 0; i < spawned; i++) thrd_join(tids[i], NULL);

            if (atomic_load(&pool.failed)) {
                c->failed = 1;
                return 0;
            }
            return root_slot;
        }
        /* fall through to serial on slicing failure or a single task */
        if (ntasks >= 1 && root_slot == LRT_TRI_NO_HIT) {
            /* root task never expanded: build it serially below */
        }
        if (root_slot != LRT_TRI_NO_HIT) {
            /* frontier was partially expanded; finish remaining tasks serially */
            for (uint32_t i = 0; i < ntasks; i++) {
                uint32_t r = tri_build_recursive(c, tasks[i].first, tasks[i].num,
                                                 tasks[i].depth);
                if (c->failed) return 0;
                *tasks[i].parent_slot = r;
            }
            return root_slot;
        }
    }
#else
    (void)num_threads;
#endif
    return tri_build_recursive(c, 0, (uint32_t)c->ntris, 0);
}

/* LBVH counterpart of tri_build_binary: indices[] are already Morton-sorted
 * and c->lbvh_keys is set. Frontier expansion is a binary search per split
 * (no O(N) binning/partition), so it parallelizes much better; frontier node
 * bounds are filled in afterwards from their children (reverse creation
 * order: children are always created after their parent). */
static uint32_t tri_build_lbvh(tri_build_ctx *c, unsigned num_threads) {
#if !defined(__STDC_NO_THREADS__)
    if (num_threads > 1 && c->ntris >= 4096) {
        enum { MAX_TASKS = 256 };
        tri_build_task tasks[MAX_TASKS];
        uint32_t fixup[MAX_TASKS];
        uint32_t nfix = 0;
        uint32_t root_slot = LRT_TRI_NO_HIT;
        uint32_t ntasks = 0;
        unsigned target = num_threads * 8u;
        if (target > MAX_TASKS) target = MAX_TASKS;

        tasks[ntasks++] = (tri_build_task){0, (uint32_t)c->ntris, 0, &root_slot};

        while (ntasks > 0 && ntasks < target) {
            uint32_t big = 0;
            for (uint32_t i = 1; i < ntasks; i++) {
                if (tasks[i].num > tasks[big].num) big = i;
            }
            if (tasks[big].num <= c->lbvh_leaf * 4u) break;

            tri_build_task task = tasks[big];
            if (c->node_next >= c->node_end) {
                c->failed = 1;
                return 0;
            }
            uint32_t node_idx = c->node_next++;
            tri_bnode *node = &c->bnodes[node_idx];
            node->count = 0; /* bounds deferred to the fixup pass */
            *task.parent_slot = node_idx;
            fixup[nfix++] = node_idx;

            uint32_t mid = tri_lbvh_find_split(c->lbvh_keys, task.first, task.num);
            tasks[big] = (tri_build_task){task.first, mid, task.depth + 1,
                                          &node->a};
            tasks[ntasks++] = (tri_build_task){task.first + mid, task.num - mid,
                                               task.depth + 1, &node->b};
        }

        for (uint32_t i = 1; i < ntasks; i++) { /* LPT order */
            tri_build_task key = tasks[i];
            uint32_t k = i;
            while (k > 0 && tasks[k - 1].num < key.num) {
                tasks[k] = tasks[k - 1];
                k--;
            }
            tasks[k] = key;
        }

        uint32_t slice_base[MAX_TASKS];
        uint32_t cursor = c->node_next;
        int fits = 1;
        for (uint32_t i = 0; i < ntasks; i++) {
            slice_base[i] = cursor;
            if (2u * tasks[i].num > c->node_end - cursor) {
                fits = 0;
                break;
            }
            cursor += 2u * tasks[i].num;
        }

        if (fits && ntasks > 1) {
            tri_build_pool pool;
            pool.proto = c;
            pool.tasks = tasks;
            pool.ntasks = ntasks;
            atomic_init(&pool.next_task, 0u);
            pool.slice_base = slice_base;
            atomic_init(&pool.failed, 0);
            pool.lbvh = 1;

            thrd_t tids[64];
            unsigned nthr = num_threads < 64u ? num_threads : 64u;
            if (nthr > ntasks) nthr = ntasks;
            unsigned spawned = 0;
            for (unsigned i = 0; i + 1 < nthr; i++) {
                if (thrd_create(&tids[i], tri_build_worker, &pool) ==
                    thrd_success) {
                    spawned++;
                } else {
                    break;
                }
            }
            tri_build_worker(&pool);
            for (unsigned i = 0; i < spawned; i++) thrd_join(tids[i], NULL);

            if (atomic_load(&pool.failed)) {
                c->failed = 1;
                return 0;
            }
            /* Bounds fixup: reverse creation order guarantees children (later
             * frontier nodes or worker-built subtree roots) are final. */
            for (uint32_t i = nfix; i-- > 0;) {
                tri_bnode *n = &c->bnodes[fixup[i]];
                const tri_bnode *l = &c->bnodes[n->a];
                const tri_bnode *r = &c->bnodes[n->b];
                for (int a = 0; a < 3; a++) {
                    n->lo[a] = tri_minf(l->lo[a], r->lo[a]);
                    n->hi[a] = tri_maxf(l->hi[a], r->hi[a]);
                }
            }
            return root_slot;
        }
        if (root_slot != LRT_TRI_NO_HIT) {
            /* slicing failed after partial expansion: finish serially */
            for (uint32_t i = 0; i < ntasks; i++) {
                float lo[3], hi[3];
                uint32_t r = tri_lbvh_range(c, tasks[i].first, tasks[i].num, lo, hi);
                if (c->failed) return 0;
                *tasks[i].parent_slot = r;
            }
            for (uint32_t i = nfix; i-- > 0;) {
                tri_bnode *n = &c->bnodes[fixup[i]];
                const tri_bnode *l = &c->bnodes[n->a];
                const tri_bnode *r = &c->bnodes[n->b];
                for (int a = 0; a < 3; a++) {
                    n->lo[a] = tri_minf(l->lo[a], r->lo[a]);
                    n->hi[a] = tri_maxf(l->hi[a], r->hi[a]);
                }
            }
            return root_slot;
        }
    }
#else
    (void)num_threads;
#endif
    float lo[3], hi[3];
    return tri_lbvh_range(c, 0, (uint32_t)c->ntris, lo, hi);
}

/* ---- SBVH (spatial splits) build ------------------------------------------
 *
 * LRT_TRI_BUILD_HQ: binned object SAH plus spatial splits (Stich et al.,
 * HPG 2009). A primitive reference straddling a chosen split plane is
 * duplicated into both children with plane-clipped bounds, removing the
 * child-box overlap that costs short (shadow) rays the most. References are
 * AABB-clipped; leaves store original prim ids, so a triangle may simply be
 * tested by more than one leaf - no other correctness impact.
 *
 * Ranges live in a shared refs array with slack distributed proportionally
 * to the children (extended ranges), so duplication never reallocates; when
 * a range's slack is exhausted the node falls back to an object split.
 */
#define TRI_SBVH_SPATIAL_BINS 32u
#define TRI_SBVH_ALPHA 1e-5f    /* overlap/root-area gate for spatial split */
#define TRI_SBVH_SPLIT_FACTOR 2 /* refs capacity = factor * ntris */

typedef struct tri_pref {
    float lo[3], hi[3];
    uint32_t prim;
    uint32_t _pad;
} tri_pref; /* 32 bytes */

typedef struct tri_sbvh_ctx {
    tri_pref *refs;    /* cap slots, ranges with slack */
    tri_pref *scratch; /* cap slots, spatial-split staging */
    uint32_t cap;
    tri_bnode *bnodes;
    uint32_t node_next, node_end;
    uint32_t max_leaf;
    uint32_t block_shift;
    float root_area;
    uint32_t emit_total; /* sum of leaf reference counts */
    uint32_t dbg_spatial, dbg_object, dbg_sp_considered;
    int failed;
} tri_sbvh_ctx;

static uint32_t tri_sbvh_make_leaf(tri_sbvh_ctx *c, uint32_t node_idx,
                                   uint32_t first, uint32_t num,
                                   const float nlo[3], const float nhi[3]) {
    tri_bnode *node = &c->bnodes[node_idx];
    for (int a = 0; a < 3; a++) {
        node->lo[a] = nlo[a];
        node->hi[a] = nhi[a];
    }
    node->a = first;
    node->b = 0;
    node->count = num;
    c->emit_total += num;
    return node_idx;
}

static uint32_t tri_sbvh_recursive(tri_sbvh_ctx *c, uint32_t first,
                                   uint32_t num, uint32_t avail,
                                   uint32_t depth) {
    if (c->failed) return 0;
    if (c->node_next >= c->node_end) {
        c->failed = 1;
        return 0;
    }

    tri_pref *refs = c->refs;

    /* Node + centroid bounds. */
    float nlo[3], nhi[3], clo[3], chi[3];
    tri_box_reset(nlo, nhi);
    tri_box_reset(clo, chi);
    for (uint32_t i = 0; i < num; i++) {
        const tri_pref *r = &refs[first + i];
        tri_box_expand(nlo, nhi, r->lo, r->hi);
        for (int a = 0; a < 3; a++) {
            float ce = 0.5f * (r->lo[a] + r->hi[a]);
            clo[a] = tri_minf(clo[a], ce);
            chi[a] = tri_maxf(chi[a], ce);
        }
    }

    uint32_t node_idx = c->node_next++;
    uint32_t block_width = 1u << c->block_shift;
    uint32_t always_leaf = c->max_leaf < block_width ? c->max_leaf : block_width;
    if (num <= always_leaf) {
        return tri_sbvh_make_leaf(c, node_idx, first, num, nlo, nhi);
    }

    float parent_area = tri_surface_area(nlo, nhi);
    if (parent_area <= 0.0f) parent_area = 1.0f;

    /* --- Binned object split (all 3 axes, one pass). --- */
    int obj_axis = -1;
    float obj_pos = 0.0f;
    float obj_cost = TRI_INF_F;
    float obj_llo[3], obj_lhi[3], obj_rlo[3], obj_rhi[3]; /* overlap gate */
    int best_bin = -1;
    int use_median = depth >= TRI_MEDIAN_DEPTH;

    if (!use_median) {
        tri_bin bins[3][TRI_NUM_BINS];
        float amin[3], ascale[3];
        int axis_ok[3];
        for (int a = 0; a < 3; a++) {
            amin[a] = clo[a];
            float ext = chi[a] - clo[a];
            axis_ok[a] = ext > 1e-6f;
            ascale[a] = axis_ok[a] ? (float)TRI_NUM_BINS / ext : 0.0f;
            for (uint32_t b = 0; b < TRI_NUM_BINS; b++) {
                tri_box_reset(bins[a][b].lo, bins[a][b].hi);
                bins[a][b].count = 0;
            }
        }
        for (uint32_t i = 0; i < num; i++) {
            const tri_pref *r = &refs[first + i];
            for (int a = 0; a < 3; a++) {
                if (!axis_ok[a]) continue;
                float cen = 0.5f * (r->lo[a] + r->hi[a]);
                uint32_t b = (uint32_t)((cen - amin[a]) * ascale[a]);
                if (b >= TRI_NUM_BINS) b = TRI_NUM_BINS - 1;
                tri_box_expand(bins[a][b].lo, bins[a][b].hi, r->lo, r->hi);
                bins[a][b].count++;
            }
        }
        for (int a = 0; a < 3; a++) {
            if (!axis_ok[a]) continue;
            float left_area[TRI_NUM_BINS];
            uint32_t left_cnt[TRI_NUM_BINS];
            float run_lo[3], run_hi[3];
            uint32_t run = 0;
            tri_box_reset(run_lo, run_hi);
            for (uint32_t b = 0; b < TRI_NUM_BINS; b++) {
                tri_box_expand(run_lo, run_hi, bins[a][b].lo, bins[a][b].hi);
                run += bins[a][b].count;
                left_area[b] = tri_surface_area(run_lo, run_hi);
                left_cnt[b] = run;
            }
            tri_box_reset(run_lo, run_hi);
            run = 0;
            for (uint32_t b = TRI_NUM_BINS - 1; b > 0; b--) {
                tri_box_expand(run_lo, run_hi, bins[a][b].lo, bins[a][b].hi);
                run += bins[a][b].count;
                uint32_t lc = left_cnt[b - 1];
                if (lc == 0 || run == 0) continue;
                float cost = TRI_TRAV_COST +
                             TRI_ISECT_COST *
                                 (tri_sah_blocks(lc, c->block_shift) *
                                      left_area[b - 1] +
                                  tri_sah_blocks(run, c->block_shift) *
                                      tri_surface_area(run_lo, run_hi)) /
                                 parent_area;
                if (cost < obj_cost) {
                    obj_cost = cost;
                    obj_axis = a;
                    obj_pos = amin[a] + (float)b / ascale[a];
                    best_bin = (int)b;
                }
            }
        }
        /* Best object split's child bounds, for the overlap gate. */
        if (obj_axis >= 0) {
            tri_box_reset(obj_llo, obj_lhi);
            tri_box_reset(obj_rlo, obj_rhi);
            for (uint32_t b = 0; b < TRI_NUM_BINS; b++) {
                if ((int)b < best_bin) {
                    tri_box_expand(obj_llo, obj_lhi, bins[obj_axis][b].lo,
                                   bins[obj_axis][b].hi);
                } else {
                    tri_box_expand(obj_rlo, obj_rhi, bins[obj_axis][b].lo,
                                   bins[obj_axis][b].hi);
                }
            }
        }
    }

    /* --- Spatial split, gated on object-split child overlap. --- */
    int sp_axis = -1;
    float sp_pos = 0.0f;
    float sp_cost = TRI_INF_F;
    if (obj_axis >= 0 && avail > num) {
        float ov_lo[3], ov_hi[3];
        int overlapping = 1;
        for (int a = 0; a < 3; a++) {
            ov_lo[a] = tri_maxf(obj_llo[a], obj_rlo[a]);
            ov_hi[a] = tri_minf(obj_lhi[a], obj_rhi[a]);
            if (ov_lo[a] > ov_hi[a]) overlapping = 0;
        }
        if (overlapping &&
            tri_surface_area(ov_lo, ov_hi) / c->root_area > TRI_SBVH_ALPHA) {
            for (int a = 0; a < 3; a++) {
                float ext = nhi[a] - nlo[a];
                if (ext <= 1e-6f) continue;
                float bin_w = ext / (float)TRI_SBVH_SPATIAL_BINS;
                float inv_bin_w = (float)TRI_SBVH_SPATIAL_BINS / ext;
                float blo[TRI_SBVH_SPATIAL_BINS][3], bhi[TRI_SBVH_SPATIAL_BINS][3];
                uint32_t enter[TRI_SBVH_SPATIAL_BINS], leave[TRI_SBVH_SPATIAL_BINS];
                for (uint32_t b = 0; b < TRI_SBVH_SPATIAL_BINS; b++) {
                    tri_box_reset(blo[b], bhi[b]);
                    enter[b] = leave[b] = 0;
                }
                for (uint32_t i = 0; i < num; i++) {
                    const tri_pref *r = &refs[first + i];
                    int b0 = (int)((r->lo[a] - nlo[a]) * inv_bin_w);
                    int b1 = (int)((r->hi[a] - nlo[a]) * inv_bin_w);
                    if (b0 < 0) b0 = 0;
                    if (b0 > (int)TRI_SBVH_SPATIAL_BINS - 1)
                        b0 = (int)TRI_SBVH_SPATIAL_BINS - 1;
                    if (b1 < b0) b1 = b0;
                    if (b1 > (int)TRI_SBVH_SPATIAL_BINS - 1)
                        b1 = (int)TRI_SBVH_SPATIAL_BINS - 1;
                    enter[b0]++;
                    leave[b1]++;
                    for (int b = b0; b <= b1; b++) {
                        float cl[3], ch[3];
                        for (int k = 0; k < 3; k++) {
                            cl[k] = r->lo[k];
                            ch[k] = r->hi[k];
                        }
                        cl[a] = tri_maxf(cl[a], nlo[a] + (float)b * bin_w);
                        ch[a] = tri_minf(ch[a], nlo[a] + (float)(b + 1) * bin_w);
                        tri_box_expand(blo[b], bhi[b], cl, ch);
                    }
                }
                float left_area[TRI_SBVH_SPATIAL_BINS];
                uint32_t left_cnt[TRI_SBVH_SPATIAL_BINS];
                float run_lo[3], run_hi[3];
                uint32_t run = 0;
                tri_box_reset(run_lo, run_hi);
                for (uint32_t b = 0; b < TRI_SBVH_SPATIAL_BINS; b++) {
                    tri_box_expand(run_lo, run_hi, blo[b], bhi[b]);
                    run += enter[b];
                    left_area[b] = tri_surface_area(run_lo, run_hi);
                    left_cnt[b] = run;
                }
                tri_box_reset(run_lo, run_hi);
                run = 0;
                for (uint32_t b = TRI_SBVH_SPATIAL_BINS - 1; b > 0; b--) {
                    tri_box_expand(run_lo, run_hi, blo[b], bhi[b]);
                    run += leave[b];
                    uint32_t lc = left_cnt[b - 1];
                    if (lc == 0 || run == 0) continue;
                    float cost = TRI_TRAV_COST +
                                 TRI_ISECT_COST *
                                     (tri_sah_blocks(lc, c->block_shift) *
                                          left_area[b - 1] +
                                      tri_sah_blocks(run, c->block_shift) *
                                          tri_surface_area(run_lo, run_hi)) /
                                     parent_area;
                    if (cost < sp_cost) {
                        sp_cost = cost;
                        sp_axis = a;
                        sp_pos = nlo[a] + (float)b * bin_w;
                    }
                }
            }
        }
    }

    /* --- Leaf if neither split beats testing everything here (the forced
     * median path below handles oversized degenerate clusters). --- */
    float leaf_cost = TRI_ISECT_COST * tri_sah_blocks(num, c->block_shift);
    float best_cost = obj_cost < sp_cost ? obj_cost : sp_cost;
    if (num <= c->max_leaf &&
        ((obj_axis < 0 && sp_axis < 0 && !use_median) ||
         best_cost >= leaf_cost)) {
        return tri_sbvh_make_leaf(c, node_idx, first, num, nlo, nhi);
    }

    uint32_t l_first = first, l_num = 0, l_avail = 0;
    uint32_t r_first = 0, r_num = 0, r_avail = 0;
    int did_split = 0;

    if (sp_axis >= 0) c->dbg_sp_considered++;
#ifdef TRI_SBVH_DEBUG
    if (depth <= 2) {
        fprintf(stderr,
                "sbvh d%u num %u: obj_cost %.1f (axis %d) sp_cost %.1f (axis %d)\n",
                depth, num, (double)obj_cost, obj_axis, (double)sp_cost, sp_axis);
    }
#endif
    if (sp_axis >= 0 && sp_cost < obj_cost) {
        /* Spatial split: classify into scratch (lefts bottom-up, rights
         * top-down), duplicating straddlers with plane-clipped bounds, then
         * copy into the slack-extended range. Falls back to the object split
         * when slack is insufficient or the split degenerates. */
        tri_pref *sc = c->scratch;
        uint32_t nl = 0;
        uint32_t nr_end = avail;
        int room = 1;
        for (uint32_t i = 0; i < num; i++) {
            const tri_pref *r = &refs[first + i];
            if (nl + (avail - nr_end) + 2 > avail) {
                room = 0;
                break;
            }
            if (r->hi[sp_axis] <= sp_pos) {
                sc[nl++] = *r;
            } else if (r->lo[sp_axis] >= sp_pos) {
                sc[--nr_end] = *r;
            } else {
                tri_pref l = *r, rr = *r;
                l.hi[sp_axis] = sp_pos;
                rr.lo[sp_axis] = sp_pos;
                sc[nl++] = l;
                sc[--nr_end] = rr;
            }
        }
        uint32_t nr = avail - nr_end;
        if (room && nl >= 1 && nr >= 1 && (nl < num || nr < num) &&
            nl + nr <= avail) {
            uint32_t slack = avail - (nl + nr);
            l_num = nl;
            l_avail = nl + (uint32_t)((uint64_t)slack * nl / (nl + nr));
            r_num = nr;
            r_avail = avail - l_avail;
            r_first = first + l_avail;
            memcpy(refs + first, sc, (size_t)nl * sizeof(tri_pref));
            memcpy(refs + r_first, sc + nr_end, (size_t)nr * sizeof(tri_pref));
            did_split = 1;
            c->dbg_spatial++;
        }
    }

    if (!did_split) {
        c->dbg_object++;
        /* Object split (in-place partition by centroid). */
        int axis;
        float pos;
        if (obj_axis >= 0 && !use_median) {
            axis = obj_axis;
            pos = obj_pos;
        } else {
            axis = tri_longest_axis(clo, chi);
            pos = 0.5f * (clo[axis] + chi[axis]);
        }
        uint32_t mid = 0;
        for (uint32_t i = 0; i < num; i++) {
            tri_pref *r = &refs[first + i];
            if (0.5f * (r->lo[axis] + r->hi[axis]) < pos) {
                tri_pref tmp = *r;
                *r = refs[first + mid];
                refs[first + mid] = tmp;
                mid++;
            }
        }
        if (mid == 0 || mid == num) mid = num / 2;
        uint32_t slack = avail - num;
        l_num = mid;
        l_avail = mid + (uint32_t)((uint64_t)slack * mid / num);
        r_num = num - mid;
        r_avail = avail - l_avail;
        r_first = first + l_avail;
        memmove(refs + r_first, refs + first + mid,
                (size_t)r_num * sizeof(tri_pref));
    }

    uint32_t left = tri_sbvh_recursive(c, l_first, l_num, l_avail, depth + 1);
    uint32_t right = tri_sbvh_recursive(c, r_first, r_num, r_avail, depth + 1);
    if (c->failed) return 0;

    tri_bnode *node = &c->bnodes[node_idx];
    for (int a = 0; a < 3; a++) {
        node->lo[a] = nlo[a];
        node->hi[a] = nhi[a];
    }
    node->a = left;
    node->b = right;
    node->count = 0;
    return node_idx;
}

/* Build the binary SBVH; rewrites bc->indices so binary leaves reference the
 * (possibly duplicated) prim ids, and reports the leaf-reference total for
 * sizing the collapse outputs. Returns the root index or fails via
 * bc->failed. */
static uint32_t tri_build_sbvh(tri_build_ctx *bc, uint32_t *emit_total) {
    size_t ntris = bc->ntris;
    uint32_t cap = (uint32_t)(ntris * TRI_SBVH_SPLIT_FACTOR);
    if (cap < 16u) cap = 16u;

    tri_sbvh_ctx c;
    memset(&c, 0, sizeof(c));
    c.refs = (tri_pref *)calloc(cap, sizeof(tri_pref));
    c.scratch = (tri_pref *)malloc((size_t)cap * sizeof(tri_pref));
    c.cap = cap;
    c.bnodes = bc->bnodes;
    c.node_next = bc->node_next;
    c.node_end = bc->node_end;
    c.max_leaf = bc->max_leaf;
    c.block_shift = bc->block_shift;
    if (!c.refs || !c.scratch) {
        free(c.refs);
        free(c.scratch);
        bc->failed = 1;
        return 0;
    }

    float root_lo[3], root_hi[3];
    tri_box_reset(root_lo, root_hi);
    for (size_t i = 0; i < ntris; i++) {
        tri_pref *r = &c.refs[i];
        for (int a = 0; a < 3; a++) {
            r->lo[a] = bc->plo[i * 3 + a];
            r->hi[a] = bc->phi[i * 3 + a];
        }
        r->prim = (uint32_t)i;
        tri_box_expand(root_lo, root_hi, r->lo, r->hi);
    }
    c.root_area = tri_surface_area(root_lo, root_hi);
    if (c.root_area <= 0.0f) c.root_area = 1.0f;

    uint32_t root = tri_sbvh_recursive(&c, 0, (uint32_t)ntris, cap, 0);

#ifdef TRI_SBVH_DEBUG
    fprintf(stderr, "sbvh: %u spatial, %u object, %u considered, emit %u/%zu\n",
            c.dbg_spatial, c.dbg_object, c.dbg_sp_considered, c.emit_total,
            ntris);
#endif
    bc->node_next = c.node_next;
    if (c.failed) {
        free(c.refs);
        free(c.scratch);
        bc->failed = 1;
        return 0;
    }

    /* Leaves reference ranges of the refs array; expose the prim ids through
     * bc->indices for the (unchanged) leaf emission. calloc'd refs keep dead
     * slack slots at prim 0, which no leaf references. */
    uint32_t *idx = (uint32_t *)malloc((size_t)cap * sizeof(uint32_t));
    if (!idx) {
        free(c.refs);
        free(c.scratch);
        bc->failed = 1;
        return 0;
    }
    for (uint32_t i = 0; i < cap; i++) idx[i] = c.refs[i].prim;
    free(bc->indices);
    bc->indices = idx;

    free(c.refs);
    free(c.scratch);
    *emit_total = c.emit_total;
    return root;
}

/* ------------------------------------------------------------------------- */
/* Collapse binary -> wide BVH.                                              */
/* ------------------------------------------------------------------------- */

typedef struct tri_collapse_ctx {
    const tri_build_ctx *bc;
    lrt_tri_scene *s;
    uint32_t node_cap;
    uint32_t block_cap;
    float root_area;
    double sah_inner; /* sum SA(node)/SA(root) */
    double sah_leaf;  /* sum SA(leaf)*count/SA(root) */
    uint32_t leaf_count;
    uint32_t max_depth;
    int width;
    int quantized;
    int qnode; /* node-bound quant format (s->qnode) */
    int failed;
} tri_collapse_ctx;

/* Conservative 8-bit quantization of a child bound onto the parent grid:
 * decoded lo never exceeds the true lo, decoded hi never undershoots. */
static inline uint8_t tri_quantize_lo(float v, float org, float scale,
                                      float inv_scale) {
    if (scale <= 0.0f) return 0;
    float f = (v - org) * inv_scale;
    int q = (int)f;
    if (q < 0) q = 0;
    if (q > 255) q = 255;
    while (q > 0 && org + (float)q * scale > v) q--;
    return (uint8_t)q;
}

static inline uint8_t tri_quantize_hi(float v, float org, float scale,
                                      float inv_scale) {
    if (scale <= 0.0f) return 0; /* zero extent: decode == org == v */
    float f = (v - org) * inv_scale;
    int q = (int)f + 1;
    if (q < 0) q = 0;
    if (q > 255) q = 255;
    while (q < 255 && org + (float)q * scale < v) q++;
    return (uint8_t)q;
}

/* ---- Quantized-triangle vertex codecs (scalar reference) ---------------- */

/* E2M1 (4-bit float) value table: codes 0..7 positive, 8..15 negated. */
static const float g_tri_e2m1[16] = {0.0f,  0.5f,  1.0f,  1.5f,  2.0f,
                                     3.0f,  4.0f,  6.0f,  0.0f,  -0.5f,
                                     -1.0f, -1.5f, -2.0f, -3.0f, -4.0f, -6.0f};
#define TRI_E2M1_MAX 6.0f

/* E4M3 (1-4-3, bias 7) decode of a nonnegative code to float. */
static inline float tri_e4m3_decode(uint8_t q) {
    uint32_t e = (uint32_t)(q >> 3) & 0xFu, m = (uint32_t)q & 0x7u;
    if (e == 0u) return (float)m * (1.0f / 512.0f); /* subnormal: m * 2^-9 */
    uint32_t bits = ((e + 120u) << 23) | (m << 20);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
static inline uint8_t tri_e4m3_encode(float n) {
    if (!(n > 0.0f)) return 0u;
    if (n >= 448.0f) return 0x7Eu;
    uint32_t b;
    memcpy(&b, &n, 4);
    int E = (int)((b >> 23) & 0xFFu) - 127 + 7;
    uint32_t m = (b >> 20) & 0x7u;
    uint32_t rnd = (b >> 19) & 1u;
    if (E >= 1) {
        uint32_t code = ((uint32_t)E << 3) | m;
        code += rnd;
        if (code > 0x7Eu) code = 0x7Eu;
        return (uint8_t)code;
    }
    int q = (int)(n * 512.0f + 0.5f); /* subnormal region */
    if (q > 7) q = 7;
    if (q < 0) q = 0;
    return (uint8_t)q;
}
/* nearest E2M1 code for a signed normalized value in [-1,1] (×TRI_E2M1_MAX). */
static inline uint8_t tri_e2m1_encode(float n) {
    float t = n * TRI_E2M1_MAX;
    int best = 0;
    float bestd = 1e30f;
    for (int i = 0; i < 16; i++) {
        float d = g_tri_e2m1[i] - t;
        d = d < 0 ? -d : d;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return (uint8_t)best;
}

static inline uint8_t tri_nib_get(const uint8_t *nib, int idx) {
    uint8_t byte = nib[idx >> 1];
    return (idx & 1) ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0F);
}
static inline void tri_nib_set(uint8_t *nib, int idx, uint8_t v) {
    uint8_t *byte = &nib[idx >> 1];
    if (idx & 1)
        *byte = (uint8_t)((*byte & 0x0F) | (v << 4));
    else
        *byte = (uint8_t)((*byte & 0xF0) | (v & 0x0F));
}

/* Decode the 3 vertices of quantized-leaf lane `lane`, format-dispatched. */
static inline void tri_qtri_decode_lane(const lrt_tri_scene *s, const void *blkv,
                                        int lane, float v0[3], float v1[3],
                                        float v2[3]) {
    float *vv[3] = {v0, v1, v2};
    if (s->qfmt == LRT_QTRI_Q16) {
        const lrt_qtri16 *b = (const lrt_qtri16 *)blkv;
        const uint16_t *q[9] = {b->qv0x, b->qv0y, b->qv0z, b->qv1x, b->qv1y,
                                b->qv1z, b->qv2x, b->qv2y, b->qv2z};
        for (int t = 0; t < 3; t++)
            for (int a = 0; a < 3; a++)
                vv[t][a] = s->qgrid_org[a] +
                           (float)q[t * 3 + a][lane] * s->qgrid_scale[a];
        return;
    }
    if (s->qfmt == LRT_QTRI_FP4) {
        const lrt_qtri4 *b = (const lrt_qtri4 *)blkv;
        for (int t = 0; t < 3; t++)
            for (int a = 0; a < 3; a++) {
                int c = t * 3 + a;
                uint8_t code = tri_nib_get(b->nib, c * 4 + lane);
                vv[t][a] = b->org[a] +
                           g_tri_e2m1[code] * (1.0f / TRI_E2M1_MAX) * b->scale[a];
            }
        return;
    }
    /* Q8 / FP8 */
    const lrt_qtri8 *b = (const lrt_qtri8 *)blkv;
    const uint8_t *q[9] = {b->qv0x, b->qv0y, b->qv0z, b->qv1x, b->qv1y,
                           b->qv1z, b->qv2x, b->qv2y, b->qv2z};
    int fp8 = (s->qfmt == LRT_QTRI_FP8);
    for (int t = 0; t < 3; t++)
        for (int a = 0; a < 3; a++) {
            uint8_t code = q[t * 3 + a][lane];
            float u = fp8 ? tri_e4m3_decode(code) : (float)code;
            vv[t][a] = b->org[a] + u * b->scale[a];
        }
}

/* Conservative dilation margin as a fraction of the leaf extent, per format
 * (covers the worst-case decode level spacing; tuned so the no-miss test
 * passes). Lossy uses 0. */
static inline float tri_qtri_mfrac(int fmt) {
    switch (fmt) {
        case LRT_QTRI_Q16: return 0.0005f;
        case LRT_QTRI_Q8: return 0.02f;
        case LRT_QTRI_FP8: return 0.10f;
        default: return 0.35f; /* FP4 */
    }
}

static inline int tri_iround(float f) {
    int q = (int)(f + (f >= 0.0f ? 0.5f : -0.5f));
    return q;
}

/* Encode one (already-dilated) coordinate into the format's integer code. */
static inline uint16_t tri_qtri_enc(int fmt, float v, float org, float scale,
                                    float half) {
    if (fmt == LRT_QTRI_Q16) {
        if (scale <= 0.0f) return 0;
        int q = tri_iround((v - org) / scale);
        if (q < 0) q = 0;
        if (q > 65535) q = 65535;
        return (uint16_t)q;
    }
    if (fmt == LRT_QTRI_FP4) {
        if (half <= 0.0f) return 0;
        float n = (v - org) / half; /* org=center, half=halfext, n in [-1,1] */
        if (n < -1.0f) n = -1.0f;
        if (n > 1.0f) n = 1.0f;
        return tri_e2m1_encode(n);
    }
    if (fmt == LRT_QTRI_FP8) {
        if (scale <= 0.0f) return 0;
        float n = (v - org) / scale; /* [0,1] */
        if (n < 0.0f) n = 0.0f;
        if (n > 1.0f) n = 1.0f;
        return tri_e4m3_encode(n);
    }
    /* Q8 */
    if (scale <= 0.0f) return 0;
    int q = tri_iround((v - org) / scale);
    if (q < 0) q = 0;
    if (q > 255) q = 255;
    return (uint8_t)q;
}

/* Emit one quantized-triangle block (up to 4 tris). prims[k]=NO_HIT marks a
 * padding lane. */
static void tri_qtri_emit_block(lrt_tri_scene *s, void *blkv,
                                const float vtx[4][9], const uint32_t prims[4]) {
    const int fmt = s->qfmt;
    const int cons = (s->qflags & LRT_QTRI_CONSERVATIVE) != 0;
    float work[4][9];
    /* 1. block bbox over true vertices. */
    float lo[3] = {TRI_INF_F, TRI_INF_F, TRI_INF_F};
    float hi[3] = {-TRI_INF_F, -TRI_INF_F, -TRI_INF_F};
    for (int k = 0; k < 4; k++) {
        if (prims[k] == LRT_TRI_NO_HIT) continue;
        for (int j = 0; j < 9; j++) {
            int a = j % 3;
            float c = vtx[k][j];
            if (c < lo[a]) lo[a] = c;
            if (c > hi[a]) hi[a] = c;
        }
    }
    for (int a = 0; a < 3; a++)
        if (lo[a] > hi[a]) { lo[a] = 0.0f; hi[a] = 0.0f; }
    float margin[3];
    float mf = cons ? tri_qtri_mfrac(fmt) : 0.0f;
    for (int a = 0; a < 3; a++) {
        float ext = hi[a] - lo[a];
        float m = mf * (ext > 1e-20f ? ext : 1.0f);
        margin[a] = m;
    }
    /* 2. dilate (conservative): scale each triangle about its centroid by sigma
     *    (a homothety about an in-plane point - it grows the triangle WITHIN its
     *    own plane, so the decoded triangle contains the true one without
     *    tilting the plane, which would otherwise make grazing rays miss). */
    float M = margin[0] > margin[1] ? margin[0] : margin[1];
    if (margin[2] > M) M = margin[2];
    for (int k = 0; k < 4; k++) {
        if (prims[k] == LRT_TRI_NO_HIT) {
            for (int j = 0; j < 9; j++) work[k][j] = 0.0f;
            continue;
        }
        if (!cons) {
            for (int j = 0; j < 9; j++) work[k][j] = vtx[k][j];
            continue;
        }
        float cen[3];
        for (int a = 0; a < 3; a++)
            cen[a] = (vtx[k][a] + vtx[k][3 + a] + vtx[k][6 + a]) / 3.0f;
        float dmin = TRI_INF_F;
        for (int i = 0; i < 3; i++) {
            float d2 = 0.0f;
            for (int a = 0; a < 3; a++) {
                float d = vtx[k][i * 3 + a] - cen[a];
                d2 += d * d;
            }
            if (d2 < dmin) dmin = d2;
        }
        dmin = sqrtf(dmin);
        float sigma = 1.0f + M / (dmin > 1e-6f ? dmin : 1e-6f);
        if (sigma > 8.0f) sigma = 8.0f; /* clamp for slivers */
        for (int j = 0; j < 9; j++) {
            int a = j % 3;
            work[k][j] = cen[a] + sigma * (vtx[k][j] - cen[a]);
        }
    }
    /* 3. grid from (dilated) work verts. */
    float glo[3] = {TRI_INF_F, TRI_INF_F, TRI_INF_F};
    float ghi[3] = {-TRI_INF_F, -TRI_INF_F, -TRI_INF_F};
    for (int k = 0; k < 4; k++) {
        if (prims[k] == LRT_TRI_NO_HIT) continue;
        for (int j = 0; j < 9; j++) {
            int a = j % 3;
            float c = work[k][j];
            if (c < glo[a]) glo[a] = c;
            if (c > ghi[a]) ghi[a] = c;
        }
    }
    for (int a = 0; a < 3; a++)
        if (glo[a] > ghi[a]) { glo[a] = 0.0f; ghi[a] = 0.0f; }
    float org[3], scale[3], half[3];
    for (int a = 0; a < 3; a++) {
        float ext = ghi[a] - glo[a];
        if (fmt == LRT_QTRI_FP4) {
            org[a] = 0.5f * (glo[a] + ghi[a]); /* center */
            half[a] = ext > 0.0f ? 0.5f * ext * (1.0f + 4e-7f) : 0.0f;
            scale[a] = half[a];
        } else if (fmt == LRT_QTRI_FP8) {
            org[a] = glo[a];
            scale[a] = ext > 0.0f ? ext * (1.0f + 4e-7f) : 0.0f; /* normalize to [0,1] */
            half[a] = 0.0f;
        } else { /* Q8, Q16 (Q16 grid is the scene grid; org/scale below unused) */
            org[a] = glo[a];
            scale[a] = ext > 0.0f ? (ext / 255.0f) * (1.0f + 4e-7f) : 0.0f;
            half[a] = 0.0f;
        }
    }
    /* 4. store grid + encode. */
    if (fmt == LRT_QTRI_Q16) {
        lrt_qtri16 *b = (lrt_qtri16 *)blkv;
        uint16_t *q[9] = {b->qv0x, b->qv0y, b->qv0z, b->qv1x, b->qv1y,
                          b->qv1z, b->qv2x, b->qv2y, b->qv2z};
        for (int k = 0; k < 4; k++) {
            for (int j = 0; j < 9; j++)
                q[j][k] = tri_qtri_enc(fmt, work[k][j], s->qgrid_org[j % 3],
                                       s->qgrid_scale[j % 3], 0.0f);
            b->prim_id[k] = prims[k];
        }
    } else if (fmt == LRT_QTRI_FP4) {
        lrt_qtri4 *b = (lrt_qtri4 *)blkv;
        for (int a = 0; a < 3; a++) { b->org[a] = org[a]; b->scale[a] = scale[a]; }
        memset(b->nib, 0, sizeof(b->nib));
        for (int k = 0; k < 4; k++) {
            for (int j = 0; j < 9; j++) {
                uint8_t code = (uint8_t)tri_qtri_enc(fmt, work[k][j], org[j % 3],
                                                     scale[j % 3], half[j % 3]);
                tri_nib_set(b->nib, j * 4 + k, code);
            }
            b->prim_id[k] = prims[k];
        }
    } else { /* Q8, FP8 */
        lrt_qtri8 *b = (lrt_qtri8 *)blkv;
        for (int a = 0; a < 3; a++) { b->org[a] = org[a]; b->scale[a] = scale[a]; }
        uint8_t *q[9] = {b->qv0x, b->qv0y, b->qv0z, b->qv1x, b->qv1y,
                         b->qv1z, b->qv2x, b->qv2y, b->qv2z};
        for (int k = 0; k < 4; k++) {
            for (int j = 0; j < 9; j++)
                q[j][k] = (uint8_t)tri_qtri_enc(fmt, work[k][j], org[j % 3],
                                                scale[j % 3], 0.0f);
            b->prim_id[k] = prims[k];
        }
    }
}

/* de Casteljau sub-cubic extraction (defined with the Bezier intersector). */
static void tri_bez_subcurve(const float cp[16], float a, float b,
                             float out[16]);

/* Emit the triangles of a binary leaf as SoA blocks of the scene's SIMD
 * width; returns a leaf ref. */
static uint32_t tri_emit_leaf(tri_collapse_ctx *cc, const tri_bnode *bn) {
    const tri_build_ctx *bc = cc->bc;
    lrt_tri_scene *s = cc->s;
    const uint32_t bw = (uint32_t)cc->width;
    uint32_t count = bn->count;
    uint32_t nblocks = (count + bw - 1u) / bw;
    if (s->block_count + nblocks > cc->block_cap || nblocks > 15u) {
        cc->failed = 1;
        return TRI_MAKE_LEAF_REF(0, 0);
    }
    uint32_t first_block = s->block_count;
    if (bc->subsegs) { /* capsule leaves */
        for (uint32_t b = 0; b < nblocks; b++) {
            lrt_crv4 *blk = (lrt_crv4 *)(void *)tri_block_floats(
                s->blocks, s->block_count, cc->width);
            s->block_count++;
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint32_t k = b * 4u + lane;
                if (k < count) {
                    const tri_subseg *ss =
                        &bc->subsegs[bc->indices[bn->a + k]];
                    blk->p0x[lane] = ss->p0[0];
                    blk->p0y[lane] = ss->p0[1];
                    blk->p0z[lane] = ss->p0[2];
                    blk->dx[lane] = ss->p1[0] - ss->p0[0];
                    blk->dy[lane] = ss->p1[1] - ss->p0[1];
                    blk->dz[lane] = ss->p1[2] - ss->p0[2];
                    blk->rad[lane] = ss->r;
                    blk->u0[lane] = ss->u0;
                    blk->u1[lane] = ss->u1;
                    blk->prim[lane] = ss->prim;
                } else {
                    blk->p0x[lane] = blk->p0y[lane] = blk->p0z[lane] = 0.0f;
                    blk->dx[lane] = blk->dy[lane] = blk->dz[lane] = 0.0f;
                    blk->rad[lane] = 0.0f;
                    blk->u0[lane] = blk->u1[lane] = 0.0f;
                    blk->prim[lane] = LRT_TRI_NO_HIT;
                }
            }
        }
        cc->leaf_count++;
        cc->sah_leaf +=
            (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
            (double)count;
        return TRI_MAKE_LEAF_REF(first_block, nblocks);
    }
    if (bc->emit_kind == TRI_PRIM_USER) { /* user-geometry leaves (BVH4) */
        for (uint32_t b = 0; b < nblocks; b++) {
            lrt_user4 *blk = (lrt_user4 *)(void *)tri_block_floats(
                s->blocks, s->block_count, cc->width);
            s->block_count++;
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint32_t k = b * 4u + lane;
                if (k < count) {
                    uint32_t prim = bc->indices[bn->a + k];
                    const float *bx = &bc->user_aabbs[(size_t)prim * 6];
                    blk->lo_x[lane] = bx[0];
                    blk->lo_y[lane] = bx[1];
                    blk->lo_z[lane] = bx[2];
                    blk->hi_x[lane] = bx[3];
                    blk->hi_y[lane] = bx[4];
                    blk->hi_z[lane] = bx[5];
                    blk->prim[lane] = prim;
                } else {
                    blk->lo_x[lane] = blk->lo_y[lane] = blk->lo_z[lane] =
                        TRI_INF_F;
                    blk->hi_x[lane] = blk->hi_y[lane] = blk->hi_z[lane] =
                        -TRI_INF_F;
                    blk->prim[lane] = LRT_TRI_NO_HIT;
                }
            }
        }
        cc->leaf_count++;
        cc->sah_leaf +=
            (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
            (double)count;
        return TRI_MAKE_LEAF_REF(first_block, nblocks);
    }
    if (bc->emit_kind == TRI_PRIM_SPHERE) { /* analytic sphere leaves (BVH4) */
        for (uint32_t b = 0; b < nblocks; b++) {
            lrt_sph4 *blk = (lrt_sph4 *)(void *)tri_block_floats(
                s->blocks, s->block_count, cc->width);
            s->block_count++;
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint32_t k = b * 4u + lane;
                if (k < count) {
                    uint32_t prim = bc->indices[bn->a + k];
                    const float *sp = &bc->spheres[(size_t)prim * 4];
                    blk->cx[lane] = sp[0];
                    blk->cy[lane] = sp[1];
                    blk->cz[lane] = sp[2];
                    blk->r[lane] = sp[3];
                    blk->prim[lane] = prim;
                } else {
                    blk->cx[lane] = blk->cy[lane] = blk->cz[lane] = 0.0f;
                    blk->r[lane] = 0.0f;
                    blk->prim[lane] = LRT_TRI_NO_HIT;
                }
            }
        }
        cc->leaf_count++;
        cc->sah_leaf +=
            (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
            (double)count;
        return TRI_MAKE_LEAF_REF(first_block, nblocks);
    }
    if (bc->emit_kind == TRI_PRIM_RLCURVE) { /* round-linear curve leaves (BVH4) */
        for (uint32_t b = 0; b < nblocks; b++) {
            lrt_rlc4 *blk =
                (lrt_rlc4 *)tri_block_ptr(s->blocks, s->block_count, s->block_stride);
            s->block_count++;
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint32_t k = b * 4u + lane;
                if (k < count) {
                    const tri_rlcseg *rs = &bc->rlcsegs[bc->indices[bn->a + k]];
                    blk->p0x[lane] = rs->p0[0];
                    blk->p0y[lane] = rs->p0[1];
                    blk->p0z[lane] = rs->p0[2];
                    blk->r0[lane] = rs->r0;
                    blk->p1x[lane] = rs->p1[0];
                    blk->p1y[lane] = rs->p1[1];
                    blk->p1z[lane] = rs->p1[2];
                    blk->r1[lane] = rs->r1;
                    blk->pLx[lane] = rs->pL[0];
                    blk->pLy[lane] = rs->pL[1];
                    blk->pLz[lane] = rs->pL[2];
                    blk->rL[lane] = rs->rL;
                    blk->pRx[lane] = rs->pR[0];
                    blk->pRy[lane] = rs->pR[1];
                    blk->pRz[lane] = rs->pR[2];
                    blk->rR[lane] = rs->rR;
                    blk->prim[lane] = rs->prim;
                } else {
                    blk->p0x[lane] = blk->p0y[lane] = blk->p0z[lane] = 0.0f;
                    blk->r0[lane] = 0.0f;
                    blk->p1x[lane] = blk->p1y[lane] = blk->p1z[lane] = 0.0f;
                    blk->r1[lane] = 0.0f;
                    blk->pLx[lane] = blk->pLy[lane] = blk->pLz[lane] = TRI_INF_F;
                    blk->rL[lane] = 0.0f;
                    blk->pRx[lane] = blk->pRy[lane] = blk->pRz[lane] = TRI_INF_F;
                    blk->rR[lane] = 0.0f;
                    blk->prim[lane] = LRT_TRI_NO_HIT;
                }
            }
        }
        cc->leaf_count++;
        cc->sah_leaf +=
            (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
            (double)count;
        return TRI_MAKE_LEAF_REF(first_block, nblocks);
    }
    if (bc->emit_kind == TRI_PRIM_POINT) { /* point leaves (BVH4) */
        for (uint32_t b = 0; b < nblocks; b++) {
            lrt_point4 *blk = (lrt_point4 *)(void *)tri_block_floats(
                s->blocks, s->block_count, cc->width);
            s->block_count++;
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint32_t k = b * 4u + lane;
                if (k < count) {
                    uint32_t prim = bc->indices[bn->a + k];
                    blk->cx[lane] = bc->points_xyz[(size_t)prim * 3 + 0];
                    blk->cy[lane] = bc->points_xyz[(size_t)prim * 3 + 1];
                    blk->cz[lane] = bc->points_xyz[(size_t)prim * 3 + 2];
                    blk->r[lane] = bc->points_r[prim];
                    if (bc->points_n) {
                        blk->nx[lane] = bc->points_n[(size_t)prim * 3 + 0];
                        blk->ny[lane] = bc->points_n[(size_t)prim * 3 + 1];
                        blk->nz[lane] = bc->points_n[(size_t)prim * 3 + 2];
                    } else {
                        blk->nx[lane] = blk->ny[lane] = 0.0f;
                        blk->nz[lane] = 1.0f;
                    }
                    blk->prim[lane] = prim;
                } else {
                    blk->cx[lane] = blk->cy[lane] = blk->cz[lane] = 0.0f;
                    blk->r[lane] = 0.0f;
                    blk->nx[lane] = blk->ny[lane] = 0.0f;
                    blk->nz[lane] = 1.0f;
                    blk->prim[lane] = LRT_TRI_NO_HIT;
                }
            }
        }
        cc->leaf_count++;
        cc->sah_leaf +=
            (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
            (double)count;
        return TRI_MAKE_LEAF_REF(first_block, nblocks);
    }
    if (bc->emit_kind == TRI_PRIM_FLATCURVE) { /* flat (ribbon) curve leaves */
        for (uint32_t b = 0; b < nblocks; b++) {
            lrt_flat4 *blk =
                (lrt_flat4 *)tri_block_ptr(s->blocks, s->block_count, s->block_stride);
            s->block_count++;
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint32_t k = b * 4u + lane;
                if (k < count) {
                    const tri_rlcseg *rs = &bc->rlcsegs[bc->indices[bn->a + k]];
                    blk->p0x[lane] = rs->p0[0];
                    blk->p0y[lane] = rs->p0[1];
                    blk->p0z[lane] = rs->p0[2];
                    blk->r0[lane] = rs->r0;
                    blk->p1x[lane] = rs->p1[0];
                    blk->p1y[lane] = rs->p1[1];
                    blk->p1z[lane] = rs->p1[2];
                    blk->r1[lane] = rs->r1;
                    blk->prim[lane] = rs->prim;
                } else {
                    blk->p0x[lane] = blk->p0y[lane] = blk->p0z[lane] = 0.0f;
                    blk->r0[lane] = 0.0f;
                    blk->p1x[lane] = blk->p1y[lane] = blk->p1z[lane] = 0.0f;
                    blk->r1[lane] = 0.0f;
                    blk->prim[lane] = LRT_TRI_NO_HIT;
                }
            }
        }
        cc->leaf_count++;
        cc->sah_leaf +=
            (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
            (double)count;
        return TRI_MAKE_LEAF_REF(first_block, nblocks);
    }
    if (bc->emit_kind == TRI_PRIM_BEZCURVE) { /* round Bezier curve leaves */
        for (uint32_t b = 0; b < nblocks; b++) {
            lrt_bez4 *blk =
                (lrt_bez4 *)tri_block_ptr(s->blocks, s->block_count, s->block_stride);
            s->block_count++;
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint32_t k = b * 4u + lane;
                float v[16];
                uint32_t prim;
                if (k < count) {
                    uint32_t sub = bc->indices[bn->a + k];
                    uint32_t sp = bc->bez_split ? bc->bez_split : 1u;
                    uint32_t seg = sub / sp, kk = sub % sp;
                    /* materialize this leaf's sub-cubic from the original cubic
                     * (no pre-stored sub-CP array). */
                    tri_bez_subcurve(&bc->bez_cps[(size_t)seg * 16],
                                     (float)kk / (float)sp,
                                     (float)(kk + 1) / (float)sp, v);
                    prim = seg;
                } else {
                    prim = LRT_TRI_NO_HIT;
                    memset(v, 0, sizeof(v));
                }
                blk->b0x[lane] = v[0]; blk->b0y[lane] = v[1];
                blk->b0z[lane] = v[2]; blk->b0r[lane] = v[3];
                blk->b1x[lane] = v[4]; blk->b1y[lane] = v[5];
                blk->b1z[lane] = v[6]; blk->b1r[lane] = v[7];
                blk->b2x[lane] = v[8]; blk->b2y[lane] = v[9];
                blk->b2z[lane] = v[10]; blk->b2r[lane] = v[11];
                blk->b3x[lane] = v[12]; blk->b3y[lane] = v[13];
                blk->b3z[lane] = v[14]; blk->b3r[lane] = v[15];
                blk->prim[lane] = prim;
            }
        }
        cc->leaf_count++;
        cc->sah_leaf +=
            (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
            (double)count;
        return TRI_MAKE_LEAF_REF(first_block, nblocks);
    }
    if (bc->emit_kind == TRI_PRIM_QTRI) { /* quantized triangle leaves (BVH4) */
        for (uint32_t b = 0; b < nblocks; b++) {
            void *blk = tri_block_ptr(s->blocks, s->block_count, s->block_stride);
            s->block_count++;
            float vtx[4][9];
            uint32_t prims[4];
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint32_t k = b * 4u + lane;
                if (k < count) {
                    uint32_t prim = bc->indices[bn->a + k];
                    prims[lane] = prim;
                    memcpy(vtx[lane], &bc->verts[(size_t)prim * 9],
                           9 * sizeof(float));
                } else {
                    prims[lane] = LRT_TRI_NO_HIT;
                    memset(vtx[lane], 0, 9 * sizeof(float));
                }
            }
            tri_qtri_emit_block(s, blk, vtx, prims);
        }
        cc->leaf_count++;
        cc->sah_leaf +=
            (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
            (double)count;
        return TRI_MAKE_LEAF_REF(first_block, nblocks);
    }
    for (uint32_t b = 0; b < nblocks; b++) {
        float *f = (float *)(void *)tri_block_floats(s->blocks, s->block_count,
                                                     cc->width);
        uint32_t *ids = (uint32_t *)(f + 9u * bw);
        s->block_count++;
        for (uint32_t lane = 0; lane < bw; lane++) {
            uint32_t k = b * bw + lane;
            if (k < count) {
                uint32_t prim = bc->indices[bn->a + k];
                const float *v = &bc->verts[(size_t)prim * 9];
                f[0 * bw + lane] = v[0];
                f[1 * bw + lane] = v[1];
                f[2 * bw + lane] = v[2];
                f[3 * bw + lane] = v[3] - v[0];
                f[4 * bw + lane] = v[4] - v[1];
                f[5 * bw + lane] = v[5] - v[2];
                f[6 * bw + lane] = v[6] - v[0];
                f[7 * bw + lane] = v[7] - v[1];
                f[8 * bw + lane] = v[8] - v[2];
                ids[lane] = prim;
            } else {
                for (int arr = 0; arr < 9; arr++) f[(uint32_t)arr * bw + lane] = 0.0f;
                ids[lane] = LRT_TRI_NO_HIT;
            }
        }
    }
    cc->leaf_count++;
    cc->sah_leaf += (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area) *
                    (double)count;
    return TRI_MAKE_LEAF_REF(first_block, nblocks);
}

static uint32_t tri_collapse(tri_collapse_ctx *cc, uint32_t b_idx,
                             uint32_t depth);

/* Fill one wide-node slot from binary node m_idx. */
static void tri_fill_slot(tri_collapse_ctx *cc, uint32_t m_idx, uint32_t depth,
                          float *lo_x, float *lo_y, float *lo_z, float *hi_x,
                          float *hi_y, float *hi_z, uint32_t *child, int slot) {
    const tri_bnode *m = &cc->bc->bnodes[m_idx];
    lo_x[slot] = m->lo[0];
    lo_y[slot] = m->lo[1];
    lo_z[slot] = m->lo[2];
    hi_x[slot] = m->hi[0];
    hi_y[slot] = m->hi[1];
    hi_z[slot] = m->hi[2];
    child[slot] = tri_collapse(cc, m_idx, depth);
}

/* E4M3 conservative node-bound quantization (normalized [0,1]; lo down, hi up
 * so the decoded box contains the true one). */
static inline uint8_t tri_e4m3_q_lo(float v, float org, float ext) {
    if (ext <= 0.0f) return 0;
    float n = (v - org) / ext;
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    uint8_t c = tri_e4m3_encode(n);
    while (c > 0 && tri_e4m3_decode(c) > n) c--;
    return c;
}
static inline uint8_t tri_e4m3_q_hi(float v, float org, float ext) {
    if (ext <= 0.0f) return 0;
    float n = (v - org) / ext;
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    uint8_t c = tri_e4m3_encode(n);
    while (c < 0x7Eu && tri_e4m3_decode(c) < n) c++;
    return c;
}
/* 4-bit linear conservative quantization (org + q*scale, scale = ext/15). */
static inline uint8_t tri_q4_lo(float v, float org, float scale) {
    if (scale <= 0.0f) return 0;
    int q = (int)((v - org) / scale);
    if (q < 0) q = 0;
    if (q > 15) q = 15;
    while (q > 0 && org + (float)q * scale > v) q--;
    return (uint8_t)q;
}
static inline uint8_t tri_q4_hi(float v, float org, float scale) {
    if (scale <= 0.0f) return 0;
    int q = (int)((v - org) / scale) + 1;
    if (q < 0) q = 0;
    if (q > 15) q = 15;
    while (q < 15 && org + (float)q * scale < v) q++;
    return (uint8_t)q;
}
static inline void tri_q4_set(uint8_t packed[4], int child, uint8_t v) {
    uint8_t *b = &packed[child >> 1];
    if (child & 1)
        *b = (uint8_t)((*b & 0x0F) | (v << 4));
    else
        *b = (uint8_t)((*b & 0xF0) | (v & 0x0F));
}

/* Collapse the binary subtree at b_idx into a wide node (or leaf ref). */
static uint32_t tri_collapse(tri_collapse_ctx *cc, uint32_t b_idx,
                             uint32_t depth) {
    if (cc->failed) return 0;
    const tri_bnode *bn = &cc->bc->bnodes[b_idx];
    if (depth > cc->max_depth) cc->max_depth = depth;

    if (bn->count > 0) return tri_emit_leaf(cc, bn);

    /* Gather up to `width` binary children by repeatedly expanding the inner
     * member with the largest surface area. */
    uint32_t set[8];
    int n = 0;
    set[n++] = bn->a;
    set[n++] = bn->b;
    const int width = cc->width;
    while (n < width) {
        int expand = -1;
        float best_area = -1.0f;
        for (int i = 0; i < n; i++) {
            const tri_bnode *m = &cc->bc->bnodes[set[i]];
            if (m->count > 0) continue; /* leaf: cannot expand */
            float area = tri_surface_area(m->lo, m->hi);
            if (area > best_area) {
                best_area = area;
                expand = i;
            }
        }
        if (expand < 0) break;
        uint32_t inner = set[expand];
        const tri_bnode *m = &cc->bc->bnodes[inner];
        set[expand] = m->a;
        set[n++] = m->b;
    }

    lrt_tri_scene *s = cc->s;
    if (s->node_count >= cc->node_cap) {
        cc->failed = 1;
        return 0;
    }
    uint32_t node_idx = s->node_count++;
    cc->sah_inner += (double)(tri_surface_area(bn->lo, bn->hi) / cc->root_area);

    if (width == 4) {
        lrt_bvh4_node *w = &s->nodes4[node_idx];
        memset(w, 0, sizeof(*w));
        /* Empty slots: a degenerate point box at +INF. SIMD kernels also mask
         * lanes >= nchildren, so these values are only a second line of
         * defense (a min/max slab test would otherwise accept an inverted
         * lo > hi box after its min/max swap). */
        for (int i = 0; i < 4; i++) {
            w->lo_x[i] = w->lo_y[i] = w->lo_z[i] = TRI_INF_F;
            w->hi_x[i] = w->hi_y[i] = w->hi_z[i] = TRI_INF_F;
        }
        w->nchildren = (uint32_t)n;
        for (int i = 0; i < n; i++) {
            /* w may move only if nodes4 reallocated; capacity is fixed. */
            tri_fill_slot(cc, set[i], depth + 1, s->nodes4[node_idx].lo_x,
                          s->nodes4[node_idx].lo_y, s->nodes4[node_idx].lo_z,
                          s->nodes4[node_idx].hi_x, s->nodes4[node_idx].hi_y,
                          s->nodes4[node_idx].hi_z, s->nodes4[node_idx].child,
                          i);
        }
        /* Per-octant approximate front-to-back slot order: sort children by
         * their center projected onto the octant's direction signs. */
        w = &s->nodes4[node_idx];
        for (int oct = 0; oct < 8; oct++) {
            float key[4];
            uint8_t order[4];
            for (int i = 0; i < n; i++) {
                float cx = 0.5f * (w->lo_x[i] + w->hi_x[i]);
                float cy = 0.5f * (w->lo_y[i] + w->hi_y[i]);
                float cz = 0.5f * (w->lo_z[i] + w->hi_z[i]);
                key[i] = (oct & 1 ? -cx : cx) + (oct & 2 ? -cy : cy) +
                         (oct & 4 ? -cz : cz);
                order[i] = (uint8_t)i;
            }
            for (int i = 1; i < n; i++) { /* nearest first */
                uint8_t oi = order[i];
                float ki = key[oi];
                int j = i;
                while (j > 0 && key[order[j - 1]] > ki) {
                    order[j] = order[j - 1];
                    j--;
                }
                order[j] = oi;
            }
            uint8_t p = 0;
            for (int i = 0; i < n; i++) p |= (uint8_t)(order[i] << (2 * i));
            for (int i = n; i < 4; i++) p |= (uint8_t)(i << (2 * i));
            w->perm[oct] = p;
        }
    } else if (cc->quantized && cc->qnode == 2) { /* 4-bit nodes */
        lrt_bvh8q4_node *w = &s->nodes8q4[node_idx];
        memset(w, 0, sizeof(*w));
        float scale[3];
        for (int a = 0; a < 3; a++) {
            w->org[a] = bn->lo[a];
            float ext = bn->hi[a] - bn->lo[a];
            scale[a] = ext > 0.0f ? (ext / 15.0f) * (1.0f + 4e-6f) : 0.0f;
            w->scale[a] = scale[a];
            for (int i = 0; i < 8; i++) {
                tri_q4_set(w->qlo[a], i, 15);
                tri_q4_set(w->qhi[a], i, 0);
            }
        }
        w->nchildren = (uint32_t)n;
        for (int i = 0; i < n; i++) {
            const tri_bnode *m = &cc->bc->bnodes[set[i]];
            w = &s->nodes8q4[node_idx];
            for (int a = 0; a < 3; a++) {
                tri_q4_set(w->qlo[a], i, tri_q4_lo(m->lo[a], w->org[a], scale[a]));
                tri_q4_set(w->qhi[a], i, tri_q4_hi(m->hi[a], w->org[a], scale[a]));
            }
            uint32_t ref = tri_collapse(cc, set[i], depth + 1);
            s->nodes8q4[node_idx].child[i] = ref;
        }
    } else if (cc->quantized) { /* 8-bit linear (qnode 0) or E4M3 fp8 (qnode 1) */
        const int fp8 = (cc->qnode == 1);
        lrt_bvh8q_node *w = &s->nodes8q[node_idx];
        memset(w, 0, sizeof(*w));
        float scale[3], inv_scale[3];
        for (int a = 0; a < 3; a++) {
            w->org[a] = bn->lo[a];
            float ext = bn->hi[a] - bn->lo[a];
            /* fp8 stores the full extent; linear stores ext/255. The small
             * relative inflation keeps the decoded box >= the true one. */
            scale[a] = ext > 0.0f
                           ? (fp8 ? ext * (1.0f + 4e-7f)
                                  : (ext / 255.0f) * (1.0f + 4e-7f))
                           : 0.0f;
            inv_scale[a] = scale[a] > 0.0f ? 1.0f / scale[a] : 0.0f;
            w->scale[a] = scale[a];
        }
        for (int i = 0; i < 8; i++) { /* empty slots decode inverted */
            w->qlo_x[i] = w->qlo_y[i] = w->qlo_z[i] = 255;
            w->qhi_x[i] = w->qhi_y[i] = w->qhi_z[i] = 0;
        }
        w->nchildren = (uint32_t)n;
        for (int i = 0; i < n; i++) {
            const tri_bnode *m = &cc->bc->bnodes[set[i]];
            w = &s->nodes8q[node_idx];
            uint8_t *ql[3] = {w->qlo_x, w->qlo_y, w->qlo_z};
            uint8_t *qh[3] = {w->qhi_x, w->qhi_y, w->qhi_z};
            for (int a = 0; a < 3; a++) {
                ql[a][i] = fp8 ? tri_e4m3_q_lo(m->lo[a], w->org[a], w->scale[a])
                               : tri_quantize_lo(m->lo[a], w->org[a], scale[a],
                                                 inv_scale[a]);
                qh[a][i] = fp8 ? tri_e4m3_q_hi(m->hi[a], w->org[a], w->scale[a])
                               : tri_quantize_hi(m->hi[a], w->org[a], scale[a],
                                                 inv_scale[a]);
            }
            uint32_t ref = tri_collapse(cc, set[i], depth + 1);
            s->nodes8q[node_idx].child[i] = ref;
        }
    } else {
        lrt_bvh8_node *w = &s->nodes8[node_idx];
        memset(w, 0, sizeof(*w));
        for (int i = 0; i < 8; i++) {
            w->lo_x[i] = w->lo_y[i] = w->lo_z[i] = TRI_INF_F;
            w->hi_x[i] = w->hi_y[i] = w->hi_z[i] = TRI_INF_F;
        }
        w->nchildren = (uint32_t)n;
        for (int i = 0; i < n; i++) {
            tri_fill_slot(cc, set[i], depth + 1, s->nodes8[node_idx].lo_x,
                          s->nodes8[node_idx].lo_y, s->nodes8[node_idx].lo_z,
                          s->nodes8[node_idx].hi_x, s->nodes8[node_idx].hi_y,
                          s->nodes8[node_idx].hi_z, s->nodes8[node_idx].child,
                          i);
        }
    }
    return TRI_MAKE_NODE_REF(node_idx);
}

/* ------------------------------------------------------------------------- */
/* Ray setup shared by all kernels.                                          */
/* ------------------------------------------------------------------------- */

typedef struct tri_ray_ctx {
    float org[3];
    float dir[3];
    float invd[3]; /* infinite components replaced by clamped huge values */
    float oinv[3]; /* org * invd, clamped finite: slab planes become FMAs */
    float tmin;
    int octant; /* direction sign bits: 1=x neg, 2=y neg, 4=z neg */
} tri_ray_ctx;

/* Degenerate direction components get a finite huge inverse instead of inf.
 * 1e18 (not FLT_MAX) so that the quantized-node decode, which multiplies
 * invd by a node-local scale, cannot overflow to inf and produce 0 * inf
 * NaNs; |dir| < 1e-18 is treated as zero. */
#define TRI_INVD_MAX 1e18f

static inline void tri_ray_setup(const lrt_ray *ray, tri_ray_ctx *rc) {
    for (int k = 0; k < 3; k++) {
        rc->org[k] = ray->org[k];
        rc->dir[k] = ray->dir[k];
        float inv = 1.0f / ray->dir[k];
        if (!(inv >= -TRI_INVD_MAX && inv <= TRI_INVD_MAX)) {
            inv = copysignf(TRI_INVD_MAX, ray->dir[k] == 0.0f ? 1.0f : ray->dir[k]);
        }
        rc->invd[k] = inv;
        /* Clamped so that fmsub(bound, invd, oinv) can never see inf - inf:
         * bound*invd may overflow to +/-inf, but oinv stays finite. */
        float oi = ray->org[k] * inv;
        if (!(oi >= -1e37f && oi <= 1e37f)) oi = copysignf(1e37f, oi);
        rc->oinv[k] = oi;
    }
    rc->octant = (rc->invd[0] < 0.0f ? 1 : 0) | (rc->invd[1] < 0.0f ? 2 : 0) |
                 (rc->invd[2] < 0.0f ? 4 : 0);
    rc->tmin = ray->tmin;
}

typedef struct tri_stack_entry {
    uint32_t ref;
    float tnear;
} tri_stack_entry;

#if LRT_TRI_HAS_SSE4
/* Prefetch the data a child ref will touch when popped: the bounds planes of
 * an inner node (2 lines for BVH4, 3 for BVH8), or the first triangle block
 * of a leaf. Incoherent rays make these fetches the critical path. */
static inline void tri_prefetch_ref(const lrt_tri_scene *s, uint32_t ref,
                                    int width) {
    (void)width;
    if (TRI_REF_IS_LEAF(ref)) {
        const char *b = (const char *)tri_block_ptr(
            s->blocks, TRI_REF_BLOCK(ref), s->block_stride);
        _mm_prefetch(b, _MM_HINT_T0);
        _mm_prefetch(b + 64, _MM_HINT_T0);
        return;
    }
    if (s->qnode == 2) {
        const char *p = (const char *)&s->nodes8q4[TRI_REF_NODE(ref)];
        _mm_prefetch(p, _MM_HINT_T0);
        _mm_prefetch(p + 64, _MM_HINT_T0);
        return;
    }
    if (s->quantized) {
        const char *p = (const char *)&s->nodes8q[TRI_REF_NODE(ref)];
        _mm_prefetch(p, _MM_HINT_T0);
        _mm_prefetch(p + 64, _MM_HINT_T0);
        return;
    }
    const char *p = (width == 4) ? (const char *)&s->nodes4[TRI_REF_NODE(ref)]
                                 : (const char *)&s->nodes8[TRI_REF_NODE(ref)];
    _mm_prefetch(p, _MM_HINT_T0);
    _mm_prefetch(p + 64, _MM_HINT_T0);
    if (width == 8) {
        _mm_prefetch(p + 128, _MM_HINT_T0);
        _mm_prefetch(p + 192, _MM_HINT_T0); /* child refs */
    }
}
#endif

/* ------------------------------------------------------------------------- */
/* Scalar kernels (always compiled; fallback + correctness oracle).          */
/* ------------------------------------------------------------------------- */

static inline int tri_isect_det_parallel(float e1x, float e1y, float e1z,
                                         float e2x, float e2y, float e2z,
                                         float det) {
    float e1_len = sqrtf(e1x * e1x + e1y * e1y + e1z * e1z);
    float e2_len = sqrtf(e2x * e2x + e2y * e2y + e2z * e2z);
    float scale = e1_len + e2_len;
    float rel_thresh = TRI_ISECT_DET_EPS_SCALE * scale * scale;
    if (rel_thresh < TRI_ISECT_DET_EPS_MIN) rel_thresh = TRI_ISECT_DET_EPS_MIN;
    if (rel_thresh > TRI_ISECT_DET_EPS_MAX) rel_thresh = TRI_ISECT_DET_EPS_MAX;
    return (det > -rel_thresh && det < rel_thresh);
}

#if LRT_TRI_HAS_SSE4
static inline __m128 tri_isect_det_parallel4(__m128 e1x, __m128 e1y, __m128 e1z,
                                             __m128 e2x, __m128 e2y, __m128 e2z,
                                             __m128 det) {
    const __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    __m128 e1_len2 = _mm_add_ps(_mm_mul_ps(e1x, e1x), _mm_mul_ps(e1y, e1y));
    __m128 e2_len2 = _mm_add_ps(_mm_mul_ps(e2x, e2x), _mm_mul_ps(e2y, e2y));
    e1_len2 = _mm_add_ps(e1_len2, _mm_mul_ps(e1z, e1z));
    e2_len2 = _mm_add_ps(e2_len2, _mm_mul_ps(e2z, e2z));
    __m128 e1_len = _mm_sqrt_ps(_mm_max_ps(e1_len2, _mm_set1_ps(0.0f)));
    __m128 e2_len = _mm_sqrt_ps(_mm_max_ps(e2_len2, _mm_set1_ps(0.0f)));
    __m128 scale = _mm_add_ps(e1_len, e2_len);
    __m128 rel = _mm_mul_ps(_mm_set1_ps(TRI_ISECT_DET_EPS_SCALE),
                            _mm_mul_ps(scale, scale));
    rel = _mm_max_ps(_mm_set1_ps(TRI_ISECT_DET_EPS_MIN), rel);
    rel = _mm_min_ps(_mm_set1_ps(TRI_ISECT_DET_EPS_MAX), rel);
    return _mm_cmpgt_ps(_mm_and_ps(det, abs_mask), rel);
}
#endif

#if LRT_TRI_HAS_AVX2
static inline __m256 tri_isect_det_parallel8(__m256 e1x, __m256 e1y, __m256 e1z,
                                             __m256 e2x, __m256 e2y, __m256 e2z,
                                             __m256 det) {
    const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
    __m256 e1_len2 = _mm256_add_ps(_mm256_mul_ps(e1x, e1x), _mm256_mul_ps(e1y, e1y));
    __m256 e2_len2 = _mm256_add_ps(_mm256_mul_ps(e2x, e2x), _mm256_mul_ps(e2y, e2y));
    e1_len2 = _mm256_add_ps(e1_len2, _mm256_mul_ps(e1z, e1z));
    e2_len2 = _mm256_add_ps(e2_len2, _mm256_mul_ps(e2z, e2z));
    __m256 e1_len = _mm256_sqrt_ps(_mm256_max_ps(e1_len2, _mm256_set1_ps(0.0f)));
    __m256 e2_len = _mm256_sqrt_ps(_mm256_max_ps(e2_len2, _mm256_set1_ps(0.0f)));
    __m256 scale = _mm256_add_ps(e1_len, e2_len);
    __m256 rel = _mm256_mul_ps(_mm256_set1_ps(TRI_ISECT_DET_EPS_SCALE),
                               _mm256_mul_ps(scale, scale));
    rel = _mm256_max_ps(_mm256_set1_ps(TRI_ISECT_DET_EPS_MIN), rel);
    rel = _mm256_min_ps(_mm256_set1_ps(TRI_ISECT_DET_EPS_MAX), rel);
    return _mm256_cmp_ps(_mm256_and_ps(det, abs_mask),
                         rel, _CMP_GT_OQ);
}
#endif

/* Moller-Trumbore on one lane of a generic-width block (f = 9 float arrays of
 * `bw` lanes). Returns 1 on hit in (tmin, t_best). */
static inline int tri_isect_lane(const float *f, int bw, int lane,
                                 const tri_ray_ctx *rc, float t_best, float *t,
                                 float *u, float *v) {
    float e1x = f[3 * bw + lane], e1y = f[4 * bw + lane], e1z = f[5 * bw + lane];
    float e2x = f[6 * bw + lane], e2y = f[7 * bw + lane], e2z = f[8 * bw + lane];
    float px = rc->dir[1] * e2z - rc->dir[2] * e2y;
    float py = rc->dir[2] * e2x - rc->dir[0] * e2z;
    float pz = rc->dir[0] * e2y - rc->dir[1] * e2x;
    float det = e1x * px + e1y * py + e1z * pz;
    if (tri_isect_det_parallel(e1x, e1y, e1z, e2x, e2y, e2z, det)) return 0;
    float inv_det = 1.0f / det;
    float tvx = rc->org[0] - f[0 * bw + lane];
    float tvy = rc->org[1] - f[1 * bw + lane];
    float tvz = rc->org[2] - f[2 * bw + lane];
    float uu = (tvx * px + tvy * py + tvz * pz) * inv_det;
    if (uu < 0.0f || uu > 1.0f) return 0;
    float qx = tvy * e1z - tvz * e1y;
    float qy = tvz * e1x - tvx * e1z;
    float qz = tvx * e1y - tvy * e1x;
    float vv = (rc->dir[0] * qx + rc->dir[1] * qy + rc->dir[2] * qz) * inv_det;
    if (vv < 0.0f || uu + vv > 1.0f) return 0;
    float tt = (e2x * qx + e2y * qy + e2z * qz) * inv_det;
    if (tt < rc->tmin || tt >= t_best) return 0;
    *t = tt;
    *u = uu;
    *v = vv;
    return 1;
}

/* Ray vs capsule (segment p0 + d swept by radius r). Reports the nearest
 * t in [tmin, t_max) and the axial parameter s in [0,1]. Padding lanes have
 * r == 0 and never hit. */
static int tri_capsule_isect(float p0x, float p0y, float p0z, float dx,
                             float dy, float dz, float r,
                             const tri_ray_ctx *rc, float t_max, float *t_out,
                             float *s_out) {
    if (r <= 0.0f) return 0;
    float mx = rc->org[0] - p0x, my = rc->org[1] - p0y, mz = rc->org[2] - p0z;
    float ox = rc->dir[0], oy = rc->dir[1], oz = rc->dir[2];
    float dd = dx * dx + dy * dy + dz * dz;
    float best = t_max;
    float best_s = 0.0f;
    int hit = 0;

    if (dd > 1e-20f) {
        float inv_dd = 1.0f / dd;
        float nd = (ox * dx + oy * dy + oz * dz) * inv_dd;
        float md = (mx * dx + my * dy + mz * dz) * inv_dd;
        /* components of dir and m orthogonal to the axis */
        float ax = ox - dx * nd, ay = oy - dy * nd, az = oz - dz * nd;
        float bx = mx - dx * md, by = my - dy * md, bz = mz - dz * md;
        float A = ax * ax + ay * ay + az * az;
        float B = ax * bx + ay * by + az * bz;
        float C = bx * bx + by * by + bz * bz - r * r;
        if (A > TRI_ISECT_DET_EPS_MIN) {
            float disc = B * B - A * C;
            if (disc >= 0.0f) {
                float sq = sqrtf(disc);
                float inv_a = 1.0f / A;
                for (int k = 0; k < 2; k++) {
                    float t = (k == 0 ? (-B - sq) : (-B + sq)) * inv_a;
                    if (t < rc->tmin || t >= best) continue;
                    float s = md + t * nd;
                    if (s >= 0.0f && s <= 1.0f) {
                        best = t;
                        best_s = s;
                        hit = 1;
                        break; /* roots are ordered */
                    }
                }
            }
        }
    }

    /* End-cap spheres at p0 (s=0) and p1 (s=1). */
    float a2 = ox * ox + oy * oy + oz * oz;
    if (a2 > 1e-20f) {
        for (int cap = 0; cap < 2; cap++) {
            float cx = cap ? mx - dx : mx;
            float cy = cap ? my - dy : my;
            float cz = cap ? mz - dz : mz;
            float b = cx * ox + cy * oy + cz * oz;
            float cc = cx * cx + cy * cy + cz * cz - r * r;
            float disc = b * b - a2 * cc;
            if (disc < 0.0f) continue;
            float t = (-b - sqrtf(disc)) / a2;
            if (t >= rc->tmin && t < best) {
                best = t;
                best_s = cap ? 1.0f : 0.0f;
                hit = 1;
            }
        }
    }

    if (hit) {
        *t_out = best;
        *s_out = best_s;
    }
    return hit;
}

/* One capsule leaf block, scalar lanes. Updates the best hit in place. */
static inline void tri_crv4_isect(const lrt_crv4 *blk, const tri_ray_ctx *rc,
                                  float *best_t, float *best_u,
                                  uint32_t *best_prim) {
    for (int lane = 0; lane < 4; lane++) {
        if (blk->prim[lane] == LRT_TRI_NO_HIT) continue;
        float t, s;
        if (tri_capsule_isect(blk->p0x[lane], blk->p0y[lane], blk->p0z[lane],
                              blk->dx[lane], blk->dy[lane], blk->dz[lane],
                              blk->rad[lane], rc, *best_t, &t, &s)) {
            *best_t = t;
            *best_u = blk->u0[lane] + s * (blk->u1[lane] - blk->u0[lane]);
            *best_prim = blk->prim[lane];
        }
    }
}

/* ---- Round-linear curve (Embree round_linear_curve) intersection ---------- *
 * Scalar port of embree kernels/geometry/roundline_intersector.h: each segment
 * is a cone tangent to its two end spheres (varying radius r0->r1), CSG-clipped
 * at the joints against the strand neighbors so abutting segments do not double
 * up. A neighbor with px == +INF means "no neighbor" (strand start/end). */

#define TRI_RLC_ULP 1.1920929e-7f
#define TRI_RLC_MIN_A 1e-18f

/* Is p inside the capped cone (cp0,cr0)->(cp1,cr1)? (ConeGeometry::isInsideCappedCone) */
static inline int tri_rlc_inside_capped_cone(const float cp0[3], float cr0,
                                             const float cp1[3], float cr1,
                                             const float p[3]) {
    if (!(cp1[0] < TRI_INF_F) || !(cp0[0] < TRI_INF_F)) return 0;
    float dPx = cp1[0] - cp0[0], dPy = cp1[1] - cp0[1], dPz = cp1[2] - cp0[2];
    float dPdP = dPx * dPx + dPy * dPy + dPz * dPz;
    float dr = cr1 - cr0;
    float r0dr = cr0 * dr;
    float g = dPdP - dr * dr;
    float p0px = p[0] - cp0[0], p0py = p[1] - cp0[1], p0pz = p[2] - cp0[2];
    float y = p0px * dPx + p0py * dPy + p0pz * dPz;
    if (!(y > -r0dr + TRI_RLC_ULP)) return 0; /* start plane */
    if (!(y < -cr1 * dr + dPdP)) return 0;     /* end plane */
    float p0p2 = p0px * p0px + p0py * p0py + p0pz * p0pz;
    return (p0p2 * g - y * y) < (dPdP * cr0 * cr0 + 2.0f * r0dr * y);
}

/* Does the neighbor cone's start plane clip away p? (ConeGeometry::isClippedByPlane) */
static inline int tri_rlc_clipped_by_plane(const float cp0[3], float cr0,
                                           const float cp1[3], float cr1,
                                           const float p[3]) {
    if (!(cp1[0] < TRI_INF_F) || !(cp0[0] < TRI_INF_F)) return 0;
    float dPx = cp1[0] - cp0[0], dPy = cp1[1] - cp0[1], dPz = cp1[2] - cp0[2];
    float r0dr = cr0 * (cr1 - cr0);
    float p0px = p[0] - cp0[0], p0py = p[1] - cp0[1], p0pz = p[2] - cp0[2];
    float y = p0px * dPx + p0py * dPy + p0pz * dPz;
    return y > -r0dr;
}

/* Closest round-linear curve hit for one segment. Returns 1 and fills t/u (u in
 * [0,1] along the segment, 0 at p0) when a hit lies in [rc->tmin, best_t). */
static int tri_rlc_isect_one(const tri_ray_ctx *rc, float best_t,
                             const float p0[3], float r0, const float p1[3],
                             float r1, const float pL[3], float rL,
                             const float pR[3], float rR, float *t_out,
                             float *u_out) {
    const float POS_INF = TRI_INF_F;
    const float NEG_INF = -TRI_INF_F;
    float dirx = rc->dir[0], diry = rc->dir[1], dirz = rc->dir[2];
    float dOdO = dirx * dirx + diry * diry + dirz * dirz;
    if (dOdO <= 0.0f) return 0;
    float rcp_dOdO = 1.0f / dOdO;

    /* re-center ray origin near the segment for numerical stability; the final
     * hit distance is dt + (t solved against the shifted origin). */
    float cx = 0.5f * (p0[0] + p1[0]);
    float cy = 0.5f * (p0[1] + p1[1]);
    float cz = 0.5f * (p0[2] + p1[2]);
    float dt = ((cx - rc->org[0]) * dirx + (cy - rc->org[1]) * diry +
                (cz - rc->org[2]) * dirz) * rcp_dOdO;
    float ox = rc->org[0] + dt * dirx;
    float oy = rc->org[1] + dt * diry;
    float oz = rc->org[2] + dt * dirz;

    float dPx = p1[0] - p0[0], dPy = p1[1] - p0[1], dPz = p1[2] - p0[2];
    float dPdP = dPx * dPx + dPy * dPy + dPz * dPz;
    float dr = r1 - r0;
    float r0dr = r0 * dr;
    float g = dPdP - dr * dr;
    float Ox = ox - p0[0], Oy = oy - p0[1], Oz = oz - p0[2];
    float OdP = Ox * dPx + Oy * dPy + Oz * dPz;
    float dOdP = dirx * dPx + diry * dPy + dirz * dPz;
    float yp = OdP + r0dr;

    /* cone hits (front/back), with the y in [0,g] clip planes */
    float t_cone_lo = POS_INF, t_cone_hi = NEG_INF;
    float y_cone_lo = 0.0f, y_cone_hi = 0.0f;
    int cone_valid = 0;
    {
        float OO = Ox * Ox + Oy * Oy + Oz * Oz;
        float OdO = dirx * Ox + diry * Oy + dirz * Oz;
        float A = g * dOdO - dOdP * dOdP;
        float B = 2.0f * (g * OdO - dOdP * yp);
        float C = g * OO - OdP * OdP - r0 * r0 * dPdP - 2.0f * r0dr * OdP;
        float D = B * B - 4.0f * A * C;
        if (D >= 0.0f && g > 0.0f && (A > TRI_RLC_MIN_A || A < -TRI_RLC_MIN_A)) {
            cone_valid = 1;
            float Q = sqrtf(D);
            float rcp_2A = 1.0f / (2.0f * A);
            float tf = (-B - Q) * rcp_2A;
            float yf = yp + tf * dOdP;
            if (yf > -TRI_RLC_ULP && yf <= g) { t_cone_lo = tf; y_cone_lo = yf; }
            float tb = (-B + Q) * rcp_2A;
            float yb = yp + tb * dOdP;
            if (yb > -TRI_RLC_ULP && yb <= g) { t_cone_hi = tb; y_cone_hi = yb; }
        }
    }
    /* a proper cone (g>0) that the ray misses also misses the inscribed spheres */
    if (!cone_valid && g > 0.0f) return 0;

    /* clip cone hits that fall inside a neighbor segment (joint CSG) */
    if (t_cone_lo != POS_INF) {
        float hp[3] = {ox + t_cone_lo * dirx, oy + t_cone_lo * diry,
                       oz + t_cone_lo * dirz};
        if (tri_rlc_inside_capped_cone(p0, r0, pL, rL, hp) ||
            tri_rlc_inside_capped_cone(p1, r1, pR, rR, hp))
            t_cone_lo = POS_INF;
    }
    if (t_cone_hi != NEG_INF) {
        float hp[3] = {ox + t_cone_hi * dirx, oy + t_cone_hi * diry,
                       oz + t_cone_hi * dirz};
        if (tri_rlc_inside_capped_cone(p0, r0, pL, rL, hp) ||
            tri_rlc_inside_capped_cone(p1, r1, pR, rR, hp))
            t_cone_hi = NEG_INF;
    }

    /* end sphere at p1 (its cap past y>g), clipped by the next segment's cone */
    float t_sph1_lo = POS_INF, t_sph1_hi = NEG_INF;
    {
        float O1x = ox - p1[0], O1y = oy - p1[1], O1z = oz - p1[2];
        float O1dO = O1x * dirx + O1y * diry + O1z * dirz;
        float O1O1 = O1x * O1x + O1y * O1y + O1z * O1z;
        float h2 = O1dO * O1dO - dOdO * (O1O1 - r1 * r1);
        if (h2 >= 0.0f) {
            float rhs = sqrtf(h2);
            float tf = (-O1dO - rhs) * rcp_dOdO;
            float hf[3] = {ox + tf * dirx, oy + tf * diry, oz + tf * dirz};
            if (yp + tf * dOdP > g &&
                !tri_rlc_clipped_by_plane(p1, r1, pR, rR, hf))
                t_sph1_lo = tf;
            float tb = (-O1dO + rhs) * rcp_dOdO;
            float hb[3] = {ox + tb * dirx, oy + tb * diry, oz + tb * dirz};
            if (yp + tb * dOdP > g &&
                !tri_rlc_clipped_by_plane(p1, r1, pR, rR, hb))
                t_sph1_hi = tb;
        }
    }

    /* begin sphere at p0, only at a strand start (no left neighbor) */
    float t_sph0_lo = POS_INF, t_sph0_hi = NEG_INF;
    if (!(pL[0] < TRI_INF_F)) {
        float O0x = ox - p0[0], O0y = oy - p0[1], O0z = oz - p0[2];
        float O0dO = O0x * dirx + O0y * diry + O0z * dirz;
        float O0O0 = O0x * O0x + O0y * O0y + O0z * O0z;
        float h2 = O0dO * O0dO - dOdO * (O0O0 - r0 * r0);
        if (h2 >= 0.0f) {
            float rhs = sqrtf(h2);
            float tf = (-O0dO - rhs) * rcp_dOdO;
            if (yp + tf * dOdP < 0.0f) t_sph0_lo = tf;
            float tb = (-O0dO + rhs) * rcp_dOdO;
            if (yp + tb * dOdP < 0.0f) t_sph0_hi = tb;
        }
    }

    /* CSG union: lower = nearest surface, upper = farthest */
    float t_sph_lo = tri_minf(t_sph0_lo, t_sph1_lo);
    float lo = tri_minf(t_cone_lo, t_sph_lo);
    float t_sph_hi = tri_maxf(t_sph0_hi, t_sph1_hi);
    float hi = tri_maxf(t_cone_hi, t_sph_hi);

    float tmin = rc->tmin;
    int lo_valid = (lo != POS_INF) && (dt + lo >= tmin) && (dt + lo < best_t);
    int hi_valid = (hi != NEG_INF) && (dt + hi >= tmin) && (dt + hi < best_t);
    if (!lo_valid && !hi_valid) return 0;

    float tloc = lo_valid ? lo : hi;
    float u;
    if (tloc == t_cone_lo || tloc == t_cone_hi) {
        float y = (tloc == t_cone_lo) ? y_cone_lo : y_cone_hi;
        u = g > 0.0f ? y / g : 0.0f;
        if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
    } else if (tloc == t_sph0_lo || tloc == t_sph0_hi) {
        u = 0.0f;
    } else {
        u = 1.0f;
    }
    *t_out = dt + tloc;
    *u_out = u;
    return 1;
}

/* One round-linear leaf block (scalar lanes). Updates the best hit in place. */
static inline void tri_rlc4_isect(const lrt_rlc4 *blk, const tri_ray_ctx *rc,
                                  float *best_t, float *best_u,
                                  uint32_t *best_prim) {
    for (int lane = 0; lane < 4; lane++) {
        if (blk->prim[lane] == LRT_TRI_NO_HIT) continue;
        float p0[3] = {blk->p0x[lane], blk->p0y[lane], blk->p0z[lane]};
        float p1[3] = {blk->p1x[lane], blk->p1y[lane], blk->p1z[lane]};
        float pL[3] = {blk->pLx[lane], blk->pLy[lane], blk->pLz[lane]};
        float pR[3] = {blk->pRx[lane], blk->pRy[lane], blk->pRz[lane]};
        float t, u;
        if (tri_rlc_isect_one(rc, *best_t, p0, blk->r0[lane], p1, blk->r1[lane],
                              pL, blk->rL[lane], pR, blk->rR[lane], &t, &u)) {
            *best_t = t;
            *best_u = u;
            *best_prim = blk->prim[lane];
        }
    }
}

/* ---- Point primitives: sphere / ray-facing disc / oriented disc ---------- *
 * Direct port of embree's sphere_intersector.h + disc_intersector.h. */
static int tri_point_isect_one(const tri_ray_ctx *rc, float best_t, float cx,
                               float cy, float cz, float r, float nx, float ny,
                               float nz, int point_type, float *t_out,
                               float *u_out) {
    float dx = rc->dir[0], dy = rc->dir[1], dz = rc->dir[2];
    float dOdO = dx * dx + dy * dy + dz * dz;
    if (dOdO <= 0.0f) return 0;
    float c0x = cx - rc->org[0], c0y = cy - rc->org[1], c0z = cz - rc->org[2];
    float tmin = rc->tmin;

    if (point_type == TRI_POINT_ORIENTED_DISC) {
        float divisor = dx * nx + dy * ny + dz * nz;
        if (divisor == 0.0f) return 0; /* ray parallel to disc plane */
        float t = (c0x * nx + c0y * ny + c0z * nz) / divisor;
        if (t < tmin || t >= best_t) return 0;
        float hx = rc->org[0] + t * dx - cx, hy = rc->org[1] + t * dy - cy,
              hz = rc->org[2] + t * dz - cz;
        if (hx * hx + hy * hy + hz * hz >= r * r) return 0;
        *t_out = t;
        *u_out = 0.0f;
        return 1;
    }

    float rd2 = 1.0f / dOdO;
    float projC0 = (c0x * dx + c0y * dy + c0z * dz) * rd2;
    float perpx = c0x - projC0 * dx, perpy = c0y - projC0 * dy,
          perpz = c0z - projC0 * dz;
    float l2 = perpx * perpx + perpy * perpy + perpz * perpz;
    float r2 = r * r;
    if (l2 > r2) return 0;

    if (point_type == TRI_POINT_DISC) { /* ray-facing disc */
        if (projC0 < tmin || projC0 >= best_t) return 0;
        *t_out = projC0;
        *u_out = 0.0f;
        return 1;
    }

    /* sphere: nearest root in [tmin, best_t) */
    float td = sqrtf((r2 - l2) * rd2);
    float t_front = projC0 - td;
    if (t_front >= tmin && t_front < best_t) {
        *t_out = t_front;
        *u_out = 0.0f;
        return 1;
    }
    float t_back = projC0 + td;
    if (t_back >= tmin && t_back < best_t) {
        *t_out = t_back;
        *u_out = 0.0f;
        return 1;
    }
    return 0;
}

static inline void tri_point4_isect(const lrt_point4 *blk, const tri_ray_ctx *rc,
                                    int point_type, float *best_t, float *best_u,
                                    uint32_t *best_prim) {
    for (int lane = 0; lane < 4; lane++) {
        if (blk->prim[lane] == LRT_TRI_NO_HIT) continue;
        float t, u;
        if (tri_point_isect_one(rc, *best_t, blk->cx[lane], blk->cy[lane],
                                blk->cz[lane], blk->r[lane], blk->nx[lane],
                                blk->ny[lane], blk->nz[lane], point_type, &t,
                                &u)) {
            *best_t = t;
            *best_u = u;
            *best_prim = blk->prim[lane];
        }
    }
}

/* ---- Flat (ribbon) linear curve: ray-facing quad ------------------------- *
 * A segment p0(r0)->p1(r1) drawn as a ribbon whose width direction faces the
 * ray: b = normalize(cross(p1-p0, dir)); quad corners p0+-r0*b, p1+-r1*b. This is
 * the world-space equivalent of embree's ray-space ribbon for linear curves. */
static int tri_flat_isect_one(const tri_ray_ctx *rc, float best_t,
                              const float p0[3], float r0, const float p1[3],
                              float r1, float *t_out, float *u_out) {
    float dx = rc->dir[0], dy = rc->dir[1], dz = rc->dir[2];
    float Tx = p1[0] - p0[0], Ty = p1[1] - p0[1], Tz = p1[2] - p0[2];
    float bx = Ty * dz - Tz * dy, by = Tz * dx - Tx * dz, bz = Tx * dy - Ty * dx;
    float bl2 = bx * bx + by * by + bz * bz;
    if (bl2 <= 1e-20f) return 0; /* ray ~parallel to the tangent: ribbon edge-on */
    float invb = 1.0f / sqrtf(bl2);
    bx *= invb; by *= invb; bz *= invb;
    float q[4][3];
    q[0][0] = p0[0] + r0 * bx; q[0][1] = p0[1] + r0 * by; q[0][2] = p0[2] + r0 * bz;
    q[1][0] = p1[0] + r1 * bx; q[1][1] = p1[1] + r1 * by; q[1][2] = p1[2] + r1 * bz;
    q[2][0] = p1[0] - r1 * bx; q[2][1] = p1[1] - r1 * by; q[2][2] = p1[2] - r1 * bz;
    q[3][0] = p0[0] - r0 * bx; q[3][1] = p0[1] - r0 * by; q[3][2] = p0[2] - r0 * bz;

    float Tl2 = Tx * Tx + Ty * Ty + Tz * Tz;
    float best = best_t, hit_s = 0.0f;
    int hit = 0;
    /* two triangles (q0,q1,q2) and (q0,q2,q3), double-sided Moller-Trumbore */
    for (int tri = 0; tri < 2; tri++) {
        const float *a = q[0];
        const float *bb = tri == 0 ? q[1] : q[2];
        const float *cc = tri == 0 ? q[2] : q[3];
        float e1x = bb[0] - a[0], e1y = bb[1] - a[1], e1z = bb[2] - a[2];
        float e2x = cc[0] - a[0], e2y = cc[1] - a[1], e2z = cc[2] - a[2];
        float px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z,
              pz = dx * e2y - dy * e2x;
        float det = e1x * px + e1y * py + e1z * pz;
        if (tri_isect_det_parallel(e1x, e1y, e1z, e2x, e2y, e2z, det)) continue;
        float inv = 1.0f / det;
        float tvx = rc->org[0] - a[0], tvy = rc->org[1] - a[1],
              tvz = rc->org[2] - a[2];
        float u = (tvx * px + tvy * py + tvz * pz) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        float qx = tvy * e1z - tvz * e1y, qy = tvz * e1x - tvx * e1z,
              qz = tvx * e1y - tvy * e1x;
        float v = (dx * qx + dy * qy + dz * qz) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        float t = (e2x * qx + e2y * qy + e2z * qz) * inv;
        if (t < rc->tmin || t >= best) continue;
        best = t;
        hit = 1;
        float hpx = rc->org[0] + t * dx, hpy = rc->org[1] + t * dy,
              hpz = rc->org[2] + t * dz;
        float s = Tl2 > 0.0f ? ((hpx - p0[0]) * Tx + (hpy - p0[1]) * Ty +
                                (hpz - p0[2]) * Tz) / Tl2
                             : 0.0f;
        hit_s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    }
    if (hit) {
        *t_out = best;
        *u_out = hit_s;
    }
    return hit;
}

static inline void tri_flat4_isect(const lrt_flat4 *blk, const tri_ray_ctx *rc,
                                   float *best_t, float *best_u,
                                   uint32_t *best_prim) {
    for (int lane = 0; lane < 4; lane++) {
        if (blk->prim[lane] == LRT_TRI_NO_HIT) continue;
        float p0[3] = {blk->p0x[lane], blk->p0y[lane], blk->p0z[lane]};
        float p1[3] = {blk->p1x[lane], blk->p1y[lane], blk->p1z[lane]};
        float t, u;
        if (tri_flat_isect_one(rc, *best_t, p0, blk->r0[lane], p1, blk->r1[lane],
                               &t, &u)) {
            *best_t = t;
            *best_u = u;
            *best_prim = blk->prim[lane];
        }
    }
}

/* ---- Round cubic Bezier curve (Embree sweep) ----------------------------- *
 * Subdivide [0,1], seed a Newton (Jacobian) iteration from a bounding capsule
 * per sub-interval, and converge to the exact tube surface. Port of embree's
 * curve_intersector_sweep.h (intersect_bezier_iterative_jacobian). cp holds 4
 * control points, each xyz + radius (16 floats). */
/* Build-time pre-subdivision: split each input cubic into this many tight
 * sub-arcs so the BVH culls well and the runtime sweep stays cheap. */
#define TRI_BEZ_BUILD_SPLIT 2

/* Cubic Bezier: position+radius P[4], 1st deriv dP[4], 2nd deriv ddP[4]. */
static inline void tri_bez_eval(const float cp[16], float u, float P[4],
                                float dP[4], float ddP[4]) {
    float u1 = 1.0f - u;
    for (int c = 0; c < 4; c++) {
        float B0 = cp[c], B1 = cp[4 + c], B2 = cp[8 + c], B3 = cp[12 + c];
        P[c] = u1 * u1 * u1 * B0 + 3.0f * u1 * u1 * u * B1 +
               3.0f * u1 * u * u * B2 + u * u * u * B3;
        float e0 = B1 - B0, e1 = B2 - B1, e2 = B3 - B2;
        dP[c] = 3.0f * (u1 * u1 * e0 + 2.0f * u1 * u * e1 + u * u * e2);
        ddP[c] = 6.0f * (u1 * (B2 - 2.0f * B1 + B0) + u * (B3 - 2.0f * B2 + B1));
    }
}

/* de Casteljau split of a cubic (4 control points, xyz+radius) at parameter t
 * into the two halves L (restriction to [0,t]) and R ([t,1]). */
static void tri_bez_split(const float c[16], float t, float L[16], float R[16]) {
    for (int k = 0; k < 4; k++) {
        float P0 = c[k], P1 = c[4 + k], P2 = c[8 + k], P3 = c[12 + k];
        float ab = P0 + (P1 - P0) * t, bc = P1 + (P2 - P1) * t,
              cd = P2 + (P3 - P2) * t;
        float abc = ab + (bc - ab) * t, bcd = bc + (cd - bc) * t;
        float abcd = abc + (bcd - abc) * t;
        L[k] = P0; L[4 + k] = ab; L[8 + k] = abc; L[12 + k] = abcd;
        R[k] = abcd; R[4 + k] = bcd; R[8 + k] = cd; R[12 + k] = P3;
    }
}

/* Control points of the sub-cubic restricted to [a,b] (0 <= a < b <= 1). */
static void tri_bez_subcurve(const float cp[16], float a, float b,
                             float out[16]) {
    float L[16], R[16];
    tri_bez_split(cp, b, L, R); /* L = restriction to [0,b] */
    if (a > 0.0f) {
        float L2[16];
        tri_bez_split(L, a / b, L2, out); /* out = right part = [a,b] */
    } else {
        memcpy(out, L, 16 * sizeof(float));
    }
}

/* 2D Newton on (u,t): f = dot(R,T) (perpendicular foot), g = dist-to-axis -
 * radius (tube surface). cp is in ray-local space (ray origin at 0), dir = ray
 * direction. Returns 1 and fills t,u on a converged in-range hit. */
static int tri_bez_newton(const float cp[16], const float dir[3], float tmin,
                          float u, float t, float best_t, float *t_out,
                          float *u_out) {
    for (int it = 0; it < 8; it++) {
        float P[4], dP[4], ddP[4];
        tri_bez_eval(cp, u, P, dP, ddP);
        float Rx = t * dir[0] - P[0], Ry = t * dir[1] - P[1],
              Rz = t * dir[2] - P[2];
        float dpd = dP[0] * dP[0] + dP[1] * dP[1] + dP[2] * dP[2];
        if (dpd < 1e-20f) return 0;
        float rcpl = 1.0f / sqrtf(dpd);
        float Tx = dP[0] * rcpl, Ty = dP[1] * rcpl, Tz = dP[2] * rcpl;
        float Tdd = Tx * ddP[0] + Ty * ddP[1] + Tz * ddP[2];
        float dTx = (ddP[0] - Tx * Tdd) * rcpl, dTy = (ddP[1] - Ty * Tdd) * rcpl,
              dTz = (ddP[2] - Tz * Tdd) * rcpl;
        float f = Rx * Tx + Ry * Ty + Rz * Tz;
        float dfdu = (-dP[0] * Tx - dP[1] * Ty - dP[2] * Tz) +
                     (Rx * dTx + Ry * dTy + Rz * dTz);
        float dfdt = dir[0] * Tx + dir[1] * Ty + dir[2] * Tz;
        float RR = Rx * Rx + Ry * Ry + Rz * Rz;
        float K = RR - f * f;
        if (K < 1e-20f) K = 1e-20f;
        float sqrtK = sqrtf(K), rsK = 1.0f / sqrtK;
        float r = P[3] > 0.0f ? P[3] : 0.0f;
        float g = sqrtK - r;
        float RdRdu = -(Rx * dP[0] + Ry * dP[1] + Rz * dP[2]);
        float RdRdt = Rx * dir[0] + Ry * dir[1] + Rz * dir[2];
        float dgdu = (RdRdu - f * dfdu) * rsK - dP[3];
        float dgdt = (RdRdt - f * dfdt) * rsK;
        float det = dfdu * dgdt - dfdt * dgdu;
        if (fabsf(det) < 1e-20f) return 0;
        float invd = 1.0f / det;
        u -= (dgdt * f - dfdt * g) * invd;
        t -= (dfdu * g - dgdu * f) * invd;
        float lenR = sqrtf(RR);
        float eps = 1e-5f * (1.0f + lenR);
        if (fabsf(f) < eps && fabsf(g) < eps) {
            if (t >= tmin && t < best_t && u >= 0.0f && u <= 1.0f) {
                *t_out = t;
                *u_out = u;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

/* Lean tapered-cone seed (cone tangent to spheres p0(r0)->p1(r1), no neighbor
 * CSG and no begin/end conditionals): nearest ray hit in [tmin,best) over the
 * cone (y in [0,g]) and the two end-sphere caps (y<0 at p0, y>g at p1). Ray
 * origin is the local 0; returns t and the axial parameter s in [0,1]. A seed
 * for the Bezier Newton, so it is only approximate. */
static int tri_cone_seed(const float dir[3], float tmin, float best,
                         const float p0[3], float r0, const float p1[3],
                         float r1, float *t_out, float *s_out) {
    float dOdO = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
    if (dOdO <= 0.0f) return 0;
    float rcp_dOdO = 1.0f / dOdO;
    /* re-center the (local) origin near the segment for stability */
    float cx = 0.5f * (p0[0] + p1[0]), cy = 0.5f * (p0[1] + p1[1]),
          cz = 0.5f * (p0[2] + p1[2]);
    float dt = (cx * dir[0] + cy * dir[1] + cz * dir[2]) * rcp_dOdO;
    float ox = dt * dir[0], oy = dt * dir[1], oz = dt * dir[2];
    float dPx = p1[0] - p0[0], dPy = p1[1] - p0[1], dPz = p1[2] - p0[2];
    float dPdP = dPx * dPx + dPy * dPy + dPz * dPz;
    float dr = r1 - r0, r0dr = r0 * dr, g = dPdP - dr * dr;
    float Ox = ox - p0[0], Oy = oy - p0[1], Oz = oz - p0[2];
    float OdP = Ox * dPx + Oy * dPy + Oz * dPz;
    float dOdP = dir[0] * dPx + dir[1] * dPy + dir[2] * dPz;
    float yp = OdP + r0dr;

    float lo = best, slo = 0.0f;
    int hit = 0;
    if (g > 0.0f) {
        float OO = Ox * Ox + Oy * Oy + Oz * Oz;
        float OdO = dir[0] * Ox + dir[1] * Oy + dir[2] * Oz;
        float A = g * dOdO - dOdP * dOdP;
        float B = 2.0f * (g * OdO - dOdP * yp);
        float C = g * OO - OdP * OdP - r0 * r0 * dPdP - 2.0f * r0dr * OdP;
        float D = B * B - 4.0f * A * C;
        if (D >= 0.0f && (A > 1e-18f || A < -1e-18f)) {
            float Q = sqrtf(D), rcp2A = 1.0f / (2.0f * A);
            for (int k = 0; k < 2; k++) {
                float tl = (k == 0 ? (-B - Q) : (-B + Q)) * rcp2A;
                float y = yp + tl * dOdP;
                if (y > -1.2e-7f && y <= g) {
                    float tw = dt + tl;
                    if (tw >= tmin && tw < lo) {
                        lo = tw;
                        float s = y / g;
                        slo = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
                        hit = 1;
                    }
                }
            }
        }
    }
    for (int e = 0; e < 2; e++) { /* end-sphere caps: p0 (y<0), p1 (y>g) */
        const float *c = e ? p1 : p0;
        float rc = e ? r1 : r0;
        float Ex = ox - c[0], Ey = oy - c[1], Ez = oz - c[2];
        float EdO = Ex * dir[0] + Ey * dir[1] + Ez * dir[2];
        float h2 = EdO * EdO - dOdO * (Ex * Ex + Ey * Ey + Ez * Ez - rc * rc);
        if (h2 < 0.0f) continue;
        float tl = (-EdO - sqrtf(h2)) * rcp_dOdO;
        float y = yp + tl * dOdP;
        if (e ? (y > g) : (y < 0.0f)) {
            float tw = dt + tl;
            if (tw >= tmin && tw < lo) {
                lo = tw;
                slo = e ? 1.0f : 0.0f;
                hit = 1;
            }
        }
    }
    if (hit) {
        *t_out = lo;
        *s_out = slo;
    }
    return hit;
}

static int tri_bez_isect_one(const float cp_in[16], const tri_ray_ctx *rc,
                             float best_t, float *t_out, float *u_out) {
    /* translate control points to ray-local space (ray origin at 0) */
    float cp[16];
    for (int k = 0; k < 4; k++) {
        cp[k * 4 + 0] = cp_in[k * 4 + 0] - rc->org[0];
        cp[k * 4 + 1] = cp_in[k * 4 + 1] - rc->org[1];
        cp[k * 4 + 2] = cp_in[k * 4 + 2] - rc->org[2];
        cp[k * 4 + 3] = cp_in[k * 4 + 3];
    }
    tri_ray_ctx rcl = *rc;
    rcl.org[0] = rcl.org[1] = rcl.org[2] = 0.0f;
    const float dir[3] = {rc->dir[0], rc->dir[1], rc->dir[2]};
    const float tmin = rc->tmin;

    float best = best_t, bu = 0.0f;
    int hit = 0;

    /* Adaptive subdivision: recurse [a,b] only where the ray comes within the
     * sub-arc's bounding capsule (chord Pa..Pb + control-polygon bulge + max
     * radius), so far regions of the curve cost only a cheap reject. At max
     * depth, seed a tapered cone over the leaf interval and refine with Newton.
     * Binary recursion -> up to 2^BEZ_MAXDEPTH leaf intervals. */
    enum { BEZ_MAXDEPTH = 5 };
    struct {
        float a, b;
        int depth;
    } stk[2 * BEZ_MAXDEPTH + 2];
    int sp = 0;
    stk[sp].a = 0.0f;
    stk[sp].b = 1.0f;
    stk[sp].depth = 0;
    sp++;
    while (sp > 0) {
        float a = stk[--sp].a, b = stk[sp].b;
        int depth = stk[sp].depth;
        float h = b - a;
        float Pa[4], dPa[4], dda[4], Pb[4], dPb[4], ddb[4];
        tri_bez_eval(cp, a, Pa, dPa, dda);
        tri_bez_eval(cp, b, Pb, dPb, ddb);
        /* sub-arc bound (de Casteljau inner CPs P1=Pa+dPa*h/3, P2=Pb-dPb*h/3) */
        float cxd = Pb[0] - Pa[0], cyd = Pb[1] - Pa[1], czd = Pb[2] - Pa[2];
        float cl2 = cxd * cxd + cyd * cyd + czd * czd;
        float maxperp = 0.0f;
        if (cl2 > 1e-20f) {
            float inv = 1.0f / cl2;
            float mid[2][3] = {
                {Pa[0] + dPa[0] * h / 3.0f, Pa[1] + dPa[1] * h / 3.0f,
                 Pa[2] + dPa[2] * h / 3.0f},
                {Pb[0] - dPb[0] * h / 3.0f, Pb[1] - dPb[1] * h / 3.0f,
                 Pb[2] - dPb[2] * h / 3.0f}};
            for (int m = 0; m < 2; m++) {
                float wx = mid[m][0] - Pa[0], wy = mid[m][1] - Pa[1],
                      wz = mid[m][2] - Pa[2];
                float tt = (wx * cxd + wy * cyd + wz * czd) * inv;
                float dx = wx - tt * cxd, dy = wy - tt * cyd, dz = wz - tt * czd;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > maxperp) maxperp = d2;
            }
            maxperp = sqrtf(maxperp);
        }
        float r1w = Pa[3] + dPa[3] * h / 3.0f, r2w = Pb[3] - dPb[3] * h / 3.0f;
        float r0c = Pa[3] > r1w ? Pa[3] : r1w;
        float r3c = Pb[3] > r2w ? Pb[3] : r2w;
        r0c = (r0c > 0.0f ? r0c : 0.0f) + maxperp;
        r3c = (r3c > 0.0f ? r3c : 0.0f) + maxperp;

        /* cheap conservative reject: ray vs the bounding capsule (chord, rcap),
         * via the closest ray-to-segment distance (robust for any orientation). */
        float rcap = r0c > r3c ? r0c : r3c;
        {
            float a_ = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
            float bb = dir[0] * cxd + dir[1] * cyd + dir[2] * czd;
            float cc = cl2;
            float w0x = -Pa[0], w0y = -Pa[1], w0z = -Pa[2]; /* org=0 */
            float dd = dir[0] * w0x + dir[1] * w0y + dir[2] * w0z;
            float ed = cxd * w0x + cyd * w0y + czd * w0z;
            float den = a_ * cc - bb * bb;
            float ss = den > 1e-20f ? (a_ * ed - bb * dd) / den : 0.0f;
            ss = ss < 0.0f ? 0.0f : (ss > 1.0f ? 1.0f : ss);
            float tr = (bb * ss - dd) / (a_ > 1e-20f ? a_ : 1e-20f);
            tr = tr < tmin ? tmin : (tr > best ? best : tr);
            ss = (bb * tr + ed) / (cc > 1e-20f ? cc : 1e-20f);
            ss = ss < 0.0f ? 0.0f : (ss > 1.0f ? 1.0f : ss);
            float gx = tr * dir[0] - (Pa[0] + ss * cxd);
            float gy = tr * dir[1] - (Pa[1] + ss * cyd);
            float gz = tr * dir[2] - (Pa[2] + ss * czd);
            if (gx * gx + gy * gy + gz * gz > rcap * rcap) continue;
        }

        if (depth >= BEZ_MAXDEPTH) {
            /* leaf: tapered-cone seed + Newton over [a,b] */
            if (r0c > 0.0f || r3c > 0.0f) {
                float P0p[3] = {Pa[0], Pa[1], Pa[2]},
                      P3p[3] = {Pb[0], Pb[1], Pb[2]};
                float tseg, s;
                if (tri_cone_seed(dir, tmin, best, P0p, r0c, P3p, r3c, &tseg,
                                  &s)) {
                    float th, uh;
                    if (tri_bez_newton(cp, dir, tmin, a + s * h, tseg, best, &th,
                                       &uh)) {
                        best = th;
                        bu = uh;
                        hit = 1;
                    } else {
                        best = tseg;
                        bu = a + s * h;
                        hit = 1;
                    }
                }
            }
        } else {
            float m = 0.5f * (a + b);
            stk[sp].a = a;
            stk[sp].b = m;
            stk[sp].depth = depth + 1;
            sp++;
            stk[sp].a = m;
            stk[sp].b = b;
            stk[sp].depth = depth + 1;
            sp++;
        }
    }
    if (hit) {
        *t_out = best;
        *u_out = bu;
    }
    return hit;
}

static inline void tri_bez4_isect(const lrt_bez4 *blk, const tri_ray_ctx *rc,
                                  float *best_t, float *best_u,
                                  uint32_t *best_prim) {
    for (int lane = 0; lane < 4; lane++) {
        if (blk->prim[lane] == LRT_TRI_NO_HIT) continue;
        float cp[16] = {
            blk->b0x[lane], blk->b0y[lane], blk->b0z[lane], blk->b0r[lane],
            blk->b1x[lane], blk->b1y[lane], blk->b1z[lane], blk->b1r[lane],
            blk->b2x[lane], blk->b2y[lane], blk->b2z[lane], blk->b2r[lane],
            blk->b3x[lane], blk->b3y[lane], blk->b3z[lane], blk->b3r[lane]};
        float t, u;
        if (tri_bez_isect_one(cp, rc, *best_t, &t, &u)) {
            *best_t = t;
            *best_u = u;
            *best_prim = blk->prim[lane];
        }
    }
}

#define TRI_INV_2PI 0.15915494309189535f
#define TRI_INV_PI 0.31830988618379067f

/* Ray vs one sphere lane of a generic-width sphere block (f = 4 float arrays
 * cx,cy,cz,r of `bw` lanes). dir is NOT normalized, so t is in units of |dir|
 * like the rest of the library. Returns 1 on a hit in [tmin, t_best); writes t
 * and (u,v) = spherical coords (longitude, latitude) of the surface normal. */
static inline int tri_sph_isect_lane(const float *f, int bw, int lane,
                                     const tri_ray_ctx *rc, float t_best,
                                     float *t, float *u, float *v) {
    float cx = f[0 * bw + lane], cy = f[1 * bw + lane], cz = f[2 * bw + lane];
    float r = f[3 * bw + lane];
    if (r <= 0.0f) return 0;
    float ocx = rc->org[0] - cx, ocy = rc->org[1] - cy, ocz = rc->org[2] - cz;
    float a = rc->dir[0] * rc->dir[0] + rc->dir[1] * rc->dir[1] +
              rc->dir[2] * rc->dir[2];
    if (a <= 1e-20f) return 0;
    float b = ocx * rc->dir[0] + ocy * rc->dir[1] + ocz * rc->dir[2];
    float c = ocx * ocx + ocy * ocy + ocz * ocz - r * r;
    float disc = b * b - a * c;
    if (disc < 0.0f) return 0;
    float sq = sqrtf(disc);
    float inv_a = 1.0f / a;
    float tt = (-b - sq) * inv_a;
    if (tt < rc->tmin || tt >= t_best) {
        tt = (-b + sq) * inv_a; /* near root behind tmin: try the far root */
        if (tt < rc->tmin || tt >= t_best) return 0;
    }
    float nx = (ocx + tt * rc->dir[0]) / r;
    float ny = (ocy + tt * rc->dir[1]) / r;
    float nz = (ocz + tt * rc->dir[2]) / r;
    float cl = ny < -1.0f ? -1.0f : (ny > 1.0f ? 1.0f : ny);
    *t = tt;
    *u = atan2f(nz, nx) * TRI_INV_2PI + 0.5f;
    *v = acosf(cl) * TRI_INV_PI;
    return 1;
}

/* Ray vs one sphere lane, any-hit only: returns 1 if hit in [tmin, tmax]. */
static inline int tri_sph_occluded_lane(const float *f, int bw, int lane,
                                        const tri_ray_ctx *rc, float tmax) {
    float cx = f[0 * bw + lane], cy = f[1 * bw + lane], cz = f[2 * bw + lane];
    float r = f[3 * bw + lane];
    if (r <= 0.0f) return 0;
    float ocx = rc->org[0] - cx, ocy = rc->org[1] - cy, ocz = rc->org[2] - cz;
    float a = rc->dir[0] * rc->dir[0] + rc->dir[1] * rc->dir[1] +
              rc->dir[2] * rc->dir[2];
    if (a <= 1e-20f) return 0;
    float b = ocx * rc->dir[0] + ocy * rc->dir[1] + ocz * rc->dir[2];
    float c = ocx * ocx + ocy * ocy + ocz * ocz - r * r;
    float disc = b * b - a * c;
    if (disc < 0.0f) return 0;
    float sq = sqrtf(disc);
    float inv_a = 1.0f / a;
    float t0 = (-b - sq) * inv_a;
    if (t0 >= rc->tmin && t0 <= tmax) return 1;
    float t1 = (-b + sq) * inv_a;
    return t1 >= rc->tmin && t1 <= tmax;
}

/* Decode a quantized node's child bounds to floats (scalar fallback path). */
static inline void tri_bvh8q_decode(const lrt_bvh8q_node *n, float *lo_x,
                                    float *lo_y, float *lo_z, float *hi_x,
                                    float *hi_y, float *hi_z) {
    for (int i = 0; i < 8; i++) {
        lo_x[i] = n->org[0] + (float)n->qlo_x[i] * n->scale[0];
        lo_y[i] = n->org[1] + (float)n->qlo_y[i] * n->scale[1];
        lo_z[i] = n->org[2] + (float)n->qlo_z[i] * n->scale[2];
        hi_x[i] = n->org[0] + (float)n->qhi_x[i] * n->scale[0];
        hi_y[i] = n->org[1] + (float)n->qhi_y[i] * n->scale[1];
        hi_z[i] = n->org[2] + (float)n->qhi_z[i] * n->scale[2];
    }
}

/* Scalar slab test for slot `i` of SoA bounds arrays. */
static inline int tri_slab_scalar(const float *lo_x, const float *lo_y,
                                  const float *lo_z, const float *hi_x,
                                  const float *hi_y, const float *hi_z, int i,
                                  const tri_ray_ctx *rc, float t_best,
                                  float *tnear_out) {
    float tlx = (lo_x[i] - rc->org[0]) * rc->invd[0];
    float thx = (hi_x[i] - rc->org[0]) * rc->invd[0];
    float tly = (lo_y[i] - rc->org[1]) * rc->invd[1];
    float thy = (hi_y[i] - rc->org[1]) * rc->invd[1];
    float tlz = (lo_z[i] - rc->org[2]) * rc->invd[2];
    float thz = (hi_z[i] - rc->org[2]) * rc->invd[2];
    float tnx = tri_minf(tlx, thx), tfx = tri_maxf(tlx, thx);
    float tny = tri_minf(tly, thy), tfy = tri_maxf(tly, thy);
    float tnz = tri_minf(tlz, thz), tfz = tri_maxf(tlz, thz);
    float tnear = tri_maxf(tri_maxf(tnx, tny), tri_maxf(tnz, rc->tmin));
    float tfar = tri_minf(tri_minf(tfx, tfy), tri_minf(tfz, t_best));
    *tnear_out = tnear;
    return tnear <= tfar;
}

/* Generic-width scalar traversal. */
static int tri_intersect_scalar(const lrt_tri_scene *s, const lrt_ray *ray,
                                lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    float best_t = ray->tmax;
    float best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;
    const int width = s->layout;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;

    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            if (s->curve) {
                const lrt_crv4 *blocks = (const lrt_crv4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    tri_crv4_isect(&blocks[blk0 + b], &rc, &best_t, &best_u,
                                   &best_prim);
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_RLCURVE) {
                const lrt_rlc4 *blocks = (const lrt_rlc4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    tri_rlc4_isect(&blocks[blk0 + b], &rc, &best_t, &best_u,
                                   &best_prim);
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_POINT) {
                const lrt_point4 *blocks = (const lrt_point4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    tri_point4_isect(&blocks[blk0 + b], &rc, s->point_type,
                                     &best_t, &best_u, &best_prim);
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_FLATCURVE) {
                const lrt_flat4 *blocks = (const lrt_flat4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    tri_flat4_isect(&blocks[blk0 + b], &rc, &best_t, &best_u,
                                    &best_prim);
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_BEZCURVE) {
                const lrt_bez4 *blocks = (const lrt_bez4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    tri_bez4_isect(&blocks[blk0 + b], &rc, &best_t, &best_u,
                                   &best_prim);
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_SPHERE) {
                for (uint32_t b = 0; b < nblk; b++) {
                    const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                    const uint32_t *ids = (const uint32_t *)(f + 4 * width);
                    for (int lane = 0; lane < width; lane++) {
                        if (ids[lane] == LRT_TRI_NO_HIT) continue;
                        float t, u, v;
                        if (tri_sph_isect_lane(f, width, lane, &rc, best_t, &t,
                                               &u, &v)) {
                            best_t = t;
                            best_u = u;
                            best_v = v;
                            best_prim = ids[lane];
                        }
                    }
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_USER) {
                const lrt_user4 *blocks = (const lrt_user4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    const lrt_user4 *blk = &blocks[blk0 + b];
                    for (int lane = 0; lane < 4; lane++) {
                        if (blk->prim[lane] == LRT_TRI_NO_HIT) continue;
                        float tn;
                        if (!tri_slab_scalar(blk->lo_x, blk->lo_y, blk->lo_z,
                                             blk->hi_x, blk->hi_y, blk->hi_z,
                                             lane, &rc, best_t, &tn))
                            continue;
                        lrt_ray q = {{rc.org[0], rc.org[1], rc.org[2]},
                                     ray->tmin,
                                     {rc.dir[0], rc.dir[1], rc.dir[2]},
                                     best_t};
                        float t, u, v;
                        if (s->user_isect(&q, blk->prim[lane], s->user_ptr, &t,
                                          &u, &v) &&
                            t >= ray->tmin && t < best_t) {
                            best_t = t;
                            best_u = u;
                            best_v = v;
                            best_prim = blk->prim[lane];
                        }
                    }
                }
                continue;
            }
            for (uint32_t b = 0; b < nblk; b++) {
                const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                const uint32_t *ids = (const uint32_t *)(f + 9 * width);
                for (int lane = 0; lane < width; lane++) {
                    if (ids[lane] == LRT_TRI_NO_HIT) continue;
                    float t, u, v;
                    if (tri_isect_lane(f, width, lane, &rc, best_t, &t, &u, &v)) {
                        best_t = t;
                        best_u = u;
                        best_v = v;
                        best_prim = ids[lane];
                    }
                }
            }
            continue;
        }

        const float *lo_x, *lo_y, *lo_z, *hi_x, *hi_y, *hi_z;
        const uint32_t *child;
        int nchildren;
        float dec[48]; /* decode buffer for quantized nodes */
        if (width == 4) {
            const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
            lo_x = n->lo_x; lo_y = n->lo_y; lo_z = n->lo_z;
            hi_x = n->hi_x; hi_y = n->hi_y; hi_z = n->hi_z;
            child = n->child;
            nchildren = (int)n->nchildren;
        } else if (s->quantized) {
            const lrt_bvh8q_node *n = &s->nodes8q[TRI_REF_NODE(e.ref)];
            tri_bvh8q_decode(n, dec, dec + 8, dec + 16, dec + 24, dec + 32,
                             dec + 40);
            lo_x = dec; lo_y = dec + 8; lo_z = dec + 16;
            hi_x = dec + 24; hi_y = dec + 32; hi_z = dec + 40;
            child = n->child;
            nchildren = (int)n->nchildren;
        } else {
            const lrt_bvh8_node *n = &s->nodes8[TRI_REF_NODE(e.ref)];
            lo_x = n->lo_x; lo_y = n->lo_y; lo_z = n->lo_z;
            hi_x = n->hi_x; hi_y = n->hi_y; hi_z = n->hi_z;
            child = n->child;
            nchildren = (int)n->nchildren;
        }

        /* Test children, then push hits sorted far-to-near so the nearest is
         * popped first. */
        uint32_t hit_ref[8];
        float hit_tnear[8];
        int nhit = 0;
        for (int i = 0; i < nchildren; i++) {
            float tnear;
            if (tri_slab_scalar(lo_x, lo_y, lo_z, hi_x, hi_y, hi_z, i, &rc,
                                best_t, &tnear)) {
                /* insertion: keep ascending tnear */
                int j = nhit++;
                while (j > 0 && hit_tnear[j - 1] > tnear) {
                    hit_tnear[j] = hit_tnear[j - 1];
                    hit_ref[j] = hit_ref[j - 1];
                    j--;
                }
                hit_tnear[j] = tnear;
                hit_ref[j] = child[i];
            }
        }
        if (sp + nhit > TRI_STACK_SIZE) return 0; /* cannot happen: depth-capped */
        for (int i = nhit - 1; i >= 0; i--) {
            stack[sp].ref = hit_ref[i];
            stack[sp].tnear = hit_tnear[i];
            sp++;
        }
    }

    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_occluded_scalar(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    const float t_max = ray->tmax;
    const int width = s->layout;

    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;

    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref);
            uint32_t nblk = TRI_REF_NBLOCKS(ref);
            if (s->curve) {
                const lrt_crv4 *blocks = (const lrt_crv4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    float t = t_max, u = 0.0f;
                    uint32_t prim = LRT_TRI_NO_HIT;
                    tri_crv4_isect(&blocks[blk0 + b], &rc, &t, &u, &prim);
                    if (prim != LRT_TRI_NO_HIT) return 1;
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_RLCURVE) {
                const lrt_rlc4 *blocks = (const lrt_rlc4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    float t = t_max, u = 0.0f;
                    uint32_t prim = LRT_TRI_NO_HIT;
                    tri_rlc4_isect(&blocks[blk0 + b], &rc, &t, &u, &prim);
                    if (prim != LRT_TRI_NO_HIT) return 1;
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_POINT) {
                const lrt_point4 *blocks = (const lrt_point4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    float t = t_max, u = 0.0f;
                    uint32_t prim = LRT_TRI_NO_HIT;
                    tri_point4_isect(&blocks[blk0 + b], &rc, s->point_type, &t,
                                     &u, &prim);
                    if (prim != LRT_TRI_NO_HIT) return 1;
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_FLATCURVE) {
                const lrt_flat4 *blocks = (const lrt_flat4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    float t = t_max, u = 0.0f;
                    uint32_t prim = LRT_TRI_NO_HIT;
                    tri_flat4_isect(&blocks[blk0 + b], &rc, &t, &u, &prim);
                    if (prim != LRT_TRI_NO_HIT) return 1;
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_BEZCURVE) {
                const lrt_bez4 *blocks = (const lrt_bez4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    float t = t_max, u = 0.0f;
                    uint32_t prim = LRT_TRI_NO_HIT;
                    tri_bez4_isect(&blocks[blk0 + b], &rc, &t, &u, &prim);
                    if (prim != LRT_TRI_NO_HIT) return 1;
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_SPHERE) {
                for (uint32_t b = 0; b < nblk; b++) {
                    const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                    const uint32_t *ids = (const uint32_t *)(f + 4 * width);
                    for (int lane = 0; lane < width; lane++) {
                        if (ids[lane] == LRT_TRI_NO_HIT) continue;
                        if (tri_sph_occluded_lane(f, width, lane, &rc, t_max))
                            return 1;
                    }
                }
                continue;
            }
            if (s->prim_kind == TRI_PRIM_USER) {
                const lrt_user4 *blocks = (const lrt_user4 *)s->blocks;
                for (uint32_t b = 0; b < nblk; b++) {
                    const lrt_user4 *blk = &blocks[blk0 + b];
                    for (int lane = 0; lane < 4; lane++) {
                        if (blk->prim[lane] == LRT_TRI_NO_HIT) continue;
                        float tn;
                        if (!tri_slab_scalar(blk->lo_x, blk->lo_y, blk->lo_z,
                                             blk->hi_x, blk->hi_y, blk->hi_z,
                                             lane, &rc, t_max, &tn))
                            continue;
                        lrt_ray q = {{rc.org[0], rc.org[1], rc.org[2]},
                                     ray->tmin,
                                     {rc.dir[0], rc.dir[1], rc.dir[2]},
                                     t_max};
                        if (s->user_occ) {
                            if (s->user_occ(&q, blk->prim[lane], s->user_ptr))
                                return 1;
                        } else {
                            float t, u, v;
                            if (s->user_isect(&q, blk->prim[lane], s->user_ptr,
                                              &t, &u, &v) &&
                                t >= ray->tmin && t <= t_max)
                                return 1;
                        }
                    }
                }
                continue;
            }
            for (uint32_t b = 0; b < nblk; b++) {
                const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                const uint32_t *ids = (const uint32_t *)(f + 9 * width);
                for (int lane = 0; lane < width; lane++) {
                    if (ids[lane] == LRT_TRI_NO_HIT) continue;
                    float t, u, v;
                    if (tri_isect_lane(f, width, lane, &rc, t_max, &t, &u, &v)) {
                        return 1;
                    }
                }
            }
            continue;
        }

        const float *lo_x, *lo_y, *lo_z, *hi_x, *hi_y, *hi_z;
        const uint32_t *child;
        int nchildren;
        float dec[48];
        if (width == 4) {
            const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
            lo_x = n->lo_x; lo_y = n->lo_y; lo_z = n->lo_z;
            hi_x = n->hi_x; hi_y = n->hi_y; hi_z = n->hi_z;
            child = n->child;
            nchildren = (int)n->nchildren;
        } else if (s->quantized) {
            const lrt_bvh8q_node *n = &s->nodes8q[TRI_REF_NODE(ref)];
            tri_bvh8q_decode(n, dec, dec + 8, dec + 16, dec + 24, dec + 32,
                             dec + 40);
            lo_x = dec; lo_y = dec + 8; lo_z = dec + 16;
            hi_x = dec + 24; hi_y = dec + 32; hi_z = dec + 40;
            child = n->child;
            nchildren = (int)n->nchildren;
        } else {
            const lrt_bvh8_node *n = &s->nodes8[TRI_REF_NODE(ref)];
            lo_x = n->lo_x; lo_y = n->lo_y; lo_z = n->lo_z;
            hi_x = n->hi_x; hi_y = n->hi_y; hi_z = n->hi_z;
            child = n->child;
            nchildren = (int)n->nchildren;
        }
        if (sp + nchildren > TRI_STACK_SIZE) return 0;
        for (int i = 0; i < nchildren; i++) {
            float tnear;
            if (tri_slab_scalar(lo_x, lo_y, lo_z, hi_x, hi_y, hi_z, i, &rc,
                                t_max, &tnear)) {
                stack[sp++] = child[i];
            }
        }
    }
    return 0;
}

/* ---- Quantized-triangle leaves: scalar decode + Moller-Trumbore --------- */

static inline uint32_t tri_qtri_prim(const lrt_tri_scene *s, const void *blk,
                                     int lane) {
    if (s->qfmt == LRT_QTRI_Q16) return ((const lrt_qtri16 *)blk)->prim_id[lane];
    if (s->qfmt == LRT_QTRI_FP4) return ((const lrt_qtri4 *)blk)->prim_id[lane];
    return ((const lrt_qtri8 *)blk)->prim_id[lane];
}

/* Moller-Trumbore on explicit v0,e1,e2 (same epsilon/semantics as
 * tri_isect_lane). */
static inline int tri_mt_scalar(const float v0[3], const float e1[3],
                                const float e2[3], const tri_ray_ctx *rc,
                                float t_best, float *t, float *u, float *v) {
    float px = rc->dir[1] * e2[2] - rc->dir[2] * e2[1];
    float py = rc->dir[2] * e2[0] - rc->dir[0] * e2[2];
    float pz = rc->dir[0] * e2[1] - rc->dir[1] * e2[0];
    float det = e1[0] * px + e1[1] * py + e1[2] * pz;
    if (tri_isect_det_parallel(e1[0], e1[1], e1[2], e2[0], e2[1], e2[2], det)) return 0;
    float inv = 1.0f / det;
    float tvx = rc->org[0] - v0[0], tvy = rc->org[1] - v0[1],
          tvz = rc->org[2] - v0[2];
    float uu = (tvx * px + tvy * py + tvz * pz) * inv;
    if (uu < 0.0f || uu > 1.0f) return 0;
    float qx = tvy * e1[2] - tvz * e1[1];
    float qy = tvz * e1[0] - tvx * e1[2];
    float qz = tvx * e1[1] - tvy * e1[0];
    float vv = (rc->dir[0] * qx + rc->dir[1] * qy + rc->dir[2] * qz) * inv;
    if (vv < 0.0f || uu + vv > 1.0f) return 0;
    float tt = (e2[0] * qx + e2[1] * qy + e2[2] * qz) * inv;
    if (tt < rc->tmin || tt >= t_best) return 0;
    *t = tt;
    *u = uu;
    *v = vv;
    return 1;
}

static inline void tri_qtri_block_isect(const lrt_tri_scene *s, const void *blk,
                                        const tri_ray_ctx *rc, float *best_t,
                                        float *best_u, float *best_v,
                                        uint32_t *best_prim) {
    for (int lane = 0; lane < 4; lane++) {
        uint32_t prim = tri_qtri_prim(s, blk, lane);
        if (prim == LRT_TRI_NO_HIT) continue;
        float v0[3], v1[3], v2[3];
        tri_qtri_decode_lane(s, blk, lane, v0, v1, v2);
        float e1[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
        float e2[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};
        float t, u, v;
        if (tri_mt_scalar(v0, e1, e2, rc, *best_t, &t, &u, &v)) {
            *best_t = t;
            *best_u = u;
            *best_v = v;
            *best_prim = prim;
        }
    }
}

static inline int tri_qtri_block_occluded(const lrt_tri_scene *s,
                                          const void *blk, const tri_ray_ctx *rc,
                                          float t_max) {
    for (int lane = 0; lane < 4; lane++) {
        uint32_t prim = tri_qtri_prim(s, blk, lane);
        if (prim == LRT_TRI_NO_HIT) continue;
        float v0[3], v1[3], v2[3];
        tri_qtri_decode_lane(s, blk, lane, v0, v1, v2);
        float e1[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
        float e2[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};
        float t, u, v;
        if (tri_mt_scalar(v0, e1, e2, rc, t_max, &t, &u, &v)) return 1;
    }
    return 0;
}

#if !LRT_TRI_HAS_SSE4
/* Scalar BVH4 traversal over quantized leaves (non-SSE fallback). */
static int tri_qtri_intersect_scalar(const lrt_tri_scene *s, const lrt_ray *ray,
                                     lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    float best_t = ray->tmax, best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;
    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref), nblk = TRI_REF_NBLOCKS(e.ref);
            for (uint32_t b = 0; b < nblk; b++)
                tri_qtri_block_isect(
                    s, tri_block_ptr(s->blocks, blk0 + b, s->block_stride), &rc,
                    &best_t, &best_u, &best_v, &best_prim);
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        int nch = (int)n->nchildren;
        uint32_t hit_ref[4];
        float hit_tn[4];
        int nhit = 0;
        for (int i = 0; i < nch; i++) {
            float tnear;
            if (tri_slab_scalar(n->lo_x, n->lo_y, n->lo_z, n->hi_x, n->hi_y,
                                n->hi_z, i, &rc, best_t, &tnear)) {
                int j = nhit++;
                while (j > 0 && hit_tn[j - 1] > tnear) {
                    hit_tn[j] = hit_tn[j - 1];
                    hit_ref[j] = hit_ref[j - 1];
                    j--;
                }
                hit_tn[j] = tnear;
                hit_ref[j] = n->child[i];
            }
        }
        for (int i = nhit - 1; i >= 0; i--) {
            stack[sp].ref = hit_ref[i];
            stack[sp].tnear = hit_tn[i];
            sp++;
        }
    }
    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_qtri_occluded_scalar(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    const float t_max = ray->tmax;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            for (uint32_t b = 0; b < nblk; b++)
                if (tri_qtri_block_occluded(
                        s, tri_block_ptr(s->blocks, blk0 + b, s->block_stride),
                        &rc, t_max))
                    return 1;
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        int nch = (int)n->nchildren;
        for (int i = 0; i < nch; i++) {
            float tnear;
            if (tri_slab_scalar(n->lo_x, n->lo_y, n->lo_z, n->hi_x, n->hi_y,
                                n->hi_z, i, &rc, t_max, &tnear)) {
                if (sp < TRI_STACK_SIZE) stack[sp++] = n->child[i];
            }
        }
    }
    return 0;
}
#endif /* !LRT_TRI_HAS_SSE4 */

/* ------------------------------------------------------------------------- */
/* SSE4 kernels (BVH4).                                                      */
/* ------------------------------------------------------------------------- */
#if LRT_TRI_HAS_SSE4

typedef struct tri_sse_ctx {
    __m128 orgx, orgy, orgz;
    __m128 invdx, invdy, invdz;
    __m128 oinvx, oinvy, oinvz; /* org * invd (clamped), for FMA slab */
    __m128 dirx, diry, dirz;
    __m128 tmin;
} tri_sse_ctx;

static inline void tri_sse_setup(const tri_ray_ctx *rc, tri_sse_ctx *sc) {
    sc->orgx = _mm_set1_ps(rc->org[0]);
    sc->orgy = _mm_set1_ps(rc->org[1]);
    sc->orgz = _mm_set1_ps(rc->org[2]);
    sc->invdx = _mm_set1_ps(rc->invd[0]);
    sc->invdy = _mm_set1_ps(rc->invd[1]);
    sc->invdz = _mm_set1_ps(rc->invd[2]);
    sc->oinvx = _mm_set1_ps(rc->oinv[0]);
    sc->oinvy = _mm_set1_ps(rc->oinv[1]);
    sc->oinvz = _mm_set1_ps(rc->oinv[2]);
    sc->dirx = _mm_set1_ps(rc->dir[0]);
    sc->diry = _mm_set1_ps(rc->dir[1]);
    sc->dirz = _mm_set1_ps(rc->dir[2]);
    sc->tmin = _mm_set1_ps(rc->tmin);
}

/* One ray vs the 4 child boxes of a BVH4 node. Returns hit mask; writes the
 * per-slot entry distances to tnear_out (aligned 16). */
static inline int tri_bvh4_slab_sse(const lrt_bvh4_node *n,
                                    const tri_sse_ctx *sc, __m128 t_best,
                                    float *tnear_out) {
#if LRT_TRI_HAS_AVX2 /* implies FMA: plane = bound*invd - org*invd, one op */
    __m128 tlx = _mm_fmsub_ps(_mm_load_ps(n->lo_x), sc->invdx, sc->oinvx);
    __m128 thx = _mm_fmsub_ps(_mm_load_ps(n->hi_x), sc->invdx, sc->oinvx);
    __m128 tly = _mm_fmsub_ps(_mm_load_ps(n->lo_y), sc->invdy, sc->oinvy);
    __m128 thy = _mm_fmsub_ps(_mm_load_ps(n->hi_y), sc->invdy, sc->oinvy);
    __m128 tlz = _mm_fmsub_ps(_mm_load_ps(n->lo_z), sc->invdz, sc->oinvz);
    __m128 thz = _mm_fmsub_ps(_mm_load_ps(n->hi_z), sc->invdz, sc->oinvz);
#else
    __m128 tlx = _mm_mul_ps(_mm_sub_ps(_mm_load_ps(n->lo_x), sc->orgx), sc->invdx);
    __m128 thx = _mm_mul_ps(_mm_sub_ps(_mm_load_ps(n->hi_x), sc->orgx), sc->invdx);
    __m128 tly = _mm_mul_ps(_mm_sub_ps(_mm_load_ps(n->lo_y), sc->orgy), sc->invdy);
    __m128 thy = _mm_mul_ps(_mm_sub_ps(_mm_load_ps(n->hi_y), sc->orgy), sc->invdy);
    __m128 tlz = _mm_mul_ps(_mm_sub_ps(_mm_load_ps(n->lo_z), sc->orgz), sc->invdz);
    __m128 thz = _mm_mul_ps(_mm_sub_ps(_mm_load_ps(n->hi_z), sc->orgz), sc->invdz);
#endif
    __m128 tnear = _mm_max_ps(
        _mm_max_ps(_mm_min_ps(tlx, thx), _mm_min_ps(tly, thy)),
        _mm_max_ps(_mm_min_ps(tlz, thz), sc->tmin));
    __m128 tfar = _mm_min_ps(
        _mm_min_ps(_mm_max_ps(tlx, thx), _mm_max_ps(tly, thy)),
        _mm_min_ps(_mm_max_ps(tlz, thz), t_best));
    _mm_store_ps(tnear_out, tnear);
    return _mm_movemask_ps(_mm_cmple_ps(tnear, tfar));
}

/* 4-wide SoA Moller-Trumbore over one block. Updates best hit in place. */
static inline void tri_block_isect_sse(const lrt_tri4 *blk,
                                       const tri_sse_ctx *sc, float *best_t,
                                       float *best_u, float *best_v,
                                       uint32_t *best_prim) {
    __m128 e1x = _mm_load_ps(blk->e1x), e1y = _mm_load_ps(blk->e1y),
           e1z = _mm_load_ps(blk->e1z);
    __m128 e2x = _mm_load_ps(blk->e2x), e2y = _mm_load_ps(blk->e2y),
           e2z = _mm_load_ps(blk->e2z);

    /* pvec = dir x e2 */
    __m128 px = _mm_sub_ps(_mm_mul_ps(sc->diry, e2z), _mm_mul_ps(sc->dirz, e2y));
    __m128 py = _mm_sub_ps(_mm_mul_ps(sc->dirz, e2x), _mm_mul_ps(sc->dirx, e2z));
    __m128 pz = _mm_sub_ps(_mm_mul_ps(sc->dirx, e2y), _mm_mul_ps(sc->diry, e2x));

    __m128 det = _mm_add_ps(_mm_add_ps(_mm_mul_ps(e1x, px), _mm_mul_ps(e1y, py)),
                            _mm_mul_ps(e1z, pz));
    __m128 valid = tri_isect_det_parallel4(e1x, e1y, e1z, e2x, e2y, e2z, det);
    if (!_mm_movemask_ps(valid)) return;

    __m128 inv_det = _mm_div_ps(_mm_set1_ps(1.0f), det);

    __m128 tvx = _mm_sub_ps(sc->orgx, _mm_load_ps(blk->v0x));
    __m128 tvy = _mm_sub_ps(sc->orgy, _mm_load_ps(blk->v0y));
    __m128 tvz = _mm_sub_ps(sc->orgz, _mm_load_ps(blk->v0z));

    __m128 u = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(tvx, px), _mm_mul_ps(tvy, py)),
                   _mm_mul_ps(tvz, pz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_cmpge_ps(u, _mm_setzero_ps()));
    valid = _mm_and_ps(valid, _mm_cmple_ps(u, _mm_set1_ps(1.0f)));
    if (!_mm_movemask_ps(valid)) return;

    /* qvec = tvec x e1 */
    __m128 qx = _mm_sub_ps(_mm_mul_ps(tvy, e1z), _mm_mul_ps(tvz, e1y));
    __m128 qy = _mm_sub_ps(_mm_mul_ps(tvz, e1x), _mm_mul_ps(tvx, e1z));
    __m128 qz = _mm_sub_ps(_mm_mul_ps(tvx, e1y), _mm_mul_ps(tvy, e1x));

    __m128 v = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(sc->dirx, qx), _mm_mul_ps(sc->diry, qy)),
                   _mm_mul_ps(sc->dirz, qz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_cmpge_ps(v, _mm_setzero_ps()));
    valid = _mm_and_ps(valid, _mm_cmple_ps(_mm_add_ps(u, v), _mm_set1_ps(1.0f)));
    if (!_mm_movemask_ps(valid)) return;

    __m128 t = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(e2x, qx), _mm_mul_ps(e2y, qy)),
                   _mm_mul_ps(e2z, qz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_cmpge_ps(t, sc->tmin));
    valid = _mm_and_ps(valid, _mm_cmplt_ps(t, _mm_set1_ps(*best_t)));
    int mask = _mm_movemask_ps(valid);
    if (!mask) return;

    float ta[4], ua[4], va[4];
    _mm_storeu_ps(ta, t);
    _mm_storeu_ps(ua, u);
    _mm_storeu_ps(va, v);
    while (mask) {
        int lane = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        if (ta[lane] < *best_t) {
            *best_t = ta[lane];
            *best_u = ua[lane];
            *best_v = va[lane];
            *best_prim = blk->prim_id[lane];
        }
    }
}

/* 4-wide Moller-Trumbore on decoded SoA vertices (for quantized leaves).
 * prim[] supplies the per-lane primitive ids. */
static inline __m128 tri_fma4(__m128 a, __m128 b, __m128 c) {
#if LRT_TRI_HAS_AVX2
    return _mm_fmadd_ps(a, b, c);
#else
    return _mm_add_ps(_mm_mul_ps(a, b), c);
#endif
}
static inline __m128 tri_load4_u8(const uint8_t *p) {
    int v;
    memcpy(&v, p, 4);
    return _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(v)));
}
static inline __m128 tri_load4_u16(const uint16_t *p) {
    return _mm_cvtepi32_ps(
        _mm_cvtepu16_epi32(_mm_loadl_epi64((const __m128i *)(const void *)p)));
}

static inline void tri_mt4_decoded_sse(__m128 v0x, __m128 v0y, __m128 v0z,
                                       __m128 v1x, __m128 v1y, __m128 v1z,
                                       __m128 v2x, __m128 v2y, __m128 v2z,
                                       const tri_sse_ctx *sc,
                                       const uint32_t *prim, float *best_t,
                                       float *best_u, float *best_v,
                                       uint32_t *best_prim) {
    __m128 e1x = _mm_sub_ps(v1x, v0x), e1y = _mm_sub_ps(v1y, v0y),
           e1z = _mm_sub_ps(v1z, v0z);
    __m128 e2x = _mm_sub_ps(v2x, v0x), e2y = _mm_sub_ps(v2y, v0y),
           e2z = _mm_sub_ps(v2z, v0z);
    __m128 px = _mm_sub_ps(_mm_mul_ps(sc->diry, e2z), _mm_mul_ps(sc->dirz, e2y));
    __m128 py = _mm_sub_ps(_mm_mul_ps(sc->dirz, e2x), _mm_mul_ps(sc->dirx, e2z));
    __m128 pz = _mm_sub_ps(_mm_mul_ps(sc->dirx, e2y), _mm_mul_ps(sc->diry, e2x));
    __m128 det = _mm_add_ps(_mm_add_ps(_mm_mul_ps(e1x, px), _mm_mul_ps(e1y, py)),
                            _mm_mul_ps(e1z, pz));
    __m128 valid = tri_isect_det_parallel4(e1x, e1y, e1z, e2x, e2y, e2z, det);
    if (!_mm_movemask_ps(valid)) return;
    __m128 inv_det = _mm_div_ps(_mm_set1_ps(1.0f), det);
    __m128 tvx = _mm_sub_ps(sc->orgx, v0x);
    __m128 tvy = _mm_sub_ps(sc->orgy, v0y);
    __m128 tvz = _mm_sub_ps(sc->orgz, v0z);
    __m128 u = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(tvx, px), _mm_mul_ps(tvy, py)),
                   _mm_mul_ps(tvz, pz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_and_ps(_mm_cmpge_ps(u, _mm_setzero_ps()),
                                         _mm_cmple_ps(u, _mm_set1_ps(1.0f))));
    if (!_mm_movemask_ps(valid)) return;
    __m128 qx = _mm_sub_ps(_mm_mul_ps(tvy, e1z), _mm_mul_ps(tvz, e1y));
    __m128 qy = _mm_sub_ps(_mm_mul_ps(tvz, e1x), _mm_mul_ps(tvx, e1z));
    __m128 qz = _mm_sub_ps(_mm_mul_ps(tvx, e1y), _mm_mul_ps(tvy, e1x));
    __m128 v = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(sc->dirx, qx), _mm_mul_ps(sc->diry, qy)),
                   _mm_mul_ps(sc->dirz, qz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_and_ps(_mm_cmpge_ps(v, _mm_setzero_ps()),
                                         _mm_cmple_ps(_mm_add_ps(u, v),
                                                      _mm_set1_ps(1.0f))));
    if (!_mm_movemask_ps(valid)) return;
    __m128 t = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(e2x, qx), _mm_mul_ps(e2y, qy)),
                   _mm_mul_ps(e2z, qz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_cmpge_ps(t, sc->tmin));
    valid = _mm_and_ps(valid, _mm_cmplt_ps(t, _mm_set1_ps(*best_t)));
    int mask = _mm_movemask_ps(valid);
    if (!mask) return;
    float ta[4], ua[4], va[4];
    _mm_storeu_ps(ta, t);
    _mm_storeu_ps(ua, u);
    _mm_storeu_ps(va, v);
    while (mask) {
        int lane = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        if (ta[lane] < *best_t) {
            *best_t = ta[lane];
            *best_u = ua[lane];
            *best_v = va[lane];
            *best_prim = prim[lane];
        }
    }
}

/* q16/q8 SIMD decode (cvt + fma) then 4-wide MT. */
static inline void tri_qtri16_isect_sse(const lrt_qtri16 *b,
                                        const tri_sse_ctx *sc, const __m128 og[3],
                                        const __m128 sg[3], float *bt, float *bu,
                                        float *bv, uint32_t *bp) {
    __m128 v0x = tri_fma4(tri_load4_u16(b->qv0x), sg[0], og[0]);
    __m128 v0y = tri_fma4(tri_load4_u16(b->qv0y), sg[1], og[1]);
    __m128 v0z = tri_fma4(tri_load4_u16(b->qv0z), sg[2], og[2]);
    __m128 v1x = tri_fma4(tri_load4_u16(b->qv1x), sg[0], og[0]);
    __m128 v1y = tri_fma4(tri_load4_u16(b->qv1y), sg[1], og[1]);
    __m128 v1z = tri_fma4(tri_load4_u16(b->qv1z), sg[2], og[2]);
    __m128 v2x = tri_fma4(tri_load4_u16(b->qv2x), sg[0], og[0]);
    __m128 v2y = tri_fma4(tri_load4_u16(b->qv2y), sg[1], og[1]);
    __m128 v2z = tri_fma4(tri_load4_u16(b->qv2z), sg[2], og[2]);
    tri_mt4_decoded_sse(v0x, v0y, v0z, v1x, v1y, v1z, v2x, v2y, v2z, sc,
                        b->prim_id, bt, bu, bv, bp);
}
static inline void tri_qtri8_isect_sse(const lrt_qtri8 *b, const tri_sse_ctx *sc,
                                       float *bt, float *bu, float *bv,
                                       uint32_t *bp) {
    __m128 ox = _mm_set1_ps(b->org[0]), oy = _mm_set1_ps(b->org[1]),
           oz = _mm_set1_ps(b->org[2]);
    __m128 sx = _mm_set1_ps(b->scale[0]), sy = _mm_set1_ps(b->scale[1]),
           sz = _mm_set1_ps(b->scale[2]);
    __m128 v0x = tri_fma4(tri_load4_u8(b->qv0x), sx, ox);
    __m128 v0y = tri_fma4(tri_load4_u8(b->qv0y), sy, oy);
    __m128 v0z = tri_fma4(tri_load4_u8(b->qv0z), sz, oz);
    __m128 v1x = tri_fma4(tri_load4_u8(b->qv1x), sx, ox);
    __m128 v1y = tri_fma4(tri_load4_u8(b->qv1y), sy, oy);
    __m128 v1z = tri_fma4(tri_load4_u8(b->qv1z), sz, oz);
    __m128 v2x = tri_fma4(tri_load4_u8(b->qv2x), sx, ox);
    __m128 v2y = tri_fma4(tri_load4_u8(b->qv2y), sy, oy);
    __m128 v2z = tri_fma4(tri_load4_u8(b->qv2z), sz, oz);
    tri_mt4_decoded_sse(v0x, v0y, v0z, v1x, v1y, v1z, v2x, v2y, v2z, sc,
                        b->prim_id, bt, bu, bv, bp);
}

/* Any-hit variant: returns 1 if any lane hits within (tmin, tmax]. */
static inline int tri_block_occluded_sse(const lrt_tri4 *blk,
                                         const tri_sse_ctx *sc, __m128 tmax) {
    __m128 e1x = _mm_load_ps(blk->e1x), e1y = _mm_load_ps(blk->e1y),
           e1z = _mm_load_ps(blk->e1z);
    __m128 e2x = _mm_load_ps(blk->e2x), e2y = _mm_load_ps(blk->e2y),
           e2z = _mm_load_ps(blk->e2z);
    __m128 px = _mm_sub_ps(_mm_mul_ps(sc->diry, e2z), _mm_mul_ps(sc->dirz, e2y));
    __m128 py = _mm_sub_ps(_mm_mul_ps(sc->dirz, e2x), _mm_mul_ps(sc->dirx, e2z));
    __m128 pz = _mm_sub_ps(_mm_mul_ps(sc->dirx, e2y), _mm_mul_ps(sc->diry, e2x));
    __m128 det = _mm_add_ps(_mm_add_ps(_mm_mul_ps(e1x, px), _mm_mul_ps(e1y, py)),
                            _mm_mul_ps(e1z, pz));
    __m128 valid = tri_isect_det_parallel4(e1x, e1y, e1z, e2x, e2y, e2z, det);
    if (!_mm_movemask_ps(valid)) return 0;
    __m128 inv_det = _mm_div_ps(_mm_set1_ps(1.0f), det);
    __m128 tvx = _mm_sub_ps(sc->orgx, _mm_load_ps(blk->v0x));
    __m128 tvy = _mm_sub_ps(sc->orgy, _mm_load_ps(blk->v0y));
    __m128 tvz = _mm_sub_ps(sc->orgz, _mm_load_ps(blk->v0z));
    __m128 u = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(tvx, px), _mm_mul_ps(tvy, py)),
                   _mm_mul_ps(tvz, pz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_cmpge_ps(u, _mm_setzero_ps()));
    valid = _mm_and_ps(valid, _mm_cmple_ps(u, _mm_set1_ps(1.0f)));
    if (!_mm_movemask_ps(valid)) return 0;
    __m128 qx = _mm_sub_ps(_mm_mul_ps(tvy, e1z), _mm_mul_ps(tvz, e1y));
    __m128 qy = _mm_sub_ps(_mm_mul_ps(tvz, e1x), _mm_mul_ps(tvx, e1z));
    __m128 qz = _mm_sub_ps(_mm_mul_ps(tvx, e1y), _mm_mul_ps(tvy, e1x));
    __m128 v = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(sc->dirx, qx), _mm_mul_ps(sc->diry, qy)),
                   _mm_mul_ps(sc->dirz, qz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_cmpge_ps(v, _mm_setzero_ps()));
    valid = _mm_and_ps(valid, _mm_cmple_ps(_mm_add_ps(u, v), _mm_set1_ps(1.0f)));
    if (!_mm_movemask_ps(valid)) return 0;
    __m128 t = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(e2x, qx), _mm_mul_ps(e2y, qy)),
                   _mm_mul_ps(e2z, qz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_cmpge_ps(t, sc->tmin));
    valid = _mm_and_ps(valid, _mm_cmple_ps(t, tmax));
    return _mm_movemask_ps(valid) != 0;
}

static int tri_intersect_bvh4_sse(const lrt_tri_scene *s, const lrt_ray *ray,
                                  lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);

    float best_t = ray->tmax;
    float best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;

    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_tri4 *blocks = (const lrt_tri4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                tri_block_isect_sse(&blocks[blk0 + b], &sc, &best_t, &best_u,
                                    &best_v, &best_prim);
            }
            continue;
        }

        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        /* Push far-to-near following the precomputed octant order (no
         * per-node sort; the nearest child ends on top of the stack). */
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }

    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_occluded_bvh4_sse(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 tmax4 = _mm_set1_ps(ray->tmax);

    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;

    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref);
            uint32_t nblk = TRI_REF_NBLOCKS(ref);
            const lrt_tri4 *blocks = (const lrt_tri4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                if (tri_block_occluded_sse(&blocks[blk0 + b], &sc, tmax4)) {
                    return 1;
                }
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        /* Unordered pushes: measured faster for any-hit than tnear-sorted,
         * octant-ordered, and eager-leaf variants on this workload. */
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* ---- Curve (capsule) traversal: SSE node tests, scalar capsule leaves ---- */
static int tri_curve_intersect_bvh4(const lrt_tri_scene *s, const lrt_ray *ray,
                                    lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);

    float best_t = ray->tmax;
    float best_u = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;

    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_crv4 *blocks = (const lrt_crv4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                tri_crv4_isect(&blocks[blk0 + b], &rc, &best_t, &best_u,
                               &best_prim);
            }
            continue;
        }

        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }

    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = 0.0f;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_curve_occluded_bvh4(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 tmax4 = _mm_set1_ps(ray->tmax);
    const float t_max = ray->tmax;

    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;

    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref);
            uint32_t nblk = TRI_REF_NBLOCKS(ref);
            const lrt_crv4 *blocks = (const lrt_crv4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                float t = t_max, u = 0.0f;
                uint32_t prim = LRT_TRI_NO_HIT;
                tri_crv4_isect(&blocks[blk0 + b], &rc, &t, &u, &prim);
                if (prim != LRT_TRI_NO_HIT) return 1;
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* ---- Round-linear curve leaf: 4-wide SSE (the lrt_rlc4 block is SoA-4) ----- *
 * Lane-parallel port of tri_rlc_isect_one. Plain mul+add (no FMA) to track the
 * scalar reference's rounding. */
static inline __m128 rlc_dot4(__m128 ax, __m128 ay, __m128 az, __m128 bx,
                              __m128 by, __m128 bz) {
    return _mm_add_ps(_mm_add_ps(_mm_mul_ps(ax, bx), _mm_mul_ps(ay, by)),
                      _mm_mul_ps(az, bz));
}
static inline __m128 rlc_madd4(__m128 a, __m128 b, __m128 c) { /* a*b + c */
    return _mm_add_ps(_mm_mul_ps(a, b), c);
}

/* ConeGeometry::isInsideCappedCone, 4-wide (all-ones lanes = inside). */
static inline __m128 rlc_inside_capped_cone4(__m128 cp0x, __m128 cp0y,
                                             __m128 cp0z, __m128 cr0,
                                             __m128 cp1x, __m128 cp1y,
                                             __m128 cp1z, __m128 cr1, __m128 px,
                                             __m128 py, __m128 pz) {
    const __m128 INF = _mm_set1_ps(TRI_INF_F);
    const __m128 Z = _mm_setzero_ps();
    __m128 ok = _mm_and_ps(_mm_cmplt_ps(cp1x, INF), _mm_cmplt_ps(cp0x, INF));
    __m128 dPx = _mm_sub_ps(cp1x, cp0x), dPy = _mm_sub_ps(cp1y, cp0y),
           dPz = _mm_sub_ps(cp1z, cp0z);
    __m128 dPdP = rlc_dot4(dPx, dPy, dPz, dPx, dPy, dPz);
    __m128 dr = _mm_sub_ps(cr1, cr0);
    __m128 r0dr = _mm_mul_ps(cr0, dr);
    __m128 g = _mm_sub_ps(dPdP, _mm_mul_ps(dr, dr));
    __m128 p0px = _mm_sub_ps(px, cp0x), p0py = _mm_sub_ps(py, cp0y),
           p0pz = _mm_sub_ps(pz, cp0z);
    __m128 y = rlc_dot4(p0px, p0py, p0pz, dPx, dPy, dPz);
    __m128 cap0 = _mm_add_ps(_mm_sub_ps(Z, r0dr), _mm_set1_ps(TRI_RLC_ULP));
    __m128 cap1 = _mm_add_ps(_mm_mul_ps(_mm_sub_ps(Z, cr1), dr), dPdP);
    ok = _mm_and_ps(ok, _mm_cmpgt_ps(y, cap0));
    ok = _mm_and_ps(ok, _mm_cmplt_ps(y, cap1));
    __m128 p0p2 = rlc_dot4(p0px, p0py, p0pz, p0px, p0py, p0pz);
    __m128 lhs = _mm_sub_ps(_mm_mul_ps(p0p2, g), _mm_mul_ps(y, y));
    __m128 rhs = _mm_add_ps(_mm_mul_ps(dPdP, _mm_mul_ps(cr0, cr0)),
                            _mm_mul_ps(_mm_set1_ps(2.0f), _mm_mul_ps(r0dr, y)));
    return _mm_and_ps(ok, _mm_cmplt_ps(lhs, rhs));
}

/* ConeGeometry::isClippedByPlane, 4-wide. */
static inline __m128 rlc_clipped_by_plane4(__m128 cp0x, __m128 cp0y,
                                           __m128 cp0z, __m128 cr0, __m128 cp1x,
                                           __m128 cp1y, __m128 cp1z, __m128 cr1,
                                           __m128 px, __m128 py, __m128 pz) {
    const __m128 INF = _mm_set1_ps(TRI_INF_F);
    __m128 ok = _mm_and_ps(_mm_cmplt_ps(cp1x, INF), _mm_cmplt_ps(cp0x, INF));
    __m128 dPx = _mm_sub_ps(cp1x, cp0x), dPy = _mm_sub_ps(cp1y, cp0y),
           dPz = _mm_sub_ps(cp1z, cp0z);
    __m128 r0dr = _mm_mul_ps(cr0, _mm_sub_ps(cr1, cr0));
    __m128 p0px = _mm_sub_ps(px, cp0x), p0py = _mm_sub_ps(py, cp0y),
           p0pz = _mm_sub_ps(pz, cp0z);
    __m128 y = rlc_dot4(p0px, p0py, p0pz, dPx, dPy, dPz);
    return _mm_and_ps(ok, _mm_cmpgt_ps(y, _mm_sub_ps(_mm_setzero_ps(), r0dr)));
}

static inline void tri_rlc4_isect_sse(const lrt_rlc4 *blk,
                                      const tri_sse_ctx *sc, float *best_t,
                                      float *best_u, uint32_t *best_prim) {
    const __m128 INF = _mm_set1_ps(TRI_INF_F);
    const __m128 NINF = _mm_set1_ps(-TRI_INF_F);
    const __m128 Z = _mm_setzero_ps();
    const __m128 ONE = _mm_set1_ps(1.0f);
    const __m128 ulpneg = _mm_set1_ps(-TRI_RLC_ULP);

    __m128 dirx = sc->dirx, diry = sc->diry, dirz = sc->dirz;
    __m128 orgx = sc->orgx, orgy = sc->orgy, orgz = sc->orgz;
    __m128 dOdO = rlc_dot4(dirx, diry, dirz, dirx, diry, dirz);
    __m128 rcp_dOdO = _mm_div_ps(ONE, dOdO);

    __m128 p0x = _mm_loadu_ps(blk->p0x), p0y = _mm_loadu_ps(blk->p0y),
           p0z = _mm_loadu_ps(blk->p0z), r0 = _mm_loadu_ps(blk->r0);
    __m128 p1x = _mm_loadu_ps(blk->p1x), p1y = _mm_loadu_ps(blk->p1y),
           p1z = _mm_loadu_ps(blk->p1z), r1 = _mm_loadu_ps(blk->r1);
    __m128 pLx = _mm_loadu_ps(blk->pLx), pLy = _mm_loadu_ps(blk->pLy),
           pLz = _mm_loadu_ps(blk->pLz), rL = _mm_loadu_ps(blk->rL);
    __m128 pRx = _mm_loadu_ps(blk->pRx), pRy = _mm_loadu_ps(blk->pRy),
           pRz = _mm_loadu_ps(blk->pRz), rR = _mm_loadu_ps(blk->rR);

    /* re-center ray origin near each segment */
    __m128 cx = _mm_mul_ps(_mm_set1_ps(0.5f), _mm_add_ps(p0x, p1x));
    __m128 cy = _mm_mul_ps(_mm_set1_ps(0.5f), _mm_add_ps(p0y, p1y));
    __m128 cz = _mm_mul_ps(_mm_set1_ps(0.5f), _mm_add_ps(p0z, p1z));
    __m128 dt = _mm_mul_ps(rlc_dot4(_mm_sub_ps(cx, orgx), _mm_sub_ps(cy, orgy),
                                    _mm_sub_ps(cz, orgz), dirx, diry, dirz),
                           rcp_dOdO);
    __m128 ox = rlc_madd4(dt, dirx, orgx);
    __m128 oy = rlc_madd4(dt, diry, orgy);
    __m128 oz = rlc_madd4(dt, dirz, orgz);

    __m128 dPx = _mm_sub_ps(p1x, p0x), dPy = _mm_sub_ps(p1y, p0y),
           dPz = _mm_sub_ps(p1z, p0z);
    __m128 dPdP = rlc_dot4(dPx, dPy, dPz, dPx, dPy, dPz);
    __m128 dr = _mm_sub_ps(r1, r0);
    __m128 r0dr = _mm_mul_ps(r0, dr);
    __m128 g = _mm_sub_ps(dPdP, _mm_mul_ps(dr, dr));
    __m128 Ox = _mm_sub_ps(ox, p0x), Oy = _mm_sub_ps(oy, p0y),
           Oz = _mm_sub_ps(oz, p0z);
    __m128 OdP = rlc_dot4(Ox, Oy, Oz, dPx, dPy, dPz);
    __m128 dOdP = rlc_dot4(dirx, diry, dirz, dPx, dPy, dPz);
    __m128 yp = _mm_add_ps(OdP, r0dr);

    /* cone quadratic */
    __m128 OO = rlc_dot4(Ox, Oy, Oz, Ox, Oy, Oz);
    __m128 OdO = rlc_dot4(dirx, diry, dirz, Ox, Oy, Oz);
    __m128 A = _mm_sub_ps(_mm_mul_ps(g, dOdO), _mm_mul_ps(dOdP, dOdP));
    __m128 B = _mm_mul_ps(_mm_set1_ps(2.0f),
                          _mm_sub_ps(_mm_mul_ps(g, OdO), _mm_mul_ps(dOdP, yp)));
    __m128 C = _mm_sub_ps(
        _mm_sub_ps(_mm_sub_ps(_mm_mul_ps(g, OO), _mm_mul_ps(OdP, OdP)),
                   _mm_mul_ps(_mm_mul_ps(r0, r0), dPdP)),
        _mm_mul_ps(_mm_mul_ps(_mm_set1_ps(2.0f), r0dr), OdP));
    __m128 D = _mm_sub_ps(_mm_mul_ps(B, B),
                          _mm_mul_ps(_mm_set1_ps(4.0f), _mm_mul_ps(A, C)));
    const __m128 absmask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    __m128 cone_ok = _mm_and_ps(
        _mm_and_ps(_mm_cmpge_ps(D, Z), _mm_cmpgt_ps(g, Z)),
        _mm_cmpgt_ps(_mm_and_ps(A, absmask), _mm_set1_ps(TRI_RLC_MIN_A)));
    __m128 Q = _mm_sqrt_ps(_mm_max_ps(D, Z));
    __m128 twoA = _mm_add_ps(A, A);
    __m128 rcp2A = _mm_div_ps(ONE, _mm_blendv_ps(ONE, twoA, cone_ok));
    __m128 nB = _mm_sub_ps(Z, B);
    __m128 tf = _mm_mul_ps(_mm_sub_ps(nB, Q), rcp2A);
    __m128 yf = rlc_madd4(tf, dOdP, yp);
    __m128 lo_ok = _mm_and_ps(cone_ok, _mm_and_ps(_mm_cmpgt_ps(yf, ulpneg),
                                                  _mm_cmple_ps(yf, g)));
    __m128 t_cone_lo = _mm_blendv_ps(INF, tf, lo_ok);
    __m128 tb = _mm_mul_ps(_mm_add_ps(nB, Q), rcp2A);
    __m128 yb = rlc_madd4(tb, dOdP, yp);
    __m128 hi_ok = _mm_and_ps(cone_ok, _mm_and_ps(_mm_cmpgt_ps(yb, ulpneg),
                                                  _mm_cmple_ps(yb, g)));
    __m128 t_cone_hi = _mm_blendv_ps(NINF, tb, hi_ok);

    /* clip cone hits inside the neighbor cones */
    __m128 hlx = rlc_madd4(t_cone_lo, dirx, ox), hly = rlc_madd4(t_cone_lo, diry, oy),
           hlz = rlc_madd4(t_cone_lo, dirz, oz);
    __m128 clip_lo = _mm_or_ps(
        rlc_inside_capped_cone4(p0x, p0y, p0z, r0, pLx, pLy, pLz, rL, hlx, hly, hlz),
        rlc_inside_capped_cone4(p1x, p1y, p1z, r1, pRx, pRy, pRz, rR, hlx, hly, hlz));
    t_cone_lo = _mm_blendv_ps(t_cone_lo, INF, clip_lo);
    __m128 hhx = rlc_madd4(t_cone_hi, dirx, ox), hhy = rlc_madd4(t_cone_hi, diry, oy),
           hhz = rlc_madd4(t_cone_hi, dirz, oz);
    __m128 clip_hi = _mm_or_ps(
        rlc_inside_capped_cone4(p0x, p0y, p0z, r0, pLx, pLy, pLz, rL, hhx, hhy, hhz),
        rlc_inside_capped_cone4(p1x, p1y, p1z, r1, pRx, pRy, pRz, rR, hhx, hhy, hhz));
    t_cone_hi = _mm_blendv_ps(t_cone_hi, NINF, clip_hi);

    /* end sphere at p1 (cap past y>g), clipped by the next segment's cone */
    __m128 O1x = _mm_sub_ps(ox, p1x), O1y = _mm_sub_ps(oy, p1y),
           O1z = _mm_sub_ps(oz, p1z);
    __m128 O1dO = rlc_dot4(O1x, O1y, O1z, dirx, diry, dirz);
    __m128 O1O1 = rlc_dot4(O1x, O1y, O1z, O1x, O1y, O1z);
    __m128 h2 = _mm_sub_ps(_mm_mul_ps(O1dO, O1dO),
                           _mm_mul_ps(dOdO, _mm_sub_ps(O1O1, _mm_mul_ps(r1, r1))));
    __m128 h2ok = _mm_cmpge_ps(h2, Z);
    __m128 rhs1 = _mm_sqrt_ps(_mm_max_ps(h2, Z));
    __m128 nO1dO = _mm_sub_ps(Z, O1dO);
    __m128 sf = _mm_mul_ps(_mm_sub_ps(nO1dO, rhs1), rcp_dOdO);
    __m128 ysf = rlc_madd4(sf, dOdP, yp);
    __m128 hsfx = rlc_madd4(sf, dirx, ox), hsfy = rlc_madd4(sf, diry, oy),
           hsfz = rlc_madd4(sf, dirz, oz);
    __m128 sf_ok = _mm_andnot_ps(
        rlc_clipped_by_plane4(p1x, p1y, p1z, r1, pRx, pRy, pRz, rR, hsfx, hsfy, hsfz),
        _mm_and_ps(h2ok, _mm_cmpgt_ps(ysf, g)));
    __m128 t_sph1_lo = _mm_blendv_ps(INF, sf, sf_ok);
    __m128 sb = _mm_mul_ps(_mm_add_ps(nO1dO, rhs1), rcp_dOdO);
    __m128 ysb = rlc_madd4(sb, dOdP, yp);
    __m128 hsbx = rlc_madd4(sb, dirx, ox), hsby = rlc_madd4(sb, diry, oy),
           hsbz = rlc_madd4(sb, dirz, oz);
    __m128 sb_ok = _mm_andnot_ps(
        rlc_clipped_by_plane4(p1x, p1y, p1z, r1, pRx, pRy, pRz, rR, hsbx, hsby, hsbz),
        _mm_and_ps(h2ok, _mm_cmpgt_ps(ysb, g)));
    __m128 t_sph1_hi = _mm_blendv_ps(NINF, sb, sb_ok);

    /* begin sphere at p0, only where there is no left neighbor */
    __m128 isStart = _mm_cmpeq_ps(pLx, INF);
    __m128 O0x = _mm_sub_ps(ox, p0x), O0y = _mm_sub_ps(oy, p0y),
           O0z = _mm_sub_ps(oz, p0z);
    __m128 O0dO = rlc_dot4(O0x, O0y, O0z, dirx, diry, dirz);
    __m128 O0O0 = rlc_dot4(O0x, O0y, O0z, O0x, O0y, O0z);
    __m128 h2b = _mm_sub_ps(_mm_mul_ps(O0dO, O0dO),
                            _mm_mul_ps(dOdO, _mm_sub_ps(O0O0, _mm_mul_ps(r0, r0))));
    __m128 h2bok = _mm_and_ps(isStart, _mm_cmpge_ps(h2b, Z));
    __m128 rhs0 = _mm_sqrt_ps(_mm_max_ps(h2b, Z));
    __m128 nO0dO = _mm_sub_ps(Z, O0dO);
    __m128 b0f = _mm_mul_ps(_mm_sub_ps(nO0dO, rhs0), rcp_dOdO);
    __m128 t_sph0_lo = _mm_blendv_ps(
        INF, b0f, _mm_and_ps(h2bok, _mm_cmplt_ps(rlc_madd4(b0f, dOdP, yp), Z)));
    __m128 b0b = _mm_mul_ps(_mm_add_ps(nO0dO, rhs0), rcp_dOdO);
    __m128 t_sph0_hi = _mm_blendv_ps(
        NINF, b0b, _mm_and_ps(h2bok, _mm_cmplt_ps(rlc_madd4(b0b, dOdP, yp), Z)));

    /* CSG union; then kill lanes that miss a proper cone (scalar early-out) */
    __m128 lo = _mm_min_ps(t_cone_lo, _mm_min_ps(t_sph0_lo, t_sph1_lo));
    __m128 hi = _mm_max_ps(t_cone_hi, _mm_max_ps(t_sph0_hi, t_sph1_hi));
    __m128 lane_alive = _mm_or_ps(cone_ok, _mm_cmple_ps(g, Z));
    lo = _mm_blendv_ps(INF, lo, lane_alive);
    hi = _mm_blendv_ps(NINF, hi, lane_alive);

    __m128 tmin = sc->tmin;
    __m128 best = _mm_set1_ps(*best_t);
    __m128 glo = _mm_add_ps(dt, lo), ghi = _mm_add_ps(dt, hi);
    __m128 lo_v = _mm_and_ps(_mm_cmpneq_ps(lo, INF),
                             _mm_and_ps(_mm_cmpge_ps(glo, tmin), _mm_cmplt_ps(glo, best)));
    __m128 hi_v = _mm_and_ps(_mm_cmpneq_ps(hi, NINF),
                             _mm_and_ps(_mm_cmpge_ps(ghi, tmin), _mm_cmplt_ps(ghi, best)));
    __m128 any = _mm_or_ps(lo_v, hi_v);

    /* prim != NO_HIT (drops padding lanes) */
    __m128i primi = _mm_loadu_si128((const __m128i *)(const void *)blk->prim);
    __m128 prim_ok = _mm_castsi128_ps(_mm_xor_si128(
        _mm_cmpeq_epi32(primi, _mm_set1_epi32((int)0xFFFFFFFFu)), _mm_set1_epi32(-1)));
    __m128 hitmask = _mm_and_ps(any, prim_ok);
    int mask = _mm_movemask_ps(hitmask);
    if (!mask) return;

    __m128 tloc = _mm_blendv_ps(hi, lo, lo_v);
    __m128 tworld = _mm_add_ps(dt, tloc);
    /* u = clamp((yp + tloc*dOdP)/g): y<0 -> 0 (begin), y>g -> 1 (end), else y/g */
    __m128 yhit = rlc_madd4(tloc, dOdP, yp);
    __m128 u = _mm_div_ps(yhit, _mm_max_ps(g, _mm_set1_ps(1e-20f)));
    u = _mm_min_ps(ONE, _mm_max_ps(Z, u));

    _Alignas(16) float ta[4], ua[4];
    _Alignas(16) uint32_t pa[4];
    _mm_store_ps(ta, tworld);
    _mm_store_ps(ua, u);
    _mm_store_si128((__m128i *)(void *)pa, primi);
    while (mask) {
        int lane = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        if (ta[lane] < *best_t) {
            *best_t = ta[lane];
            *best_u = ua[lane];
            *best_prim = pa[lane];
        }
    }
}

/* ---- Round-linear curve traversal: SSE node tests + 4-wide cone leaves ---- */
static int tri_rlcurve_intersect_bvh4(const lrt_tri_scene *s, const lrt_ray *ray,
                                      lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);

    float best_t = ray->tmax;
    float best_u = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;

    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_rlc4 *blocks = (const lrt_rlc4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                tri_rlc4_isect_sse(&blocks[blk0 + b], &sc, &best_t, &best_u,
                                   &best_prim);
            }
            continue;
        }

        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }

    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = 0.0f;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_rlcurve_occluded_bvh4(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 tmax4 = _mm_set1_ps(ray->tmax);
    const float t_max = ray->tmax;

    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;

    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref);
            uint32_t nblk = TRI_REF_NBLOCKS(ref);
            const lrt_rlc4 *blocks = (const lrt_rlc4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                float t = t_max, u = 0.0f;
                uint32_t prim = LRT_TRI_NO_HIT;
                tri_rlc4_isect_sse(&blocks[blk0 + b], &sc, &t, &u, &prim);
                if (prim != LRT_TRI_NO_HIT) return 1;
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* ---- Point + flat-curve leaves: 4-wide SSE over the SoA blocks ------------ */

/* 4-wide point intersection (sphere / ray-facing disc / oriented disc). */
static inline void tri_point4_isect_sse(const lrt_point4 *blk,
                                        const tri_sse_ctx *sc, int point_type,
                                        float *best_t, float *best_u,
                                        uint32_t *best_prim) {
    const __m128 INF = _mm_set1_ps(TRI_INF_F);
    const __m128 Z = _mm_setzero_ps();
    const __m128 ONE = _mm_set1_ps(1.0f);
    __m128 dirx = sc->dirx, diry = sc->diry, dirz = sc->dirz;
    __m128 cx = _mm_loadu_ps(blk->cx), cy = _mm_loadu_ps(blk->cy),
           cz = _mm_loadu_ps(blk->cz), r = _mm_loadu_ps(blk->r);
    __m128 c0x = _mm_sub_ps(cx, sc->orgx), c0y = _mm_sub_ps(cy, sc->orgy),
           c0z = _mm_sub_ps(cz, sc->orgz);
    __m128 r2 = _mm_mul_ps(r, r);
    __m128 tmin = sc->tmin, best = _mm_set1_ps(*best_t);
    __m128 tcand;

    if (point_type == TRI_POINT_ORIENTED_DISC) {
        __m128 nx = _mm_loadu_ps(blk->nx), ny = _mm_loadu_ps(blk->ny),
               nz = _mm_loadu_ps(blk->nz);
        __m128 div = rlc_dot4(dirx, diry, dirz, nx, ny, nz);
        __m128 nonpar = _mm_cmpneq_ps(div, Z);
        __m128 divs = _mm_blendv_ps(ONE, div, nonpar);
        __m128 t = _mm_div_ps(rlc_dot4(c0x, c0y, c0z, nx, ny, nz), divs);
        __m128 hx = _mm_sub_ps(_mm_add_ps(sc->orgx, _mm_mul_ps(t, dirx)), cx);
        __m128 hy = _mm_sub_ps(_mm_add_ps(sc->orgy, _mm_mul_ps(t, diry)), cy);
        __m128 hz = _mm_sub_ps(_mm_add_ps(sc->orgz, _mm_mul_ps(t, dirz)), cz);
        __m128 dd = rlc_dot4(hx, hy, hz, hx, hy, hz);
        __m128 valid = _mm_and_ps(nonpar, _mm_and_ps(_mm_cmpge_ps(t, tmin),
                                                     _mm_cmplt_ps(t, best)));
        valid = _mm_and_ps(valid, _mm_cmplt_ps(dd, r2));
        tcand = _mm_blendv_ps(INF, t, valid);
    } else {
        __m128 dOdO = rlc_dot4(dirx, diry, dirz, dirx, diry, dirz);
        __m128 rd2 = _mm_div_ps(ONE, dOdO);
        __m128 proj = _mm_mul_ps(rlc_dot4(c0x, c0y, c0z, dirx, diry, dirz), rd2);
        __m128 px = _mm_sub_ps(c0x, _mm_mul_ps(proj, dirx));
        __m128 py = _mm_sub_ps(c0y, _mm_mul_ps(proj, diry));
        __m128 pz = _mm_sub_ps(c0z, _mm_mul_ps(proj, dirz));
        __m128 l2 = rlc_dot4(px, py, pz, px, py, pz);
        __m128 inside = _mm_cmple_ps(l2, r2);
        if (point_type == TRI_POINT_DISC) {
            __m128 valid = _mm_and_ps(inside, _mm_and_ps(_mm_cmpge_ps(proj, tmin),
                                                         _mm_cmplt_ps(proj, best)));
            tcand = _mm_blendv_ps(INF, proj, valid);
        } else { /* sphere: nearest root */
            __m128 td = _mm_sqrt_ps(_mm_max_ps(_mm_mul_ps(_mm_sub_ps(r2, l2), rd2), Z));
            __m128 tf = _mm_sub_ps(proj, td), tb = _mm_add_ps(proj, td);
            __m128 fok = _mm_and_ps(inside, _mm_and_ps(_mm_cmpge_ps(tf, tmin),
                                                       _mm_cmplt_ps(tf, best)));
            __m128 bok = _mm_and_ps(inside, _mm_and_ps(_mm_cmpge_ps(tb, tmin),
                                                       _mm_cmplt_ps(tb, best)));
            tcand = INF;
            tcand = _mm_blendv_ps(tcand, tb, bok);
            tcand = _mm_blendv_ps(tcand, tf, fok);
        }
    }

    __m128i primi = _mm_loadu_si128((const __m128i *)(const void *)blk->prim);
    __m128 prim_ok = _mm_castsi128_ps(_mm_xor_si128(
        _mm_cmpeq_epi32(primi, _mm_set1_epi32((int)0xFFFFFFFFu)), _mm_set1_epi32(-1)));
    __m128 hit = _mm_and_ps(_mm_cmpneq_ps(tcand, INF), prim_ok);
    int mask = _mm_movemask_ps(hit);
    if (!mask) return;
    _Alignas(16) float ta[4];
    _Alignas(16) uint32_t pa[4];
    _mm_store_ps(ta, tcand);
    _mm_store_si128((__m128i *)(void *)pa, primi);
    while (mask) {
        int lane = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        if (ta[lane] < *best_t) {
            *best_t = ta[lane];
            *best_u = 0.0f;
            *best_prim = pa[lane];
        }
    }
}

/* 4-wide Moller-Trumbore on one ribbon triangle; returns t per lane (or +INF).
 * Double-sided; `bok` masks lanes whose ribbon is degenerate (ray || tangent). */
static inline __m128 tri_flat_tri_sse(__m128 ax, __m128 ay, __m128 az,
                                      __m128 e1x, __m128 e1y, __m128 e1z,
                                      __m128 e2x, __m128 e2y, __m128 e2z,
                                      const tri_sse_ctx *sc, __m128 best,
                                      __m128 bok) {
    const __m128 INF = _mm_set1_ps(TRI_INF_F);
    const __m128 Z = _mm_setzero_ps();
    const __m128 ONE = _mm_set1_ps(1.0f);
    __m128 dirx = sc->dirx, diry = sc->diry, dirz = sc->dirz;
    __m128 pvx = _mm_sub_ps(_mm_mul_ps(diry, e2z), _mm_mul_ps(dirz, e2y));
    __m128 pvy = _mm_sub_ps(_mm_mul_ps(dirz, e2x), _mm_mul_ps(dirx, e2z));
    __m128 pvz = _mm_sub_ps(_mm_mul_ps(dirx, e2y), _mm_mul_ps(diry, e2x));
    __m128 det = rlc_dot4(e1x, e1y, e1z, pvx, pvy, pvz);
    __m128 detok = tri_isect_det_parallel4(e1x, e1y, e1z, e2x, e2y, e2z, det);
    __m128 inv = _mm_div_ps(ONE, _mm_blendv_ps(ONE, det, detok));
    __m128 tvx = _mm_sub_ps(sc->orgx, ax), tvy = _mm_sub_ps(sc->orgy, ay),
           tvz = _mm_sub_ps(sc->orgz, az);
    __m128 u = _mm_mul_ps(rlc_dot4(tvx, tvy, tvz, pvx, pvy, pvz), inv);
    __m128 valid = _mm_and_ps(_mm_and_ps(detok, bok),
                              _mm_and_ps(_mm_cmpge_ps(u, Z), _mm_cmple_ps(u, ONE)));
    __m128 qvx = _mm_sub_ps(_mm_mul_ps(tvy, e1z), _mm_mul_ps(tvz, e1y));
    __m128 qvy = _mm_sub_ps(_mm_mul_ps(tvz, e1x), _mm_mul_ps(tvx, e1z));
    __m128 qvz = _mm_sub_ps(_mm_mul_ps(tvx, e1y), _mm_mul_ps(tvy, e1x));
    __m128 v = _mm_mul_ps(rlc_dot4(dirx, diry, dirz, qvx, qvy, qvz), inv);
    valid = _mm_and_ps(valid, _mm_and_ps(_mm_cmpge_ps(v, Z),
                                         _mm_cmple_ps(_mm_add_ps(u, v), ONE)));
    __m128 t = _mm_mul_ps(rlc_dot4(e2x, e2y, e2z, qvx, qvy, qvz), inv);
    valid = _mm_and_ps(valid, _mm_and_ps(_mm_cmpge_ps(t, sc->tmin),
                                         _mm_cmplt_ps(t, best)));
    return _mm_blendv_ps(INF, t, valid);
}

/* 4-wide flat (ribbon) curve intersection. */
static inline void tri_flat4_isect_sse(const lrt_flat4 *blk,
                                       const tri_sse_ctx *sc, float *best_t,
                                       float *best_u, uint32_t *best_prim) {
    const __m128 INF = _mm_set1_ps(TRI_INF_F);
    const __m128 Z = _mm_setzero_ps();
    const __m128 ONE = _mm_set1_ps(1.0f);
    __m128 dirx = sc->dirx, diry = sc->diry, dirz = sc->dirz;
    __m128 p0x = _mm_loadu_ps(blk->p0x), p0y = _mm_loadu_ps(blk->p0y),
           p0z = _mm_loadu_ps(blk->p0z), r0 = _mm_loadu_ps(blk->r0);
    __m128 p1x = _mm_loadu_ps(blk->p1x), p1y = _mm_loadu_ps(blk->p1y),
           p1z = _mm_loadu_ps(blk->p1z), r1 = _mm_loadu_ps(blk->r1);
    __m128 Tx = _mm_sub_ps(p1x, p0x), Ty = _mm_sub_ps(p1y, p0y),
           Tz = _mm_sub_ps(p1z, p0z);
    __m128 bx = _mm_sub_ps(_mm_mul_ps(Ty, dirz), _mm_mul_ps(Tz, diry));
    __m128 by = _mm_sub_ps(_mm_mul_ps(Tz, dirx), _mm_mul_ps(Tx, dirz));
    __m128 bz = _mm_sub_ps(_mm_mul_ps(Tx, diry), _mm_mul_ps(Ty, dirx));
    __m128 bl2 = rlc_dot4(bx, by, bz, bx, by, bz);
    __m128 bok = _mm_cmpgt_ps(bl2, _mm_set1_ps(1e-20f));
    __m128 invb = _mm_div_ps(ONE, _mm_sqrt_ps(_mm_max_ps(bl2, _mm_set1_ps(1e-30f))));
    bx = _mm_mul_ps(bx, invb); by = _mm_mul_ps(by, invb); bz = _mm_mul_ps(bz, invb);
    __m128 r0b_x = _mm_mul_ps(r0, bx), r0b_y = _mm_mul_ps(r0, by), r0b_z = _mm_mul_ps(r0, bz);
    __m128 r1b_x = _mm_mul_ps(r1, bx), r1b_y = _mm_mul_ps(r1, by), r1b_z = _mm_mul_ps(r1, bz);
    /* corners q0=p0+r0b, q1=p1+r1b, q2=p1-r1b, q3=p0-r0b */
    __m128 q0x = _mm_add_ps(p0x, r0b_x), q0y = _mm_add_ps(p0y, r0b_y), q0z = _mm_add_ps(p0z, r0b_z);
    __m128 q1x = _mm_add_ps(p1x, r1b_x), q1y = _mm_add_ps(p1y, r1b_y), q1z = _mm_add_ps(p1z, r1b_z);
    __m128 q2x = _mm_sub_ps(p1x, r1b_x), q2y = _mm_sub_ps(p1y, r1b_y), q2z = _mm_sub_ps(p1z, r1b_z);
    __m128 q3x = _mm_sub_ps(p0x, r0b_x), q3y = _mm_sub_ps(p0y, r0b_y), q3z = _mm_sub_ps(p0z, r0b_z);
    __m128 best = _mm_set1_ps(*best_t);
    /* triangle A = (q0,q1,q2), triangle B = (q0,q2,q3) */
    __m128 tA = tri_flat_tri_sse(q0x, q0y, q0z, _mm_sub_ps(q1x, q0x),
                                 _mm_sub_ps(q1y, q0y), _mm_sub_ps(q1z, q0z),
                                 _mm_sub_ps(q2x, q0x), _mm_sub_ps(q2y, q0y),
                                 _mm_sub_ps(q2z, q0z), sc, best, bok);
    __m128 tB = tri_flat_tri_sse(q0x, q0y, q0z, _mm_sub_ps(q2x, q0x),
                                 _mm_sub_ps(q2y, q0y), _mm_sub_ps(q2z, q0z),
                                 _mm_sub_ps(q3x, q0x), _mm_sub_ps(q3y, q0y),
                                 _mm_sub_ps(q3z, q0z), sc, best, bok);
    __m128 t = _mm_min_ps(tA, tB);
    __m128i primi = _mm_loadu_si128((const __m128i *)(const void *)blk->prim);
    __m128 prim_ok = _mm_castsi128_ps(_mm_xor_si128(
        _mm_cmpeq_epi32(primi, _mm_set1_epi32((int)0xFFFFFFFFu)), _mm_set1_epi32(-1)));
    __m128 hit = _mm_and_ps(_mm_cmpneq_ps(t, INF), prim_ok);
    int mask = _mm_movemask_ps(hit);
    if (!mask) return;
    /* u = clamp(dot(hit - p0, T)/|T|^2) along the segment */
    __m128 Tl2 = rlc_dot4(Tx, Ty, Tz, Tx, Ty, Tz);
    __m128 hpx = _mm_sub_ps(_mm_add_ps(sc->orgx, _mm_mul_ps(t, dirx)), p0x);
    __m128 hpy = _mm_sub_ps(_mm_add_ps(sc->orgy, _mm_mul_ps(t, diry)), p0y);
    __m128 hpz = _mm_sub_ps(_mm_add_ps(sc->orgz, _mm_mul_ps(t, dirz)), p0z);
    __m128 uu = _mm_div_ps(rlc_dot4(hpx, hpy, hpz, Tx, Ty, Tz),
                           _mm_max_ps(Tl2, _mm_set1_ps(1e-20f)));
    uu = _mm_min_ps(ONE, _mm_max_ps(Z, uu));
    _Alignas(16) float ta[4], ua[4];
    _Alignas(16) uint32_t pa[4];
    _mm_store_ps(ta, t);
    _mm_store_ps(ua, uu);
    _mm_store_si128((__m128i *)(void *)pa, primi);
    while (mask) {
        int lane = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        if (ta[lane] < *best_t) {
            *best_t = ta[lane];
            *best_u = ua[lane];
            *best_prim = pa[lane];
        }
    }
}

/* ---- Point + flat-curve traversal: SSE node tests + 4-wide SSE leaves ----- */
static int tri_point_intersect_bvh4(const lrt_tri_scene *s, const lrt_ray *ray,
                                    lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    const int ptype = s->point_type;
    float best_t = ray->tmax, best_u = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref), nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_point4 *blocks = (const lrt_point4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++)
                tri_point4_isect_sse(&blocks[blk0 + b], &sc, ptype, &best_t,
                                     &best_u, &best_prim);
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }
    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = 0.0f;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_point_occluded_bvh4(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    const int ptype = s->point_type;
    __m128 tmax4 = _mm_set1_ps(ray->tmax);
    const float t_max = ray->tmax;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            const lrt_point4 *blocks = (const lrt_point4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                float t = t_max, u = 0.0f;
                uint32_t prim = LRT_TRI_NO_HIT;
                tri_point4_isect_sse(&blocks[blk0 + b], &sc, ptype, &t, &u, &prim);
                if (prim != LRT_TRI_NO_HIT) return 1;
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

static int tri_flatcurve_intersect_bvh4(const lrt_tri_scene *s,
                                        const lrt_ray *ray, lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    float best_t = ray->tmax, best_u = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref), nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_flat4 *blocks = (const lrt_flat4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++)
                tri_flat4_isect_sse(&blocks[blk0 + b], &sc, &best_t, &best_u,
                                    &best_prim);
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }
    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = 0.0f;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_flatcurve_occluded_bvh4(const lrt_tri_scene *s,
                                       const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 tmax4 = _mm_set1_ps(ray->tmax);
    const float t_max = ray->tmax;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            const lrt_flat4 *blocks = (const lrt_flat4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                float t = t_max, u = 0.0f;
                uint32_t prim = LRT_TRI_NO_HIT;
                tri_flat4_isect_sse(&blocks[blk0 + b], &sc, &t, &u, &prim);
                if (prim != LRT_TRI_NO_HIT) return 1;
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* Evaluate ONE cubic (control points broadcast in C[16]) at 4 different u values
 * (lanes of `u`): position+radius P[4] and derivative dP[4], each a 4-lane vec. */
static inline void tri_bez_eval4u(const __m128 C[16], __m128 u, __m128 P[4],
                                  __m128 dP[4]) {
    const __m128 ONE = _mm_set1_ps(1.0f);
    __m128 u1 = _mm_sub_ps(ONE, u);
    __m128 u1u1 = _mm_mul_ps(u1, u1), uu = _mm_mul_ps(u, u);
    __m128 b0 = _mm_mul_ps(u1u1, u1);
    __m128 b1 = _mm_mul_ps(_mm_set1_ps(3.0f), _mm_mul_ps(u1u1, u));
    __m128 b2 = _mm_mul_ps(_mm_set1_ps(3.0f), _mm_mul_ps(u1, uu));
    __m128 b3 = _mm_mul_ps(uu, u);
    __m128 e0 = _mm_mul_ps(_mm_set1_ps(3.0f), u1u1);
    __m128 e1 = _mm_mul_ps(_mm_set1_ps(6.0f), _mm_mul_ps(u1, u));
    __m128 e2 = _mm_mul_ps(_mm_set1_ps(3.0f), uu);
    for (int c = 0; c < 4; c++) {
        __m128 C0 = C[c], C1 = C[4 + c], C2 = C[8 + c], C3 = C[12 + c];
        P[c] = _mm_add_ps(_mm_add_ps(_mm_mul_ps(b0, C0), _mm_mul_ps(b1, C1)),
                          _mm_add_ps(_mm_mul_ps(b2, C2), _mm_mul_ps(b3, C3)));
        __m128 d0 = _mm_sub_ps(C1, C0), d1 = _mm_sub_ps(C2, C1),
               d2 = _mm_sub_ps(C3, C2);
        dP[c] = _mm_add_ps(_mm_add_ps(_mm_mul_ps(e0, d0), _mm_mul_ps(e1, d1)),
                           _mm_mul_ps(e2, d2));
    }
}

/* SIMD adaptive sweep of ONE Bezier curve: recursively split [0,1], evaluating 4
 * sub-intervals at a time (4-wide), pruning each whose bounding capsule the ray
 * misses, and refining the surviving leaf intervals with the scalar cone+Newton.
 * This vectorizes the broad phase that the scalar tri_bez_isect_one does one
 * interval at a time, so low pre-subdivision (K small) stays fast. */
static int tri_bez_sweep_sse(const float cp_world[16], const tri_ray_ctx *rc,
                             float best_t, float *t_out, float *u_out) {
    const __m128 Z = _mm_setzero_ps(), ONE = _mm_set1_ps(1.0f);
    float cpl[16];
    __m128 C[16];
    for (int k = 0; k < 4; k++)
        for (int c = 0; c < 4; c++) {
            float v = cp_world[k * 4 + c] - (c < 3 ? rc->org[c] : 0.0f);
            cpl[k * 4 + c] = v;
            C[k * 4 + c] = _mm_set1_ps(v);
        }
    const float dir[3] = {rc->dir[0], rc->dir[1], rc->dir[2]};
    __m128 dirx = _mm_set1_ps(dir[0]), diry = _mm_set1_ps(dir[1]),
           dirz = _mm_set1_ps(dir[2]);
    __m128 dOdO = _mm_set1_ps(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    const float tmin = rc->tmin;
    __m128 tmin4 = _mm_set1_ps(tmin);
    float best = best_t, bu = 0.0f;
    int hit = 0;

    enum { BEZ_SIMD_DEPTH = 3 }; /* 4^3 = 64 leaf intervals (adaptively pruned) */
    struct {
        float a, b;
        int depth;
    } stk[4 * BEZ_SIMD_DEPTH + 8];
    int sp = 0;
    stk[sp].a = 0.0f;
    stk[sp].b = 1.0f;
    stk[sp].depth = 0;
    sp++;
    while (sp > 0) {
        float a = stk[--sp].a, b = stk[sp].b;
        int depth = stk[sp].depth;
        float h = b - a, hw = h * 0.25f;
        __m128 best4 = _mm_set1_ps(best);
        __m128 us = _mm_setr_ps(a, a + hw, a + 2.0f * hw, a + 3.0f * hw);
        __m128 ue = _mm_add_ps(us, _mm_set1_ps(hw));
        __m128 PA[4], dPA[4], PB[4], dPB[4];
        tri_bez_eval4u(C, us, PA, dPA);
        tri_bez_eval4u(C, ue, PB, dPB);
        __m128 cx = _mm_sub_ps(PB[0], PA[0]), cy = _mm_sub_ps(PB[1], PA[1]),
               cz = _mm_sub_ps(PB[2], PA[2]);
        __m128 cl2 = rlc_dot4(cx, cy, cz, cx, cy, cz);
        __m128 invcl = _mm_div_ps(ONE, _mm_max_ps(cl2, _mm_set1_ps(1e-20f)));
        __m128 hw3 = _mm_set1_ps(hw / 3.0f);
        __m128 perp2 = Z;
        for (int e = 0; e < 2; e++) {
            __m128 mxp = e ? _mm_sub_ps(PB[0], _mm_mul_ps(dPB[0], hw3))
                           : _mm_add_ps(PA[0], _mm_mul_ps(dPA[0], hw3));
            __m128 myp = e ? _mm_sub_ps(PB[1], _mm_mul_ps(dPB[1], hw3))
                           : _mm_add_ps(PA[1], _mm_mul_ps(dPA[1], hw3));
            __m128 mzp = e ? _mm_sub_ps(PB[2], _mm_mul_ps(dPB[2], hw3))
                           : _mm_add_ps(PA[2], _mm_mul_ps(dPA[2], hw3));
            __m128 wx = _mm_sub_ps(mxp, PA[0]), wy = _mm_sub_ps(myp, PA[1]),
                   wz = _mm_sub_ps(mzp, PA[2]);
            __m128 tt = _mm_mul_ps(rlc_dot4(wx, wy, wz, cx, cy, cz), invcl);
            __m128 px = _mm_sub_ps(wx, _mm_mul_ps(tt, cx));
            __m128 py = _mm_sub_ps(wy, _mm_mul_ps(tt, cy));
            __m128 pz = _mm_sub_ps(wz, _mm_mul_ps(tt, cz));
            perp2 = _mm_max_ps(perp2, rlc_dot4(px, py, pz, px, py, pz));
        }
        __m128 maxperp = _mm_sqrt_ps(perp2);
        __m128 r1w = _mm_add_ps(PA[3], _mm_mul_ps(dPA[3], hw3));
        __m128 r2w = _mm_sub_ps(PB[3], _mm_mul_ps(dPB[3], hw3));
        __m128 rmax = _mm_max_ps(_mm_max_ps(PA[3], PB[3]), _mm_max_ps(r1w, r2w));
        __m128 rcap = _mm_add_ps(maxperp, _mm_max_ps(rmax, Z));
        __m128 rc2 = _mm_mul_ps(rcap, rcap);
        /* ray (org=0) vs segment PA..PB: closest distance <= rcap, t in range */
        __m128 w0x = _mm_sub_ps(Z, PA[0]), w0y = _mm_sub_ps(Z, PA[1]),
               w0z = _mm_sub_ps(Z, PA[2]);
        __m128 bb = rlc_dot4(dirx, diry, dirz, cx, cy, cz);
        __m128 dd = rlc_dot4(dirx, diry, dirz, w0x, w0y, w0z);
        __m128 eee = rlc_dot4(cx, cy, cz, w0x, w0y, w0z);
        __m128 den = _mm_sub_ps(_mm_mul_ps(dOdO, cl2), _mm_mul_ps(bb, bb));
        __m128 par = _mm_cmple_ps(den, _mm_set1_ps(1e-20f));
        __m128 invden = _mm_div_ps(ONE, _mm_max_ps(den, _mm_set1_ps(1e-20f)));
        __m128 ss = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(dOdO, eee), _mm_mul_ps(bb, dd)), invden);
        ss = _mm_blendv_ps(ss, Z, par);
        ss = _mm_min_ps(ONE, _mm_max_ps(Z, ss));
        __m128 inva = _mm_div_ps(ONE, _mm_max_ps(dOdO, _mm_set1_ps(1e-20f)));
        __m128 tr = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(bb, ss), dd), inva);
        tr = _mm_min_ps(best4, _mm_max_ps(tmin4, tr));
        __m128 invc = _mm_div_ps(ONE, _mm_max_ps(cl2, _mm_set1_ps(1e-20f)));
        ss = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(bb, tr), eee), invc);
        ss = _mm_min_ps(ONE, _mm_max_ps(Z, ss));
        __m128 gx = _mm_sub_ps(_mm_mul_ps(tr, dirx), _mm_add_ps(PA[0], _mm_mul_ps(ss, cx)));
        __m128 gy = _mm_sub_ps(_mm_mul_ps(tr, diry), _mm_add_ps(PA[1], _mm_mul_ps(ss, cy)));
        __m128 gz = _mm_sub_ps(_mm_mul_ps(tr, dirz), _mm_add_ps(PA[2], _mm_mul_ps(ss, cz)));
        __m128 fire = _mm_cmple_ps(rlc_dot4(gx, gy, gz, gx, gy, gz), rc2);
        int mask = _mm_movemask_ps(fire);
        if (!mask) continue;

        _Alignas(16) float usa[4], uea[4], pax[4], pay[4], paz[4], par_[4],
            pbx[4], pby[4], pbz[4], pbr[4], mpa[4], r1a[4], r2a[4];
        _mm_store_ps(usa, us);
        _mm_store_ps(uea, ue);
        _mm_store_ps(pax, PA[0]); _mm_store_ps(pay, PA[1]);
        _mm_store_ps(paz, PA[2]); _mm_store_ps(par_, PA[3]);
        _mm_store_ps(pbx, PB[0]); _mm_store_ps(pby, PB[1]);
        _mm_store_ps(pbz, PB[2]); _mm_store_ps(pbr, PB[3]);
        _mm_store_ps(mpa, maxperp);
        _mm_store_ps(r1a, r1w);
        _mm_store_ps(r2a, r2w);
        int leaf = (depth + 1 >= BEZ_SIMD_DEPTH);
        while (mask) {
            int L = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            if (leaf) {
                float u0 = usa[L], u1 = uea[L], hL = u1 - u0;
                float P0p[3] = {pax[L], pay[L], paz[L]};
                float P3p[3] = {pbx[L], pby[L], pbz[L]};
                float r0c = (par_[L] > r1a[L] ? par_[L] : r1a[L]);
                float r3c = (pbr[L] > r2a[L] ? pbr[L] : r2a[L]);
                r0c = (r0c > 0.0f ? r0c : 0.0f) + mpa[L];
                r3c = (r3c > 0.0f ? r3c : 0.0f) + mpa[L];
                if (r0c > 0.0f || r3c > 0.0f) {
                    float tseg, s;
                    if (tri_cone_seed(dir, tmin, best, P0p, r0c, P3p, r3c, &tseg,
                                      &s)) {
                        float th, uh;
                        if (tri_bez_newton(cpl, dir, tmin, u0 + s * hL, tseg,
                                           best, &th, &uh)) {
                            best = th;
                            bu = uh;
                            hit = 1;
                        } else {
                            best = tseg;
                            bu = u0 + s * hL;
                            hit = 1;
                        }
                    }
                }
            } else {
                stk[sp].a = usa[L];
                stk[sp].b = uea[L];
                stk[sp].depth = depth + 1;
                sp++;
            }
        }
    }
    if (hit) {
        *t_out = best;
        *u_out = bu;
    }
    return hit;
}

/* ---- Bezier curve leaf: 4-wide broad phase over the block's 4 curves -------
 * The 4 curves share the runtime u-grid, so each sub-interval evaluates all 4
 * cubics 4-wide (SoA) and seeds them 4-wide with a bounding capsule. Only lanes
 * whose capsule the ray crosses pay the scalar Newton refinement, so misses (the
 * common BVH-leaf case) cost just the vectorized broad phase. */

static inline void tri_bez4_isect_sse(const lrt_bez4 *blk, const tri_ray_ctx *rc,
                                      const tri_sse_ctx *sc, float *best_t,
                                      float *best_u, uint32_t *best_prim) {
    const __m128 Z = _mm_setzero_ps();
    const __m128 ONE = _mm_set1_ps(1.0f);
    __m128 dirx = sc->dirx, diry = sc->diry, dirz = sc->dirz;
    __m128 orgx = sc->orgx, orgy = sc->orgy, orgz = sc->orgz;
    __m128 tmin = sc->tmin;
    __m128 best4 = _mm_set1_ps(*best_t);
    __m128 dOdO = rlc_dot4(dirx, diry, dirz, dirx, diry, dirz);

    /* control points (B0=cp0=curve(0), B3=cp3=curve(1); B1,B2 = inner CPs). */
    __m128 B0x = _mm_loadu_ps(blk->b0x), B0y = _mm_loadu_ps(blk->b0y),
           B0z = _mm_loadu_ps(blk->b0z), r0 = _mm_loadu_ps(blk->b0r);
    __m128 B1x = _mm_loadu_ps(blk->b1x), B1y = _mm_loadu_ps(blk->b1y),
           B1z = _mm_loadu_ps(blk->b1z), r1 = _mm_loadu_ps(blk->b1r);
    __m128 B2x = _mm_loadu_ps(blk->b2x), B2y = _mm_loadu_ps(blk->b2y),
           B2z = _mm_loadu_ps(blk->b2z), r2 = _mm_loadu_ps(blk->b2r);
    __m128 B3x = _mm_loadu_ps(blk->b3x), B3y = _mm_loadu_ps(blk->b3y),
           B3z = _mm_loadu_ps(blk->b3z), r3 = _mm_loadu_ps(blk->b3r);
    __m128i primi = _mm_loadu_si128((const __m128i *)(const void *)blk->prim);
    __m128 prim_ok = _mm_castsi128_ps(_mm_xor_si128(
        _mm_cmpeq_epi32(primi, _mm_set1_epi32((int)0xFFFFFFFFu)),
        _mm_set1_epi32(-1)));

    /* Conservative whole-curve bounding CAPSULE around the chord LINE (much
     * tighter than a sphere, so far fewer lanes survive to the scalar sweep).
     * Axis extended to the control-point projection range + max radius (so a
     * folded polygon / the radius-along-axis can't escape the caps); radius =
     * max perpendicular CP distance + max CP radius. Numerically robust for the
     * very short pre-subdivided arcs: the axial parameter is formed as
     * (dot(m,n) + t*dot(dir,n)) / L with UNIT n and divided by L only at the end
     * (O(dist) terms cancelling to O(L)), NOT md = dot(m,d)/L^2 (which is
     * O(dist/L) and cancels catastrophically). Fired lanes are bit-identical to
     * the scalar leaf. */
    __m128 chx = _mm_sub_ps(B3x, B0x), chy = _mm_sub_ps(B3y, B0y),
           chz = _mm_sub_ps(B3z, B0z);
    __m128 cl2 = rlc_dot4(chx, chy, chz, chx, chy, chz);
    __m128 rcpcl = _mm_div_ps(ONE, _mm_sqrt_ps(_mm_max_ps(cl2, _mm_set1_ps(1e-20f))));
    __m128 nx = _mm_mul_ps(chx, rcpcl), ny = _mm_mul_ps(chy, rcpcl),
           nz = _mm_mul_ps(chz, rcpcl); /* unit chord direction */
    __m128 pmin = Z, pmax = Z, maxperp = Z;
    __m128 maxr = _mm_max_ps(_mm_max_ps(r0, r1), _mm_max_ps(r2, r3));
    {
        __m128 cpx[4] = {B0x, B1x, B2x, B3x};
        __m128 cpy[4] = {B0y, B1y, B2y, B3y};
        __m128 cpz[4] = {B0z, B1z, B2z, B3z};
        for (int k = 0; k < 4; k++) {
            __m128 wx = _mm_sub_ps(cpx[k], B0x), wy = _mm_sub_ps(cpy[k], B0y),
                   wz = _mm_sub_ps(cpz[k], B0z);
            __m128 proj = rlc_dot4(wx, wy, wz, nx, ny, nz);
            pmin = _mm_min_ps(pmin, proj);
            pmax = _mm_max_ps(pmax, proj);
            __m128 perp2 =
                _mm_sub_ps(rlc_dot4(wx, wy, wz, wx, wy, wz), _mm_mul_ps(proj, proj));
            maxperp = _mm_max_ps(maxperp, _mm_sqrt_ps(_mm_max_ps(perp2, Z)));
        }
    }
    __m128 mr = _mm_max_ps(maxr, Z);
    __m128 rcap = _mm_add_ps(maxperp, mr);
    __m128 rc2 = _mm_mul_ps(rcap, rcap);
    pmin = _mm_sub_ps(pmin, mr); /* tube extends +-maxr along the axis */
    pmax = _mm_add_ps(pmax, mr);
    __m128 A0x = _mm_add_ps(B0x, _mm_mul_ps(pmin, nx));
    __m128 A0y = _mm_add_ps(B0y, _mm_mul_ps(pmin, ny));
    __m128 A0z = _mm_add_ps(B0z, _mm_mul_ps(pmin, nz));
    __m128 L = _mm_sub_ps(pmax, pmin);

    /* Fire if the ray (t in [tmin, best]) comes within rcap of the axis SEGMENT
     * A0..A1. Robust closest ray-segment distance (handles any orientation incl.
     * exactly parallel - the cylinder's roots/axial param degenerate there and
     * silently under-fired). E = A1 - A0 = n*L. */
    __m128 Ex = _mm_mul_ps(nx, L), Ey = _mm_mul_ps(ny, L), Ez = _mm_mul_ps(nz, L);
    __m128 w0x = _mm_sub_ps(orgx, A0x), w0y = _mm_sub_ps(orgy, A0y),
           w0z = _mm_sub_ps(orgz, A0z);
    __m128 a = dOdO;                                       /* D.D */
    __m128 b = rlc_dot4(dirx, diry, dirz, Ex, Ey, Ez);    /* D.E */
    __m128 cc = _mm_mul_ps(L, L);                          /* E.E */
    __m128 d = rlc_dot4(dirx, diry, dirz, w0x, w0y, w0z); /* D.w0 */
    __m128 ee = rlc_dot4(Ex, Ey, Ez, w0x, w0y, w0z);      /* E.w0 */
    __m128 denom = _mm_sub_ps(_mm_mul_ps(a, cc), _mm_mul_ps(b, b));
    __m128 par = _mm_cmple_ps(denom, _mm_set1_ps(1e-20f)); /* parallel-ish */
    __m128 invden = _mm_div_ps(ONE, _mm_max_ps(denom, _mm_set1_ps(1e-20f)));
    /* ss along the segment (parallel -> 0, refined by the clamp dance) */
    __m128 ss = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(a, ee), _mm_mul_ps(b, d)), invden);
    ss = _mm_blendv_ps(ss, Z, par);
    ss = _mm_min_ps(ONE, _mm_max_ps(Z, ss));
    /* tr along the ray for this ss, clamped to the live range */
    __m128 inva = _mm_div_ps(ONE, _mm_max_ps(a, _mm_set1_ps(1e-20f)));
    __m128 tr = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(b, ss), d), inva);
    tr = _mm_min_ps(best4, _mm_max_ps(tmin, tr));
    /* recompute ss for that tr */
    __m128 invc = _mm_div_ps(ONE, _mm_max_ps(cc, _mm_set1_ps(1e-20f)));
    ss = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(b, tr), ee), invc);
    ss = _mm_min_ps(ONE, _mm_max_ps(Z, ss));
    __m128 px = _mm_add_ps(orgx, _mm_mul_ps(tr, dirx));
    __m128 py = _mm_add_ps(orgy, _mm_mul_ps(tr, diry));
    __m128 pz = _mm_add_ps(orgz, _mm_mul_ps(tr, dirz));
    __m128 qx = _mm_add_ps(A0x, _mm_mul_ps(ss, Ex));
    __m128 qy = _mm_add_ps(A0y, _mm_mul_ps(ss, Ey));
    __m128 qz = _mm_add_ps(A0z, _mm_mul_ps(ss, Ez));
    __m128 gx = _mm_sub_ps(px, qx), gy = _mm_sub_ps(py, qy), gz = _mm_sub_ps(pz, qz);
    __m128 dist2 = rlc_dot4(gx, gy, gz, gx, gy, gz);
    __m128 fire = _mm_and_ps(_mm_cmple_ps(dist2, rc2), prim_ok);
    int mask = _mm_movemask_ps(fire);
    if (!mask) return;

    /* exact scalar Bezier sweep on the few surviving lanes */
    const float *src[16] = {blk->b0x, blk->b0y, blk->b0z, blk->b0r,
                            blk->b1x, blk->b1y, blk->b1z, blk->b1r,
                            blk->b2x, blk->b2y, blk->b2z, blk->b2r,
                            blk->b3x, blk->b3y, blk->b3z, blk->b3r};
    while (mask) {
        int lane = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        float cp[16];
        for (int k = 0; k < 16; k++) cp[k] = src[k][lane];
        float t, u;
        if (tri_bez_sweep_sse(cp, rc, *best_t, &t, &u)) {
            *best_t = t;
            *best_u = u;
            *best_prim = blk->prim[lane];
        }
    }
}

/* ---- Bezier curve traversal: SSE node tests, 4-wide leaves ---------------- */
static int tri_bezcurve_intersect_bvh4(const lrt_tri_scene *s,
                                       const lrt_ray *ray, lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    float best_t = ray->tmax, best_u = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref), nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_bez4 *blocks = (const lrt_bez4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++)
                tri_bez4_isect_sse(&blocks[blk0 + b], &rc, &sc, &best_t, &best_u,
                                   &best_prim);
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }
    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = 0.0f;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_bezcurve_occluded_bvh4(const lrt_tri_scene *s,
                                      const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 tmax4 = _mm_set1_ps(ray->tmax);
    const float t_max = ray->tmax;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            const lrt_bez4 *blocks = (const lrt_bez4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                float t = t_max, u = 0.0f;
                uint32_t prim = LRT_TRI_NO_HIT;
                tri_bez4_isect_sse(&blocks[blk0 + b], &rc, &sc, &t, &u, &prim);
                if (prim != LRT_TRI_NO_HIT) return 1;
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* ---- Sphere (analytic) traversal: SSE node tests + 4-wide ray-sphere ----- */

/* 4-wide ray-sphere over one sphere block; updates best hit in place. dir is
 * not normalized so t stays in |dir| units. */
static inline void tri_block_isect_sph_sse(const lrt_sph4 *blk,
                                           const tri_sse_ctx *sc,
                                           const tri_ray_ctx *rc, float *best_t,
                                           float *best_u, float *best_v,
                                           uint32_t *best_prim) {
    __m128 cx = _mm_load_ps(blk->cx), cy = _mm_load_ps(blk->cy),
           cz = _mm_load_ps(blk->cz), r = _mm_load_ps(blk->r);
    __m128 ocx = _mm_sub_ps(sc->orgx, cx);
    __m128 ocy = _mm_sub_ps(sc->orgy, cy);
    __m128 ocz = _mm_sub_ps(sc->orgz, cz);
    __m128 a = _mm_add_ps(_mm_add_ps(_mm_mul_ps(sc->dirx, sc->dirx),
                                     _mm_mul_ps(sc->diry, sc->diry)),
                          _mm_mul_ps(sc->dirz, sc->dirz));
    __m128 b = _mm_add_ps(_mm_add_ps(_mm_mul_ps(ocx, sc->dirx),
                                     _mm_mul_ps(ocy, sc->diry)),
                          _mm_mul_ps(ocz, sc->dirz));
    __m128 c = _mm_sub_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(ocx, ocx),
                                                _mm_mul_ps(ocy, ocy)),
                                     _mm_mul_ps(ocz, ocz)),
                          _mm_mul_ps(r, r));
    __m128 disc = _mm_sub_ps(_mm_mul_ps(b, b), _mm_mul_ps(a, c));
    __m128 valid = _mm_and_ps(_mm_cmpgt_ps(r, _mm_setzero_ps()),
                              _mm_cmpge_ps(disc, _mm_setzero_ps()));
    if (!_mm_movemask_ps(valid)) return;
    __m128 sq = _mm_sqrt_ps(_mm_max_ps(disc, _mm_setzero_ps()));
    __m128 inv_a = _mm_div_ps(_mm_set1_ps(1.0f), a);
    __m128 negb = _mm_sub_ps(_mm_setzero_ps(), b);
    __m128 t0 = _mm_mul_ps(_mm_sub_ps(negb, sq), inv_a);
    __m128 t1 = _mm_mul_ps(_mm_add_ps(negb, sq), inv_a);
    __m128 bt = _mm_set1_ps(*best_t);
    __m128 in0 = _mm_and_ps(_mm_cmpge_ps(t0, sc->tmin), _mm_cmplt_ps(t0, bt));
    __m128 in1 = _mm_and_ps(_mm_cmpge_ps(t1, sc->tmin), _mm_cmplt_ps(t1, bt));
    __m128 ok = _mm_and_ps(valid, _mm_or_ps(in0, in1));
    if (!_mm_movemask_ps(ok)) return;
    __m128 t = _mm_blendv_ps(t1, t0, in0); /* near root if it qualifies */
    __m128 tsel = _mm_blendv_ps(_mm_set1_ps(TRI_INF_F), t, ok);
    float ta[4];
    _mm_storeu_ps(ta, tsel);
    int mask = _mm_movemask_ps(_mm_cmplt_ps(tsel, bt));
    while (mask) {
        int lane = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        if (ta[lane] < *best_t) {
            *best_t = ta[lane];
            *best_prim = blk->prim[lane];
            float rr = blk->r[lane];
            float nx = (rc->org[0] - blk->cx[lane] + ta[lane] * rc->dir[0]) / rr;
            float ny = (rc->org[1] - blk->cy[lane] + ta[lane] * rc->dir[1]) / rr;
            float nz = (rc->org[2] - blk->cz[lane] + ta[lane] * rc->dir[2]) / rr;
            float cl = ny < -1.0f ? -1.0f : (ny > 1.0f ? 1.0f : ny);
            *best_u = atan2f(nz, nx) * TRI_INV_2PI + 0.5f;
            *best_v = acosf(cl) * TRI_INV_PI;
        }
    }
}

static inline int tri_block_occluded_sph_sse(const lrt_sph4 *blk,
                                             const tri_sse_ctx *sc,
                                             __m128 tmax) {
    __m128 cx = _mm_load_ps(blk->cx), cy = _mm_load_ps(blk->cy),
           cz = _mm_load_ps(blk->cz), r = _mm_load_ps(blk->r);
    __m128 ocx = _mm_sub_ps(sc->orgx, cx);
    __m128 ocy = _mm_sub_ps(sc->orgy, cy);
    __m128 ocz = _mm_sub_ps(sc->orgz, cz);
    __m128 a = _mm_add_ps(_mm_add_ps(_mm_mul_ps(sc->dirx, sc->dirx),
                                     _mm_mul_ps(sc->diry, sc->diry)),
                          _mm_mul_ps(sc->dirz, sc->dirz));
    __m128 b = _mm_add_ps(_mm_add_ps(_mm_mul_ps(ocx, sc->dirx),
                                     _mm_mul_ps(ocy, sc->diry)),
                          _mm_mul_ps(ocz, sc->dirz));
    __m128 c = _mm_sub_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(ocx, ocx),
                                                _mm_mul_ps(ocy, ocy)),
                                     _mm_mul_ps(ocz, ocz)),
                          _mm_mul_ps(r, r));
    __m128 disc = _mm_sub_ps(_mm_mul_ps(b, b), _mm_mul_ps(a, c));
    __m128 valid = _mm_and_ps(_mm_cmpgt_ps(r, _mm_setzero_ps()),
                              _mm_cmpge_ps(disc, _mm_setzero_ps()));
    if (!_mm_movemask_ps(valid)) return 0;
    __m128 sq = _mm_sqrt_ps(_mm_max_ps(disc, _mm_setzero_ps()));
    __m128 inv_a = _mm_div_ps(_mm_set1_ps(1.0f), a);
    __m128 negb = _mm_sub_ps(_mm_setzero_ps(), b);
    __m128 t0 = _mm_mul_ps(_mm_sub_ps(negb, sq), inv_a);
    __m128 t1 = _mm_mul_ps(_mm_add_ps(negb, sq), inv_a);
    __m128 in0 = _mm_and_ps(_mm_cmpge_ps(t0, sc->tmin), _mm_cmple_ps(t0, tmax));
    __m128 in1 = _mm_and_ps(_mm_cmpge_ps(t1, sc->tmin), _mm_cmple_ps(t1, tmax));
    return _mm_movemask_ps(_mm_and_ps(valid, _mm_or_ps(in0, in1))) != 0;
}

static int tri_sphere_intersect_bvh4(const lrt_tri_scene *s, const lrt_ray *ray,
                                     lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    float best_t = ray->tmax, best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;
    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_sph4 *blocks = (const lrt_sph4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++)
                tri_block_isect_sph_sse(&blocks[blk0 + b], &sc, &rc, &best_t,
                                        &best_u, &best_v, &best_prim);
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }
    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_sphere_occluded_bvh4(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 tmax4 = _mm_set1_ps(ray->tmax);
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref);
            uint32_t nblk = TRI_REF_NBLOCKS(ref);
            const lrt_sph4 *blocks = (const lrt_sph4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++)
                if (tri_block_occluded_sph_sse(&blocks[blk0 + b], &sc, tmax4))
                    return 1;
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* ---- User (custom) geometry traversal: SSE node + box pretest + callback -- */

/* 4-wide AABB test of one user leaf block; returns lane mask of boxes the ray
 * pierces within t_best (same math as tri_bvh4_slab_sse on the leaf arrays). */
static inline int tri_user_box_mask_sse(const lrt_user4 *blk,
                                        const tri_sse_ctx *sc, __m128 t_best) {
#if LRT_TRI_HAS_AVX2
    __m128 tlx = _mm_fmsub_ps(_mm_load_ps(blk->lo_x), sc->invdx, sc->oinvx);
    __m128 thx = _mm_fmsub_ps(_mm_load_ps(blk->hi_x), sc->invdx, sc->oinvx);
    __m128 tly = _mm_fmsub_ps(_mm_load_ps(blk->lo_y), sc->invdy, sc->oinvy);
    __m128 thy = _mm_fmsub_ps(_mm_load_ps(blk->hi_y), sc->invdy, sc->oinvy);
    __m128 tlz = _mm_fmsub_ps(_mm_load_ps(blk->lo_z), sc->invdz, sc->oinvz);
    __m128 thz = _mm_fmsub_ps(_mm_load_ps(blk->hi_z), sc->invdz, sc->oinvz);
#else
    __m128 tlx =
        _mm_mul_ps(_mm_sub_ps(_mm_load_ps(blk->lo_x), sc->orgx), sc->invdx);
    __m128 thx =
        _mm_mul_ps(_mm_sub_ps(_mm_load_ps(blk->hi_x), sc->orgx), sc->invdx);
    __m128 tly =
        _mm_mul_ps(_mm_sub_ps(_mm_load_ps(blk->lo_y), sc->orgy), sc->invdy);
    __m128 thy =
        _mm_mul_ps(_mm_sub_ps(_mm_load_ps(blk->hi_y), sc->orgy), sc->invdy);
    __m128 tlz =
        _mm_mul_ps(_mm_sub_ps(_mm_load_ps(blk->lo_z), sc->orgz), sc->invdz);
    __m128 thz =
        _mm_mul_ps(_mm_sub_ps(_mm_load_ps(blk->hi_z), sc->orgz), sc->invdz);
#endif
    __m128 tnear = _mm_max_ps(
        _mm_max_ps(_mm_min_ps(tlx, thx), _mm_min_ps(tly, thy)),
        _mm_max_ps(_mm_min_ps(tlz, thz), sc->tmin));
    __m128 tfar = _mm_min_ps(
        _mm_min_ps(_mm_max_ps(tlx, thx), _mm_max_ps(tly, thy)),
        _mm_min_ps(_mm_max_ps(tlz, thz), t_best));
    return _mm_movemask_ps(_mm_cmple_ps(tnear, tfar));
}

static int tri_user_intersect_bvh4(const lrt_tri_scene *s, const lrt_ray *ray,
                                   lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    float best_t = ray->tmax, best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;
    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_user4 *blocks = (const lrt_user4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                const lrt_user4 *blk = &blocks[blk0 + b];
                int m = tri_user_box_mask_sse(blk, &sc, _mm_set1_ps(best_t));
                while (m) {
                    int lane = __builtin_ctz((unsigned)m);
                    m &= m - 1;
                    uint32_t pid = blk->prim[lane];
                    if (pid == LRT_TRI_NO_HIT) continue;
                    lrt_ray q = {{rc.org[0], rc.org[1], rc.org[2]},
                                 ray->tmin,
                                 {rc.dir[0], rc.dir[1], rc.dir[2]},
                                 best_t};
                    float t, u, v;
                    if (s->user_isect(&q, pid, s->user_ptr, &t, &u, &v) &&
                        t >= ray->tmin && t < best_t) {
                        best_t = t;
                        best_u = u;
                        best_v = v;
                        best_prim = pid;
                    }
                }
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }
    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_user_occluded_bvh4(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 tmax4 = _mm_set1_ps(ray->tmax);
    const float t_max = ray->tmax;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref);
            uint32_t nblk = TRI_REF_NBLOCKS(ref);
            const lrt_user4 *blocks = (const lrt_user4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                const lrt_user4 *blk = &blocks[blk0 + b];
                int m = tri_user_box_mask_sse(blk, &sc, tmax4);
                while (m) {
                    int lane = __builtin_ctz((unsigned)m);
                    m &= m - 1;
                    uint32_t pid = blk->prim[lane];
                    if (pid == LRT_TRI_NO_HIT) continue;
                    lrt_ray q = {{rc.org[0], rc.org[1], rc.org[2]},
                                 ray->tmin,
                                 {rc.dir[0], rc.dir[1], rc.dir[2]},
                                 t_max};
                    if (s->user_occ) {
                        if (s->user_occ(&q, pid, s->user_ptr)) return 1;
                    } else {
                        float t, u, v;
                        if (s->user_isect(&q, pid, s->user_ptr, &t, &u, &v) &&
                            t >= ray->tmin && t <= t_max)
                            return 1;
                    }
                }
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* ---- Quantized-triangle traversal: SSE node tests + scalar decoded leaf -- */
static int tri_qtri_intersect_bvh4(const lrt_tri_scene *s, const lrt_ray *ray,
                                   lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 og[3], sg[3];
    if (s->qfmt == LRT_QTRI_Q16)
        for (int a = 0; a < 3; a++) {
            og[a] = _mm_set1_ps(s->qgrid_org[a]);
            sg[a] = _mm_set1_ps(s->qgrid_scale[a]);
        }
    float best_t = ray->tmax, best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;
    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref), nblk = TRI_REF_NBLOCKS(e.ref);
            for (uint32_t b = 0; b < nblk; b++) {
                void *blk = tri_block_ptr(s->blocks, blk0 + b, s->block_stride);
                if (s->qfmt == LRT_QTRI_Q8)
                    tri_qtri8_isect_sse((const lrt_qtri8 *)blk, &sc, &best_t,
                                        &best_u, &best_v, &best_prim);
                else if (s->qfmt == LRT_QTRI_Q16)
                    tri_qtri16_isect_sse((const lrt_qtri16 *)blk, &sc, og, sg,
                                         &best_t, &best_u, &best_v, &best_prim);
                else
                    tri_qtri_block_isect(s, blk, &rc, &best_t, &best_u, &best_v,
                                         &best_prim);
            }
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, _mm_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint8_t perm = n->perm[rc.octant];
        for (int p = 3; p >= 0; p--) {
            int slot = (perm >> (2 * p)) & 3;
            if (!(mask & (1 << slot))) continue;
            tri_prefetch_ref(s, n->child[slot], 4);
            stack[sp].ref = n->child[slot];
            stack[sp].tnear = tnear[slot];
            sp++;
        }
    }
    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_qtri_occluded_bvh4(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_sse_ctx sc;
    tri_sse_setup(&rc, &sc);
    __m128 tmax4 = _mm_set1_ps(ray->tmax);
    const float t_max = ray->tmax;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            for (uint32_t b = 0; b < nblk; b++)
                if (tri_qtri_block_occluded(
                        s, tri_block_ptr(s->blocks, blk0 + b, s->block_stride),
                        &rc, t_max))
                    return 1;
            continue;
        }
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &sc, tmax4, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* ---- Software-pipelined batch traversal (BVH4/SSE) -----------------------
 *
 * Incoherent single-ray traversal is bound by the latency of one dependent
 * node fetch per step. Keeping TRI_PIPE_WIDTH rays in flight per thread and
 * advancing each by one node/leaf visit per turn overlaps one ray's cache
 * miss with the others' compute, like a software MIMD pipeline. Results are
 * identical to the single-ray kernel (each ray runs the same algorithm).
 */
/* 8 rays in flight per thread: measured peak on Zen 1 (K sweep 2..32 gave
 * 1.55/1.91/2.19/2.25/2.19/2.00 Mrays/s for K=2/4/8/12/16/32 at 710k tris). */
#ifndef TRI_PIPE_WIDTH
#define TRI_PIPE_WIDTH 8
#endif

typedef struct tri_pipe4_state {
    tri_ray_ctx rc;
    tri_sse_ctx sc;
    float best_t, best_u, best_v;
    uint32_t best_prim;
    lrt_hit *hit;
    int sp;
    tri_stack_entry stack[TRI_STACK_SIZE];
} tri_pipe4_state;

static inline void tri_pipe4_init(const lrt_tri_scene *s, tri_pipe4_state *st,
                                  const lrt_ray *ray, lrt_hit *hit) {
    (void)s;
    tri_ray_setup(ray, &st->rc);
    tri_sse_setup(&st->rc, &st->sc);
    st->best_t = ray->tmax;
    st->best_u = 0.0f;
    st->best_v = 0.0f;
    st->best_prim = LRT_TRI_NO_HIT;
    st->hit = hit;
    st->stack[0].ref = s->root;
    st->stack[0].tnear = st->rc.tmin;
    st->sp = 1;
}

/* Advance one node or leaf visit; returns 0 once the ray is finished. */
static inline int tri_pipe4_step(const lrt_tri_scene *s, tri_pipe4_state *st) {
    while (st->sp > 0) {
        tri_stack_entry e = st->stack[--st->sp];
        if (e.tnear >= st->best_t) continue; /* culled pops cost no turn */
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_tri4 *blocks = (const lrt_tri4 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                tri_block_isect_sse(&blocks[blk0 + b], &st->sc, &st->best_t,
                                    &st->best_u, &st->best_v, &st->best_prim);
            }
            return 1;
        }

        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(e.ref)];
        _Alignas(16) float tnear[4];
        int mask = tri_bvh4_slab_sse(n, &st->sc, _mm_set1_ps(st->best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        /* Exact tnear ordering: when latency-bound, the insertion sort hides
         * behind cache misses and the tighter order saves whole node visits
         * (measured better than the octant table here, unlike the coherent
         * single-ray kernel). */
        uint32_t hit_ref[4];
        float hit_tn[4];
        int nhit = 0;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 4);
            int j = nhit++;
            while (j > 0 && hit_tn[j - 1] > tnear[i]) {
                hit_tn[j] = hit_tn[j - 1];
                hit_ref[j] = hit_ref[j - 1];
                j--;
            }
            hit_tn[j] = tnear[i];
            hit_ref[j] = n->child[i];
        }
        for (int i = nhit - 1; i >= 0; i--) {
            st->stack[st->sp].ref = hit_ref[i];
            st->stack[st->sp].tnear = hit_tn[i];
            st->sp++;
        }
        return 1;
    }
    return 0;
}

static inline void tri_pipe4_finish(tri_pipe4_state *st) {
    st->hit->t = st->best_prim != LRT_TRI_NO_HIT ? st->best_t : 0.0f;
    st->hit->u = st->best_u;
    st->hit->v = st->best_v;
    st->hit->prim_id = st->best_prim;
}

static void tri_intersect1N_bvh4_sse(const lrt_tri_scene *s,
                                     const lrt_ray *rays, lrt_hit *hits,
                                     size_t n) {
    if (n < TRI_PIPE_WIDTH * 2u) {
        for (size_t i = 0; i < n; i++) {
            tri_intersect_bvh4_sse(s, &rays[i], &hits[i]);
        }
        return;
    }
    tri_pipe4_state st[TRI_PIPE_WIDTH]; /* ~66KB of thread stack */
    size_t next = 0;
    unsigned alive = 0;
    for (int k = 0; k < TRI_PIPE_WIDTH; k++) {
        tri_pipe4_init(s, &st[k], &rays[next], &hits[next]);
        next++;
        alive |= 1u << k;
    }
    while (alive) {
        for (int k = 0; k < TRI_PIPE_WIDTH; k++) {
            if (!(alive & (1u << k))) continue;
            if (!tri_pipe4_step(s, &st[k])) {
                tri_pipe4_finish(&st[k]);
                if (next < n) {
                    tri_pipe4_init(s, &st[k], &rays[next], &hits[next]);
                    next++;
                } else {
                    alive &= ~(1u << k);
                }
            }
        }
    }
}

/* Pipelined any-hit: same scheme, simpler state (no best-hit tracking, a
 * ref-only stack, terminate the ray on its first confirmed hit). */
typedef struct tri_pipeocc4_state {
    tri_ray_ctx rc;
    tri_sse_ctx sc;
    __m128 tmax4;
    uint8_t *out;
    int sp;
    uint32_t stack[TRI_STACK_SIZE];
} tri_pipeocc4_state;

static inline void tri_pipeocc4_init(const lrt_tri_scene *s,
                                     tri_pipeocc4_state *st,
                                     const lrt_ray *ray, uint8_t *out) {
    tri_ray_setup(ray, &st->rc);
    tri_sse_setup(&st->rc, &st->sc);
    st->tmax4 = _mm_set1_ps(ray->tmax);
    st->out = out;
    st->stack[0] = s->root;
    st->sp = 1;
}

/* Returns 0 when the ray is finished (*st->out already written). */
static inline int tri_pipeocc4_step(const lrt_tri_scene *s,
                                    tri_pipeocc4_state *st) {
    if (st->sp <= 0) {
        *st->out = 0;
        return 0;
    }
    uint32_t ref = st->stack[--st->sp];
    if (TRI_REF_IS_LEAF(ref)) {
        uint32_t blk0 = TRI_REF_BLOCK(ref);
        uint32_t nblk = TRI_REF_NBLOCKS(ref);
        const lrt_tri4 *blocks = (const lrt_tri4 *)s->blocks;
        for (uint32_t b = 0; b < nblk; b++) {
            if (tri_block_occluded_sse(&blocks[blk0 + b], &st->sc, st->tmax4)) {
                *st->out = 1;
                return 0;
            }
        }
        return 1;
    }
    const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(ref)];
    _Alignas(16) float tnear[4];
    int mask = tri_bvh4_slab_sse(n, &st->sc, st->tmax4, tnear);
    mask &= (1 << n->nchildren) - 1;
    while (mask) {
        int i = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        tri_prefetch_ref(s, n->child[i], 4);
        st->stack[st->sp++] = n->child[i];
    }
    return 1;
}

static void tri_occluded1N_bvh4_sse(const lrt_tri_scene *s,
                                    const lrt_ray *rays, uint8_t *occluded,
                                    size_t n) {
    if (n < TRI_PIPE_WIDTH * 2u) {
        for (size_t i = 0; i < n; i++) {
            occluded[i] = (uint8_t)tri_occluded_bvh4_sse(s, &rays[i]);
        }
        return;
    }
    tri_pipeocc4_state st[TRI_PIPE_WIDTH];
    size_t next = 0;
    unsigned alive = 0;
    for (int k = 0; k < TRI_PIPE_WIDTH; k++) {
        tri_pipeocc4_init(s, &st[k], &rays[next], &occluded[next]);
        next++;
        alive |= 1u << k;
    }
    while (alive) {
        for (int k = 0; k < TRI_PIPE_WIDTH; k++) {
            if (!(alive & (1u << k))) continue;
            if (!tri_pipeocc4_step(s, &st[k])) {
                if (next < n) {
                    tri_pipeocc4_init(s, &st[k], &rays[next], &occluded[next]);
                    next++;
                } else {
                    alive &= ~(1u << k);
                }
            }
        }
    }
}

#endif /* LRT_TRI_HAS_SSE4 */

/* ------------------------------------------------------------------------- */
/* AVX2 kernels (BVH8). Leaf blocks remain 4-wide SSE.                       */
/* ------------------------------------------------------------------------- */
#if LRT_TRI_HAS_AVX2

typedef struct tri_avx_ctx {
    __m256 orgx, orgy, orgz;
    __m256 invdx, invdy, invdz;
    __m256 oinvx, oinvy, oinvz;
    __m256 dirx, diry, dirz;
    __m256 tmin;
} tri_avx_ctx;

static inline void tri_avx_setup(const tri_ray_ctx *rc, tri_avx_ctx *ac) {
    ac->orgx = _mm256_set1_ps(rc->org[0]);
    ac->orgy = _mm256_set1_ps(rc->org[1]);
    ac->orgz = _mm256_set1_ps(rc->org[2]);
    ac->invdx = _mm256_set1_ps(rc->invd[0]);
    ac->invdy = _mm256_set1_ps(rc->invd[1]);
    ac->invdz = _mm256_set1_ps(rc->invd[2]);
    ac->oinvx = _mm256_set1_ps(rc->oinv[0]);
    ac->oinvy = _mm256_set1_ps(rc->oinv[1]);
    ac->oinvz = _mm256_set1_ps(rc->oinv[2]);
    ac->dirx = _mm256_set1_ps(rc->dir[0]);
    ac->diry = _mm256_set1_ps(rc->dir[1]);
    ac->dirz = _mm256_set1_ps(rc->dir[2]);
    ac->tmin = _mm256_set1_ps(rc->tmin);
}

/* 8-wide SoA Moller-Trumbore over one lrt_tri8 block. Updates best in place. */
static inline void tri_block_isect_avx(const lrt_tri8 *blk,
                                       const tri_avx_ctx *ac, float *best_t,
                                       float *best_u, float *best_v,
                                       uint32_t *best_prim) {
    __m256 e1x = _mm256_load_ps(blk->e1x), e1y = _mm256_load_ps(blk->e1y),
           e1z = _mm256_load_ps(blk->e1z);
    __m256 e2x = _mm256_load_ps(blk->e2x), e2y = _mm256_load_ps(blk->e2y),
           e2z = _mm256_load_ps(blk->e2z);

    __m256 px = _mm256_fmsub_ps(ac->diry, e2z, _mm256_mul_ps(ac->dirz, e2y));
    __m256 py = _mm256_fmsub_ps(ac->dirz, e2x, _mm256_mul_ps(ac->dirx, e2z));
    __m256 pz = _mm256_fmsub_ps(ac->dirx, e2y, _mm256_mul_ps(ac->diry, e2x));

    __m256 det = _mm256_fmadd_ps(
        e1x, px, _mm256_fmadd_ps(e1y, py, _mm256_mul_ps(e1z, pz)));
    __m256 valid = tri_isect_det_parallel8(e1x, e1y, e1z, e2x, e2y, e2z, det);
    if (!_mm256_movemask_ps(valid)) return;

    __m256 inv_det = _mm256_div_ps(_mm256_set1_ps(1.0f), det);

    __m256 tvx = _mm256_sub_ps(ac->orgx, _mm256_load_ps(blk->v0x));
    __m256 tvy = _mm256_sub_ps(ac->orgy, _mm256_load_ps(blk->v0y));
    __m256 tvz = _mm256_sub_ps(ac->orgz, _mm256_load_ps(blk->v0z));

    __m256 u = _mm256_mul_ps(
        _mm256_fmadd_ps(tvx, px, _mm256_fmadd_ps(tvy, py, _mm256_mul_ps(tvz, pz))),
        inv_det);
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(u, _mm256_setzero_ps(), _CMP_GE_OQ));
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(u, _mm256_set1_ps(1.0f), _CMP_LE_OQ));
    if (!_mm256_movemask_ps(valid)) return;

    __m256 qx = _mm256_fmsub_ps(tvy, e1z, _mm256_mul_ps(tvz, e1y));
    __m256 qy = _mm256_fmsub_ps(tvz, e1x, _mm256_mul_ps(tvx, e1z));
    __m256 qz = _mm256_fmsub_ps(tvx, e1y, _mm256_mul_ps(tvy, e1x));

    __m256 v = _mm256_mul_ps(
        _mm256_fmadd_ps(ac->dirx, qx,
                        _mm256_fmadd_ps(ac->diry, qy, _mm256_mul_ps(ac->dirz, qz))),
        inv_det);
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(v, _mm256_setzero_ps(), _CMP_GE_OQ));
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(_mm256_add_ps(u, v),
                                               _mm256_set1_ps(1.0f), _CMP_LE_OQ));
    if (!_mm256_movemask_ps(valid)) return;

    __m256 t = _mm256_mul_ps(
        _mm256_fmadd_ps(e2x, qx, _mm256_fmadd_ps(e2y, qy, _mm256_mul_ps(e2z, qz))),
        inv_det);
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(t, ac->tmin, _CMP_GE_OQ));
    valid = _mm256_and_ps(valid,
                          _mm256_cmp_ps(t, _mm256_set1_ps(*best_t), _CMP_LT_OQ));
    int mask = _mm256_movemask_ps(valid);
    if (!mask) return;

    _Alignas(32) float ta[8], ua[8], va[8];
    _mm256_store_ps(ta, t);
    _mm256_store_ps(ua, u);
    _mm256_store_ps(va, v);
    while (mask) {
        int lane = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        if (ta[lane] < *best_t) {
            *best_t = ta[lane];
            *best_u = ua[lane];
            *best_v = va[lane];
            *best_prim = blk->prim_id[lane];
        }
    }
}

/* Any-hit variant. */
static inline int tri_block_occluded_avx(const lrt_tri8 *blk,
                                         const tri_avx_ctx *ac, __m256 tmax) {
    __m256 e1x = _mm256_load_ps(blk->e1x), e1y = _mm256_load_ps(blk->e1y),
           e1z = _mm256_load_ps(blk->e1z);
    __m256 e2x = _mm256_load_ps(blk->e2x), e2y = _mm256_load_ps(blk->e2y),
           e2z = _mm256_load_ps(blk->e2z);
    __m256 px = _mm256_fmsub_ps(ac->diry, e2z, _mm256_mul_ps(ac->dirz, e2y));
    __m256 py = _mm256_fmsub_ps(ac->dirz, e2x, _mm256_mul_ps(ac->dirx, e2z));
    __m256 pz = _mm256_fmsub_ps(ac->dirx, e2y, _mm256_mul_ps(ac->diry, e2x));
    __m256 det = _mm256_fmadd_ps(
        e1x, px, _mm256_fmadd_ps(e1y, py, _mm256_mul_ps(e1z, pz)));
    __m256 valid = tri_isect_det_parallel8(e1x, e1y, e1z, e2x, e2y, e2z, det);
    if (!_mm256_movemask_ps(valid)) return 0;
    __m256 inv_det = _mm256_div_ps(_mm256_set1_ps(1.0f), det);
    __m256 tvx = _mm256_sub_ps(ac->orgx, _mm256_load_ps(blk->v0x));
    __m256 tvy = _mm256_sub_ps(ac->orgy, _mm256_load_ps(blk->v0y));
    __m256 tvz = _mm256_sub_ps(ac->orgz, _mm256_load_ps(blk->v0z));
    __m256 u = _mm256_mul_ps(
        _mm256_fmadd_ps(tvx, px, _mm256_fmadd_ps(tvy, py, _mm256_mul_ps(tvz, pz))),
        inv_det);
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(u, _mm256_setzero_ps(), _CMP_GE_OQ));
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(u, _mm256_set1_ps(1.0f), _CMP_LE_OQ));
    if (!_mm256_movemask_ps(valid)) return 0;
    __m256 qx = _mm256_fmsub_ps(tvy, e1z, _mm256_mul_ps(tvz, e1y));
    __m256 qy = _mm256_fmsub_ps(tvz, e1x, _mm256_mul_ps(tvx, e1z));
    __m256 qz = _mm256_fmsub_ps(tvx, e1y, _mm256_mul_ps(tvy, e1x));
    __m256 v = _mm256_mul_ps(
        _mm256_fmadd_ps(ac->dirx, qx,
                        _mm256_fmadd_ps(ac->diry, qy, _mm256_mul_ps(ac->dirz, qz))),
        inv_det);
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(v, _mm256_setzero_ps(), _CMP_GE_OQ));
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(_mm256_add_ps(u, v),
                                               _mm256_set1_ps(1.0f), _CMP_LE_OQ));
    if (!_mm256_movemask_ps(valid)) return 0;
    __m256 t = _mm256_mul_ps(
        _mm256_fmadd_ps(e2x, qx, _mm256_fmadd_ps(e2y, qy, _mm256_mul_ps(e2z, qz))),
        inv_det);
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(t, ac->tmin, _CMP_GE_OQ));
    valid = _mm256_and_ps(valid, _mm256_cmp_ps(t, tmax, _CMP_LE_OQ));
    return _mm256_movemask_ps(valid) != 0;
}

static inline int tri_bvh8_slab_avx(const lrt_bvh8_node *n,
                                    const tri_avx_ctx *ac, __m256 t_best,
                                    float *tnear_out) {
    __m256 tlx = _mm256_fmsub_ps(_mm256_load_ps(n->lo_x), ac->invdx, ac->oinvx);
    __m256 thx = _mm256_fmsub_ps(_mm256_load_ps(n->hi_x), ac->invdx, ac->oinvx);
    __m256 tly = _mm256_fmsub_ps(_mm256_load_ps(n->lo_y), ac->invdy, ac->oinvy);
    __m256 thy = _mm256_fmsub_ps(_mm256_load_ps(n->hi_y), ac->invdy, ac->oinvy);
    __m256 tlz = _mm256_fmsub_ps(_mm256_load_ps(n->lo_z), ac->invdz, ac->oinvz);
    __m256 thz = _mm256_fmsub_ps(_mm256_load_ps(n->hi_z), ac->invdz, ac->oinvz);
    __m256 tnear = _mm256_max_ps(
        _mm256_max_ps(_mm256_min_ps(tlx, thx), _mm256_min_ps(tly, thy)),
        _mm256_max_ps(_mm256_min_ps(tlz, thz), ac->tmin));
    __m256 tfar = _mm256_min_ps(
        _mm256_min_ps(_mm256_max_ps(tlx, thx), _mm256_max_ps(tly, thy)),
        _mm256_min_ps(_mm256_max_ps(tlz, thz), t_best));
    _mm256_store_ps(tnear_out, tnear);
    return _mm256_movemask_ps(_mm256_cmp_ps(tnear, tfar, _CMP_LE_OQ));
}

static int tri_intersect_bvh8_avx2(const lrt_tri_scene *s, const lrt_ray *ray,
                                   lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_avx_ctx ac;
    tri_avx_setup(&rc, &ac);

    float best_t = ray->tmax;
    float best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;

    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_tri8 *blocks = (const lrt_tri8 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                tri_block_isect_avx(&blocks[blk0 + b], &ac, &best_t, &best_u,
                                    &best_v, &best_prim);
            }
            continue;
        }

        const lrt_bvh8_node *n = &s->nodes8[TRI_REF_NODE(e.ref)];
        _Alignas(32) float tnear[8];
        int mask = tri_bvh8_slab_avx(n, &ac, _mm256_set1_ps(best_t), tnear);
        mask &= (1 << n->nchildren) - 1;
        uint32_t hit_ref[8];
        float hit_tn[8];
        int nhit = 0;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 8);
            int j = nhit++;
            while (j > 0 && hit_tn[j - 1] > tnear[i]) {
                hit_tn[j] = hit_tn[j - 1];
                hit_ref[j] = hit_ref[j - 1];
                j--;
            }
            hit_tn[j] = tnear[i];
            hit_ref[j] = n->child[i];
        }
        for (int i = nhit - 1; i >= 0; i--) {
            stack[sp].ref = hit_ref[i];
            stack[sp].tnear = hit_tn[i];
            sp++;
        }
    }

    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_occluded_bvh8_avx2(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_avx_ctx ac;
    tri_avx_setup(&rc, &ac);
    __m256 tmax8 = _mm256_set1_ps(ray->tmax);

    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;

    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref);
            uint32_t nblk = TRI_REF_NBLOCKS(ref);
            const lrt_tri8 *blocks = (const lrt_tri8 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                if (tri_block_occluded_avx(&blocks[blk0 + b], &ac, tmax8)) {
                    return 1;
                }
            }
            continue;
        }
        const lrt_bvh8_node *n = &s->nodes8[TRI_REF_NODE(ref)];
        _Alignas(32) float tnear[8];
        int mask = tri_bvh8_slab_avx(n, &ac, tmax8, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 8);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* Quantized-node slab: decode 8-bit child bounds on the fly. With
 * s = scale*invd and b = (org_node - org_ray)*invd per axis, each plane is
 * t = q * s + b (one cvt + fma per plane). invd is clamped to +/-1e18 at ray
 * setup, so s and b stay finite and no 0*inf NaN can appear. */
static inline int tri_bvh8q_slab_avx(const lrt_bvh8q_node *n,
                                     const tri_ray_ctx *rc, __m256 tmin8,
                                     __m256 t_best, float *tnear_out) {
    __m256 sx = _mm256_set1_ps(n->scale[0] * rc->invd[0]);
    __m256 sy = _mm256_set1_ps(n->scale[1] * rc->invd[1]);
    __m256 sz = _mm256_set1_ps(n->scale[2] * rc->invd[2]);
    __m256 bx = _mm256_set1_ps((n->org[0] - rc->org[0]) * rc->invd[0]);
    __m256 by = _mm256_set1_ps((n->org[1] - rc->org[1]) * rc->invd[1]);
    __m256 bz = _mm256_set1_ps((n->org[2] - rc->org[2]) * rc->invd[2]);

#define TRI_Q8_LOAD(arr) \
    _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32( \
        _mm_loadl_epi64((const __m128i *)(const void *)(arr))))
    __m256 tlx = _mm256_fmadd_ps(TRI_Q8_LOAD(n->qlo_x), sx, bx);
    __m256 thx = _mm256_fmadd_ps(TRI_Q8_LOAD(n->qhi_x), sx, bx);
    __m256 tly = _mm256_fmadd_ps(TRI_Q8_LOAD(n->qlo_y), sy, by);
    __m256 thy = _mm256_fmadd_ps(TRI_Q8_LOAD(n->qhi_y), sy, by);
    __m256 tlz = _mm256_fmadd_ps(TRI_Q8_LOAD(n->qlo_z), sz, bz);
    __m256 thz = _mm256_fmadd_ps(TRI_Q8_LOAD(n->qhi_z), sz, bz);
#undef TRI_Q8_LOAD

    __m256 tnear = _mm256_max_ps(
        _mm256_max_ps(_mm256_min_ps(tlx, thx), _mm256_min_ps(tly, thy)),
        _mm256_max_ps(_mm256_min_ps(tlz, thz), tmin8));
    __m256 tfar = _mm256_min_ps(
        _mm256_min_ps(_mm256_max_ps(tlx, thx), _mm256_max_ps(tly, thy)),
        _mm256_min_ps(_mm256_max_ps(tlz, thz), t_best));
    _mm256_store_ps(tnear_out, tnear);
    return _mm256_movemask_ps(_mm256_cmp_ps(tnear, tfar, _CMP_LE_OQ));
}

static int tri_intersect_bvh8q_avx2(const lrt_tri_scene *s, const lrt_ray *ray,
                                    lrt_hit *hit) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_avx_ctx ac;
    tri_avx_setup(&rc, &ac);

    float best_t = ray->tmax;
    float best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT;

    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;

    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_tri8 *blocks = (const lrt_tri8 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                tri_block_isect_avx(&blocks[blk0 + b], &ac, &best_t, &best_u,
                                    &best_v, &best_prim);
            }
            continue;
        }

        const lrt_bvh8q_node *n = &s->nodes8q[TRI_REF_NODE(e.ref)];
        _Alignas(32) float tnear[8];
        int mask = tri_bvh8q_slab_avx(n, &rc, ac.tmin, _mm256_set1_ps(best_t),
                                      tnear);
        mask &= (1 << n->nchildren) - 1;
        uint32_t hit_ref[8];
        float hit_tn[8];
        int nhit = 0;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 8);
            int j = nhit++;
            while (j > 0 && hit_tn[j - 1] > tnear[i]) {
                hit_tn[j] = hit_tn[j - 1];
                hit_ref[j] = hit_ref[j - 1];
                j--;
            }
            hit_tn[j] = tnear[i];
            hit_ref[j] = n->child[i];
        }
        for (int i = nhit - 1; i >= 0; i--) {
            stack[sp].ref = hit_ref[i];
            stack[sp].tnear = hit_tn[i];
            sp++;
        }
    }

    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

static int tri_occluded_bvh8q_avx2(const lrt_tri_scene *s, const lrt_ray *ray) {
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    tri_avx_ctx ac;
    tri_avx_setup(&rc, &ac);
    __m256 tmax8 = _mm256_set1_ps(ray->tmax);

    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;

    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref);
            uint32_t nblk = TRI_REF_NBLOCKS(ref);
            const lrt_tri8 *blocks = (const lrt_tri8 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                if (tri_block_occluded_avx(&blocks[blk0 + b], &ac, tmax8)) {
                    return 1;
                }
            }
            continue;
        }
        const lrt_bvh8q_node *n = &s->nodes8q[TRI_REF_NODE(ref)];
        _Alignas(32) float tnear[8];
        int mask = tri_bvh8q_slab_avx(n, &rc, ac.tmin, tmax8, tnear);
        mask &= (1 << n->nchildren) - 1;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, n->child[i], 8);
            stack[sp++] = n->child[i];
        }
    }
    return 0;
}

/* ---- 4-bit / fp8 node variants (AVX2) ----------------------------------- */

/* E4M3 expand of 8 codes (low 8 bytes) to 8 floats (normal + subnormal). */
static inline __m256 tri_e4m3_expand_avx(const uint8_t *bytes) {
    __m256i q = _mm256_cvtepu8_epi32(
        _mm_loadl_epi64((const __m128i *)(const void *)bytes));
    __m256i e = _mm256_and_si256(_mm256_srli_epi32(q, 3), _mm256_set1_epi32(0xF));
    __m256i m = _mm256_and_si256(q, _mm256_set1_epi32(0x7));
    __m256i nb =
        _mm256_or_si256(_mm256_slli_epi32(
                            _mm256_add_epi32(e, _mm256_set1_epi32(120)), 23),
                        _mm256_slli_epi32(m, 20));
    __m256 normal = _mm256_castsi256_ps(nb);
    __m256 sub =
        _mm256_mul_ps(_mm256_cvtepi32_ps(m), _mm256_set1_ps(1.0f / 512.0f));
    __m256 isz =
        _mm256_castsi256_ps(_mm256_cmpeq_epi32(e, _mm256_setzero_si256()));
    return _mm256_blendv_ps(normal, sub, isz);
}
/* Unpack 8 nibbles (child order) from 4 packed bytes to 8 floats. */
static inline __m256 tri_q4_unpack_avx(const uint8_t packed[4]) {
    _Alignas(16) uint8_t n8[8];
    for (int c = 0; c < 8; c++)
        n8[c] = (c & 1) ? (uint8_t)(packed[c >> 1] >> 4)
                        : (uint8_t)(packed[c >> 1] & 0x0F);
    return _mm256_cvtepi32_ps(
        _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i *)(const void *)n8)));
}

static inline int tri_bvh8qf8_slab_avx(const lrt_bvh8q_node *n,
                                       const tri_ray_ctx *rc, __m256 tmin8,
                                       __m256 t_best, float *tnear_out) {
    __m256 sx = _mm256_set1_ps(n->scale[0] * rc->invd[0]);
    __m256 sy = _mm256_set1_ps(n->scale[1] * rc->invd[1]);
    __m256 sz = _mm256_set1_ps(n->scale[2] * rc->invd[2]);
    __m256 bx = _mm256_set1_ps((n->org[0] - rc->org[0]) * rc->invd[0]);
    __m256 by = _mm256_set1_ps((n->org[1] - rc->org[1]) * rc->invd[1]);
    __m256 bz = _mm256_set1_ps((n->org[2] - rc->org[2]) * rc->invd[2]);
    __m256 tlx = _mm256_fmadd_ps(tri_e4m3_expand_avx(n->qlo_x), sx, bx);
    __m256 thx = _mm256_fmadd_ps(tri_e4m3_expand_avx(n->qhi_x), sx, bx);
    __m256 tly = _mm256_fmadd_ps(tri_e4m3_expand_avx(n->qlo_y), sy, by);
    __m256 thy = _mm256_fmadd_ps(tri_e4m3_expand_avx(n->qhi_y), sy, by);
    __m256 tlz = _mm256_fmadd_ps(tri_e4m3_expand_avx(n->qlo_z), sz, bz);
    __m256 thz = _mm256_fmadd_ps(tri_e4m3_expand_avx(n->qhi_z), sz, bz);
    __m256 tnear = _mm256_max_ps(
        _mm256_max_ps(_mm256_min_ps(tlx, thx), _mm256_min_ps(tly, thy)),
        _mm256_max_ps(_mm256_min_ps(tlz, thz), tmin8));
    __m256 tfar = _mm256_min_ps(
        _mm256_min_ps(_mm256_max_ps(tlx, thx), _mm256_max_ps(tly, thy)),
        _mm256_min_ps(_mm256_max_ps(tlz, thz), t_best));
    _mm256_store_ps(tnear_out, tnear);
    return _mm256_movemask_ps(_mm256_cmp_ps(tnear, tfar, _CMP_LE_OQ));
}

static inline int tri_bvh8q4_slab_avx(const lrt_bvh8q4_node *n,
                                      const tri_ray_ctx *rc, __m256 tmin8,
                                      __m256 t_best, float *tnear_out) {
    __m256 sx = _mm256_set1_ps(n->scale[0] * rc->invd[0]);
    __m256 sy = _mm256_set1_ps(n->scale[1] * rc->invd[1]);
    __m256 sz = _mm256_set1_ps(n->scale[2] * rc->invd[2]);
    __m256 bx = _mm256_set1_ps((n->org[0] - rc->org[0]) * rc->invd[0]);
    __m256 by = _mm256_set1_ps((n->org[1] - rc->org[1]) * rc->invd[1]);
    __m256 bz = _mm256_set1_ps((n->org[2] - rc->org[2]) * rc->invd[2]);
    __m256 tlx = _mm256_fmadd_ps(tri_q4_unpack_avx(n->qlo[0]), sx, bx);
    __m256 thx = _mm256_fmadd_ps(tri_q4_unpack_avx(n->qhi[0]), sx, bx);
    __m256 tly = _mm256_fmadd_ps(tri_q4_unpack_avx(n->qlo[1]), sy, by);
    __m256 thy = _mm256_fmadd_ps(tri_q4_unpack_avx(n->qhi[1]), sy, by);
    __m256 tlz = _mm256_fmadd_ps(tri_q4_unpack_avx(n->qlo[2]), sz, bz);
    __m256 thz = _mm256_fmadd_ps(tri_q4_unpack_avx(n->qhi[2]), sz, bz);
    __m256 tnear = _mm256_max_ps(
        _mm256_max_ps(_mm256_min_ps(tlx, thx), _mm256_min_ps(tly, thy)),
        _mm256_max_ps(_mm256_min_ps(tlz, thz), tmin8));
    __m256 tfar = _mm256_min_ps(
        _mm256_min_ps(_mm256_max_ps(tlx, thx), _mm256_max_ps(tly, thy)),
        _mm256_min_ps(_mm256_max_ps(tlz, thz), t_best));
    _mm256_store_ps(tnear_out, tnear);
    return _mm256_movemask_ps(_mm256_cmp_ps(tnear, tfar, _CMP_LE_OQ));
}

/* Generate intersect/occluded AVX2 kernels for an 8-wide quantized node format
 * (identical traversal; only the node type, scene pointer, and slab differ). */
#define TRI_DEFINE_QNODE_KERNELS(SUF, NODET, NODES, SLAB)                      \
    static int tri_intersect_##SUF##_avx2(const lrt_tri_scene *s,             \
                                          const lrt_ray *ray, lrt_hit *hit) {  \
        tri_ray_ctx rc;                                                        \
        tri_ray_setup(ray, &rc);                                              \
        tri_avx_ctx ac;                                                        \
        tri_avx_setup(&rc, &ac);                                              \
        float best_t = ray->tmax, best_u = 0.0f, best_v = 0.0f;               \
        uint32_t best_prim = LRT_TRI_NO_HIT;                                   \
        tri_stack_entry stack[TRI_STACK_SIZE];                                \
        int sp = 0;                                                            \
        stack[sp].ref = s->root;                                              \
        stack[sp].tnear = rc.tmin;                                            \
        sp++;                                                                  \
        while (sp > 0) {                                                       \
            tri_stack_entry e = stack[--sp];                                  \
            if (e.tnear >= best_t) continue;                                  \
            if (TRI_REF_IS_LEAF(e.ref)) {                                     \
                uint32_t blk0 = TRI_REF_BLOCK(e.ref), nblk = TRI_REF_NBLOCKS(e.ref); \
                const lrt_tri8 *blocks = (const lrt_tri8 *)s->blocks;         \
                for (uint32_t b = 0; b < nblk; b++)                          \
                    tri_block_isect_avx(&blocks[blk0 + b], &ac, &best_t,      \
                                        &best_u, &best_v, &best_prim);        \
                continue;                                                     \
            }                                                                 \
            const NODET *n = &s->NODES[TRI_REF_NODE(e.ref)];                  \
            _Alignas(32) float tnear[8];                                      \
            int mask = SLAB(n, &rc, ac.tmin, _mm256_set1_ps(best_t), tnear);  \
            mask &= (1 << n->nchildren) - 1;                                  \
            uint32_t hit_ref[8];                                              \
            float hit_tn[8];                                                  \
            int nhit = 0;                                                      \
            while (mask) {                                                     \
                int i = __builtin_ctz((unsigned)mask);                        \
                mask &= mask - 1;                                             \
                tri_prefetch_ref(s, n->child[i], 8);                         \
                int j = nhit++;                                              \
                while (j > 0 && hit_tn[j - 1] > tnear[i]) {                   \
                    hit_tn[j] = hit_tn[j - 1];                               \
                    hit_ref[j] = hit_ref[j - 1];                             \
                    j--;                                                     \
                }                                                             \
                hit_tn[j] = tnear[i];                                        \
                hit_ref[j] = n->child[i];                                    \
            }                                                                 \
            for (int i = nhit - 1; i >= 0; i--) {                            \
                stack[sp].ref = hit_ref[i];                                  \
                stack[sp].tnear = hit_tn[i];                                 \
                sp++;                                                        \
            }                                                                 \
        }                                                                     \
        if (hit) {                                                            \
            hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;            \
            hit->u = best_u;                                                  \
            hit->v = best_v;                                                  \
            hit->prim_id = best_prim;                                        \
        }                                                                     \
        return best_prim != LRT_TRI_NO_HIT;                                  \
    }                                                                         \
    static int tri_occluded_##SUF##_avx2(const lrt_tri_scene *s,             \
                                         const lrt_ray *ray) {                \
        tri_ray_ctx rc;                                                        \
        tri_ray_setup(ray, &rc);                                              \
        tri_avx_ctx ac;                                                        \
        tri_avx_setup(&rc, &ac);                                              \
        __m256 tmax8 = _mm256_set1_ps(ray->tmax);                            \
        uint32_t stack[TRI_STACK_SIZE];                                       \
        int sp = 0;                                                            \
        stack[sp++] = s->root;                                                \
        while (sp > 0) {                                                       \
            uint32_t ref = stack[--sp];                                       \
            if (TRI_REF_IS_LEAF(ref)) {                                       \
                uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref); \
                const lrt_tri8 *blocks = (const lrt_tri8 *)s->blocks;         \
                for (uint32_t b = 0; b < nblk; b++)                          \
                    if (tri_block_occluded_avx(&blocks[blk0 + b], &ac, tmax8)) \
                        return 1;                                            \
                continue;                                                     \
            }                                                                 \
            const NODET *n = &s->NODES[TRI_REF_NODE(ref)];                    \
            _Alignas(32) float tnear[8];                                      \
            int mask = SLAB(n, &rc, ac.tmin, tmax8, tnear);                  \
            mask &= (1 << n->nchildren) - 1;                                  \
            while (mask) {                                                     \
                int i = __builtin_ctz((unsigned)mask);                        \
                mask &= mask - 1;                                             \
                tri_prefetch_ref(s, n->child[i], 8);                         \
                if (sp < TRI_STACK_SIZE) stack[sp++] = n->child[i];          \
            }                                                                 \
        }                                                                     \
        return 0;                                                             \
    }

TRI_DEFINE_QNODE_KERNELS(bvh8qf8, lrt_bvh8q_node, nodes8q, tri_bvh8qf8_slab_avx)
TRI_DEFINE_QNODE_KERNELS(bvh8q4, lrt_bvh8q4_node, nodes8q4, tri_bvh8q4_slab_avx)

/* ---- Software-pipelined batch traversal (BVH8/BVH8Q, AVX2) ----------------
 * Same scheme as the BVH4 pipeline; one state serves both 8-wide node
 * encodings (the per-step branch on s->quantized is loop-invariant and
 * predicted perfectly). The 8-wide nodes are 2-4x larger than BVH4's, so a
 * narrower pipe keeps the in-flight footprint from thrashing the shared
 * cache when many threads run. */
#ifndef TRI_PIPE_WIDTH8
#define TRI_PIPE_WIDTH8 TRI_PIPE_WIDTH
#endif
typedef struct tri_pipe8_state {
    tri_ray_ctx rc;
    tri_avx_ctx ac;
    float best_t, best_u, best_v;
    uint32_t best_prim;
    lrt_hit *hit;
    int sp;
    tri_stack_entry stack[TRI_STACK_SIZE];
} tri_pipe8_state;

static inline void tri_pipe8_init(const lrt_tri_scene *s, tri_pipe8_state *st,
                                  const lrt_ray *ray, lrt_hit *hit) {
    tri_ray_setup(ray, &st->rc);
    tri_avx_setup(&st->rc, &st->ac);
    st->best_t = ray->tmax;
    st->best_u = 0.0f;
    st->best_v = 0.0f;
    st->best_prim = LRT_TRI_NO_HIT;
    st->hit = hit;
    st->stack[0].ref = s->root;
    st->stack[0].tnear = st->rc.tmin;
    st->sp = 1;
}

static inline int tri_pipe8_step(const lrt_tri_scene *s, tri_pipe8_state *st) {
    while (st->sp > 0) {
        tri_stack_entry e = st->stack[--st->sp];
        if (e.tnear >= st->best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref);
            uint32_t nblk = TRI_REF_NBLOCKS(e.ref);
            const lrt_tri8 *blocks = (const lrt_tri8 *)s->blocks;
            for (uint32_t b = 0; b < nblk; b++) {
                tri_block_isect_avx(&blocks[blk0 + b], &st->ac, &st->best_t,
                                    &st->best_u, &st->best_v, &st->best_prim);
            }
            return 1;
        }

        _Alignas(32) float tnear[8];
        const uint32_t *child;
        int mask;
        if (s->quantized) {
            const lrt_bvh8q_node *n = &s->nodes8q[TRI_REF_NODE(e.ref)];
            mask = tri_bvh8q_slab_avx(n, &st->rc, st->ac.tmin,
                                      _mm256_set1_ps(st->best_t), tnear);
            mask &= (1 << n->nchildren) - 1;
            child = n->child;
        } else {
            const lrt_bvh8_node *n = &s->nodes8[TRI_REF_NODE(e.ref)];
            mask = tri_bvh8_slab_avx(n, &st->ac, _mm256_set1_ps(st->best_t),
                                     tnear);
            mask &= (1 << n->nchildren) - 1;
            child = n->child;
        }
        uint32_t hit_ref[8];
        float hit_tn[8];
        int nhit = 0;
        while (mask) {
            int i = __builtin_ctz((unsigned)mask);
            mask &= mask - 1;
            tri_prefetch_ref(s, child[i], 8);
            int j = nhit++;
            while (j > 0 && hit_tn[j - 1] > tnear[i]) {
                hit_tn[j] = hit_tn[j - 1];
                hit_ref[j] = hit_ref[j - 1];
                j--;
            }
            hit_tn[j] = tnear[i];
            hit_ref[j] = child[i];
        }
        for (int i = nhit - 1; i >= 0; i--) {
            st->stack[st->sp].ref = hit_ref[i];
            st->stack[st->sp].tnear = hit_tn[i];
            st->sp++;
        }
        return 1;
    }
    return 0;
}

static inline void tri_pipe8_finish(tri_pipe8_state *st) {
    st->hit->t = st->best_prim != LRT_TRI_NO_HIT ? st->best_t : 0.0f;
    st->hit->u = st->best_u;
    st->hit->v = st->best_v;
    st->hit->prim_id = st->best_prim;
}

static void tri_intersect1N_bvh8_avx2(const lrt_tri_scene *s,
                                      const lrt_ray *rays, lrt_hit *hits,
                                      size_t n) {
    if (n < TRI_PIPE_WIDTH8 * 2u) {
        for (size_t i = 0; i < n; i++) {
            if (s->quantized) {
                tri_intersect_bvh8q_avx2(s, &rays[i], &hits[i]);
            } else {
                tri_intersect_bvh8_avx2(s, &rays[i], &hits[i]);
            }
        }
        return;
    }
    tri_pipe8_state st[TRI_PIPE_WIDTH8];
    size_t next = 0;
    unsigned alive = 0;
    for (int k = 0; k < TRI_PIPE_WIDTH8; k++) {
        tri_pipe8_init(s, &st[k], &rays[next], &hits[next]);
        next++;
        alive |= 1u << k;
    }
    while (alive) {
        for (int k = 0; k < TRI_PIPE_WIDTH8; k++) {
            if (!(alive & (1u << k))) continue;
            if (!tri_pipe8_step(s, &st[k])) {
                tri_pipe8_finish(&st[k]);
                if (next < n) {
                    tri_pipe8_init(s, &st[k], &rays[next], &hits[next]);
                    next++;
                } else {
                    alive &= ~(1u << k);
                }
            }
        }
    }
}

/* Pipelined any-hit for the 8-wide layouts. */
typedef struct tri_pipeocc8_state {
    tri_ray_ctx rc;
    tri_avx_ctx ac;
    __m256 tmax8;
    uint8_t *out;
    int sp;
    uint32_t stack[TRI_STACK_SIZE];
} tri_pipeocc8_state;

static inline void tri_pipeocc8_init(const lrt_tri_scene *s,
                                     tri_pipeocc8_state *st,
                                     const lrt_ray *ray, uint8_t *out) {
    tri_ray_setup(ray, &st->rc);
    tri_avx_setup(&st->rc, &st->ac);
    st->tmax8 = _mm256_set1_ps(ray->tmax);
    st->out = out;
    st->stack[0] = s->root;
    st->sp = 1;
}

static inline int tri_pipeocc8_step(const lrt_tri_scene *s,
                                    tri_pipeocc8_state *st) {
    if (st->sp <= 0) {
        *st->out = 0;
        return 0;
    }
    uint32_t ref = st->stack[--st->sp];
    if (TRI_REF_IS_LEAF(ref)) {
        uint32_t blk0 = TRI_REF_BLOCK(ref);
        uint32_t nblk = TRI_REF_NBLOCKS(ref);
        const lrt_tri8 *blocks = (const lrt_tri8 *)s->blocks;
        for (uint32_t b = 0; b < nblk; b++) {
            if (tri_block_occluded_avx(&blocks[blk0 + b], &st->ac, st->tmax8)) {
                *st->out = 1;
                return 0;
            }
        }
        return 1;
    }
    _Alignas(32) float tnear[8];
    const uint32_t *child;
    int mask;
    if (s->quantized) {
        const lrt_bvh8q_node *n = &s->nodes8q[TRI_REF_NODE(ref)];
        mask = tri_bvh8q_slab_avx(n, &st->rc, st->ac.tmin, st->tmax8, tnear);
        mask &= (1 << n->nchildren) - 1;
        child = n->child;
    } else {
        const lrt_bvh8_node *n = &s->nodes8[TRI_REF_NODE(ref)];
        mask = tri_bvh8_slab_avx(n, &st->ac, st->tmax8, tnear);
        mask &= (1 << n->nchildren) - 1;
        child = n->child;
    }
    while (mask) {
        int i = __builtin_ctz((unsigned)mask);
        mask &= mask - 1;
        tri_prefetch_ref(s, child[i], 8);
        st->stack[st->sp++] = child[i];
    }
    return 1;
}

static void tri_occluded1N_bvh8_avx2(const lrt_tri_scene *s,
                                     const lrt_ray *rays, uint8_t *occluded,
                                     size_t n) {
    if (n < TRI_PIPE_WIDTH8 * 2u) {
        for (size_t i = 0; i < n; i++) {
            occluded[i] = (uint8_t)(s->quantized
                                        ? tri_occluded_bvh8q_avx2(s, &rays[i])
                                        : tri_occluded_bvh8_avx2(s, &rays[i]));
        }
        return;
    }
    tri_pipeocc8_state st[TRI_PIPE_WIDTH8];
    size_t next = 0;
    unsigned alive = 0;
    for (int k = 0; k < TRI_PIPE_WIDTH8; k++) {
        tri_pipeocc8_init(s, &st[k], &rays[next], &occluded[next]);
        next++;
        alive |= 1u << k;
    }
    while (alive) {
        for (int k = 0; k < TRI_PIPE_WIDTH8; k++) {
            if (!(alive & (1u << k))) continue;
            if (!tri_pipeocc8_step(s, &st[k])) {
                if (next < n) {
                    tri_pipeocc8_init(s, &st[k], &rays[next], &occluded[next]);
                    next++;
                } else {
                    alive &= ~(1u << k);
                }
            }
        }
    }
}

#endif /* LRT_TRI_HAS_AVX2 */

/* ------------------------------------------------------------------------- */
/* Public API.                                                               */
/* ------------------------------------------------------------------------- */

static void tri_set_err(lrt_result *err, lrt_result v) {
    if (err) *err = v;
}

/* Count the wide nodes and leaf blocks tri_collapse will emit, by mirroring its
 * grouping logic exactly (no writes). Lets the builders allocate the exact
 * sizes instead of the ntris worst-case upper bound, which otherwise reserves
 * ~5x the address space the tree actually uses (resident memory is unaffected
 * - the tail is never touched - but the virtual/committed reservation is). */
static void tri_collapse_count(const tri_build_ctx *bc, int width,
                               uint32_t b_idx, uint32_t *nodes,
                               uint32_t *blocks) {
    const tri_bnode *bn = &bc->bnodes[b_idx];
    if (bn->count > 0) {
        *blocks += (bn->count + (uint32_t)width - 1u) / (uint32_t)width;
        return;
    }
    uint32_t set[8];
    int n = 0;
    set[n++] = bn->a;
    set[n++] = bn->b;
    while (n < width) {
        int expand = -1;
        float best_area = -1.0f;
        for (int i = 0; i < n; i++) {
            const tri_bnode *m = &bc->bnodes[set[i]];
            if (m->count > 0) continue;
            float area = tri_surface_area(m->lo, m->hi);
            if (area > best_area) {
                best_area = area;
                expand = i;
            }
        }
        if (expand < 0) break;
        const tri_bnode *m = &bc->bnodes[set[expand]];
        set[expand] = m->a;
        set[n++] = m->b;
    }
    *nodes += 1;
    for (int i = 0; i < n; i++)
        tri_collapse_count(bc, width, set[i], nodes, blocks);
}

/* Exact (node_cap, block_cap) for a collapse of bc's tree rooted at b_root.
 * Clamped to >= 1 so the allocations are never zero-sized. */
static void tri_collapse_caps(const tri_build_ctx *bc, int width,
                              uint32_t b_root, uint32_t *node_cap,
                              uint32_t *block_cap) {
    uint32_t nn = 0, bb = 0;
    tri_collapse_count(bc, width, b_root, &nn, &bb);
    *node_cap = nn ? nn : 1u;
    *block_cap = bb ? bb : 1u;
}

/* Collapse bc's binary tree (root b_root) into s's already-allocated wide
 * nodes/blocks, fill stats + root AABB. s->layout/quantized must be set.
 * Returns 1 on failure (capacity overflow). Used by the AABB-driven builders
 * (user, sphere); the triangle/curve builders inline the same steps. */
static int tri_collapse_into(lrt_tri_scene *s, tri_build_ctx *bc,
                             uint32_t b_root, uint32_t node_cap,
                             uint32_t block_cap) {
    tri_collapse_ctx cc;
    memset(&cc, 0, sizeof(cc));
    cc.bc = bc;
    cc.s = s;
    cc.node_cap = node_cap;
    cc.block_cap = block_cap;
    cc.width = s->layout;
    cc.quantized = s->quantized;
    cc.qnode = s->qnode;
    const tri_bnode *rootn = &bc->bnodes[b_root];
    cc.root_area = tri_surface_area(rootn->lo, rootn->hi);
    if (cc.root_area <= 0.0f) cc.root_area = 1.0f;
    for (int a = 0; a < 3; a++) {
        s->root_lo[a] = rootn->lo[a];
        s->root_hi[a] = rootn->hi[a];
    }
    if (rootn->count > 0) {
        s->root = tri_emit_leaf(&cc, rootn);
    } else {
        s->root = tri_collapse(&cc, b_root, 0);
    }
    if (cc.failed) return 1;
    s->stats.node_count = s->node_count;
    s->stats.leaf_count = cc.leaf_count;
    s->stats.max_depth = cc.max_depth;
    size_t node_size = s->layout == 4    ? sizeof(lrt_bvh4_node)
                       : s->quantized    ? sizeof(lrt_bvh8q_node)
                                         : sizeof(lrt_bvh8_node);
    s->stats.memory_bytes = (size_t)s->node_count * node_size +
                            (size_t)s->block_count * s->block_stride;
    s->stats.sah_cost = (float)(TRI_TRAV_COST * cc.sah_inner +
                                TRI_ISECT_COST * cc.sah_leaf);
    return 0;
}

static lrt_tri_scene *tri_scene_build_impl(const float *vertices, size_t ntris,
                                           const lrt_tri_build_options *opts,
                                           const uint32_t *morton_override,
                                           lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!vertices || ntris == 0 || ntris > 0x07FFFFFFu) {
        /* leaf-ref encoding: block index needs 27 bits -> <= ~134M blocks */
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }

    lrt_tri_build_options o;
    if (opts) {
        o = *opts;
    } else {
        memset(&o, 0, sizeof(o));
        o.quality = LRT_TRI_BUILD_DEFAULT;
        o.layout = LRT_TRI_LAYOUT_AUTO;
    }

    int layout;
    int quantized = 0;
    int qnode = 0;
    if (o.layout == LRT_TRI_LAYOUT_BVH4) {
        layout = 4;
    } else if (o.layout == LRT_TRI_LAYOUT_BVH8) {
        layout = 8;
    } else if (o.layout == LRT_TRI_LAYOUT_BVH8Q) {
        layout = 8;
        quantized = 1;
    } else if (o.layout == LRT_TRI_LAYOUT_BVH8_QF8) {
        layout = 8;
        quantized = 1;
        qnode = 1; /* E4M3 node bounds */
    } else if (o.layout == LRT_TRI_LAYOUT_BVH8_Q4) {
        layout = 8;
        quantized = 1;
        qnode = 2; /* 4-bit node bounds */
    } else {
        /* AUTO: BVH4. Measured on Zen 1 (mandelbulb, 128k tris, primary and
         * incoherent rays, 1 and 16 threads), BVH4 matches or beats BVH8:
         * 256-bit ops execute as 2x128-bit there, so the wider node test
         * gains nothing while 256-byte nodes cost more bandwidth. Pass
         * LRT_TRI_LAYOUT_BVH8 explicitly on CPUs with a native 256-bit
         * datapath. */
        layout = 4;
    }
#if !LRT_TRI_HAS_AVX2
    if (qnode != 0) { /* q4/fp8 node kernels are AVX2-only */
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
#endif

    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    /* --- Precompute pass: per-tri bounds + centroids. --- */
    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.verts = vertices;
    bc.ntris = ntris;
    bc.max_leaf = max_leaf;
    bc.block_shift = layout == 8 ? 3u : 2u;
    bc.quality = o.quality;

    size_t n3 = ntris * 3;
    bc.plo = (float *)malloc(n3 * sizeof(float));
    bc.phi = (float *)malloc(n3 * sizeof(float));
    bc.cen = (float *)malloc(n3 * sizeof(float));
    bc.indices = (uint32_t *)malloc(ntris * sizeof(uint32_t));
    /* 2*ntris covers any binary tree; +512 absorbs the per-task rounding of
     * the parallel builder's arena slices (2k vs the exact 2k-1). Spatial
     * splits (HQ) duplicate references, so their tree is bounded by the ref
     * capacity instead. Computed in uint64_t to avoid uint32_t overflow. */
    uint64_t bnode_cap64 = (o.quality == LRT_TRI_BUILD_HQ)
                               ? (uint64_t)2 * ntris * TRI_SBVH_SPLIT_FACTOR + 512ull
                               : (uint64_t)2 * ntris + 512ull;
    if (bnode_cap64 > 0x7FFFFFFFu) bnode_cap64 = 0x7FFFFFFFu;
    uint32_t bnode_cap = (uint32_t)bnode_cap64;
    bc.node_next = 0;
    bc.node_end = bnode_cap;
    bc.bnodes = (tri_bnode *)malloc((size_t)bnode_cap * sizeof(tri_bnode));
    if (!bc.plo || !bc.phi || !bc.cen || !bc.indices || !bc.bnodes) {
        free(bc.plo);
        free(bc.phi);
        free(bc.cen);
        free(bc.indices);
        free(bc.bnodes);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
#if !defined(__STDC_NO_THREADS__)
    if (num_threads > 1 && ntris >= 4096) {
        bc.par_threads = num_threads;
        bc.par_scratch = (uint32_t *)malloc(ntris * sizeof(uint32_t));
        /* scratch failure just disables the parallel partition path */
        if (!bc.par_scratch) bc.par_threads = 0;
    }
#endif

    int bad_input = tri_precompute(&bc, num_threads);
    if (bad_input) {
        free(bc.plo);
        free(bc.phi);
        free(bc.cen);
        free(bc.indices);
        free(bc.bnodes);
        free(bc.par_scratch);
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }

    /* --- LBVH preprocessing (LRT_TRI_BUILD_FAST): Morton sort. --- */
    uint64_t *lbvh_keys = NULL, *lbvh_tmp = NULL;
    if (o.quality == LRT_TRI_BUILD_FAST) {
        lbvh_keys = (uint64_t *)malloc(ntris * sizeof(uint64_t));
        lbvh_tmp = (uint64_t *)malloc(ntris * sizeof(uint64_t));
        if (!lbvh_keys || !lbvh_tmp) {
            free(lbvh_keys);
            free(lbvh_tmp);
            free(bc.plo);
            free(bc.phi);
            free(bc.cen);
            free(bc.indices);
            free(bc.bnodes);
            free(bc.par_scratch);
            tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
            return NULL;
        }
        if (morton_override) {
            /* GPU-assisted build: take the 30-bit Morton codes from the caller
             * (computed on the GPU) instead of encoding them on the CPU. */
            for (size_t i = 0; i < ntris; i++)
                lbvh_keys[i] =
                    ((uint64_t)(morton_override[i] & 0x3FFFFFFFu) << 32) |
                    (uint64_t)i;
        } else {
            tri_morton_encode(&bc, lbvh_keys, num_threads);
        }
        const uint64_t *sorted = tri_radix_sort_keys(lbvh_keys, lbvh_tmp, ntris);
        for (size_t i = 0; i < ntris; i++) {
            bc.indices[i] = (uint32_t)sorted[i];
        }
        bc.lbvh_keys = sorted;
        uint32_t bw = 1u << bc.block_shift;
        bc.lbvh_leaf = 2u * bw;
        if (bc.lbvh_leaf > max_leaf) bc.lbvh_leaf = max_leaf;
    }

    /* --- Binary build (parallel when num_threads > 1; HQ/SBVH is serial). */
    uint32_t emit_total = (uint32_t)ntris;
    uint32_t b_root;
    if (o.quality == LRT_TRI_BUILD_HQ) {
        b_root = tri_build_sbvh(&bc, &emit_total);
    } else if (o.quality == LRT_TRI_BUILD_FAST) {
        b_root = tri_build_lbvh(&bc, num_threads);
    } else {
        b_root = tri_build_binary(&bc, num_threads);
    }

    lrt_tri_scene *s = NULL;
    if (!bc.failed) {
        s = (lrt_tri_scene *)calloc(1, sizeof(lrt_tri_scene));
        if (!s) bc.failed = 1;
    }

    if (!bc.failed) {
        s->layout = layout;
        s->quantized = quantized;
        s->qnode = qnode;
        s->block_stride = (uint32_t)tri_block_size(layout);
        s->original_ntris = (uint32_t)ntris;
        /* Allocate exactly what the collapse will emit (vs the ntris worst-case
         * bound, which over-reserves ~5x the address space). */
        uint32_t node_cap, block_cap;
        tri_collapse_caps(&bc, layout, b_root, &node_cap, &block_cap);
        (void)emit_total;
        if (layout == 4) {
            s->nodes4 = (lrt_bvh4_node *)tri_aligned_alloc(
                64, (size_t)node_cap * sizeof(lrt_bvh4_node));
        } else if (qnode == 2) {
            s->nodes8q4 = (lrt_bvh8q4_node *)tri_aligned_alloc(
                64, (size_t)node_cap * sizeof(lrt_bvh8q4_node));
        } else if (quantized) {
            s->nodes8q = (lrt_bvh8q_node *)tri_aligned_alloc(
                64, (size_t)node_cap * sizeof(lrt_bvh8q_node));
        } else {
            s->nodes8 = (lrt_bvh8_node *)tri_aligned_alloc(
                64, (size_t)node_cap * sizeof(lrt_bvh8_node));
        }
        s->blocks = tri_aligned_alloc(
            64, (size_t)block_cap * s->block_stride);
        if ((!s->nodes4 && !s->nodes8 && !s->nodes8q && !s->nodes8q4) ||
            !s->blocks) {
            bc.failed = 1;
        } else {
            tri_collapse_ctx cc;
            memset(&cc, 0, sizeof(cc));
            cc.bc = &bc;
            cc.s = s;
            cc.node_cap = node_cap;
            cc.block_cap = block_cap;
            cc.width = layout;
            cc.quantized = quantized;
            cc.qnode = s->qnode;
            const tri_bnode *rootn = &bc.bnodes[b_root];
            cc.root_area = tri_surface_area(rootn->lo, rootn->hi);
            if (cc.root_area <= 0.0f) cc.root_area = 1.0f;
            for (int a = 0; a < 3; a++) {
                s->root_lo[a] = rootn->lo[a];
                s->root_hi[a] = rootn->hi[a];
            }

            if (rootn->count > 0) {
                s->root = tri_emit_leaf(&cc, rootn);
            } else {
                s->root = tri_collapse(&cc, b_root, 0);
            }
            if (cc.failed) {
                bc.failed = 1;
            } else {
                s->stats.node_count = s->node_count;
                s->stats.leaf_count = cc.leaf_count;
                s->stats.max_depth = cc.max_depth;
                size_t node_size = layout == 4   ? sizeof(lrt_bvh4_node)
                                   : qnode == 2  ? sizeof(lrt_bvh8q4_node)
                                   : quantized   ? sizeof(lrt_bvh8q_node)
                                                 : sizeof(lrt_bvh8_node);
                s->stats.memory_bytes =
                    (size_t)s->node_count * node_size +
                    (size_t)s->block_count * s->block_stride;
                s->stats.sah_cost = (float)(TRI_TRAV_COST * cc.sah_inner +
                                            TRI_ISECT_COST * cc.sah_leaf);
            }
        }
    }

    free(bc.plo);
    free(bc.phi);
    free(bc.cen);
    free(bc.indices);
    free(bc.bnodes);
    free(bc.par_scratch);
    free(lbvh_keys);
    free(lbvh_tmp);

    if (bc.failed) {
        lrt_tri_scene_free(s);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

#if LRT_TRI_HAS_AVX2
    s->kernel_name = layout == 4    ? "bvh4/sse4"
                     : quantized    ? "bvh8q/avx2"
                                    : "bvh8/avx2";
#elif LRT_TRI_HAS_SSE4
    s->kernel_name = layout == 4    ? "bvh4/sse4"
                     : quantized    ? "bvh8q/scalar"
                                    : "bvh8/scalar";
#else
    s->kernel_name = layout == 4    ? "bvh4/scalar"
                     : quantized    ? "bvh8q/scalar"
                                    : "bvh8/scalar";
#endif
    return s;
}

lrt_tri_scene *lrt_tri_scene_build(const float *vertices, size_t ntris,
                                   const lrt_tri_build_options *opts,
                                   lrt_result *err) {
    return tri_scene_build_impl(vertices, ntris, opts, NULL, err);
}

/* GPU-assisted build hook used by lightrt_c_vk.c (declared extern there; not in
 * the public header). Build a FAST (LBVH) triangle scene from caller-supplied
 * 30-bit Morton codes (morton[i] in [0, 2^30)) instead of encoding them on the
 * CPU. Given identical codes this is byte-identical to lrt_tri_scene_build()
 * with LRT_TRI_BUILD_FAST. layout must be BVH4 or BVH8. */
lrt_tri_scene *lrt_tri_scene_build_lbvh_morton(const float *vertices,
                                               size_t ntris,
                                               const uint32_t *morton,
                                               lrt_tri_layout layout,
                                               unsigned max_leaf_size,
                                               lrt_result *err) {
    if (!morton) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    lrt_tri_build_options o;
    memset(&o, 0, sizeof(o));
    o.quality = LRT_TRI_BUILD_FAST;
    o.layout = layout;
    o.max_leaf_size = max_leaf_size;
    o.num_threads = 1;
    return tri_scene_build_impl(vertices, ntris, &o, morton, err);
}

/* Release a memory-mapped scene's backing store. The mmap path is implemented
 * in Workstream E; defined here so lrt_tri_scene_free has one place to call. */
static void tri_scene_unmap(lrt_tri_scene *s) {
#if LRT_TRI_HAS_MMAP
    if (s->map_base && s->map_size) munmap(s->map_base, s->map_size);
#else
    free(s->map_base);
#endif
    s->map_base = NULL;
    s->map_size = 0;
}

void lrt_tri_scene_free(lrt_tri_scene *s) {
    if (!s) return;
    if (s->mem_mapped) {
        /* nodes/blocks alias the file mapping; release it, not them. */
        tri_scene_unmap(s);
    } else {
        tri_aligned_free(s->nodes4);
        tri_aligned_free(s->nodes8);
        tri_aligned_free(s->nodes8q);
        tri_aligned_free(s->nodes8q4);
        tri_aligned_free(s->blocks);
    }
    free(s->owned_user);
    free(s);
}

int lrt_tri_intersect1(const lrt_tri_scene *s, const lrt_ray *ray, lrt_hit *hit) {
    if (!s || !ray) {
        if (hit) hit->prim_id = LRT_TRI_NO_HIT;
        return 0;
    }
    if (s->prim_kind == TRI_PRIM_QTRI) {
#if LRT_TRI_HAS_SSE4
        return tri_qtri_intersect_bvh4(s, ray, hit);
#else
        return tri_qtri_intersect_scalar(s, ray, hit);
#endif
    }
#if LRT_TRI_HAS_SSE4
    if (s->curve) return tri_curve_intersect_bvh4(s, ray, hit);
    if (s->prim_kind == TRI_PRIM_RLCURVE)
        return tri_rlcurve_intersect_bvh4(s, ray, hit);
    if (s->prim_kind == TRI_PRIM_POINT)
        return tri_point_intersect_bvh4(s, ray, hit);
    if (s->prim_kind == TRI_PRIM_FLATCURVE)
        return tri_flatcurve_intersect_bvh4(s, ray, hit);
    if (s->prim_kind == TRI_PRIM_BEZCURVE)
        return tri_bezcurve_intersect_bvh4(s, ray, hit);
    if (s->prim_kind == TRI_PRIM_SPHERE)
        return tri_sphere_intersect_bvh4(s, ray, hit);
    if (s->prim_kind == TRI_PRIM_USER)
        return tri_user_intersect_bvh4(s, ray, hit);
#endif
#if LRT_TRI_HAS_AVX2
    if (s->layout == 8) {
        if (s->qnode == 2) return tri_intersect_bvh8q4_avx2(s, ray, hit);
        if (s->qnode == 1) return tri_intersect_bvh8qf8_avx2(s, ray, hit);
        return s->quantized ? tri_intersect_bvh8q_avx2(s, ray, hit)
                            : tri_intersect_bvh8_avx2(s, ray, hit);
    }
#endif
#if LRT_TRI_HAS_SSE4
    if (s->layout == 4) return tri_intersect_bvh4_sse(s, ray, hit);
#endif
    return tri_intersect_scalar(s, ray, hit);
}

int lrt_tri_occluded1(const lrt_tri_scene *s, const lrt_ray *ray) {
    if (!s || !ray) return 0;
    if (s->prim_kind == TRI_PRIM_QTRI) {
#if LRT_TRI_HAS_SSE4
        return tri_qtri_occluded_bvh4(s, ray);
#else
        return tri_qtri_occluded_scalar(s, ray);
#endif
    }
#if LRT_TRI_HAS_SSE4
    if (s->curve) return tri_curve_occluded_bvh4(s, ray);
    if (s->prim_kind == TRI_PRIM_RLCURVE)
        return tri_rlcurve_occluded_bvh4(s, ray);
    if (s->prim_kind == TRI_PRIM_POINT)
        return tri_point_occluded_bvh4(s, ray);
    if (s->prim_kind == TRI_PRIM_FLATCURVE)
        return tri_flatcurve_occluded_bvh4(s, ray);
    if (s->prim_kind == TRI_PRIM_BEZCURVE)
        return tri_bezcurve_occluded_bvh4(s, ray);
    if (s->prim_kind == TRI_PRIM_SPHERE) return tri_sphere_occluded_bvh4(s, ray);
    if (s->prim_kind == TRI_PRIM_USER) return tri_user_occluded_bvh4(s, ray);
#endif
#if LRT_TRI_HAS_AVX2
    if (s->layout == 8) {
        if (s->qnode == 2) return tri_occluded_bvh8q4_avx2(s, ray);
        if (s->qnode == 1) return tri_occluded_bvh8qf8_avx2(s, ray);
        return s->quantized ? tri_occluded_bvh8q_avx2(s, ray)
                            : tri_occluded_bvh8_avx2(s, ray);
    }
#endif
#if LRT_TRI_HAS_SSE4
    if (s->layout == 4) return tri_occluded_bvh4_sse(s, ray);
#endif
    return tri_occluded_scalar(s, ray);
}

/* Transpose 4 consecutive AoS rays into a SoA packet. */
static inline void tri_pack4(const lrt_ray *r, lrt_ray4 *p) {
    for (int k = 0; k < 4; k++) {
        p->orgx[k] = r[k].org[0]; p->orgy[k] = r[k].org[1]; p->orgz[k] = r[k].org[2];
        p->dirx[k] = r[k].dir[0]; p->diry[k] = r[k].dir[1]; p->dirz[k] = r[k].dir[2];
        p->tmin[k] = r[k].tmin; p->tmax[k] = r[k].tmax;
    }
}

void lrt_tri_intersect1N(const lrt_tri_scene *s, const lrt_ray *rays,
                         lrt_hit *hits, size_t n, lrt_tri_batch_hint hint) {
    if (!s || !rays || !hits) return;
    (void)hint; /* unused in scalar-only builds */
    if (s->curve || s->prim_kind == TRI_PRIM_USER ||
        s->prim_kind == TRI_PRIM_SPHERE || s->prim_kind == TRI_PRIM_QTRI ||
        s->prim_kind == TRI_PRIM_RLCURVE || s->prim_kind == TRI_PRIM_POINT ||
        s->prim_kind == TRI_PRIM_FLATCURVE || s->prim_kind == TRI_PRIM_BEZCURVE ||
        s->qnode != 0) {
        /* interleaved pipelines are triangle-specific; loop per ray */
        for (size_t i = 0; i < n; i++) lrt_tri_intersect1(s, &rays[i], &hits[i]);
        return;
    }
#if LRT_TRI_HAS_SSE4
    if (hint == LRT_TRI_BATCH_COHERENT && s->layout == 4) {
        /* Coherent closest-hit: the Ray4 packet amortizes node and leaf fetches
         * over the packet. Measured +20-30% over the per-ray kernel on primary
         * camera rays (~1.3-1.5x Embree on the mandelbulb). BVH4 only: the
         * packet kernel is SSE4, matching the BVH4 single-ray kernel bit for
         * bit (so results stay identical to looping intersect1); BVH8 packets
         * would use SSE4 while BVH8 single-ray uses AVX2, breaking that, for a
         * smaller win. Any-hit deliberately does NOT packetize - lockstep
         * traversal defeats per-ray early-out (see lrt_tri_occluded1N). */
        size_t i = 0;
        for (; i + 4 <= n; i += 4) {
            lrt_ray4 p;
            tri_pack4(&rays[i], &p);
            lrt_hit4 h;
            lrt_tri_intersect4(s, &p, &h);
            for (int k = 0; k < 4; k++) {
                hits[i + k].t = h.t[k];
                hits[i + k].u = h.u[k];
                hits[i + k].v = h.v[k];
                hits[i + k].prim_id = h.prim_id[k];
            }
        }
        for (; i < n; i++) lrt_tri_intersect1(s, &rays[i], &hits[i]);
        return;
    }
#endif
    /* Interleaved traversal pays off only when rays diverge (cache-cold
     * nodes); coherent batches run the plain per-ray kernel. */
#if LRT_TRI_HAS_AVX2
    if (s->layout == 8 && hint != LRT_TRI_BATCH_COHERENT) {
        tri_intersect1N_bvh8_avx2(s, rays, hits, n);
        return;
    }
#endif
#if LRT_TRI_HAS_SSE4
    if (s->layout == 4 && hint != LRT_TRI_BATCH_COHERENT) {
        tri_intersect1N_bvh4_sse(s, rays, hits, n);
        return;
    }
#endif
    for (size_t i = 0; i < n; i++) {
#if LRT_TRI_HAS_SSE4
        if (i + 1 < n) _mm_prefetch((const char *)&rays[i + 1], _MM_HINT_T0);
#endif
        lrt_tri_intersect1(s, &rays[i], &hits[i]);
    }
}

void lrt_tri_occluded1N(const lrt_tri_scene *s, const lrt_ray *rays,
                        uint8_t *occluded, size_t n, lrt_tri_batch_hint hint) {
    if (!s || !rays || !occluded) return;
    (void)hint; /* unused in scalar-only builds */
    if (s->curve || s->prim_kind == TRI_PRIM_USER ||
        s->prim_kind == TRI_PRIM_SPHERE || s->prim_kind == TRI_PRIM_QTRI ||
        s->prim_kind == TRI_PRIM_RLCURVE || s->prim_kind == TRI_PRIM_POINT ||
        s->prim_kind == TRI_PRIM_FLATCURVE || s->prim_kind == TRI_PRIM_BEZCURVE ||
        s->qnode != 0) {
        for (size_t i = 0; i < n; i++) {
            occluded[i] = (uint8_t)lrt_tri_occluded1(s, &rays[i]);
        }
        return;
    }
#if LRT_TRI_HAS_AVX2
    if (s->layout == 8 && hint != LRT_TRI_BATCH_COHERENT) {
        tri_occluded1N_bvh8_avx2(s, rays, occluded, n);
        return;
    }
#endif
#if LRT_TRI_HAS_SSE4
    if (s->layout == 4 && hint != LRT_TRI_BATCH_COHERENT) {
        tri_occluded1N_bvh4_sse(s, rays, occluded, n);
        return;
    }
#endif
    for (size_t i = 0; i < n; i++) {
#if LRT_TRI_HAS_SSE4
        if (i + 1 < n) _mm_prefetch((const char *)&rays[i + 1], _MM_HINT_T0);
#endif
        occluded[i] = (uint8_t)lrt_tri_occluded1(s, &rays[i]);
    }
}

void lrt_tri_scene_stats(const lrt_tri_scene *s, lrt_tri_stats *out) {
    if (!out) return;
    if (!s) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = s->stats;
}

const char *lrt_tri_kernel_name(const lrt_tri_scene *s) {
    return s ? s->kernel_name : "none";
}

/* ------------------------------------------------------------------------- */
/* Spatial queries (triangle scenes): nearest point, kNN, region, multi-hit. */
/* ------------------------------------------------------------------------- */

/* Unified view of a wide node's child bounds + refs (quantized nodes decoded
 * into the caller's float[48] scratch). */
typedef struct tri_node_view {
    const float *lo_x, *lo_y, *lo_z, *hi_x, *hi_y, *hi_z;
    const uint32_t *child;
    int n;
} tri_node_view;

static inline void tri_node_load(const lrt_tri_scene *s, uint32_t node_ref,
                                 float *dec, tri_node_view *v) {
    if (s->layout == 4) {
        const lrt_bvh4_node *n = &s->nodes4[TRI_REF_NODE(node_ref)];
        v->lo_x = n->lo_x; v->lo_y = n->lo_y; v->lo_z = n->lo_z;
        v->hi_x = n->hi_x; v->hi_y = n->hi_y; v->hi_z = n->hi_z;
        v->child = n->child; v->n = (int)n->nchildren;
    } else if (s->quantized) {
        const lrt_bvh8q_node *n = &s->nodes8q[TRI_REF_NODE(node_ref)];
        tri_bvh8q_decode(n, dec, dec + 8, dec + 16, dec + 24, dec + 32,
                         dec + 40);
        v->lo_x = dec; v->lo_y = dec + 8; v->lo_z = dec + 16;
        v->hi_x = dec + 24; v->hi_y = dec + 32; v->hi_z = dec + 40;
        v->child = n->child; v->n = (int)n->nchildren;
    } else {
        const lrt_bvh8_node *n = &s->nodes8[TRI_REF_NODE(node_ref)];
        v->lo_x = n->lo_x; v->lo_y = n->lo_y; v->lo_z = n->lo_z;
        v->hi_x = n->hi_x; v->hi_y = n->hi_y; v->hi_z = n->hi_z;
        v->child = n->child; v->n = (int)n->nchildren;
    }
}

/* Squared distance from p to child slot i's AABB. */
static inline float tri_point_box_distsq(const tri_node_view *v, int i,
                                         const float p[3]) {
    float dx = p[0] < v->lo_x[i] ? v->lo_x[i] - p[0]
               : p[0] > v->hi_x[i] ? p[0] - v->hi_x[i] : 0.0f;
    float dy = p[1] < v->lo_y[i] ? v->lo_y[i] - p[1]
               : p[1] > v->hi_y[i] ? p[1] - v->hi_y[i] : 0.0f;
    float dz = p[2] < v->lo_z[i] ? v->lo_z[i] - p[2]
               : p[2] > v->hi_z[i] ? p[2] - v->hi_z[i] : 0.0f;
    return dx * dx + dy * dy + dz * dz;
}

/* Closest point q on triangle ABC to p; returns the squared distance. Ericson,
 * Real-Time Collision Detection. */
static float tri_point_tri_distsq(const float *A, const float *B,
                                  const float *C, const float p[3],
                                  float q[3]) {
    float ab[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};
    float ac[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
    float ap[3] = {p[0] - A[0], p[1] - A[1], p[2] - A[2]};
    float d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2];
    float d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
    do {
        if (d1 <= 0.0f && d2 <= 0.0f) {
            q[0] = A[0]; q[1] = A[1]; q[2] = A[2];
            break;
        }
        float bp[3] = {p[0] - B[0], p[1] - B[1], p[2] - B[2]};
        float d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2];
        float d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
        if (d3 >= 0.0f && d4 <= d3) {
            q[0] = B[0]; q[1] = B[1]; q[2] = B[2];
            break;
        }
        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            float vv = d1 / (d1 - d3);
            for (int a = 0; a < 3; a++) q[a] = A[a] + vv * ab[a];
            break;
        }
        float cp[3] = {p[0] - C[0], p[1] - C[1], p[2] - C[2]};
        float d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2];
        float d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
        if (d6 >= 0.0f && d5 <= d6) {
            q[0] = C[0]; q[1] = C[1]; q[2] = C[2];
            break;
        }
        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            float ww = d2 / (d2 - d6);
            for (int a = 0; a < 3; a++) q[a] = A[a] + ww * ac[a];
            break;
        }
        float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            float ww = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            for (int a = 0; a < 3; a++) q[a] = B[a] + ww * (C[a] - B[a]);
            break;
        }
        float denom = 1.0f / (va + vb + vc);
        float vv = vb * denom, ww = vc * denom;
        for (int a = 0; a < 3; a++) q[a] = A[a] + ab[a] * vv + ac[a] * ww;
    } while (0);
    float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
    return dx * dx + dy * dy + dz * dz;
}

/* Reconstruct triangle vertices from a leaf block lane (v0, v0+e1, v0+e2). */
static inline void tri_leaf_verts(const float *f, int bw, int lane, float A[3],
                                  float B[3], float C[3]) {
    for (int a = 0; a < 3; a++) {
        float v0 = f[a * bw + lane];
        A[a] = v0;
        B[a] = v0 + f[(3 + a) * bw + lane];
        C[a] = v0 + f[(6 + a) * bw + lane];
    }
}

static inline void tri_leaf_prim_aabb(const float *f, int bw, int lane,
                                      float lo[3], float hi[3]) {
    float A[3], B[3], C[3];
    tri_leaf_verts(f, bw, lane, A, B, C);
    for (int a = 0; a < 3; a++) {
        float mn = A[a] < B[a] ? A[a] : B[a];
        mn = mn < C[a] ? mn : C[a];
        float mx = A[a] > B[a] ? A[a] : B[a];
        mx = mx > C[a] ? mx : C[a];
        lo[a] = mn;
        hi[a] = mx;
    }
}

/* Distance-ordered DFS that maintains an ascending k-set of nearest primitives
 * in out[0..k). Returns the count; best_pt (optional) receives the closest
 * point of out[0]. */
static size_t tri_nearest_core(const lrt_tri_scene *s, const float p[3],
                               uint32_t k, lrt_knn_result *out,
                               float best_pt[3]) {
    const int width = s->layout;
    struct { uint32_t ref; float d; } stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].d = 0.0f;
    sp++;
    size_t cnt = 0;
    float kth = TRI_INF_F * TRI_INF_F;
    while (sp > 0) {
        uint32_t ref = stack[sp - 1].ref;
        float bd = stack[sp - 1].d;
        sp--;
        if (cnt == k && bd >= kth) continue;
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            for (uint32_t b = 0; b < nblk; b++) {
                const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                const uint32_t *ids = (const uint32_t *)(f + 9 * width);
                for (int lane = 0; lane < width; lane++) {
                    if (ids[lane] == LRT_TRI_NO_HIT) continue;
                    float A[3], B[3], C[3], q[3];
                    tri_leaf_verts(f, width, lane, A, B, C);
                    float dsq = tri_point_tri_distsq(A, B, C, p, q);
                    if (cnt < k || dsq < kth) {
                        uint32_t pid = ids[lane];
                        int dup = 0;
                        for (size_t m = 0; m < cnt; m++)
                            if (out[m].prim_id == pid) {
                                dup = 1;
                                break;
                            }
                        if (dup) continue;
                        size_t pos;
                        if (cnt < k) {
                            pos = cnt;
                            cnt++;
                        } else {
                            pos = k - 1;
                        }
                        while (pos > 0 && out[pos - 1].dist_sq > dsq) {
                            out[pos] = out[pos - 1];
                            pos--;
                        }
                        out[pos].dist_sq = dsq;
                        out[pos].prim_id = pid;
                        if (pos == 0 && best_pt) {
                            best_pt[0] = q[0];
                            best_pt[1] = q[1];
                            best_pt[2] = q[2];
                        }
                        kth = cnt == k ? out[k - 1].dist_sq : TRI_INF_F * TRI_INF_F;
                    }
                }
            }
            continue;
        }
        float dec[48];
        tri_node_view nv;
        tri_node_load(s, ref, dec, &nv);
        uint32_t cref[8];
        float cd[8];
        int m = 0;
        for (int i = 0; i < nv.n; i++) {
            float d = tri_point_box_distsq(&nv, i, p);
            if (cnt < k || d < kth) {
                int j = m++;
                while (j > 0 && cd[j - 1] < d) {
                    cd[j] = cd[j - 1];
                    cref[j] = cref[j - 1];
                    j--;
                }
                cd[j] = d;
                cref[j] = nv.child[i];
            }
        }
        if (sp + m > TRI_STACK_SIZE) m = TRI_STACK_SIZE - sp;
        for (int i = 0; i < m; i++) {
            stack[sp].ref = cref[i];
            stack[sp].d = cd[i];
            sp++;
        }
    }
    return cnt;
}

int lrt_tri_closest_point(const lrt_tri_scene *s, const float p[3],
                          lrt_point_hit *out) {
    if (!s || !p || s->prim_kind != TRI_PRIM_TRI) return 0;
    lrt_knn_result one;
    float q[3] = {0, 0, 0};
    if (tri_nearest_core(s, p, 1, &one, q) == 0) return 0;
    if (out) {
        out->dist_sq = one.dist_sq;
        out->prim_id = one.prim_id;
        out->point[0] = q[0];
        out->point[1] = q[1];
        out->point[2] = q[2];
    }
    return 1;
}

size_t lrt_tri_knn(const lrt_tri_scene *s, const float p[3], uint32_t k,
                   lrt_knn_result *out, size_t cap) {
    if (!s || !p || !out || cap == 0 || k == 0 || s->prim_kind != TRI_PRIM_TRI)
        return 0;
    if ((size_t)k > cap) k = (uint32_t)cap;
    return tri_nearest_core(s, p, k, out, NULL);
}

/* Region descriptor for the AABB / sphere / frustum collectors. */
typedef struct tri_region {
    int type; /* 0 = aabb, 1 = sphere, 2 = frustum */
    float lo[3], hi[3];
    float c[3], r2;
    const lrt_frustum *fr;
} tri_region;

/* True if the box [blo, bhi] is (potentially) inside the region. */
static inline int tri_region_test_box(const tri_region *rg, const float blo[3],
                                      const float bhi[3]) {
    if (rg->type == 0) {
        return blo[0] <= rg->hi[0] && bhi[0] >= rg->lo[0] &&
               blo[1] <= rg->hi[1] && bhi[1] >= rg->lo[1] &&
               blo[2] <= rg->hi[2] && bhi[2] >= rg->lo[2];
    }
    if (rg->type == 1) {
        float dsq = 0.0f;
        for (int a = 0; a < 3; a++) {
            float v = rg->c[a];
            float cl = v < blo[a] ? blo[a] : (v > bhi[a] ? bhi[a] : v);
            float d = v - cl;
            dsq += d * d;
        }
        return dsq <= rg->r2;
    }
    /* frustum: cull only if the p-vertex is outside some plane */
    for (int pl = 0; pl < 6; pl++) {
        const float *P = rg->fr->planes[pl];
        float px = P[0] >= 0.0f ? bhi[0] : blo[0];
        float py = P[1] >= 0.0f ? bhi[1] : blo[1];
        float pz = P[2] >= 0.0f ? bhi[2] : blo[2];
        if (P[0] * px + P[1] * py + P[2] * pz + P[3] < 0.0f) return 0;
    }
    return 1;
}

static size_t tri_query_region(const lrt_tri_scene *s, const tri_region *rg,
                               uint32_t *out, size_t cap) {
    if (!s || !out || cap == 0 || s->prim_kind != TRI_PRIM_TRI) return 0;
    const int width = s->layout;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    size_t cnt = 0;
    while (sp > 0 && cnt < cap) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            for (uint32_t b = 0; b < nblk && cnt < cap; b++) {
                const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                const uint32_t *ids = (const uint32_t *)(f + 9 * width);
                for (int lane = 0; lane < width && cnt < cap; lane++) {
                    if (ids[lane] == LRT_TRI_NO_HIT) continue;
                    float blo[3], bhi[3];
                    tri_leaf_prim_aabb(f, width, lane, blo, bhi);
                    if (tri_region_test_box(rg, blo, bhi)) out[cnt++] = ids[lane];
                }
            }
            continue;
        }
        float dec[48];
        tri_node_view nv;
        tri_node_load(s, ref, dec, &nv);
        for (int i = 0; i < nv.n && sp < TRI_STACK_SIZE; i++) {
            float blo[3] = {nv.lo_x[i], nv.lo_y[i], nv.lo_z[i]};
            float bhi[3] = {nv.hi_x[i], nv.hi_y[i], nv.hi_z[i]};
            if (tri_region_test_box(rg, blo, bhi)) stack[sp++] = nv.child[i];
        }
    }
    return cnt;
}

size_t lrt_tri_query_aabb(const lrt_tri_scene *s, const float lo[3],
                          const float hi[3], uint32_t *out, size_t cap) {
    if (!lo || !hi) return 0;
    /* Validate: lo[a] <= hi[a] for each axis. Inverted bounds return empty. */
    {
        int valid = 1;
        int a;
        for (a = 0; a < 3; a++) {
            if (lo[a] > hi[a]) { valid = 0; break; }
        }
        if (!valid) return 0;
    }
    tri_region rg;
    memset(&rg, 0, sizeof(rg));
    rg.type = 0;
    for (int a = 0; a < 3; a++) {
        rg.lo[a] = lo[a];
        rg.hi[a] = hi[a];
    }
    return tri_query_region(s, &rg, out, cap);
}

size_t lrt_tri_query_sphere(const lrt_tri_scene *s, const float center[3],
                            float radius, uint32_t *out, size_t cap) {
    if (!center) return 0;
    tri_region rg;
    memset(&rg, 0, sizeof(rg));
    rg.type = 1;
    for (int a = 0; a < 3; a++) rg.c[a] = center[a];
    rg.r2 = radius * radius;
    return tri_query_region(s, &rg, out, cap);
}

void lrt_frustum_from_matrix(const float m[16], lrt_frustum *out) {
    if (!m || !out) return;
    /* Row-major m, clip = m * [x y z 1]^T, GL z in [-1,1]. Gribb-Hartmann:
     * left/right = row3 +/- row0, bottom/top = row3 +/- row1,
     * near/far = row3 +/- row2. Plane normals point inward (n.p + d >= 0). */
    for (int i = 0; i < 6; i++) {
        int row = i >> 1;
        int add = (i & 1) == 0;
        float *pl = out->planes[i];
        for (int c = 0; c < 4; c++) {
            float w = m[3 * 4 + c];
            float a = m[row * 4 + c];
            pl[c] = add ? (w + a) : (w - a);
        }
        float nl = sqrtf(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
        if (nl > 0.0f)
            for (int c = 0; c < 4; c++) pl[c] /= nl;
    }
}

size_t lrt_tri_query_frustum(const lrt_tri_scene *s, const lrt_frustum *f,
                             uint32_t *out, size_t cap) {
    if (!f) return 0;
    tri_region rg;
    memset(&rg, 0, sizeof(rg));
    rg.type = 2;
    rg.fr = f;
    return tri_query_region(s, &rg, out, cap);
}

size_t lrt_tri_intersect_n(const lrt_tri_scene *s, const lrt_ray *ray,
                           lrt_hit *out, size_t max_hits) {
    if (!s || !ray || !out || max_hits == 0 || s->prim_kind != TRI_PRIM_TRI)
        return 0;
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    const int width = s->layout;
    size_t cnt = 0;
    float prune_t = ray->tmax;
    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = s->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= prune_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(e.ref), nblk = TRI_REF_NBLOCKS(e.ref);
            for (uint32_t b = 0; b < nblk; b++) {
                const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                const uint32_t *ids = (const uint32_t *)(f + 9 * width);
                for (int lane = 0; lane < width; lane++) {
                    if (ids[lane] == LRT_TRI_NO_HIT) continue;
                    float t, u, v;
                    if (!tri_isect_lane(f, width, lane, &rc, prune_t, &t, &u, &v))
                        continue;
                    uint32_t pid = ids[lane];
                    int dup = 0;
                    for (size_t mm = 0; mm < cnt; mm++)
                        if (out[mm].prim_id == pid) {
                            dup = 1;
                            break;
                        }
                    if (dup) continue;
                    size_t pos;
                    if (cnt < max_hits) {
                        pos = cnt;
                        cnt++;
                    } else {
                        if (t >= out[max_hits - 1].t) continue;
                        pos = max_hits - 1;
                    }
                    while (pos > 0 && out[pos - 1].t > t) {
                        out[pos] = out[pos - 1];
                        pos--;
                    }
                    out[pos].t = t;
                    out[pos].u = u;
                    out[pos].v = v;
                    out[pos].prim_id = pid;
                    if (cnt == max_hits) prune_t = out[max_hits - 1].t;
                }
            }
            continue;
        }
        float dec[48];
        tri_node_view nv;
        tri_node_load(s, e.ref, dec, &nv);
        uint32_t cref[8];
        float cd[8];
        int m = 0;
        for (int i = 0; i < nv.n; i++) {
            float tnear;
            if (tri_slab_scalar(nv.lo_x, nv.lo_y, nv.lo_z, nv.hi_x, nv.hi_y,
                                nv.hi_z, i, &rc, prune_t, &tnear)) {
                int j = m++;
                while (j > 0 && cd[j - 1] < tnear) {
                    cd[j] = cd[j - 1];
                    cref[j] = cref[j - 1];
                    j--;
                }
                cd[j] = tnear;
                cref[j] = nv.child[i];
            }
        }
        if (sp + m > TRI_STACK_SIZE) m = TRI_STACK_SIZE - sp;
        for (int i = 0; i < m; i++) {
            stack[sp].ref = cref[i];
            stack[sp].tnear = cd[i];
            sp++;
        }
    }
    return cnt;
}

/* --------------------------------------------------------------------------
 * Curve (hair) scenes: capsules subdivided into short sub-segments so their
 * AABBs are tight, then run through the regular binary SAH build + BVH4
 * collapse with capsule leaf blocks.
 * ------------------------------------------------------------------------ */
#define TRI_CURVE_MAX_SUBDIV 32u

lrt_tri_scene *lrt_curve_scene_build(const float *segments, const float *radii,
                                     float constant_radius, size_t nsegs,
                                     const lrt_tri_build_options *opts,
                                     lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!segments || nsegs == 0 || nsegs > 0x07FFFFFFu ||
        (!radii && !(constant_radius > 0.0f))) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }

    lrt_tri_build_options o;
    if (opts) {
        o = *opts;
    } else {
        memset(&o, 0, sizeof(o));
    }
    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    /* Pass 1: subdivision counts. Sub-segment length targets ~16 radii so
     * boxes hug the hair; capped to bound memory. */
    size_t nsub = 0;
    int bad_input = 0;
    for (size_t i = 0; i < nsegs; i++) {
        const float *p = &segments[i * 6];
        float r = radii ? radii[i] : constant_radius;
        for (int k = 0; k < 6; k++) {
            if (!isfinite(p[k])) bad_input = 1;
        }
        if (!isfinite(r) || r <= 0.0f) bad_input = 1;
        if (bad_input) break;
        float dx = p[3] - p[0], dy = p[4] - p[1], dz = p[5] - p[2];
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        uint32_t k = (uint32_t)(len / (16.0f * r));
        if (k < 1u) k = 1u;
        if (k > TRI_CURVE_MAX_SUBDIV) k = TRI_CURVE_MAX_SUBDIV;
        nsub += k;
    }
    if (bad_input) {
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }
    if (nsub > 0x07FFFFFFu) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    /* Pass 2: fill sub-segments + their bounds. */
    tri_subseg *subs = (tri_subseg *)malloc(nsub * sizeof(tri_subseg));
    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.subsegs = subs;
    bc.ntris = nsub;
    bc.max_leaf = max_leaf;
    bc.block_shift = 2u; /* capsule blocks are 4-wide */
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    bc.plo = (float *)malloc(nsub * 3 * sizeof(float));
    bc.phi = (float *)malloc(nsub * 3 * sizeof(float));
    bc.cen = (float *)malloc(nsub * 3 * sizeof(float));
    bc.indices = (uint32_t *)malloc(nsub * sizeof(uint32_t));
    uint32_t bnode_cap = (uint32_t)(2 * nsub) + 512u;
    bc.node_next = 0;
    bc.node_end = bnode_cap;
    bc.bnodes = (tri_bnode *)malloc((size_t)bnode_cap * sizeof(tri_bnode));
    if (!subs || !bc.plo || !bc.phi || !bc.cen || !bc.indices || !bc.bnodes) {
        free(subs);
        free(bc.plo);
        free(bc.phi);
        free(bc.cen);
        free(bc.indices);
        free(bc.bnodes);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    size_t w = 0;
    for (size_t i = 0; i < nsegs; i++) {
        const float *p = &segments[i * 6];
        float r = radii ? radii[i] : constant_radius;
        float dx = p[3] - p[0], dy = p[4] - p[1], dz = p[5] - p[2];
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        uint32_t k = (uint32_t)(len / (16.0f * r));
        if (k < 1u) k = 1u;
        if (k > TRI_CURVE_MAX_SUBDIV) k = TRI_CURVE_MAX_SUBDIV;
        for (uint32_t j = 0; j < k; j++, w++) {
            tri_subseg *ss = &subs[w];
            float f0 = (float)j / (float)k;
            float f1 = (float)(j + 1) / (float)k;
            for (int a = 0; a < 3; a++) {
                ss->p0[a] = p[a] + (p[3 + a] - p[a]) * f0;
                ss->p1[a] = p[a] + (p[3 + a] - p[a]) * f1;
            }
            ss->r = r;
            ss->u0 = f0;
            ss->u1 = f1;
            ss->prim = (uint32_t)i;
            for (int a = 0; a < 3; a++) {
                float lo = tri_minf(ss->p0[a], ss->p1[a]) - r;
                float hi = tri_maxf(ss->p0[a], ss->p1[a]) + r;
                bc.plo[w * 3 + a] = lo;
                bc.phi[w * 3 + a] = hi;
                bc.cen[w * 3 + a] = 0.5f * (lo + hi);
            }
            bc.indices[w] = (uint32_t)w;
        }
    }

#if !defined(__STDC_NO_THREADS__)
    if (num_threads > 1 && nsub >= 4096) {
        bc.par_threads = num_threads;
        bc.par_scratch = (uint32_t *)malloc(nsub * sizeof(uint32_t));
        if (!bc.par_scratch) bc.par_threads = 0;
    }
#endif

    uint32_t b_root = tri_build_binary(&bc, num_threads);

    lrt_tri_scene *s = NULL;
    if (!bc.failed) {
        s = (lrt_tri_scene *)calloc(1, sizeof(lrt_tri_scene));
        if (!s) bc.failed = 1;
    }
    if (!bc.failed) {
        s->layout = 4;
        s->curve = 1;
        s->prim_kind = TRI_PRIM_CURVE;
        s->block_stride = (uint32_t)sizeof(lrt_crv4);
        uint32_t node_cap, block_cap;
        tri_collapse_caps(&bc, 4, b_root, &node_cap, &block_cap);
        s->nodes4 = (lrt_bvh4_node *)tri_aligned_alloc(
            64, (size_t)node_cap * sizeof(lrt_bvh4_node));
        s->blocks = tri_aligned_alloc(64, (size_t)block_cap * sizeof(lrt_crv4));
        if (!s->nodes4 || !s->blocks) {
            bc.failed = 1;
        } else {
            tri_collapse_ctx cc;
            memset(&cc, 0, sizeof(cc));
            cc.bc = &bc;
            cc.s = s;
            cc.node_cap = node_cap;
            cc.block_cap = block_cap;
            cc.width = 4;
            const tri_bnode *rootn = &bc.bnodes[b_root];
            cc.root_area = tri_surface_area(rootn->lo, rootn->hi);
            if (cc.root_area <= 0.0f) cc.root_area = 1.0f;
            for (int a = 0; a < 3; a++) {
                s->root_lo[a] = rootn->lo[a];
                s->root_hi[a] = rootn->hi[a];
            }
            if (rootn->count > 0) {
                s->root = tri_emit_leaf(&cc, rootn);
            } else {
                s->root = tri_collapse(&cc, b_root, 0);
            }
            if (cc.failed) {
                bc.failed = 1;
            } else {
                s->stats.node_count = s->node_count;
                s->stats.leaf_count = cc.leaf_count;
                s->stats.max_depth = cc.max_depth;
                s->stats.memory_bytes =
                    (size_t)s->node_count * sizeof(lrt_bvh4_node) +
                    (size_t)s->block_count * sizeof(lrt_crv4);
                s->stats.sah_cost = (float)(TRI_TRAV_COST * cc.sah_inner +
                                            TRI_ISECT_COST * cc.sah_leaf);
            }
        }
    }

    free(subs);
    free(bc.plo);
    free(bc.phi);
    free(bc.cen);
    free(bc.indices);
    free(bc.bnodes);
    free(bc.par_scratch);

    if (bc.failed) {
        lrt_tri_scene_free(s);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

#if LRT_TRI_HAS_SSE4
    s->kernel_name = "curve-bvh4/sse4";
#else
    s->kernel_name = "curve-bvh4/scalar";
#endif
    return s;
}

/* Round-linear (Embree-style) hair scene: one tapered-cone segment per pair of
 * consecutive strand points, capped by end spheres, with CSG joint clipping
 * against the strand neighbors. No sub-segment subdivision (segments are short);
 * each leaf primitive is a full segment and reports its global index. */
lrt_tri_scene *lrt_roundcurve_scene_build(const lrt_hair_strands *strands,
                                          const lrt_tri_build_options *opts,
                                          lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!strands || !strands->points || !strands->strand_first ||
        !strands->strand_count || strands->nstrands == 0 ||
        strands->npoints == 0 ||
        (!strands->radius && !(strands->constant_radius > 0.0f))) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }

    const float *P = strands->points;
    const float *R = strands->radius;
    const float CR = strands->constant_radius;
    const uint32_t *sf = strands->strand_first;
    const uint32_t *scnt = strands->strand_count;
    const size_t npts = strands->npoints;

    lrt_tri_build_options o;
    if (opts) o = *opts;
    else memset(&o, 0, sizeof(o));
    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    /* Pass 1: count segments + range-check the strand offsets. */
    size_t nseg = 0;
    for (size_t i = 0; i < strands->nstrands; i++) {
        uint32_t first = sf[i], cnt = scnt[i];
        if (cnt < 2) continue;
        if ((size_t)first + (size_t)cnt > npts) {
            tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
            return NULL;
        }
        nseg += (size_t)(cnt - 1u);
    }
    if (nseg == 0) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    if (nseg > 0x07FFFFFFu) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    tri_rlcseg *segs = (tri_rlcseg *)malloc(nseg * sizeof(tri_rlcseg));
    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.rlcsegs = segs;
    bc.ntris = nseg;
    bc.max_leaf = max_leaf;
    bc.block_shift = 2u; /* round-linear blocks are 4-wide */
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    bc.emit_kind = TRI_PRIM_RLCURVE;
    bc.plo = (float *)malloc(nseg * 3 * sizeof(float));
    bc.phi = (float *)malloc(nseg * 3 * sizeof(float));
    bc.cen = (float *)malloc(nseg * 3 * sizeof(float));
    bc.indices = (uint32_t *)malloc(nseg * sizeof(uint32_t));
    uint32_t bnode_cap = (uint32_t)(2 * nseg) + 512u;
    bc.node_next = 0;
    bc.node_end = bnode_cap;
    bc.bnodes = (tri_bnode *)malloc((size_t)bnode_cap * sizeof(tri_bnode));
    if (!segs || !bc.plo || !bc.phi || !bc.cen || !bc.indices || !bc.bnodes) {
        free(segs);
        free(bc.plo);
        free(bc.phi);
        free(bc.cen);
        free(bc.indices);
        free(bc.bnodes);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    /* Pass 2: build segments + per-segment cone AABBs (box around end spheres).
     * Neighbor px == +INF marks a strand endpoint (no joint to clip against). */
    const float INF = TRI_INF_F;
    int bad_input = 0;
    size_t w = 0;
    for (size_t i = 0; i < strands->nstrands; i++) {
        uint32_t first = sf[i], cnt = scnt[i];
        if (cnt < 2) continue;
        for (uint32_t j = 0; j + 1u < cnt; j++, w++) {
            tri_rlcseg *rs = &segs[w];
            uint32_t i0 = first + j, i1 = first + j + 1u;
            float r0 = R ? R[i0] : CR;
            float r1 = R ? R[i1] : CR;
            for (int a = 0; a < 3; a++) {
                rs->p0[a] = P[(size_t)i0 * 3 + a];
                rs->p1[a] = P[(size_t)i1 * 3 + a];
            }
            rs->r0 = r0;
            rs->r1 = r1;
            if (j > 0u) {
                uint32_t il = first + j - 1u;
                for (int a = 0; a < 3; a++) rs->pL[a] = P[(size_t)il * 3 + a];
                rs->rL = R ? R[il] : CR;
            } else {
                rs->pL[0] = rs->pL[1] = rs->pL[2] = INF;
                rs->rL = 0.0f;
            }
            if (j + 2u < cnt) {
                uint32_t ir = first + j + 2u;
                for (int a = 0; a < 3; a++) rs->pR[a] = P[(size_t)ir * 3 + a];
                rs->rR = R ? R[ir] : CR;
            } else {
                rs->pR[0] = rs->pR[1] = rs->pR[2] = INF;
                rs->rR = 0.0f;
            }
            rs->prim = (uint32_t)w;

            for (int a = 0; a < 3; a++) {
                if (!isfinite(rs->p0[a]) || !isfinite(rs->p1[a])) bad_input = 1;
            }
            if (!isfinite(r0) || !isfinite(r1) || (r0 <= 0.0f && r1 <= 0.0f))
                bad_input = 1;
            float r0v = r0 > 0.0f ? r0 : 0.0f, r1v = r1 > 0.0f ? r1 : 0.0f;
            for (int a = 0; a < 3; a++) {
                float lo = tri_minf(rs->p0[a] - r0v, rs->p1[a] - r1v);
                float hi = tri_maxf(rs->p0[a] + r0v, rs->p1[a] + r1v);
                bc.plo[w * 3 + a] = lo;
                bc.phi[w * 3 + a] = hi;
                bc.cen[w * 3 + a] = 0.5f * (lo + hi);
            }
            bc.indices[w] = (uint32_t)w;
        }
    }
    if (bad_input) {
        free(segs);
        free(bc.plo);
        free(bc.phi);
        free(bc.cen);
        free(bc.indices);
        free(bc.bnodes);
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }

#if !defined(__STDC_NO_THREADS__)
    if (num_threads > 1 && nseg >= 4096) {
        bc.par_threads = num_threads;
        bc.par_scratch = (uint32_t *)malloc(nseg * sizeof(uint32_t));
        if (!bc.par_scratch) bc.par_threads = 0;
    }
#endif

    uint32_t b_root = tri_build_binary(&bc, num_threads);

    lrt_tri_scene *s = NULL;
    if (!bc.failed) {
        s = (lrt_tri_scene *)calloc(1, sizeof(lrt_tri_scene));
        if (!s) bc.failed = 1;
    }
    if (!bc.failed) {
        s->layout = 4;
        s->curve = 0; /* dispatched via prim_kind, not the legacy curve flag */
        s->prim_kind = TRI_PRIM_RLCURVE;
        s->block_stride = (uint32_t)sizeof(lrt_rlc4);
        uint32_t node_cap, block_cap;
        tri_collapse_caps(&bc, 4, b_root, &node_cap, &block_cap);
        s->nodes4 = (lrt_bvh4_node *)tri_aligned_alloc(
            64, (size_t)node_cap * sizeof(lrt_bvh4_node));
        s->blocks = tri_aligned_alloc(64, (size_t)block_cap * sizeof(lrt_rlc4));
        if (!s->nodes4 || !s->blocks) {
            bc.failed = 1;
        } else {
            tri_collapse_ctx cc;
            memset(&cc, 0, sizeof(cc));
            cc.bc = &bc;
            cc.s = s;
            cc.node_cap = node_cap;
            cc.block_cap = block_cap;
            cc.width = 4;
            const tri_bnode *rootn = &bc.bnodes[b_root];
            cc.root_area = tri_surface_area(rootn->lo, rootn->hi);
            if (cc.root_area <= 0.0f) cc.root_area = 1.0f;
            for (int a = 0; a < 3; a++) {
                s->root_lo[a] = rootn->lo[a];
                s->root_hi[a] = rootn->hi[a];
            }
            if (rootn->count > 0) {
                s->root = tri_emit_leaf(&cc, rootn);
            } else {
                s->root = tri_collapse(&cc, b_root, 0);
            }
            if (cc.failed) {
                bc.failed = 1;
            } else {
                s->stats.node_count = s->node_count;
                s->stats.leaf_count = cc.leaf_count;
                s->stats.max_depth = cc.max_depth;
                s->stats.memory_bytes =
                    (size_t)s->node_count * sizeof(lrt_bvh4_node) +
                    (size_t)s->block_count * sizeof(lrt_rlc4);
                s->stats.sah_cost = (float)(TRI_TRAV_COST * cc.sah_inner +
                                            TRI_ISECT_COST * cc.sah_leaf);
            }
        }
    }

    free(segs);
    free(bc.plo);
    free(bc.phi);
    free(bc.cen);
    free(bc.indices);
    free(bc.bnodes);
    free(bc.par_scratch);

    if (bc.failed) {
        lrt_tri_scene_free(s);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

#if LRT_TRI_HAS_SSE4
    s->kernel_name = "rlcurve-bvh4/sse4";
#else
    s->kernel_name = "rlcurve-bvh4/scalar";
#endif
    return s;
}

/* aabb-scene scratch helpers (defined with the custom-geometry builders below) */
static int tri_alloc_aabb_scratch(tri_build_ctx *bc, size_t nprims);
static void tri_free_aabb_scratch(tri_build_ctx *bc);

/* Flat (ribbon) linear curve scene: one ray-facing ribbon per strand segment
 * (varying radius). Same strand input + per-segment cone AABB as the round
 * curves, but no neighbor data (ribbons do not CSG-clip joints). */
lrt_tri_scene *lrt_flatcurve_scene_build(const lrt_hair_strands *strands,
                                         const lrt_tri_build_options *opts,
                                         lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!strands || !strands->points || !strands->strand_first ||
        !strands->strand_count || strands->nstrands == 0 ||
        strands->npoints == 0 ||
        (!strands->radius && !(strands->constant_radius > 0.0f))) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    const float *P = strands->points;
    const float *R = strands->radius;
    const float CR = strands->constant_radius;
    const uint32_t *sf = strands->strand_first;
    const uint32_t *scnt = strands->strand_count;
    const size_t npts = strands->npoints;

    lrt_tri_build_options o;
    if (opts) o = *opts;
    else memset(&o, 0, sizeof(o));
    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    size_t nseg = 0;
    for (size_t i = 0; i < strands->nstrands; i++) {
        uint32_t first = sf[i], cnt = scnt[i];
        if (cnt < 2) continue;
        if ((size_t)first + (size_t)cnt > npts) {
            tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
            return NULL;
        }
        nseg += (size_t)(cnt - 1u);
    }
    if (nseg == 0) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    if (nseg > 0x07FFFFFFu) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    tri_rlcseg *segs = (tri_rlcseg *)calloc(nseg, sizeof(tri_rlcseg));
    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.rlcsegs = segs;
    bc.ntris = nseg;
    bc.max_leaf = max_leaf;
    bc.block_shift = 2u;
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    bc.emit_kind = TRI_PRIM_FLATCURVE;
    if (!segs || tri_alloc_aabb_scratch(&bc, nseg)) {
        free(segs);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    int bad_input = 0;
    size_t w = 0;
    for (size_t i = 0; i < strands->nstrands; i++) {
        uint32_t first = sf[i], cnt = scnt[i];
        if (cnt < 2) continue;
        for (uint32_t j = 0; j + 1u < cnt; j++, w++) {
            tri_rlcseg *rs = &segs[w];
            uint32_t i0 = first + j, i1 = first + j + 1u;
            float r0 = R ? R[i0] : CR;
            float r1 = R ? R[i1] : CR;
            for (int a = 0; a < 3; a++) {
                rs->p0[a] = P[(size_t)i0 * 3 + a];
                rs->p1[a] = P[(size_t)i1 * 3 + a];
            }
            rs->r0 = r0;
            rs->r1 = r1;
            rs->prim = (uint32_t)w;
            for (int a = 0; a < 3; a++) {
                if (!isfinite(rs->p0[a]) || !isfinite(rs->p1[a])) bad_input = 1;
            }
            if (!isfinite(r0) || !isfinite(r1) || (r0 <= 0.0f && r1 <= 0.0f))
                bad_input = 1;
            float r0v = r0 > 0.0f ? r0 : 0.0f, r1v = r1 > 0.0f ? r1 : 0.0f;
            for (int a = 0; a < 3; a++) {
                float lo = tri_minf(rs->p0[a] - r0v, rs->p1[a] - r1v);
                float hi = tri_maxf(rs->p0[a] + r0v, rs->p1[a] + r1v);
                bc.plo[w * 3 + a] = lo;
                bc.phi[w * 3 + a] = hi;
                bc.cen[w * 3 + a] = 0.5f * (lo + hi);
            }
            bc.indices[w] = (uint32_t)w;
        }
    }
    if (bad_input) {
        tri_free_aabb_scratch(&bc);
        free(segs);
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }

    lrt_tri_scene *s = NULL;
    {
        /* like tri_finish_aabb_build but with the flat block stride */
#if !defined(__STDC_NO_THREADS__)
        if (num_threads > 1 && nseg >= 4096) {
            bc.par_threads = num_threads;
            bc.par_scratch = (uint32_t *)malloc(nseg * sizeof(uint32_t));
            if (!bc.par_scratch) bc.par_threads = 0;
        }
#endif
        uint32_t b_root = tri_build_binary(&bc, num_threads);
        if (!bc.failed) {
            s = (lrt_tri_scene *)calloc(1, sizeof(lrt_tri_scene));
            if (!s) bc.failed = 1;
        }
        if (!bc.failed) {
            s->layout = 4;
            s->prim_kind = TRI_PRIM_FLATCURVE;
            s->block_stride = (uint32_t)sizeof(lrt_flat4);
            uint32_t node_cap, block_cap;
            tri_collapse_caps(&bc, 4, b_root, &node_cap, &block_cap);
            s->nodes4 = (lrt_bvh4_node *)tri_aligned_alloc(
                64, (size_t)node_cap * sizeof(lrt_bvh4_node));
            s->blocks =
                tri_aligned_alloc(64, (size_t)block_cap * sizeof(lrt_flat4));
            if (!s->nodes4 || !s->blocks)
                bc.failed = 1;
            else if (tri_collapse_into(s, &bc, b_root, node_cap, block_cap))
                bc.failed = 1;
        }
    }
    tri_free_aabb_scratch(&bc);
    free(segs);
    if (bc.failed) {
        lrt_tri_scene_free(s);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
#if LRT_TRI_HAS_SSE4
    s->kernel_name = "flatcurve-bvh4/sse4";
#else
    s->kernel_name = "flatcurve-bvh4/scalar";
#endif
    return s;
}

/* Round cubic-Bezier curve scene. cps = 16*nseg floats (per segment: 4 control
 * points, each xyz + radius). Each segment is an independent cubic tube; share
 * endpoints across segments for C0 continuity. */
lrt_tri_scene *lrt_bezcurve_scene_build(const float *cps, size_t nseg,
                                        const lrt_tri_build_options *opts,
                                        lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!cps || nseg == 0 || nseg > 0x07FFFFFFu) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    lrt_tri_build_options o;
    if (opts) o = *opts;
    else memset(&o, 0, sizeof(o));
    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    /* Pre-subdivide each cubic into TRI_BEZ_BUILD_SPLIT tight sub-arcs (de
     * Casteljau) so the BVH culls well. The sub-cubics are computed on the fly
     * (here for AABBs, again in tri_emit_leaf) rather than materialized into a
     * temporary array - the original cubics are the only geometry input. The hit
     * keeps the ORIGINAL segment id (seg = sub / K). */
    const uint32_t K = TRI_BEZ_BUILD_SPLIT;
    if (nseg > 0x07FFFFFFu / K) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    size_t nsub = nseg * (size_t)K;

    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.ntris = nsub;
    bc.max_leaf = max_leaf;
    bc.block_shift = 2u;
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    bc.bez_cps = cps;
    bc.bez_split = K;
    bc.emit_kind = TRI_PRIM_BEZCURVE;
    if (tri_alloc_aabb_scratch(&bc, nsub)) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    /* AABB = box around the sub-cubic's 4 control points dilated by their radii
     * (the convex hull of the control polygon contains the curve). */
    int bad = 0;
    for (size_t i = 0; i < nsub; i++) {
        float cp[16];
        uint32_t seg = (uint32_t)(i / K), kk = (uint32_t)(i % K);
        tri_bez_subcurve(&cps[(size_t)seg * 16], (float)kk / (float)K,
                         (float)(kk + 1) / (float)K, cp);
        float lo[3] = {TRI_INF_F, TRI_INF_F, TRI_INF_F};
        float hi[3] = {-TRI_INF_F, -TRI_INF_F, -TRI_INF_F};
        for (int k = 0; k < 4; k++) {
            float rr = cp[k * 4 + 3] > 0.0f ? cp[k * 4 + 3] : 0.0f;
            for (int a = 0; a < 3; a++) {
                float c = cp[k * 4 + a];
                if (!isfinite(c)) bad = 1;
                if (c - rr < lo[a]) lo[a] = c - rr;
                if (c + rr > hi[a]) hi[a] = c + rr;
            }
            if (!isfinite(cp[k * 4 + 3])) bad = 1;
        }
        for (int a = 0; a < 3; a++) {
            bc.plo[i * 3 + a] = lo[a];
            bc.phi[i * 3 + a] = hi[a];
            bc.cen[i * 3 + a] = 0.5f * (lo[a] + hi[a]);
        }
        bc.indices[i] = (uint32_t)i;
    }
    if (bad) {
        tri_free_aabb_scratch(&bc);
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }

    lrt_tri_scene *s = NULL;
#if !defined(__STDC_NO_THREADS__)
    if (num_threads > 1 && nsub >= 4096) {
        bc.par_threads = num_threads;
        bc.par_scratch = (uint32_t *)malloc(nsub * sizeof(uint32_t));
        if (!bc.par_scratch) bc.par_threads = 0;
    }
#endif
    uint32_t b_root = tri_build_binary(&bc, num_threads);
    if (!bc.failed) {
        s = (lrt_tri_scene *)calloc(1, sizeof(lrt_tri_scene));
        if (!s) bc.failed = 1;
    }
    if (!bc.failed) {
        s->layout = 4;
        s->prim_kind = TRI_PRIM_BEZCURVE;
        s->block_stride = (uint32_t)sizeof(lrt_bez4);
        uint32_t node_cap, block_cap;
        tri_collapse_caps(&bc, 4, b_root, &node_cap, &block_cap);
        s->nodes4 = (lrt_bvh4_node *)tri_aligned_alloc(
            64, (size_t)node_cap * sizeof(lrt_bvh4_node));
        s->blocks = tri_aligned_alloc(64, (size_t)block_cap * sizeof(lrt_bez4));
        if (!s->nodes4 || !s->blocks)
            bc.failed = 1;
        else if (tri_collapse_into(s, &bc, b_root, node_cap, block_cap))
            bc.failed = 1;
    }
    tri_free_aabb_scratch(&bc);
    if (bc.failed) {
        lrt_tri_scene_free(s);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
#if LRT_TRI_HAS_SSE4
    s->kernel_name = "bezcurve-bvh4/sse4";
#else
    s->kernel_name = "bezcurve-bvh4/scalar";
#endif
    return s;
}

/* --------------------------------------------------------------------------
 * Custom geometry + analytic sphere scenes. Both feed already-computed AABBs
 * through the same binary SAH build + BVH4 collapse as triangles/curves, with
 * a dedicated leaf-block type and leaf kernel. Layout is forced to BVH4 and
 * quality to the binary SAH (spatial splits / LBVH are triangle-specific).
 * ------------------------------------------------------------------------ */

/* Shared tail for the AABB-driven builders: run the binary build, allocate the
 * BVH4 nodes/blocks, collapse, finalize. bc.plo/phi/cen/indices/bnodes are
 * filled and owned by the caller (freed by it afterward). prim_kind selects the
 * leaf kernel; kernel_name is set accordingly. */
static lrt_tri_scene *tri_finish_aabb_build(tri_build_ctx *bc, size_t nprims,
                                            int prim_kind, unsigned num_threads,
                                            lrt_result *err) {
#if !defined(__STDC_NO_THREADS__)
    if (num_threads > 1 && nprims >= 4096) {
        bc->par_threads = num_threads;
        bc->par_scratch = (uint32_t *)malloc(nprims * sizeof(uint32_t));
        if (!bc->par_scratch) bc->par_threads = 0;
    }
#endif
    uint32_t b_root = tri_build_binary(bc, num_threads);

    lrt_tri_scene *s = NULL;
    if (!bc->failed) {
        s = (lrt_tri_scene *)calloc(1, sizeof(lrt_tri_scene));
        if (!s) bc->failed = 1;
    }
    if (!bc->failed) {
        s->layout = 4;
        s->prim_kind = prim_kind;
        if (!s->block_stride) s->block_stride = (uint32_t)tri_block_size(4);
        uint32_t node_cap, block_cap;
        tri_collapse_caps(bc, 4, b_root, &node_cap, &block_cap);
        (void)nprims;
        s->nodes4 = (lrt_bvh4_node *)tri_aligned_alloc(
            64, (size_t)node_cap * sizeof(lrt_bvh4_node));
        s->blocks = tri_aligned_alloc(64, (size_t)block_cap * s->block_stride);
        if (!s->nodes4 || !s->blocks) {
            bc->failed = 1;
        } else if (tri_collapse_into(s, bc, b_root, node_cap, block_cap)) {
            bc->failed = 1;
        }
    }
    if (bc->failed) {
        lrt_tri_scene_free(s);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
#if LRT_TRI_HAS_SSE4
    s->kernel_name =
        prim_kind == TRI_PRIM_SPHERE ? "sphere-bvh4/sse4" : "user-bvh4/sse4";
#else
    s->kernel_name = prim_kind == TRI_PRIM_SPHERE ? "sphere-bvh4/scalar"
                                                  : "user-bvh4/scalar";
#endif
    return s;
}

/* Allocate the build scratch (plo/phi/cen/indices/bnodes) for an nprims AABB
 * scene. Returns 0 on success (fields set in *bc), 1 on OOM (nothing leaked). */
static int tri_alloc_aabb_scratch(tri_build_ctx *bc, size_t nprims) {
    bc->plo = (float *)malloc(nprims * 3 * sizeof(float));
    bc->phi = (float *)malloc(nprims * 3 * sizeof(float));
    bc->cen = (float *)malloc(nprims * 3 * sizeof(float));
    bc->indices = (uint32_t *)malloc(nprims * sizeof(uint32_t));
    uint32_t bnode_cap = (uint32_t)(2 * nprims) + 512u;
    bc->node_next = 0;
    bc->node_end = bnode_cap;
    bc->bnodes = (tri_bnode *)malloc((size_t)bnode_cap * sizeof(tri_bnode));
    if (!bc->plo || !bc->phi || !bc->cen || !bc->indices || !bc->bnodes) {
        free(bc->plo);
        free(bc->phi);
        free(bc->cen);
        free(bc->indices);
        free(bc->bnodes);
        return 1;
    }
    return 0;
}

static void tri_free_aabb_scratch(tri_build_ctx *bc) {
    free(bc->plo);
    free(bc->phi);
    free(bc->cen);
    free(bc->indices);
    free(bc->bnodes);
    free(bc->par_scratch);
}

lrt_tri_scene *lrt_user_scene_build(const float *aabbs, size_t nprims,
                                    lrt_user_intersect_cb isect,
                                    lrt_user_occluded_cb occ, void *user,
                                    const lrt_tri_build_options *opts,
                                    lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!aabbs || !isect || nprims == 0 || nprims > 0x07FFFFFFu) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    lrt_tri_build_options o;
    if (opts) {
        o = *opts;
    } else {
        memset(&o, 0, sizeof(o));
    }
    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.ntris = nprims;
    bc.max_leaf = max_leaf;
    bc.block_shift = 2u;
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    bc.user_aabbs = aabbs;
    bc.emit_kind = TRI_PRIM_USER;
    if (tri_alloc_aabb_scratch(&bc, nprims)) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    int bad = 0;
    for (size_t i = 0; i < nprims; i++) {
        const float *b = &aabbs[i * 6];
        for (int k = 0; k < 6; k++)
            if (!isfinite(b[k])) bad = 1;
        for (int a = 0; a < 3; a++) {
            float lo = b[a], hi = b[3 + a];
            if (lo > hi) {
                float t = lo;
                lo = hi;
                hi = t;
            }
            bc.plo[i * 3 + a] = lo;
            bc.phi[i * 3 + a] = hi;
            bc.cen[i * 3 + a] = 0.5f * (lo + hi);
        }
        bc.indices[i] = (uint32_t)i;
    }
    if (bad) {
        tri_free_aabb_scratch(&bc);
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }

    lrt_tri_scene *s =
        tri_finish_aabb_build(&bc, nprims, TRI_PRIM_USER, num_threads, err);
    if (s) {
        s->user_isect = isect;
        s->user_occ = occ;
        s->user_ptr = user;
    }
    tri_free_aabb_scratch(&bc);
    return s;
}

lrt_tri_scene *lrt_sphere_scene_build(const float *spheres, size_t nprims,
                                      const lrt_tri_build_options *opts,
                                      lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!spheres || nprims == 0 || nprims > 0x07FFFFFFu) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    lrt_tri_build_options o;
    if (opts) {
        o = *opts;
    } else {
        memset(&o, 0, sizeof(o));
    }
    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.ntris = nprims;
    bc.max_leaf = max_leaf;
    bc.block_shift = 2u;
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    bc.spheres = spheres;
    bc.emit_kind = TRI_PRIM_SPHERE;
    if (tri_alloc_aabb_scratch(&bc, nprims)) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    int bad = 0;
    for (size_t i = 0; i < nprims; i++) {
        const float *sp = &spheres[i * 4];
        for (int k = 0; k < 4; k++)
            if (!isfinite(sp[k])) bad = 1;
        float rr = sp[3] > 0.0f ? sp[3] : 0.0f; /* r<=0: degenerate point box */
        for (int a = 0; a < 3; a++) {
            bc.plo[i * 3 + a] = sp[a] - rr;
            bc.phi[i * 3 + a] = sp[a] + rr;
            bc.cen[i * 3 + a] = sp[a];
        }
        bc.indices[i] = (uint32_t)i;
    }
    if (bad) {
        tri_free_aabb_scratch(&bc);
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }

    lrt_tri_scene *s =
        tri_finish_aabb_build(&bc, nprims, TRI_PRIM_SPHERE, num_threads, err);
    tri_free_aabb_scratch(&bc);
    return s;
}

lrt_tri_scene *lrt_points_scene_build(const float *centers, const float *radii,
                                      const float *normals, int point_type,
                                      size_t nprims,
                                      const lrt_tri_build_options *opts,
                                      lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!centers || !radii || nprims == 0 || nprims > 0x07FFFFFFu ||
        point_type < TRI_POINT_SPHERE || point_type > TRI_POINT_ORIENTED_DISC ||
        (point_type == TRI_POINT_ORIENTED_DISC && !normals)) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    lrt_tri_build_options o;
    if (opts) o = *opts;
    else memset(&o, 0, sizeof(o));
    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.ntris = nprims;
    bc.max_leaf = max_leaf;
    bc.block_shift = 2u;
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    bc.points_xyz = centers;
    bc.points_r = radii;
    bc.points_n = normals;
    bc.emit_kind = TRI_PRIM_POINT;
    if (tri_alloc_aabb_scratch(&bc, nprims)) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }

    int bad = 0;
    for (size_t i = 0; i < nprims; i++) {
        const float *c = &centers[i * 3];
        float r = radii[i];
        if (!isfinite(c[0]) || !isfinite(c[1]) || !isfinite(c[2]) ||
            !isfinite(r))
            bad = 1;
        float rr = r > 0.0f ? r : 0.0f;
        for (int a = 0; a < 3; a++) {
            bc.plo[i * 3 + a] = c[a] - rr;
            bc.phi[i * 3 + a] = c[a] + rr;
            bc.cen[i * 3 + a] = c[a];
        }
        bc.indices[i] = (uint32_t)i;
    }
    if (bad) {
        tri_free_aabb_scratch(&bc);
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }

    lrt_tri_scene *s =
        tri_finish_aabb_build(&bc, nprims, TRI_PRIM_POINT, num_threads, err);
    if (s) {
        s->point_type = point_type;
#if LRT_TRI_HAS_SSE4
        s->kernel_name = "point-bvh4/sse4";
#else
        s->kernel_name = "point-bvh4/scalar";
#endif
    }
    tri_free_aabb_scratch(&bc);
    return s;
}

/* --------------------------------------------------------------------------
 * Quantized triangle scenes (approximate / LOD / preview).
 * ------------------------------------------------------------------------ */

/* AABB of a quantized leaf, decoded (used to set node bounds that enclose the
 * decoded - and, conservative-mode, dilated - geometry). */
static void tri_qtri_leaf_box(const lrt_tri_scene *s, uint32_t ref, float lo[3],
                              float hi[3]) {
    lo[0] = lo[1] = lo[2] = TRI_INF_F;
    hi[0] = hi[1] = hi[2] = -TRI_INF_F;
    uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
    for (uint32_t b = 0; b < nblk; b++) {
        const void *blk = tri_block_ptr(s->blocks, blk0 + b, s->block_stride);
        for (int lane = 0; lane < 4; lane++) {
            if (tri_qtri_prim(s, blk, lane) == LRT_TRI_NO_HIT) continue;
            float v0[3], v1[3], v2[3];
            tri_qtri_decode_lane(s, blk, lane, v0, v1, v2);
            for (int a = 0; a < 3; a++) {
                float mn = v0[a] < v1[a] ? v0[a] : v1[a];
                mn = mn < v2[a] ? mn : v2[a];
                float mx = v0[a] > v1[a] ? v0[a] : v1[a];
                mx = mx > v2[a] ? mx : v2[a];
                if (mn < lo[a]) lo[a] = mn;
                if (mx > hi[a]) hi[a] = mx;
            }
        }
    }
}

/* Recompute every BVH4 node's child bounds from the DECODED leaf geometry
 * (mandatory: quantized/dilated verts drift outside the true AABB, so the build
 * bounds could cull the leaf holding the closest hit). Reverse-index bottom-up
 * (children have higher node indices). Returns 1 on OOM. */
static int tri_qtri_recompute_node_bounds(lrt_tri_scene *s) {
    if (s->node_count == 0) {
        tri_qtri_leaf_box(s, s->root, s->root_lo, s->root_hi);
        return 0;
    }
    float *nlo = (float *)malloc((size_t)s->node_count * 3 * sizeof(float));
    float *nhi = (float *)malloc((size_t)s->node_count * 3 * sizeof(float));
    if (!nlo || !nhi) {
        free(nlo);
        free(nhi);
        return 1;
    }
    for (uint32_t i = s->node_count; i-- > 0;) {
        lrt_bvh4_node *w = &s->nodes4[i];
        float blo[3] = {TRI_INF_F, TRI_INF_F, TRI_INF_F};
        float bhi[3] = {-TRI_INF_F, -TRI_INF_F, -TRI_INF_F};
        for (uint32_t c = 0; c < w->nchildren; c++) {
            float clo[3], chi[3];
            if (TRI_REF_IS_LEAF(w->child[c])) {
                tri_qtri_leaf_box(s, w->child[c], clo, chi);
            } else {
                uint32_t M = TRI_REF_NODE(w->child[c]);
                for (int a = 0; a < 3; a++) {
                    clo[a] = nlo[(size_t)M * 3 + a];
                    chi[a] = nhi[(size_t)M * 3 + a];
                }
            }
            w->lo_x[c] = clo[0]; w->lo_y[c] = clo[1]; w->lo_z[c] = clo[2];
            w->hi_x[c] = chi[0]; w->hi_y[c] = chi[1]; w->hi_z[c] = chi[2];
            for (int a = 0; a < 3; a++) {
                if (clo[a] < blo[a]) blo[a] = clo[a];
                if (chi[a] > bhi[a]) bhi[a] = chi[a];
            }
        }
        for (int a = 0; a < 3; a++) {
            nlo[(size_t)i * 3 + a] = blo[a];
            nhi[(size_t)i * 3 + a] = bhi[a];
        }
    }
    if (TRI_REF_IS_LEAF(s->root)) {
        tri_qtri_leaf_box(s, s->root, s->root_lo, s->root_hi);
    } else {
        uint32_t rn = TRI_REF_NODE(s->root);
        for (int a = 0; a < 3; a++) {
            s->root_lo[a] = nlo[(size_t)rn * 3 + a];
            s->root_hi[a] = nhi[(size_t)rn * 3 + a];
        }
    }
    free(nlo);
    free(nhi);
    return 0;
}

lrt_tri_scene *lrt_qtri_scene_build(const float *vertices, size_t ntris,
                                    lrt_qtri_format fmt, unsigned flags,
                                    const lrt_tri_build_options *opts,
                                    lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!vertices || ntris == 0 || ntris > 0x07FFFFFFu || fmt < 0 ||
        fmt > LRT_QTRI_FP4) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    lrt_tri_build_options o;
    if (opts) {
        o = *opts;
    } else {
        memset(&o, 0, sizeof(o));
    }
    unsigned num_threads = o.num_threads ? o.num_threads : 1u;
    uint32_t max_leaf = o.max_leaf_size ? o.max_leaf_size : TRI_DEFAULT_LEAF;
    if (max_leaf > TRI_MAX_LEAF) max_leaf = TRI_MAX_LEAF;

    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.verts = vertices;
    bc.ntris = ntris;
    bc.max_leaf = max_leaf;
    bc.block_shift = 2u; /* BVH4 */
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    bc.emit_kind = TRI_PRIM_QTRI;
    if (tri_alloc_aabb_scratch(&bc, ntris)) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    if (tri_precompute(&bc, num_threads)) {
        tri_free_aabb_scratch(&bc);
        tri_set_err(err, LRT_RESULT_INVALID_BOUNDS);
        return NULL;
    }
    uint32_t b_root = tri_build_binary(&bc, num_threads);

    lrt_tri_scene *s = NULL;
    if (!bc.failed) {
        s = (lrt_tri_scene *)calloc(1, sizeof(lrt_tri_scene));
        if (!s) bc.failed = 1;
    }
    if (!bc.failed) {
        s->layout = 4;
        s->prim_kind = TRI_PRIM_QTRI;
        s->qfmt = (int)fmt;
        s->qflags = flags;
        s->block_stride = lrt_qtri_block_stride((int)fmt);
        if (fmt == LRT_QTRI_Q16) {
            const tri_bnode *rt = &bc.bnodes[b_root];
            float mf = (flags & LRT_QTRI_CONSERVATIVE) ? tri_qtri_mfrac(fmt)
                                                       : 0.0f;
            for (int a = 0; a < 3; a++) {
                float ext = rt->hi[a] - rt->lo[a];
                float m = mf * (ext > 1e-20f ? ext : 1.0f);
                float glo = rt->lo[a] - m, gext = ext + 2.0f * m;
                s->qgrid_org[a] = glo;
                s->qgrid_scale[a] =
                    gext > 0.0f ? (gext / 65535.0f) * (1.0f + 4e-7f) : 0.0f;
            }
        }
        uint32_t node_cap, block_cap;
        tri_collapse_caps(&bc, 4, b_root, &node_cap, &block_cap);
        s->nodes4 = (lrt_bvh4_node *)tri_aligned_alloc(
            64, (size_t)node_cap * sizeof(lrt_bvh4_node));
        s->blocks = tri_aligned_alloc(64, (size_t)block_cap * s->block_stride);
        if (!s->nodes4 || !s->blocks) {
            bc.failed = 1;
        } else if (tri_collapse_into(s, &bc, b_root, node_cap, block_cap)) {
            bc.failed = 1;
        } else if (tri_qtri_recompute_node_bounds(s)) {
            bc.failed = 1;
        }
    }
    tri_free_aabb_scratch(&bc);
    if (bc.failed) {
        lrt_tri_scene_free(s);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    static const char *names[4] = {"qtri-q16-bvh4", "qtri-q8-bvh4",
                                   "qtri-fp8-bvh4", "qtri-fp4-bvh4"};
    s->kernel_name = names[fmt];
    return s;
}

/* --------------------------------------------------------------------------
 * Sphere tracing for implicit surfaces (signed distance fields).
 * ------------------------------------------------------------------------ */

int lrt_sdf_sphere_trace(const float org[3], const float dir[3], float tmin,
                         float tmax, lrt_sdf_cb sdf, void *user,
                         const lrt_sdf_params *params, lrt_sdf_hit *out) {
    if (out) {
        out->hit = 0;
        out->iters = 0;
    }
    if (!sdf || !org || !dir) return 0;
    lrt_sdf_params p;
    if (params) {
        p = *params;
    } else {
        memset(&p, 0, sizeof(p));
    }
    unsigned max_steps = p.max_steps ? p.max_steps : 128u;
    float eps = p.epsilon > 0.0f ? p.epsilon : 1e-4f;
    float omega = p.over_relax;
    if (!(omega >= 1.0f && omega < 2.0f)) omega = 1.0f;

    /* March along the normalized direction in Euclidean units; convert the
     * interval and the reported t to/from |dir| units so the result matches
     * the rest of the library. */
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len > 0.0f) || !isfinite(len)) return 0;
    float inv_len = 1.0f / len;
    float d[3] = {dir[0] * inv_len, dir[1] * inv_len, dir[2] * inv_len};
    float t0 = tmin * len, t1 = tmax * len;
    if (!(t1 > t0)) return 0;

    float t = t0;
    float ps[3] = {org[0] + t * d[0], org[1] + t * d[1], org[2] + t * d[2]};
    float sign = sdf(ps, user) < 0.0f ? -1.0f : 1.0f; /* support origin inside */
    float prev_radius = 0.0f, step = 0.0f;
    unsigned i = 0;
    int got = 0;
    for (; i < max_steps; i++) {
        float pp[3] = {org[0] + t * d[0], org[1] + t * d[1], org[2] + t * d[2]};
        float signed_radius = sign * sdf(pp, user);
        float radius = fabsf(signed_radius);
        int sor_fail = omega > 1.0f && (radius + prev_radius) < step;
        if (sor_fail) {
            step -= omega * step; /* undo the overshoot, fall back to classic */
            omega = 1.0f;
        } else {
            step = signed_radius * omega;
        }
        prev_radius = radius;
        float eps_t = p.t_eps_scale > 0.0f ? eps * (1.0f + p.t_eps_scale * t)
                                           : eps;
        if (!sor_fail && radius < eps_t) {
            got = 1;
            break;
        }
        t += step;
        if (t > t1) break; /* exited the interval: miss */
        if (t < t0) t = t0;
    }
    if (!got) return 0;
    if (out) {
        out->t = t * inv_len;
        out->p[0] = org[0] + t * d[0];
        out->p[1] = org[1] + t * d[1];
        out->p[2] = org[2] + t * d[2];
        out->iters = i;
        out->hit = 1;
        out->n[0] = out->n[1] = out->n[2] = 0.0f;
        if (p.normal_eps > 0.0f) {
            float h = p.normal_eps;
            float ex = out->p[0], ey = out->p[1], ez = out->p[2];
            float nx = 0.0f, ny = 0.0f, nz = 0.0f, s;
            /* tetrahedron finite differences (4 evals): n ~ grad(sdf) */
            float q0[3] = {ex + h, ey - h, ez - h};
            s = sdf(q0, user);
            nx += s;
            ny -= s;
            nz -= s;
            float q1[3] = {ex - h, ey - h, ez + h};
            s = sdf(q1, user);
            nx -= s;
            ny -= s;
            nz += s;
            float q2[3] = {ex - h, ey + h, ez - h};
            s = sdf(q2, user);
            nx -= s;
            ny += s;
            nz -= s;
            float q3[3] = {ex + h, ey + h, ez + h};
            s = sdf(q3, user);
            nx += s;
            ny += s;
            nz += s;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nl > 0.0f) {
                out->n[0] = nx / nl;
                out->n[1] = ny / nl;
                out->n[2] = nz / nl;
            }
        }
    }
    return 1;
}

/* BVH-accelerated SDF scene: each blob is a user primitive whose intersector
 * clips the ray to the blob AABB and sphere-traces within that interval. */
typedef struct sdf_table {
    size_t nblobs;
    lrt_sdf_params params;
    lrt_sdf_blob blobs[]; /* flexible array; one allocation, freed by owned_user */
} sdf_table;

/* Clip the ray to an AABB, returning the [t_enter, t_exit] sub-interval (in
 * |dir| units) intersected with [tmin, tmax]. Returns 0 if it misses. */
static int sdf_clip_aabb(const lrt_ray *ray, const float aabb[6],
                         float *t_enter, float *t_exit) {
    float te = ray->tmin, tx = ray->tmax;
    for (int a = 0; a < 3; a++) {
        float dd = ray->dir[a];
        float inv = dd != 0.0f ? 1.0f / dd : (dd < 0.0f ? -1e30f : 1e30f);
        float ta = (aabb[a] - ray->org[a]) * inv;
        float tb = (aabb[3 + a] - ray->org[a]) * inv;
        if (ta > tb) {
            float tmp = ta;
            ta = tb;
            tb = tmp;
        }
        if (ta > te) te = ta;
        if (tb < tx) tx = tb;
    }
    if (te > tx) return 0;
    *t_enter = te;
    *t_exit = tx;
    return 1;
}

static int sdf_blob_isect(const lrt_ray *ray, uint32_t prim, void *user,
                          float *t, float *u, float *v) {
    const sdf_table *T = (const sdf_table *)user;
    const lrt_sdf_blob *blob = &T->blobs[prim];
    float te, tx;
    if (!sdf_clip_aabb(ray, blob->aabb, &te, &tx)) return 0;
    lrt_sdf_hit hit;
    if (lrt_sdf_sphere_trace(ray->org, ray->dir, te, tx, blob->sdf, blob->user,
                             &T->params, &hit) &&
        hit.t >= ray->tmin && hit.t < ray->tmax) {
        *t = hit.t;
        *u = 0.0f;
        *v = 0.0f;
        return 1;
    }
    return 0;
}

lrt_tri_scene *lrt_sdf_scene_build(const lrt_sdf_blob *blobs, size_t nblobs,
                                   const lrt_sdf_params *params,
                                   const lrt_tri_build_options *opts,
                                   lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!blobs || nblobs == 0 || nblobs > 0x07FFFFFFu) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    sdf_table *T =
        (sdf_table *)malloc(sizeof(sdf_table) + nblobs * sizeof(lrt_sdf_blob));
    float *aabbs = (float *)malloc(nblobs * 6 * sizeof(float));
    if (!T || !aabbs) {
        free(T);
        free(aabbs);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    T->nblobs = nblobs;
    if (params) {
        T->params = *params;
    } else {
        memset(&T->params, 0, sizeof(T->params));
    }
    int bad = 0;
    for (size_t i = 0; i < nblobs; i++) {
        if (!blobs[i].sdf) bad = 1;
        T->blobs[i] = blobs[i];
        for (int k = 0; k < 6; k++) aabbs[i * 6 + k] = blobs[i].aabb[k];
    }
    if (bad) {
        free(T);
        free(aabbs);
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }

    lrt_tri_scene *s =
        lrt_user_scene_build(aabbs, nblobs, sdf_blob_isect, NULL, T, opts, err);
    free(aabbs);
    if (!s) {
        free(T);
        return NULL;
    }
    s->owned_user = T; /* freed by lrt_tri_scene_free */
    return s;
}

/* --------------------------------------------------------------------------
 * Serialization: save/load a built triangle or curve scene; zero-copy mmap.
 * Nodes and blocks are POD with internal indices, so they serialize as raw
 * bytes. User/sphere/SDF scenes hold callbacks and cannot be serialized.
 * ------------------------------------------------------------------------ */
#define LRT_TRI_FILE_VERSION 1u
#define LRT_TRI_FILE_ALIGN 64u

typedef struct lrt_tri_file_header {
    char magic[4]; /* 'L','R','T','S' */
    uint32_t version;
    uint32_t endian; /* 0x01020304 */
    uint32_t flags;  /* bit0 curve, bit1 quantized */
    uint32_t layout; /* 4 or 8 */
    uint32_t prim_kind;
    uint32_t node_count;
    uint32_t block_count;
    uint32_t root;
    uint32_t node_stride;
    uint32_t block_stride;
    uint32_t reserved0;
    float root_lo[3];
    float root_hi[3];
    uint64_t node_offset;
    uint64_t block_offset;
    uint64_t file_size;
} lrt_tri_file_header;
_Static_assert(sizeof(lrt_tri_file_header) == 96, "file header must be 96 bytes");

static size_t tri_align_up(size_t x, size_t a) {
    return (x + a - 1u) & ~(a - 1u);
}

static size_t tri_node_stride(int layout, int quantized) {
    return layout == 4    ? sizeof(lrt_bvh4_node)
           : quantized    ? sizeof(lrt_bvh8q_node)
                          : sizeof(lrt_bvh8_node);
}

static int tri_ref_in_range(uint32_t ref, uint32_t node_count,
                            uint32_t block_count) {
    if (TRI_REF_IS_LEAF(ref)) {
        uint64_t blk = TRI_REF_BLOCK(ref);
        uint64_t nb = TRI_REF_NBLOCKS(ref);
        return blk + nb <= (uint64_t)block_count;
    }
    return TRI_REF_NODE(ref) < node_count;
}

/* Walk every node's child refs (and root) checking they stay in bounds, so a
 * corrupt file cannot drive the hot traversal out of bounds. */
static int tri_validate_refs(const lrt_tri_scene *s) {
    /* node_count == 0 is valid: a tiny scene whose root is a single leaf ref. */
    if (!tri_ref_in_range(s->root, s->node_count, s->block_count)) return 0;
    unsigned maxc = s->layout == 4 ? 4u : 8u;
    for (uint32_t i = 0; i < s->node_count; i++) {
        const uint32_t *child;
        uint32_t nch;
        if (s->layout == 4) {
            child = s->nodes4[i].child;
            nch = s->nodes4[i].nchildren;
        } else if (s->quantized) {
            child = s->nodes8q[i].child;
            nch = s->nodes8q[i].nchildren;
        } else {
            child = s->nodes8[i].child;
            nch = s->nodes8[i].nchildren;
        }
        if (nch > maxc) return 0;
        for (uint32_t c = 0; c < nch; c++)
            if (!tri_ref_in_range(child[c], s->node_count, s->block_count))
                return 0;
    }
    return 1;
}

static const void *tri_scene_nodes_ptr(const lrt_tri_scene *s) {
    return s->layout == 4    ? (const void *)s->nodes4
           : s->quantized    ? (const void *)s->nodes8q
                             : (const void *)s->nodes8;
}

static void tri_set_kernel_name(lrt_tri_scene *s) {
    if (s->prim_kind == TRI_PRIM_CURVE) {
#if LRT_TRI_HAS_SSE4
        s->kernel_name = "curve-bvh4/sse4";
#else
        s->kernel_name = "curve-bvh4/scalar";
#endif
        return;
    }
#if LRT_TRI_HAS_AVX2
    s->kernel_name = s->layout == 4 ? "bvh4/sse4"
                     : s->quantized ? "bvh8q/avx2"
                                    : "bvh8/avx2";
#elif LRT_TRI_HAS_SSE4
    s->kernel_name = s->layout == 4 ? "bvh4/sse4"
                     : s->quantized ? "bvh8q/scalar"
                                    : "bvh8/scalar";
#else
    s->kernel_name = s->layout == 4 ? "bvh4/scalar"
                     : s->quantized ? "bvh8q/scalar"
                                    : "bvh8/scalar";
#endif
}

static void tri_recompute_stats(lrt_tri_scene *s) {
    s->stats.node_count = s->node_count;
    s->stats.leaf_count = 0; /* not retained across serialization */
    s->stats.max_depth = 0;
    s->stats.memory_bytes =
        (size_t)s->node_count * tri_node_stride(s->layout, s->quantized) +
        (size_t)s->block_count * s->block_stride;
    s->stats.sah_cost = 0.0f;
}

lrt_result lrt_tri_scene_save_to_memory(const lrt_tri_scene *s, void **buf,
                                        size_t *n) {
    if (!s || !buf || !n) return LRT_RESULT_INVALID_ARGUMENT;
    *buf = NULL;
    *n = 0;
    if (s->prim_kind != TRI_PRIM_TRI && s->prim_kind != TRI_PRIM_CURVE)
        return LRT_RESULT_INVALID_ARGUMENT; /* callbacks can't serialize */
    if (s->qnode != 0)
        return LRT_RESULT_INVALID_ARGUMENT; /* qnode format not in the v1 header */
    size_t nstride = tri_node_stride(s->layout, s->quantized);
    size_t bstride = s->block_stride;
    size_t node_off =
        tri_align_up(sizeof(lrt_tri_file_header), LRT_TRI_FILE_ALIGN);
    size_t nodes_bytes = (size_t)s->node_count * nstride;
    size_t block_off = tri_align_up(node_off + nodes_bytes, LRT_TRI_FILE_ALIGN);
    size_t blocks_bytes = (size_t)s->block_count * bstride;
    size_t total = block_off + blocks_bytes;
    unsigned char *out = (unsigned char *)calloc(1, total);
    if (!out) return LRT_RESULT_OUT_OF_MEMORY;
    lrt_tri_file_header h;
    memset(&h, 0, sizeof(h));
    h.magic[0] = 'L';
    h.magic[1] = 'R';
    h.magic[2] = 'T';
    h.magic[3] = 'S';
    h.version = LRT_TRI_FILE_VERSION;
    h.endian = 0x01020304u;
    h.flags = (s->prim_kind == TRI_PRIM_CURVE ? 1u : 0u) |
              (s->quantized ? 2u : 0u);
    h.layout = (uint32_t)s->layout;
    h.prim_kind = (uint32_t)s->prim_kind;
    h.node_count = s->node_count;
    h.block_count = s->block_count;
    h.root = s->root;
    h.node_stride = (uint32_t)nstride;
    h.block_stride = (uint32_t)bstride;
    for (int a = 0; a < 3; a++) {
        h.root_lo[a] = s->root_lo[a];
        h.root_hi[a] = s->root_hi[a];
    }
    h.node_offset = node_off;
    h.block_offset = block_off;
    h.file_size = total;
    memcpy(out, &h, sizeof(h));
    memcpy(out + node_off, tri_scene_nodes_ptr(s), nodes_bytes);
    memcpy(out + block_off, s->blocks, blocks_bytes);
    *buf = out;
    *n = total;
    return LRT_RESULT_OK;
}

lrt_result lrt_tri_scene_save(const lrt_tri_scene *s, const char *path) {
    if (!path) return LRT_RESULT_INVALID_ARGUMENT;
    void *buf;
    size_t n;
    lrt_result r = lrt_tri_scene_save_to_memory(s, &buf, &n);
    if (r != LRT_RESULT_OK) return r;
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(buf);
        return LRT_RESULT_INVALID_ARGUMENT;
    }
    size_t w = fwrite(buf, 1, n, fp);
    int ok = (fclose(fp) == 0);
    free(buf);
    return (w == n && ok) ? LRT_RESULT_OK : LRT_RESULT_OUT_OF_MEMORY;
}

static int tri_header_check(const lrt_tri_file_header *h, size_t n) {
    if (n < sizeof(*h)) return 1;
    if (h->magic[0] != 'L' || h->magic[1] != 'R' || h->magic[2] != 'T' ||
        h->magic[3] != 'S')
        return 1;
    if (h->version != LRT_TRI_FILE_VERSION) return 1;
    if (h->endian != 0x01020304u) return 1;
    if (h->layout != 4 && h->layout != 8) return 1;
    if (h->prim_kind != TRI_PRIM_TRI && h->prim_kind != TRI_PRIM_CURVE) return 1;
    int quantized = (h->flags & 2u) != 0;
    if (h->node_stride != tri_node_stride((int)h->layout, quantized)) return 1;
    if (h->block_stride != tri_block_size((int)h->layout)) return 1;
    if (h->file_size != (uint64_t)n) return 1;
    uint64_t nodes_bytes = (uint64_t)h->node_count * h->node_stride;
    uint64_t blocks_bytes = (uint64_t)h->block_count * h->block_stride;
    if (h->node_offset < sizeof(*h)) return 1;
    if (h->node_offset + nodes_bytes > h->block_offset) return 1;
    if (h->block_offset + blocks_bytes > h->file_size) return 1;
    return 0;
}

/* Build a scene from a header+payload buffer. copy=1 allocates and memcpys;
 * copy=0 points the scene into the (read-only) mapping at map_base. */
static lrt_tri_scene *tri_scene_from_buffer(const void *buf, size_t n, int copy,
                                            void *map_base, size_t map_size,
                                            lrt_result *err) {
    const lrt_tri_file_header *h = (const lrt_tri_file_header *)buf;
    if (tri_header_check(h, n)) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    lrt_tri_scene *s = (lrt_tri_scene *)calloc(1, sizeof(*s));
    if (!s) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    s->layout = (int)h->layout;
    s->quantized = (h->flags & 2u) != 0;
    s->curve = (h->flags & 1u) != 0;
    s->prim_kind = (int)h->prim_kind;
    s->root = h->root;
    s->node_count = h->node_count;
    s->block_count = h->block_count;
    s->block_stride = h->block_stride;
    for (int a = 0; a < 3; a++) {
        s->root_lo[a] = h->root_lo[a];
        s->root_hi[a] = h->root_hi[a];
    }
    const unsigned char *base = (const unsigned char *)buf;
    const void *nodes_src = base + h->node_offset;
    const void *blocks_src = base + h->block_offset;
    size_t nodes_bytes = (size_t)h->node_count * h->node_stride;
    size_t blocks_bytes = (size_t)h->block_count * h->block_stride;
    if (copy) {
        void *nodes = tri_aligned_alloc(64, nodes_bytes ? nodes_bytes : 64);
        void *blocks = tri_aligned_alloc(64, blocks_bytes ? blocks_bytes : 64);
        if (!nodes || !blocks) {
            tri_aligned_free(nodes);
            tri_aligned_free(blocks);
            free(s);
            tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
            return NULL;
        }
        memcpy(nodes, nodes_src, nodes_bytes);
        memcpy(blocks, blocks_src, blocks_bytes);
        if (s->layout == 4)
            s->nodes4 = (lrt_bvh4_node *)nodes;
        else if (s->quantized)
            s->nodes8q = (lrt_bvh8q_node *)nodes;
        else
            s->nodes8 = (lrt_bvh8_node *)nodes;
        s->blocks = blocks;
        s->mem_mapped = 0;
    } else {
        if (s->layout == 4)
            s->nodes4 = (lrt_bvh4_node *)(void *)nodes_src;
        else if (s->quantized)
            s->nodes8q = (lrt_bvh8q_node *)(void *)nodes_src;
        else
            s->nodes8 = (lrt_bvh8_node *)(void *)nodes_src;
        s->blocks = (void *)blocks_src;
        s->mem_mapped = 1;
        s->map_base = map_base;
        s->map_size = map_size;
    }
    if (!tri_validate_refs(s)) {
        if (copy) {
            lrt_tri_scene_free(s); /* frees the copied nodes/blocks */
        } else {
            free(s); /* caller unmaps map_base */
        }
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    tri_set_kernel_name(s);
    tri_recompute_stats(s);
    tri_set_err(err, LRT_RESULT_OK);
    return s;
}

lrt_tri_scene *lrt_tri_scene_load_from_memory(const void *buf, size_t n,
                                              lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!buf) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    return tri_scene_from_buffer(buf, n, 1, NULL, 0, err);
}

lrt_tri_scene *lrt_tri_scene_load(const char *path, lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!path) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(fp);
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    void *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (rd != (size_t)sz) {
        free(buf);
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    lrt_tri_scene *s = tri_scene_from_buffer(buf, (size_t)sz, 1, NULL, 0, err);
    free(buf);
    return s;
}

lrt_tri_scene *lrt_tri_scene_open_mmap(const char *path, lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!path) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
#if LRT_TRI_HAS_MMAP
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    struct stat stt;
    if (fstat(fd, &stt) != 0 || stt.st_size <= 0) {
        close(fd);
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    size_t sz = (size_t)stt.st_size;
    void *m = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    lrt_tri_scene *s = tri_scene_from_buffer(m, sz, 0, m, sz, err);
    if (!s) munmap(m, sz);
    return s;
#else
    (void)path;
    tri_set_err(err, LRT_RESULT_NOT_BUILT);
    return NULL;
#endif
}

/* --------------------------------------------------------------------------
 * Refit: update triangle positions in place and recompute node bounds without
 * changing the tree topology. Relies on (a) leaf blocks storing the original
 * prim id per lane, and (b) the parent-before-child node order produced by the
 * collapse (so a single reverse-index pass recomputes bounds bottom-up).
 * ------------------------------------------------------------------------ */

/* AABB of a leaf reference, from its (already-updated) block vertices. */
static void tri_leaf_box(const lrt_tri_scene *s, uint32_t ref, float lo[3],
                         float hi[3]) {
    lo[0] = lo[1] = lo[2] = TRI_INF_F;
    hi[0] = hi[1] = hi[2] = -TRI_INF_F;
    int width = s->layout;
    uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
    for (uint32_t b = 0; b < nblk; b++) {
        const float *f = tri_block_floats(s->blocks, blk0 + b, width);
        const uint32_t *ids = (const uint32_t *)(f + 9 * width);
        for (int lane = 0; lane < width; lane++) {
            if (ids[lane] == LRT_TRI_NO_HIT) continue;
            float plo[3], phi[3];
            tri_leaf_prim_aabb(f, width, lane, plo, phi);
            for (int a = 0; a < 3; a++) {
                if (plo[a] < lo[a]) lo[a] = plo[a];
                if (phi[a] > hi[a]) hi[a] = phi[a];
            }
        }
    }
}

lrt_result lrt_tri_scene_refit(lrt_tri_scene *s, const float *vertices,
                               size_t ntris) {
    if (!s || !vertices || ntris == 0) return LRT_RESULT_INVALID_ARGUMENT;
    if (s->mem_mapped) return LRT_RESULT_INVALID_ARGUMENT; /* read-only */
    if (s->prim_kind != TRI_PRIM_TRI) return LRT_RESULT_INVALID_ARGUMENT;
    /* Validate: ntris must match the original triangle count. */
    if ((size_t)s->original_ntris != ntris)
        return LRT_RESULT_INVALID_ARGUMENT;
    /* Validate vertex finiteness - NaN/inf would corrupt BVH bounds. */
    {
        size_t i;
        for (i = 0; i < ntris * 9; i++) {
            if (!isfinite(vertices[i]))
                return LRT_RESULT_INVALID_ARGUMENT;
        }
    }
    const int width = s->layout;

    /* 1. Scatter new vertices into the leaf blocks by original prim id. */
    for (uint32_t bi = 0; bi < s->block_count; bi++) {
        float *f = (float *)(void *)tri_block_floats(s->blocks, bi, width);
        uint32_t *ids = (uint32_t *)(f + 9 * width);
        for (int lane = 0; lane < width; lane++) {
            uint32_t pid = ids[lane];
            if (pid == LRT_TRI_NO_HIT) continue;
            if ((size_t)pid >= ntris) return LRT_RESULT_INVALID_ARGUMENT;
            const float *v = &vertices[(size_t)pid * 9];
            f[0 * width + lane] = v[0];
            f[1 * width + lane] = v[1];
            f[2 * width + lane] = v[2];
            f[3 * width + lane] = v[3] - v[0];
            f[4 * width + lane] = v[4] - v[1];
            f[5 * width + lane] = v[5] - v[2];
            f[6 * width + lane] = v[6] - v[0];
            f[7 * width + lane] = v[7] - v[1];
            f[8 * width + lane] = v[8] - v[2];
        }
    }

    /* 2. Recompute node bounds bottom-up (children have higher indices). */
    float root_lo[3] = {TRI_INF_F, TRI_INF_F, TRI_INF_F};
    float root_hi[3] = {-TRI_INF_F, -TRI_INF_F, -TRI_INF_F};
    if (s->node_count == 0) {
        tri_leaf_box(s, s->root, root_lo, root_hi);
    } else {
        float *nlo = (float *)malloc((size_t)s->node_count * 3 * sizeof(float));
        float *nhi = (float *)malloc((size_t)s->node_count * 3 * sizeof(float));
        if (!nlo || !nhi) {
            free(nlo);
            free(nhi);
            return LRT_RESULT_OUT_OF_MEMORY;
        }
        for (uint32_t i = s->node_count; i-- > 0;) {
            const uint32_t *child;
            uint32_t nch;
            if (s->layout == 4) {
                child = s->nodes4[i].child;
                nch = s->nodes4[i].nchildren;
            } else if (s->quantized) {
                child = s->nodes8q[i].child;
                nch = s->nodes8q[i].nchildren;
            } else {
                child = s->nodes8[i].child;
                nch = s->nodes8[i].nchildren;
            }
            float slo[8][3], shi[8][3];
            float blo[3] = {TRI_INF_F, TRI_INF_F, TRI_INF_F};
            float bhi[3] = {-TRI_INF_F, -TRI_INF_F, -TRI_INF_F};
            for (uint32_t c = 0; c < nch; c++) {
                float clo[3], chi[3];
                if (TRI_REF_IS_LEAF(child[c])) {
                    tri_leaf_box(s, child[c], clo, chi);
                } else {
                    uint32_t M = TRI_REF_NODE(child[c]);
                    for (int a = 0; a < 3; a++) {
                        clo[a] = nlo[(size_t)M * 3 + a];
                        chi[a] = nhi[(size_t)M * 3 + a];
                    }
                }
                for (int a = 0; a < 3; a++) {
                    slo[c][a] = clo[a];
                    shi[c][a] = chi[a];
                    if (clo[a] < blo[a]) blo[a] = clo[a];
                    if (chi[a] > bhi[a]) bhi[a] = chi[a];
                }
            }
            if (s->layout == 4) {
                lrt_bvh4_node *w = &s->nodes4[i];
                for (uint32_t c = 0; c < nch; c++) {
                    w->lo_x[c] = slo[c][0]; w->lo_y[c] = slo[c][1]; w->lo_z[c] = slo[c][2];
                    w->hi_x[c] = shi[c][0]; w->hi_y[c] = shi[c][1]; w->hi_z[c] = shi[c][2];
                }
            } else if (s->quantized) {
                lrt_bvh8q_node *w = &s->nodes8q[i];
                float scale[3], inv_scale[3];
                for (int a = 0; a < 3; a++) {
                    w->org[a] = blo[a];
                    float ext = bhi[a] - blo[a];
                    scale[a] = ext > 0.0f ? (ext / 255.0f) * (1.0f + 4e-7f) : 0.0f;
                    inv_scale[a] = scale[a] > 0.0f ? 1.0f / scale[a] : 0.0f;
                    w->scale[a] = scale[a];
                }
                for (uint32_t c = 0; c < nch; c++) {
                    w->qlo_x[c] = tri_quantize_lo(slo[c][0], w->org[0], scale[0], inv_scale[0]);
                    w->qlo_y[c] = tri_quantize_lo(slo[c][1], w->org[1], scale[1], inv_scale[1]);
                    w->qlo_z[c] = tri_quantize_lo(slo[c][2], w->org[2], scale[2], inv_scale[2]);
                    w->qhi_x[c] = tri_quantize_hi(shi[c][0], w->org[0], scale[0], inv_scale[0]);
                    w->qhi_y[c] = tri_quantize_hi(shi[c][1], w->org[1], scale[1], inv_scale[1]);
                    w->qhi_z[c] = tri_quantize_hi(shi[c][2], w->org[2], scale[2], inv_scale[2]);
                }
            } else {
                lrt_bvh8_node *w = &s->nodes8[i];
                for (uint32_t c = 0; c < nch; c++) {
                    w->lo_x[c] = slo[c][0]; w->lo_y[c] = slo[c][1]; w->lo_z[c] = slo[c][2];
                    w->hi_x[c] = shi[c][0]; w->hi_y[c] = shi[c][1]; w->hi_z[c] = shi[c][2];
                }
            }
            for (int a = 0; a < 3; a++) {
                nlo[(size_t)i * 3 + a] = blo[a];
                nhi[(size_t)i * 3 + a] = bhi[a];
            }
        }
        if (TRI_REF_IS_LEAF(s->root)) {
            tri_leaf_box(s, s->root, root_lo, root_hi);
        } else {
            uint32_t rn = TRI_REF_NODE(s->root);
            for (int a = 0; a < 3; a++) {
                root_lo[a] = nlo[(size_t)rn * 3 + a];
                root_hi[a] = nhi[(size_t)rn * 3 + a];
            }
        }
        free(nlo);
        free(nhi);
    }
    for (int a = 0; a < 3; a++) {
        s->root_lo[a] = root_lo[a];
        s->root_hi[a] = root_hi[a];
    }
    return LRT_RESULT_OK;
}

/* --------------------------------------------------------------------------
 * Instancing / TLAS: a BVH4 over instance world-AABBs; each leaf transforms
 * the ray into a BLAS's object space (dir NOT renormalized, so the parameter t
 * stays in world units) and queries the BLAS.
 * ------------------------------------------------------------------------ */

typedef struct lrt_instance_int {
    float obj2world[12];
    float world2obj[12];
    uint32_t blas_id, instance_id, mask, degenerate;
} lrt_instance_int;

struct lrt_tlas {
    lrt_bvh4_node *nodes;
    uint32_t node_count, node_cap;
    uint32_t root;
    uint32_t *inst_refs;
    uint32_t inst_ref_count, inst_ref_cap;
    lrt_instance_int *insts;
    size_t ninsts;
    lrt_tri_scene **blas; /* owned copy of the pointer array (scenes not owned) */
    size_t nblas;
};

/* out = inverse of the 3x4 affine m (row-major). Returns 0 if (near-)singular. */
static int tlas_affine_invert(const float m[12], float out[12]) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8],
          h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!(fabsf(det) > 1e-20f)) return 0;
    float inv = 1.0f / det;
    float L[9];
    L[0] = (e * i - f * h) * inv;
    L[1] = -(b * i - c * h) * inv;
    L[2] = (b * f - c * e) * inv;
    L[3] = -(d * i - f * g) * inv;
    L[4] = (a * i - c * g) * inv;
    L[5] = -(a * f - c * d) * inv;
    L[6] = (d * h - e * g) * inv;
    L[7] = -(a * h - b * g) * inv;
    L[8] = (a * e - b * d) * inv;
    float t0 = m[3], t1 = m[7], t2 = m[11];
    out[0] = L[0]; out[1] = L[1]; out[2] = L[2];
    out[3] = -(L[0] * t0 + L[1] * t1 + L[2] * t2);
    out[4] = L[3]; out[5] = L[4]; out[6] = L[5];
    out[7] = -(L[3] * t0 + L[4] * t1 + L[5] * t2);
    out[8] = L[6]; out[9] = L[7]; out[10] = L[8];
    out[11] = -(L[6] * t0 + L[7] * t1 + L[8] * t2);
    return 1;
}

static inline void tlas_xform_point(const float m[12], const float p[3],
                                    float out[3]) {
    for (int a = 0; a < 3; a++)
        out[a] = m[a * 4 + 0] * p[0] + m[a * 4 + 1] * p[1] +
                 m[a * 4 + 2] * p[2] + m[a * 4 + 3];
}
static inline void tlas_xform_vec(const float m[12], const float v[3],
                                  float out[3]) {
    for (int a = 0; a < 3; a++)
        out[a] = m[a * 4 + 0] * v[0] + m[a * 4 + 1] * v[1] + m[a * 4 + 2] * v[2];
}

/* World AABB of an object-space box transformed by m (8 corners). */
static void tlas_transform_aabb(const float m[12], const float olo[3],
                                const float ohi[3], float wlo[3], float whi[3]) {
    wlo[0] = wlo[1] = wlo[2] = TRI_INF_F;
    whi[0] = whi[1] = whi[2] = -TRI_INF_F;
    for (int k = 0; k < 8; k++) {
        float c[3] = {(k & 1) ? ohi[0] : olo[0], (k & 2) ? ohi[1] : olo[1],
                      (k & 4) ? ohi[2] : olo[2]};
        float w[3];
        tlas_xform_point(m, c, w);
        for (int a = 0; a < 3; a++) {
            if (w[a] < wlo[a]) wlo[a] = w[a];
            if (w[a] > whi[a]) whi[a] = w[a];
        }
    }
}

typedef struct tlas_collapse_ctx {
    lrt_tlas *t;
    tri_build_ctx *bc;
    uint32_t node_cap, ref_cap;
    int failed;
} tlas_collapse_ctx;

static uint32_t tlas_emit_leaf(tlas_collapse_ctx *cc, const tri_bnode *bn) {
    uint32_t count = bn->count;
    if (count > 15u || cc->t->inst_ref_count + count > cc->ref_cap) {
        cc->failed = 1;
        return TRI_REF_LEAF_BIT;
    }
    uint32_t first = cc->t->inst_ref_count;
    for (uint32_t k = 0; k < count; k++)
        cc->t->inst_refs[cc->t->inst_ref_count++] = cc->bc->indices[bn->a + k];
    return TRI_MAKE_LEAF_REF(first, count);
}

static uint32_t tlas_collapse(tlas_collapse_ctx *cc, uint32_t b_idx) {
    if (cc->failed) return 0;
    const tri_bnode *bn = &cc->bc->bnodes[b_idx];
    if (bn->count > 0) return tlas_emit_leaf(cc, bn);
    uint32_t set[4];
    int n = 0;
    set[n++] = bn->a;
    set[n++] = bn->b;
    while (n < 4) {
        int expand = -1;
        float best_area = -1.0f;
        for (int i = 0; i < n; i++) {
            const tri_bnode *m = &cc->bc->bnodes[set[i]];
            if (m->count > 0) continue;
            float area = tri_surface_area(m->lo, m->hi);
            if (area > best_area) {
                best_area = area;
                expand = i;
            }
        }
        if (expand < 0) break;
        const tri_bnode *m = &cc->bc->bnodes[set[expand]];
        set[expand] = m->a;
        set[n++] = m->b;
    }
    if (cc->t->node_count >= cc->node_cap) {
        cc->failed = 1;
        return 0;
    }
    uint32_t node_idx = cc->t->node_count++;
    lrt_bvh4_node *w = &cc->t->nodes[node_idx];
    memset(w, 0, sizeof(*w));
    for (int i = 0; i < 4; i++) {
        w->lo_x[i] = w->lo_y[i] = w->lo_z[i] = TRI_INF_F;
        w->hi_x[i] = w->hi_y[i] = w->hi_z[i] = TRI_INF_F;
    }
    w->nchildren = (uint32_t)n;
    for (int i = 0; i < n; i++) {
        const tri_bnode *m = &cc->bc->bnodes[set[i]];
        lrt_bvh4_node *wn = &cc->t->nodes[node_idx];
        wn->lo_x[i] = m->lo[0]; wn->lo_y[i] = m->lo[1]; wn->lo_z[i] = m->lo[2];
        wn->hi_x[i] = m->hi[0]; wn->hi_y[i] = m->hi[1]; wn->hi_z[i] = m->hi[2];
        uint32_t ref = tlas_collapse(cc, set[i]);
        cc->t->nodes[node_idx].child[i] = ref;
    }
    w = &cc->t->nodes[node_idx];
    for (int oct = 0; oct < 8; oct++) {
        float key[4];
        uint8_t order[4];
        for (int i = 0; i < n; i++) {
            float cx = 0.5f * (w->lo_x[i] + w->hi_x[i]);
            float cy = 0.5f * (w->lo_y[i] + w->hi_y[i]);
            float cz = 0.5f * (w->lo_z[i] + w->hi_z[i]);
            key[i] = (oct & 1 ? -cx : cx) + (oct & 2 ? -cy : cy) +
                     (oct & 4 ? -cz : cz);
            order[i] = (uint8_t)i;
        }
        for (int i = 1; i < n; i++) {
            uint8_t oi = order[i];
            float ki = key[oi];
            int j = i;
            while (j > 0 && key[order[j - 1]] > ki) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = oi;
        }
        uint8_t pmask = 0;
        for (int i = 0; i < n; i++) pmask |= (uint8_t)(order[i] << (2 * i));
        for (int i = n; i < 4; i++) pmask |= (uint8_t)(i << (2 * i));
        w->perm[oct] = pmask;
    }
    return TRI_MAKE_NODE_REF(node_idx);
}

/* (Re)build the TLAS BVH over the instances' current world AABBs. Frees any
 * existing nodes/inst_refs first. Returns 0 on success. */
static int tlas_rebuild(lrt_tlas *t, unsigned num_threads) {
    size_t ninsts = t->ninsts;
    tri_build_ctx bc;
    memset(&bc, 0, sizeof(bc));
    bc.ntris = ninsts;
    bc.max_leaf = 4u;
    bc.block_shift = 2u;
    bc.quality = LRT_TRI_BUILD_DEFAULT;
    if (tri_alloc_aabb_scratch(&bc, ninsts)) return 1;
    for (size_t i = 0; i < ninsts; i++) {
        const lrt_instance_int *in = &t->insts[i];
        float wlo[3], whi[3];
        const lrt_tri_scene *bl = t->blas[in->blas_id];
        if (in->degenerate) {
            /* keep it in the tree but with a degenerate box (never matched by
             * the mask check anyway) */
            for (int a = 0; a < 3; a++) {
                wlo[a] = 0.0f;
                whi[a] = 0.0f;
            }
        } else {
            tlas_transform_aabb(in->obj2world, bl->root_lo, bl->root_hi, wlo, whi);
        }
        for (int a = 0; a < 3; a++) {
            bc.plo[i * 3 + a] = wlo[a];
            bc.phi[i * 3 + a] = whi[a];
            bc.cen[i * 3 + a] = 0.5f * (wlo[a] + whi[a]);
        }
        bc.indices[i] = (uint32_t)i;
    }
#if !defined(__STDC_NO_THREADS__)
    if (num_threads > 1 && ninsts >= 4096) {
        bc.par_threads = num_threads;
        bc.par_scratch = (uint32_t *)malloc(ninsts * sizeof(uint32_t));
        if (!bc.par_scratch) bc.par_threads = 0;
    }
#endif
    uint32_t b_root = tri_build_binary(&bc, num_threads);

    tri_aligned_free(t->nodes);
    free(t->inst_refs);
    t->nodes = NULL;
    t->inst_refs = NULL;
    t->node_count = 0;
    t->inst_ref_count = 0;

    uint32_t node_cap = (uint32_t)ninsts;
    if (node_cap < 1) node_cap = 1;
    t->nodes =
        (lrt_bvh4_node *)tri_aligned_alloc(64, (size_t)node_cap * sizeof(lrt_bvh4_node));
    t->inst_refs = (uint32_t *)malloc((size_t)ninsts * sizeof(uint32_t));
    int failed = bc.failed || !t->nodes || !t->inst_refs;
    if (!failed) {
        t->node_cap = node_cap;
        t->inst_ref_cap = (uint32_t)ninsts;
        tlas_collapse_ctx cc;
        memset(&cc, 0, sizeof(cc));
        cc.t = t;
        cc.bc = &bc;
        cc.node_cap = node_cap;
        cc.ref_cap = (uint32_t)ninsts;
        const tri_bnode *rootn = &bc.bnodes[b_root];
        if (rootn->count > 0) {
            t->root = tlas_emit_leaf(&cc, rootn);
        } else {
            t->root = tlas_collapse(&cc, b_root);
        }
        failed = cc.failed;
    }
    tri_free_aabb_scratch(&bc);
    return failed;
}

static int tlas_fill_instances(lrt_tlas *t, const lrt_instance *insts,
                               size_t ninsts) {
    for (size_t i = 0; i < ninsts; i++) {
        lrt_instance_int *in = &t->insts[i];
        if (insts[i].blas_id >= t->nblas) return 1;
        memcpy(in->obj2world, insts[i].obj2world, sizeof(in->obj2world));
        in->blas_id = insts[i].blas_id;
        in->instance_id = insts[i].instance_id;
        in->mask = insts[i].mask;
        in->degenerate = tlas_affine_invert(in->obj2world, in->world2obj) ? 0u : 1u;
    }
    return 0;
}

lrt_tlas *lrt_tlas_build(lrt_tri_scene *const *blas, size_t nblas,
                         const lrt_instance *insts, size_t ninsts,
                         const lrt_tri_build_options *opts, lrt_result *err) {
    tri_set_err(err, LRT_RESULT_OK);
    if (!blas || nblas == 0 || !insts || ninsts == 0 || ninsts > 0x07FFFFFFu) {
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    /* Validate BLAS pointers are non-NULL. */
    {
        size_t i;
        for (i = 0; i < nblas; i++) {
            if (!blas[i]) {
                tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
                return NULL;
            }
        }
    }
    unsigned num_threads = (opts && opts->num_threads) ? opts->num_threads : 1u;
    lrt_tlas *t = (lrt_tlas *)calloc(1, sizeof(lrt_tlas));
    if (!t) {
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    t->nblas = nblas;
    t->ninsts = ninsts;
    t->blas = (lrt_tri_scene **)malloc(nblas * sizeof(lrt_tri_scene *));
    t->insts = (lrt_instance_int *)malloc(ninsts * sizeof(lrt_instance_int));
    if (!t->blas || !t->insts) {
        free(t->blas);
        free(t->insts);
        free(t);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    memcpy(t->blas, blas, nblas * sizeof(lrt_tri_scene *));
    if (tlas_fill_instances(t, insts, ninsts)) {
        free(t->insts);
        free(t);
        tri_set_err(err, LRT_RESULT_INVALID_ARGUMENT);
        return NULL;
    }
    if (tlas_rebuild(t, num_threads)) {
        lrt_tlas_free(t);
        tri_set_err(err, LRT_RESULT_OUT_OF_MEMORY);
        return NULL;
    }
    return t;
}

void lrt_tlas_free(lrt_tlas *t) {
    if (!t) return;
    tri_aligned_free(t->nodes);
    free(t->inst_refs);
    free(t->insts);
    free(t->blas); /* the pointer-array copy; the BLAS scenes are not owned */
    free(t);
}

lrt_result lrt_tlas_refit(lrt_tlas *t, const lrt_instance *insts,
                          size_t ninsts) {
    if (!t || !insts || ninsts != t->ninsts) return LRT_RESULT_INVALID_ARGUMENT;
    if (tlas_fill_instances(t, insts, ninsts)) return LRT_RESULT_INVALID_ARGUMENT;
    return tlas_rebuild(t, 1u) ? LRT_RESULT_OUT_OF_MEMORY : LRT_RESULT_OK;
}

int lrt_tlas_intersect1(const lrt_tlas *t, const lrt_ray *ray, uint32_t ray_mask,
                        lrt_tlas_hit *hit) {
    if (!t || !ray) {
        if (hit) hit->prim_id = LRT_TRI_NO_HIT;
        return 0;
    }
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    float best_t = ray->tmax, best_u = 0.0f, best_v = 0.0f;
    uint32_t best_prim = LRT_TRI_NO_HIT, best_inst = LRT_TRI_NO_HIT;
    tri_stack_entry stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp].ref = t->root;
    stack[sp].tnear = rc.tmin;
    sp++;
    while (sp > 0) {
        tri_stack_entry e = stack[--sp];
        if (e.tnear >= best_t) continue;
        if (TRI_REF_IS_LEAF(e.ref)) {
            uint32_t first = TRI_REF_BLOCK(e.ref), count = TRI_REF_NBLOCKS(e.ref);
            for (uint32_t k = 0; k < count; k++) {
                const lrt_instance_int *in = &t->insts[t->inst_refs[first + k]];
                if (in->degenerate || !(in->mask & ray_mask)) continue;
                lrt_ray q;
                tlas_xform_point(in->world2obj, ray->org, q.org);
                tlas_xform_vec(in->world2obj, ray->dir, q.dir);
                q.tmin = ray->tmin;
                q.tmax = best_t;
                lrt_hit lh;
                if (lrt_tri_intersect1(t->blas[in->blas_id], &q, &lh) &&
                    lh.t < best_t) {
                    best_t = lh.t;
                    best_u = lh.u;
                    best_v = lh.v;
                    best_prim = lh.prim_id;
                    best_inst = in->instance_id;
                }
            }
            continue;
        }
        const lrt_bvh4_node *n = &t->nodes[TRI_REF_NODE(e.ref)];
        int nchildren = (int)n->nchildren;
        uint32_t hit_ref[4];
        float hit_tnear[4];
        int nhit = 0;
        for (int i = 0; i < nchildren; i++) {
            float tnear;
            if (tri_slab_scalar(n->lo_x, n->lo_y, n->lo_z, n->hi_x, n->hi_y,
                                n->hi_z, i, &rc, best_t, &tnear)) {
                int j = nhit++;
                while (j > 0 && hit_tnear[j - 1] > tnear) {
                    hit_tnear[j] = hit_tnear[j - 1];
                    hit_ref[j] = hit_ref[j - 1];
                    j--;
                }
                hit_tnear[j] = tnear;
                hit_ref[j] = n->child[i];
            }
        }
        for (int i = nhit - 1; i >= 0; i--) {
            stack[sp].ref = hit_ref[i];
            stack[sp].tnear = hit_tnear[i];
            sp++;
        }
    }
    if (hit) {
        hit->t = best_prim != LRT_TRI_NO_HIT ? best_t : 0.0f;
        hit->u = best_u;
        hit->v = best_v;
        hit->prim_id = best_prim;
        hit->inst_id = best_inst;
    }
    return best_prim != LRT_TRI_NO_HIT;
}

int lrt_tlas_occluded1(const lrt_tlas *t, const lrt_ray *ray, uint32_t ray_mask) {
    if (!t || !ray) return 0;
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    const float t_max = ray->tmax;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = t->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t first = TRI_REF_BLOCK(ref), count = TRI_REF_NBLOCKS(ref);
            for (uint32_t k = 0; k < count; k++) {
                const lrt_instance_int *in = &t->insts[t->inst_refs[first + k]];
                if (in->degenerate || !(in->mask & ray_mask)) continue;
                lrt_ray q;
                tlas_xform_point(in->world2obj, ray->org, q.org);
                tlas_xform_vec(in->world2obj, ray->dir, q.dir);
                q.tmin = ray->tmin;
                q.tmax = t_max;
                if (lrt_tri_occluded1(t->blas[in->blas_id], &q)) return 1;
            }
            continue;
        }
        const lrt_bvh4_node *n = &t->nodes[TRI_REF_NODE(ref)];
        int nchildren = (int)n->nchildren;
        for (int i = 0; i < nchildren; i++) {
            float tnear;
            if (tri_slab_scalar(n->lo_x, n->lo_y, n->lo_z, n->hi_x, n->hi_y,
                                n->hi_z, i, &rc, t_max, &tnear)) {
                if (sp < TRI_STACK_SIZE) stack[sp++] = n->child[i];
            }
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Coherent ray packets (Ray4/Ray8) and the any-hit filter variant.
 * ------------------------------------------------------------------------ */

/* Any-hit with a user accept/reject filter; scalar so the indirect call does
 * not poison the SIMD any-hit early-out. Triangle scenes only. */
int lrt_tri_occluded1_filtered(const lrt_tri_scene *s, const lrt_ray *ray,
                               lrt_anyhit_filter filter, void *user) {
    if (!s || !ray) return 0;
    if (!filter || s->prim_kind != TRI_PRIM_TRI) return lrt_tri_occluded1(s, ray);
    tri_ray_ctx rc;
    tri_ray_setup(ray, &rc);
    const float t_max = ray->tmax;
    const int width = s->layout;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            for (uint32_t b = 0; b < nblk; b++) {
                const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                const uint32_t *ids = (const uint32_t *)(f + 9 * width);
                for (int lane = 0; lane < width; lane++) {
                    if (ids[lane] == LRT_TRI_NO_HIT) continue;
                    float t, u, v;
                    if (tri_isect_lane(f, width, lane, &rc, t_max, &t, &u, &v) &&
                        filter(user, ids[lane], t, u, v))
                        return 1;
                }
            }
            continue;
        }
        float dec[48];
        tri_node_view nv;
        tri_node_load(s, ref, dec, &nv);
        for (int i = 0; i < nv.n && sp < TRI_STACK_SIZE; i++) {
            float tnear;
            if (tri_slab_scalar(nv.lo_x, nv.lo_y, nv.lo_z, nv.hi_x, nv.hi_y,
                                nv.hi_z, i, &rc, t_max, &tnear))
                stack[sp++] = nv.child[i];
        }
    }
    return 0;
}

#if LRT_TRI_HAS_SSE4
/* SoA 4-ray packet (lane = ray). */
typedef struct tri_packet4 {
    __m128 orgx, orgy, orgz;
    __m128 invdx, invdy, invdz;
    __m128 dirx, diry, dirz;
    __m128 tmin;
} tri_packet4;

static void tri_packet4_setup(const float *orgx, const float *orgy,
                              const float *orgz, const float *dirx,
                              const float *diry, const float *dirz,
                              const float *tmin, tri_packet4 *p) {
    _Alignas(16) float ox[4], oy[4], oz[4], ix[4], iy[4], iz[4], dx[4], dy[4],
        dz[4], tn[4];
    for (int k = 0; k < 4; k++) {
        lrt_ray rr = {{orgx[k], orgy[k], orgz[k]},
                      tmin[k],
                      {dirx[k], diry[k], dirz[k]},
                      0.0f};
        tri_ray_ctx rc;
        tri_ray_setup(&rr, &rc);
        ox[k] = rc.org[0]; oy[k] = rc.org[1]; oz[k] = rc.org[2];
        ix[k] = rc.invd[0]; iy[k] = rc.invd[1]; iz[k] = rc.invd[2];
        dx[k] = rc.dir[0]; dy[k] = rc.dir[1]; dz[k] = rc.dir[2];
        tn[k] = rc.tmin;
    }
    p->orgx = _mm_load_ps(ox); p->orgy = _mm_load_ps(oy); p->orgz = _mm_load_ps(oz);
    p->invdx = _mm_load_ps(ix); p->invdy = _mm_load_ps(iy); p->invdz = _mm_load_ps(iz);
    p->dirx = _mm_load_ps(dx); p->diry = _mm_load_ps(dy); p->dirz = _mm_load_ps(dz);
    p->tmin = _mm_load_ps(tn);
}

/* Mask of packet rays whose interval enters the scalar box [lo,hi], within
 * per-ray tmax4. */
static inline __m128 tri_packet4_slab(const tri_packet4 *p, float lo_x,
                                      float lo_y, float lo_z, float hi_x,
                                      float hi_y, float hi_z, __m128 tmax4) {
    __m128 tlx = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(lo_x), p->orgx), p->invdx);
    __m128 thx = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(hi_x), p->orgx), p->invdx);
    __m128 tly = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(lo_y), p->orgy), p->invdy);
    __m128 thy = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(hi_y), p->orgy), p->invdy);
    __m128 tlz = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(lo_z), p->orgz), p->invdz);
    __m128 thz = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(hi_z), p->orgz), p->invdz);
    __m128 tnear = _mm_max_ps(
        _mm_max_ps(_mm_min_ps(tlx, thx), _mm_min_ps(tly, thy)),
        _mm_max_ps(_mm_min_ps(tlz, thz), p->tmin));
    __m128 tfar = _mm_min_ps(
        _mm_min_ps(_mm_max_ps(tlx, thx), _mm_max_ps(tly, thy)),
        _mm_min_ps(_mm_max_ps(tlz, thz), tmax4));
    return _mm_cmple_ps(tnear, tfar);
}

/* Packet vs one triangle (scalar v0/e1/e2 broadcast). Returns the valid mask
 * and writes t/u/v for hit lanes. */
static inline __m128 tri_packet4_tri(const tri_packet4 *p, const float v0[3],
                                     const float e1[3], const float e2[3],
                                     __m128 tmax4, __m128 *t_out, __m128 *u_out,
                                     __m128 *v_out) {
    __m128 e1x = _mm_set1_ps(e1[0]), e1y = _mm_set1_ps(e1[1]), e1z = _mm_set1_ps(e1[2]);
    __m128 e2x = _mm_set1_ps(e2[0]), e2y = _mm_set1_ps(e2[1]), e2z = _mm_set1_ps(e2[2]);
    __m128 px = _mm_sub_ps(_mm_mul_ps(p->diry, e2z), _mm_mul_ps(p->dirz, e2y));
    __m128 py = _mm_sub_ps(_mm_mul_ps(p->dirz, e2x), _mm_mul_ps(p->dirx, e2z));
    __m128 pz = _mm_sub_ps(_mm_mul_ps(p->dirx, e2y), _mm_mul_ps(p->diry, e2x));
    __m128 det = _mm_add_ps(_mm_add_ps(_mm_mul_ps(e1x, px), _mm_mul_ps(e1y, py)),
                            _mm_mul_ps(e1z, pz));
    __m128 valid = tri_isect_det_parallel4(e1x, e1y, e1z, e2x, e2y, e2z, det);
    __m128 inv_det = _mm_div_ps(_mm_set1_ps(1.0f), det);
    __m128 tvx = _mm_sub_ps(p->orgx, _mm_set1_ps(v0[0]));
    __m128 tvy = _mm_sub_ps(p->orgy, _mm_set1_ps(v0[1]));
    __m128 tvz = _mm_sub_ps(p->orgz, _mm_set1_ps(v0[2]));
    __m128 u = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(tvx, px), _mm_mul_ps(tvy, py)),
                   _mm_mul_ps(tvz, pz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_and_ps(_mm_cmpge_ps(u, _mm_setzero_ps()),
                                         _mm_cmple_ps(u, _mm_set1_ps(1.0f))));
    __m128 qx = _mm_sub_ps(_mm_mul_ps(tvy, e1z), _mm_mul_ps(tvz, e1y));
    __m128 qy = _mm_sub_ps(_mm_mul_ps(tvz, e1x), _mm_mul_ps(tvx, e1z));
    __m128 qz = _mm_sub_ps(_mm_mul_ps(tvx, e1y), _mm_mul_ps(tvy, e1x));
    __m128 v = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(p->dirx, qx), _mm_mul_ps(p->diry, qy)),
                   _mm_mul_ps(p->dirz, qz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_and_ps(_mm_cmpge_ps(v, _mm_setzero_ps()),
                                         _mm_cmple_ps(_mm_add_ps(u, v),
                                                      _mm_set1_ps(1.0f))));
    __m128 t = _mm_mul_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(e2x, qx), _mm_mul_ps(e2y, qy)),
                   _mm_mul_ps(e2z, qz)),
        inv_det);
    valid = _mm_and_ps(valid, _mm_and_ps(_mm_cmpge_ps(t, p->tmin),
                                         _mm_cmplt_ps(t, tmax4)));
    *t_out = t;
    *u_out = u;
    *v_out = v;
    return valid;
}

static void tri_intersect4_sse(const lrt_tri_scene *s, const lrt_ray4 *r,
                               lrt_hit4 *h) {
    tri_packet4 p;
    tri_packet4_setup(r->orgx, r->orgy, r->orgz, r->dirx, r->diry, r->dirz,
                      r->tmin, &p);
    __m128 best_t = _mm_loadu_ps(r->tmax);
    __m128 best_u = _mm_setzero_ps(), best_v = _mm_setzero_ps();
    uint32_t best_prim[4] = {LRT_TRI_NO_HIT, LRT_TRI_NO_HIT, LRT_TRI_NO_HIT,
                             LRT_TRI_NO_HIT};
    const int width = s->layout;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            for (uint32_t b = 0; b < nblk; b++) {
                const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                const uint32_t *ids = (const uint32_t *)(f + 9 * width);
                for (int lane = 0; lane < width; lane++) {
                    if (ids[lane] == LRT_TRI_NO_HIT) continue;
                    float v0[3], e1[3], e2[3];
                    for (int a = 0; a < 3; a++) {
                        v0[a] = f[a * width + lane];
                        e1[a] = f[(3 + a) * width + lane];
                        e2[a] = f[(6 + a) * width + lane];
                    }
                    __m128 tt, uu, vv;
                    __m128 m = tri_packet4_tri(&p, v0, e1, e2, best_t, &tt, &uu,
                                               &vv);
                    __m128 closer = _mm_and_ps(m, _mm_cmplt_ps(tt, best_t));
                    int cm = _mm_movemask_ps(closer);
                    if (cm) {
                        best_t = _mm_blendv_ps(best_t, tt, closer);
                        best_u = _mm_blendv_ps(best_u, uu, closer);
                        best_v = _mm_blendv_ps(best_v, vv, closer);
                        while (cm) {
                            int l = __builtin_ctz((unsigned)cm);
                            cm &= cm - 1;
                            best_prim[l] = ids[lane];
                        }
                    }
                }
            }
            continue;
        }
        float dec[48];
        tri_node_view nv;
        tri_node_load(s, ref, dec, &nv);
        for (int i = 0; i < nv.n && sp < TRI_STACK_SIZE; i++) {
            __m128 m = tri_packet4_slab(&p, nv.lo_x[i], nv.lo_y[i], nv.lo_z[i],
                                        nv.hi_x[i], nv.hi_y[i], nv.hi_z[i],
                                        best_t);
            if (_mm_movemask_ps(m)) stack[sp++] = nv.child[i];
        }
    }
    _mm_storeu_ps(h->t, best_t);
    _mm_storeu_ps(h->u, best_u);
    _mm_storeu_ps(h->v, best_v);
    for (int k = 0; k < 4; k++) {
        h->prim_id[k] = best_prim[k];
        if (best_prim[k] == LRT_TRI_NO_HIT) h->t[k] = 0.0f;
    }
}

static void tri_occluded4_sse(const lrt_tri_scene *s, const lrt_ray4 *r,
                              uint8_t occ[4]) {
    tri_packet4 p;
    tri_packet4_setup(r->orgx, r->orgy, r->orgz, r->dirx, r->diry, r->dirz,
                      r->tmin, &p);
    __m128 tmax4 = _mm_loadu_ps(r->tmax);
    int active = _mm_movemask_ps(_mm_cmplt_ps(p.tmin, tmax4));
    occ[0] = occ[1] = occ[2] = occ[3] = 0;
    const int width = s->layout;
    uint32_t stack[TRI_STACK_SIZE];
    int sp = 0;
    stack[sp++] = s->root;
    while (sp > 0 && active) {
        uint32_t ref = stack[--sp];
        if (TRI_REF_IS_LEAF(ref)) {
            uint32_t blk0 = TRI_REF_BLOCK(ref), nblk = TRI_REF_NBLOCKS(ref);
            for (uint32_t b = 0; b < nblk && active; b++) {
                const float *f = tri_block_floats(s->blocks, blk0 + b, width);
                const uint32_t *ids = (const uint32_t *)(f + 9 * width);
                for (int lane = 0; lane < width; lane++) {
                    if (ids[lane] == LRT_TRI_NO_HIT) continue;
                    float v0[3], e1[3], e2[3];
                    for (int a = 0; a < 3; a++) {
                        v0[a] = f[a * width + lane];
                        e1[a] = f[(3 + a) * width + lane];
                        e2[a] = f[(6 + a) * width + lane];
                    }
                    __m128 tt, uu, vv;
                    __m128 m = tri_packet4_tri(&p, v0, e1, e2, tmax4, &tt, &uu,
                                               &vv);
                    int hm = _mm_movemask_ps(m) & active;
                    if (hm) {
                        for (int k = 0; k < 4; k++)
                            if (hm & (1 << k)) occ[k] = 1;
                        active &= ~hm;
                        if (!active) break;
                    }
                }
            }
            continue;
        }
        float dec[48];
        tri_node_view nv;
        tri_node_load(s, ref, dec, &nv);
        for (int i = 0; i < nv.n && sp < TRI_STACK_SIZE; i++) {
            __m128 m = tri_packet4_slab(&p, nv.lo_x[i], nv.lo_y[i], nv.lo_z[i],
                                        nv.hi_x[i], nv.hi_y[i], nv.hi_z[i],
                                        tmax4);
            if (_mm_movemask_ps(m) & active) stack[sp++] = nv.child[i];
        }
    }
}
#endif /* LRT_TRI_HAS_SSE4 */

void lrt_tri_intersect4(const lrt_tri_scene *s, const lrt_ray4 *r,
                        lrt_hit4 *h) {
    if (!s || !r || !h) return;
#if LRT_TRI_HAS_SSE4
    if (s->prim_kind == TRI_PRIM_TRI) {
        tri_intersect4_sse(s, r, h);
        return;
    }
#endif
    for (int k = 0; k < 4; k++) {
        lrt_ray rr = {{r->orgx[k], r->orgy[k], r->orgz[k]},
                      r->tmin[k],
                      {r->dirx[k], r->diry[k], r->dirz[k]},
                      r->tmax[k]};
        lrt_hit hit;
        lrt_tri_intersect1(s, &rr, &hit);
        h->t[k] = hit.t;
        h->u[k] = hit.u;
        h->v[k] = hit.v;
        h->prim_id[k] = hit.prim_id;
    }
}

void lrt_tri_occluded4(const lrt_tri_scene *s, const lrt_ray4 *r,
                       uint8_t occ[4]) {
    if (!s || !r || !occ) return;
#if LRT_TRI_HAS_SSE4
    if (s->prim_kind == TRI_PRIM_TRI) {
        tri_occluded4_sse(s, r, occ);
        return;
    }
#endif
    for (int k = 0; k < 4; k++) {
        lrt_ray rr = {{r->orgx[k], r->orgy[k], r->orgz[k]},
                      r->tmin[k],
                      {r->dirx[k], r->diry[k], r->dirz[k]},
                      r->tmax[k]};
        occ[k] = (uint8_t)lrt_tri_occluded1(s, &rr);
    }
}

/* 8-wide packets run as two 4-wide sub-packets (keeps one kernel; coherent
 * benefit applies per half). */
static void tri_ray8_half(const lrt_ray8 *r, int o, lrt_ray4 *out) {
    for (int k = 0; k < 4; k++) {
        out->orgx[k] = r->orgx[o + k]; out->orgy[k] = r->orgy[o + k];
        out->orgz[k] = r->orgz[o + k]; out->dirx[k] = r->dirx[o + k];
        out->diry[k] = r->diry[o + k]; out->dirz[k] = r->dirz[o + k];
        out->tmin[k] = r->tmin[o + k]; out->tmax[k] = r->tmax[o + k];
    }
}

void lrt_tri_intersect8(const lrt_tri_scene *s, const lrt_ray8 *r,
                        lrt_hit8 *h) {
    if (!s || !r || !h) return;
    for (int o = 0; o < 8; o += 4) {
        lrt_ray4 r4;
        lrt_hit4 h4;
        tri_ray8_half(r, o, &r4);
        lrt_tri_intersect4(s, &r4, &h4);
        for (int k = 0; k < 4; k++) {
            h->t[o + k] = h4.t[k]; h->u[o + k] = h4.u[k]; h->v[o + k] = h4.v[k];
            h->prim_id[o + k] = h4.prim_id[k];
        }
    }
}

void lrt_tri_occluded8(const lrt_tri_scene *s, const lrt_ray8 *r,
                       uint8_t occ[8]) {
    if (!s || !r || !occ) return;
    for (int o = 0; o < 8; o += 4) {
        lrt_ray4 r4;
        uint8_t o4[4];
        tri_ray8_half(r, o, &r4);
        lrt_tri_occluded4(s, &r4, o4);
        for (int k = 0; k < 4; k++) occ[o + k] = o4[k];
    }
}
