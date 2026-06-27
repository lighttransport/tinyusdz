// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Scene Converter Implementation

#include "render-converter.hh"
#include "next/schema/usd-shade.hh"
#include "next/schema/usd-skel.hh"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_set>

namespace tinyusdz {
namespace tydra {
namespace next {

using ::tinyusdz::next::Stage;
using ::tinyusdz::next::UsdPrim;
using ::tinyusdz::next::Value;

namespace {

constexpr float kAlphaEpsilon = 1.0e-6f;

std::string SourcePrimPathFromConnection(const std::string& connection_path) {
  size_t dot_pos = connection_path.find(".outputs:");
  if (dot_pos == std::string::npos) {
    dot_pos = connection_path.find(".inputs:");
  }
  if (dot_pos == std::string::npos) {
    dot_pos = connection_path.rfind('.');
  }
  if (dot_pos == std::string::npos) {
    return connection_path;
  }
  return connection_path.substr(0, dot_pos);
}

bool SplitConnectionPath(const std::string& connection_path,
                         std::string* prim_path,
                         std::string* prop_name) {
  if (!prim_path || !prop_name) return false;
  size_t dot_pos = connection_path.find(".outputs:");
  if (dot_pos == std::string::npos) {
    dot_pos = connection_path.find(".inputs:");
  }
  if (dot_pos == std::string::npos) {
    dot_pos = connection_path.rfind('.');
  }
  if (dot_pos == std::string::npos) return false;

  *prim_path = connection_path.substr(0, dot_pos);
  *prop_name = connection_path.substr(dot_pos + 1);
  return !prim_path->empty() && !prop_name->empty();
}

bool ResolveConnectedValue(const Stage& stage,
                           const std::string& connection_path,
                           double time_code,
                           Value* out,
                           int depth = 0) {
  if (!out || depth > 16) return false;

  std::string prim_path;
  std::string prop_name;
  if (!SplitConnectionPath(connection_path, &prim_path, &prop_name)) return false;

  UsdPrim prim = stage.GetPrimAtPath(prim_path);
  if (!prim.IsValid()) return false;

  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(time_code);
  if (eval.HasConnection(prim, prop_name)) {
    return ResolveConnectedValue(stage, eval.GetConnectionPath(prim, prop_name),
                                 time_code, out, depth + 1);
  }

  ::tinyusdz::next::EvalOptions opts = eval.GetOptions();
  opts.follow_connections = false;
  ::tinyusdz::next::EvalResult result = eval.EvalWith(prim, prop_name, opts);
  if (result.success) {
    *out = std::move(result.value);
    return true;
  }

  return false;
}

RenderTexture::Channel ChannelFromConnection(const std::string& connection_path) {
  size_t pos = connection_path.find(".outputs:");
  if (pos == std::string::npos) {
    return RenderTexture::Channel::RGBA;
  }

  const std::string channel = connection_path.substr(pos + 9);
  if (channel == "r" || channel == "x") return RenderTexture::Channel::R;
  if (channel == "g" || channel == "y") return RenderTexture::Channel::G;
  if (channel == "b" || channel == "z") return RenderTexture::Channel::B;
  if (channel == "a" || channel == "w") return RenderTexture::Channel::A;
  if (channel == "rgb" || channel == "xyz") return RenderTexture::Channel::RGB;
  return RenderTexture::Channel::RGBA;
}

WrapMode ParseWrapMode(const std::string& token) {
  if (token == "clamp") return WrapMode::Clamp;
  if (token == "mirror") return WrapMode::Mirror;
  if (token == "black") return WrapMode::Black;
  return WrapMode::Repeat;
}

ColorSpace ParseColorSpace(const std::string& token) {
  if (token == "raw") return ColorSpace::Raw;
  if (token == "linear" || token == "Linear") return ColorSpace::Linear;
  if (token == "sRGB" || token == "srgb") return ColorSpace::sRGB;
  if (token == "acescg" || token == "ACEScg") return ColorSpace::ACEScg;
  if (token == "rec709" || token == "Rec709") return ColorSpace::Rec709;
  if (token == "rec2020" || token == "Rec2020") return ColorSpace::Rec2020;
  if (token == "displayP3" || token == "DisplayP3") return ColorSpace::DisplayP3;
  return ColorSpace::Unknown;
}

void SetParamFloat(ShaderParam* out, float x) {
  out->texture_id = -1;
  out->value = Float4(x, 0.0f, 0.0f, 0.0f);
}

void SetParamFloat3(ShaderParam* out, float x, float y, float z) {
  out->texture_id = -1;
  out->value = Float4(x, y, z, 1.0f);
}

void SetParamFloat4(ShaderParam* out, float x, float y, float z, float w) {
  out->texture_id = -1;
  out->value = Float4(x, y, z, w);
}

bool ValueToShaderParam(const Value& value, ShaderParam* out) {
  if (!out || value.is_empty() || value.is_array()) return false;

  if (const float* v = value.as_float()) {
    SetParamFloat(out, *v);
    return true;
  }
  if (const double* v = value.as_double()) {
    SetParamFloat(out, static_cast<float>(*v));
    return true;
  }
  if (const int32_t* v = value.as_int()) {
    SetParamFloat(out, static_cast<float>(*v));
    return true;
  }
  if (const uint32_t* v = value.as_uint()) {
    SetParamFloat(out, static_cast<float>(*v));
    return true;
  }
  if (const bool* v = value.as_bool()) {
    SetParamFloat(out, *v ? 1.0f : 0.0f);
    return true;
  }
  if (const float* v = value.as_float2()) {
    SetParamFloat4(out, v[0], v[1], 0.0f, 1.0f);
    return true;
  }
  if (const float* v = value.as_float3()) {
    SetParamFloat3(out, v[0], v[1], v[2]);
    return true;
  }
  if (const float* v = value.as_float4()) {
    SetParamFloat4(out, v[0], v[1], v[2], v[3]);
    return true;
  }
  if (const double* v = value.as_double2()) {
    SetParamFloat4(out, static_cast<float>(v[0]), static_cast<float>(v[1]),
                   0.0f, 1.0f);
    return true;
  }
  if (const double* v = value.as_double3()) {
    SetParamFloat3(out, static_cast<float>(v[0]), static_cast<float>(v[1]),
                   static_cast<float>(v[2]));
    return true;
  }
  if (const double* v = value.as_double4()) {
    SetParamFloat4(out, static_cast<float>(v[0]), static_cast<float>(v[1]),
                   static_cast<float>(v[2]), static_cast<float>(v[3]));
    return true;
  }

  return false;
}

bool ValueToFloat4(const Value& value, Float4* out) {
  if (!out || value.is_empty() || value.is_array()) return false;

  if (const float* v = value.as_float()) {
    *out = Float4(*v, 0.0f, 0.0f, 0.0f);
    return true;
  }
  if (const double* v = value.as_double()) {
    *out = Float4(static_cast<float>(*v), 0.0f, 0.0f, 0.0f);
    return true;
  }
  if (const float* v = value.as_float3()) {
    *out = Float4(v[0], v[1], v[2], 0.0f);
    return true;
  }
  if (const double* v = value.as_double3()) {
    *out = Float4(static_cast<float>(v[0]), static_cast<float>(v[1]),
                  static_cast<float>(v[2]), 0.0f);
    return true;
  }
  if (const float* v = value.as_float4()) {
    *out = Float4(v[0], v[1], v[2], v[3]);
    return true;
  }
  if (const double* v = value.as_double4()) {
    *out = Float4(static_cast<float>(v[0]), static_cast<float>(v[1]),
                  static_cast<float>(v[2]), static_cast<float>(v[3]));
    return true;
  }
  return false;
}

bool ValueToAnimationFloat4(const std::string& prop_name,
                            const Value& value,
                            Float4* out) {
  if (!out || value.is_empty() || value.is_array()) return false;

  float scalar = 0.0f;
  bool is_scalar = false;
  if (const float* v = value.as_float()) {
    scalar = *v;
    is_scalar = true;
  } else if (const double* v = value.as_double()) {
    scalar = static_cast<float>(*v);
    is_scalar = true;
  }

  if (is_scalar) {
    if (prop_name.find("rotateX") != std::string::npos) {
      *out = Float4(scalar, 0.0f, 0.0f, 0.0f);
    } else if (prop_name.find("rotateY") != std::string::npos) {
      *out = Float4(0.0f, scalar, 0.0f, 0.0f);
    } else if (prop_name.find("rotateZ") != std::string::npos) {
      *out = Float4(0.0f, 0.0f, scalar, 0.0f);
    } else if (prop_name.find("scale") != std::string::npos) {
      *out = Float4(scalar, scalar, scalar, 0.0f);
    } else {
      *out = Float4(scalar, 0.0f, 0.0f, 0.0f);
    }
    return true;
  }

  return ValueToFloat4(value, out);
}

void AssignNodeDataId(RenderScene* scene,
                      const std::string& prim_path,
                      int32_t data_id) {
  if (!scene) return;
  const auto node_it = scene->node_by_path.find(prim_path);
  if (node_it == scene->node_by_path.end()) return;
  const int32_t node_id = node_it->second;
  if (node_id < 0 || static_cast<size_t>(node_id) >= scene->nodes.size()) return;
  scene->nodes[static_cast<size_t>(node_id)].data_id = data_id;
}

void SetIdentity(Matrix4* m) {
  if (!m) return;
  *m = Matrix4::Identity();
}

void CopyMatrixFromDoubles(const std::vector<double>& values,
                           size_t matrix_index,
                           Matrix4* out) {
  if (!out) return;
  SetIdentity(out);
  const size_t offset = matrix_index * 16;
  if (offset + 16 > values.size()) return;
  for (size_t i = 0; i < 16; ++i) {
    out->m[i] = static_cast<float>(values[offset + i]);
  }
}

std::string LeafNameFromJointPath(const std::string& path) {
  size_t pos = path.rfind('/');
  if (pos == std::string::npos) return path;
  if (pos + 1 >= path.size()) return "";
  return path.substr(pos + 1);
}

bool LocalVisibility(const UsdPrim& prim) {
  const Value* value = prim.GetPropertyValue("visibility");
  if (!value) return true;
  if (const std::string* token = value->as_token()) {
    return *token != "invisible";
  }
  if (const std::string* str = value->as_string()) {
    return *str != "invisible";
  }
  return true;
}

AnimationChannel::TargetPath TargetPathForXformOp(const std::string& prop_name) {
  if (prop_name.find("translate") != std::string::npos) {
    return AnimationChannel::TargetPath::Translation;
  }
  if (prop_name.find("scale") != std::string::npos) {
    return AnimationChannel::TargetPath::Scale;
  }
  return AnimationChannel::TargetPath::Rotation;
}

bool IsXformAnimationProperty(const std::string& prop_name) {
  if (prop_name.find("xformOp:") != 0) return false;
  return prop_name.find("translate") != std::string::npos ||
         prop_name.find("scale") != std::string::npos ||
         prop_name.find("rotate") != std::string::npos ||
         prop_name.find("orient") != std::string::npos;
}

struct TextureNodeData {
  std::string file;
  std::string wrap_s = "useMetadata";
  std::string wrap_t = "useMetadata";
  float scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float bias[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  std::string source_color_space = "auto";
};

bool ExtractTextureNodeData(const Stage& stage,
                            const UsdPrim& texture_prim,
                            double time_code,
                            TextureNodeData* out) {
  if (!out || !texture_prim.IsValid()) return false;

  ::tinyusdz::next::UVTextureData uv;
  if (::tinyusdz::next::GetUVTextureData(stage, texture_prim, &uv, time_code)) {
    out->file = uv.file;
    out->wrap_s = uv.wrap_s;
    out->wrap_t = uv.wrap_t;
    out->source_color_space = uv.source_color_space;
    std::memcpy(out->scale, uv.scale, sizeof(out->scale));
    std::memcpy(out->bias, uv.bias, sizeof(out->bias));
    return !out->file.empty();
  }

  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(time_code);

  std::optional<std::string> file = eval.EvalAssetPath(texture_prim, "inputs:file");
  if (!file) {
    file = eval.EvalString(texture_prim, "inputs:file");
  }
  if (!file || file->empty()) return false;
  out->file = *file;

  if (std::optional<std::string> wrap_s = eval.EvalToken(texture_prim, "inputs:wrapS")) {
    out->wrap_s = *wrap_s;
  }
  if (std::optional<std::string> wrap_t = eval.EvalToken(texture_prim, "inputs:wrapT")) {
    out->wrap_t = *wrap_t;
  }
  float scale[4];
  if (eval.EvalFloat4(texture_prim, "inputs:scale", scale)) {
    std::memcpy(out->scale, scale, sizeof(out->scale));
  }
  float bias[4];
  if (eval.EvalFloat4(texture_prim, "inputs:bias", bias)) {
    std::memcpy(out->bias, bias, sizeof(out->bias));
  }
  if (std::optional<std::string> cs = eval.EvalToken(texture_prim, "inputs:sourceColorSpace")) {
    out->source_color_space = *cs;
  }

  return true;
}

bool IsOpenPBRShaderId(const std::string& id) {
  return id == "ND_open_pbr_surface_surfaceshader" ||
         id == "open_pbr_surface" ||
         id == "OpenPBRSurface";
}

}  // namespace

//
// Constructor / Destructor
//

RenderSceneConverter::RenderSceneConverter(const ConverterConfig& config)
    : config_(config) {}

RenderSceneConverter::~RenderSceneConverter() = default;

//
// Main conversion
//

ConvertResult RenderSceneConverter::Convert(const Stage& stage) {
  ConvertResult result;
  warnings_.clear();

  // Built with -fno-exceptions: the conversion helpers report failures via
  // return codes / the warnings_ list rather than throwing, so no try/catch.
  {
    // Report progress
    if (config_.progress_callback) {
      config_.progress_callback(0.0f, "Starting conversion...");
    }

    // Set scene metadata
    auto meta = stage.GetMeta();
    result.scene.name = meta.defaultPrim;
    result.scene.default_prim = meta.defaultPrim;
    result.scene.meters_per_unit = static_cast<float>(meta.metersPerUnit);
    result.scene.up_axis = (meta.upAxis == "Z") ?
                           RenderScene::UpAxis::Z : RenderScene::UpAxis::Y;
    result.scene.start_time = meta.startTimeCode;
    result.scene.end_time = meta.endTimeCode;
    result.scene.frames_per_second = meta.timeCodesPerSecond;

    RenderExtractOptions xopts;
    xopts.time_code = config_.time_code;
    xopts.collect_other = true;
    RenderExtractResult extracted;
    CollectRenderPrims(stage, xopts, &extracted);

    // Build node hierarchy first
    if (config_.progress_callback) {
      config_.progress_callback(0.1f, "Building node hierarchy...");
    }
    BuildNodeHierarchy(extracted, &result.scene);

    for (const auto& rec : extracted.records) {
      AnimationClip clip;
      if (ConvertAnimation(rec.prim, &clip)) {
        const auto node_it = result.scene.node_by_path.find(rec.path);
        if (node_it != result.scene.node_by_path.end()) {
          for (AnimationChannel& channel : clip.channels) {
            channel.target_node = node_it->second;
          }
        }
        result.scene.animations.push_back(std::move(clip));
      }
    }

    // Convert meshes
    float mesh_progress_start = 0.2f;
    float mesh_progress_end = 0.5f;

    for (size_t i = 0; i < extracted.meshes.size(); ++i) {
      const UsdPrim& mesh_prim = extracted.meshes[i].prim;
      if (config_.progress_callback) {
        float p = mesh_progress_start +
                  (mesh_progress_end - mesh_progress_start) * i /
                      std::max<size_t>(extracted.meshes.size(), 1);
        config_.progress_callback(p, "Converting mesh: " + mesh_prim.GetName());
      }

      RenderMesh mesh;
      if (ConvertMesh(mesh_prim, &mesh)) {
        int32_t mesh_id = static_cast<int32_t>(result.scene.meshes.size());
        result.scene.mesh_by_path[mesh.prim_path] = mesh_id;
        result.scene.meshes.push_back(std::move(mesh));
        AssignNodeDataId(&result.scene, mesh_prim.GetPath().str(), mesh_id);
      } else {
        warnings_.push_back("Failed to convert mesh: " + mesh_prim.GetPath().str());
      }
    }

    // Convert materials
    float mat_progress_start = 0.5f;
    float mat_progress_end = 0.7f;

    for (size_t i = 0; i < extracted.materials.size(); ++i) {
      const UsdPrim& mat_prim = extracted.materials[i].prim;
      if (config_.progress_callback) {
        float p = mat_progress_start +
                  (mat_progress_end - mat_progress_start) * i /
                      std::max<size_t>(extracted.materials.size(), 1);
        config_.progress_callback(p, "Converting material: " + mat_prim.GetName());
      }

      RenderMaterial material;
      if (ConvertMaterial(stage, mat_prim, &material, &result.scene)) {
        int32_t mat_id = static_cast<int32_t>(result.scene.materials.size());
        result.scene.material_by_path[material.prim_path] = mat_id;
        result.scene.materials.push_back(std::move(material));
      } else {
        warnings_.push_back("Failed to convert material: " + mat_prim.GetPath().str());
      }
    }

    // Convert lights
    for (const auto& rec : extracted.lights) {
      RenderLight light;
      if (ConvertLight(rec.prim, &light)) {
        int32_t light_id = static_cast<int32_t>(result.scene.lights.size());
        result.scene.lights.push_back(std::move(light));
        AssignNodeDataId(&result.scene, rec.path, light_id);
      }
    }

    // Convert cameras
    for (const auto& rec : extracted.cameras) {
      RenderCamera camera;
      if (ConvertCamera(rec.prim, &camera)) {
        int32_t camera_id = static_cast<int32_t>(result.scene.cameras.size());
        result.scene.cameras.push_back(std::move(camera));
        AssignNodeDataId(&result.scene, rec.path, camera_id);
      }
    }

    // Convert skeletons
    for (const auto& rec : extracted.skeletons) {
      Skeleton skeleton;
      if (ConvertSkeleton(rec.prim, &skeleton)) {
        int32_t skeleton_id = static_cast<int32_t>(result.scene.skeletons.size());
        result.scene.skeletons.push_back(std::move(skeleton));
        AssignNodeDataId(&result.scene, rec.path, skeleton_id);
      }
    }

    if (config_.progress_callback) {
      config_.progress_callback(1.0f, "Conversion complete");
    }

    result.success = true;
    result.warnings = std::move(warnings_);
  }

  return result;
}

//
// Node hierarchy
//

void RenderSceneConverter::BuildNodeHierarchy(const RenderExtractResult& extracted,
                                              RenderScene* scene) {
  std::unordered_map<std::string, int32_t> path_to_node;

  for (const RenderPrimRecord& rec : extracted.records) {
    const UsdPrim& prim = rec.prim;
    SceneNode node;
    node.name = prim.GetName();
    node.prim_path = rec.path;

    // Determine node type
    const std::string& type = rec.type_name;
    if (type == "Mesh") node.type = NodeType::Mesh;
    else if (type == "Xform") node.type = NodeType::Xform;
    else if (type == "Camera") node.type = NodeType::Camera;
    else if (type == "Skeleton") node.type = NodeType::Skeleton;
    else if (IsLight(prim)) {
      LightKind kind = GetLightKind(prim);
      switch (kind) {
        case LightKind::DistantLight: node.type = NodeType::DirectionalLight; break;
        case LightKind::DomeLight: node.type = NodeType::DomeLight; break;
        case LightKind::RectLight: node.type = NodeType::RectLight; break;
        case LightKind::DiskLight: node.type = NodeType::DiskLight; break;
        case LightKind::SphereLight: node.type = NodeType::SphereLight; break;
        case LightKind::PointLight: node.type = NodeType::PointLight; break;
        default: node.type = NodeType::PointLight; break;
      }
    }

    // Compute transforms
    for (int i = 0; i < 16; ++i) {
      node.local_transform.m[i] = static_cast<float>(rec.local[i]);
      node.world_transform.m[i] = static_cast<float>(rec.world[i]);
    }

    int32_t node_id = static_cast<int32_t>(scene->nodes.size());
    path_to_node[node.prim_path] = node_id;
    scene->node_by_path[node.prim_path] = node_id;

    // Set parent
    std::string parent_path = GetParentPath(node.prim_path);
    bool parent_visible = true;
    if (!parent_path.empty() && parent_path != "/") {
      auto it = path_to_node.find(parent_path);
      if (it != path_to_node.end()) {
        node.parent_id = it->second;
        scene->nodes[it->second].children.push_back(node_id);
        parent_visible = scene->nodes[it->second].visible;
      }
    } else {
      scene->root_nodes.push_back(node_id);
    }

    node.visible = parent_visible && LocalVisibility(prim);

    scene->nodes.push_back(std::move(node));
  }
}

//
// Mesh conversion
//

bool RenderSceneConverter::ConvertMesh(const UsdPrim& prim, RenderMesh* out) {
  if (!out || !IsMesh(prim)) {
    last_error_ = "Invalid mesh prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Extract topology
  if (!ExtractMeshTopology(prim, out)) {
    return false;
  }

  // Extract geometry
  if (!ExtractMeshGeometry(prim, out)) {
    return false;
  }

  // Extract primvars (UVs, colors, etc.)
  ExtractMeshPrimvars(prim, out);

  // Triangulate if requested
  if (config_.mesh.triangulate && !out->is_triangulated) {
    TriangulateMesh(out);
  }

  // Compute normals if needed
  if (config_.mesh.compute_normals && out->normals.empty()) {
    ComputeVertexNormals(out);
  }

  return true;
}

bool RenderSceneConverter::ExtractMeshTopology(const UsdPrim& prim, RenderMesh* mesh) {
  // Get face vertex counts
  ValueArrayRead<int32_t> face_counts;
  ReadIntArray(prim, "faceVertexCounts", config_.time_code, &face_counts);
  if (face_counts.empty()) {
    last_error_ = "Mesh has no faceVertexCounts";
    return false;
  }

  mesh->face_vertex_counts.reserve(face_counts.size());
  for (int32_t c : face_counts) {
    mesh->face_vertex_counts.push_back(static_cast<uint32_t>(c));
  }

  // Get face vertex indices
  ValueArrayRead<int32_t> indices;
  ReadIntArray(prim, "faceVertexIndices", config_.time_code, &indices);
  if (indices.empty()) {
    last_error_ = "Mesh has no faceVertexIndices";
    return false;
  }

  mesh->face_vertex_indices.reserve(indices.size());
  for (int32_t i : indices) {
    mesh->face_vertex_indices.push_back(static_cast<uint32_t>(i));
  }

  return true;
}

bool RenderSceneConverter::ExtractMeshGeometry(const UsdPrim& prim, RenderMesh* mesh) {
  ValueArrayRead<float> points;
  ReadFloatArray(prim, "points", config_.time_code, &points);
  if (points.empty()) {
    last_error_ = "Invalid points data";
    return false;
  }

  // Copy directly to chunked array
  mesh->points.append(points.view.data, points.view.size);

  // Compute bounding box
  size_t num_points = mesh->point_count();
  if (num_points > 0) {
    mesh->bbox_min = Float3(1e30f, 1e30f, 1e30f);
    mesh->bbox_max = Float3(-1e30f, -1e30f, -1e30f);

    for (size_t i = 0; i < num_points; ++i) {
      float x = mesh->points[i * 3 + 0];
      float y = mesh->points[i * 3 + 1];
      float z = mesh->points[i * 3 + 2];

      mesh->bbox_min.x = std::min(mesh->bbox_min.x, x);
      mesh->bbox_min.y = std::min(mesh->bbox_min.y, y);
      mesh->bbox_min.z = std::min(mesh->bbox_min.z, z);
      mesh->bbox_max.x = std::max(mesh->bbox_max.x, x);
      mesh->bbox_max.y = std::max(mesh->bbox_max.y, y);
      mesh->bbox_max.z = std::max(mesh->bbox_max.z, z);
    }
    mesh->has_bbox = true;
  }

  // Get normals if present
  ValueArrayRead<float> normals;
  if (ReadFloatArray(prim, "normals", config_.time_code, &normals)) {
    if (!normals.empty()) {
      mesh->normals.append(normals.view.data, normals.view.size);

      // Determine interpolation
      std::string interp;
      GetToken(prim, "normals:interpolation", &interp);
      if (interp == "faceVarying") {
        mesh->normals_interp = Interpolation::FaceVarying;
      } else {
        mesh->normals_interp = Interpolation::Vertex;
      }
    }
  }

  return true;
}

bool RenderSceneConverter::ExtractMeshPrimvars(const UsdPrim& prim, RenderMesh* mesh) {
  // Get UV coordinates
  std::string uv_name = "primvars:" + config_.mesh.default_uv_primvar;
  ValueArrayRead<float> uvs;
  if (ReadFloatArray(prim, uv_name.c_str(), config_.time_code, &uvs)) {
    if (!uvs.empty()) {
      mesh->texcoords_0.append(uvs.view.data, uvs.view.size);

      std::string interp;
      GetToken(prim, uv_name + ":interpolation", &interp);
      if (interp == "faceVarying") {
        mesh->texcoords_0_interp = Interpolation::FaceVarying;
      } else {
        mesh->texcoords_0_interp = Interpolation::Vertex;
      }
    }
  }

  // Get vertex colors
  ValueArrayRead<float> colors;
  if (ReadFloatArray(prim, "primvars:displayColor", config_.time_code, &colors) &&
      !colors.empty()) {
    mesh->colors.append(colors.view.data, colors.view.size);
  }

  return true;
}

//
// Triangulation
//

bool RenderSceneConverter::TriangulateMesh(RenderMesh* mesh) {
  if (mesh->face_vertex_counts.empty()) return false;

  // Check if already triangulated
  bool all_triangles = true;
  for (size_t i = 0; i < mesh->face_vertex_counts.size(); ++i) {
    if (mesh->face_vertex_counts[i] != 3) {
      all_triangles = false;
      break;
    }
  }

  if (all_triangles) {
    // Just copy indices
    mesh->triangulated_indices.resize(mesh->face_vertex_indices.size());
    for (size_t i = 0; i < mesh->face_vertex_indices.size(); ++i) {
      mesh->triangulated_indices[i] = mesh->face_vertex_indices[i];
    }
    mesh->is_triangulated = true;
    return true;
  }

  size_t tri_count = 0;
  for (size_t i = 0; i < mesh->face_vertex_counts.size(); ++i) {
    uint32_t nverts = mesh->face_vertex_counts[i];
    if (nverts >= 3) tri_count += nverts - 2;
  }

  mesh->triangulated_indices.reserve(tri_count * 3);
  size_t idx_offset = 0;
  for (size_t f = 0; f < mesh->face_vertex_counts.size(); ++f) {
    const uint32_t nverts = mesh->face_vertex_counts[f];
    if (idx_offset + nverts > mesh->face_vertex_indices.size()) return false;
    if (nverts >= 3) {
      const uint32_t v0 = mesh->face_vertex_indices[idx_offset];
      for (uint32_t i = 1; i < nverts - 1; ++i) {
        mesh->triangulated_indices.push_back(v0);
        mesh->triangulated_indices.push_back(mesh->face_vertex_indices[idx_offset + i]);
        mesh->triangulated_indices.push_back(mesh->face_vertex_indices[idx_offset + i + 1]);
      }
    }
    idx_offset += nverts;
  }

  mesh->is_triangulated = true;
  return true;
}

bool RenderSceneConverter::TriangulateFan(
    const uint32_t* face_vertex_counts, size_t face_count,
    const uint32_t* indices, size_t index_count,
    UInt32Chunked* out_indices) {

  // Count triangles
  size_t tri_count = 0;
  size_t required_index_count = 0;
  for (size_t i = 0; i < face_count; ++i) {
    uint32_t nverts = face_vertex_counts[i];
    required_index_count += nverts;
    if (nverts >= 3) {
      tri_count += nverts - 2;
    }
  }
  if (required_index_count > index_count) {
    return false;
  }

  out_indices->reserve(tri_count * 3);

  size_t idx_offset = 0;
  for (size_t f = 0; f < face_count; ++f) {
    uint32_t nverts = face_vertex_counts[f];
    if (nverts < 3) {
      idx_offset += nverts;
      continue;
    }

    // Triangle fan: v0, v1, v2; v0, v2, v3; v0, v3, v4; ...
    uint32_t v0 = indices[idx_offset];
    for (uint32_t i = 1; i < nverts - 1; ++i) {
      out_indices->push_back(v0);
      out_indices->push_back(indices[idx_offset + i]);
      out_indices->push_back(indices[idx_offset + i + 1]);
    }

    idx_offset += nverts;
  }

  return true;
}

//
// Normal computation
//

bool RenderSceneConverter::ComputeVertexNormals(RenderMesh* mesh) {
  if (mesh->points.empty() || !mesh->is_triangulated) {
    // Need triangulated mesh for normal computation
    if (!mesh->is_triangulated) {
      TriangulateMesh(mesh);
    }
    if (!mesh->is_triangulated) return false;
  }

  size_t num_points = mesh->point_count();
  size_t num_tris = mesh->triangulated_indices.size() / 3;

  // Initialize normals to zero
  mesh->normals.resize(num_points * 3, 0.0f);

  // Accumulate face normals at each vertex
  for (size_t t = 0; t < num_tris; ++t) {
    uint32_t i0 = mesh->triangulated_indices[t * 3 + 0];
    uint32_t i1 = mesh->triangulated_indices[t * 3 + 1];
    uint32_t i2 = mesh->triangulated_indices[t * 3 + 2];

    float p0[3] = {mesh->points[i0*3], mesh->points[i0*3+1], mesh->points[i0*3+2]};
    float p1[3] = {mesh->points[i1*3], mesh->points[i1*3+1], mesh->points[i1*3+2]};
    float p2[3] = {mesh->points[i2*3], mesh->points[i2*3+1], mesh->points[i2*3+2]};

    // Edge vectors
    float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
    float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};

    // Cross product
    float n[3] = {
      e1[1]*e2[2] - e1[2]*e2[1],
      e1[2]*e2[0] - e1[0]*e2[2],
      e1[0]*e2[1] - e1[1]*e2[0]
    };

    // Add to each vertex
    mesh->normals[i0*3+0] += n[0]; mesh->normals[i0*3+1] += n[1]; mesh->normals[i0*3+2] += n[2];
    mesh->normals[i1*3+0] += n[0]; mesh->normals[i1*3+1] += n[1]; mesh->normals[i1*3+2] += n[2];
    mesh->normals[i2*3+0] += n[0]; mesh->normals[i2*3+1] += n[1]; mesh->normals[i2*3+2] += n[2];
  }

  // Normalize
  for (size_t v = 0; v < num_points; ++v) {
    float nx = mesh->normals[v*3+0];
    float ny = mesh->normals[v*3+1];
    float nz = mesh->normals[v*3+2];
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 1e-8f) {
      mesh->normals[v*3+0] = nx / len;
      mesh->normals[v*3+1] = ny / len;
      mesh->normals[v*3+2] = nz / len;
    } else {
      mesh->normals[v*3+0] = 0.0f;
      mesh->normals[v*3+1] = 1.0f;
      mesh->normals[v*3+2] = 0.0f;
    }
  }

