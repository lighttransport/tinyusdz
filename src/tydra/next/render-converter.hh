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
#include "scene-access.hh"
#include "next/stage/stage.hh"

namespace tinyusdz {
namespace tydra {
namespace next {

//
// Conversion configuration
//

struct MeshConfig {
  // Triangulation
  bool triangulate = true;
  enum class TriangulationMethod { Earcut, Fan } triangulation_method = TriangulationMethod::Fan;

  // Normals
  bool compute_normals = true;
  bool compute_tangents = false;

  // UV handling
  std::string default_uv_primvar = "st";

  // Index optimization
  bool build_vertex_indices = true;
  float dedup_epsilon = 1e-6f;

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

struct ConverterConfig {
  MeshConfig mesh;
  MaterialConfig material;

  // Time code for evaluation
  double time_code = 0.0;

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
  bool ConvertMesh(const UsdPrim& prim, RenderMesh* out);
  bool ConvertMaterial(const ::tinyusdz::next::Stage& stage, const UsdPrim& prim, RenderMaterial* out);
  bool ConvertLight(const UsdPrim& prim, RenderLight* out);
  bool ConvertCamera(const UsdPrim& prim, RenderCamera* out);
  bool ConvertSkeleton(const UsdPrim& prim, Skeleton* out);
  bool ConvertAnimation(const UsdPrim& prim, AnimationClip* out);

  // Texture loading
  bool LoadTexture(const std::string& asset_path, TextureImage* out);

  // Get last error
  const std::string& GetLastError() const { return last_error_; }

 private:
  // Build scene hierarchy
  void BuildNodeHierarchy(const ::tinyusdz::next::Stage& stage, RenderScene* scene);

  // Extract mesh data directly into chunked arrays
  bool ExtractMeshGeometry(const UsdPrim& prim, RenderMesh* mesh);
  bool ExtractMeshTopology(const UsdPrim& prim, RenderMesh* mesh);
  bool ExtractMeshPrimvars(const UsdPrim& prim, RenderMesh* mesh);

  // Triangulation
  bool TriangulateMesh(RenderMesh* mesh);
  bool TriangulateFan(const uint32_t* face_vertex_counts, size_t face_count,
                      const uint32_t* indices, size_t index_count,
                      UInt32Chunked* out_indices);

  // Normal computation
  bool ComputeFaceNormals(RenderMesh* mesh);
  bool ComputeVertexNormals(RenderMesh* mesh);

  // Material extraction
  bool ExtractPreviewSurface(const ::tinyusdz::next::Stage& stage,
                             const UsdPrim& shader_prim,
                             PreviewSurfaceShader* out);
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

// Triangulate a polygon (ear clipping)
bool TriangulatePolygon(const float* positions, size_t vertex_count,
                        const uint32_t* indices, std::vector<uint32_t>* out_triangles);

// Compute flat normal for a triangle
void ComputeTriangleNormal(const float* p0, const float* p1, const float* p2,
                           float* normal);

// Compute smooth normals by averaging face normals at each vertex
bool ComputeSmoothNormals(const float* positions, size_t vertex_count,
                          const uint32_t* indices, size_t triangle_count,
                          float* normals);

// Compute tangent frame (Mikktspace-compatible)
bool ComputeTangentFrame(const float* positions, const float* normals,
                         const float* texcoords, const uint32_t* indices,
                         size_t vertex_count, size_t triangle_count,
                         float* tangents);  // vec4 output (xyz = tangent, w = sign)

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
