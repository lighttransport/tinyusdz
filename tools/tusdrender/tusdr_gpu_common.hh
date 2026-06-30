// SPDX-License-Identifier: Apache-2.0
// tusdrender — shared helpers for the LightRT GPU backends (-vk / -vkr / -hip).
//
// The Vulkan and HIP drivers differ only in how they create their engine and
// dispatch the trace; the geometry flatten + BVH build, primary-ray generation
// (with Halton AA), and CPU shading + image write are identical. These helpers
// hold that common code so the per-backend drivers stay thin.
#pragma once

#include <cstdint>
#include <vector>

#include "lightrt_c_tri.h"  // lrt_tri_scene, lrt_ray, lrt_hit
#include "tusdr_context.hh"  // Vec3, Options, CameraFrame, RTPreviewStats

namespace tusdr {

// Flattened indexed geometry + per-triangle shading data shared by the GPU
// backends. `scene` is a BVH4 lrt_tri_scene built from flat_verts/flat_idx;
// the caller owns it and must lrt_tri_scene_free() it.
struct GpuTriScene {
  std::vector<float> flat_verts;    // 3*nverts unique positions (indexed input)
  std::vector<uint32_t> flat_idx;   // 3*ntris vertex ids
  uint32_t ntris = 0;
  std::vector<Vec3> base_colors;    // per-triangle base color
  std::vector<Vec3> normals;        // per-triangle flat face normal (fallback)
  std::vector<Vec3> vn0, vn1, vn2;  // per-triangle vertex normals (smooth shading)
  lrt_tri_scene *scene = nullptr;
};

// Flatten `geos` (with per-mesh `base_colors`) into a single indexed mesh and
// build the BVH4 scene. Returns false (with a stderr diagnostic) when there is
// no geometry or the BVH build fails; on success `out->scene` is non-null.
bool BuildGpuTriScene(const std::vector<Vec3> &base_colors,
                      const std::vector<RTPreviewStats::MeshGeometry> &geos,
                      GpuTriScene *out);

// --- True two-level (instanced) GPU scene -----------------------------------
//
// For the -vkr two-level path: prototype geometry stored ONCE (object/prototype
// space) plus a list of placements (per-instance transform + prototype id). The
// GPU builds one BLAS per prototype and one TLAS instance per placement; the hit
// id decodes as instance = prim_id / stride, prototypeLocalTri = prim_id % stride.
struct GpuInstProto {
  std::vector<float> verts;          // 3*nverts prototype-local positions
  std::vector<uint32_t> idx;         // 3*ntris vertex ids
  uint32_t ntris = 0;
  std::vector<Vec3> normals;         // per-tri flat face normal (prototype space)
  std::vector<Vec3> vn0, vn1, vn2;   // per-tri vertex normals (prototype space)
  Vec3 base_color{0.5f, 0.5f, 0.5f};
};
struct GpuInstPlacement {
  float o2w[12];  // object->world 3x4 row-major (world = M*[p;1])
  float n2w[9];   // normal matrix: inverse-transpose of o2w's upper 3x3, row-major
  uint32_t proto = 0;
};
struct GpuInstancedScene {
  std::vector<GpuInstProto> protos;
  std::vector<GpuInstPlacement> insts;
  uint32_t stride = 0;  // = max prototype ntris (the prim_id stride); set by build
};

// Build the two-level GPU acceleration structure (lrt_vk_rtx_scene_build_instanced)
// from `scene`, trace `camera`'s primary rays, decode the instanced hits, and write
// the image. Sets scene.stride from the build. Returns false (leaving the scene
// untouched) when ray query is unavailable or the build fails, so the caller can
// fall back to the flat path. Defined only under HAVE_VULKAN.
bool RunVulkanLightRTInstanced(const Options &opt, GpuInstancedScene &scene,
                               const CameraFrame &camera, int height);

// Shade the traced `hits` for the two-level scene: decode prim_id ->
// (instance, prototype-local triangle), shade the prototype's triangle and
// transform its object-space normal by the instance's normal matrix. Same
// lighting model + spp averaging as ShadeAndWriteImage. Returns false on write
// failure.
bool ShadeAndWriteImageInstanced(const Options &opt, const GpuInstancedScene &s,
                                 const std::vector<lrt_ray> &rays,
                                 const std::vector<lrt_hit> &hits, int w, int h,
                                 int spp);

// Generate w*h*spp primary rays for `camera`, ray index = (y*w + x)*spp + s,
// each sample offset by a Halton(2,3) sub-pixel jitter for anti-aliasing.
void GenerateCameraRays(const CameraFrame &camera, int w, int h, int spp,
                        std::vector<lrt_ray> *rays);

// Shade the traced `hits` (smooth normal + key light + camera headlight +
// ambient), average the spp samples per pixel, and write the image to
// opt.output. Returns false on image-write failure.
bool ShadeAndWriteImage(const Options &opt, const GpuTriScene &s,
                        const std::vector<lrt_ray> &rays,
                        const std::vector<lrt_hit> &hits, int w, int h, int spp);

}  // namespace tusdr