  mesh->normals_interp = Interpolation::Vertex;
  return true;
}

//
// Material conversion
//

bool RenderSceneConverter::ConvertMaterial(const Stage& stage,
                                           const UsdPrim& prim,
                                           RenderMaterial* out) {
  return ConvertMaterial(stage, prim, out, nullptr);
}

bool RenderSceneConverter::ConvertMaterial(const Stage& stage,
                                           const UsdPrim& prim,
                                           RenderMaterial* out,
                                           RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsMaterial(prim)) {
    last_error_ = "Invalid material prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Find shader(s) in material
  bool found_shader = false;

  auto children = prim.GetChildren();
  for (const auto& child : children) {
    if (::tinyusdz::next::IsShader(child)) {
      std::string shader_id;
      GetToken(child, "info:id", &shader_id);

      if (shader_id == "UsdPreviewSurface" ||
          shader_id == "ND_UsdPreviewSurface_surfaceshader") {
        // MaterialX's UsdPreviewSurface node (`ND_UsdPreviewSurface_surfaceshader`)
        // has the same inputs as UsdPreviewSurface — treat it as one (matches the
        // legacy tydra path, e.g. usd-wg MaterialXTest/basic_flatten).
        out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
        out->preview_surface = std::make_unique<PreviewSurfaceShader>();
        ExtractPreviewSurface(stage, child, out->preview_surface.get(), scene);
        if (out->preview_surface->opacity.is_texture() ||
            out->preview_surface->opacity.value.x < 1.0f - kAlphaEpsilon) {
          out->alpha_mode = RenderMaterial::AlphaMode::Blend;
        }
        if (out->preview_surface->opacity_threshold.value.x > kAlphaEpsilon) {
          out->alpha_mode = RenderMaterial::AlphaMode::Mask;
          out->alpha_cutoff = out->preview_surface->opacity_threshold.value.x;
        }
        found_shader = true;
      } else if (IsOpenPBRShaderId(shader_id)) {
        out->shader_type = RenderMaterial::ShaderType::OpenPBR;
        out->openpbr = std::make_unique<OpenPBRSurfaceShader>();
        ExtractOpenPBRSurface(stage, child, out->openpbr.get(), scene);
        if (out->openpbr->opacity.is_texture() ||
            out->openpbr->opacity.value.x < 1.0f - kAlphaEpsilon) {
          out->alpha_mode = RenderMaterial::AlphaMode::Blend;
        }
        found_shader = true;
      }
    }
  }

  return found_shader;
}

bool RenderSceneConverter::ExtractPreviewSurface(const Stage& stage,
                                                 const UsdPrim& shader_prim,
                                                 PreviewSurfaceShader* out,
                                                 RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsShader(shader_prim)) return false;

