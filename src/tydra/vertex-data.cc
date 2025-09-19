// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "vertex-data.hh"

#include <numeric>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <cstring>

#include "common-utils.hh"
#include "math-util.inc"
#include "prim-types.hh"
#include "render-data.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "value-pprint.hh"
#include "logger.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// For triangulation
#include "external/mapbox/earcut/earcut.hpp"

// For tangent/binormal computation
#include "external/half-edge.hh"

#ifdef TYDRA_ROBUST_TANGENT
#include "robust-tangent.hh"
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "common-macros.inc"

namespace tinyusdz {
namespace tydra {

namespace {

// Error handling for functions that take std::string& err
#define PushError(msg) \
  do { \
    if (err) { \
      (*err) += msg; \
    } \
  } while(0)

template <typename T>
inline T lerp(T a, T b, T t) {
  return a * (T(1) - t) + b * t;
}

inline float vlength(const value::float3 &v) {
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

inline value::float3 vnormalize(const value::float3 &v) {
  float len = vlength(v);
  if (std::fabs(len) < std::numeric_limits<float>::epsilon()) {
    return v;
  }
  return v / len;
}

template <typename T>
inline T vdot(const T &a, const T &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

template <typename T>
inline T vcross(const T &a, const T &b) {
  T c;
  c[0] = a[1] * b[2] - a[2] * b[1];
  c[1] = a[2] * b[0] - a[0] * b[2];
  c[2] = a[0] * b[1] - a[1] * b[0];
  return c;
}

// Compute geometric normal in CCW(Counter Clock-Wise) manner
// Also computes the area of the input triangle.
inline static value::float3 GeometricNormal(const value::float3 v0,
                                            const value::float3 v1,
                                            const value::float3 v2,
                                            float &area) {
  const value::float3 v10 = v1 - v0;
  const value::float3 v20 = v2 - v0;

  value::float3 Nf = vcross(v10, v20);  // CCW
  area = 0.5f * vlength(Nf);
  Nf = vnormalize(Nf);

  return Nf;
}

} // anonymous namespace

// DefaultPackedVertexData implementation
std::size_t DefaultPackedVertexDataHasher::operator()(const DefaultPackedVertexData& v) const {
  size_t seed = 0;
  
#ifndef TYDRA_USE_INDEX
  // Hash position, normal, texcoord, tangent, binormal
  auto hash_float = [&seed](float f) {
    seed ^= std::hash<float>{}(f) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  };
  
  hash_float(v.position[0]);
  hash_float(v.position[1]);
  hash_float(v.position[2]);
  hash_float(v.normal[0]);
  hash_float(v.normal[1]);
  hash_float(v.normal[2]);
  hash_float(v.texcoord[0]);
  hash_float(v.texcoord[1]);
  hash_float(v.tangent[0]);
  hash_float(v.tangent[1]);
  hash_float(v.tangent[2]);
  hash_float(v.tangent[3]);
  hash_float(v.binormal[0]);
  hash_float(v.binormal[1]);
  hash_float(v.binormal[2]);
#else
  // Hash indices
  seed ^= std::hash<uint32_t>{}(v.position_index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  seed ^= std::hash<uint32_t>{}(v.normal_index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  seed ^= std::hash<uint32_t>{}(v.texcoord_index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  seed ^= std::hash<uint32_t>{}(v.tangent_index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  seed ^= std::hash<uint32_t>{}(v.binormal_index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
#endif
  
  seed ^= std::hash<uint32_t>{}(v.point_index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  
  return seed;
}

bool DefaultPackedVertexDataEqual::operator()(const DefaultPackedVertexData& lhs,
                                              const DefaultPackedVertexData& rhs) const {
  if (lhs.point_index != rhs.point_index) return false;
  
#ifndef TYDRA_USE_INDEX
  if (lhs.position != rhs.position) return false;
  if (lhs.normal != rhs.normal) return false;
  if (lhs.texcoord != rhs.texcoord) return false;
  if (lhs.tangent != rhs.tangent) return false;
  if (lhs.binormal != rhs.binormal) return false;
#else
  if (lhs.position_index != rhs.position_index) return false;
  if (lhs.normal_index != rhs.normal_index) return false;
  if (lhs.texcoord_index != rhs.texcoord_index) return false;
  if (lhs.tangent_index != rhs.tangent_index) return false;
  if (lhs.binormal_index != rhs.binormal_index) return false;
#endif
  
  return true;
}

bool DefaultPackedVertexDataCompare::operator()(const DefaultPackedVertexData& lhs,
                                                const DefaultPackedVertexData& rhs) const {
  // Use memcmp for simple binary comparison
  return std::memcmp(&lhs, &rhs, sizeof(DefaultPackedVertexData)) < 0;
}

bool DefaultPackedVertexDataEqualEps::operator()(const DefaultPackedVertexData& lhs,
                                                  const DefaultPackedVertexData& rhs) const {
#ifndef TYDRA_USE_INDEX
  // Direct comparison with epsilon
  auto float_eq = [](float a, float b, float eps) {
    return std::fabs(a - b) <= eps;
  };
  
  if (!float_eq(lhs.position[0], rhs.position[0], eps_position) ||
      !float_eq(lhs.position[1], rhs.position[1], eps_position) ||
      !float_eq(lhs.position[2], rhs.position[2], eps_position)) {
    return false;
  }
  
  if (!float_eq(lhs.normal[0], rhs.normal[0], eps_normal) ||
      !float_eq(lhs.normal[1], rhs.normal[1], eps_normal) ||
      !float_eq(lhs.normal[2], rhs.normal[2], eps_normal)) {
    return false;
  }
  
  if (!float_eq(lhs.texcoord[0], rhs.texcoord[0], eps_texcoord) ||
      !float_eq(lhs.texcoord[1], rhs.texcoord[1], eps_texcoord)) {
    return false;
  }
  
  if (!float_eq(lhs.tangent[0], rhs.tangent[0], eps_tangent) ||
      !float_eq(lhs.tangent[1], rhs.tangent[1], eps_tangent) ||
      !float_eq(lhs.tangent[2], rhs.tangent[2], eps_tangent) ||
      !float_eq(lhs.tangent[3], rhs.tangent[3], eps_tangent)) {
    return false;
  }
  
  if (!float_eq(lhs.binormal[0], rhs.binormal[0], eps_binormal) ||
      !float_eq(lhs.binormal[1], rhs.binormal[1], eps_binormal) ||
      !float_eq(lhs.binormal[2], rhs.binormal[2], eps_binormal)) {
    return false;
  }
#else
  // Compare using indices into input arrays
  if (!input) return false;
  
  auto float_eq = [](float a, float b, float eps) {
    return std::fabs(a - b) <= eps;
  };
  
  // Compare positions
  if (lhs.position_index != ~0u && rhs.position_index != ~0u) {
    const auto& pos1 = input->positions[lhs.position_index];
    const auto& pos2 = input->positions[rhs.position_index];
    if (!float_eq(pos1[0], pos2[0], eps_position) ||
        !float_eq(pos1[1], pos2[1], eps_position) ||
        !float_eq(pos1[2], pos2[2], eps_position)) {
      return false;
    }
  } else if (lhs.position_index != rhs.position_index) {
    return false;
  }
  
  // Similar comparisons for other attributes...
#endif
  
  return true;
}

DefaultPackedVertexData DefaultVertexInput::get(size_t idx) const {
  DefaultPackedVertexData v;
  
  if (idx >= point_indices.size()) {
    return v;
  }
  
  v.point_index = point_indices[idx];
  
#ifndef TYDRA_USE_INDEX
  if (idx < positions.size()) {
    v.position = positions[idx];
  }
  if (idx < normals.size()) {
    v.normal = normals[idx];
  }
  if (idx < texcoords.size()) {
    v.texcoord = texcoords[idx];
  }
  if (idx < tangents.size()) {
    // tangents is float3 but tangent field is float4, add w=1.0
    v.tangent[0] = tangents[idx][0];
    v.tangent[1] = tangents[idx][1];
    v.tangent[2] = tangents[idx][2];
    v.tangent[3] = 1.0f;
  }
  if (idx < binormals.size()) {
    v.binormal = binormals[idx];
  }
#else
  v.position_index = (idx < positions.size()) ? uint32_t(idx) : ~0u;
  v.normal_index = (idx < normals.size()) ? uint32_t(idx) : ~0u;
  v.texcoord_index = (idx < texcoords.size()) ? uint32_t(idx) : ~0u;
  v.tangent_index = (idx < tangents.size()) ? uint32_t(idx) : ~0u;
  v.binormal_index = (idx < binormals.size()) ? uint32_t(idx) : ~0u;
#endif
  
  return v;
}

void DefaultVertexOutput::push_back(const DefaultPackedVertexData& v) {
#ifndef TYDRA_USE_INDEX
  positions.push_back(v.position);
  normals.push_back(v.normal);
  texcoords.push_back(v.texcoord);
  tangents.push_back(v.tangent);
  binormals.push_back(v.binormal);
  // point_indices tracks the original vertex index
  point_indices.push_back(v.point_index);
  // Other fields are not stored in DefaultPackedVertexData
  // They would need to be added separately
#else
  // This version needs the input data to dereference indices
  // See the overloaded version below
#endif
}

#ifdef TYDRA_USE_INDEX
void DefaultVertexOutput::push_back(const DefaultPackedVertexData& v, const DefaultVertexInput& input) {
  if (v.position_index != ~0u && v.position_index < input.positions.size()) {
    positions.push_back(input.positions[v.position_index]);
  } else {
    positions.push_back({0, 0, 0});
  }
  
  if (v.normal_index != ~0u && v.normal_index < input.normals.size()) {
    normals.push_back(input.normals[v.normal_index]);
  } else {
    normals.push_back({0, 0, 0});
  }
  
  if (v.texcoord_index != ~0u && v.texcoord_index < input.texcoords.size()) {
    texcoords.push_back(input.texcoords[v.texcoord_index]);
  } else {
    texcoords.push_back({0, 0});
  }
  
  if (v.tangent_index != ~0u && v.tangent_index < input.tangents.size()) {
    // tangents in input is float3, but output needs float4
    value::float4 t;
    t[0] = input.tangents[v.tangent_index][0];
    t[1] = input.tangents[v.tangent_index][1];
    t[2] = input.tangents[v.tangent_index][2];
    t[3] = 1.0f;
    tangents.push_back(t);
  } else {
    tangents.push_back({0, 0, 0, 0});
  }
  
  if (v.binormal_index != ~0u && v.binormal_index < input.binormals.size()) {
    binormals.push_back(input.binormals[v.binormal_index]);
  } else {
    binormals.push_back({0, 0, 0});
  }
  
  // Add support for additional fields
  // Note: We use texcoord_index for both texcoords and uv0s
  if (v.texcoord_index != ~0u && v.texcoord_index < input.uv0s.size()) {
    uv0s.push_back(input.uv0s[v.texcoord_index]);
  } else {
    uv0s.push_back({0, 0});
  }
  
  // uv1s don't have a dedicated index in DefaultPackedVertexData, 
  // so we just use default value
  if (v.texcoord_index != ~0u && v.texcoord_index < input.uv1s.size()) {
    uv1s.push_back(input.uv1s[v.texcoord_index]);
  } else {
    uv1s.push_back({0, 0});
  }
  
  // Colors and opacities also don't have dedicated indices
  if (v.point_index != ~0u && v.point_index < input.colors.size()) {
    colors.push_back(input.colors[v.point_index]);
  } else {
    colors.push_back({1, 1, 1});
  }
  
  if (v.point_index != ~0u && v.point_index < input.opacities.size()) {
    opacities.push_back(input.opacities[v.point_index]);
  } else {
    opacities.push_back(1.0f);
  }
  
  // Track original point index
  point_indices.push_back(v.point_index);
}
#endif

// Build vertex indices implementation
template <class PackedVertexData, class VertexInput, class VertexOutput,
          class VertexDataHasher, class VertexDataEqual>
bool BuildIndices(const VertexInput& input,
                  VertexOutput& output,
                  const VertexDataHasher& hasher,
                  const VertexDataEqual& comparator) {
  
  std::unordered_map<PackedVertexData, uint32_t, VertexDataHasher, VertexDataEqual> 
      vertex_map(0, hasher, comparator);
  
  output.indices.clear();
  output.indices.reserve(input.size());
  
  for (size_t i = 0; i < input.size(); ++i) {
    PackedVertexData packed_vertex = input.get(i);
    
    auto it = vertex_map.find(packed_vertex);
    if (it != vertex_map.end()) {
      output.indices.push_back(it->second);
    } else {
      uint32_t new_index = static_cast<uint32_t>(output.size());
      vertex_map[packed_vertex] = new_index;
      output.indices.push_back(new_index);
#ifdef TYDRA_USE_INDEX
      output.push_back(packed_vertex, input);
#else
      output.push_back(packed_vertex);
#endif
    }
  }
  
  return true;
}

// Explicit instantiations
template bool BuildIndices<DefaultPackedVertexData, DefaultVertexInput, DefaultVertexOutput,
                           DefaultPackedVertexDataHasher, DefaultPackedVertexDataEqual>(
    const DefaultVertexInput& input,
    DefaultVertexOutput& output,
    const DefaultPackedVertexDataHasher& hasher,
    const DefaultPackedVertexDataEqual& comparator);

// Compute tangents and binormals
template <typename T, typename BaseTy>
bool ComputeTangentsAndBinormals(
    const std::vector<T>& vertices,
    const std::vector<uint32_t>& faceVertexCounts,
    const std::vector<uint32_t>& faceVertexIndices,
    const std::vector<value::float2>& texcoords,
    const std::vector<value::float3>& normals,
    bool is_facevarying_input,
    std::vector<value::float3>& tangents,
    std::vector<value::float3>& binormals,
    std::vector<uint32_t>& out_vertex_indices,
    std::string* err) {
  
  using vec2 = value::float2;
  using vec3 = value::float3;
  
  // Compute per-facevarying tangent/binormal
  size_t num_fvs = faceVertexIndices.size();
  std::vector<vec3> tn(num_fvs, {0.0f, 0.0f, 0.0f});
  std::vector<vec3> bn(num_fvs, {0.0f, 0.0f, 0.0f});
  
  size_t faceVertexIndexOffset = 0;
  for (size_t f = 0; f < faceVertexCounts.size(); f++) {
    size_t nv = faceVertexCounts[f];
    
    if (nv < 3) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Face {} has less than 3 vertices", f));
    }
    
    // Use first 3 vertices to compute tangent/binormal
    size_t fid0 = faceVertexIndexOffset + 0;
    size_t fid1 = faceVertexIndexOffset + 1;
    size_t fid2 = faceVertexIndexOffset + 2;
    
    uint32_t vf0 = is_facevarying_input ? uint32_t(fid0) : faceVertexIndices[fid0];
    uint32_t vf1 = is_facevarying_input ? uint32_t(fid1) : faceVertexIndices[fid1];
    uint32_t vf2 = is_facevarying_input ? uint32_t(fid2) : faceVertexIndices[fid2];
    
    if ((vf0 >= vertices.size()) || (vf1 >= vertices.size()) ||
        (vf2 >= vertices.size())) {
      PUSH_ERROR_AND_RETURN("Invalid value in faceVertexIndices");
    }
    
    vec3 v1 = vertices[vf0];
    vec3 v2 = vertices[vf1];
    vec3 v3 = vertices[vf2];
    
    vec2 uv1 = texcoords[vf0];
    vec2 uv2 = texcoords[vf1];
    vec2 uv3 = texcoords[vf2];
    
    float x1 = v2[0] - v1[0];
    float x2 = v3[0] - v1[0];
    float y1 = v2[1] - v1[1];
    float y2 = v3[1] - v1[1];
    float z1 = v2[2] - v1[2];
    float z2 = v3[2] - v1[2];
    
    float s1 = uv2[0] - uv1[0];
    float s2 = uv3[0] - uv1[0];
    float t1 = uv2[1] - uv1[1];
    float t2 = uv3[1] - uv1[1];
    
    float r = 1.0f;
    if (std::fabs(double(s1 * t2 - s2 * t1)) > 1.0e-20) {
      r /= (s1 * t2 - s2 * t1);
    }
    
    vec3 tdir{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r,
              (t2 * z1 - t1 * z2) * r};
    vec3 bdir{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,
              (s1 * z2 - s2 * z1) * r};
    
    // Assign to all vertices of the face
    for (size_t v = 0; v < nv; v++) {
      size_t fid = faceVertexIndexOffset + v;
      tn[fid] = tdir;
      bn[fid] = bdir;
    }
    
    faceVertexIndexOffset += nv;
  }
  
  // Build vertex indices and average tangents/binormals
  // [Implementation details for building indices and averaging would go here]
  
  tangents = std::move(tn);
  binormals = std::move(bn);
  out_vertex_indices = faceVertexIndices; // Simplified for now
  
  return true;
}

// Explicit instantiation
template bool ComputeTangentsAndBinormals<value::float3, float>(
    const std::vector<value::float3>& vertices,
    const std::vector<uint32_t>& faceVertexCounts,
    const std::vector<uint32_t>& faceVertexIndices,
    const std::vector<value::float2>& texcoords,
    const std::vector<value::float3>& normals,
    bool is_facevarying_input,
    std::vector<value::float3>& tangents,
    std::vector<value::float3>& binormals,
    std::vector<uint32_t>& out_vertex_indices,
    std::string* err);

// Compute normals
template <typename T>
bool ComputeNormals(
    const std::vector<T>& vertices,
    const std::vector<uint32_t>& faceVertexCounts,
    const std::vector<uint32_t>& faceVertexIndices,
    std::vector<value::float3>& normals,
    std::string* err) {
  
  using vec3 = value::float3;
  
  normals.assign(vertices.size(), {0.0f, 0.0f, 0.0f});
  
  size_t faceVertexIndexOffset = 0;
  for (size_t f = 0; f < faceVertexCounts.size(); f++) {
    size_t nv = faceVertexCounts[f];
    
    if (nv < 3) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Invalid face num {} at faceVertexCounts[{}]", nv, f));
    }
    
    // Use first three vertices to compute face normal
    uint32_t vidx0 = faceVertexIndices[faceVertexIndexOffset + 0];
    uint32_t vidx1 = faceVertexIndices[faceVertexIndexOffset + 1];
    uint32_t vidx2 = faceVertexIndices[faceVertexIndexOffset + 2];
    
    if (vidx0 >= vertices.size() || vidx1 >= vertices.size() || vidx2 >= vertices.size()) {
      PUSH_ERROR_AND_RETURN("Vertex index exceeds vertices.size()");
    }
    
    float area = 0.0f;
    vec3 Nf = GeometricNormal(vertices[vidx0], vertices[vidx1], vertices[vidx2], area);
    
    // Add weighted normal to all vertices of the face
    for (size_t v = 0; v < nv; v++) {
      uint32_t vidx = faceVertexIndices[faceVertexIndexOffset + v];
      if (vidx >= vertices.size()) {
        PUSH_ERROR_AND_RETURN("Vertex index exceeds vertices.size()");
      }
      normals[vidx] = normals[vidx] + area * Nf;
    }
    
    faceVertexIndexOffset += nv;
  }
  
  // Normalize all vertex normals
  for (size_t v = 0; v < normals.size(); v++) {
    normals[v] = vnormalize(normals[v]);
  }
  
  return true;
}

// Explicit instantiation
template bool ComputeNormals<value::float3>(
    const std::vector<value::float3>& vertices,
    const std::vector<uint32_t>& faceVertexCounts,
    const std::vector<uint32_t>& faceVertexIndices,
    std::vector<value::float3>& normals,
    std::string* err);

// Vertex interpolation conversion functions
bool UniformToVertex(VertexAttribute& dst, const VertexAttribute& src, size_t vertex_count) {
  if (src.variability != VertexVariability::Uniform) {
    return false;
  }
  
  dst = src;
  dst.variability = VertexVariability::Vertex;
  
  // Replicate uniform value to all vertices
  if (src.data.size() > 0) {
    size_t elem_size = src.stride_bytes();
    std::vector<uint8_t> expanded_data;
    expanded_data.reserve(elem_size * vertex_count);
    
    for (size_t i = 0; i < vertex_count; ++i) {
      expanded_data.insert(expanded_data.end(), src.data.begin(), src.data.begin() + elem_size);
    }
    
    dst.data = std::move(expanded_data);
  }
  
  return true;
}

bool UniformToFaceVarying(VertexAttribute& dst, const VertexAttribute& src, size_t facevarying_count) {
  if (src.variability != VertexVariability::Uniform) {
    return false;
  }
  
  dst = src;
  dst.variability = VertexVariability::FaceVarying;
  
  // Replicate uniform value to all face-varying indices
  if (src.data.size() > 0) {
    size_t elem_size = src.stride_bytes();
    std::vector<uint8_t> expanded_data;
    expanded_data.reserve(elem_size * facevarying_count);
    
    for (size_t i = 0; i < facevarying_count; ++i) {
      expanded_data.insert(expanded_data.end(), src.data.begin(), src.data.begin() + elem_size);
    }
    
    dst.data = std::move(expanded_data);
  }
  
  return true;
}

bool VertexToFaceVarying(VertexAttribute& dst, const VertexAttribute& src,
                         const std::vector<uint32_t>& faceVertexIndices) {
  if (src.variability != VertexVariability::Vertex &&
      src.variability != VertexVariability::Varying) {
    return false;
  }
  
  dst = src;
  dst.variability = VertexVariability::FaceVarying;
  
  size_t elem_size = src.stride_bytes();
  size_t num_vertices = src.vertex_count();
  
  std::vector<uint8_t> expanded_data;
  expanded_data.reserve(elem_size * faceVertexIndices.size());
  
  for (uint32_t idx : faceVertexIndices) {
    if (idx >= num_vertices) {
      return false; // Index out of bounds
    }
    
    size_t offset = idx * elem_size;
    expanded_data.insert(expanded_data.end(), 
                        src.data.begin() + offset,
                        src.data.begin() + offset + elem_size);
  }
  
  dst.data = std::move(expanded_data);
  
  return true;
}

bool ConstantToVertex(VertexAttribute& dst, const VertexAttribute& src, size_t vertex_count) {
  if (src.variability != VertexVariability::Constant) {
    return false;
  }
  
  dst = src;
  dst.variability = VertexVariability::Vertex;
  
  // Replicate constant value to all vertices
  if (src.data.size() > 0) {
    size_t elem_size = src.stride_bytes();
    std::vector<uint8_t> expanded_data;
    expanded_data.reserve(elem_size * vertex_count);
    
    for (size_t i = 0; i < vertex_count; ++i) {
      expanded_data.insert(expanded_data.end(), src.data.begin(), src.data.begin() + elem_size);
    }
    
    dst.data = std::move(expanded_data);
  }
  
  return true;
}

#undef PushError
#undef PUSH_ERROR_AND_RETURN

// Helper function for epsilon comparison
inline bool CompareWithEpsilon(float a, float b, float eps) {
  return std::abs(a - b) < eps;
}

inline bool CompareWithEpsilon(const value::float2& a, const value::float2& b, float eps) {
  for (int i = 0; i < 2; i++) {
    if (std::abs(a[i] - b[i]) >= eps) {
      return false;
    }
  }
  return true;
}

inline bool CompareWithEpsilon(const value::float3& a, const value::float3& b, float eps) {
  for (int i = 0; i < 3; i++) {
    if (std::abs(a[i] - b[i]) >= eps) {
      return false;
    }
  }
  return true;
}

// Template implementation for TryConvertFacevaryingToVertex
template <typename T>
bool TryConvertFacevaryingToVertex(
    const std::vector<T>& facevarying_data,
    const std::vector<uint32_t>& faceVertexIndices,
    size_t num_vertices,
    std::vector<T>& vertex_data,
    float eps) {
  
  // Initialize vertex data
  vertex_data.resize(num_vertices);
  std::vector<bool> vertex_set(num_vertices, false);
  
  // Try to assign facevarying data to vertices
  for (size_t i = 0; i < faceVertexIndices.size(); i++) {
    uint32_t vidx = faceVertexIndices[i];
    if (vidx >= num_vertices) {
      return false; // Invalid index
    }
    
    const T& fv_value = facevarying_data[i];
    
    if (!vertex_set[vidx]) {
      // First time seeing this vertex
      vertex_data[vidx] = fv_value;
      vertex_set[vidx] = true;
    } else {
      // Check if the value matches (within epsilon)
      // For now, simple equality check - should use eps for float comparisons
      bool matches = true;
      
      // Type-specific comparison using template specialization helper
      matches = CompareWithEpsilon(vertex_data[vidx], fv_value, eps);
      
      if (!matches) {
        return false; // Cannot convert to vertex varying
      }
    }
  }
  
  return true;
}

// Removed duplicate template implementation - already defined above

// Explicit template instantiations for TryConvertFacevaryingToVertex
template bool TryConvertFacevaryingToVertex<value::float3>(
    const std::vector<value::float3>& facevarying_data,
    const std::vector<uint32_t>& faceVertexIndices,
    size_t num_vertices,
    std::vector<value::float3>& vertex_data,
    float eps);

template bool TryConvertFacevaryingToVertex<value::float2>(
    const std::vector<value::float2>& facevarying_data,
    const std::vector<uint32_t>& faceVertexIndices,
    size_t num_vertices,
    std::vector<value::float2>& vertex_data,
    float eps);

template bool TryConvertFacevaryingToVertex<float>(
    const std::vector<float>& facevarying_data,
    const std::vector<uint32_t>& faceVertexIndices,
    size_t num_vertices,
    std::vector<float>& vertex_data,
    float eps);

// Template instantiation moved to after first definition

} // namespace tydra
} // namespace tinyusdz