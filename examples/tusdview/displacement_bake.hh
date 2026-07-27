// SPDX-License-Identifier: Apache-2.0
// CPU-side coarse displacement baking for the ray-tracing backends. Ray tracers
// intersect actual triangles, so (unlike the raster vertex/tess shaders) the
// displacement must be applied to the geometry before the BVH/TLAS is built. This
// mirrors tusdrender's offline path: offset each vertex along its normal by the
// sampled UsdPreviewSurface displacement, then recompute normals on the deformed
// surface.
#pragma once

#include <cstdint>
#include <vector>

#include "gpu_scene.hh"

namespace tusdview {

// Bilinear sample of a DrawScene texture's red channel at (u, v) with the texture's
// wrap modes (matches the GPU's displacement sampling). Returns 0 when the index is
// invalid or the image is empty.
float SampleTextureRed(const DrawScene& scene, int texIndex, float u, float v,
                       uint32_t ptexFace = UINT32_MAX);

// True if any of `mesh`'s submesh materials carry displacement.
bool MeshHasDisplacement(const DrawScene& scene, const DrawMeshCPU& mesh);

// Bake coarse displacement into a copy of `mesh.vertices`: every vertex whose
// submesh material has displacement is offset along its (original) normal by
// height*globalScale, where height = texel.r*texScale + texBias (or the constant).
// Per-vertex normals on the deformed surface are then recomputed (area-weighted)
// for the displaced vertices. Returns false and leaves `out` empty when the mesh
// has no displacement or globalScale == 0 (callers then use the original vertices).
bool BakeDisplacedVertices(const DrawScene& scene, const DrawMeshCPU& mesh,
                           float globalScale, std::vector<DrawVertex>* out);

// Fill `rtDisplacedVertices` for every displaced mesh in `scene` (at the authored
// scale), so the Vulkan ray-query backend can build its BLAS from the displaced
// geometry. Cheap no-op for meshes without displacement. Call once after the scene
// is finalized.
void BakeRTDisplacement(DrawScene* scene);

}  // namespace tusdview