  ExtractShaderParam(stage, shader_prim, "diffuseColor", &out->diffuse_color, scene);
  ExtractShaderParam(stage, shader_prim, "emissiveColor", &out->emissive_color, scene);
  ExtractShaderParam(stage, shader_prim, "specularColor", &out->specular_color, scene);
  ExtractShaderParam(stage, shader_prim, "metallic", &out->metallic, scene);
  ExtractShaderParam(stage, shader_prim, "roughness", &out->roughness, scene);
  ExtractShaderParam(stage, shader_prim, "clearcoat", &out->clearcoat, scene);
  ExtractShaderParam(stage, shader_prim, "clearcoatRoughness",
                     &out->clearcoat_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "opacity", &out->opacity, scene);
  ExtractShaderParam(stage, shader_prim, "opacityThreshold",
                     &out->opacity_threshold, scene);
  ExtractShaderParam(stage, shader_prim, "ior", &out->ior, scene);
  ExtractShaderParam(stage, shader_prim, "normal", &out->normal, scene);
  ExtractShaderParam(stage, shader_prim, "displacement", &out->displacement, scene);
  ExtractShaderParam(stage, shader_prim, "occlusion", &out->occlusion, scene);

  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(config_.time_code);
  if (std::optional<int32_t> use_spec =
          eval.EvalInt(shader_prim, "inputs:useSpecularWorkflow")) {
    out->use_specular_workflow = (*use_spec != 0);
  }

