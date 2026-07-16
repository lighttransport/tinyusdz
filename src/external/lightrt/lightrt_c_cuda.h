/*
 * lightrt_c_cuda.h — CUDA (NVIDIA) GPU interop for the LightRT C11 triangle
 * kernel. Targets Blackwell (sm_120) but works on any CUDA device.
 *
 * Mirrors the Vulkan backend (lightrt_c_vk.h) but over CUDA, with two upgrades:
 *
 *   - A DEVICE-RESIDENT scene handle: upload a CPU-built BVH once
 *     (lrt_cuda_scene_upload) and trace many ray batches against it
 *     (lrt_cuda_scene_trace), so throughput reflects GPU traversal rather than
 *     per-call PCIe upload (the Vulkan vk-trace re-uploads every call).
 *
 *   - Low-precision / quantized trace modes for the dynamic / motion-blur path
 *     (lrt_cuda_scene_trace_ex): bf16 (default), fp8 (opt-in), and int8/int4
 *     quantized leaves accelerated with Blackwell WMMA (matrix cores). Phase 2.
 *
 * The fp32 path (the default) traverses the SAME node/leaf memory image the C
 * builder produces, bridged through the position-independent LRTS serialization
 * (lrt_tri_scene_save_to_memory), so a closest-hit matches lrt_tri_intersect1
 * within fp tolerance.
 *
 * Everything degrades gracefully: if no CUDA device is available,
 * lrt_cuda_engine_create() returns NULL and the caller falls back to the CPU
 * kernel. An engine is created once and may be reused; a single engine is NOT
 * safe to use from multiple threads concurrently (one stream).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTRT_C_CUDA_H
#define LIGHTRT_C_CUDA_H

#include <stddef.h>
#include <stdint.h>

#include "lightrt_c.h"     /* lrt_result */
#include "lightrt_c_tri.h" /* lrt_tri_scene, lrt_ray, lrt_hit, lrt_tri_layout */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lrt_cuda_engine lrt_cuda_engine;
typedef struct lrt_cuda_scene lrt_cuda_scene;

/* Capability flags reported by lrt_cuda_engine_caps(). */
typedef enum lrt_cuda_caps {
    LRT_CUDA_CAP_COMPUTE = 1u << 0, /* always set when an engine exists       */
    LRT_CUDA_CAP_WMMA = 1u << 1,    /* sm_70+ WMMA (matrix cores) usable  */
    LRT_CUDA_CAP_FP8 = 1u << 2,     /* fp8 WMMA inputs (sm_90+)                */
    LRT_CUDA_CAP_INT8 = 1u << 3,    /* iu8 WMMA inputs                        */
    LRT_CUDA_CAP_INT4 = 1u << 4     /* iu4 WMMA inputs                        */
} lrt_cuda_caps;

typedef struct lrt_cuda_engine_options {
    int device_index; /* explicit CUDA device ordinal, or -1 = auto-pick      */
    uint32_t flags;   /* reserved; pass 0                                    */
} lrt_cuda_engine_options;

/* Trace precision / acceleration mode (lrt_cuda_scene_trace_ex). LRT_CUDA_TRACE_FP32
 * is the default and matches the CPU oracle; the others trade precision for
 * throughput on the dynamic / motion-blur path and are introduced in Phase 2. */
typedef enum lrt_cuda_trace_mode {
    LRT_CUDA_TRACE_FP32 = 0,     /* fp32 traversal, matches lrt_tri_intersect1 */
    LRT_CUDA_TRACE_WMMA_BF16,    /* bf16 WMMA leaf, no fp32 follow-up (dynamic)*/
    LRT_CUDA_TRACE_WMMA_FP8,     /* fp8  WMMA leaf, experimental opt-in         */
    LRT_CUDA_TRACE_WMMA_F32SEED, /* bf16 WMMA cull + fp32 follow-up (static)    */
    LRT_CUDA_TRACE_INT8,         /* int8 quantized leaf + iu8 WMMA              */
    LRT_CUDA_TRACE_INT4          /* int4 quantized leaf + iu4 WMMA, experimental*/
} lrt_cuda_trace_mode;

/* Create an engine (cudaSetDevice + device pick). Returns NULL and, when err is
 * non-NULL, stores the reason, on any failure: no CUDA runtime, no device. opts
 * may be NULL for defaults (auto device). */
lrt_cuda_engine *lrt_cuda_engine_create(const lrt_cuda_engine_options *opts,
                                      lrt_result *err);
void lrt_cuda_engine_destroy(lrt_cuda_engine *e);

/* Bitmask of lrt_cuda_caps for the created device. */
uint32_t lrt_cuda_engine_caps(const lrt_cuda_engine *e);

