// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "common-types.hh"
#include "typed-array.hh"
#include "value-types.hh"
#include "spatial-hashes.hh"

namespace tinyusdz {
namespace tydra {

// Forward declarations
struct RenderMesh;
struct VertexAttribute;

///
/// Default vertex data structures for building vertex indices
///

struct DefaultVertexInput;

///
/// Packed vertex data for vertex deduplication and index buffer generation.
///
/// Memory optimization:
/// - Define TYDRA_USE_INDEX to use array indices instead of values,
///   reducing memory usage by ~55% per vertex
/// - When TYDRA_USE_INDEX is defined, attributes are stored as uint32_t
///   indices into shared attribute arrays instead of duplicate values
/// - Use index value ~0u (UINT32_MAX) to indicate missing attributes
///
struct DefaultPackedVertexData {
  uint32_t point_index;  // original vertex index

#ifndef TYDRA_USE_INDEX
  value::float3 position;
  value::float3 normal;
  value::float2 texcoord;
  value::float4 tangent;
  value::float3 binormal;
#else
  // Indices into attribute arrays for memory-efficient storage
  uint32_t position_index;
  uint32_t normal_index;
  uint32_t texcoord_index;
  uint32_t tangent_index;
  uint32_t binormal_index;
#endif

  DefaultPackedVertexData()
      : point_index(~0u)
#ifndef TYDRA_USE_INDEX
      , position({0.0f, 0.0f, 0.0f})
      , normal({0.0f, 0.0f, 0.0f})
      , texcoord({0.0f, 0.0f})
      , tangent({0.0f, 0.0f, 0.0f, 0.0f})
      , binormal({0.0f, 0.0f, 0.0f})
#else
      , position_index(~0u)
      , normal_index(~0u)
      , texcoord_index(~0u)
      , tangent_index(~0u)
      , binormal_index(~0u)
#endif
  {
  }
};

///
/// Hasher for DefaultPackedVertexData for use with std::unordered_map
///
struct DefaultPackedVertexDataHasher {
  std::size_t operator()(const DefaultPackedVertexData& v) const;
};

///
/// Equality comparison for DefaultPackedVertexData
///
struct DefaultPackedVertexDataEqual {
  bool operator()(const DefaultPackedVertexData& lhs,
                  const DefaultPackedVertexData& rhs) const;
};

///
/// Comparison operator for sorting DefaultPackedVertexData
///
struct DefaultPackedVertexDataCompare {
  bool operator()(const DefaultPackedVertexData& lhs,
                  const DefaultPackedVertexData& rhs) const;
};

///
/// Epsilon-based equality comparison for DefaultPackedVertexData
/// Provides robust floating-point comparison for vertex deduplication
///
struct DefaultPackedVertexDataEqualEps {
  float eps_position = 1e-6f;
  float eps_normal = 1e-3f;
  float eps_texcoord = 1e-3f;
  float eps_tangent = 1e-3f;
  float eps_binormal = 1e-3f;

#ifdef TYDRA_USE_INDEX
  const DefaultVertexInput* input = nullptr;

  DefaultPackedVertexDataEqualEps(const DefaultVertexInput* in = nullptr)
      : input(in) {}
#endif

  bool operator()(const DefaultPackedVertexData& lhs,
                  const DefaultPackedVertexData& rhs) const;
};

///
/// Input vertex data for index buffer generation
///
struct DefaultVertexInput {
  // Vertex attributes
  std::vector<value::float3> positions;
  std::vector<value::float3> normals;
  std::vector<value::float2> texcoords;
  std::vector<value::float2> uv0s;  // Primary UVs
  std::vector<value::float2> uv1s;  // Secondary UVs
  std::vector<value::float3> tangents;  // Changed from float4 to float3 to match usage
  std::vector<value::float3> binormals;
  std::vector<value::float3> colors;
  std::vector<float> opacities;

  // Original face-varying indices
  std::vector<uint32_t> point_indices;

  size_t size() const { return point_indices.size(); }

  DefaultPackedVertexData get(size_t idx) const;
};

///
/// Output vertex data after index buffer generation
///
struct DefaultVertexOutput {
  // Deduplicated vertex attributes
  std::vector<value::float3> positions;
  std::vector<value::float3> normals;
  std::vector<value::float2> texcoords;
  std::vector<value::float2> uv0s;
  std::vector<value::float2> uv1s;
  std::vector<value::float4> tangents;
  std::vector<value::float3> binormals;
  std::vector<value::float3> colors;
  std::vector<float> opacities;

  // Index buffer
  std::vector<uint32_t> indices;
  std::vector<uint32_t> point_indices;  // Original point indices for reordering

  size_t size() const { return positions.size(); }

  void push_back(const DefaultPackedVertexData& v);
#ifdef TYDRA_USE_INDEX
  void push_back(const DefaultPackedVertexData& v, const DefaultVertexInput& input);
#endif
};

///
/// Build vertex indices from face-varying attributes
/// Creates index buffer and deduplicated vertex attributes
///
template <class PackedVertexData, class VertexInput, class VertexOutput,
          class VertexDataHasher, class VertexDataEqual>
bool BuildIndices(const VertexInput& input,
                  VertexOutput& output,
                  const VertexDataHasher& hasher = VertexDataHasher(),
                  const VertexDataEqual& comparator = VertexDataEqual());

///
/// Build vertex indices using spatial hashing for better performance
/// Uses Morton code ordering and spatial subdivision for O(1) similarity search
///
template <class PackedVertexData, class VertexInput, class VertexOutput,
          class VertexDataEqual>
bool BuildIndicesWithSpatialHash(const VertexInput& input,
                                  VertexOutput& output,
                                  const VertexDataEqual& comparator = VertexDataEqual());

///
/// Compute tangents and binormals for a mesh
///
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
    std::string* err);

///
/// Compute normals for a mesh
///
template <typename T>
bool ComputeNormals(
    const std::vector<T>& vertices,
    const std::vector<uint32_t>& faceVertexCounts,
    const std::vector<uint32_t>& faceVertexIndices,
    std::vector<value::float3>& normals,
    std::string* err);

///
/// Triangulate polygon mesh
///
template <typename T, typename BaseTy>
bool TriangulatePolygon(
    const std::vector<T>& points,
    const std::vector<uint32_t>& faceVertexCounts,
    const std::vector<uint32_t>& faceVertexIndices,
    std::vector<uint32_t>& triangulatedFaceVertexCounts,
    std::vector<uint32_t>& triangulatedFaceVertexIndices,
    std::vector<size_t>& triangulatedToOrigFaceVertexIndexMap,
    std::vector<uint32_t>& triangulatedFaceCounts,
    std::string& err);

///
/// Try to convert facevarying attribute to vertex attribute
///
template <typename T>
bool TryConvertFacevaryingToVertex(
    const std::vector<T>& facevarying_data,
    const std::vector<uint32_t>& faceVertexIndices,
    size_t num_vertices,
    std::vector<T>& vertex_data,
    float eps = 1e-6f);

///
/// Convert vertex interpolation modes
///
bool UniformToVertex(VertexAttribute& dst, const VertexAttribute& src, size_t vertex_count);
bool UniformToFaceVarying(VertexAttribute& dst, const VertexAttribute& src, size_t facevarying_count);
bool VertexToFaceVarying(VertexAttribute& dst, const VertexAttribute& src,
                         const std::vector<uint32_t>& faceVertexIndices);
bool ConstantToVertex(VertexAttribute& dst, const VertexAttribute& src, size_t vertex_count);

} // namespace tydra
} // namespace tinyusdz