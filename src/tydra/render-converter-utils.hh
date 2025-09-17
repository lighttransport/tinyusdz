// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Core conversion utilities for render scene converter
// Extracted from render-data.cc

#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include "../value-types.hh"
#include "../prim-types.hh"
#include "render-data.hh"

namespace tinyusdz {

// Forward declarations
class Stage;
class Layer;
class Prim;
class GeomMesh;
class GeomCamera;
class GeomPoints;
class SkelRoot;
class Skeleton;
class Material;

namespace tydra {

// Forward declarations
class RenderScene;
class RenderMesh;
class RenderMaterial;
class Node;
struct RenderSceneConverterEnv;

namespace converter_utils {

// Conversion callbacks
using MeshConversionCallback = std::function<bool(
    const GeomMesh &usd_mesh,
    RenderMesh &render_mesh,
    const RenderSceneConverterEnv &env)>;

using MaterialConversionCallback = std::function<bool(
    const Material &usd_material,
    RenderMaterial &render_material,
    const RenderSceneConverterEnv &env)>;

using NodeConversionCallback = std::function<bool(
    const Prim &prim,
    Node &node,
    const RenderSceneConverterEnv &env)>;

// Visitor pattern for prim traversal
class PrimVisitor {
public:
  virtual ~PrimVisitor() = default;
  
  virtual bool Visit(const Prim &prim, int depth) = 0;
  virtual bool PreVisit(const Prim &prim, int depth) { return true; }
  virtual void PostVisit(const Prim &prim, int depth) {}
};

// Mesh visitor for specialized mesh processing
class MeshVisitor : public PrimVisitor {
public:
  explicit MeshVisitor(RenderSceneConverterEnv &env) : env_(env) {}
  
  bool Visit(const Prim &prim, int depth) override;
  
protected:
  virtual bool ProcessMesh(const GeomMesh &mesh, const Prim &prim) = 0;
  
private:
  RenderSceneConverterEnv &env_;
};

// Transform utilities
struct TransformData {
  value::double3 translation = {0, 0, 0};
  value::quatd rotation = value::quatd::identity();
  value::double3 scale = {1, 1, 1};
  value::matrix4d matrix = value::matrix4d::identity();
  bool use_matrix = false;
  
  // Conversion methods
  value::matrix4d ToMatrix() const;
  void FromMatrix(const value::matrix4d &m);
  
  // Interpolation
  static TransformData Interpolate(
      const TransformData &a,
      const TransformData &b,
      double t);
};

TransformData ExtractTransform(
    const Prim &prim,
    double time = value::TimeCode::Default());

std::vector<TransformData> ExtractTransformSamples(
    const Prim &prim,
    const std::vector<double> &times);

// Node hierarchy building
struct NodeHierarchyOptions {
  bool flatten_transforms = false;
  bool preserve_xform_ops = true;
  bool create_instance_nodes = true;
  bool merge_static_meshes = false;
  int max_depth = -1;  // -1 for unlimited
};

bool BuildNodeHierarchy(
    const Prim &root,
    Node &root_node,
    const NodeHierarchyOptions &options,
    RenderSceneConverterEnv &env);

// Instance detection and handling
struct InstanceInfo {
  Path prototype_path;
  std::vector<Path> instance_paths;
  std::vector<value::matrix4d> instance_transforms;
};

std::vector<InstanceInfo> DetectInstances(
    const Stage &stage,
    const RenderSceneConverterEnv &env);

bool ConvertPointInstancer(
    const Prim &instancer_prim,
    std::vector<Node> &instance_nodes,
    RenderSceneConverterEnv &env);

// Animation extraction
struct AnimationExtractionOptions {
  bool extract_transform_animations = true;
  bool extract_morph_animations = true;
  bool extract_skeletal_animations = true;
  bool extract_visibility_animations = true;
  double sample_rate = 24.0;  // FPS
  double start_time = 0.0;
  double end_time = -1.0;  // -1 for auto-detect
};

std::vector<Animation> ExtractAnimations(
    const Stage &stage,
    const AnimationExtractionOptions &options,
    RenderSceneConverterEnv &env);

// Skeletal animation conversion
bool ConvertSkelRoot(
    const SkelRoot &skel_root,
    Skeleton &skeleton,
    std::vector<Animation> &animations,
    RenderSceneConverterEnv &env);

bool ConvertSkeleton(
    const Skeleton &usd_skeleton,
    Skeleton &render_skeleton,
    RenderSceneConverterEnv &env);

// Camera conversion
bool ConvertCamera(
    const GeomCamera &usd_camera,
    Camera &render_camera,
    RenderSceneConverterEnv &env);

// Light conversion
bool ConvertLight(
    const Prim &light_prim,
    Light &render_light,
    RenderSceneConverterEnv &env);

// Points/particles conversion
bool ConvertPoints(
    const GeomPoints &points,
    RenderMesh &point_cloud,
    RenderSceneConverterEnv &env);

// Scene optimization utilities
struct SceneOptimizationOptions {
  bool merge_meshes_by_material = false;
  bool deduplicate_vertices = true;
  bool optimize_vertex_cache = false;
  bool quantize_positions = false;
  bool quantize_normals = false;
  bool quantize_uvs = false;
  float position_quantization_bits = 14;
  float normal_quantization_bits = 10;
  float uv_quantization_bits = 12;
};

void OptimizeRenderScene(
    RenderScene &scene,
    const SceneOptimizationOptions &options);

// Material collection and deduplication
struct MaterialCollection {
  std::vector<RenderMaterial> materials;
  std::map<Path, size_t> material_map;
  
  size_t AddMaterial(const RenderMaterial &mat, const Path &path);
  RenderMaterial* GetMaterial(const Path &path);
  const RenderMaterial* GetMaterial(const Path &path) const;
};

MaterialCollection CollectMaterials(
    const Stage &stage,
    RenderSceneConverterEnv &env);

// Texture collection and deduplication
struct TextureCollection {
  std::vector<TextureImage> images;
  std::vector<UVTexture> textures;
  std::map<std::string, size_t> image_map;
  std::map<Path, size_t> texture_map;
  
  size_t AddImage(const TextureImage &img, const std::string &uri);
  size_t AddTexture(const UVTexture &tex, const Path &path);
};

TextureCollection CollectTextures(
    const Stage &stage,
    RenderSceneConverterEnv &env);

// Error handling utilities
enum class ConversionError {
  None,
  InvalidInput,
  UnsupportedFeature,
  ResourceNotFound,
  MemoryAllocation,
  ValidationFailed
};

struct ConversionResult {
  bool success = true;
  ConversionError error = ConversionError::None;
  std::string error_message;
  std::vector<std::string> warnings;
  
  operator bool() const { return success; }
};

// Conversion context management
class ConversionContext {
public:
  ConversionContext(RenderSceneConverterEnv &env) : env_(env) {}
  
  void PushPath(const Path &path);
  void PopPath();
  Path GetCurrentPath() const;
  
  void AddWarning(const std::string &msg);
  void AddError(const std::string &msg);
  
  const std::vector<std::string>& GetWarnings() const { return warnings_; }
  const std::vector<std::string>& GetErrors() const { return errors_; }
  
  bool HasErrors() const { return !errors_.empty(); }
  bool HasWarnings() const { return !warnings_.empty(); }
  
private:
  RenderSceneConverterEnv &env_;
  std::vector<Path> path_stack_;
  std::vector<std::string> warnings_;
  std::vector<std::string> errors_;
};

} // namespace converter_utils

} // namespace tydra
} // namespace tinyusdz