/* Selected device name (e.g. "NVIDIA GeForce RTX 5060 Ti"). */
const char *lrt_cuda_engine_device_name(const lrt_cuda_engine *e);

/* Human-readable message for the last failed call on this engine. */
const char *lrt_cuda_engine_last_error(const lrt_cuda_engine *e);

/* --- Resident scene: upload once, trace many ------------------------------
 *
 * Upload a CPU-built scene (via its LRTS serialization) to device memory. The
 * node/block buffers stay resident until lrt_cuda_scene_free(). Only plain
 * triangle scenes with BVH4/BVH8 layout are supported (quantized/curve/user
 * scenes are rejected, as by lrt_tri_scene_save_to_memory). The engine must
 * outlive the scene. Returns NULL on error (err set). */
lrt_cuda_scene *lrt_cuda_scene_upload(lrt_cuda_engine *e, const lrt_tri_scene *s,
                                    lrt_result *err);
void lrt_cuda_scene_free(lrt_cuda_engine *e, lrt_cuda_scene *s);

/* Closest-hit n rays against the resident scene (fp32). Writes n hits to out;
 * ray/hit device scratch is reused/grown across calls. Returns the number of
 * rays that hit, or -1 on error (err set). Results match lrt_tri_intersect1. */
int lrt_cuda_scene_trace(lrt_cuda_engine *e, lrt_cuda_scene *s, const lrt_ray *rays,
                        uint32_t n, lrt_hit *out, lrt_result *err);

/* Closest-hit + per-ray geometric SURFACE/CURVE normal, computed on the device.
 * Writes n hits to out and 3 floats/ray to out_normals (the unnormalized
 * geometric normal Ng = cross(dP/du, dP/dv) for parametric surfaces — bilinear /
 * bicubic Bezier / NURBS / trimmed NURBS — or the normalized radial normal for a
 * round-linear curve; zero on miss). Mirrors lrt_tri_surface_normal /
 * lrt_tri_curve_frame. Returns #hits, or -1 on error. LRT_RESULT_UNSUPPORTED if
 * the scene kind carries no shade data (e.g. a triangle scene, or upload OOM). */
int lrt_cuda_scene_trace_normals(lrt_cuda_engine *e, lrt_cuda_scene *s,
                                const lrt_ray *rays, uint32_t n, lrt_hit *out,
                                float *out_normals, lrt_result *err);

/* Any-hit (occlusion) n rays. occluded[i] = 0 or 1. Returns #occluded, or -1. */
int lrt_cuda_scene_occluded(lrt_cuda_engine *e, lrt_cuda_scene *s,
                           const lrt_ray *rays, uint32_t n, uint8_t *occluded,
                           lrt_result *err);

/* Refit a resident scene in place for animation / motion blur (dynamic path):
 * update the leaf vertices and recompute node bounds WITHOUT rebuilding the tree.
 * vertices is 9*ntris floats in the ORIGINAL build order (indexed by prim_id);
 * ntris must equal the original triangle count. Tree topology is preserved (large
 * deformations degrade traversal quality but stay correct). Far cheaper than a
 * rebuild — the realtime dynamic-scene path. Returns 0 on success, -1 on error. */
int lrt_cuda_scene_refit(lrt_cuda_engine *e, lrt_cuda_scene *s,
                        const float *vertices, uint32_t ntris, lrt_result *err);

/* As lrt_cuda_scene_trace but selects a precision / acceleration mode. With
 * LRT_CUDA_TRACE_FP32 it is identical to lrt_cuda_scene_trace. Phase 2 modes fall
 * back to fp32 with LRT_RESULT_NOT_BUILT until implemented. */
int lrt_cuda_scene_trace_ex(lrt_cuda_engine *e, lrt_cuda_scene *s,
                           lrt_cuda_trace_mode mode, const lrt_ray *rays,
                           uint32_t n, lrt_hit *out, lrt_result *err);

/* --- One-shot convenience (upload + trace + free) ------------------------- */
int lrt_cuda_trace_scene(lrt_cuda_engine *e, const lrt_tri_scene *s,
                        const lrt_ray *rays, uint32_t n, lrt_hit *out,
                        lrt_result *err);

