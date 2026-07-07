/*
 * lightrt_c_d3d11.h — Direct3D 11 GPU interop for the LightRT triangle kernel.
 *
 * Mirror of lightrt_c_vk.h "Path A" (CPU build -> GPU trace), but on a D3D11
 * compute shader instead of Vulkan. The trace shader (d3d/shaders/trace_bvh.hlsl)
 * is decompiled from the Vulkan trace_bvh SPIR-V with SPIRV-Cross, so it walks
 * the same serialized BVH4/BVH8 scene and returns identical hits.
 *
 * Motivation: on AMD GCN/Polaris the amdvlk Vulkan compute path mis-renders /
 * hangs, while the (much more mature) D3D11 driver is solid. D3D11 is also
 * ubiquitous on Windows and needs no SDK to build (d3d11 + d3dcompiler ship with
 * the OS). Windows-only; on other platforms this header compiles to nothing.
 *
 * Unlike the Vulkan helper this batches a whole ray array into ONE dispatch, so
 * a full-frame trace is a single GPU round-trip rather than one per pixel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTRT_C_D3D11_H
#define LIGHTRT_C_D3D11_H

#include <stdint.h>

#include "lightrt_c.h"     /* lrt_result */
#include "lightrt_c_tri.h" /* lrt_tri_scene, lrt_ray, lrt_hit */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lrt_d3d11_engine lrt_d3d11_engine;

/* Create a D3D11 compute engine. prefer_discrete picks a discrete adapter when
 * available. Returns NULL on any failure (no D3D11, no compute device); the
 * caller should fall back to the CPU kernel. err is set when non-NULL. */
lrt_d3d11_engine *lrt_d3d11_engine_create(int prefer_discrete, lrt_result *err);
void lrt_d3d11_engine_destroy(lrt_d3d11_engine *e);

/* Selected adapter name (e.g. "AMD Radeon RX 570"). */
const char *lrt_d3d11_engine_device_name(const lrt_d3d11_engine *e);

/* Human-readable message for the last failed call. */
const char *lrt_d3d11_engine_last_error(const lrt_d3d11_engine *e);

/* Trace n rays against a CPU-built scene on the GPU. Writes n hits to out
 * (prim_id == LRT_TRI_NO_HIT for misses). Returns the number of rays that hit,
 * or -1 on error (err set). Only plain BVH4/BVH8 triangle scenes are supported
 * (same restriction as lrt_vk_trace_scene). */
int lrt_d3d11_trace_scene(lrt_d3d11_engine *e, const lrt_tri_scene *s,
                          const lrt_ray *rays, uint32_t n, lrt_hit *out,
                          lrt_result *err);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTRT_C_D3D11_H */
