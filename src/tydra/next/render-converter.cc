// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Scene Converter Implementation

#include "render-converter.hh"
#include "materialx.hh"
#include "next/schema/usdPhysics.hh"
#include "next/schema/usd-shade.hh"
#include "next/schema/usd-skel.hh"
#include "next/types/type-info.hh"
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

Matrix4 MatrixFromPointInstancerTransform(
    const ::tinyusdz::next::PointInstancerTransform& src) {
  Matrix4 dst;
  for (size_t i = 0; i < 16; ++i) {
    dst.m[i] = static_cast<float>(src.matrix[i]);
  }
  return dst;
}

Matrix4 MulMatrix4(const Matrix4& a, const Matrix4& b) {
  Matrix4 r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      r.m[i * 4 + j] =
          a.m[i * 4 + 0] * b.m[0 * 4 + j] +
          a.m[i * 4 + 1] * b.m[1 * 4 + j] +
          a.m[i * 4 + 2] * b.m[2 * 4 + j] +
          a.m[i * 4 + 3] * b.m[3 * 4 + j];
    }
  }
  return r;
}

std::vector<uint8_t> BuildInstanceVisibility(
    size_t instance_count,
    const std::vector<int64_t>& ids,
    const std::vector<int64_t>& invisible_ids,
    const std::vector<int64_t>& inactive_ids) {
  std::vector<uint8_t> visible(instance_count, uint8_t{1});
  if (instance_count == 0) return visible;

  std::unordered_set<int64_t> hidden;
  hidden.reserve(invisible_ids.size() + inactive_ids.size());
  hidden.insert(invisible_ids.begin(), invisible_ids.end());
  hidden.insert(inactive_ids.begin(), inactive_ids.end());
  if (hidden.empty()) return visible;

  if (ids.size() == instance_count) {
    for (size_t i = 0; i < ids.size(); ++i) {
      if (hidden.find(ids[i]) != hidden.end()) {
        visible[i] = 0;
      }
    }
    return visible;
  }

  for (int64_t id : hidden) {
    if (id >= 0 && static_cast<size_t>(id) < instance_count) {
      visible[static_cast<size_t>(id)] = 0;
    }
  }
  return visible;
}

template <typename Chunked>
void CopyChunkedArray(const Chunked& src, Chunked* dst) {
  if (!dst) return;
  dst->reserve(src.size());
  for (size_t i = 0; i < src.size(); ++i) {
    dst->push_back(src[i]);
  }
}

Float3 TransformPoint(const Matrix4& m, float x, float y, float z) {
  return Float3(
      x * m.m[0] + y * m.m[4] + z * m.m[8] + m.m[12],
      x * m.m[1] + y * m.m[5] + z * m.m[9] + m.m[13],
      x * m.m[2] + y * m.m[6] + z * m.m[10] + m.m[14]);
}

Float3 TransformDirection(const Matrix4& m, float x, float y, float z) {
  Float3 d(
      x * m.m[0] + y * m.m[4] + z * m.m[8],
      x * m.m[1] + y * m.m[5] + z * m.m[9],
      x * m.m[2] + y * m.m[6] + z * m.m[10]);
  const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
  if (len > 1.0e-8f) {
    d.x /= len;
    d.y /= len;
    d.z /= len;
  }
  return d;
}

void CopyVertexAttribute(const VertexAttribute& src, VertexAttribute* dst) {
  if (!dst) return;
  dst->name = src.name;
  dst->format = src.format;
  dst->interpolation = src.interpolation;
  CopyChunkedArray(src.float_data, &dst->float_data);
  CopyChunkedArray(src.int_data, &dst->int_data);
  CopyChunkedArray(src.uint_data, &dst->uint_data);
  CopyChunkedArray(src.indices, &dst->indices);
}

void CopyRenderMeshCommon(const RenderMesh& src, RenderMesh* dst) {
  if (!dst) return;
  CopyChunkedArray(src.face_vertex_counts, &dst->face_vertex_counts);
  CopyChunkedArray(src.face_vertex_indices, &dst->face_vertex_indices);
  CopyChunkedArray(src.texcoords_0, &dst->texcoords_0);
  CopyChunkedArray(src.texcoords_1, &dst->texcoords_1);
  CopyChunkedArray(src.colors, &dst->colors);
  CopyChunkedArray(src.triangulated_indices, &dst->triangulated_indices);
  dst->normals_interp = src.normals_interp;
  dst->texcoords_0_interp = src.texcoords_0_interp;
  dst->texcoords_1_interp = src.texcoords_1_interp;
  dst->colors_interp = src.colors_interp;
  dst->material_id = src.material_id;
  dst->material_subsets = src.material_subsets;
  dst->is_triangulated = src.is_triangulated;

  dst->primvars.reserve(src.primvars.size());
  for (const VertexAttribute& pv : src.primvars) {
    VertexAttribute copy;
    CopyVertexAttribute(pv, &copy);
    dst->primvars.push_back(std::move(copy));
  }

  if (src.skin) {
    dst->skin = std::make_unique<RenderMesh::SkinBinding>();
    CopyChunkedArray(src.skin->joint_indices, &dst->skin->joint_indices);
    CopyChunkedArray(src.skin->joint_weights, &dst->skin->joint_weights);
    dst->skin->skeleton_id = src.skin->skeleton_id;
    dst->skin->geom_bind_transform = src.skin->geom_bind_transform;
  }

  dst->blend_shapes.reserve(src.blend_shapes.size());
  for (const RenderMesh::BlendShape& bs : src.blend_shapes) {
    RenderMesh::BlendShape copy;
    copy.name = bs.name;
    CopyChunkedArray(bs.point_offsets, &copy.point_offsets);
    CopyChunkedArray(bs.normal_offsets, &copy.normal_offsets);
    copy.weight = bs.weight;
    dst->blend_shapes.push_back(std::move(copy));
  }
}

bool CloneMeshForPointInstance(const RenderMesh& src,
                               const RenderPointInstanceDraw& draw,
                               RenderMesh* dst) {
  if (!dst) return false;
  dst->name = src.name + "_pointInstance_" + std::to_string(draw.instance_index);
  dst->prim_path = src.prim_path + ".pointInstance[" +
                   std::to_string(draw.instance_index) + "]";
  CopyRenderMeshCommon(src, dst);

  dst->points.reserve(src.points.size());
  for (size_t i = 0; i + 2 < src.points.size(); i += 3) {
    const Float3 p = TransformPoint(draw.transform, src.points[i],
                                    src.points[i + 1], src.points[i + 2]);
    dst->points.push_back(p.x);
    dst->points.push_back(p.y);
    dst->points.push_back(p.z);
  }

  dst->normals.reserve(src.normals.size());
  for (size_t i = 0; i + 2 < src.normals.size(); i += 3) {
    const Float3 n = TransformDirection(draw.transform, src.normals[i],
                                        src.normals[i + 1], src.normals[i + 2]);
    dst->normals.push_back(n.x);
    dst->normals.push_back(n.y);
    dst->normals.push_back(n.z);
  }

  dst->tangents.reserve(src.tangents.size());
  for (size_t i = 0; i + 3 < src.tangents.size(); i += 4) {
    const Float3 t = TransformDirection(draw.transform, src.tangents[i],
                                        src.tangents[i + 1], src.tangents[i + 2]);
    dst->tangents.push_back(t.x);
    dst->tangents.push_back(t.y);
    dst->tangents.push_back(t.z);
    dst->tangents.push_back(src.tangents[i + 3]);
  }

  if (dst->point_count() > 0) {
    dst->bbox_min = Float3(1e30f, 1e30f, 1e30f);
    dst->bbox_max = Float3(-1e30f, -1e30f, -1e30f);
    for (size_t i = 0; i + 2 < dst->points.size(); i += 3) {
      dst->bbox_min.x = std::min(dst->bbox_min.x, dst->points[i]);
      dst->bbox_min.y = std::min(dst->bbox_min.y, dst->points[i + 1]);
      dst->bbox_min.z = std::min(dst->bbox_min.z, dst->points[i + 2]);
      dst->bbox_max.x = std::max(dst->bbox_max.x, dst->points[i]);
      dst->bbox_max.y = std::max(dst->bbox_max.y, dst->points[i + 1]);
      dst->bbox_max.z = std::max(dst->bbox_max.z, dst->points[i + 2]);
    }
    dst->has_bbox = true;
  }
  return dst->point_count() == src.point_count();
}