/* --- Phase 2: WMMA / integer-quantized leaf intersection ------------------
 *
 * The honest open question on NVIDIA is whether the matrix cores (WMMA) beat
 * scalar FMA for the leaf-intersection inner kernel. lrt_cuda_leaf_bench isolates
 * exactly that: nblocks independent "leaves", each a tile of 16 coherent rays
 * vs tris_per_leaf (<=16) triangles, intersected with the chosen method, so the
 * benchmark can compare throughput AND accuracy (vs the scalar fp32 method) on
 * the same data. This is the convergent inner kernel a coherent-packet traversal
 * would call; it is measured standalone to report S-vs-W without conflating it
 * with divergent traversal.
 *
 * The ray-triangle test is the Plücker edge-side formulation: ray Plücker
 * (dir, cross(org,dir)) dotted against per-edge Plücker (moment, direction) as a
 * 16x16x16 GEMM (3 edges -> 3 mma ops/leaf), fp32-accumulated; the sign of the
 * three edge functions classifies in/out and t is recovered in fp32 for the few
 * candidates. WMMA_BF16 runs the GEMM in bf16 (dynamic / motion-blur path, no
 * fp32 re-test); WMMA_INT8 quantizes to int8 in leaf-local coords (iu8 GEMM);
 * SCALAR is the fp32 Moller-Trumbore baseline. */
typedef enum lrt_cuda_isect_method {
    LRT_CUDA_ISECT_SCALAR = 0,   /* fp32 Moller-Trumbore (baseline / oracle)   */
    LRT_CUDA_ISECT_WMMA_BF16,    /* bf16 Plücker GEMM, fp32 t                   */
    LRT_CUDA_ISECT_WMMA_FP16,    /* fp16 Plücker GEMM, fp32 t                   */
    LRT_CUDA_ISECT_WMMA_FP8,     /* fp8 (e4m3) leaf-local Plücker GEMM, fp32 t  */
    LRT_CUDA_ISECT_WMMA_INT8     /* int8 leaf-local Plücker GEMM, fp32 t        */
} lrt_cuda_isect_method;

/* leaf_tris: nblocks*tris_per_leaf*9 floats (v0 v1 v2). rays/out: 16*nblocks
 * (rays packed per leaf). out[k].prim_id = local triangle index [0,tris_per_leaf)
 * of the closest hit in that leaf, or LRT_TRI_NO_HIT. *kernel_ms gets the device
 * kernel time (excludes H2D/D2H). Returns 0 on success, -1 on error. Returns -1
 * with LRT_RESULT_NOT_BUILT if the method needs WMMA and it is unavailable. */
int lrt_cuda_leaf_bench(lrt_cuda_engine *e, lrt_cuda_isect_method method,
                       const float *leaf_tris, uint32_t nblocks,
                       uint32_t tris_per_leaf, const lrt_ray *rays, lrt_hit *out,
                       double *kernel_ms, lrt_result *err);

/* Batched ray transform (instancing + motion blur) — the cleanest WMMA fit.
 * out[i] transforms rays[i] by a 3x4 affine; if m1 != NULL the per-ray transform
 * is the time-lerp (1-t)*m0 + t*m1 with t = times[i] (times != NULL). method
 * selects scalar vs WMMA (bf16/fp16). m0/m1 are 12 floats row-major (3 rows of
 * [r0 r1 r2 tx]). *kernel_ms gets device kernel time. Returns 0 / -1. */
int lrt_cuda_transform_bench(lrt_cuda_engine *e, lrt_cuda_isect_method method,
                            const lrt_ray *rays, lrt_ray *out, uint32_t n,
                            const float *m0, const float *m1, const float *times,
                            double *kernel_ms, lrt_result *err);

/* Whether WMMA leaf/transform kernels were compiled in (nvcuda::wmma present). */
int lrt_cuda_have_wmma(void);

/* --- Path B: GPU build front end -> CPU scene -----------------------------
 *
 * vertices = 9*ntris floats (v0 v1 v2 per triangle). Computes centroids and
 * 30-bit Morton codes on the GPU, finishes the LBVH on the CPU, returns a heap
 * scene traversable with the normal lrt_tri_* queries (matches a FAST CPU
 * build). layout must be BVH4 or BVH8. Free the result with
 * lrt_tri_scene_free(). Returns 0 on success, -1 on error (err set). */
int lrt_cuda_build_scene(lrt_cuda_engine *e, const float *vertices, uint32_t ntris,
                        lrt_tri_layout layout, lrt_tri_scene **out,
                        lrt_result *err);

/* --- Full-GPU LBVH build --------------------------------------------------
 *
 * Build a BVH entirely on the GPU (no CPU hierarchy work): centroids + 30-bit
 * Morton on the GPU, CUB radix sort of unique 64-bit (morton<<32|index) keys,
 * a Karras radix tree, bottom-up bounds via atomic flags, and emission directly
 * into the trace kernel's BVH4 wide-node format as a binary (2-wide) tree.
 * Returns a resident scene traceable immediately with lrt_cuda_scene_trace.
 *
 * Far faster build than the hybrid lrt_cuda_build_scene (which finishes on the
 * CPU) — the path for topology-changing dynamic scenes. Traversal is a touch
 * slower than a collapsed SAH BVH (the tree is un-collapsed 2-wide). Requires
 * CUB; returns NULL with LRT_RESULT_NOT_BUILT if it was not compiled in
 * (check lrt_cuda_have_gpu_build()). Free with lrt_cuda_scene_free(). */
