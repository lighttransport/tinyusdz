// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Refactored render-data implementation
// Main interface that delegates to specialized modules

#include "render-data.hh"
#include "render-mesh-utils.hh"
#include "render-material-utils.hh"
#include "render-converter-utils.hh"
#include "render-scene-dump.hh"

#include "../prim-types.hh"
#include "../usdGeom.hh"
#include "../usdShade.hh"
#include "../usdLux.hh"
#include "../usdSkel.hh"
#include "../value-types.hh"
#include "../str-util.hh"
#include "../io-util.hh"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace tinyusdz {
namespace tydra {

// RenderSceneConverter implementation
class RenderSceneConverter::Impl {
public:
  Impl() = default;
  ~Impl() = default;
  
  bool ConvertToRenderScene(
      const Stage &stage,
      RenderScene *render_scene,
      const RenderSceneConverterOptions &options);
  
  // Component conversion methods
  bool ConvertMesh(const GeomMesh &mesh, RenderMesh &render_mesh);
  bool ConvertMaterial(const Material &material, RenderMaterial &render_material);
  bool ConvertCamera(const GeomCamera &camera, Camera &render_camera);
  bool ConvertLight(const Prim &prim, Light &render_light);
  bool ConvertNode(const Prim &prim, Node &node);
  
  // Error reporting
  std::string GetError() const { return error_; }
  std::string GetWarning() const { return warning_; }
  
private:
  RenderSceneConverterEnv env_;
  converter_utils::ConversionContext context_;
  
  std::string error_;
  std::string warning_;
  
  // Material and texture caches
  converter_utils::MaterialCollection materials_;
  converter_utils::TextureCollection textures_;
  