void CollectMeshIdsUnderNode(const RenderScene& scene,
                             int32_t node_id,
                             const Matrix4& parent_relative,
                             std::vector<int32_t>* out_ids,
                             std::vector<Matrix4>* out_transforms) {
  if (!out_ids || !out_transforms || node_id < 0 ||
      static_cast<size_t>(node_id) >= scene.nodes.size()) {
    return;
  }

  const SceneNode& node = scene.nodes[static_cast<size_t>(node_id)];
  const Matrix4 relative = MulMatrix4(node.local_transform, parent_relative);
  if (node.type == NodeType::Mesh && node.data_id >= 0) {
    out_ids->push_back(node.data_id);
    out_transforms->push_back(relative);
  }

  for (int32_t child_id : node.children) {
    CollectMeshIdsUnderNode(scene, child_id, relative, out_ids, out_transforms);
  }
}

void ResolvePointInstancerPrototypeBindings(RenderScene* scene,
                                            RenderPointInstancer* instancer) {
  if (!scene || !instancer) return;

  instancer->prototype_node_ids.clear();
  instancer->prototype_mesh_offsets.clear();
  instancer->prototype_mesh_ids.clear();
  instancer->prototype_mesh_transforms.clear();
  instancer->prototype_node_ids.reserve(instancer->prototype_paths.size());
  instancer->prototype_mesh_offsets.reserve(instancer->prototype_paths.size() + 1);
  instancer->prototype_mesh_offsets.push_back(0);

  for (const std::string& path : instancer->prototype_paths) {
    int32_t node_id = -1;
    const auto node_it = scene->node_by_path.find(path);
    if (node_it != scene->node_by_path.end()) {
      node_id = node_it->second;
    }
    instancer->prototype_node_ids.push_back(node_id);
    CollectMeshIdsUnderNode(*scene, node_id, Matrix4::Identity(),
                            &instancer->prototype_mesh_ids,
                            &instancer->prototype_mesh_transforms);
    instancer->prototype_mesh_offsets.push_back(
        static_cast<uint32_t>(instancer->prototype_mesh_ids.size()));
  }
}

void AppendPointInstanceDraws(int32_t instancer_id,
                              RenderPointInstancer* instancer,
                              RenderScene* scene) {
  if (!scene || !instancer || instancer_id < 0) return;
  instancer->draw_start = static_cast<uint32_t>(scene->point_instance_draws.size());
  instancer->draw_count = 0;

  const size_t instance_count = instancer->instance_count();
  for (size_t instance_index = 0; instance_index < instance_count; ++instance_index) {
    if (!instancer->instance_visible.empty() &&
        !instancer->instance_visible[instance_index]) {
      continue;
    }
    const int32_t proto_index = instancer->proto_indices[instance_index];
    if (proto_index < 0 ||
        static_cast<size_t>(proto_index + 1) >=
            instancer->prototype_mesh_offsets.size()) {
      continue;
    }

    const uint32_t begin =
        instancer->prototype_mesh_offsets[static_cast<size_t>(proto_index)];
    const uint32_t end =
        instancer->prototype_mesh_offsets[static_cast<size_t>(proto_index) + 1];
    for (uint32_t mesh_ref = begin; mesh_ref < end; ++mesh_ref) {
      if (mesh_ref >= instancer->prototype_mesh_ids.size()) continue;
      const int32_t mesh_id = instancer->prototype_mesh_ids[mesh_ref];
      if (mesh_id < 0) continue;

      RenderPointInstanceDraw draw;
      draw.point_instancer_id = instancer_id;
      draw.instance_index = static_cast<uint32_t>(instance_index);
      draw.prototype_index = static_cast<uint32_t>(proto_index);
      draw.mesh_id = mesh_id;
      if (static_cast<size_t>(mesh_id) < scene->meshes.size()) {
        draw.material_id = scene->meshes[static_cast<size_t>(mesh_id)].material_id;
      }
      Matrix4 instance_transform = Matrix4::Identity();
      if (instance_index < instancer->transforms.size()) {
        instance_transform = instancer->transforms[instance_index];
      }
      if (mesh_ref < instancer->prototype_mesh_transforms.size()) {
        draw.transform = MulMatrix4(instancer->prototype_mesh_transforms[mesh_ref],
                                    instance_transform);
      } else {
        draw.transform = instance_transform;
      }
      scene->point_instance_draws.push_back(draw);
      ++instancer->draw_count;
    }
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
  // From the inputs:st chain (UsdTransform2d -> UsdPrimvarReader_float2):
  std::string uv_primvar;               // varname of the primvar reader
  float uv_translation[2] = {0.0f, 0.0f};
  float uv_rotation = 0.0f;             // degrees (UsdTransform2d convention)
  float uv_scale[2] = {1.0f, 1.0f};
};

// Trace a UsdUVTexture's inputs:st connection chain: UsdTransform2d nodes
// accumulate the UV transform (chained via their inputs:in); a
// UsdPrimvarReader_* terminates the chain and names the UV set.
static bool GetFloat2Local(const UsdPrim& prim, const std::string& name,
                           float* out2) {
  const ::tinyusdz::next::Value* v = GetAttribute(prim, name);
  if (!v) return false;
  const float* f = v->as_float2();
  if (!f) return false;
  out2[0] = f[0];
  out2[1] = f[1];
  return true;
}

void TraceTextureStChain(const Stage& stage, const UsdPrim& texture_prim,
                         TextureNodeData* out) {
  UsdPrim cur = texture_prim;
  std::string prop = "inputs:st";
  for (int hop = 0; hop < 4 && cur.IsValid(); ++hop) {
    const ::tinyusdz::next::PrimSpec* spec = cur.GetPrimSpec();
    const std::vector<::tinyusdz::next::Path>* conns =
        spec ? spec->connection(prop) : nullptr;
    if (!conns || conns->empty()) return;
    const std::string next_path = SourcePrimPathFromConnection((*conns)[0].str());
    UsdPrim np = stage.GetPrimAtPath(next_path);
    if (!np.IsValid()) return;
    std::string id;
    GetToken(np, "info:id", &id);
    if (id == "UsdTransform2d") {
      float tr[2];
      if (GetFloat2Local(np, "inputs:translation", tr)) {
        out->uv_translation[0] = tr[0];
        out->uv_translation[1] = tr[1];
      }
      float rot = 0.0f;
      if (GetFloat(np, "inputs:rotation", &rot)) out->uv_rotation = rot;
      float sc[2];
      if (GetFloat2Local(np, "inputs:scale", sc)) {
        out->uv_scale[0] = sc[0];
        out->uv_scale[1] = sc[1];
      }
      cur = np;
      prop = "inputs:in";
      continue;
    }
    if (id.rfind("UsdPrimvarReader", 0) == 0) {
      out->uv_primvar = ::tinyusdz::next::GetPrimvarReaderVarname(np);
      return;
    }
    return;
  }
}

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
    TraceTextureStChain(stage, texture_prim, out);
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
  TraceTextureStChain(stage, texture_prim, out);

  return true;
}

bool IsOpenPBRShaderId(const std::string& id) {
  return id == "ND_open_pbr_surface_surfaceshader" ||
         id == "open_pbr_surface" ||
         id == "OpenPBRSurface";
}

// Does this prim's binding declare `bindMaterialAs = "strongerThanDescendants"`?
bool BindingIsStrongerThanDescendants(const UsdPrim& prim) {
  static const char* kBindingOrder[] = {"material:binding:preview",
                                        "material:binding",
                                        "material:binding:full"};
  const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec();
  if (!spec) return false;
  for (const char* rel : kBindingOrder) {
    const std::vector<::tinyusdz::next::Path>* targets =
        prim.GetRelationship(rel);
    if (!targets || targets->empty()) continue;
    if (const ::tinyusdz::next::PropMeta* pm = spec->property_meta(rel)) {
      if ((pm->authored & ::tinyusdz::next::PropMeta::kBindMaterialAs) &&
          pm->bindMaterialAs == "strongerThanDescendants") {
        return true;
      }
    }
    return false;  // binding found; default weakerThanDescendants
  }
  return false;
}

bool IsPhysicsExtensionPropertyName(const std::string& name) {
  return name.rfind("mjc:", 0) == 0 ||
         name.rfind("newton:", 0) == 0 ||
         name.rfind("physx", 0) == 0 ||
         name.rfind("state:", 0) == 0;
}