lrt_cuda_scene *lrt_cuda_scene_build_gpu(lrt_cuda_engine *e, const float *vertices,
                                       uint32_t ntris, lrt_result *err);

/* 1 if the full-GPU LBVH builder was compiled in (CUB present). */
int lrt_cuda_have_gpu_build(void);

/* --- Fully GPU-resident dynamic pipeline (no per-frame PCIe readback) ------
 *
 * For dynamic scenes / motion blur, keep vertices, rays and hits in device
 * memory across frames: build (or refit) the BVH, generate rays, and intersect
 * entirely on the GPU, with nothing copied back to the host per frame. The
 * builds above (lrt_cuda_scene_build_gpu) and refit compute base/scale on-device
 * with no D2H. Use lrt_cuda_dbuffer for resident vertex/ray/hit buffers; the
 * *_device queries below take and produce those device buffers directly.
 * Explicit upload/download are provided only for setup / final present / debug. */
typedef struct lrt_cuda_dbuffer lrt_cuda_dbuffer;

lrt_cuda_dbuffer *lrt_cuda_dbuffer_alloc(lrt_cuda_engine *e, size_t bytes,
                                       lrt_result *err);
void lrt_cuda_dbuffer_free(lrt_cuda_engine *e, lrt_cuda_dbuffer *b);
void *lrt_cuda_dbuffer_ptr(lrt_cuda_dbuffer *b);   /* raw device pointer */
size_t lrt_cuda_dbuffer_size(const lrt_cuda_dbuffer *b);
int lrt_cuda_dbuffer_upload(lrt_cuda_engine *e, lrt_cuda_dbuffer *b,
                           const void *src, size_t bytes, lrt_result *err);
int lrt_cuda_dbuffer_download(lrt_cuda_engine *e, const lrt_cuda_dbuffer *b,
                             void *dst, size_t bytes, lrt_result *err);

/* Generate pinhole-camera primary rays directly into a device ray buffer (n =
 * width*height lrt_rays). The ray (x,y) is origin -> lower_left + sx*horizontal
 * + sy*vertical with sx=(x+0.5)/width, sy=(y+0.5)/height (dir not normalized).
 * No host data crosses PCIe. */
int lrt_cuda_raygen_camera(lrt_cuda_engine *e, lrt_cuda_dbuffer *rays,
                          uint32_t width, uint32_t height,
                          const float origin[3], const float lower_left[3],
                          const float horizontal[3], const float vertical[3],
                          float tmin, float tmax, lrt_result *err);

/* Trace / occlude device rays -> device hits. No PCIe readback; results stay on
 * the GPU (for shading / further passes). Returns 0 on success, -1 on error. */
int lrt_cuda_scene_trace_device(lrt_cuda_engine *e, lrt_cuda_scene *s,
                               const lrt_cuda_dbuffer *rays, uint32_t n,
                               lrt_cuda_dbuffer *hits, lrt_result *err);
int lrt_cuda_scene_occluded_device(lrt_cuda_engine *e, lrt_cuda_scene *s,
                                  const lrt_cuda_dbuffer *rays, uint32_t n,
                                  lrt_cuda_dbuffer *occluded, lrt_result *err);

/* As lrt_cuda_scene_trace_device but also fills `normals` (3 floats/ray) with the
 * on-device surface/curve geometric normal — the fully-resident shading path
 * (no PCIe). normals->size must be >= n*3*sizeof(float). LRT_RESULT_UNSUPPORTED
 * if the scene carries no shade data. Returns 0 on success, -1 on error. */
int lrt_cuda_scene_trace_normals_device(lrt_cuda_engine *e, lrt_cuda_scene *s,
                                       const lrt_cuda_dbuffer *rays, uint32_t n,
                                       lrt_cuda_dbuffer *hits,
                                       lrt_cuda_dbuffer *normals,
                                       lrt_result *err);

/* Refit / full-GPU build from a DEVICE vertex buffer (9*ntris floats already on
 * the GPU) — no per-frame H2D for animated geometry. */
int lrt_cuda_scene_refit_device(lrt_cuda_engine *e, lrt_cuda_scene *s,
                               const lrt_cuda_dbuffer *vertices, uint32_t ntris,
                               lrt_result *err);
lrt_cuda_scene *lrt_cuda_scene_build_gpu_device(lrt_cuda_engine *e,
                                              const lrt_cuda_dbuffer *vertices,
                                              uint32_t ntris, lrt_result *err);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTRT_C_CUDA_H */
