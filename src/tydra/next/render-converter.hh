// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Scene Converter
//
// Converts next::Stage to RenderScene with minimal intermediate copies

#pragma once

#include <string>
#include <functional>

#include "render-data.hh"
#include "render-extract.hh"
#include "scene-access.hh"
#include "next/stage/stage.hh"

namespace tinyusdz {
namespace next {
class AssetResolver;
}  // namespace next
}  // namespace tinyusdz

namespace tinyusdz {
namespace tydra {
namespace next {

//
// Conversion configuration
//

struct MeshConfig {
  // Triangulation
  bool triangulate = true;
  enum class TriangulationMethod { Earcut, Fan } triangulation_method = TriangulationMethod::Earcut;

  // Normals
  bool compute_normals = true;
  bool compute_tangents = false;
  enum class TangentComputationMethod {
    Lengyel,
    MikkTSpace,
    FastMikkTSpace,
    Hybrid
  };
  TangentComputationMethod tangent_method = TangentComputationMethod::Hybrid;

  // UV handling
  std::string default_uv_primvar = "st";

  // Index optimization
  bool build_vertex_indices = true;
  float dedup_epsilon = 1e-6f;

  // Optional real-time skinning reduction. Keeps the strongest influences per
  // point and renormalizes them, matching the legacy converter contract.
  bool enable_bone_reduction = false;
  uint32_t target_bone_count = 4;

  // Memory optimization
  bool use_chunked_arrays = true;
};

struct MaterialConfig {
  // Texture loading
  bool load_textures = true;
  bool allow_missing_textures = true;

  // Color space
  ColorSpace target_color_space = ColorSpace::Linear;

  // Texture loader callback (optional custom loader)
  using TextureLoader = std::function<bool(const std::string& path, TextureImage* out)>;
  TextureLoader custom_texture_loader;
};

struct CurvesConfig {
  // Polyline samples per cubic/NURBS span when tessellating BasisCurves /
  // NurbsCurves (linear curves pass through unchanged). Clamped to >= 1.
  uint32_t tessellation_segments = 8;
};

struct PointInstancerConfig {
  // Keep the default as lightweight draw references. Enable this only for
  // consumers that require ordinary mesh payloads for each visible instance.
  bool duplicate_meshes = false;
};

struct AnimationConfig {
  // Bake value clips into ordinary AnimationClip channels. Clip layers are
  // supplied by the application so the converter remains filesystem- and
  // archive-agnostic.
  bool bake_value_clips = true;
  using ClipStageLoader = std::function<bool(
      const std::string& asset_path, ::tinyusdz::next::Stage* stage,
      std::string* warn, std::string* err)>;
  ClipStageLoader clip_stage_loader;

  // Maximum number of samples generated for a clip set. The authored stage
  // timeCodesPerSecond is used as the sampling rate.
  uint32_t max_value_clip_samples = 10000;
};

struct ConverterConfig {
  MeshConfig mesh;
  MaterialConfig material;
  CurvesConfig curves;
  PointInstancerConfig point_instancer;
  AnimationConfig animation;

  // Time code for evaluation
  double time_code = 0.0;

  // Directory of the source USD file: relative texture asset paths resolve
  // against it into TextureImage::resolved_path. Empty = leave paths as
  // authored.
  std::string asset_base_dir;

  // Optional next-core AssetResolver (non-owning). When set it takes over
  // texture asset-path resolution (anchor/working-dir/search paths +
  // suffix fallback) instead of the plain asset_base_dir prefix above.
  const ::tinyusdz::next::AssetResolver* asset_resolver = nullptr;

  // Progress callback
  using ProgressCallback = std::function<void(float progress, const std::string& message)>;
  ProgressCallback progress_callback;
};

//
// Conversion result
//

struct ConvertResult {
  bool success = false;
  std::string error;
  std::vector<std::string> warnings;

  RenderScene scene;
};

//
// Main converter class
//

class RenderSceneConverter {
 public:
  explicit RenderSceneConverter(const ConverterConfig& config = {});
  ~RenderSceneConverter();

  // Non-copyable
  RenderSceneConverter(const RenderSceneConverter&) = delete;
  RenderSceneConverter& operator=(const RenderSceneConverter&) = delete;

  // Main conversion entry point
  ConvertResult Convert(const ::tinyusdz::next::Stage& stage);