  return true;
}

bool RenderSceneConverter::ExtractOpenPBRSurface(const Stage& stage,
                                                 const UsdPrim& shader_prim,
                                                 OpenPBRSurfaceShader* out,
                                                 RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsShader(shader_prim)) return false;

  ExtractShaderParam(stage, shader_prim, "base_weight", &out->base_weight, scene);
  ExtractShaderParam(stage, shader_prim, "base_color", &out->base_color, scene);
  ExtractShaderParam(stage, shader_prim, "baseColor", &out->base_color, scene);
  ExtractShaderParam(stage, shader_prim, "base_roughness", &out->base_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "roughness", &out->base_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "base_metalness", &out->base_metalness, scene);
  ExtractShaderParam(stage, shader_prim, "metalness", &out->base_metalness, scene);

  ExtractShaderParam(stage, shader_prim, "specular_weight", &out->specular_weight, scene);
  ExtractShaderParam(stage, shader_prim, "specular_color", &out->specular_color, scene);
  ExtractShaderParam(stage, shader_prim, "specular_roughness",
                     &out->specular_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "specular_ior", &out->specular_ior, scene);
  ExtractShaderParam(stage, shader_prim, "specular_anisotropy",
                     &out->specular_anisotropy, scene);
  ExtractShaderParam(stage, shader_prim, "specular_rotation",
                     &out->specular_rotation, scene);