  // Helper methods
  bool ProcessPrimTree(const Prim &prim, Node &parent_node, int depth);
  bool CollectResources(const Stage &stage);
  void OptimizeScene(RenderScene &scene);
};

// Constructor/Destructor
RenderSceneConverter::RenderSceneConverter() 
    : impl_(std::make_unique<Impl>()) {
}

RenderSceneConverter::~RenderSceneConverter() = default;

// Main conversion method
bool RenderSceneConverter::ConvertToRenderScene(
    const Stage &stage,
    RenderScene *render_scene) {
  RenderSceneConverterOptions default_options;
  return ConvertToRenderScene(stage, render_scene, default_options);
}

bool RenderSceneConverter::ConvertToRenderScene(
    const Stage &stage,
    RenderScene *render_scene,
    const RenderSceneConverterOptions &options) {
  if (!render_scene) {
    return false;
  }
  
  return impl_->ConvertToRenderScene(stage, render_scene, options);
}

// Implementation of main conversion
bool RenderSceneConverter::Impl::ConvertToRenderScene(
    const Stage &stage,
    RenderScene *render_scene,
    const RenderSceneConverterOptions &options) {
  
  // Initialize environment
  env_ = RenderSceneConverterEnv(stage);
  env_.timecode = options.timecode;
  
  // Clear previous state
  error_.clear();
  warning_.clear();
  render_scene->nodes.clear();
  render_scene->meshes.clear();
  render_scene->materials.clear();
  
  // Collect all resources first
  if (!CollectResources(stage)) {
    return false;
  }
  
  // Get root prims
  const Prim *root = stage.root_prim();
  if (!root) {
    error_ = "No root prim in stage";
    return false;
  }
  
  // Build node hierarchy
  converter_utils::NodeHierarchyOptions hierarchy_opts;
  hierarchy_opts.flatten_transforms = options.flatten_transforms;
  hierarchy_opts.preserve_xform_ops = true;
  hierarchy_opts.create_instance_nodes = !options.disable_instancing;
  
  Node root_node;
  root_node.name = "root";
  
  // Process prim tree recursively
  for (const auto &child : root->children()) {
    Node child_node;
    if (!ProcessPrimTree(child, child_node, 0)) {
      // Continue on error if not strict
      if (options.strict_mode) {
        return false;
      }
    }
    root_node.children.push_back(render_scene->nodes.size());
    render_scene->nodes.push_back(child_node);
  }
  
  // Add root node
  render_scene->nodes.insert(render_scene->nodes.begin(), root_node);
  
  // Extract animations if requested
  if (options.extract_animations) {
    converter_utils::AnimationExtractionOptions anim_opts;
    anim_opts.sample_rate = options.animation_sample_rate;
    anim_opts.start_time = stage.start_timeCode();
    anim_opts.end_time = stage.end_timeCode();
    
    render_scene->animations = converter_utils::ExtractAnimations(
        stage, anim_opts, env_);
  }
  
  // Optimize if requested
  if (options.optimize_scene) {
    OptimizeScene(*render_scene);
  }
  
  // Copy collected materials and textures
  render_scene->materials = materials_.materials;
  render_scene->textures = textures_.textures;
  render_scene->images = textures_.images;
  
  // Gather warnings
  if (context_.HasWarnings()) {
    for (const auto &w : context_.GetWarnings()) {
      warning_ += w + "\n";
    }
  }
  
  return true;
}

// Process prim tree recursively
bool RenderSceneConverter::Impl::ProcessPrimTree(
    const Prim &prim, 
    Node &node, 
    int depth) {
  
  context_.PushPath(prim.path());
  
  // Set basic node properties
  node.name = prim.name();
  node.display_name = prim.metas().displayName;
  
  // Extract transform
  auto transform = converter_utils::ExtractTransform(prim, env_.timecode);
  node.local_matrix = transform.ToMatrix();
  
  // Check prim type and convert accordingly
  if (auto mesh = prim.as<GeomMesh>()) {
    RenderMesh render_mesh;
    if (ConvertMesh(*mesh, render_mesh)) {
      node.mesh_id = render_scene->meshes.size();
      render_scene->meshes.push_back(render_mesh);
    }
  } else if (auto camera = prim.as<GeomCamera>()) {
    Camera render_camera;
    if (ConvertCamera(*camera, render_camera)) {
      node.camera_id = render_scene->cameras.size();
      render_scene->cameras.push_back(render_camera);
    }
  } else if (prim.is<DomeLight>() || prim.is<SphereLight>() || 
             prim.is<RectLight>() || prim.is<DistantLight>()) {
    Light render_light;
    if (ConvertLight(prim, render_light)) {
      node.light_id = render_scene->lights.size();
      render_scene->lights.push_back(render_light);
    }
  }
  
  // Process children
  for (const auto &child : prim.children()) {
    Node child_node;
    if (ProcessPrimTree(child, child_node, depth + 1)) {
      node.children.push_back(render_scene->nodes.size());
      render_scene->nodes.push_back(child_node);
    }
  }
  
  context_.PopPath();
  return true;
}

// Mesh conversion
bool RenderSceneConverter::Impl::ConvertMesh(
    const GeomMesh &mesh, 
    RenderMesh &render_mesh) {
  
  // Get basic mesh data
  render_mesh.name = mesh.name;
  
  // Get vertex positions
  std::vector<float> positions;
  if (!mesh.points.get_value(&positions, env_.timecode)) {
    context_.AddError("Failed to get mesh points");
    return false;
  }
  
  // Get face vertex counts and indices
  std::vector<uint32_t> faceVertexCounts;
  std::vector<uint32_t> faceVertexIndices;
  
  if (!mesh.faceVertexCounts.get_value(&faceVertexCounts, env_.timecode)) {
    context_.AddError("Failed to get face vertex counts");
    return false;
  }
  
  if (!mesh.faceVertexIndices.get_value(&faceVertexIndices, env_.timecode)) {
    context_.AddError("Failed to get face vertex indices");
    return false;
  }
  
  // Triangulate if needed
  bool needs_triangulation = false;
  for (auto count : faceVertexCounts) {
    if (count != 3) {
      needs_triangulation = true;
      break;
    }
  }
  
  if (needs_triangulation) {
    auto result = mesh_utils::TriangulatePolygon(
        positions, faceVertexCounts, faceVertexIndices);
    
    if (!result.success) {
      context_.AddError("Triangulation failed: " + result.error);
      return false;
    }
    
    render_mesh.positions = result.positions;
    render_mesh.indices = result.indices;
  } else {
    render_mesh.positions = positions;
    render_mesh.indices = faceVertexIndices;
  }
  
  // Get normals or compute them
  std::vector<float> normals;
  if (mesh.normals.authored()) {
    if (!mesh.normals.get_value(&normals, env_.timecode)) {
      context_.AddWarning("Failed to get authored normals");
    }
  }
  
  if (normals.empty()) {
    // Compute normals
    mesh_utils::NormalComputationOptions norm_opts;
    norm_opts.smooth_normals = true;
    normals = mesh_utils::ComputeNormals(
        render_mesh.positions, render_mesh.indices, norm_opts);
  }
  
  // Create normal attribute
  VertexAttribute normal_attr;
  normal_attr.name = "normals";
  normal_attr.format = VertexAttributeFormat::FLOAT3;
  normal_attr.data = normals;
  render_mesh.attributes.push_back(normal_attr);
  
  // Get UV coordinates
  auto uv_names = mesh_utils::ListUVNames(mesh);
  for (const auto &uv_name : uv_names) {
    auto tex_coords = mesh_utils::GetTextureCoordinates(mesh, uv_name);
    if (!tex_coords.empty()) {
      VertexAttribute uv_attr;
      uv_attr.name = uv_name;
      uv_attr.format = VertexAttributeFormat::FLOAT2;
      uv_attr.data = tex_coords[0].values;
      render_mesh.attributes.push_back(uv_attr);
    }
  }
  
  // Get material bindings
  auto material_subsets = mesh_utils::GetMaterialBindGeomSubsets(mesh, env_);
  for (const auto &subset : material_subsets) {
    MaterialSubset render_subset;
    render_subset.name = subset.name;
    render_subset.indices = subset.indices;
    
    // Look up material in collection
    if (auto mat = materials_.GetMaterial(subset.material_path)) {
      render_subset.material_id = 
          std::distance(materials_.materials.begin(), 
                       std::find_if(materials_.materials.begin(),
                                   materials_.materials.end(),
                                   [mat](const RenderMaterial &m) {
                                     return &m == mat;
                                   }));
    }
    
    render_mesh.material_subsets.push_back(render_subset);
  }
  
  return true;
}

// Material conversion
bool RenderSceneConverter::Impl::ConvertMaterial(
    const Material &material,
    RenderMaterial &render_material) {
  
  material_utils::MaterialConversionOptions opts;
  opts.convert_preview_surface = true;
  opts.use_display_colors = false;
  
  return material_utils::ConvertMaterial(
      material, render_material, opts, env_);
}

// Camera conversion
bool RenderSceneConverter::Impl::ConvertCamera(
    const GeomCamera &camera,
    Camera &render_camera) {
  
  return converter_utils::ConvertCamera(camera, render_camera, env_);
}

// Light conversion
bool RenderSceneConverter::Impl::ConvertLight(
    const Prim &prim,
    Light &render_light) {
  
  return converter_utils::ConvertLight(prim, render_light, env_);
}

// Collect resources
bool RenderSceneConverter::Impl::CollectResources(const Stage &stage) {
  // Collect materials
  materials_ = converter_utils::CollectMaterials(stage, env_);
  
  // Collect textures
  textures_ = converter_utils::CollectTextures(stage, env_);
  
  return true;
}

// Optimize scene
void RenderSceneConverter::Impl::OptimizeScene(RenderScene &scene) {
  converter_utils::SceneOptimizationOptions opts;
  opts.deduplicate_vertices = true;
  opts.optimize_vertex_cache = true;
  
  converter_utils::OptimizeRenderScene(scene, opts);
}

// Error/warning getters
std::string RenderSceneConverter::GetError() const {
  return impl_->GetError();
}

std::string RenderSceneConverter::GetWarning() const {
  return impl_->GetWarning();
}

// Dump functions (delegating to dump_utils)
std::string DumpRenderScene(const RenderScene &scene) {
  return dump_utils::DumpRenderScene(scene);
}

std::string DumpMesh(const RenderMesh &mesh) {
  return dump_utils::DumpMesh(mesh);
}

std::string DumpMaterial(const RenderMaterial &material) {
  return dump_utils::DumpMaterial(material);
}

std::string DumpNode(const Node &node) {
  return dump_utils::DumpNode(node);
}

// Stream operators
std::ostream& operator<<(std::ostream &os, const RenderScene &scene) {
  os << dump_utils::DumpRenderScene(scene);
  return os;
}

std::ostream& operator<<(std::ostream &os, const RenderMesh &mesh) {
  os << dump_utils::DumpMesh(mesh);
  return os;
}

std::ostream& operator<<(std::ostream &os, const RenderMaterial &material) {
  os << dump_utils::DumpMaterial(material);
  return os;
}

} // namespace tydra
} // namespace tinyusdz