std::string ValueSummary(const Value& value) {
  if (const bool* b = value.as_bool()) return *b ? "true" : "false";
  if (const int32_t* i = value.as_int()) return std::to_string(*i);
  if (const int64_t* i = value.as_int64()) return std::to_string(*i);
  if (const float* f = value.as_float()) return std::to_string(*f);
  if (const double* d = value.as_double()) return std::to_string(*d);
  if (const std::string* s = value.as_string()) return *s;
  if (const std::string* s = value.as_token()) return *s;
  if (const std::string* s = value.as_asset_path()) return *s;
  if (const float* v = value.as_float3()) {
    return std::to_string(v[0]) + "," + std::to_string(v[1]) + "," +
           std::to_string(v[2]);
  }
  if (const float* v = value.as_float4()) {
    return std::to_string(v[0]) + "," + std::to_string(v[1]) + "," +
           std::to_string(v[2]) + "," + std::to_string(v[3]);
  }
  if (const std::vector<float>* arr = value.as_float_array()) {
    return "float[" + std::to_string(arr->size()) + "]";
  }
  if (const std::vector<double>* arr = value.as_double_array()) {
    return "double[" + std::to_string(arr->size()) + "]";
  }
  if (const std::vector<int32_t>* arr = value.as_int_array()) {
    return "int[" + std::to_string(arr->size()) + "]";
  }
  if (const std::vector<int64_t>* arr = value.as_int64_array()) {
    return "int64[" + std::to_string(arr->size()) + "]";
  }
  if (const std::vector<std::string>* arr = value.as_token_array()) {
    return "token[" + std::to_string(arr->size()) + "]";
  }
  const char* type_name = ::tinyusdz::next::GetTypeName(value.type_id());
  return type_name ? type_name : "value";
}

std::vector<PhysicsProperty> CollectPhysicsExtensionProperties(
    const UsdPrim& prim) {
  std::vector<PhysicsProperty> props;
  for (const std::string& name : prim.GetPropertyNames()) {
    if (!IsPhysicsExtensionPropertyName(name)) continue;
    if (const Value* value = prim.GetPropertyValue(name)) {
      PhysicsProperty prop;
      prop.name = name;
      prop.value = ValueSummary(*value);
      props.push_back(std::move(prop));
    }
  }
  for (const std::string& name : prim.GetRelationshipNames()) {
    if (!IsPhysicsExtensionPropertyName(name)) continue;
    PhysicsProperty prop;
    prop.name = name;
    const std::vector<::tinyusdz::next::Path>* targets =
        prim.GetRelationship(name);
    if (targets) {
      prop.value = "rel[" + std::to_string(targets->size()) + "]";
    }
    props.push_back(std::move(prop));
  }
  return props;
}

Float3 Float3FromArray(const float v[3]) {
  return Float3(v[0], v[1], v[2]);
}

Float4 Float4FromArray(const float v[4]) {
  return Float4(v[0], v[1], v[2], v[3]);
}