  // Individual conversion methods (for custom pipelines)
  bool ConvertMesh(const Stage& stage, const UsdPrim& prim, RenderMesh* out);
  bool ConvertPoints(const UsdPrim& prim, RenderPoints* out);
  bool ConvertCurves(const UsdPrim& prim, RenderCurves* out);
  bool ConvertPointInstancer(const UsdPrim& prim, RenderPointInstancer* out);
  bool ConvertMaterial(const ::tinyusdz::next::Stage& stage, const UsdPrim& prim, RenderMaterial* out);
  bool ConvertLight(const UsdPrim& prim, RenderLight* out);
  bool ConvertCamera(const UsdPrim& prim, RenderCamera* out);
  bool ConvertSkeleton(const UsdPrim& prim, Skeleton* out);
  bool ConvertAnimation(const ::tinyusdz::next::Stage& stage,
                        const UsdPrim& prim, AnimationClip* out);

  // Texture loading
  bool LoadTexture(const std::string& asset_path, TextureImage* out);

  // Get last error
  const std::string& GetLastError() const { return last_error_; }

 private:
  // Build scene hierarchy
  void BuildNodeHierarchy(const RenderExtractResult& extracted, RenderScene* scene);
  void ExtractPhysicsAnnotations(const ::tinyusdz::next::Stage& stage,
                                 RenderScene* scene);
  void AssignMaterialBindings(const ::tinyusdz::next::Stage& stage,
                              RenderScene* scene);
  void AssignPointInstanceDrawMaterials(RenderScene* scene);
  void DuplicatePointInstanceMeshes(RenderScene* scene);

  // Extract mesh data directly into chunked arrays
  bool ConvertGeomPrimitive(const UsdPrim& prim, RenderMesh* out);
  bool ExtractMeshGeometry(const UsdPrim& prim, RenderMesh* mesh);
  bool ExtractMeshTopology(const UsdPrim& prim, RenderMesh* mesh);
  /// Drop faces with out-of-range (or negative) indices and truncate counts
  /// that overrun the index buffer; appends a warning when anything changed.
  void SanitizeMeshTopology(RenderMesh* mesh);
  bool ExtractMeshPrimvars(const UsdPrim& prim, RenderMesh* mesh);

  // Triangulation
  bool TriangulateMesh(RenderMesh* mesh);
  bool TriangulateFan(const uint32_t* face_vertex_counts, size_t face_count,
                      const uint32_t* indices, size_t index_count,
                      UInt32Chunked* out_indices);

  // Normal computation
  bool ComputeVertexNormals(RenderMesh* mesh);
  /// Tangent frame (xyzw, w=handedness) from triangulated topology, normals,
  /// and UVs. Emits vertex tangents for vertex-varying inputs and faceVarying
  /// tangents when seams/mirrors require per-corner data.
  bool ComputeVertexTangents(RenderMesh* mesh);

  /// Resolve an authored asset path against the source layer directory.
  std::string ResolveAssetPath(const std::string& file) const;
  /// Find-or-create an image record for `file`; returns its id (-1 on empty).
  int32_t ResolveImageId(RenderScene* scene, const std::string& file,
                         ColorSpace color_space);

  // Material extraction
  bool ConvertMaterial(const ::tinyusdz::next::Stage& stage,
                       const UsdPrim& prim,
                       RenderMaterial* out,
                       RenderScene* scene);
  bool ExtractPreviewSurface(const ::tinyusdz::next::Stage& stage,
                             const UsdPrim& shader_prim,
                             PreviewSurfaceShader* out,
                             RenderScene* scene);
  bool ExtractOpenPBRSurface(const ::tinyusdz::next::Stage& stage,
                             const UsdPrim& shader_prim,
                             OpenPBRSurfaceShader* out,
                             RenderScene* scene);
  bool ExtractShaderParam(const ::tinyusdz::next::Stage& stage,
                          const UsdPrim& shader_prim,
                          const std::string& param_name,
                          ShaderParam* out,
                          RenderScene* scene);

  ConverterConfig config_;
  std::string last_error_;
  std::vector<std::string> warnings_;
};

//
// Utility functions
//

// (Utility triangulation/normal/tangent free functions were declared here but
// never defined; the declarations were removed — a caller would only get a
// link error. Fan triangulation and vertex-normal generation live on
// RenderSceneConverter.)

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
