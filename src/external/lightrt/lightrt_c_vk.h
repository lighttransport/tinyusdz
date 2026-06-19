/*
 * lightrt_c_vk.h — Vulkan GPU interop for the LightRT C11 triangle kernel.
 *
 * Two directions, both built on Vulkan compute (the loader, lightrt_vkew, opens
 * libvulkan at runtime — there is NO Vulkan SDK header or link-time dependency):
 *
 *   Path A  CPU build -> GPU trace : lrt_vk_trace_scene() uploads a scene built
 *           by lrt_tri_scene_build() and traverses it with a compute shader that
 *           mirrors the scalar CPU kernel, returning identical hits (within fp
 *           tolerance).
 *
 *   Path B  GPU build -> CPU trace : lrt_vk_build_scene() runs the LBVH front
 *           end (centroids + Morton codes) on the GPU, finishes the hierarchy on
 *           the CPU, and returns a heap lrt_tri_scene traversable with the normal
 *           lrt_tri_intersect1() / etc.
 *
 * Everything degrades gracefully: if no Vulkan loader/device is available,
 * lrt_vk_engine_create() returns NULL and the caller falls back to the CPU
 * kernel. An engine is created once and may be reused across calls; a single
 * engine is NOT safe to use from multiple threads concurrently.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTRT_C_VK_H
#define LIGHTRT_C_VK_H

#include <stddef.h>
#include <stdint.h>

#include "lightrt_c.h"     /* lrt_result */
#include "lightrt_c_tri.h" /* lrt_tri_scene, lrt_ray, lrt_hit, lrt_tri_layout */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lrt_vk_engine lrt_vk_engine;

/* Capability flags reported by lrt_vk_engine_caps(). */
typedef enum lrt_vk_caps {
    LRT_VK_CAP_COMPUTE        = 1u << 0, /* always set when an engine exists      */
    LRT_VK_CAP_BUFFER_ADDRESS = 1u << 1, /* bufferDeviceAddress usable            */
    LRT_VK_CAP_ACCEL_STRUCT   = 1u << 2, /* VK_KHR_acceleration_structure loaded  */
    LRT_VK_CAP_RAY_QUERY      = 1u << 3  /* VK_KHR_ray_query usable in compute     */
} lrt_vk_caps;

typedef struct lrt_vk_engine_options {
    int device_index;     /* explicit physical-device index, or -1 = auto-pick   */
    int prefer_discrete;  /* when auto-picking, prefer a discrete GPU            */
    int want_ray_tracing; /* try to enable AS + ray_query extensions if present  */
    uint32_t flags;       /* reserved; pass 0                                    */
} lrt_vk_engine_options;

/* Create an engine (calls vkewInit internally). Returns NULL and, when err is
 * non-NULL, stores the reason, on any failure: no libvulkan, no instance, no
 * compute-capable device. opts may be NULL for defaults (auto device, no RT). */
lrt_vk_engine *lrt_vk_engine_create(const lrt_vk_engine_options *opts,
                                    lrt_result *err);
void lrt_vk_engine_destroy(lrt_vk_engine *e);

/* Bitmask of lrt_vk_caps for the created device. */
uint32_t lrt_vk_engine_caps(const lrt_vk_engine *e);

/* Selected physical-device name (e.g. "NVIDIA GeForce RTX 3070"). */
const char *lrt_vk_engine_device_name(const lrt_vk_engine *e);

/* Human-readable message for the last failed call on this engine. */
const char *lrt_vk_engine_last_error(const lrt_vk_engine *e);

/* --- Path A: trace a CPU-built scene on the GPU ---------------------------
 *
 * Uploads s (via its position-independent LRTS serialization) and traverses n
 * rays with a compute shader. Writes n hits to out. Returns the number of rays
 * that hit geometry, or -1 on error (err set). Only plain triangle scenes with
 * BVH4/BVH8 layout are supported (quantized/curve/user scenes are rejected, as
 * they are by lrt_tri_scene_save_to_memory). Results match lrt_tri_intersect1
 * within fp tolerance. */
int lrt_vk_trace_scene(lrt_vk_engine *e, const lrt_tri_scene *s,
                       const lrt_ray *rays, uint32_t n, lrt_hit *out,
                       lrt_result *err);

/* --- Path B: build a BVH with a GPU front end, return a CPU scene ----------
 *
 * vertices = 9*ntris floats (v0 v1 v2 per triangle). Computes centroids and
 * 30-bit Morton codes on the GPU, then finishes the LBVH (radix sort + collapse
 * + leaf packing) on the CPU and returns a heap scene. layout must be BVH4 or
 * BVH8 (others are rejected). Free the result with lrt_tri_scene_free(). Returns
 * 0 on success, -1 on error (err set). The result is traversable with the normal
 * lrt_tri_* query functions and matches a FAST CPU build. */
int lrt_vk_build_scene(lrt_vk_engine *e, const float *vertices, uint32_t ntris,
                       lrt_tri_layout layout, lrt_tri_scene **out,
                       lrt_result *err);

/* --- Hardware ray tracing (VK_KHR_ray_query) ------------------------------
 *
 * Build a real acceleration structure (BLAS + identity-instance TLAS) on the GPU
 * from the triangle soup (9*ntris floats) and trace n rays against it with a
 * ray_query compute shader. Writes n hits to out; returns the number of rays
 * that hit, or -1 on error. Requires an engine created with want_ray_tracing=1
 * on an RT-capable device (check lrt_vk_engine_caps() & LRT_VK_CAP_RAY_QUERY);
 * otherwise returns -1 with LRT_RESULT_NOT_BUILT.
 *
 * Hits match a Moller-Trumbore CPU trace within fp tolerance: t is in units of
 * |dir|, (u,v) are hit barycentrics, prim_id is the triangle index. Unlike Path
 * A/B this takes raw triangles (the AS is a vendor-opaque blob and cannot be fed
 * back into lrt_tri_scene), so it is a trace-only backend. */
int lrt_vk_trace_scene_rtx(lrt_vk_engine *e, const float *vertices, uint32_t ntris,
                           const lrt_ray *rays, uint32_t n, lrt_hit *out,
                           lrt_result *err);

/* Resident ray-tracing scene: build the acceleration structure ONCE and trace
 * many ray batches against it. Unlike the one-shot lrt_vk_trace_scene_rtx (which
 * rebuilds the AS per call), the BLAS+TLAS stay device-resident and the trace
 * uses device-local ray/hit buffers (bulk staged), so lrt_vk_rtx_scene_trace
 * measures GPU traversal throughput rather than per-batch build + transfer cost.
 * The engine must outlive the scene. */
typedef struct lrt_vk_rtx_scene lrt_vk_rtx_scene;

lrt_vk_rtx_scene *lrt_vk_rtx_scene_build(lrt_vk_engine *e, const float *vertices,
                                         uint32_t ntris, lrt_result *err);

/* Trace n rays against the resident AS. Returns #rays that hit, or -1 on error.
 * Trace buffers are reused/grown across calls. */
int lrt_vk_rtx_scene_trace(lrt_vk_engine *e, lrt_vk_rtx_scene *s,
                           const lrt_ray *rays, uint32_t n, lrt_hit *out,
                           lrt_result *err);

void lrt_vk_rtx_scene_free(lrt_vk_engine *e, lrt_vk_rtx_scene *s);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTRT_C_VK_H */