std::string FindInheritedMaterialBinding(const Stage& stage,
                                         const std::string& prim_path) {
  // Walk leaf-up (descendant wins by default), but an ANCESTOR binding marked
  // bindMaterialAs="strongerThanDescendants" overrides everything below it —
  // so track the highest such ancestor.
  std::string leaf_binding;
  std::string strongest_ancestor;
  std::string path = prim_path;
  while (!path.empty() && path != "/") {
    UsdPrim prim = stage.GetPrimAtPath(path);
    if (prim.IsValid()) {
      const std::string material_path = ::tinyusdz::next::GetBoundMaterialPath(prim);
      if (!material_path.empty()) {
        if (leaf_binding.empty()) leaf_binding = material_path;
        if (path != prim_path && BindingIsStrongerThanDescendants(prim)) {
          strongest_ancestor = material_path;  // higher ancestors overwrite
        }
      }
    }
    path = GetParentPath(path);
  }
  return strongest_ancestor.empty() ? leaf_binding : strongest_ancestor;
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
    ExtractPhysicsAnnotations(stage, &result.scene);

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
      if (ConvertMesh(stage, mesh_prim, &mesh)) {
        int32_t mesh_id = static_cast<int32_t>(result.scene.meshes.size());
        result.scene.mesh_by_path[mesh.prim_path] = mesh_id;
        result.scene.meshes.push_back(std::move(mesh));
        AssignNodeDataId(&result.scene, mesh_prim.GetPath().str(), mesh_id);
      } else {
        warnings_.push_back("Failed to convert mesh: " + mesh_prim.GetPath().str());
      }
    }

    for (const auto& rec : extracted.point_instancers) {
      RenderPointInstancer instancer;
      if (ConvertPointInstancer(rec.prim, &instancer)) {
        int32_t instancer_id =
            static_cast<int32_t>(result.scene.point_instancers.size());
        result.scene.point_instancer_by_path[instancer.prim_path] = instancer_id;
        if (!instancer.valid) {
          warnings_.push_back("Invalid PointInstancer data at " +
                              instancer.prim_path + ": " +
                              instancer.validation_error);
        }
        ResolvePointInstancerPrototypeBindings(&result.scene, &instancer);
        for (size_t proto_i = 0; proto_i < instancer.prototype_paths.size();
             ++proto_i) {
          if (proto_i >= instancer.prototype_node_ids.size() ||
              instancer.prototype_node_ids[proto_i] < 0) {
            warnings_.push_back("Unresolved PointInstancer prototype at " +
                                instancer.prim_path + ": " +
                                instancer.prototype_paths[proto_i]);
          } else if (instancer.prototype_mesh_count(proto_i) == 0) {
            warnings_.push_back("PointInstancer prototype has no meshes at " +
                                instancer.prim_path + ": " +
                                instancer.prototype_paths[proto_i]);
          }
        }
        AppendPointInstanceDraws(instancer_id, &instancer, &result.scene);
        result.scene.point_instancers.push_back(std::move(instancer));
        AssignNodeDataId(&result.scene, rec.path, instancer_id);
      } else {
        warnings_.push_back("Failed to convert PointInstancer: " + rec.path);
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

    AssignMaterialBindings(stage, &result.scene);
    AssignPointInstanceDrawMaterials(&result.scene);
    if (config_.point_instancer.duplicate_meshes) {
      DuplicatePointInstanceMeshes(&result.scene);
    }

    // Convert lights
    for (const auto& rec : extracted.lights) {
      RenderLight light;
      if (ConvertLight(rec.prim, &light)) {
        for (int i = 0; i < 16; ++i) {
          light.transform.m[i] = static_cast<float>(rec.world[i]);
        }
        // DomeLight environment texture -> image, id stored in params.dome.
        if (light.type == LightType::Dome) {
          light.params.dome.texture_id = -1;
          std::string tex;
          const Value* fv = GetAttribute(rec.prim, "inputs:texture:file");
          if (fv) {
            if (const std::string* ap = fv->as_asset_path()) tex = *ap;
            else if (const std::string* s = fv->as_string()) tex = *s;
            else if (const std::string* t = fv->as_token()) tex = *t;
          }
          if (!tex.empty()) {
            light.params.dome.texture_id =
                ResolveImageId(&result.scene, tex, ColorSpace::Linear);
          }
        }
        int32_t light_id = static_cast<int32_t>(result.scene.lights.size());
        result.scene.lights.push_back(std::move(light));
        AssignNodeDataId(&result.scene, rec.path, light_id);
      }
    }

    // Convert cameras
    for (const auto& rec : extracted.cameras) {
      RenderCamera camera;
      if (ConvertCamera(rec.prim, &camera)) {
        for (int i = 0; i < 16; ++i) {
          camera.transform.m[i] = static_cast<float>(rec.world[i]);
        }
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

    // Resolve mesh skin bindings to skeleton ids (skeletons converted above).
    for (RenderMesh& mesh : result.scene.meshes) {
      if (!mesh.skin || mesh.skin->skeleton_path.empty()) continue;
      for (size_t si = 0; si < result.scene.skeletons.size(); ++si) {
        if (result.scene.skeletons[si].prim_path == mesh.skin->skeleton_path) {
          mesh.skin->skeleton_id = static_cast<int32_t>(si);
          break;
        }
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

void RenderSceneConverter::ExtractPhysicsAnnotations(const Stage& stage,
                                                     RenderScene* scene) {
  if (!scene) return;

  stage.Traverse([&](const UsdPrim& prim) {
    const std::string path = prim.GetPath().str();

    if (::tinyusdz::next::IsPhysicsScene(prim)) {
      ::tinyusdz::next::PhysicsSceneData data;
      if (::tinyusdz::next::GetPhysicsSceneData(stage, prim, &data,
                                                config_.time_code)) {
        PhysicsSceneAnnotation out;
        out.prim_path = path;
        out.gravity_direction = Float3FromArray(data.gravityDirection);
        out.gravity_magnitude = data.gravityMagnitude;
        out.extension_properties = CollectPhysicsExtensionProperties(prim);
        scene->physics.scenes.push_back(std::move(out));
      }
    }

    if (::tinyusdz::next::HasPhysicsRigidBodyAPI(prim) ||
        ::tinyusdz::next::HasPhysicsMassAPI(prim)) {
      PhysicsRigidBodyAnnotation out;
      out.prim_path = path;
      if (::tinyusdz::next::HasPhysicsRigidBodyAPI(prim)) {
        ::tinyusdz::next::PhysicsRigidBodyData data;
        if (::tinyusdz::next::GetPhysicsRigidBodyData(stage, prim, &data,
                                                      config_.time_code)) {
          out.rigid_body_enabled = data.rigidBodyEnabled;
          out.kinematic_enabled = data.kinematicEnabled;
          out.simulation_owner = data.simulationOwner;
          out.velocity = Float3FromArray(data.velocity);
          out.angular_velocity = Float3FromArray(data.angularVelocity);
          out.starts_asleep = data.startsAsleep;
        }
      }
      if (::tinyusdz::next::HasPhysicsMassAPI(prim)) {
        ::tinyusdz::next::PhysicsMassData data;
        if (::tinyusdz::next::GetPhysicsMassData(stage, prim, &data)) {
          out.has_mass = true;
          out.mass = data.mass;
          out.density = data.density;
          out.center_of_mass = Float3FromArray(data.centerOfMass);
          out.diagonal_inertia = Float3FromArray(data.diagonalInertia);
          out.principal_axes = Float4FromArray(data.principalAxes);
        }
      }
      out.extension_properties = CollectPhysicsExtensionProperties(prim);
      scene->physics.rigid_bodies.push_back(std::move(out));
    }

    if (::tinyusdz::next::HasPhysicsCollisionAPI(prim) ||
        ::tinyusdz::next::HasPhysicsMeshCollisionAPI(prim)) {
      PhysicsColliderAnnotation out;
      out.prim_path = path;
      if (::tinyusdz::next::HasPhysicsCollisionAPI(prim)) {
        ::tinyusdz::next::PhysicsCollisionData data;
        if (::tinyusdz::next::GetPhysicsCollisionData(stage, prim, &data)) {
          out.collision_enabled = data.collisionEnabled;
          out.simulation_owner = data.simulationOwner;
        }
      }
      if (::tinyusdz::next::HasPhysicsMeshCollisionAPI(prim)) {
        ::tinyusdz::next::PhysicsMeshCollisionData data;
        if (::tinyusdz::next::GetPhysicsMeshCollisionData(prim, &data)) {
          out.has_mesh_collision = true;
          out.approximation = data.approximation;
        }
      }
      out.extension_properties = CollectPhysicsExtensionProperties(prim);
      scene->physics.colliders.push_back(std::move(out));
    }

    if (::tinyusdz::next::IsPhysicsJoint(prim)) {
      PhysicsJointAnnotation out;
      out.prim_path = path;
      out.type_name = prim.GetTypeName();

      ::tinyusdz::next::PhysicsJointData base;
      if (::tinyusdz::next::GetPhysicsJointData(stage, prim, &base,
                                                config_.time_code)) {
        out.body0 = base.body0;
        out.body1 = base.body1;
        out.has_body0 = base.hasBody0;
        out.has_body1 = base.hasBody1;
        out.local_pos0 = Float3FromArray(base.localPos0);
        out.local_pos1 = Float3FromArray(base.localPos1);
        out.local_rot0 = Float4FromArray(base.localQuat0);
        out.local_rot1 = Float4FromArray(base.localQuat1);
        out.collision_enabled = base.collisionEnabled;
      }

      if (::tinyusdz::next::IsPhysicsRevoluteJoint(prim)) {
        ::tinyusdz::next::PhysicsRevoluteJointData data;
        if (::tinyusdz::next::GetPhysicsRevoluteJointData(
                stage, prim, &data, config_.time_code)) {
          out.axis = Float3FromArray(data.axis);
          out.lower_limit = data.lowerLimit;
          out.upper_limit = data.upperLimit;
        }
      } else if (::tinyusdz::next::IsPhysicsPrismaticJoint(prim)) {
        ::tinyusdz::next::PhysicsPrismaticJointData data;
        if (::tinyusdz::next::GetPhysicsPrismaticJointData(
                stage, prim, &data, config_.time_code)) {
          out.axis = Float3FromArray(data.axis);
          out.lower_limit = data.lowerLimit;
          out.upper_limit = data.upperLimit;
        }
      } else if (::tinyusdz::next::IsPhysicsSliderJoint(prim)) {
        ::tinyusdz::next::PhysicsSliderJointData data;
        if (::tinyusdz::next::GetPhysicsSliderJointData(
                stage, prim, &data, config_.time_code)) {
          out.axis = Float3FromArray(data.axis);
          out.lower_limit = data.lowerLimit;
          out.upper_limit = data.upperLimit;
        }
      } else if (::tinyusdz::next::IsPhysicsSphericalJoint(prim)) {
        ::tinyusdz::next::PhysicsSphericalJointData data;
        if (::tinyusdz::next::GetPhysicsSphericalJointData(
                stage, prim, &data, config_.time_code)) {
          out.cone_angle0_limit = data.coneAngle0Limit;
          out.cone_angle1_limit = data.coneAngle1Limit;
        }
      } else if (::tinyusdz::next::IsPhysicsBallJoint(prim)) {
        ::tinyusdz::next::PhysicsBallJointData data;
        if (::tinyusdz::next::GetPhysicsBallJointData(
                stage, prim, &data, config_.time_code)) {
          out.cone_angle0_limit = data.coneAngle0Limit;
          out.cone_angle1_limit = data.coneAngle1Limit;
        }
      } else if (::tinyusdz::next::IsPhysicsDistanceJoint(prim)) {
        ::tinyusdz::next::PhysicsDistanceJointData data;
        if (::tinyusdz::next::GetPhysicsDistanceJointData(
                stage, prim, &data, config_.time_code)) {
          out.min_distance = data.minDistance;
          out.max_distance = data.maxDistance;
        }
      }

      out.extension_properties = CollectPhysicsExtensionProperties(prim);
      scene->physics.joints.push_back(std::move(out));
    }

    if (::tinyusdz::next::HasPhysicsMaterialAPI(prim)) {
      ::tinyusdz::next::PhysicsMaterialData data;
      if (::tinyusdz::next::GetPhysicsMaterialData(stage, prim, &data)) {
        PhysicsMaterialAnnotation out;
        out.prim_path = path;
        out.static_friction = data.staticFriction;
        out.dynamic_friction = data.dynamicFriction;
        out.restitution = data.restitution;
        out.density = data.density;
        out.extension_properties = CollectPhysicsExtensionProperties(prim);
        scene->physics.materials.push_back(std::move(out));
      }
    }

    if (::tinyusdz::next::HasPhysicsFilteredPairsAPI(prim)) {
      ::tinyusdz::next::PhysicsFilteredPairsData data;
      if (::tinyusdz::next::GetPhysicsFilteredPairsData(prim, &data)) {
        PhysicsFilteredPairsAnnotation out;
        out.prim_path = path;
        out.filtered_pair_paths = std::move(data.filteredPairPaths);
        scene->physics.filtered_pairs.push_back(std::move(out));
      }
    }

    if (::tinyusdz::next::HasPhysicsArticulationRootAPI(prim)) {
      scene->physics.articulation_roots.push_back(path);
    }

    return true;
  });
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
    else if (type == "PointInstancer") node.type = NodeType::PointInstancer;
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

void RenderSceneConverter::AssignMaterialBindings(const Stage& stage,
                                                  RenderScene* scene) {
  if (!scene) return;
  for (RenderMesh& mesh : scene->meshes) {
    const std::string material_path =
        FindInheritedMaterialBinding(stage, mesh.prim_path);
    if (!material_path.empty()) {
      const auto it = scene->material_by_path.find(material_path);
      if (it != scene->material_by_path.end()) mesh.material_id = it->second;
    }

    // GeomSubset material bindings (familyName == materialBind): USD subsets
    // are arbitrary face-index sets; the range-based MaterialSubset model
    // stores one entry per CONSECUTIVE run of face indices.
    UsdPrim mesh_prim = stage.GetPrimAtPath(mesh.prim_path);
    if (!mesh_prim.IsValid()) continue;
    for (const GeomSubset& sub : GetGeomSubsets(mesh_prim)) {
      if (!sub.family_name.empty() && sub.family_name != "materialBind") {
        continue;
      }
      std::string sub_mat = sub.material_path;
      if (sub_mat.empty()) {
        UsdPrim sub_prim = stage.GetPrimAtPath(sub.path);
        if (sub_prim.IsValid()) {
          sub_mat = ::tinyusdz::next::GetBoundMaterialPath(sub_prim);
        }
      }
      if (sub_mat.empty()) continue;
      const auto mit = scene->material_by_path.find(sub_mat);
      if (mit == scene->material_by_path.end()) continue;
      const uint32_t nfaces = static_cast<uint32_t>(mesh.face_count());
      // Sort + split into consecutive runs, dropping out-of-range faces.
      std::vector<uint32_t> faces;
      faces.reserve(sub.indices.size());
      for (int32_t fi : sub.indices) {
        if (fi >= 0 && static_cast<uint32_t>(fi) < nfaces) {
          faces.push_back(static_cast<uint32_t>(fi));
        }
      }
      std::sort(faces.begin(), faces.end());
      faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
      size_t run_start = 0;
      for (size_t i = 1; i <= faces.size(); ++i) {
        if (i == faces.size() || faces[i] != faces[i - 1] + 1) {
          RenderMesh::MaterialSubset ms;
          ms.face_start = faces[run_start];
          ms.face_count = static_cast<uint32_t>(i - run_start);
          ms.material_id = mit->second;
          mesh.material_subsets.push_back(ms);
          run_start = i;
        }
      }
    }
  }
}

void RenderSceneConverter::AssignPointInstanceDrawMaterials(RenderScene* scene) {
  if (!scene) return;
  for (RenderPointInstanceDraw& draw : scene->point_instance_draws) {
    if (draw.mesh_id < 0 ||
        static_cast<size_t>(draw.mesh_id) >= scene->meshes.size()) {
      draw.material_id = -1;
      continue;
    }
    draw.material_id = scene->meshes[static_cast<size_t>(draw.mesh_id)].material_id;
  }
}

void RenderSceneConverter::DuplicatePointInstanceMeshes(RenderScene* scene) {
  if (!scene) return;
  const size_t draw_count = scene->point_instance_draws.size();
  for (size_t draw_id = 0; draw_id < draw_count; ++draw_id) {
    RenderPointInstanceDraw& draw = scene->point_instance_draws[draw_id];
    if (draw.expanded_mesh_id >= 0) continue;
    const RenderMesh* src = scene->get_mesh(draw.mesh_id);
    if (!src) continue;

    RenderMesh expanded;
    if (!CloneMeshForPointInstance(*src, draw, &expanded)) {
      continue;
    }
    const int32_t mesh_id = static_cast<int32_t>(scene->meshes.size());
    scene->mesh_by_path[expanded.prim_path] = mesh_id;
    scene->meshes.push_back(std::move(expanded));
    draw.expanded_mesh_id = mesh_id;
  }
}

//
// Mesh conversion
//

bool RenderSceneConverter::ConvertMesh(const Stage& stage, const UsdPrim& prim, RenderMesh* out) {
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

  // Sanitize topology BEFORE any consumer walks it: negative / out-of-range
  // faceVertexIndices previously flowed into normal generation and the output
  // buffers (a negative index casts to ~4 billion -> segfault; an OOB index
  // hands the renderer an out-of-bounds read). Drop offending faces with a
  // warning; also truncate a counts list that overruns the index buffer.
  SanitizeMeshTopology(out);

  // Extract primvars (UVs, colors, etc.)
  ExtractMeshPrimvars(prim, out);

  // Skinning binding (skel:skeleton + skel:jointIndices/Weights primvars).
  {
    SkinBindingInfo sb;
    if (GetSkinBinding(prim, &sb) && !sb.joint_indices.empty() &&
        sb.joint_indices.size() == sb.joint_weights.size()) {
      out->skin = std::make_unique<RenderMesh::SkinBinding>();
      out->skin->joint_indices.reserve(sb.joint_indices.size());
      for (int32_t ji : sb.joint_indices) {
        out->skin->joint_indices.push_back(
            ji < 0 ? uint16_t(0) : static_cast<uint16_t>(ji));
      }
      out->skin->joint_weights.append(sb.joint_weights.data(),
                                      sb.joint_weights.size());
      std::memcpy(out->skin->geom_bind_transform.m, sb.geom_bind_transform,
                  sizeof(sb.geom_bind_transform));
      // skeleton_id is resolved by the caller once skeletons are converted
      // (stored in skin->skeleton_id via the path recorded here).
      out->skin->skeleton_path = sb.skeleton_path;
    }
  }

  // Blend shapes (skel:blendShapes names + skel:blendShapeTargets prims).
  for (const BlendShapeInfo& bs : GetBlendShapes(prim)) {
    UsdPrim bs_prim = stage.GetPrimAtPath(bs.path);
    if (!bs_prim.IsValid()) continue;
    ::tinyusdz::next::BlendShapeData bd;
    if (!::tinyusdz::next::GetBlendShapeData(stage, bs_prim, &bd)) continue;
    if (bd.offsets.empty()) continue;
    RenderMesh::BlendShape shape;
    shape.name = bs.name.empty() ? bs_prim.GetName() : bs.name;
    shape.point_offsets.append(bd.offsets.data(), bd.offsets.size());
    if (bd.hasNormalOffsets && !bd.normalOffsets.empty()) {
      shape.normal_offsets.append(bd.normalOffsets.data(),
                                  bd.normalOffsets.size());
    }
    if (bd.hasPointIndices) {
      const size_t npts = out->point_count();
      for (int32_t pi : bd.pointIndices) {
        // Drop out-of-range point indices (a consumer would index OOB).
        if (pi >= 0 && static_cast<size_t>(pi) < npts) {
          shape.point_indices.push_back(static_cast<uint32_t>(pi));
        }
      }
    }
    out->blend_shapes.push_back(std::move(shape));
  }

  // Triangulate if requested
  if (config_.mesh.triangulate && !out->is_triangulated) {
    TriangulateMesh(out);
  }

  // Compute normals if needed
  if (config_.mesh.compute_normals && out->normals.empty()) {
    ComputeVertexNormals(out);
  }

  // Compute tangents if requested (needs triangles, per-vertex normals and
  // per-vertex UVs).
  if (config_.mesh.compute_tangents && out->tangents.empty()) {
    ComputeVertexTangents(out);
  }

  return true;
}

// Per-vertex tangent frame (Lengyel's method) from triangulated topology,
// per-vertex normals and per-vertex UVs. Output is xyzw per vertex (w = sign
// so bitangent = cross(normal, tangent) * w). No-op unless all inputs are
// per-vertex and consistent.
bool RenderSceneConverter::ComputeVertexTangents(RenderMesh* mesh) {
  if (!mesh->is_triangulated) {
    if (!TriangulateMesh(mesh)) return false;
  }
  const size_t np = mesh->point_count();
  if (np == 0) return false;
  // Require per-vertex normals + per-vertex 2-component UVs of matching size.
  if (mesh->normals_interp != Interpolation::Vertex ||
      mesh->normals.size() != np * 3) {
    return false;
  }
  if (mesh->texcoords_0_interp != Interpolation::Vertex ||
      mesh->texcoords_0.size() != np * 2) {
    return false;
  }

  std::vector<float> tan(np * 3, 0.0f);
  std::vector<float> bit(np * 3, 0.0f);
  const size_t ntris = mesh->triangulated_indices.size() / 3;
  for (size_t t = 0; t < ntris; ++t) {
    const uint32_t i0 = mesh->triangulated_indices[t * 3 + 0];
    const uint32_t i1 = mesh->triangulated_indices[t * 3 + 1];
    const uint32_t i2 = mesh->triangulated_indices[t * 3 + 2];
    if (i0 >= np || i1 >= np || i2 >= np) continue;
    const float* p0 = &mesh->points[i0 * 3];
    const float* p1 = &mesh->points[i1 * 3];
    const float* p2 = &mesh->points[i2 * 3];
    const float* u0 = &mesh->texcoords_0[i0 * 2];
    const float* u1 = &mesh->texcoords_0[i1 * 2];
    const float* u2 = &mesh->texcoords_0[i2 * 2];
    const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    const float du1 = u1[0] - u0[0], dv1 = u1[1] - u0[1];
    const float du2 = u2[0] - u0[0], dv2 = u2[1] - u0[1];
    const float det = du1 * dv2 - du2 * dv1;
    const float r = (std::fabs(det) > 1e-12f) ? 1.0f / det : 0.0f;
    const float sdir[3] = {(dv2 * e1[0] - dv1 * e2[0]) * r,
                           (dv2 * e1[1] - dv1 * e2[1]) * r,
                           (dv2 * e1[2] - dv1 * e2[2]) * r};
    const float tdir[3] = {(du1 * e2[0] - du2 * e1[0]) * r,
                           (du1 * e2[1] - du2 * e1[1]) * r,
                           (du1 * e2[2] - du2 * e1[2]) * r};
    for (uint32_t vi : {i0, i1, i2}) {
      tan[vi * 3 + 0] += sdir[0];
      tan[vi * 3 + 1] += sdir[1];
      tan[vi * 3 + 2] += sdir[2];
      bit[vi * 3 + 0] += tdir[0];
      bit[vi * 3 + 1] += tdir[1];
      bit[vi * 3 + 2] += tdir[2];
    }
  }

  std::vector<float> out_tan(np * 4, 0.0f);
  for (size_t v = 0; v < np; ++v) {
    const float* n = &mesh->normals[v * 3];
    const float* tv = &tan[v * 3];
    // Gram-Schmidt orthogonalize t against n.
    const float ndt = n[0] * tv[0] + n[1] * tv[1] + n[2] * tv[2];
    float tx = tv[0] - n[0] * ndt;
    float ty = tv[1] - n[1] * ndt;
    float tz = tv[2] - n[2] * ndt;
    const float len = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (len > 1e-12f) {
      tx /= len; ty /= len; tz /= len;
    } else {
      tx = 1.0f; ty = 0.0f; tz = 0.0f;
    }
    // Handedness: sign of dot(cross(n, t), bitangent).
    const float* bv = &bit[v * 3];
    const float cx = n[1] * tz - n[2] * ty;
    const float cy = n[2] * tx - n[0] * tz;
    const float cz = n[0] * ty - n[1] * tx;
    const float w = (cx * bv[0] + cy * bv[1] + cz * bv[2]) < 0.0f ? -1.0f : 1.0f;
    out_tan[v * 4 + 0] = tx;
    out_tan[v * 4 + 1] = ty;
    out_tan[v * 4 + 2] = tz;
    out_tan[v * 4 + 3] = w;
  }
  mesh->tangents.clear();
  mesh->tangents.append(out_tan.data(), out_tan.size());
  return true;
}

void RenderSceneConverter::SanitizeMeshTopology(RenderMesh* mesh) {
  const uint32_t point_count = static_cast<uint32_t>(mesh->point_count());
  const size_t index_count = mesh->face_vertex_indices.size();

  // Fast path: everything consistent.
  bool ok = true;
  size_t need = 0;
  for (uint32_t c : mesh->face_vertex_counts) {
    need += c;
    if (need > index_count) { ok = false; break; }
  }
  if (ok && need <= index_count) {
    for (uint32_t idx : mesh->face_vertex_indices) {
      if (idx >= point_count) { ok = false; break; }
    }
    if (ok && need == index_count) return;
  }

  std::vector<uint32_t> counts;
  std::vector<uint32_t> indices;
  counts.reserve(mesh->face_vertex_counts.size());
  indices.reserve(index_count);
  size_t offset = 0;
  size_t dropped = 0;
  for (uint32_t c : mesh->face_vertex_counts) {
    if (offset + c > index_count) {
      // counts overrun the index buffer: drop this and all later faces.
      dropped += 1;
      break;
    }
    bool face_ok = true;
    for (uint32_t i = 0; i < c; ++i) {
      // face_vertex_indices is uint32; a negative authored index arrived as a
      // huge value and fails this check too.
      if (mesh->face_vertex_indices[offset + i] >= point_count) {
        face_ok = false;
        break;
      }
    }
    if (face_ok) {
      counts.push_back(c);
      for (uint32_t i = 0; i < c; ++i) {
        indices.push_back(mesh->face_vertex_indices[offset + i]);
      }
    } else {
      ++dropped;
    }
    offset += c;
  }
  if (dropped > 0 || indices.size() != index_count ||
      counts.size() != mesh->face_vertex_counts.size()) {
    warnings_.push_back("Mesh '" + mesh->prim_path +
                        "': dropped invalid faces (out-of-range or negative "
                        "faceVertexIndices, or counts overrunning the index "
                        "buffer)");
    mesh->face_vertex_counts.clear();
    mesh->face_vertex_counts.append(counts.data(), counts.size());
    mesh->face_vertex_indices.clear();
    mesh->face_vertex_indices.append(indices.data(), indices.size());
  }
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

  std::string orientation;
  if (GetToken(prim, "orientation", &orientation)) {
    mesh->left_handed = (orientation == "leftHanded");
  }

  // holeIndices: face indices excluded from rendering.
  {
    ValueArrayRead<int32_t> holes;
    if (ReadIntArray(prim, "holeIndices", config_.time_code, &holes)) {
      for (int32_t h : holes) {
        if (h >= 0) mesh->hole_faces.push_back(static_cast<uint32_t>(h));
      }
      std::sort(mesh->hole_faces.begin(), mesh->hole_faces.end());
    }
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

  // Authored normals are handled in ExtractMeshPrimvars (after topology
  // sanitization, where interpolation metadata and element-count validation
  // live).

  return true;
}

namespace {

Interpolation ParsePrimvarInterp(const std::string& s) {
  if (s == "constant") return Interpolation::Constant;
  if (s == "uniform") return Interpolation::Uniform;
  if (s == "faceVarying") return Interpolation::FaceVarying;
  if (s == "varying") return Interpolation::Varying;
  return Interpolation::Vertex;
}

// Flatten a primvar Value into floats (float/half/double backed, any comps).
// Returns comps per element (0 = unsupported/absent).
uint32_t PrimvarToFloats(const Value& v, std::vector<float>* out) {
  if (!v.is_array()) return 0;
  const uint32_t comps =
      static_cast<uint32_t>(GetComponentCount(v.type_id()));
  if (comps == 0) return 0;
  if (const std::vector<float>* fa = v.as_float_array()) {
    out->assign(fa->begin(), fa->end());
    return comps;
  }
  if (const std::vector<double>* da = v.as_double_array()) {
    out->reserve(da->size());
    for (double d : *da) out->push_back(static_cast<float>(d));
    return comps;
  }
  return 0;
}

}  // namespace

bool RenderSceneConverter::ExtractMeshPrimvars(const UsdPrim& prim, RenderMesh* mesh) {
  const std::string uv_base = config_.mesh.default_uv_primvar;
  const size_t npoints = mesh->point_count();
  const size_t nfaces = mesh->face_count();
  const size_t ncorners = mesh->face_vertex_indices.size();

  auto expected_elems = [&](Interpolation it) -> size_t {
    switch (it) {
      case Interpolation::Constant: return 1;
      case Interpolation::Uniform: return nfaces;
      case Interpolation::FaceVarying: return ncorners;
      case Interpolation::Vertex:
      case Interpolation::Varying:
      default: return npoints;
    }
  };

  // Expand an indexed primvar to direct form; false on any out-of-range index.
  auto expand_indexed = [](const std::vector<float>& data, uint32_t comps,
                           const std::vector<int32_t>& idxs,
                           std::vector<float>* out) -> bool {
    const size_t elems = comps ? data.size() / comps : 0;
    out->clear();
    out->reserve(idxs.size() * comps);
    for (int32_t raw : idxs) {
      if (raw < 0 || static_cast<size_t>(raw) >= elems) return false;
      const float* src = data.data() + static_cast<size_t>(raw) * comps;
      out->insert(out->end(), src, src + comps);
    }
    return true;
  };

  // Authored `normals` attribute (primvars:normals, handled in the loop
  // below, takes precedence per USD).
  {
    ValueArrayRead<float> normals;
    if (ReadFloatArray(prim, "normals", config_.time_code, &normals) &&
        !normals.empty()) {
      std::string interp_tok = "vertex";
      if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
        if (const ::tinyusdz::next::PropMeta* pm =
                spec->property_meta("normals")) {
          if (pm->authored & ::tinyusdz::next::PropMeta::kInterpolation) {
            interp_tok = pm->interpolation;
          }
        }
      }
      const Interpolation ni = ParsePrimvarInterp(interp_tok);
      const size_t elems = normals.view.size / 3;
      if (elems == expected_elems(ni)) {
        mesh->normals.append(normals.view.data, normals.view.size);
        mesh->normals_interp = ni;
      } else {
        warnings_.push_back("Mesh '" + mesh->prim_path +
                            "': authored normals element count does not match "
                            "their interpolation; ignoring (normals will be "
                            "computed)");
      }
    }
  }

  for (Primvar& pv : GetPrimvars(prim)) {
    if (!pv.value) continue;
    std::vector<float> data;
    const uint32_t comps = PrimvarToFloats(*pv.value, &data);
    const Interpolation interp = ParsePrimvarInterp(pv.interpolation);
    const bool is_uv0 = (pv.name == uv_base);
    const bool is_uv1 = (pv.name == uv_base + "1");
    const bool is_color = (pv.name == "displayColor");
    const bool is_normals = (pv.name == "normals");
    const bool builtin = is_uv0 || is_uv1 || is_color || is_normals;

    // Skinning primvars are consumed by the skin binding (GetSkinBinding),
    // not by the generic vertex-attribute channel.
    if (pv.name.rfind("skel:", 0) == 0) continue;

    if (comps == 0) {
      // Non-float primvar: only representable as a generic int attribute.
      if (builtin) continue;
      const std::vector<int32_t>* ia = pv.value->as_int_array();
      if (!ia || ia->empty()) continue;
      VertexAttribute attr;
      attr.name = pv.name;
      attr.format = VertexFormat::Int;
      attr.interpolation = interp;
      attr.int_data.append(ia->data(), ia->size());
      bool idx_ok = true;
      for (int32_t raw : pv.indices) {
        if (raw < 0 || static_cast<size_t>(raw) >= ia->size()) {
          idx_ok = false;
          break;
        }
        attr.indices.push_back(static_cast<uint32_t>(raw));
      }
      if (!idx_ok) {
        warnings_.push_back("Mesh '" + mesh->prim_path + "': primvar '" +
                            pv.name + "' has out-of-range indices; dropped");
        continue;
      }
      mesh->primvars.push_back(std::move(attr));
      continue;
    }

    // Indexed builtin primvars are expanded to direct form (the builtin
    // buffers carry no index channel).
    if (!pv.indices.empty() && builtin) {
      std::vector<float> expanded;
      if (!expand_indexed(data, comps, pv.indices, &expanded)) {
        warnings_.push_back("Mesh '" + mesh->prim_path + "': primvar '" +
                            pv.name + "' has out-of-range indices; dropped");
        continue;
      }
      data = std::move(expanded);
    }

    if (builtin) {
      // Size must match the declared interpolation or a consumer indexes OOB.
      const size_t elems = data.size() / comps;
      if (elems != expected_elems(interp)) {
        warnings_.push_back(
            "Mesh '" + mesh->prim_path + "': primvar '" + pv.name +
            "' element count does not match its interpolation; dropped");
        continue;
      }
      if (is_uv0 && comps == 2) {
        mesh->texcoords_0.append(data.data(), data.size());
        mesh->texcoords_0_interp = interp;
      } else if (is_uv1 && comps == 2) {
        mesh->texcoords_1.append(data.data(), data.size());
        mesh->texcoords_1_interp = interp;
      } else if (is_color && (comps == 3 || comps == 4)) {
        mesh->colors.append(data.data(), data.size());
        mesh->colors_interp = interp;
      } else if (is_normals && comps == 3) {
        // primvars:normals takes precedence over the raw `normals` attribute.
        mesh->normals.clear();
        mesh->normals.append(data.data(), data.size());
        mesh->normals_interp = interp;
      }
      continue;
    }

    // Generic primvar: keep indices as an index channel (validated).
    VertexAttribute attr;
    attr.name = pv.name;
    attr.format = comps == 1   ? VertexFormat::Float
                  : comps == 2 ? VertexFormat::Vec2
                  : comps == 3 ? VertexFormat::Vec3
                               : VertexFormat::Vec4;
    if (comps > 4) continue;  // matrices etc.: not a vertex attribute
    attr.interpolation = interp;
    attr.float_data.append(data.data(), data.size());
    bool idx_ok = true;
    const size_t elems = data.size() / comps;
    for (int32_t raw : pv.indices) {
      if (raw < 0 || static_cast<size_t>(raw) >= elems) {
        idx_ok = false;
        break;
      }
      attr.indices.push_back(static_cast<uint32_t>(raw));
    }
    if (!idx_ok) {
      warnings_.push_back("Mesh '" + mesh->prim_path + "': primvar '" +
                          pv.name + "' has out-of-range indices; dropped");
      continue;
    }
    mesh->primvars.push_back(std::move(attr));
  }

  return true;
}

bool RenderSceneConverter::ConvertPointInstancer(const UsdPrim& prim,
                                                 RenderPointInstancer* out) {
  if (!out || !::tinyusdz::next::IsPointInstancer(prim)) {
    last_error_ = "Invalid PointInstancer prim";
    return false;
  }

  PointInstancerData data;
  if (!ReadPointInstancerData(prim, config_.time_code, &data)) {
    last_error_ = data.validation_error.empty()
                      ? "Failed to read PointInstancer data"
                      : data.validation_error;
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  out->prototype_paths.reserve(data.prototypes.size());
  for (const ::tinyusdz::next::Path& path : data.prototypes) {
    out->prototype_paths.push_back(path.str());
  }
  out->proto_indices = std::move(data.proto_indices);
  out->positions = std::move(data.positions);
  out->orientations = std::move(data.orientations);
  out->scales = std::move(data.scales);
  out->velocities = std::move(data.velocities);
  out->angular_velocities = std::move(data.angular_velocities);
  out->ids = std::move(data.ids);
  out->invisible_ids = std::move(data.invisible_ids);
  out->inactive_ids = std::move(data.inactive_ids);
  out->transforms.reserve(data.transforms.size());
  for (const ::tinyusdz::next::PointInstancerTransform& transform :
       data.transforms) {
    out->transforms.push_back(MatrixFromPointInstancerTransform(transform));
  }
  out->instance_visible = BuildInstanceVisibility(
      out->instance_count(), out->ids, out->invisible_ids, out->inactive_ids);
  out->valid = data.valid;
  out->validation_error = std::move(data.validation_error);
  return true;
}

//
// Triangulation
//

bool RenderSceneConverter::TriangulateMesh(RenderMesh* mesh) {
  if (mesh->face_vertex_counts.empty()) return false;
  mesh->triangulated_indices.clear();  // re-entry / failure-path hardening
  mesh->triangulated_face_vertex_indices.clear();

  // Check if already triangulated
  bool all_triangles = true;
  for (size_t i = 0; i < mesh->face_vertex_counts.size(); ++i) {
    if (mesh->face_vertex_counts[i] != 3) {
      all_triangles = false;
      break;
    }
  }

  if (all_triangles && !mesh->left_handed && mesh->hole_faces.empty()) {
    // Just copy indices; corner remap is identity.
    const size_t n = mesh->face_vertex_indices.size();
    mesh->triangulated_indices.resize(n);
    mesh->triangulated_face_vertex_indices.resize(n);
    for (size_t i = 0; i < n; ++i) {
      mesh->triangulated_indices[i] = mesh->face_vertex_indices[i];
      mesh->triangulated_face_vertex_indices[i] = static_cast<uint32_t>(i);
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
    const bool is_hole = std::binary_search(mesh->hole_faces.begin(),
                                            mesh->hole_faces.end(),
                                            static_cast<uint32_t>(f));
    if (nverts >= 3 && !is_hole) {
      const uint32_t v0 = mesh->face_vertex_indices[idx_offset];
      const uint32_t c0 = static_cast<uint32_t>(idx_offset);
      for (uint32_t i = 1; i < nverts - 1; ++i) {
        const uint32_t ca = static_cast<uint32_t>(idx_offset + i);
        const uint32_t cb = static_cast<uint32_t>(idx_offset + i + 1);
        // leftHanded meshes emit reversed winding so the triangulated output
        // is uniformly CCW/rightHanded. The corner map records the original
        // face-vertex index for each emitted corner.
        if (mesh->left_handed) {
          mesh->triangulated_indices.push_back(v0);
          mesh->triangulated_indices.push_back(mesh->face_vertex_indices[cb]);
          mesh->triangulated_indices.push_back(mesh->face_vertex_indices[ca]);
          mesh->triangulated_face_vertex_indices.push_back(c0);
          mesh->triangulated_face_vertex_indices.push_back(cb);
          mesh->triangulated_face_vertex_indices.push_back(ca);
        } else {
          mesh->triangulated_indices.push_back(v0);
          mesh->triangulated_indices.push_back(mesh->face_vertex_indices[ca]);
          mesh->triangulated_indices.push_back(mesh->face_vertex_indices[cb]);
          mesh->triangulated_face_vertex_indices.push_back(c0);
          mesh->triangulated_face_vertex_indices.push_back(ca);
          mesh->triangulated_face_vertex_indices.push_back(cb);
        }
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

std::string RenderSceneConverter::ResolveAssetPath(
    const std::string& file) const {
  if (config_.asset_base_dir.empty() || file.empty() || file[0] == '/' ||
      file.find("://") != std::string::npos) {
    return file;
  }
  std::string rel = file;
  if (rel.rfind("./", 0) == 0) rel = rel.substr(2);
  return config_.asset_base_dir + "/" + rel;
}

int32_t RenderSceneConverter::ResolveImageId(RenderScene* scene,
                                             const std::string& file,
                                             ColorSpace color_space) {
  if (!scene || file.empty()) return -1;
  const std::string resolved = ResolveAssetPath(file);
  const ColorSpace csp =
      color_space == ColorSpace::Unknown ? ColorSpace::sRGB : color_space;
  for (size_t i = 0; i < scene->images.size(); ++i) {
    if (scene->images[i].resolved_path == resolved &&
        scene->images[i].color_space == csp) {
      return static_cast<int32_t>(i);
    }
  }
  TextureImage image;
  image.name = file;
  image.resolved_path = resolved;
  image.color_space = csp;
  const int32_t id = static_cast<int32_t>(scene->images.size());
  scene->images.push_back(std::move(image));
  return id;
}

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

  // Find shader(s) in material. The material's `outputs:surface` connection
  // names the authoritative surface shader (child iteration order previously
  // decided ties, and shaders living OUTSIDE the material prim never resolved).
  bool found_shader = false;

  std::vector<UsdPrim> candidates;
  {
    const std::string surf =
        ::tinyusdz::next::GetSurfaceShader(stage, prim);
    if (!surf.empty()) {
      UsdPrim sp = stage.GetPrimAtPath(surf);
      if (sp.IsValid()) candidates.push_back(sp);
    }
  }
  for (const auto& child : prim.GetChildren()) {
    candidates.push_back(child);
  }

  for (const auto& child : candidates) {
    if (found_shader) break;
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

  if (!found_shader) {
    // MaterialX surface shaders (e.g. ND_standard_surface_surfaceshader):
    // convert through the MaterialX -> PreviewSurface mapping.
    MtlxConverter mtlx;
    RenderMaterial mtlx_out;
    if (mtlx.ConvertUsdMtlxMaterial(stage, prim, &mtlx_out)) {
      if (mtlx_out.preview_surface) {
        out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
        out->preview_surface = std::move(mtlx_out.preview_surface);
        out->alpha_mode = mtlx_out.alpha_mode;
        out->alpha_cutoff = mtlx_out.alpha_cutoff;
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
    std::string connection_path = eval.GetConnectionPath(shader_prim, attr_name);
    // Follow pass-through hops (NodeGraph outputs forwarding to an inner
    // shader): a texture behind `Material/Graph.outputs:out` was previously
    // lost because only the FIRST hop was inspected.
    for (int hop = 0; hop < 8; ++hop) {
      const std::string hop_prim_path =
          SourcePrimPathFromConnection(connection_path);
      UsdPrim hop_prim = stage.GetPrimAtPath(hop_prim_path);
      if (!hop_prim.IsValid()) break;
      std::string hop_id;
      GetToken(hop_prim, "info:id", &hop_id);
      if (hop_id == "UsdUVTexture") break;  // reached a texture node
      std::string pp, prop;
      if (!SplitConnectionPath(connection_path, &pp, &prop)) break;
      const ::tinyusdz::next::PrimSpec* spec = hop_prim.GetPrimSpec();
      const std::vector<::tinyusdz::next::Path>* nc =
          spec ? spec->connection(prop) : nullptr;
      if (!nc || nc->empty()) break;
      connection_path = (*nc)[0].str();
    }
    const std::string texture_prim_path = SourcePrimPathFromConnection(connection_path);
    UsdPrim texture_prim = stage.GetPrimAtPath(texture_prim_path);

    TextureNodeData tex_data;
    if (scene && ExtractTextureNodeData(stage, texture_prim, config_.time_code, &tex_data)) {
      const ColorSpace cs = ParseColorSpace(tex_data.source_color_space);
      const ColorSpace image_color_space =
          cs == ColorSpace::Unknown ? ColorSpace::sRGB : cs;
      const std::string resolved = ResolveAssetPath(tex_data.file);
      int32_t image_id = -1;
      for (size_t i = 0; i < scene->images.size(); ++i) {
        if (scene->images[i].resolved_path == resolved &&
            scene->images[i].color_space == image_color_space) {
          image_id = static_cast<int32_t>(i);
          break;
        }
      }
      if (image_id < 0) {
        TextureImage image;
        image.name = texture_prim.IsValid() ? texture_prim.GetName() : tex_data.file;
        image.resolved_path = resolved;
        image.color_space = image_color_space;
        if (config_.material.load_textures) {
          TextureImage loaded;
          if (LoadTexture(resolved, &loaded)) {
            if (loaded.name.empty()) loaded.name = image.name;
            if (loaded.resolved_path.empty()) loaded.resolved_path = resolved;
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
      // UsdTransform2d on the st chain (rotation is authored in degrees;
      // RenderTexture stores radians).
      texture.offset = Float2(tex_data.uv_translation[0],
                              tex_data.uv_translation[1]);
      texture.scale = Float2(tex_data.uv_scale[0], tex_data.uv_scale[1]);
      texture.rotation = tex_data.uv_rotation * 3.14159265358979323846f / 180.0f;
      texture.uv_primvar = tex_data.uv_primvar;

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
    case LightType::Sphere: {
      GetFloat(prim, "inputs:radius", &out->params.sphere.radius);
      // Cone shaping on a sphere light makes it a spot light.
      float cone_angle = 0.0f;
      if (GetFloat(prim, "inputs:shaping:cone:angle", &cone_angle)) {
        out->type = LightType::Spot;
        out->params.spot.angle = cone_angle * 3.14159265358979323846f / 180.0f;
      }
      break;
    }
    case LightType::Rect:
      GetFloat(prim, "inputs:width", &out->params.rect.width);
      GetFloat(prim, "inputs:height", &out->params.rect.height);
      break;
    case LightType::Disk:
      GetFloat(prim, "inputs:radius", &out->params.disk.radius);
      break;
    case LightType::Cylinder:
      GetFloat(prim, "inputs:radius", &out->params.cylinder.radius);
      GetFloat(prim, "inputs:length", &out->params.cylinder.length);
      break;
    case LightType::Directional:
      GetFloat(prim, "inputs:angle", &out->params.distant.angle);
      break;
    default:
      break;
  }

  // Shadow settings (UsdLux authors `inputs:shadow:enable`; accept the
  // legacy `inputs:enableShadows` spelling too).
  if (!GetBool(prim, "inputs:shadow:enable", &out->enable_shadow)) {
    GetBool(prim, "inputs:enableShadows", &out->enable_shadow);
  }

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
