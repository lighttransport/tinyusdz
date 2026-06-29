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
