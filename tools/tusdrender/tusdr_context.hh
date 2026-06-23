// SPDX-License-Identifier: Apache-2.0
// tusdrender — the RenderContext aggregator (persistent render state shared by
// the next-loader pipeline) plus the cross-TU function prototype surface (added
// as the monolith is split into per-module .cc files).
#pragma once

#include "tusdr_types.hh"

namespace tusdr {

struct RenderContext {
  tinyusdz::next::Stage stage;  // keeps the lazy point/index arrays alive
  // Flat (no-instance) path buffers: default allocator so the shared
  // Shade/RenderImage signatures stay std::vector. The big, OOM-prone instanced
  // geometry lives in the budget-tracked Blas buffers below.
  std::vector<float> vertices;  // packed triangle positions (flat-path BVH input)
  std::vector<TriInfo> tris;
  std::vector<Texture> textures;  // diffuse textures referenced by tris[].tex_id
  std::vector<float> tri_uvs;  // 6 floats/tri (parallel to tris); empty if none
  ByteVec tri_colors;  // 12 bytes/tri (per-corner RGBA8); empty if none
  std::vector<float> tri_normals;  // 9 floats/tri (per-corner normals); empty if none
  std::vector<VolumeData> volumes;  // UsdVol volumes (OpenVDB) for raymarching
  Bounds bounds;
  RTPreviewStats stats;
  lrt_tri_scene *scene{nullptr};  // owned flat BVH (no-instance path)
  // Two-level (instanced) BVH path: built when the composed scene has native
  // instances. blas[0] is the base (non-instanced) geometry; blas[1..] are the
  // unique prototypes. instances[] place them; tlas is the top-level BVH.
  std::vector<Blas> blas;
  std::vector<InstanceRT> instances;
  lrt_tlas *tlas{nullptr};
  bool use_tlas{false};
  DirectScene direct;             // empty for the next path
  LightCache lights;              // empty -> camera-headlight fallback
  IblCache ibl;                   // image-based lighting (--env / DomeLight)
  tinyusdz::Axis up_axis{tinyusdz::Axis::Y};
  CameraFrame camera;
  Options opt;  // mutable render parameters (width/height/ambient/bg/...)
  int width{960};
  int height{540};
  // Time at which geometry + transforms are evaluated (NaN = default value).
  double frame_time{std::numeric_limits<double>::quiet_NaN()};
  double load_seconds{0.0}, stream_seconds{0.0}, bvh_seconds{0.0};

  // Free the TLAS before the BLAS scenes it references (blas[] destructs after
  // this body runs).
  ~RenderContext() {
    if (tlas) lrt_tlas_free(tlas);
    if (scene) lrt_tri_scene_free(scene);
  }
  RenderContext() = default;
  RenderContext(const RenderContext &) = delete;
  RenderContext &operator=(const RenderContext &) = delete;
};

// ---- tusdr_args.cc / tusdr_material.cc ----
bool ParseIntStrict(const std::string &s, int *out);

bool ParseFloatStrict(const std::string &s, float *out);

bool ParseDoubleStrict(const std::string &s, double *out);

bool ParseColor(const std::string &s, Vec3 *out);

void PrintUsage(const char *prog);

bool ParseArgs(int argc, char **argv, Options *opt);

void SetupNullAssetResolution(tinyusdz::AssetResolutionResolver *resolver);

Vec3 MaterialColor(const RenderScene &scene, const RenderMesh &mesh,
                   int material_id);

Vec3 MaterialEmission(const RenderScene &scene, int material_id);

float MaterialRoughness(const RenderScene &scene, int material_id);

float MaterialMetallic(const RenderScene &scene, int material_id);

Vec3 MeshLightEmission(const RenderScene &scene, const RenderMesh &mesh,
                       int material_id, float total_area);

}  // namespace tusdr
