// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Render Scene Converter Implementation

#include "render-converter.hh"
#include "next/resolver/asset-resolver.hh"
#include "materialx.hh"
#include "next/schema/usdPhysics.hh"
#include "next/schema/usd-shade.hh"
#include "next/schema/usd-skel.hh"
#include "next/types/type-info.hh"
#include "tydra/fast-mikktspace.hh"
#include "tydra/mikktspace-tangent.hh"
#include "tydra/shape-to-mesh.hh"
#include "external/mapbox/earcut/earcut.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <iomanip>
#include <unordered_set>

namespace tinyusdz {
namespace tydra {
namespace next {

using ::tinyusdz::next::Stage;
using ::tinyusdz::next::UsdPrim;
using ::tinyusdz::next::Value;

namespace {

constexpr float kAlphaEpsilon = 1.0e-6f;
// 2GB is the typical hard limit for legacy WebAssembly linear memory growth in
// non-shared-memory builds. Keep a conservative per-mesh budget for temporary
// triangulation artifacts to avoid allocator abort on pathological data.
constexpr size_t kMaxTriangulationCornerCount = 150'000'000u;
constexpr uint32_t kEarcutMaxVertices = 16384;
constexpr size_t kMaxTempAllocBytes = 256u * 1024u * 1024u;

bool WouldOverflowSizeMul(size_t a, size_t b) {
  if (a == 0 || b == 0) return false;
  return a > (std::numeric_limits<size_t>::max() / b);
}

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
  if (token == "repeat") return WrapMode::Repeat;
  if (token == "clamp") return WrapMode::Clamp;
  if (token == "mirror") return WrapMode::Mirror;
  if (token == "black") return WrapMode::Black;
  // UsdUVTexture's wrapS/wrapT fallback is "useMetadata"; with no texture
  // metadata the effective mode is clamp-to-edge (legacy tydra behavior) —
  // NOT repeat, which visibly tiles textures authored to clamp.
  return WrapMode::Clamp;
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
  // Half-precision shader inputs (half/half2/half3/half4 + role types) store
  // raw half-bit lanes; widen through the converting reads.
  {
    float h[4];
    if (value.to_float(h)) {
      SetParamFloat(out, h[0]);
      return true;
    }
    if (value.to_float2(h)) {
      SetParamFloat4(out, h[0], h[1], 0.0f, 1.0f);
      return true;
    }
    if (value.to_float3(h)) {
      SetParamFloat3(out, h[0], h[1], h[2]);
      return true;
    }
    if (value.to_float4(h)) {
      SetParamFloat4(out, h[0], h[1], h[2], h[3]);
      return true;
    }
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
  // Authored half-precision scalars (half3 rotate/scale, quath orient, ...)
  // store raw half-bit lanes; the converting reads widen them.
  float h[4];
  if (value.to_float3(h)) {
    *out = Float4(h[0], h[1], h[2], 0.0f);
    return true;
  }
  if (value.to_float4(h)) {
    *out = Float4(h[0], h[1], h[2], h[3]);
    return true;
  }
  if (value.to_float(h)) {
    *out = Float4(h[0], 0.0f, 0.0f, 0.0f);
    return true;
  }
  return false;
}

// Closed-form Euler-degrees -> quaternion (xyzw) for all six USD rotation
// orders (rotateXYZ means apply X first: Q = Qz * Qy * Qx). Ported from the
// legacy tydra converter so Rotation channels always carry quaternions.
Float4 EulerDegreesToQuatXYZW(float xdeg, float ydeg, float zdeg,
                              const std::string& order) {
  const double kHalfDegToRad = 3.14159265358979323846 / 360.0;
  const float sx = static_cast<float>(std::sin(double(xdeg) * kHalfDegToRad));
  const float cx = static_cast<float>(std::cos(double(xdeg) * kHalfDegToRad));
  const float sy = static_cast<float>(std::sin(double(ydeg) * kHalfDegToRad));
  const float cy = static_cast<float>(std::cos(double(ydeg) * kHalfDegToRad));
  const float sz = static_cast<float>(std::sin(double(zdeg) * kHalfDegToRad));
  const float cz = static_cast<float>(std::cos(double(zdeg) * kHalfDegToRad));

  if (order == "XZY") {  // Q = Qy * Qz * Qx
    return Float4(cy*cz*sx + sy*sz*cx, cy*sz*sx + sy*cz*cx,
                  cy*sz*cx - sy*cz*sx, cy*cz*cx - sy*sz*sx);
  }
  if (order == "YXZ") {  // Q = Qz * Qx * Qy
    return Float4(cz*sx*cy - sz*cx*sy, cz*cx*sy + sz*sx*cy,
                  cz*sx*sy + sz*cx*cy, cz*cx*cy - sz*sx*sy);
  }
  if (order == "YZX") {  // Q = Qx * Qz * Qy
    return Float4(sx*cz*cy - cx*sz*sy, cx*cz*sy - sx*sz*cy,
                  cx*sz*cy + sx*cz*sy, cx*cz*cy + sx*sz*sy);
  }
  if (order == "ZXY") {  // Q = Qy * Qx * Qz
    return Float4(cy*sx*cz + sy*cx*sz, sy*cx*cz - cy*sx*sz,
                  cy*cx*sz - sy*sx*cz, cy*cx*cz + sy*sx*sz);
  }
  if (order == "ZYX") {  // Q = Qx * Qy * Qz
    return Float4(cx*sy*sz + sx*cy*cz, cx*sy*cz - sx*cy*sz,
                  cx*cy*sz + sx*sy*cz, cx*cy*cz - sx*sy*sz);
  }
  // XYZ (and fallback): Q = Qz * Qy * Qx
  return Float4(cz*cy*sx - sz*sy*cx, cz*sy*cx + sz*cy*sx,
                sz*cy*cx - cz*sy*sx, cz*cy*cx + sz*sy*sx);
}

// Extracts the axis order ("XYZ", "ZYX", ...) from an xformOp:rotate<ORDER>
// property name. Returns false for single-axis rotateX/Y/Z and non-rotate ops.
bool EulerRotationOrderFromPropName(const std::string& prop_name,
                                    std::string* out_order) {
  const size_t pos = prop_name.find("rotate");
  if (pos == std::string::npos) return false;
  const std::string tail = prop_name.substr(pos + 6, 3);
  if (tail == "XYZ" || tail == "XZY" || tail == "YXZ" || tail == "YZX" ||
      tail == "ZXY" || tail == "ZYX") {
    *out_order = tail;
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
    // Single-axis rotations become quaternions: Rotation channels are
    // consumed as xyzw quats by the render layer, never as raw degrees.
    if (prop_name.find("rotateX") != std::string::npos) {
      *out = EulerDegreesToQuatXYZW(scalar, 0.0f, 0.0f, "XYZ");
    } else if (prop_name.find("rotateY") != std::string::npos) {
      *out = EulerDegreesToQuatXYZW(0.0f, scalar, 0.0f, "XYZ");
    } else if (prop_name.find("rotateZ") != std::string::npos) {
      *out = EulerDegreesToQuatXYZW(0.0f, 0.0f, scalar, "XYZ");
    } else if (prop_name.find("scale") != std::string::npos) {
      *out = Float4(scalar, scalar, scalar, 0.0f);
    } else {
      *out = Float4(scalar, 0.0f, 0.0f, 0.0f);
    }
    return true;
  }

  if (!ValueToFloat4(value, out)) return false;

  // next-core Values keep quats real-first (w, x, y, z); render animation
  // channels use xyzw (three.js quaternion order). xformOp:orient is the
  // quat-valued xform op.
  const ::tinyusdz::next::TypeId tid = value.type_id();
  if (tid == ::tinyusdz::next::TypeId::Quatf ||
      tid == ::tinyusdz::next::TypeId::Quatd ||
      tid == ::tinyusdz::next::TypeId::Quath) {
    *out = Float4(out->y, out->z, out->w, out->x);
    return true;
  }

  // Three-axis Euler rotate ops (float3 degrees) also convert to quats.
  std::string rot_order;
  if (EulerRotationOrderFromPropName(prop_name, &rot_order)) {
    *out = EulerDegreesToQuatXYZW(out->x, out->y, out->z, rot_order);
  }
  return true;
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

// General 4x4 inverse (Gauss-Jordan, double precision). Returns false for a
// singular matrix. Used to derive rest transforms from bind transforms.
bool InvertMatrix4x4D(const double m[16], double out[16]) {
  double a[4][8];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      a[r][c] = m[r * 4 + c];
      a[r][c + 4] = (r == c) ? 1.0 : 0.0;
    }
  }
  for (int col = 0; col < 4; ++col) {
    int pivot = col;
    for (int r = col + 1; r < 4; ++r) {
      if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
    }
    if (std::fabs(a[pivot][col]) < 1e-12) return false;
    if (pivot != col) {
      for (int c = 0; c < 8; ++c) std::swap(a[col][c], a[pivot][c]);
    }
    const double inv_p = 1.0 / a[col][col];
    for (int c = 0; c < 8; ++c) a[col][c] *= inv_p;
    for (int r = 0; r < 4; ++r) {
      if (r == col) continue;
      const double f = a[r][col];
      if (f == 0.0) continue;
      for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
    }
  }
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r][c + 4];
  }
  return true;
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
  CopyChunkedArray(src.triangulated_face_vertex_indices,
                   &dst->triangulated_face_vertex_indices);
  dst->normals_interp = src.normals_interp;
  dst->tangents_interp = src.tangents_interp;
  dst->texcoords_0_interp = src.texcoords_0_interp;
  dst->texcoords_1_interp = src.texcoords_1_interp;
  dst->colors_interp = src.colors_interp;
  dst->material_id = src.material_id;
  dst->material_subsets = src.material_subsets;
  dst->is_triangulated = src.is_triangulated;
  dst->hole_faces = src.hole_faces;
  dst->left_handed = src.left_handed;
  dst->bbox_min = src.bbox_min;
  dst->bbox_max = src.bbox_max;
  dst->has_bbox = src.has_bbox;

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
    dst->skin->influences_per_vertex = src.skin->influences_per_vertex;
    dst->skin->skeleton_id = src.skin->skeleton_id;
    dst->skin->skeleton_path = src.skin->skeleton_path;
    dst->skin->geom_bind_transform = src.skin->geom_bind_transform;
  }