  ExtractShaderParam(stage, shader_prim, "transmission_weight",
                     &out->transmission_weight, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_color",
                     &out->transmission_color, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_depth",
                     &out->transmission_depth, scene);

  ExtractShaderParam(stage, shader_prim, "subsurface_weight",
                     &out->subsurface_weight, scene);
  ExtractShaderParam(stage, shader_prim, "subsurface_color",
                     &out->subsurface_color, scene);
  ExtractShaderParam(stage, shader_prim, "subsurface_radius",
                     &out->subsurface_radius, scene);

  ExtractShaderParam(stage, shader_prim, "coat_weight", &out->coat_weight, scene);
  ExtractShaderParam(stage, shader_prim, "coat_color", &out->coat_color, scene);
  ExtractShaderParam(stage, shader_prim, "coat_roughness", &out->coat_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "coat_ior", &out->coat_ior, scene);

  ExtractShaderParam(stage, shader_prim, "sheen_weight", &out->sheen_weight, scene);
  ExtractShaderParam(stage, shader_prim, "sheen_color", &out->sheen_color, scene);
  ExtractShaderParam(stage, shader_prim, "sheen_roughness", &out->sheen_roughness, scene);

  ExtractShaderParam(stage, shader_prim, "emission_luminance",
                     &out->emission_luminance, scene);
  ExtractShaderParam(stage, shader_prim, "emission_color", &out->emission_color, scene);

  ExtractShaderParam(stage, shader_prim, "opacity", &out->opacity, scene);
  ExtractShaderParam(stage, shader_prim, "normal", &out->normal, scene);
  ExtractShaderParam(stage, shader_prim, "tangent", &out->tangent, scene);

  return true;
}

bool RenderSceneConverter::ExtractShaderParam(const Stage& stage,
                                              const UsdPrim& shader_prim,
                                              const std::string& param_name,
                                              ShaderParam* out,
                                              RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsShader(shader_prim)) return false;

  const std::string attr_name = "inputs:" + param_name;
  ::tinyusdz::next::AttributeEval eval(&stage);
  eval.SetTime(config_.time_code);

  if (eval.HasConnection(shader_prim, attr_name)) {
    const std::string connection_path = eval.GetConnectionPath(shader_prim, attr_name);
    const std::string texture_prim_path = SourcePrimPathFromConnection(connection_path);
    UsdPrim texture_prim = stage.GetPrimAtPath(texture_prim_path);

    TextureNodeData tex_data;
    if (scene && ExtractTextureNodeData(stage, texture_prim, config_.time_code, &tex_data)) {
      int32_t image_id = -1;
      const ColorSpace cs = ParseColorSpace(tex_data.source_color_space);
      const ColorSpace image_color_space =
          cs == ColorSpace::Unknown ? ColorSpace::sRGB : cs;
      for (size_t i = 0; i < scene->images.size(); ++i) {
        if (scene->images[i].resolved_path == tex_data.file &&
            scene->images[i].color_space == image_color_space) {
          image_id = static_cast<int32_t>(i);
          break;
        }
      }
      if (image_id < 0) {
        TextureImage image;
        image.name = texture_prim.IsValid() ? texture_prim.GetName() : tex_data.file;
        image.resolved_path = tex_data.file;
        image.color_space = image_color_space;
        if (config_.material.load_textures) {
          TextureImage loaded;
          if (LoadTexture(tex_data.file, &loaded)) {
            if (loaded.name.empty()) loaded.name = image.name;
            if (loaded.resolved_path.empty()) loaded.resolved_path = tex_data.file;
            if (!config_.material.custom_texture_loader ||
                loaded.color_space == ColorSpace::Unknown) {
              loaded.color_space = image.color_space;
            }
            image = std::move(loaded);
          } else if (!config_.material.allow_missing_textures) {
            warnings_.push_back("Failed to load texture: " + tex_data.file);
            return false;
          }
        }
        image_id = static_cast<int32_t>(scene->images.size());
        scene->images.push_back(std::move(image));
      }

      RenderTexture texture;
      texture.name = texture_prim.IsValid() ? texture_prim.GetName() : param_name;
      texture.prim_path = texture_prim_path;
      texture.asset_path = tex_data.file;
      texture.wrap_s = ParseWrapMode(tex_data.wrap_s);
      texture.wrap_t = ParseWrapMode(tex_data.wrap_t);
      texture.scale_value = Float4(tex_data.scale[0], tex_data.scale[1],
                                   tex_data.scale[2], tex_data.scale[3]);
      texture.bias = Float4(tex_data.bias[0], tex_data.bias[1],
                            tex_data.bias[2], tex_data.bias[3]);
      texture.image_id = image_id;
      texture.output_channel = ChannelFromConnection(connection_path);

      out->texture_id = static_cast<int32_t>(scene->textures.size());
      scene->textures.push_back(std::move(texture));
      return true;
    }

    Value connected_value;
    if (ResolveConnectedValue(stage, connection_path, config_.time_code,
                              &connected_value) &&
        ValueToShaderParam(connected_value, out)) {
      return true;
    }
  }