  dst->blend_shapes.reserve(src.blend_shapes.size());
  for (const RenderMesh::BlendShape& bs : src.blend_shapes) {
    RenderMesh::BlendShape copy;
    copy.name = bs.name;
    CopyChunkedArray(bs.point_offsets, &copy.point_offsets);
    CopyChunkedArray(bs.normal_offsets, &copy.normal_offsets);
    copy.point_indices = bs.point_indices;
    copy.weight = bs.weight;
    copy.inbetweens.reserve(bs.inbetweens.size());
    for (const RenderMesh::BlendShape::Inbetween& source : bs.inbetweens) {
      RenderMesh::BlendShape::Inbetween inbetween;
      inbetween.name = source.name;
      inbetween.weight = source.weight;
      CopyChunkedArray(source.point_offsets, &inbetween.point_offsets);
      copy.inbetweens.push_back(std::move(inbetween));
    }
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
  if (!instancer->valid) return;

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

struct NextClipSetMetadata {
  std::string name;
  std::vector<std::string> asset_paths;
  std::vector<std::pair<double, double>> times;
  std::vector<std::pair<double, int>> active;
  std::string prim_path;
  std::string manifest_asset_path;
  bool interpolate_missing = false;
};

bool ClipValueToDouble(const Value* value, double* out) {
  if (!value || !out) return false;
  if (const double* v = value->as_double()) {
    *out = *v;
    return true;
  }
  if (const float* v = value->as_float()) {
    *out = *v;
    return true;
  }
  if (const int32_t* v = value->as_int()) {
    *out = *v;
    return true;
  }
  if (const int64_t* v = value->as_int64()) {
    *out = static_cast<double>(*v);
    return true;
  }
  return false;
}

bool ClipValueToString(const Value* value, std::string* out) {
  if (!value || !out) return false;
  if (const std::string* v = value->as_string()) {
    *out = *v;
    return true;
  }
  if (const std::string* v = value->as_token()) {
    *out = *v;
    return true;
  }
  if (const std::string* v = value->as_asset_path()) {
    *out = *v;
    return true;
  }
  return false;
}

std::vector<std::pair<double, double>> ClipPairArray(const Value* value) {
  std::vector<std::pair<double, double>> out;
  if (!value || !value->is_array()) return out;
  if (const std::vector<double>* flat = value->as_double_array()) {
    for (size_t i = 0; i + 1 < flat->size(); i += 2) {
      out.emplace_back((*flat)[i], (*flat)[i + 1]);
    }
  } else if (const std::vector<float>* flat = value->as_float_array()) {
    for (size_t i = 0; i + 1 < flat->size(); i += 2) {
      out.emplace_back((*flat)[i], (*flat)[i + 1]);
    }
  }
  return out;
}

bool ExpandNextTemplateClips(const ::tinyusdz::next::Dict& dict,
                             NextClipSetMetadata* out,
                             std::string* error) {
  std::string pattern;
  if (!ClipValueToString(dict.find("templateAssetPath"), &pattern)) {
    return false;
  }
  double start = 0.0;
  double end = 0.0;
  double stride = 1.0;
  double active_offset = 0.0;
  ClipValueToDouble(dict.find("templateStartTime"), &start);
  ClipValueToDouble(dict.find("templateEndTime"), &end);
  ClipValueToDouble(dict.find("templateStride"), &stride);
  ClipValueToDouble(dict.find("templateActiveOffset"), &active_offset);
  if (stride <= 0.0 || end < start) {
    if (error) *error = "Invalid value-clip template time range";
    return false;
  }

  const size_t first_hash = pattern.find('#');
  if (first_hash == std::string::npos) {
    if (error) *error = "Value-clip templateAssetPath has no # placeholder";
    return false;
  }
  size_t first_end = first_hash;
  while (first_end < pattern.size() && pattern[first_end] == '#') ++first_end;
  const size_t first_width = first_end - first_hash;
  size_t second_start = std::string::npos;
  size_t second_end = std::string::npos;
  if (first_end + 1 < pattern.size() && pattern[first_end] == '.' &&
      pattern[first_end + 1] == '#') {
    second_start = first_end + 1;
    second_end = second_start;
    while (second_end < pattern.size() && pattern[second_end] == '#') {
      ++second_end;
    }
  }
  const std::string prefix = pattern.substr(0, first_hash);
  const std::string suffix = second_start == std::string::npos
                                 ? pattern.substr(first_end)
                                 : pattern.substr(second_end);

  int asset_index = 0;
  for (double time = start; time <= end + stride * 0.5; time += stride) {
    const double t = std::min(time, end);
    double integer_part = 0.0;
    const double fractional_part = std::modf(t, &integer_part);
    std::ostringstream path;
    path << prefix << std::setfill('0') << std::setw(static_cast<int>(first_width))
         << static_cast<int>(integer_part);
    if (second_start != std::string::npos) {
      const size_t width = second_end - second_start;
      const double scale = std::pow(10.0, static_cast<double>(width));
      path << '.' << std::setw(static_cast<int>(width))
           << static_cast<int>(std::round(std::fabs(fractional_part) * scale));
    }
    path << suffix;
    out->asset_paths.push_back(path.str());
    out->times.emplace_back(t, t);
    out->active.emplace_back(t + active_offset, asset_index++);
    if (t >= end) break;
  }
  return !out->asset_paths.empty();
}

bool ParseNextClipSets(const UsdPrim& prim,
                       std::vector<NextClipSetMetadata>* out,
                       std::string* error) {
  if (!out || !prim.GetPrimSpec()) return false;
  const ::tinyusdz::next::Dict* clips =
      prim.GetPrimSpec()->meta().clips().as_dictionary();
  if (!clips) return false;

  for (const auto& entry : clips->entries) {
    const ::tinyusdz::next::Dict* dict = entry.second.as_dictionary();
    if (!dict) continue;
    NextClipSetMetadata meta;
    meta.name = entry.first;
    if (dict->find("templateAssetPath")) {
      if (!ExpandNextTemplateClips(*dict, &meta, error)) continue;
    } else if (const Value* assets = dict->find("assetPaths")) {
      if (const std::vector<std::string>* paths = assets->as_token_array()) {
        meta.asset_paths = *paths;
      }
      meta.times = ClipPairArray(dict->find("times"));
      const std::vector<std::pair<double, double>> active_pairs =
          ClipPairArray(dict->find("active"));
      for (const auto& pair : active_pairs) {
        meta.active.emplace_back(pair.first, static_cast<int>(pair.second));
      }
    }
    ClipValueToString(dict->find("primPath"), &meta.prim_path);
    ClipValueToString(dict->find("manifestAssetPath"),
                      &meta.manifest_asset_path);
    if (const bool* interpolate =
            dict->find("interpolateMissingClipValues")
                ? dict->find("interpolateMissingClipValues")->as_bool()
                : nullptr) {
      meta.interpolate_missing = *interpolate;
    }
    if (meta.active.empty() && !meta.asset_paths.empty()) {
      meta.active.emplace_back(0.0, 0);
    }
    if (!meta.asset_paths.empty()) {
      std::sort(meta.times.begin(), meta.times.end());
      std::sort(meta.active.begin(), meta.active.end());
      out->push_back(std::move(meta));
    }
  }
  return !out->empty();
}

int ActiveClipIndex(const NextClipSetMetadata& meta, double stage_time) {
  if (meta.active.empty()) return meta.asset_paths.empty() ? -1 : 0;
  int index = meta.active.front().second;
  for (const auto& entry : meta.active) {
    if (entry.first > stage_time) break;
    index = entry.second;
  }
  return index;
}

double ValueClipTime(const NextClipSetMetadata& meta, double stage_time) {
  if (meta.times.empty()) return stage_time;
  if (stage_time <= meta.times.front().first) return meta.times.front().second;
  if (stage_time >= meta.times.back().first) return meta.times.back().second;
  for (size_t i = 0; i + 1 < meta.times.size(); ++i) {
    const auto& a = meta.times[i];
    const auto& b = meta.times[i + 1];
    if (stage_time < a.first || stage_time > b.first) continue;
    const double span = b.first - a.first;
    const double alpha = span > 0.0 ? (stage_time - a.first) / span : 0.0;
    return a.second + (b.second - a.second) * alpha;
  }
  return stage_time;
}

std::vector<double> ValueClipSampleTimes(const Stage& stage,
                                         const NextClipSetMetadata& meta,
                                         uint32_t max_samples) {
  std::set<double> exact;
  for (const auto& value : meta.times) exact.insert(value.first);
  for (const auto& value : meta.active) exact.insert(value.first);
  const ::tinyusdz::next::StageMeta stage_meta = stage.GetMeta();
  double start = exact.empty() ? 0.0 : *exact.begin();
  double end = exact.empty() ? start : *exact.rbegin();
  if (stage_meta.startTimeCode_set) start = stage_meta.startTimeCode;
  if (stage_meta.endTimeCode_set) end = stage_meta.endTimeCode;
  if (end < start) std::swap(start, end);
  exact.insert(start);
  exact.insert(end);

  const uint32_t limit = std::max<uint32_t>(2, max_samples);
  const double span = end - start;
  double step = 1.0;
  if (span > static_cast<double>(limit - 1)) {
    step = span / static_cast<double>(limit - 1);
  }
  for (double t = start; t <= end + step * 0.25; t += step) {
    exact.insert(std::min(t, end));
    if (exact.size() >= limit + meta.times.size() + meta.active.size()) break;
  }
  return std::vector<double>(exact.begin(), exact.end());
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
      out->uv_primvar = ::tinyusdz::next::GetPrimvarReaderVarname(stage, np);
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

  // colorSpace asset metadata on inputs:file takes precedence over the
  // sourceColorSpace attribute (legacy tydra resolution order). Applied on
  // BOTH extraction branches — the UsdUVTexture fast path below returns
  // early.
  auto apply_file_meta_color_space = [&texture_prim, out]() {
    if (const ::tinyusdz::next::PropMeta* file_meta =
            texture_prim.GetPropertyMeta("inputs:file")) {
      if (!file_meta->colorSpace.empty()) {
        out->source_color_space = file_meta->colorSpace;
      }
    }
  };

  ::tinyusdz::next::UVTextureData uv;
  if (::tinyusdz::next::GetUVTextureData(stage, texture_prim, &uv, time_code)) {
    out->file = uv.file;
    out->wrap_s = uv.wrap_s;
    out->wrap_t = uv.wrap_t;
    out->source_color_space = uv.source_color_space;
    std::memcpy(out->scale, uv.scale, sizeof(out->scale));
    std::memcpy(out->bias, uv.bias, sizeof(out->bias));
    apply_file_meta_color_space();
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
  apply_file_meta_color_space();
  TraceTextureStChain(stage, texture_prim, out);

  return true;
}

// MaterialX Autodesk standard_surface (usdMtlx flatten pattern).
bool IsStandardSurfaceShaderId(const std::string& id) {
  return id == "ND_standard_surface_surfaceshader" ||
         id == "standard_surface" ||
         id == "AutodeskStandardSurface" ||
         id == "MtlxAutodeskStandardSurface";
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

void ComputePointBounds(const FloatChunked& points, Float3* bbox_min,
                        Float3* bbox_max, bool* has_bbox) {
  if (!bbox_min || !bbox_max || !has_bbox) return;
  *has_bbox = false;
  if (points.size() < 3) return;
  *bbox_min = Float3(1e30f, 1e30f, 1e30f);
  *bbox_max = Float3(-1e30f, -1e30f, -1e30f);
  const size_t point_count = points.size() / 3;
  for (size_t i = 0; i < point_count; ++i) {
    const float x = points[i * 3 + 0];
    const float y = points[i * 3 + 1];
    const float z = points[i * 3 + 2];
    bbox_min->x = std::min(bbox_min->x, x);
    bbox_min->y = std::min(bbox_min->y, y);
    bbox_min->z = std::min(bbox_min->z, z);
    bbox_max->x = std::max(bbox_max->x, x);
    bbox_max->y = std::max(bbox_max->y, y);
    bbox_max->z = std::max(bbox_max->z, z);
  }
  *has_bbox = true;
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

bool ReadStringLikeProperty(const UsdPrim& prim, const std::string& name,
                            std::string* out) {
  if (!out) return false;
  if (GetString(prim, name, out) || GetToken(prim, name, out)) return true;
  const Value* v = GetAttribute(prim, name);
  if (!v) return false;
  if (const std::string* ap = v->as_asset_path()) {
    *out = *ap;
    return true;
  }
  return false;
}

std::vector<std::string> ReadTokenArrayProperty(const UsdPrim& prim,
                                                const std::string& name) {
  std::vector<std::string> out;
  const Value* value = prim.GetPropertyValue(name);
  if (!value) return out;
  if (const std::vector<std::string>* arr = value->as_token_array()) {
    return *arr;
  }
  if (const std::string* tok = value->as_token()) {
    out.push_back(*tok);
  } else if (const std::string* str = value->as_string()) {
    out.push_back(*str);
  }
  return out;
}

bool FirstArrayElementToFloat4(const std::vector<float>& values,
                               uint32_t stride,
                               Float4* out) {
  if (!out || values.empty() || stride == 0) return false;
  const float x = values.size() > 0 ? values[0] : 0.0f;
  const float y = values.size() > 1 ? values[1] : 0.0f;
  const float z = values.size() > 2 ? values[2] : 0.0f;
  const float w = values.size() > 3 ? values[3] : 0.0f;
  if (stride == 1) {
    *out = Float4(x, 0.0f, 0.0f, 0.0f);
  } else if (stride == 3) {
    *out = Float4(x, y, z, 0.0f);
  } else {
    *out = Float4(x, y, z, w);
  }
  return true;
}

bool JointTokenMatches(const SkeletonJoint& joint, const std::string& token) {
  if (token.empty()) return false;
  if (joint.path == token || joint.name == token) return true;
  if (LeafNameFromJointPath(joint.path) == token) return true;
  if (joint.path.size() > token.size() &&
      joint.path.compare(joint.path.size() - token.size(), token.size(),
                         token) == 0) {
    const size_t sep = joint.path.size() - token.size();
    return sep == 0 || joint.path[sep - 1] == '/';
  }
  return false;
}

// Material-driven UV primvar promotion: UsdPrimvarReader varnames other than
// the default "st" only survive as generic mesh.primvars entries, which no
// texture consumer samples. After materials are bound, promote the primvar
// each bound material's textures actually reference into texcoords_0/1
// (legacy selects UV sets from the shader network the same way).
void PromoteMaterialUVPrimvars(RenderScene* scene,
                               const std::string& default_uv,
                               std::vector<std::string>* warnings) {
  if (!scene) return;

  auto texture_uv_names = [scene](const RenderMaterial& mat,
                                  std::vector<std::string>* names) {
    auto add = [scene, names](const ShaderParam& p) {
      if (p.texture_id < 0 ||
          static_cast<size_t>(p.texture_id) >= scene->textures.size()) {
        return;
      }
      const std::string& uv =
          scene->textures[static_cast<size_t>(p.texture_id)].uv_primvar;
      if (uv.empty()) return;
      if (std::find(names->begin(), names->end(), uv) == names->end()) {
        names->push_back(uv);
      }
    };
    if (mat.preview_surface) {
      const PreviewSurfaceShader& ps = *mat.preview_surface;
      for (const ShaderParam* p :
           {&ps.diffuse_color, &ps.emissive_color, &ps.specular_color,
            &ps.metallic, &ps.roughness, &ps.clearcoat,
            &ps.clearcoat_roughness, &ps.opacity, &ps.opacity_threshold,
            &ps.ior, &ps.normal, &ps.displacement, &ps.occlusion}) {
        add(*p);
      }
    }
    if (mat.openpbr) {
      const OpenPBRSurfaceShader& o = *mat.openpbr;
      for (const ShaderParam* p :
           {&o.base_weight, &o.base_color, &o.base_roughness,
            &o.base_metalness, &o.specular_weight, &o.specular_color,
            &o.specular_roughness, &o.specular_ior, &o.transmission_weight,
            &o.coat_weight, &o.coat_color, &o.coat_roughness,
            &o.emission_luminance, &o.emission_color, &o.normal,
            &o.opacity}) {
        add(*p);
      }
    }
  };

  for (RenderMesh& mesh : scene->meshes) {
    // Gather UV names referenced by every material bound to this mesh
    // (direct binding + subsets).
    std::vector<int32_t> material_ids;
    if (mesh.material_id >= 0) material_ids.push_back(mesh.material_id);
    for (const RenderMesh::MaterialSubset& subset : mesh.material_subsets) {
      if (subset.material_id >= 0) material_ids.push_back(subset.material_id);
    }
    std::vector<std::string> wanted;
    for (int32_t mid : material_ids) {
      if (static_cast<size_t>(mid) >= scene->materials.size()) continue;
      texture_uv_names(scene->materials[static_cast<size_t>(mid)], &wanted);
    }
    // Names equal to the defaults are already in texcoords_0/1.
    wanted.erase(std::remove_if(wanted.begin(), wanted.end(),
                                [&default_uv](const std::string& n) {
                                  return n == default_uv ||
                                         n == default_uv + "1";
                                }),
                 wanted.end());
    if (wanted.empty()) continue;

    auto promote = [&mesh, warnings](const std::string& name,
                                     FloatChunked* dst,
                                     Interpolation* dst_interp) -> bool {
      for (size_t ai = 0; ai < mesh.primvars.size(); ++ai) {
        VertexAttribute& attr = mesh.primvars[ai];
        if (attr.name != name || attr.format != VertexFormat::Vec2) continue;
        dst->clear();
        if (attr.has_indices()) {
          const size_t elems = attr.float_data.size() / 2;
          for (size_t k = 0; k < attr.indices.size(); ++k) {
            const uint32_t idx = attr.indices[k];
            if (idx >= elems) {
              warnings->push_back("Mesh '" + mesh.prim_path + "': UV primvar '" +
                                  name + "' has out-of-range indices; not promoted");
              dst->clear();
              return false;
            }
            dst->push_back(attr.float_data[idx * 2 + 0]);
            dst->push_back(attr.float_data[idx * 2 + 1]);
          }
        } else {
          for (size_t k = 0; k < attr.float_data.size(); ++k) {
            dst->push_back(attr.float_data[k]);
          }
        }
        *dst_interp = attr.interpolation;
        mesh.primvars.erase(mesh.primvars.begin() +
                            static_cast<std::ptrdiff_t>(ai));
        return true;
      }
      return false;
    };

    // The material-referenced UV set takes the primary slot (matching
    // legacy's shader-network-driven selection); a second distinct name
    // fills the secondary slot when free.
    if (promote(wanted[0], &mesh.texcoords_0, &mesh.texcoords_0_interp)) {
      if (wanted.size() > 1 && mesh.texcoords_1.empty()) {
        promote(wanted[1], &mesh.texcoords_1, &mesh.texcoords_1_interp);
      }
    }
  }
}

void ResolveSkeletalAnimationTargets(RenderScene* scene) {
  if (!scene) return;
  for (size_t ai = 0; ai < scene->animations.size(); ++ai) {
    AnimationClip& clip = scene->animations[ai];
    for (AnimationChannel& channel : clip.channels) {
      if (!channel.is_skeletal) continue;

      int32_t skeleton_id = -1;
      for (size_t si = 0; si < scene->skeletons.size(); ++si) {
        const Skeleton& skel = scene->skeletons[si];
        if (!skel.animation_source_path.empty() &&
            skel.animation_source_path == clip.prim_path) {
          skeleton_id = static_cast<int32_t>(si);
          break;
        }
      }
      if (skeleton_id < 0 && !channel.joint_order.empty()) {
        size_t best_matches = 0;
        for (size_t si = 0; si < scene->skeletons.size(); ++si) {
          const Skeleton& skel = scene->skeletons[si];
          size_t matches = 0;
          for (const std::string& token : channel.joint_order) {
            for (const SkeletonJoint& joint : skel.joints) {
              if (JointTokenMatches(joint, token)) {
                ++matches;
                break;
              }
            }
          }
          if (matches > best_matches) {
            best_matches = matches;
            skeleton_id = static_cast<int32_t>(si);
          }
        }
      }

      channel.target_skeleton = skeleton_id;
      channel.joint_remap.clear();
      if (skeleton_id < 0 ||
          static_cast<size_t>(skeleton_id) >= scene->skeletons.size()) {
        continue;
      }
      const Skeleton& skel = scene->skeletons[static_cast<size_t>(skeleton_id)];
      channel.target_skeleton_path = skel.prim_path;
      channel.joint_remap.reserve(channel.joint_order.size());
      for (const std::string& token : channel.joint_order) {
        int32_t joint_id = -1;
        for (size_t ji = 0; ji < skel.joints.size(); ++ji) {
          if (JointTokenMatches(skel.joints[ji], token)) {
            joint_id = static_cast<int32_t>(ji);
            break;
          }
        }
        channel.joint_remap.push_back(joint_id);
      }
      scene->skeletons[static_cast<size_t>(skeleton_id)].animation_id =
          static_cast<int32_t>(ai);
    }
  }
}

bool PathIsAtOrUnder(const std::string& path, const std::string& root) {
  if (root.empty() || path.empty()) return false;
  if (path == root) return true;
  return path.size() > root.size() &&
         path.compare(0, root.size(), root) == 0 && path[root.size()] == '/';
}

// Resolve one CollectionAPI instance (collection:<name>:*) on a light prim
// to RenderScene mesh indices, mirroring legacy ResolveLightLinking:
// excludes take hierarchical precedence, includeRoot adds the light prim's
// subtree, explicitOnly matches exact paths, expandPrims (default) and
// expandPrimsAndProperties match descendants. Unauthored collections keep
// *links_all = true (light affects everything); membershipExpression
// collections are not evaluated (no path-expression parser in next) and
// also keep the links-all default.
void ResolveLightLinkInstance(const UsdPrim& prim, const RenderScene& scene,
                              const std::string& instance_name,
                              bool* links_all,
                              std::vector<int32_t>* mesh_indices) {
  const std::string base = "collection:" + instance_name + ":";
  if (prim.HasProperty(base + "membershipExpression")) return;

  const std::vector<::tinyusdz::next::Path>* includes =
      prim.GetRelationship(base + "includes");
  const std::vector<::tinyusdz::next::Path>* excludes =
      prim.GetRelationship(base + "excludes");
  if (!includes && !excludes) return;  // unauthored -> links all

  bool include_root = false;
  GetBool(prim, base + "includeRoot", &include_root);
  std::string rule = "expandPrims";
  GetToken(prim, base + "expansionRule", &rule);
  const bool explicit_only = (rule == "explicitOnly");
  const std::string& owner_path = prim.GetPath().str();

  *links_all = false;
  mesh_indices->clear();
  for (size_t mi = 0; mi < scene.meshes.size(); ++mi) {
    const std::string& mesh_path = scene.meshes[mi].prim_path;
    bool excluded = false;
    if (excludes) {
      for (const ::tinyusdz::next::Path& p : *excludes) {
        if (PathIsAtOrUnder(mesh_path, p.str())) { excluded = true; break; }
      }
    }
    if (excluded) continue;

    bool included = include_root && PathIsAtOrUnder(mesh_path, owner_path);
    if (!included && includes) {
      for (const ::tinyusdz::next::Path& p : *includes) {
        if (explicit_only ? (mesh_path == p.str())
                          : PathIsAtOrUnder(mesh_path, p.str())) {
          included = true;
          break;
        }
      }
    }
    if (included) mesh_indices->push_back(static_cast<int32_t>(mi));
  }
}

void ResolveLightLinking(const Stage& stage, RenderScene* scene) {
  if (!scene) return;
  for (RenderLight& light : scene->lights) {
    UsdPrim prim = stage.GetPrimAtPath(light.prim_path);
    if (!prim.IsValid()) continue;
    ResolveLightLinkInstance(prim, *scene, "lightLink",
                             &light.light_links_all,
                             &light.light_link_mesh_indices);
    ResolveLightLinkInstance(prim, *scene, "shadowLink",
                             &light.shadow_links_all,
                             &light.shadow_link_mesh_indices);
  }
}

std::vector<std::string> ReadRelationshipTargets(const UsdPrim& prim,
                                                 const std::string& name) {
  std::vector<std::string> out;
  const std::vector<::tinyusdz::next::Path>* targets =
      prim.GetRelationship(name);
  if (!targets) return out;
  out.reserve(targets->size());
  for (const ::tinyusdz::next::Path& target : *targets) {
    out.push_back(target.str());
  }
  return out;
}

double ReadDoubleProperty(const UsdPrim& prim, const std::string& name,
                          double fallback) {
  double d = fallback;
  if (GetDouble(prim, name, &d)) return d;
  return fallback;
}

void ApplyAxis(std::vector<value::float3>* points,
               std::vector<value::float3>* normals,
               const std::string& axis) {
  if (axis == "Y" || axis.empty()) return;
  auto map_point = [&](value::float3& v) {
    const float x = v[0], y = v[1], z = v[2];
    if (axis == "Z") {
      v[0] = x; v[1] = z; v[2] = y;
    } else if (axis == "X") {
      v[0] = y; v[1] = x; v[2] = z;
    }
  };
  for (value::float3& p : *points) map_point(p);
  if (normals) {
    for (value::float3& n : *normals) map_point(n);
  }
}

void FillGeneratedMesh(const UsdPrim& prim,
                       const std::vector<value::float3>& points,
                       const std::vector<int>& face_counts,
                       const std::vector<int>& face_indices,
                       const std::vector<value::float3>& normals,
                       const std::vector<value::float2>& uvs,
                       RenderMesh* out) {
  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  out->face_vertex_counts.reserve(face_counts.size());
  for (int c : face_counts) {
    out->face_vertex_counts.push_back(c < 0 ? uint32_t{0}
                                            : static_cast<uint32_t>(c));
  }
  out->face_vertex_indices.reserve(face_indices.size());
  for (int idx : face_indices) {
    out->face_vertex_indices.push_back(idx < 0 ? uint32_t{0}
                                               : static_cast<uint32_t>(idx));
  }
  for (const value::float3& p : points) {
    out->points.push_back(p[0]);
    out->points.push_back(p[1]);
    out->points.push_back(p[2]);
  }
  if (!normals.empty()) {
    out->normals_interp = Interpolation::FaceVarying;
    for (const value::float3& n : normals) {
      out->normals.push_back(n[0]);
      out->normals.push_back(n[1]);
      out->normals.push_back(n[2]);
    }
  }
  if (!uvs.empty()) {
    out->texcoords_0_interp = Interpolation::FaceVarying;
    for (const value::float2& uv : uvs) {
      out->texcoords_0.push_back(uv[0]);
      out->texcoords_0.push_back(uv[1]);
    }
  }
  std::string orientation;
  if (GetToken(prim, "orientation", &orientation)) {
    out->left_handed = (orientation == "leftHanded");
  }
  if (!points.empty()) {
    out->bbox_min = Float3(1e30f, 1e30f, 1e30f);
    out->bbox_max = Float3(-1e30f, -1e30f, -1e30f);
    for (const value::float3& p : points) {
      out->bbox_min.x = std::min(out->bbox_min.x, p[0]);
      out->bbox_min.y = std::min(out->bbox_min.y, p[1]);
      out->bbox_min.z = std::min(out->bbox_min.z, p[2]);
      out->bbox_max.x = std::max(out->bbox_max.x, p[0]);
      out->bbox_max.y = std::max(out->bbox_max.y, p[1]);
      out->bbox_max.z = std::max(out->bbox_max.z, p[2]);
    }
    out->has_bbox = true;
  }
}

void ExtractMaterialXConfig(const UsdPrim& prim,
                            RenderMaterial::MaterialXConfig* out) {
  if (!out || !prim.IsValid()) return;
  std::string v;
  if (ReadStringLikeProperty(prim, "config:mtlx:version", &v)) {
    out->version = v;
    out->authored = true;
  }
  if (ReadStringLikeProperty(prim, "config:mtlx:namespace", &v)) {
    out->name_space = v;
    out->authored = true;
  }
  if (ReadStringLikeProperty(prim, "config:mtlx:colorspace", &v)) {
    out->colorspace = v;
    out->authored = true;
  }
  if (ReadStringLikeProperty(prim, "config:mtlx:sourceUri", &v) ||
      ReadStringLikeProperty(prim, "config:mtlx:sourceAsset", &v) ||
      ReadStringLikeProperty(prim, "config:mtlx:file", &v)) {
    out->source_uri = v;
    out->authored = true;
  }
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

// First bound material path whose target actually resolves to a Material,
// walking the purpose order (preview, all-purpose, full). A dangling
// purpose-specific rel must fall through to the weaker-purpose rel on the
// SAME prim, not reject the prim (GetBoundMaterialPath returns only the
// first authored rel).
std::string FirstValidBoundMaterialPath(const Stage& stage,
                                        const ::tinyusdz::next::UsdPrim& prim) {
  static const char* kBindingOrder[] = {"material:binding:preview",
                                        "material:binding",
                                        "material:binding:full"};
  for (const char* rel : kBindingOrder) {
    const std::vector<::tinyusdz::next::Path>* targets =
        prim.GetRelationship(rel);
    if (!targets || targets->empty()) continue;
    const std::string p = (*targets)[0].str();
    if (!p.empty() && ::tinyusdz::tydra::next::IsMaterial(
                          stage.GetPrimAtPath(p))) {
      return p;
    }
  }
  return "";
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
      // Per-purpose validation: a dangling target must not shadow either a
      // weaker-purpose rel on the same prim or a valid ancestor binding
      // (legacy skips unresolvable targets).
      const std::string material_path = FirstValidBoundMaterialPath(stage, prim);
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
    for (const RenderPrimRecord& rec : extracted.records) {
      if (rec.type_name == "Points") continue;
      if (!IsUnsupportedRenderableTypeName(rec.type_name)) continue;
      UnsupportedRenderable unsupported;
      unsupported.prim_path = rec.path;
      unsupported.type_name = rec.type_name;
      unsupported.reason =
          "recognized by extraction but not converted to render geometry";
      result.scene.unsupported_renderables.push_back(unsupported);
      warnings_.push_back("Unsupported renderable prim '" + rec.path +
                          "' of type '" + rec.type_name + "'");
    }

    for (const auto& rec : extracted.records) {
      AnimationClip clip;
      if (ConvertAnimation(stage, rec.prim, &clip)) {
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
      const bool converted =
          mesh_prim.GetTypeName() == "Mesh"
              ? ConvertMesh(stage, mesh_prim, &mesh)
              : ConvertGeomPrimitive(mesh_prim, &mesh);
      if (converted && mesh.has_alloc_failure()) {
        // ConvertGeomPrimitive does not run ConvertMesh's alloc check.
        warnings_.push_back("Out of memory converting prim '" +
                            mesh_prim.GetPath().str() +
                            "'; the prim was skipped");
        continue;
      }
      if (converted) {
        // Release chunk-allocation slack before retaining: thousands of small
        // meshes each holding 64KB-minimum chunks otherwise OOM wasm32.
        mesh.compact();
        int32_t mesh_id = static_cast<int32_t>(result.scene.meshes.size());
        result.scene.mesh_by_path[mesh.prim_path] = mesh_id;
        result.scene.meshes.push_back(std::move(mesh));
        AssignNodeDataId(&result.scene, mesh_prim.GetPath().str(), mesh_id);
      } else {
        warnings_.push_back("Failed to convert renderable mesh prim: " +
                            mesh_prim.GetPath().str());
      }
    }

    for (const auto& rec : extracted.records) {
      if (rec.type_name != "Points") continue;
      RenderPoints points;
      if (ConvertPoints(rec.prim, &points)) {
        if (points.points.alloc_failed() || points.widths.alloc_failed() ||
            points.colors.alloc_failed()) {
          warnings_.push_back("Out of memory converting Points '" + rec.path +
                              "'; the prim was skipped");
          continue;
        }
        int32_t points_id = static_cast<int32_t>(result.scene.points.size());
        result.scene.points_by_path[points.prim_path] = points_id;
        result.scene.points.push_back(std::move(points));
        AssignNodeDataId(&result.scene, rec.path, points_id);
      } else {
        warnings_.push_back("Failed to convert Points: " + rec.path);
      }
    }

    for (const auto& rec : extracted.curves) {
      RenderCurves curves;
      if (ConvertCurves(rec.prim, &curves)) {
        if (curves.points.alloc_failed() || curves.widths.alloc_failed() ||
            curves.colors.alloc_failed() ||
            curves.tessellated_points.alloc_failed() ||
            curves.tessellated_widths.alloc_failed() ||
            curves.tessellated_colors.alloc_failed()) {
          warnings_.push_back("Out of memory converting curves '" + rec.path +
                              "'; the prim was skipped");
          continue;
        }
        int32_t curves_id = static_cast<int32_t>(result.scene.curves.size());
        result.scene.curves_by_path[curves.prim_path] = curves_id;
        result.scene.curves.push_back(std::move(curves));
        AssignNodeDataId(&result.scene, rec.path, curves_id);
      } else {
        warnings_.push_back("Failed to convert curves prim: " + rec.path);
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
    PromoteMaterialUVPrimvars(&result.scene, config_.mesh.default_uv_primvar,
                              &warnings_);
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
        // skel:animationSource may be authored on the SkelRoot (or another
        // ancestor) instead of the Skeleton itself; every descendant
        // Skeleton inherits it (UsdSkel binding inheritance).
        if (skeleton.animation_source_path.empty()) {
          UsdPrim anc = GetParent(stage, rec.prim);
          while (anc.IsValid()) {
            const std::vector<std::string> sources =
                ReadRelationshipTargets(anc, "skel:animationSource");
            if (!sources.empty()) {
              skeleton.animation_source_path = sources[0];
              break;
            }
            if (::tinyusdz::tydra::next::IsSkelRoot(anc)) break;
            anc = GetParent(stage, anc);
          }
        }
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

      // Mesh-local `skel:joints`: jointIndices index into the mesh's own
      // (subset/permuted) joint list — remap them onto the skeleton's joint
      // order. Unmatched tokens zero the influence weight rather than
      // silently deforming by joint 0.
      if (!mesh.skin->mesh_joint_order.empty() &&
          mesh.skin->skeleton_id >= 0) {
        const Skeleton& skel =
            result.scene.skeletons[static_cast<size_t>(mesh.skin->skeleton_id)];
        std::vector<int32_t> remap(mesh.skin->mesh_joint_order.size(), -1);
        bool identity = true;
        for (size_t k = 0; k < mesh.skin->mesh_joint_order.size(); ++k) {
          const std::string& token = mesh.skin->mesh_joint_order[k];
          for (size_t ji = 0; ji < skel.joints.size(); ++ji) {
            if (JointTokenMatches(skel.joints[ji], token)) {
              remap[k] = static_cast<int32_t>(ji);
              break;
            }
          }
          if (remap[k] != static_cast<int32_t>(k)) identity = false;
          if (remap[k] < 0) {
            warnings_.push_back("Mesh " + mesh.prim_path +
                                " skel:joints token '" + token +
                                "' not found in skeleton " + skel.prim_path);
          }
        }
        if (!identity) {
          const size_t n = mesh.skin->joint_indices.size();
          for (size_t k = 0; k < n; ++k) {
            const uint16_t local = mesh.skin->joint_indices[k];
            const int32_t target =
                local < remap.size() ? remap[local] : -1;
            if (target >= 0 && target <= 65535) {
              mesh.skin->joint_indices[k] = static_cast<uint16_t>(target);
            } else {
              mesh.skin->joint_indices[k] = 0;
              if (k < mesh.skin->joint_weights.size()) {
                mesh.skin->joint_weights[k] = 0.0f;
              }
            }
          }
        }
      }
    }

    ResolveSkeletalAnimationTargets(&result.scene);

    ResolveLightLinking(stage, &result.scene);

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
    if (IsMeshRenderableTypeName(type)) node.type = NodeType::Mesh;
    else if (type == "Points") node.type = NodeType::Points;
    else if (type == "BasisCurves" || type == "NurbsCurves") node.type = NodeType::Curves;
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
        case LightKind::GeometryLight: node.type = NodeType::PointLight; break;
        case LightKind::PortalLight: node.type = NodeType::RectLight; break;
        case LightKind::PluginLight: node.type = NodeType::PointLight; break;
        case LightKind::LightFilter: node.type = NodeType::PointLight; break;
        case LightKind::PluginLightFilter: node.type = NodeType::PointLight; break;
        case LightKind::Unknown: node.type = NodeType::PointLight; break;
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
  for (RenderCurves& curves : scene->curves) {
    const std::string material_path =
        FindInheritedMaterialBinding(stage, curves.prim_path);
    if (!material_path.empty()) {
      const auto it = scene->material_by_path.find(material_path);
      if (it != scene->material_by_path.end()) curves.material_id = it->second;
    }
  }
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
          sub_mat = FirstValidBoundMaterialPath(stage, sub_prim);
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

    // Remap subset runs from authored polygon-face space into TRIANGLE
    // space using the triangulation prefix sums (an N-gon becomes N-2
    // triangles; holes/degenerate faces contribute 0). Authored subset
    // indices no longer align when sanitization dropped faces — skip with
    // a warning rather than mis-assign materials.
    if (!mesh.material_subsets.empty() &&
        !mesh.face_triangle_offsets.empty()) {
      if (mesh.sanitize_dropped_faces > 0) {
        warnings_.push_back("Mesh '" + mesh.prim_path +
                            "': GeomSubset material bindings skipped (topology "
                            "sanitization dropped faces; authored subset "
                            "indices no longer align)");
        mesh.material_subsets.clear();
      } else {
        const std::vector<uint32_t>& offs = mesh.face_triangle_offsets;
        const uint32_t nfaces_tri =
            static_cast<uint32_t>(offs.size() > 0 ? offs.size() - 1 : 0);
        std::vector<RenderMesh::MaterialSubset> remapped;
        remapped.reserve(mesh.material_subsets.size());
        for (const RenderMesh::MaterialSubset& ms : mesh.material_subsets) {
          if (ms.face_start >= nfaces_tri) continue;
          const uint32_t face_end =
              std::min(ms.face_start + ms.face_count, nfaces_tri);
          const uint32_t tri_start = offs[ms.face_start];
          const uint32_t tri_count = offs[face_end] - tri_start;
          if (tri_count == 0) continue;
          remapped.push_back(
              RenderMesh::MaterialSubset{tri_start, tri_count, ms.material_id});
        }
        mesh.material_subsets = std::move(remapped);
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

bool RenderSceneConverter::ConvertGeomPrimitive(const UsdPrim& prim,
                                                RenderMesh* out) {
  if (!out || !prim.IsValid() || !IsAnalyticGeomTypeName(prim.GetTypeName())) {
    last_error_ = "Invalid analytic geom prim";
    return false;
  }

  std::vector<value::float3> points;
  std::vector<int> face_counts;
  std::vector<int> face_indices;
  std::vector<value::float3> normals;
  std::vector<value::float2> uvs;

  const std::string type = prim.GetTypeName();
  if (type == "Cube") {
    ::tinyusdz::tydra::GenerateCubeMesh(
        ReadDoubleProperty(prim, "size", 2.0), points, face_counts,
        face_indices, normals, uvs);
  } else if (type == "Sphere") {
    ::tinyusdz::tydra::GenerateIcosphereMesh(
        ReadDoubleProperty(prim, "radius", 2.0), 2, points, face_counts,
        face_indices, normals, uvs);
  } else if (type == "Cylinder" || type == "Cylinder_1") {
    double radius = ReadDoubleProperty(prim, "radius", 1.0);
    if (type == "Cylinder_1") {
      const double rt = ReadDoubleProperty(prim, "radiusTop", 1.0);
      const double rb = ReadDoubleProperty(prim, "radiusBottom", 1.0);
      radius = std::max(rt, rb);
      if (std::fabs(rt - rb) > 1.0e-9) {
        warnings_.push_back("Cylinder_1 '" + prim.GetPath().str() +
                            "': tapered radii are approximated with max radius");
      }
    }
    ::tinyusdz::tydra::GenerateCylinderMesh(
        radius, ReadDoubleProperty(prim, "height", 2.0), 24, 1, points,
        face_counts, face_indices, normals, uvs);
  } else if (type == "Cone") {
    ::tinyusdz::tydra::GenerateConeMesh(
        ReadDoubleProperty(prim, "radius", 1.0),
        ReadDoubleProperty(prim, "height", 2.0), 24, points, face_counts,
        face_indices, normals, uvs);
  } else if (type == "Capsule" || type == "Capsule_1") {
    double radius = ReadDoubleProperty(prim, "radius", 0.5);
    double height = ReadDoubleProperty(prim, "height", type == "Capsule_1" ? 1.0 : 2.0);
    if (type == "Capsule_1") {
      const double rt = ReadDoubleProperty(prim, "radiusTop", 0.5);
      const double rb = ReadDoubleProperty(prim, "radiusBottom", 0.5);
      radius = std::max(rt, rb);
      if (std::fabs(rt - rb) > 1.0e-9) {
        warnings_.push_back("Capsule_1 '" + prim.GetPath().str() +
                            "': asymmetric radii are approximated with max radius");
      }
    }
    ::tinyusdz::tydra::GenerateCapsuleMesh(radius, height, 24, 1, points,
                                           face_counts, face_indices, normals,
                                           uvs);
  } else if (type == "Plane") {
    ::tinyusdz::tydra::GeneratePlaneMesh(
        ReadDoubleProperty(prim, "width", 2.0),
        ReadDoubleProperty(prim, "length", 2.0), 1, 1, points, face_counts,
        face_indices, normals, uvs);
  } else {
    last_error_ = "Unsupported analytic geom prim";
    return false;
  }

  std::string axis = "Z";
  GetToken(prim, "axis", &axis);
  if (type == "Cube" || type == "Sphere") axis = "Y";
  ApplyAxis(&points, &normals, axis);
  FillGeneratedMesh(prim, points, face_counts, face_indices, normals, uvs, out);
  SanitizeMeshTopology(out);
  if (config_.mesh.triangulate && !out->is_triangulated) {
    TriangulateMesh(out);
  }
  if (config_.mesh.compute_normals && out->normals.empty()) {
    ComputeVertexNormals(out);
  }
  if (config_.mesh.compute_tangents && out->tangents.empty()) {
    ComputeVertexTangents(out);
  }
  return true;
}

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
    const bool has_skin_binding = GetSkinBinding(prim, &sb);
    if (has_skin_binding && !sb.joint_indices.empty() &&
        sb.joint_indices.size() != sb.joint_weights.size()) {
      // e.g. one indexed skin primvar expanded while its pair stayed
      // authored (malformed :indices). Skipping silently leaves the mesh in
      // bind pose with no hint why.
      warnings_.push_back("Mismatched skin jointIndices/jointWeights sizes on " +
                          prim.GetPath().str() + "; mesh renders unskinned");
    }
    if (has_skin_binding && !sb.joint_indices.empty() &&
        sb.joint_indices.size() == sb.joint_weights.size()) {
      // UsdSkel binding inheritance: `skel:skeleton` may be authored on an
      // ancestor (typically the enclosing SkelRoot) rather than on the mesh
      // itself, in which case every descendant skinnable prim inherits it.
      // GetSkinBinding only reads the mesh prim, so walk up to the nearest
      // ancestor binding when the mesh doesn't author one directly. Without
      // this, meshes that bind their skeleton at the SkelRoot (e.g. the
      // MetaHuman standalone face/body exports) resolve no skeleton_id and
      // render unskinned in bind pose.
      if (sb.skeleton_path.empty()) {
        UsdPrim anc = GetParent(stage, prim);
        UsdPrim skel_root;
        while (anc.IsValid()) {
          std::string inherited = GetBoundSkeleton(anc);
          if (!inherited.empty()) {
            sb.skeleton_path = inherited;
            break;
          }
          // Binding inheritance is scoped to the SkelRoot subtree.
          if (::tinyusdz::tydra::next::IsSkelRoot(anc)) {
            skel_root = anc;
            break;
          }
          anc = GetParent(stage, anc);
        }
        // No authored binding anywhere: fall back to a Skeleton contained
        // in the enclosing SkelRoot (common Blender / older-exporter shape;
        // legacy tydra binds this way too). First Skeleton in the subtree
        // wins, matching legacy.
        if (sb.skeleton_path.empty() && skel_root.IsValid()) {
          for (const UsdPrim& desc : GetDescendants(skel_root)) {
            if (::tinyusdz::tydra::next::IsSkeleton(desc)) {
              sb.skeleton_path = desc.GetPath().str();
              break;
            }
          }
        }
      }
      const size_t point_count = out->point_count();
      size_t influences = sb.influences_per_vertex > 0
                              ? static_cast<size_t>(sb.influences_per_vertex)
                              : 0;
      if (influences == 0 && point_count > 0 &&
          (sb.joint_indices.size() % point_count) == 0) {
        influences = sb.joint_indices.size() / point_count;
      }
      if (influences > 0 && point_count > 1 &&
          sb.joint_indices.size() == influences) {
        const std::vector<int32_t> indices = sb.joint_indices;
        const std::vector<float> weights = sb.joint_weights;
        sb.joint_indices.clear();
        sb.joint_weights.clear();
        sb.joint_indices.reserve(point_count * influences);
        sb.joint_weights.reserve(point_count * influences);
        for (size_t point = 0; point < point_count; ++point) {
          sb.joint_indices.insert(sb.joint_indices.end(), indices.begin(),
                                  indices.end());
          sb.joint_weights.insert(sb.joint_weights.end(), weights.begin(),
                                  weights.end());
        }
      }
      if (influences == 0 || point_count == 0 ||
          sb.joint_indices.size() != point_count * influences) {
        warnings_.push_back("Ignoring malformed skin influences on " +
                            prim.GetPath().str());
      } else {
        size_t output_influences = influences;
        std::vector<int32_t> reduced_indices;
        std::vector<float> reduced_weights;
        if (config_.mesh.enable_bone_reduction &&
            config_.mesh.target_bone_count > 0 &&
            config_.mesh.target_bone_count < influences &&
            // ~8B per point-influence pair of temporaries; on a nearly-full
            // heap keep the authored influences instead of abort()ing.
            !WouldOverflowSizeMul(point_count, output_influences * 8) &&
            ProbeAlloc(point_count * config_.mesh.target_bone_count * 8)) {
          output_influences = config_.mesh.target_bone_count;
          reduced_indices.resize(point_count * output_influences, 0);
          reduced_weights.resize(point_count * output_influences, 0.0f);
          std::vector<std::pair<float, int32_t>> ranked(influences);
          for (size_t point = 0; point < point_count; ++point) {
            const size_t source = point * influences;
            for (size_t i = 0; i < influences; ++i) {
              ranked[i] = {sb.joint_weights[source + i],
                           sb.joint_indices[source + i]};
            }
            std::stable_sort(
                ranked.begin(), ranked.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });
            float sum = 0.0f;
            for (size_t i = 0; i < output_influences; ++i) {
              sum += std::max(0.0f, ranked[i].first);
            }
            for (size_t i = 0; i < output_influences; ++i) {
              const size_t destination = point * output_influences + i;
              reduced_indices[destination] = ranked[i].second;
              reduced_weights[destination] =
                  sum > 0.0f ? std::max(0.0f, ranked[i].first) / sum
                             : (i == 0 ? 1.0f : 0.0f);
            }
          }
          sb.joint_indices = std::move(reduced_indices);
          sb.joint_weights = std::move(reduced_weights);
        }

      out->skin = std::make_unique<RenderMesh::SkinBinding>();
      out->skin->joint_indices.reserve(sb.joint_indices.size());
      for (int32_t ji : sb.joint_indices) {
        out->skin->joint_indices.push_back(
            ji < 0 ? uint16_t(0)
                   : static_cast<uint16_t>(std::min<int32_t>(ji, 65535)));
      }
      out->skin->joint_weights.append(sb.joint_weights.data(),
                                      sb.joint_weights.size());
      out->skin->influences_per_vertex =
          static_cast<uint32_t>(output_influences);
      out->skin->mesh_joint_order = std::move(sb.joint_order);
      std::memcpy(out->skin->geom_bind_transform.m, sb.geom_bind_transform,
                  sizeof(sb.geom_bind_transform));
      // skeleton_id is resolved by the caller once skeletons are converted
      // (stored in skin->skeleton_id via the path recorded here).
      out->skin->skeleton_path = sb.skeleton_path;
      }
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
    for (const ::tinyusdz::next::BlendShapeData::Inbetween& source :
         bd.inbetweens) {
      if (source.offsets.size() != bd.offsets.size()) {
        warnings_.push_back("Ignoring malformed in-between '" + source.name +
                            "' on " + bs_prim.GetPath().str());
        continue;
      }
      RenderMesh::BlendShape::Inbetween inbetween;
      inbetween.name = source.name;
      inbetween.weight = source.weight;
      inbetween.point_offsets.append(source.offsets.data(),
                                     source.offsets.size());
      shape.inbetweens.push_back(std::move(inbetween));
    }
    out->blend_shapes.push_back(std::move(shape));
  }

  // Triangulate if requested. A mesh whose faces were all sanitized away is
  // still a valid (empty) render mesh; only meshes with real topology that
  // cannot be triangulated (e.g. over the temp-allocation budget) are dropped.
  if (config_.mesh.triangulate && !out->is_triangulated) {
    if (!TriangulateMesh(out) && !out->face_vertex_counts.empty()) {
      warnings_.push_back("Failed to triangulate mesh '" + out->prim_path +
                          "'; skipping it to avoid conversion abort");
      return false;
    }
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

  // A chunk allocation may have failed anywhere above (nothrow growth): the
  // mesh data is truncated, so report and drop the prim instead of rendering
  // partial geometry (or aborting the module, as a throwing new would under
  // -fno-exceptions).
  if (out->has_alloc_failure()) {
    warnings_.push_back("Out of memory converting mesh '" + out->prim_path +
                        "'; the prim was skipped");
    return false;
  }

  return true;
}

// Tangent frame from triangulated topology. Lengyel keeps the compact
// per-vertex path. MikkTSpace-style methods expand to face corners first so
// UV seams and mirrored islands are not averaged through shared point indices.
bool RenderSceneConverter::ComputeVertexTangents(RenderMesh* mesh) {
  if (!mesh->is_triangulated) {
    if (!TriangulateMesh(mesh)) return false;
  }
  const size_t np = mesh->point_count();
  if (np == 0) return false;

  const bool vertex_normals =
      mesh->normals_interp == Interpolation::Vertex &&
      mesh->normals.size() == np * 3;
  const bool vertex_uvs =
      mesh->texcoords_0_interp == Interpolation::Vertex &&
      mesh->texcoords_0.size() == np * 2;

  const size_t authored_corner_count = mesh->face_vertex_indices.size();
  const size_t tri_corner_count = mesh->triangulated_indices.size();
  const bool facevarying_normals =
      mesh->normals_interp == Interpolation::FaceVarying &&
      (mesh->normals.size() == authored_corner_count * 3 ||
       mesh->normals.size() == tri_corner_count * 3);
  const bool facevarying_uvs =
      mesh->texcoords_0_interp == Interpolation::FaceVarying &&
      (mesh->texcoords_0.size() == authored_corner_count * 2 ||
       mesh->texcoords_0.size() == tri_corner_count * 2);

  if ((!vertex_normals && !facevarying_normals) ||
      (!vertex_uvs && !facevarying_uvs)) {
    return false;
  }

  // Pre-flight the temporary buffers: the corner-expanded MikkTSpace path
  // allocates ~64B per triangulated corner and the Lengyel path ~40B per
  // point. A failed probe skips tangents for this mesh (they are optional)
  // instead of abort()ing the module under -fno-exceptions.
  const size_t probe_bytes =
      (config_.mesh.tangent_method ==
           MeshConfig::TangentComputationMethod::Lengyel &&
       vertex_normals && vertex_uvs)
          ? np * (3 + 3 + 4) * sizeof(float)
          : tri_corner_count * 64;
  if (WouldOverflowSizeMul(tri_corner_count, 64) ||
      !ProbeAlloc(probe_bytes)) {
    warnings_.push_back("Out of memory computing tangents for mesh '" +
                        mesh->prim_path + "'; tangents skipped");
    return false;
  }

  std::vector<float> tan(np * 3, 0.0f);
  std::vector<float> bit(np * 3, 0.0f);
  const size_t ntris = mesh->triangulated_indices.size() / 3;
  if (config_.mesh.tangent_method == MeshConfig::TangentComputationMethod::Lengyel &&
      vertex_normals && vertex_uvs) {
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
  mesh->tangents_interp = Interpolation::Vertex;
  return true;
  }

  std::vector<value::float3> fv_positions(tri_corner_count);
  std::vector<value::float3> fv_normals(tri_corner_count);
  std::vector<value::float2> fv_uvs(tri_corner_count);
  std::vector<uint32_t> tri_counts(ntris, 3);

  const bool tri_corner_remap =
      mesh->triangulated_face_vertex_indices.size() == tri_corner_count;

  for (size_t c = 0; c < tri_corner_count; ++c) {
    const uint32_t point_id = mesh->triangulated_indices[c];
    if (point_id >= np) return false;

    const size_t authored_corner =
        tri_corner_remap ? mesh->triangulated_face_vertex_indices[c] : c;

    const size_t p3 = size_t(point_id) * 3;
    fv_positions[c] = {mesh->points[p3 + 0], mesh->points[p3 + 1],
                       mesh->points[p3 + 2]};

    size_t nidx = 0;
    if (vertex_normals) {
      nidx = size_t(point_id);
    } else if (mesh->normals.size() == tri_corner_count * 3) {
      nidx = c;
    } else {
      if (authored_corner >= authored_corner_count) return false;
      nidx = authored_corner;
    }
    const size_t n3 = nidx * 3;
    if (n3 + 2 >= mesh->normals.size()) return false;
    fv_normals[c] = {mesh->normals[n3 + 0], mesh->normals[n3 + 1],
                     mesh->normals[n3 + 2]};

    size_t uvidx = 0;
    if (vertex_uvs) {
      uvidx = size_t(point_id);
    } else if (mesh->texcoords_0.size() == tri_corner_count * 2) {
      uvidx = c;
    } else {
      if (authored_corner >= authored_corner_count) return false;
      uvidx = authored_corner;
    }
    const size_t uv2 = uvidx * 2;
    if (uv2 + 1 >= mesh->texcoords_0.size()) return false;
    fv_uvs[c] = {mesh->texcoords_0[uv2 + 0], mesh->texcoords_0[uv2 + 1]};
  }

  std::vector<value::float3> fv_tangents;
  std::vector<value::float3> fv_binormals;
  std::string tangent_error;
  bool tangent_ok = false;

  switch (config_.mesh.tangent_method) {
    case MeshConfig::TangentComputationMethod::MikkTSpace:
      tangent_ok = ::tinyusdz::tydra::ComputeTangentsMikkTSpace(
          fv_positions, fv_normals, fv_uvs, tri_counts, &fv_tangents,
          &fv_binormals, &tangent_error);
      break;
    case MeshConfig::TangentComputationMethod::FastMikkTSpace:
      tangent_ok = ::tinyusdz::tydra::fast_mikkt::ComputeTangentsFastMikkTSpace(
          fv_positions, fv_normals, fv_uvs, tri_counts, &fv_tangents,
          &fv_binormals, &tangent_error);
      break;
    case MeshConfig::TangentComputationMethod::Hybrid: {
      ::tinyusdz::tydra::fast_mikkt::HybridStats stats = {};
      tangent_ok = ::tinyusdz::tydra::fast_mikkt::ComputeTangentsHybrid(
          fv_positions, fv_normals, fv_uvs, tri_counts, &fv_tangents,
          &fv_binormals, &stats, &tangent_error);
      break;
    }
    case MeshConfig::TangentComputationMethod::Lengyel:
      // Face-varying Lengyel is intentionally not duplicated here; Hybrid is
      // the O(n) seam-aware fallback for non-vertex data.
      tangent_ok = ::tinyusdz::tydra::fast_mikkt::ComputeTangentsHybrid(
          fv_positions, fv_normals, fv_uvs, tri_counts, &fv_tangents,
          &fv_binormals, nullptr, &tangent_error);
      break;
  }

  if (!tangent_ok || fv_tangents.size() != tri_corner_count ||
      fv_binormals.size() != tri_corner_count) {
    if (!tangent_error.empty()) {
      warnings_.push_back("Tangent computation failed for mesh '" +
                          mesh->prim_path + "': " + tangent_error);
    }
    return false;
  }

  std::vector<float> fv_out(tri_corner_count * 4, 0.0f);
  for (size_t i = 0; i < tri_corner_count; ++i) {
    const value::float3& n = fv_normals[i];
    const value::float3& t = fv_tangents[i];
    const value::float3& b = fv_binormals[i];
    const float cx = n[1] * t[2] - n[2] * t[1];
    const float cy = n[2] * t[0] - n[0] * t[2];
    const float cz = n[0] * t[1] - n[1] * t[0];
    const float sign =
        (cx * b[0] + cy * b[1] + cz * b[2]) < 0.0f ? -1.0f : 1.0f;
    fv_out[i * 4 + 0] = t[0];
    fv_out[i * 4 + 1] = t[1];
    fv_out[i * 4 + 2] = t[2];
    fv_out[i * 4 + 3] = sign;
  }

  if (vertex_normals && vertex_uvs) {
    std::vector<float> vertex_out(np * 4, 0.0f);
    std::vector<uint8_t> seen(np, 0);
    for (size_t c = 0; c < tri_corner_count; ++c) {
      const uint32_t point_id = mesh->triangulated_indices[c];
      if (point_id >= np || seen[point_id]) continue;
      seen[point_id] = 1;
      vertex_out[size_t(point_id) * 4 + 0] = fv_out[c * 4 + 0];
      vertex_out[size_t(point_id) * 4 + 1] = fv_out[c * 4 + 1];
      vertex_out[size_t(point_id) * 4 + 2] = fv_out[c * 4 + 2];
      vertex_out[size_t(point_id) * 4 + 3] = fv_out[c * 4 + 3];
    }
    mesh->tangents.clear();
    mesh->tangents.append(vertex_out.data(), vertex_out.size());
    mesh->tangents_interp = Interpolation::Vertex;
  } else {
    mesh->tangents.clear();
    mesh->tangents.append(fv_out.data(), fv_out.size());
    mesh->tangents_interp = Interpolation::FaceVarying;
  }
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
    mesh->sanitize_dropped_faces = static_cast<uint32_t>(
        mesh->face_vertex_counts.size() - counts.size());
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

bool RenderSceneConverter::ConvertPoints(const UsdPrim& prim,
                                         RenderPoints* out) {
  if (!out || !prim.IsValid() || prim.GetTypeName() != "Points") {
    last_error_ = "Invalid Points prim";
    return false;
  }

  ValueArrayRead<float> points;
  if (!ReadFloatArray(prim, "points", config_.time_code, &points) ||
      points.empty() || (points.view.size % 3) != 0) {
    last_error_ = "Invalid Points.points data";
    return false;
  }

  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();
  out->points.append(points.view.data, points.view.size);

  ValueArrayRead<float> widths;
  if (ReadFloatArray(prim, "widths", config_.time_code, &widths) &&
      !widths.empty()) {
    const size_t n = out->point_count();
    if (widths.view.size == 1 || widths.view.size == n) {
      out->widths.append(widths.view.data, widths.view.size);
    } else {
      warnings_.push_back("Points '" + out->prim_path +
                          "': ignoring widths with mismatched element count");
    }
  }

  ValueArrayRead<float> colors;
  if (ReadFloatArray(prim, "primvars:displayColor", config_.time_code,
                     &colors) &&
      !colors.empty()) {
    std::string interp_tok = "vertex";
    if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
      if (const ::tinyusdz::next::PropMeta* pm =
              spec->property_meta("primvars:displayColor")) {
        if (pm->authored & ::tinyusdz::next::PropMeta::kInterpolation) {
          interp_tok = pm->interpolation;
        }
      }
    }
    const Interpolation interp = ParsePrimvarInterp(interp_tok);
    const size_t elems = colors.view.size / 3;
    const size_t expected = (interp == Interpolation::Constant)
                                ? 1
                                : out->point_count();
    if ((colors.view.size % 3) == 0 && elems == expected) {
      out->colors.append(colors.view.data, colors.view.size);
      out->colors_interp = interp;
    } else {
      warnings_.push_back("Points '" + out->prim_path +
                          "': ignoring displayColor with mismatched element count");
    }
  }

  ComputePointBounds(out->points, &out->bbox_min, &out->bbox_max,
                     &out->has_bbox);
  return true;
}

//
// Curves conversion (BasisCurves / NurbsCurves)
//

namespace {

// Cubic blending weights for control points [P0,P1,P2,P3] at span-local
// parameter t in [0,1]. Standard uniform basis matrices.
void EvalCubicBasisWeights(CurveBasis basis, float t, float w[4]) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  switch (basis) {
    case CurveBasis::BSpline:
      w[0] = (1.0f - 3.0f * t + 3.0f * t2 - t3) / 6.0f;
      w[1] = (3.0f * t3 - 6.0f * t2 + 4.0f) / 6.0f;
      w[2] = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) / 6.0f;
      w[3] = t3 / 6.0f;
      break;
    case CurveBasis::CatmullRom:
      w[0] = 0.5f * (-t3 + 2.0f * t2 - t);
      w[1] = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
      w[2] = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
      w[3] = 0.5f * (t3 - t2);
      break;
    case CurveBasis::Bezier:
    default: {
      const float s = 1.0f - t;
      w[0] = s * s * s;
      w[1] = 3.0f * t * s * s;
      w[2] = 3.0f * t2 * s;
      w[3] = t3;
      break;
    }
  }
}

// Linear sample of a per-curve scalar channel (e.g. widths) at normalized
// curve parameter u01 in [0,1]. Periodic channels wrap so the closing point
// maps back to element 0.
float SampleChannelLinear(const float* vals, size_t count, float u01,
                          bool periodic, size_t stride = 1,
                          size_t component = 0) {
  if (!vals || count == 0) return 0.0f;
  if (count == 1) return vals[component];
  u01 = std::min(std::max(u01, 0.0f), 1.0f);
  if (periodic) {
    const float f = u01 * static_cast<float>(count);
    const size_t i = static_cast<size_t>(f) % count;
    const size_t j = (i + 1) % count;
    const float frac = f - std::floor(f);
    return vals[i * stride + component] * (1.0f - frac) +
           vals[j * stride + component] * frac;
  }
  const float f = u01 * static_cast<float>(count - 1);
  const size_t i = static_cast<size_t>(f);
  if (i >= count - 1) return vals[(count - 1) * stride + component];
  const float frac = f - static_cast<float>(i);
  return vals[i * stride + component] * (1.0f - frac) +
         vals[(i + 1) * stride + component] * frac;
}

constexpr int kMaxNurbsDegree = 9;

// NURBS curve point at parameter u via de Boor's algorithm.
// `knots` must have ncv + degree + 1 non-decreasing entries; u should lie in
// [knots[degree], knots[ncv]].
bool DeBoorEval(const float* cvs, size_t ncv, const float* knots, int degree,
                float u, float out[3]) {
  if (degree < 1 || degree > kMaxNurbsDegree ||
      ncv < static_cast<size_t>(degree) + 1) {
    return false;
  }
  const int n = static_cast<int>(ncv) - 1;
  int k = degree;
  if (u >= knots[n + 1]) {
    k = n;
  } else if (u > knots[degree]) {
    while (k < n && !(u >= knots[k] && u < knots[k + 1])) ++k;
  }
  float d[kMaxNurbsDegree + 1][3];
  for (int j = 0; j <= degree; ++j) {
    const size_t idx = static_cast<size_t>(j + k - degree);
    d[j][0] = cvs[idx * 3 + 0];
    d[j][1] = cvs[idx * 3 + 1];
    d[j][2] = cvs[idx * 3 + 2];
  }
  for (int r = 1; r <= degree; ++r) {
    for (int j = degree; j >= r; --j) {
      const float tj = knots[j + k - degree];
      const float denom = knots[j + 1 + k - r] - tj;
      float alpha = 0.0f;
      if (denom > 0.0f) {
        alpha = (u - tj) / denom;
        alpha = std::min(std::max(alpha, 0.0f), 1.0f);
      }
      d[j][0] = (1.0f - alpha) * d[j - 1][0] + alpha * d[j][0];
      d[j][1] = (1.0f - alpha) * d[j - 1][1] + alpha * d[j][1];
      d[j][2] = (1.0f - alpha) * d[j - 1][2] + alpha * d[j][2];
    }
  }
  out[0] = d[degree][0];
  out[1] = d[degree][1];
  out[2] = d[degree][2];
  return true;
}

// Read a float-ish array attribute, converting double-backed data (e.g.
// NurbsCurves knots/ranges which are double[]/double2[]).
bool ReadFloatsFlexible(const UsdPrim& prim, const char* name, double time,
                        std::vector<float>* out) {
  ValueArrayRead<float> f;
  if (ReadFloatArray(prim, name, time, &f) && !f.empty()) {
    out->assign(f.begin(), f.end());
    return true;
  }
  const Value* v = GetAttribute(prim, name);
  if (!v) return false;
  ::tinyusdz::next::ArrayScratch<double> scratch;
  ::tinyusdz::next::ArrayView<double> view;
  if (!::tinyusdz::next::GetDoubleArrayView(*v, &scratch, &view) ||
      view.empty()) {
    return false;
  }
  out->clear();
  out->reserve(view.size);
  for (size_t i = 0; i < view.size; ++i) {
    out->push_back(static_cast<float>(view[i]));
  }
  return true;
}

// Per-curve tessellation plan.
struct CurveTessPlan {
  uint32_t n = 0;            // authored control point count
  uint32_t nsegs = 0;        // cubic/NURBS spans (unused for linear)
  bool linear = false;       // passthrough as polyline (also fallback mode)
  bool periodic = false;
  bool pinned = false;       // duplicate end CVs (bspline x2 / catmullRom x1)
  int degree = 0;            // NURBS only
  size_t knot_offset = 0;    // NURBS only, into the flattened knots array
  float u0 = 0.0f;           // NURBS eval domain
  float u1 = 0.0f;
  uint32_t varying_count = 0;  // varying-interp elements owned by this curve
};

}  // namespace

bool RenderSceneConverter::ConvertCurves(const UsdPrim& prim,
                                         RenderCurves* out) {
  const std::string type_name =
      prim.IsValid() ? prim.GetTypeName() : std::string();
  if (!out || (type_name != "BasisCurves" && type_name != "NurbsCurves")) {
    last_error_ = "Invalid curves prim";
    return false;
  }
  out->is_nurbs = (type_name == "NurbsCurves");
  out->name = prim.GetName();
  out->prim_path = prim.GetPath().str();

  ValueArrayRead<int32_t> counts;
  if (!ReadIntArray(prim, "curveVertexCounts", config_.time_code, &counts) ||
      counts.empty()) {
    last_error_ = "Invalid curves.curveVertexCounts data";
    return false;
  }
  ValueArrayRead<float> points;
  if (!ReadFloatArray(prim, "points", config_.time_code, &points) ||
      points.empty() || (points.view.size % 3) != 0) {
    last_error_ = "Invalid curves.points data";
    return false;
  }

  size_t total_cp = 0;
  for (int32_t c : counts) {
    if (c <= 0) {
      last_error_ = "Non-positive curveVertexCounts entry";
      return false;
    }
    total_cp += static_cast<size_t>(c);
  }
  if (total_cp != points.view.size / 3) {
    last_error_ = "curveVertexCounts sum does not match points size";
    return false;
  }

  out->curve_vertex_counts.reserve(counts.size());
  for (int32_t c : counts) {
    out->curve_vertex_counts.push_back(static_cast<uint32_t>(c));
  }
  out->points.append(points.view.data, points.view.size);

  // type / basis / wrap tokens (BasisCurves; NurbsCurves have order/knots).
  if (out->is_nurbs) {
    out->type = CurveType::Cubic;
  } else {
    std::string tok;
    if (GetToken(prim, "type", &tok) && tok == "linear") {
      out->type = CurveType::Linear;
    }
    tok.clear();
    if (GetToken(prim, "basis", &tok) && !tok.empty() && tok != "bezier") {
      if (tok == "bspline") {
        out->basis = CurveBasis::BSpline;
      } else if (tok == "catmullRom") {
        out->basis = CurveBasis::CatmullRom;
      } else {
        warnings_.push_back("BasisCurves '" + out->prim_path +
                            "': unsupported basis '" + tok +
                            "', treating as bezier");
      }
    }
    tok.clear();
    if (GetToken(prim, "wrap", &tok)) {
      if (tok == "periodic") out->wrap = CurveWrap::Periodic;
      else if (tok == "pinned") out->wrap = CurveWrap::Pinned;
    }
  }

  // NURBS attributes.
  std::vector<int32_t> nurbs_order;
  std::vector<float> nurbs_knots;
  std::vector<float> nurbs_ranges;  // 2 floats per curve, optional
  bool nurbs_data_ok = true;
  if (out->is_nurbs) {
    nurbs_order = ReadIntArrayCopy(prim, "order", config_.time_code);
    if (!ReadFloatsFlexible(prim, "knots", config_.time_code, &nurbs_knots)) {
      warnings_.push_back("NurbsCurves '" + out->prim_path +
                          "': missing/unreadable knots; using control-polygon "
                          "passthrough");
      nurbs_data_ok = false;
    }
    ReadFloatsFlexible(prim, "ranges", config_.time_code, &nurbs_ranges);
  }

  const uint32_t segs = std::max(1u, config_.curves.tessellation_segments);
  const size_t ncurves = out->curve_vertex_counts.size();

  //
  // Build per-curve tessellation plans (validation + varying counts).
  //
  std::vector<CurveTessPlan> plans(ncurves);
  size_t knot_cursor = 0;
  for (size_t ci = 0; ci < ncurves; ++ci) {
    CurveTessPlan& plan = plans[ci];
    const uint32_t n = out->curve_vertex_counts[ci];
    plan.n = n;

    auto fall_back_linear = [&](const std::string& why) {
      plan.linear = true;
      plan.periodic = (!out->is_nurbs && out->wrap == CurveWrap::Periodic);
      plan.varying_count = n;
      warnings_.push_back("Curves '" + out->prim_path + "' curve " +
                          std::to_string(ci) + ": " + why +
                          "; using control-polygon passthrough");
    };

    if (out->is_nurbs) {
      int order = 4;
      if (nurbs_order.size() == ncurves) order = nurbs_order[ci];
      else if (nurbs_order.size() == 1) order = nurbs_order[0];
      const size_t knot_count = static_cast<size_t>(n) + static_cast<size_t>(
          order > 0 ? order : 0);
      const size_t knot_offset = knot_cursor;
      if (order >= 2 && order <= kMaxNurbsDegree + 1) {
        knot_cursor += knot_count;  // advance even if this curve falls back
      }
      if (!nurbs_data_ok) {
        plan.linear = true;
        plan.varying_count = n;
        continue;
      }
      if (order < 2 || order > kMaxNurbsDegree + 1) {
        fall_back_linear("unsupported NURBS order " + std::to_string(order));
        continue;
      }
      if (n < static_cast<uint32_t>(order)) {
        fall_back_linear("fewer control points than NURBS order");
        continue;
      }
      if (knot_offset + knot_count > nurbs_knots.size()) {
        fall_back_linear("knot vector too short");
        continue;
      }
      const float* kn = nurbs_knots.data() + knot_offset;
      bool monotonic = true;
      for (size_t i = 1; i < knot_count; ++i) {
        if (kn[i] < kn[i - 1]) {
          monotonic = false;
          break;
        }
      }
      if (!monotonic) {
        fall_back_linear("decreasing knot vector");
        continue;
      }
      const int degree = order - 1;
      float u0 = kn[degree];
      float u1 = kn[n];
      if (nurbs_ranges.size() >= (ci + 1) * 2) {
        const float r0 = nurbs_ranges[ci * 2 + 0];
        const float r1 = nurbs_ranges[ci * 2 + 1];
        if (r0 < r1) {
          u0 = std::max(u0, r0);
          u1 = std::min(u1, r1);
        }
      }
      if (!(u1 > u0)) {
        fall_back_linear("degenerate NURBS parameter range");
        continue;
      }
      plan.degree = degree;
      plan.knot_offset = knot_offset;
      plan.u0 = u0;
      plan.u1 = u1;
      plan.nsegs = n - static_cast<uint32_t>(order) + 1;
      plan.varying_count = plan.nsegs + 1;
      continue;
    }

    // BasisCurves.
    if (out->type == CurveType::Linear) {
      plan.linear = true;
      plan.periodic = (out->wrap == CurveWrap::Periodic);
      if (plan.periodic && n < 3) plan.periodic = false;
      plan.varying_count = n;
      continue;
    }

    const bool bezier = (out->basis == CurveBasis::Bezier);
    // "pinned" only applies to cubic bspline/catmullRom.
    const bool pinned = (out->wrap == CurveWrap::Pinned) && !bezier;
    const bool periodic = (out->wrap == CurveWrap::Periodic);
    if (periodic) {
      if (n < 3 || (bezier && (n % 3) != 0)) {
        fall_back_linear("invalid periodic cubic control point count");
        continue;
      }
      plan.periodic = true;
      plan.nsegs = bezier ? (n / 3) : n;
      plan.varying_count = plan.nsegs;
      continue;
    }
    if (pinned) {
      if (n < 2) {
        fall_back_linear("too few control points for pinned cubic curve");
        continue;
      }
      plan.pinned = true;
      // bspline: endpoints tripled (dup x2); catmullRom: doubled (dup x1).
      const uint32_t dup = (out->basis == CurveBasis::BSpline) ? 2u : 1u;
      plan.nsegs = (n + 2 * dup) - 3;
      plan.varying_count = plan.nsegs + 1;
      continue;
    }
    // nonperiodic
    if (n < 4 || (bezier && ((n - 4) % 3) != 0)) {
      fall_back_linear("invalid cubic control point count");
      continue;
    }
    plan.nsegs = bezier ? ((n - 4) / 3 + 1) : (n - 3);
    plan.varying_count = plan.nsegs + 1;
  }

  size_t varying_total = 0;
  for (const CurveTessPlan& plan : plans) varying_total += plan.varying_count;

  //
  // widths (classified by element count; default schema interp is vertex).
  //
  ValueArrayRead<float> widths;
  if (ReadFloatArray(prim, "widths", config_.time_code, &widths) &&
      !widths.empty()) {
    const size_t m = widths.view.size;
    if (m == 1) {
      out->widths.append(widths.view.data, m);
      out->widths_interp = Interpolation::Constant;
    } else if (m == total_cp) {
      out->widths.append(widths.view.data, m);
      out->widths_interp = Interpolation::Vertex;
    } else if (m == varying_total) {
      out->widths.append(widths.view.data, m);
      out->widths_interp = Interpolation::Varying;
    } else {
      warnings_.push_back("Curves '" + out->prim_path +
                          "': ignoring widths with mismatched element count");
    }
  }

  //
  // displayColor (control data only; rgb).
  //
  ValueArrayRead<float> colors;
  if (ReadFloatArray(prim, "primvars:displayColor", config_.time_code,
                     &colors) &&
      !colors.empty() && (colors.view.size % 3) == 0) {
    std::string interp_tok = "constant";
    if (const ::tinyusdz::next::PrimSpec* spec = prim.GetPrimSpec()) {
      if (const ::tinyusdz::next::PropMeta* pm =
              spec->property_meta("primvars:displayColor")) {
        if (pm->authored & ::tinyusdz::next::PropMeta::kInterpolation) {
          interp_tok = pm->interpolation;
        }
      }
    }
    Interpolation interp = ParsePrimvarInterp(interp_tok);
    const size_t elems = colors.view.size / 3;
    auto expected = [&](Interpolation it) -> size_t {
      switch (it) {
        case Interpolation::Constant: return 1;
        case Interpolation::Uniform: return ncurves;
        case Interpolation::Varying: return varying_total;
        case Interpolation::Vertex:
        default: return total_cp;
      }
    };
    if (elems != expected(interp)) {
      // Authored interp does not match; classify by size instead.
      if (elems == 1) interp = Interpolation::Constant;
      else if (elems == total_cp) interp = Interpolation::Vertex;
      else if (elems == ncurves) interp = Interpolation::Uniform;
      else if (elems == varying_total) interp = Interpolation::Varying;
      else {
        warnings_.push_back(
            "Curves '" + out->prim_path +
            "': ignoring displayColor with mismatched element count");
        interp = Interpolation::Constant;  // expected(Constant)==1 != elems
      }
    }
    if (elems == expected(interp)) {
      out->colors.append(colors.view.data, colors.view.size);
      out->colors_interp = interp;
    }
  }

  //
  // Tessellate.
  //
  const bool emit_widths = out->has_widths() &&
                           out->widths_interp != Interpolation::Constant;
  const bool emit_colors = out->has_colors();
  size_t cp_offset = 0;
  size_t var_offset = 0;
  std::vector<float> emitted;
  std::vector<float> pinned_cvs;
  for (size_t ci = 0; ci < ncurves; ++ci) {
    const CurveTessPlan& plan = plans[ci];
    const uint32_t n = plan.n;
    const float* cv = points.view.data + cp_offset * 3;
    emitted.clear();

    if (plan.linear) {
      emitted.assign(cv, cv + static_cast<size_t>(n) * 3);
      if (plan.periodic) {
        emitted.push_back(cv[0]);
        emitted.push_back(cv[1]);
        emitted.push_back(cv[2]);
      }
    } else if (out->is_nurbs) {
      const float* kn = nurbs_knots.data() + plan.knot_offset;
      const uint32_t nsamples = plan.nsegs * segs + 1;
      emitted.reserve(static_cast<size_t>(nsamples) * 3);
      for (uint32_t k = 0; k < nsamples; ++k) {
        const float u =
            plan.u0 + (plan.u1 - plan.u0) *
                          (static_cast<float>(k) /
                           static_cast<float>(nsamples - 1));
        float p[3] = {0.0f, 0.0f, 0.0f};
        DeBoorEval(cv, n, kn, plan.degree, u, p);
        emitted.push_back(p[0]);
        emitted.push_back(p[1]);
        emitted.push_back(p[2]);
      }
    } else {
      // Cubic BasisCurves.
      const float* ecv = cv;
      uint32_t en = n;
      if (plan.pinned) {
        const uint32_t dup = (out->basis == CurveBasis::BSpline) ? 2u : 1u;
        pinned_cvs.clear();
        pinned_cvs.reserve((static_cast<size_t>(n) + 2 * dup) * 3);
        for (uint32_t d = 0; d < dup; ++d) {
          pinned_cvs.insert(pinned_cvs.end(), cv, cv + 3);
        }
        pinned_cvs.insert(pinned_cvs.end(), cv, cv + static_cast<size_t>(n) * 3);
        const float* last = cv + (static_cast<size_t>(n) - 1) * 3;
        for (uint32_t d = 0; d < dup; ++d) {
          pinned_cvs.insert(pinned_cvs.end(), last, last + 3);
        }
        ecv = pinned_cvs.data();
        en = n + 2 * dup;
      }
      const uint32_t vstep = (out->basis == CurveBasis::Bezier) ? 3u : 1u;
      emitted.reserve((static_cast<size_t>(plan.nsegs) * segs + 1) * 3);
      float w[4];
      auto eval_span = [&](uint32_t span, float t, float p[3]) {
        EvalCubicBasisWeights(out->basis, t, w);
        p[0] = p[1] = p[2] = 0.0f;
        const uint32_t base = span * vstep;
        for (uint32_t k = 0; k < 4; ++k) {
          const uint32_t idx = plan.periodic ? ((base + k) % en) : (base + k);
          p[0] += w[k] * ecv[idx * 3 + 0];
          p[1] += w[k] * ecv[idx * 3 + 1];
          p[2] += w[k] * ecv[idx * 3 + 2];
        }
      };
      for (uint32_t s = 0; s < plan.nsegs; ++s) {
        for (uint32_t j = 0; j < segs; ++j) {
          float p[3];
          eval_span(s, static_cast<float>(j) / static_cast<float>(segs), p);
          emitted.push_back(p[0]);
          emitted.push_back(p[1]);
          emitted.push_back(p[2]);
        }
      }
      if (plan.periodic) {
        // Close the loop with a copy of the first tessellated point.
        emitted.push_back(emitted[0]);
        emitted.push_back(emitted[1]);
        emitted.push_back(emitted[2]);
      } else {
        float p[3];
        eval_span(plan.nsegs - 1, 1.0f, p);
        emitted.push_back(p[0]);
        emitted.push_back(p[1]);
        emitted.push_back(p[2]);
      }
    }

    const size_t emit_count = emitted.size() / 3;
    out->tessellated_vertex_counts.push_back(
        static_cast<uint32_t>(emit_count));
    out->tessellated_points.append(emitted.data(), emitted.size());

    if (emit_widths) {
      const float* wvals = nullptr;
      size_t wcount = 0;
      if (out->widths_interp == Interpolation::Vertex) {
        wvals = widths.view.data + cp_offset;
        wcount = n;
      } else {  // Varying
        wvals = widths.view.data + var_offset;
        wcount = plan.varying_count;
      }
      for (size_t k = 0; k < emit_count; ++k) {
        const float u01 =
            emit_count > 1
                ? static_cast<float>(k) / static_cast<float>(emit_count - 1)
                : 0.0f;
        out->tessellated_widths.push_back(
            SampleChannelLinear(wvals, wcount, u01, plan.periodic));
      }
    }

    if (emit_colors) {
      const float* cvals = nullptr;
      size_t ccount = 0;
      switch (out->colors_interp) {
        case Interpolation::Constant:
          cvals = colors.view.data;
          ccount = 1;
          break;
        case Interpolation::Uniform:
          cvals = colors.view.data + ci * 3;
          ccount = 1;
          break;
        case Interpolation::Vertex:
          cvals = colors.view.data + cp_offset * 3;
          ccount = n;
          break;
        case Interpolation::Varying:
          cvals = colors.view.data + var_offset * 3;
          ccount = plan.varying_count;
          break;
        case Interpolation::FaceVarying:
          break;
      }
      if (cvals && ccount > 0) {
        for (size_t k = 0; k < emit_count; ++k) {
          const float u01 =
              emit_count > 1
                  ? static_cast<float>(k) / static_cast<float>(emit_count - 1)
                  : 0.0f;
          for (size_t component = 0; component < 3; ++component) {
            out->tessellated_colors.push_back(SampleChannelLinear(
                cvals, ccount, u01, plan.periodic, 3, component));
          }
        }
      }
    }

    cp_offset += n;
    var_offset += plan.varying_count;
  }

  ComputePointBounds(out->tessellated_points, &out->bbox_min, &out->bbox_max,
                     &out->has_bbox);
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
    // Just copy indices; corner remap is identity, one triangle per face.
    mesh->face_triangle_offsets.resize(mesh->face_vertex_counts.size() + 1);
    for (size_t f = 0; f <= mesh->face_vertex_counts.size(); ++f) {
      mesh->face_triangle_offsets[f] = static_cast<uint32_t>(f);
    }
    const size_t n = mesh->face_vertex_indices.size();
    if (WouldOverflowSizeMul(n, sizeof(uint32_t)) ||
        (n * sizeof(uint32_t)) > kMaxTempAllocBytes * 4u) {
      warnings_.push_back("Mesh '" + mesh->prim_path +
                          "' triangulated index allocation too large; skipping");
      return false;
    }
    if (!mesh->triangulated_indices.resize(n) ||
        !mesh->triangulated_face_vertex_indices.resize(n)) {
      warnings_.push_back("Out of memory triangulating mesh '" +
                          mesh->prim_path + "'");
      return false;
    }
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
  const size_t tri_corner_count = tri_count * 3;
  if (tri_count >= kMaxTriangulationCornerCount) {
    warnings_.push_back("Mesh '" + mesh->prim_path +
                        "' has too many triangulated corners (" +
                        std::to_string(tri_corner_count) +
                        "); skipping");
    return false;
  }
  if (WouldOverflowSizeMul(tri_corner_count, sizeof(uint32_t)) ||
      (tri_corner_count * sizeof(uint32_t)) > kMaxTempAllocBytes * 4u) {
    warnings_.push_back("Mesh '" + mesh->prim_path +
                        "' triangulated index allocation too large; skipping");
    return false;
  }

  if (!mesh->triangulated_indices.reserve(tri_count * 3) ||
      !mesh->triangulated_face_vertex_indices.reserve(tri_count * 3)) {
    warnings_.push_back("Out of memory triangulating mesh '" +
                        mesh->prim_path + "'");
    return false;
  }
  mesh->face_triangle_offsets.assign(mesh->face_vertex_counts.size() + 1, 0);
  size_t idx_offset = 0;
  for (size_t f = 0; f < mesh->face_vertex_counts.size(); ++f) {
    mesh->face_triangle_offsets[f] =
        static_cast<uint32_t>(mesh->triangulated_indices.size() / 3);
    const uint32_t nverts = mesh->face_vertex_counts[f];
    if (idx_offset + nverts > mesh->face_vertex_indices.size()) return false;
    const bool is_hole = std::binary_search(mesh->hole_faces.begin(),
                                            mesh->hole_faces.end(),
                                            static_cast<uint32_t>(f));
    if (nverts >= 3 && !is_hole) {
      auto emit_triangle = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (mesh->left_handed) std::swap(b, c);
        const uint32_t corners[3] = {a, b, c};
        for (uint32_t corner : corners) {
          mesh->triangulated_indices.push_back(
              mesh->face_vertex_indices[idx_offset + corner]);
          mesh->triangulated_face_vertex_indices.push_back(
              static_cast<uint32_t>(idx_offset + corner));
        }
      };

      bool used_earcut = false;
      if (config_.mesh.triangulation_method ==
              MeshConfig::TriangulationMethod::Earcut &&
          nverts > 4) {
        if (nverts > kEarcutMaxVertices) {
          // Extremely large polygons are safer with fan triangulation in this
          // converter to avoid temporary O(nverts) geometry explosions in
          // earcut allocation paths.
          used_earcut = false;
        } else {
        using Point2 = std::array<double, 2>;
        std::vector<std::vector<Point2>> polygon(1);
        polygon[0].reserve(nverts);

        // Newell normal chooses the projection plane with the largest area,
        // keeping concave and non-axis-aligned polygons stable.
        double normal[3] = {0.0, 0.0, 0.0};
        for (uint32_t i = 0; i < nverts; ++i) {
          const uint32_t ia = mesh->face_vertex_indices[idx_offset + i];
          const uint32_t ib =
              mesh->face_vertex_indices[idx_offset + ((i + 1) % nverts)];
          const size_t a = static_cast<size_t>(ia) * 3;
          const size_t b = static_cast<size_t>(ib) * 3;
          normal[0] += (mesh->points[a + 1] - mesh->points[b + 1]) *
                       (mesh->points[a + 2] + mesh->points[b + 2]);
          normal[1] += (mesh->points[a + 2] - mesh->points[b + 2]) *
                       (mesh->points[a] + mesh->points[b]);
          normal[2] += (mesh->points[a] - mesh->points[b]) *
                       (mesh->points[a + 1] + mesh->points[b + 1]);
        }
        int drop_axis = 0;
        if (std::fabs(normal[1]) > std::fabs(normal[drop_axis])) drop_axis = 1;
        if (std::fabs(normal[2]) > std::fabs(normal[drop_axis])) drop_axis = 2;
        for (uint32_t i = 0; i < nverts; ++i) {
          const uint32_t vertex = mesh->face_vertex_indices[idx_offset + i];
          const size_t p = static_cast<size_t>(vertex) * 3;
          if (drop_axis == 0) {
            polygon[0].push_back({mesh->points[p + 1], mesh->points[p + 2]});
          } else if (drop_axis == 1) {
            polygon[0].push_back({mesh->points[p], mesh->points[p + 2]});
          } else {
            polygon[0].push_back({mesh->points[p], mesh->points[p + 1]});
          }
        }
        const std::vector<uint32_t> local =
            mapbox::earcut<uint32_t>(polygon);
        if (!local.empty() && (local.size() % 3) == 0) {
          used_earcut = true;
          for (size_t i = 0; i < local.size(); i += 3) {
            // earcut emits clockwise triangles. Reverse them to the USD
            // right-handed convention; emit_triangle applies the authored
            // leftHanded correction afterwards.
            emit_triangle(local[i], local[i + 2], local[i + 1]);
          }
        } else {
          warnings_.push_back("Earcut failed for face " + std::to_string(f) +
                              " of " + mesh->prim_path +
                              "; using triangle fan fallback");
        }
        }
      }

      if (!used_earcut) {
        for (uint32_t i = 1; i < nverts - 1; ++i) {
          emit_triangle(0, i, i + 1);
        }
      }
    }
    idx_offset += nverts;
  }
  mesh->face_triangle_offsets[mesh->face_vertex_counts.size()] =
      static_cast<uint32_t>(mesh->triangulated_indices.size() / 3);

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
  if (!mesh->normals.resize(num_points * 3, 0.0f)) {
    warnings_.push_back("Out of memory computing normals for mesh '" +
                        mesh->prim_path + "'");
    return false;
  }

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
  if (file.empty()) return file;
  // A configured AssetResolver owns resolution (anchor/search paths +
  // suffix fallback); asset_base_dir doubles as the anchor directory.
  if (config_.asset_resolver) {
    const std::string anchor = config_.asset_base_dir.empty()
                                   ? std::string()
                                   : config_.asset_base_dir + "/";
    ::tinyusdz::next::ResolvedAsset resolved =
        config_.asset_resolver->Resolve(file, anchor);
    if (resolved.exists && !resolved.resolved_path.empty()) {
      return resolved.resolved_path;
    }
    return file;
  }
  if (config_.asset_base_dir.empty() || file[0] == '/' ||
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
  ExtractMaterialXConfig(prim, &out->mtlx_config);

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
            out->openpbr->opacity.value.x < 1.0f - kAlphaEpsilon ||
            out->openpbr->transmission_weight.value.x > kAlphaEpsilon) {
          // Transmissive OpenPBR (glass) needs the blend path even at
          // opacity 1 (legacy marks these Translucent).
          out->alpha_mode = RenderMaterial::AlphaMode::Blend;
        }
        found_shader = true;
      } else if (IsStandardSurfaceShaderId(shader_id)) {
        // MaterialX standard_surface maps onto OpenPBR (legacy tydra's
        // ConvertMtlxStandardSurfaceToOpenPBRSurface table).
        out->shader_type = RenderMaterial::ShaderType::OpenPBR;
        out->openpbr = std::make_unique<OpenPBRSurfaceShader>();
        ExtractStandardSurfaceAsOpenPBR(stage, child, out->openpbr.get(), scene);
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
    if (mtlx.ConvertUsdMtlxMaterial(stage, prim, &mtlx_out,
                                    /*allow_converter_delegation=*/false)) {
      if (mtlx_out.preview_surface) {
        out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
        out->preview_surface = std::move(mtlx_out.preview_surface);
        out->alpha_mode = mtlx_out.alpha_mode;
        out->alpha_cutoff = mtlx_out.alpha_cutoff;
        if (!out->mtlx_config.authored) {
          out->mtlx_config = std::move(mtlx_out.mtlx_config);
        }
        found_shader = true;
      }
    }
  }

  if (!found_shader) {
    // No convertible surface shader. This happens for materials that only
    // reference an engine source asset and author no UsdPreviewSurface — e.g.
    // Unreal Engine USD exports whose surface is `outputs:unreal:surface` ->
    // an `info:implementationSource = "sourceAsset"` shader
    // (`info:unreal:sourceAsset = @...uasset@`), as in MetaHuman face/body
    // materials — or whose surface connection doesn't resolve after
    // composition. Emit a neutral default material rather than dropping it, so
    // the mesh keeps its material binding and still renders. Mirrors the
    // legacy tydra graceful-degradation behavior.
    out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
    out->preview_surface = std::make_unique<PreviewSurfaceShader>();
    warnings_.push_back(
        "Material '" + out->prim_path +
        "' has no convertible surface shader; using a default material.");
    found_shader = true;
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

// MaterialX standard_surface -> OpenPBR field mapping (mirrors legacy
// ConvertMtlxStandardSurfaceToOpenPBRSurface). ExtractShaderParam follows
// connections, so textured inputs (ND_image chains that resolve to a file)
// come through as textures.
bool RenderSceneConverter::ExtractStandardSurfaceAsOpenPBR(
    const Stage& stage, const UsdPrim& shader_prim, OpenPBRSurfaceShader* out,
    RenderScene* scene) {
  if (!out || !::tinyusdz::next::IsShader(shader_prim)) return false;

  // Base layer
  ExtractShaderParam(stage, shader_prim, "base", &out->base_weight, scene);
  ExtractShaderParam(stage, shader_prim, "base_color", &out->base_color, scene);
  ExtractShaderParam(stage, shader_prim, "diffuse_roughness",
                     &out->base_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "metalness", &out->base_metalness,
                     scene);

  // Specular layer
  ExtractShaderParam(stage, shader_prim, "specular", &out->specular_weight,
                     scene);
  ExtractShaderParam(stage, shader_prim, "specular_color",
                     &out->specular_color, scene);
  ExtractShaderParam(stage, shader_prim, "specular_roughness",
                     &out->specular_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "specular_IOR", &out->specular_ior,
                     scene);
  ExtractShaderParam(stage, shader_prim, "specular_anisotropy",
                     &out->specular_anisotropy, scene);
  ExtractShaderParam(stage, shader_prim, "specular_rotation",
                     &out->specular_rotation, scene);

  // Transmission
  ExtractShaderParam(stage, shader_prim, "transmission",
                     &out->transmission_weight, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_color",
                     &out->transmission_color, scene);
  ExtractShaderParam(stage, shader_prim, "transmission_depth",
                     &out->transmission_depth, scene);

  // Subsurface
  ExtractShaderParam(stage, shader_prim, "subsurface",
                     &out->subsurface_weight, scene);
  ExtractShaderParam(stage, shader_prim, "subsurface_color",
                     &out->subsurface_color, scene);
  ExtractShaderParam(stage, shader_prim, "subsurface_radius",
                     &out->subsurface_radius, scene);

  // Sheen
  ExtractShaderParam(stage, shader_prim, "sheen", &out->sheen_weight, scene);
  ExtractShaderParam(stage, shader_prim, "sheen_color", &out->sheen_color,
                     scene);
  ExtractShaderParam(stage, shader_prim, "sheen_roughness",
                     &out->sheen_roughness, scene);

  // Coat
  ExtractShaderParam(stage, shader_prim, "coat", &out->coat_weight, scene);
  ExtractShaderParam(stage, shader_prim, "coat_color", &out->coat_color,
                     scene);
  ExtractShaderParam(stage, shader_prim, "coat_roughness",
                     &out->coat_roughness, scene);
  ExtractShaderParam(stage, shader_prim, "coat_IOR", &out->coat_ior, scene);

  // Emission
  ExtractShaderParam(stage, shader_prim, "emission", &out->emission_luminance,
                     scene);
  ExtractShaderParam(stage, shader_prim, "emission_color",
                     &out->emission_color, scene);

  // Geometry
  ExtractShaderParam(stage, shader_prim, "normal", &out->normal, scene);
  ExtractShaderParam(stage, shader_prim, "opacity", &out->opacity, scene);

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
      if (!nc || nc->empty()) {
        // Compute/adapter nodes (ND_convert_*, ND_mix_*, color adjust, ...)
        // carry the upstream connection on an INPUT, not on the referenced
        // output. Follow the PRIMARY data input — legacy is node-aware
        // (fg for mix, in/in1 for converts/binary ops); property order is
        // not a signal (crate flattening alphabetizes, putting `bg` before
        // `fg`), and factor/mask inputs (mix/amount/weight) must never be
        // promoted to the surface color.
        const std::vector<::tinyusdz::next::Path>* input_conn = nullptr;
        if (spec) {
          static const char* kPreferredInputs[] = {
              "inputs:in", "inputs:in1", "inputs:fg", "inputs:bg"};
          for (const char* pref : kPreferredInputs) {
            const std::vector<::tinyusdz::next::Path>* c =
                spec->connection(pref);
            if (c && !c->empty()) {
              input_conn = c;
              break;
            }
          }
          if (!input_conn) {
            auto is_factor_input = [](const std::string& n) {
              return n == "inputs:mix" || n == "inputs:amount" ||
                     n == "inputs:weight" || n == "inputs:factor" ||
                     n == "inputs:alpha" || n == "inputs:mask";
            };
            for (const std::string& prop_name : hop_prim.GetPropertyNames()) {
              if (prop_name.rfind("inputs:", 0) != 0) continue;
              if (is_factor_input(prop_name)) continue;
              const std::vector<::tinyusdz::next::Path>* c =
                  spec->connection(prop_name);
              if (c && !c->empty()) {
                input_conn = c;
                break;
              }
            }
          }
        }
        if (!input_conn) break;
        nc = input_conn;
      }
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
      texture.source_color_space = tex_data.source_color_space;
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
    case LightKind::GeometryLight: out->type = LightType::Geometry; break;
    case LightKind::PortalLight: out->type = LightType::Rect; break;
    case LightKind::PluginLight:
      out->type = LightType::Point;
      warnings_.push_back("PluginLight '" + prim.GetPath().str() +
                          "': shader registry evaluation is unsupported; "
                          "using point light fallback");
      break;
    case LightKind::LightFilter:
    case LightKind::PluginLightFilter:
      out->type = LightType::Point;
      warnings_.push_back("Light filter '" + prim.GetPath().str() +
                          "': filter evaluation is unsupported; "
                          "using inert point light fallback");
      out->intensity = 0.0f;
      break;
    default: out->type = LightType::Point; break;
  }

  // Common properties
  GetFloat3(prim, "inputs:color", &out->color.x, &out->color.y, &out->color.z);
  GetFloat(prim, "inputs:intensity", &out->intensity);
  GetFloat(prim, "inputs:exposure", &out->exposure);
  GetBool(prim, "inputs:normalize", &out->normalize);
  GetBool(prim, "inputs:enableColorTemperature",
          &out->enable_color_temperature);
  GetFloat(prim, "inputs:colorTemperature", &out->color_temperature);
  GetFloat(prim, "inputs:diffuse", &out->diffuse);
  GetFloat(prim, "inputs:specular", &out->specular);
  GetFloat(prim, "inputs:shaping:focus", &out->shaping_focus);
  GetFloat3(prim, "inputs:shaping:focusTint", &out->shaping_focus_tint.x,
            &out->shaping_focus_tint.y, &out->shaping_focus_tint.z);
  GetFloat(prim, "inputs:shaping:cone:softness",
           &out->shaping_cone_softness);
  ReadStringLikeProperty(prim, "inputs:shaping:ies:file",
                         &out->shaping_ies_file);
  GetFloat(prim, "inputs:shaping:ies:angleScale",
           &out->shaping_ies_angle_scale);
  GetBool(prim, "inputs:shaping:ies:normalize", &out->shaping_ies_normalize);
  out->light_link_targets = ReadRelationshipTargets(prim, "light:link");
  if (out->light_link_targets.empty()) {
    out->light_link_targets = ReadRelationshipTargets(prim, "collection:lightLink:includes");
  }
  out->shadow_link_targets = ReadRelationshipTargets(prim, "shadow:link");
  if (out->shadow_link_targets.empty()) {
    out->shadow_link_targets =
        ReadRelationshipTargets(prim, "collection:shadowLink:includes");
  }
  out->filter_targets = ReadRelationshipTargets(prim, "filters");
  if (out->filter_targets.empty()) {
    out->filter_targets = ReadRelationshipTargets(prim, "light:filters");
  }

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
    case LightType::Dome: {
      std::string format;
      if (GetToken(prim, "inputs:texture:format", &format)) {
        if (format == "latlong") {
          out->params.dome.texture_format =
              RenderLight::DomeTextureFormat::Latlong;
        } else if (format == "mirroredBall") {
          out->params.dome.texture_format =
              RenderLight::DomeTextureFormat::MirroredBall;
        } else if (format == "angular") {
          out->params.dome.texture_format =
              RenderLight::DomeTextureFormat::Angular;
        } else {
          out->params.dome.texture_format =
              RenderLight::DomeTextureFormat::Automatic;
        }
      }
      break;
    }
    default:
      break;
  }

  // Shadow settings (UsdLux authors `inputs:shadow:enable`; accept the
  // legacy `inputs:enableShadows` spelling too).
  if (!GetBool(prim, "inputs:shadow:enable", &out->enable_shadow)) {
    GetBool(prim, "inputs:enableShadows", &out->enable_shadow);
  }
  GetFloat3(prim, "inputs:shadow:color", &out->shadow_color.x,
            &out->shadow_color.y, &out->shadow_color.z);
  GetFloat(prim, "inputs:shadow:distance", &out->shadow_distance);
  GetFloat(prim, "inputs:shadow:falloff", &out->shadow_falloff);
  GetFloat(prim, "inputs:shadow:falloffGamma", &out->shadow_falloff_gamma);

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

  // Depth of field / exposure
  GetFloat(prim, "focusDistance", &out->focus_distance);
  GetFloat(prim, "fStop", &out->fstop);

  // Motion-blur shutter interval
  GetDouble(prim, "shutter:open", &out->shutter_open);
  GetDouble(prim, "shutter:close", &out->shutter_close);

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
  out->animation_source_path = skel.animationSource;

  // Authored-count validation: a short bindTransforms/restTransforms array
  // silently identity-fills the tail joints (visually collapsed limbs with
  // no hint why). Unauthored (empty) is fine — rest derives from bind below.
  if (!skel.bindTransforms.empty() &&
      skel.bindTransforms.size() != skel.joints.size() * 16) {
    warnings_.push_back(
        "Skeleton " + prim.GetPath().str() + " authors " +
        std::to_string(skel.bindTransforms.size() / 16) +
        " bindTransforms for " + std::to_string(skel.joints.size()) +
        " joints; missing entries use identity");
  }
  if (!skel.restTransforms.empty() &&
      skel.restTransforms.size() != skel.joints.size() * 16) {
    warnings_.push_back(
        "Skeleton " + prim.GetPath().str() + " authors " +
        std::to_string(skel.restTransforms.size() / 16) +
        " restTransforms for " + std::to_string(skel.joints.size()) +
        " joints; missing entries derive from bindTransforms");
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

  // restTransforms are optional in UsdSkel: when unauthored (or too short),
  // derive the parent-local rest pose from the world-space bindTransforms —
  // rest[i] = bind[i] * inverse(bind[parent]) (row-vector convention).
  // Leaving identity here collapses every joint onto its parent.
  if (skel.restTransforms.size() < skel.joints.size() * 16 &&
      skel.bindTransforms.size() >= skel.joints.size() * 16) {
    const size_t authored_rest = skel.restTransforms.size() / 16;
    for (size_t i = authored_rest; i < out->joints.size(); ++i) {
      SkeletonJoint& joint = out->joints[i];
      const int32_t parent = joint.parent_id;
      if (parent < 0) {
        joint.rest_transform = joint.bind_transform;
        continue;
      }
      double parent_bind[16];
      double parent_inv[16];
      for (int e = 0; e < 16; ++e) {
        parent_bind[e] =
            double(out->joints[static_cast<size_t>(parent)].bind_transform.m[e]);
      }
      if (!InvertMatrix4x4D(parent_bind, parent_inv)) {
        joint.rest_transform = joint.bind_transform;
        continue;
      }
      // rest = bind * parent_inv (row-vector: local * parent = world)
      double bind[16];
      for (int e = 0; e < 16; ++e) bind[e] = double(joint.bind_transform.m[e]);
      double rest[16];
      for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
          double sum = 0.0;
          for (int k = 0; k < 4; ++k) {
            sum += bind[r * 4 + k] * parent_inv[k * 4 + c];
          }
          rest[r * 4 + c] = sum;
        }
      }
      for (int e = 0; e < 16; ++e) {
        joint.rest_transform.m[e] = static_cast<float>(rest[e]);
      }
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

bool RenderSceneConverter::ConvertAnimation(const Stage& stage,
                                            const UsdPrim& prim,
                                            AnimationClip* out) {
  if (!out || !prim.IsValid()) return false;

  out->name = prim.GetName() + "_Anim";
  out->prim_path = prim.GetPath().str();
  out->start_time = std::numeric_limits<double>::max();
  out->end_time = -std::numeric_limits<double>::max();

  if (config_.animation.bake_value_clips) {
    std::vector<NextClipSetMetadata> clip_sets;
    std::string clip_error;
    if (ParseNextClipSets(prim, &clip_sets, &clip_error)) {
      std::map<std::string, Stage> clip_stages;
      for (const NextClipSetMetadata& clip_set : clip_sets) {
        for (const std::string& asset_path : clip_set.asset_paths) {
          if (std::find(out->clip_asset_paths.begin(),
                        out->clip_asset_paths.end(), asset_path) ==
              out->clip_asset_paths.end()) {
            out->clip_asset_paths.push_back(asset_path);
          }
          if (clip_stages.find(asset_path) != clip_stages.end()) continue;
          if (!config_.animation.clip_stage_loader) continue;
          Stage clip_stage;
          std::string warn;
          std::string err;
          if (config_.animation.clip_stage_loader(asset_path, &clip_stage,
                                                  &warn, &err)) {
            clip_stages.emplace(asset_path, std::move(clip_stage));
          } else {
            warnings_.push_back("Unable to load value clip '" + asset_path +
                                "' for " + prim.GetPath().str() +
                                (err.empty() ? std::string() : ": " + err));
          }
          if (!warn.empty()) warnings_.push_back(std::move(warn));
        }
      }

      for (const NextClipSetMetadata& clip_set : clip_sets) {
        std::set<std::string> properties;
        const std::string clip_prim_path =
            clip_set.prim_path.empty() ? prim.GetPath().str()
                                       : clip_set.prim_path;
        for (const std::string& asset_path : clip_set.asset_paths) {
          const auto stage_it = clip_stages.find(asset_path);
          if (stage_it == clip_stages.end()) continue;
          const UsdPrim clip_prim =
              stage_it->second.GetPrimAtPath(clip_prim_path);
          if (!clip_prim.IsValid()) continue;
          for (const std::string& property : clip_prim.GetPropertyNames()) {
            properties.insert(property);
          }
        }

        const std::vector<double> sample_times = ValueClipSampleTimes(
            stage, clip_set, config_.animation.max_value_clip_samples);
        for (const std::string& property : properties) {
          AnimationChannel channel;
          channel.target_path =
              IsXformAnimationProperty(property)
                  ? TargetPathForXformOp(property)
                  : AnimationChannel::TargetPath::CustomProperty;
          channel.target_prim_path = prim.GetPath().str();
          channel.property_name = property;
          channel.keyframes.reserve(sample_times.size());

          for (double stage_time : sample_times) {
            const int asset_index = ActiveClipIndex(clip_set, stage_time);
            Value value;
            bool have_value = false;
            if (asset_index >= 0 &&
                static_cast<size_t>(asset_index) <
                    clip_set.asset_paths.size()) {
              const auto stage_it = clip_stages.find(
                  clip_set.asset_paths[static_cast<size_t>(asset_index)]);
              if (stage_it != clip_stages.end()) {
                const UsdPrim clip_prim =
                    stage_it->second.GetPrimAtPath(clip_prim_path);
                if (clip_prim.IsValid() && clip_prim.HasProperty(property)) {
                  value = clip_prim.GetInterpolatedValue(
                      property, ValueClipTime(clip_set, stage_time));
                  have_value = !value.is_empty();
                }
              }
            }
            if (!have_value && prim.HasProperty(property)) {
              value = prim.GetInterpolatedValue(property, stage_time);
              have_value = !value.is_empty();
            }
            if (!have_value) continue;
            Float4 converted;
            if (!ValueToAnimationFloat4(property, value, &converted)) {
              continue;
            }
            channel.keyframes.push_back(Keyframe{stage_time, converted});
            out->start_time = std::min(out->start_time, stage_time);
            out->end_time = std::max(out->end_time, stage_time);
          }

          if (!channel.keyframes.empty()) {
            out->channels.push_back(std::move(channel));
            out->value_clip_baked = true;
          }
        }
      }
    } else if (!clip_error.empty()) {
      warnings_.push_back("Invalid value clips on " + prim.GetPath().str() +
                          ": " + clip_error);
    }
  }

  if (::tinyusdz::next::IsSkelAnimation(prim)) {
    const std::vector<std::string> joint_order =
        ReadTokenArrayProperty(prim, "joints");
    const std::vector<std::string> blend_shape_order =
        ReadTokenArrayProperty(prim, "blendShapes");

    auto append_skel_channel = [&](const char* prop_name,
                                   AnimationChannel::TargetPath target_path,
                                   uint32_t stride) {
      std::vector<double> times = prim.GetTimeSampleTimes(prop_name);
      if (times.empty()) return;
      std::sort(times.begin(), times.end());
      times.erase(std::unique(times.begin(), times.end()), times.end());

      AnimationChannel channel;
      channel.target_path = target_path;
      channel.target_prim_path = prim.GetPath().str();
      channel.property_name = prop_name;
      channel.joint_order = joint_order;
      channel.blend_shape_order = blend_shape_order;
      channel.value_stride = stride;
      channel.is_skeletal = true;

      uint32_t expected_elements = 0;
      for (double t : times) {
        ::tinyusdz::next::SkelAnimationData data;
        if (!::tinyusdz::next::GetSkelAnimationDataAtTime(stage, prim, &data,
                                                          t)) {
          continue;
        }

        const std::vector<float>* values = nullptr;
        if (target_path == AnimationChannel::TargetPath::Translation &&
            data.hasTranslations) {
          values = &data.translations;
        } else if (target_path == AnimationChannel::TargetPath::Rotation &&
                   data.hasRotations) {
          values = &data.rotations;
        } else if (target_path == AnimationChannel::TargetPath::Scale &&
                   data.hasScales) {
          values = &data.scales;
        } else if (target_path == AnimationChannel::TargetPath::Weights &&
                   data.hasBlendShapes) {
          values = &data.blendShapeWeights;
        }
        if (!values || values->empty() || ((*values).size() % stride) != 0) {
          continue;
        }

        const uint32_t element_count =
            static_cast<uint32_t>((*values).size() / stride);
        if (expected_elements == 0) {
          expected_elements = element_count;
          channel.element_count = element_count;
          channel.array_values.reserve(times.size() * values->size());
          // Width validation: blendShapeWeights samples must be as wide as
          // the declared blendShapes list, or weights drive the wrong shapes.
          if (target_path == AnimationChannel::TargetPath::Weights &&
              !blend_shape_order.empty() &&
              element_count != blend_shape_order.size()) {
            warnings_.push_back(
                "SkelAnimation " + prim.GetPath().str() + " has " +
                std::to_string(element_count) +
                " blendShapeWeights per sample for " +
                std::to_string(blend_shape_order.size()) +
                " declared blendShapes");
          }
        } else if (element_count != expected_elements) {
          warnings_.push_back("Skipping inconsistent SkelAnimation sample for " +
                              prim.GetPath().str() + "." + prop_name);
          continue;
        }

        Float4 preview;
        if (!FirstArrayElementToFloat4(*values, stride, &preview)) continue;
        channel.keyframes.push_back(Keyframe{t, preview});
        channel.array_values.insert(channel.array_values.end(),
                                    values->begin(), values->end());
        out->start_time = std::min(out->start_time, t);
        out->end_time = std::max(out->end_time, t);
      }

      if (!channel.keyframes.empty()) {
        out->channels.push_back(std::move(channel));
      }
    };

    append_skel_channel("translations",
                        AnimationChannel::TargetPath::Translation, 3);
    append_skel_channel("rotations",
                        AnimationChannel::TargetPath::Rotation, 4);
    append_skel_channel("scales",
                        AnimationChannel::TargetPath::Scale, 3);
    append_skel_channel("blendShapeWeights",
                        AnimationChannel::TargetPath::Weights, 1);

    if (!out->channels.empty()) {
      return true;
    }
  }

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