  ::tinyusdz::next::EvalOptions direct_opts = eval.GetOptions();
  direct_opts.follow_connections = false;
  ::tinyusdz::next::EvalResult direct =
      eval.EvalWith(shader_prim, attr_name, direct_opts);
  if (direct.success && ValueToShaderParam(direct.value, out)) {
    return true;
  }

  ::tinyusdz::next::EvalResult followed = eval.Eval(shader_prim, attr_name);
  if (followed.success && ValueToShaderParam(followed.value, out)) {
    return true;
  }

  return false;
}

//
// Light conversion
//

bool RenderSceneConverter::ConvertLight(const UsdPrim& prim, RenderLight* out) {
  if (!out || !IsLight(prim)) {
    last_error_ = "Invalid light prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Determine light type
  LightKind kind = GetLightKind(prim);
  switch (kind) {
    case LightKind::DistantLight: out->type = LightType::Directional; break;
    case LightKind::DomeLight: out->type = LightType::Dome; break;
    case LightKind::RectLight: out->type = LightType::Rect; break;
    case LightKind::DiskLight: out->type = LightType::Disk; break;
    case LightKind::SphereLight: out->type = LightType::Sphere; break;
    case LightKind::CylinderLight: out->type = LightType::Cylinder; break;
    default: out->type = LightType::Point; break;
  }

  // Common properties
  GetFloat3(prim, "inputs:color", &out->color.x, &out->color.y, &out->color.z);
  GetFloat(prim, "inputs:intensity", &out->intensity);
  GetFloat(prim, "inputs:exposure", &out->exposure);
  GetBool(prim, "inputs:normalize", &out->normalize);

  // Type-specific properties
  switch (out->type) {
    case LightType::Sphere:
      GetFloat(prim, "inputs:radius", &out->params.sphere.radius);
      break;
    case LightType::Rect:
      GetFloat(prim, "inputs:width", &out->params.rect.width);
      GetFloat(prim, "inputs:height", &out->params.rect.height);
      break;
    case LightType::Disk:
      GetFloat(prim, "inputs:radius", &out->params.disk.radius);
      break;
    default:
      break;
  }

  // Shadow settings
  GetBool(prim, "inputs:enableShadows", &out->enable_shadow);

  return true;
}

//
// Camera conversion
//

bool RenderSceneConverter::ConvertCamera(const UsdPrim& prim, RenderCamera* out) {
  if (!out || !IsCamera(prim)) {
    last_error_ = "Invalid camera prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  // Projection type
  std::string projection;
  GetToken(prim, "projection", &projection);
  out->type = (projection == "orthographic") ?
              CameraType::Orthographic : CameraType::Perspective;

  // Lens parameters
  GetFloat(prim, "focalLength", &out->focal_length);
  GetFloat(prim, "horizontalAperture", &out->horizontal_aperture);
  GetFloat(prim, "verticalAperture", &out->vertical_aperture);

  // Clipping
  float clip_range[2] = {0.1f, 10000.0f};
  const Value* clip_val = GetAttribute(prim, "clippingRange");
  if (clip_val) {
    const float* cr = clip_val->as_float2();
    if (cr) {
      clip_range[0] = cr[0];
      clip_range[1] = cr[1];
    }
  }
  out->near_clip = clip_range[0];
  out->far_clip = clip_range[1];

  return true;
}

//
// Skeleton conversion
//

bool RenderSceneConverter::ConvertSkeleton(const UsdPrim& prim, Skeleton* out) {
  if (!out || !::tinyusdz::next::IsSkeleton(prim)) {
    last_error_ = "Invalid skeleton prim";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  out->root_joint = -1;

  Stage stage;
  (void)stage;

  const Stage* stage_ptr = nullptr;
  // GetSkeletonData currently only needs the stage for API symmetry. Keep a
  // local empty Stage out of the hot path and read directly from the prim.
  (void)stage_ptr;

  ::tinyusdz::next::SkeletonData skel;
  // The schema accessor does not dereference Stage for Skeleton fields.
  if (!::tinyusdz::next::GetSkeletonData(stage, prim, &skel) ||
      skel.joints.empty()) {
    return true;
  }

  std::vector<int> topology;
  std::string err;
  if (!::tinyusdz::next::BuildSkelTopology(skel.joints, topology, &err)) {
    warnings_.push_back("Invalid skeleton topology for " + prim.GetPath().str() +
                        ": " + err);
    topology.assign(skel.joints.size(), -1);
  }

  out->joints.resize(skel.joints.size());
  for (size_t i = 0; i < skel.joints.size(); ++i) {
    SkeletonJoint& joint = out->joints[i];
    joint.path = skel.joints[i];
    if (i < skel.jointNames.size() && !skel.jointNames[i].empty()) {
      joint.name = skel.jointNames[i];
    } else {
      joint.name = LeafNameFromJointPath(skel.joints[i]);
    }
    joint.parent_id = (i < topology.size()) ? topology[i] : -1;
    CopyMatrixFromDoubles(skel.bindTransforms, i, &joint.bind_transform);
    CopyMatrixFromDoubles(skel.restTransforms, i, &joint.rest_transform);

    if (joint.parent_id < 0 && out->root_joint < 0) {
      out->root_joint = static_cast<int32_t>(i);
    }
  }

  for (size_t i = 0; i < out->joints.size(); ++i) {
    const int32_t parent = out->joints[i].parent_id;
    if (parent >= 0 && static_cast<size_t>(parent) < out->joints.size()) {
      out->joints[parent].children.push_back(static_cast<int32_t>(i));
    }
  }

  if (out->root_joint < 0 && !out->joints.empty()) {
    out->root_joint = 0;
  }

  return true;
}

//
// Animation conversion
//

bool RenderSceneConverter::ConvertAnimation(const UsdPrim& prim, AnimationClip* out) {
  if (!out || !prim.IsValid()) return false;

  out->name = prim.GetName() + "_Anim";
  out->prim_path = prim.GetPath().str();
  out->start_time = std::numeric_limits<double>::max();
  out->end_time = -std::numeric_limits<double>::max();

  for (const std::string& prop_name : prim.GetPropertyNames()) {
    const std::vector<double> times = prim.GetTimeSampleTimes(prop_name);
    if (times.empty()) continue;

    const bool is_xform = IsXformAnimationProperty(prop_name);
    AnimationChannel channel;
    channel.target_path = is_xform ? TargetPathForXformOp(prop_name)
                                   : AnimationChannel::TargetPath::CustomProperty;
    channel.target_prim_path = prim.GetPath().str();
    channel.property_name = prop_name;
    channel.keyframes.reserve(times.size());

    for (double t : times) {
      Value value = prim.GetInterpolatedValue(prop_name, t);
      Float4 v;
      if (!ValueToAnimationFloat4(prop_name, value, &v)) continue;
      channel.keyframes.push_back(Keyframe{t, v});
      out->start_time = std::min(out->start_time, t);
      out->end_time = std::max(out->end_time, t);
    }

    if (!channel.keyframes.empty()) {
      out->channels.push_back(std::move(channel));
    }
  }

  if (out->channels.empty()) {
    out->start_time = 0.0;
    out->end_time = 0.0;
    return false;
  }

  return true;
}

//
// Texture loading
//

bool RenderSceneConverter::LoadTexture(const std::string& asset_path, TextureImage* out) {
  if (!out) return false;

  // Use custom loader if provided
  if (config_.material.custom_texture_loader) {
    return config_.material.custom_texture_loader(asset_path, out);
  }

  // Built-in loader is metadata-only by design. Applications that need decoded
  // pixels should provide `MaterialConfig::custom_texture_loader`.
  out->resolved_path = asset_path;

  return true;
}

//
// Utility functions
//

void ComputeTriangleNormal(const float* p0, const float* p1, const float* p2, float* normal) {
  float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
  float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};

  normal[0] = e1[1]*e2[2] - e1[2]*e2[1];
  normal[1] = e1[2]*e2[0] - e1[0]*e2[2];
  normal[2] = e1[0]*e2[1] - e1[1]*e2[0];

  float len = std::sqrt(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
  if (len > 1e-8f) {
    normal[0] /= len;
    normal[1] /= len;
    normal[2] /= len;
  }
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
