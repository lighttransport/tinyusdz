// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Mesh utilities
// Triangulation, normals, tangents, bounds, vertex dedup, interleaved packing

#pragma once

#include "lydra.hh"

namespace lydra {

// ============================================================================
// Triangulation
// ============================================================================

// Fan triangulation: polygon soup -> triangle indices
std::vector<uint32_t> triangulate(
    Span<const uint32_t> face_vertex_indices,
    Span<const uint32_t> face_vertex_counts);

// ============================================================================
// Normal Computation
// ============================================================================

// Flat normals: one normal per vertex per triangle (expands vertex count)
// Returns index_count * 3 floats (one vec3 per output vertex)
std::vector<float> compute_flat_normals(
    Span<const float> positions,    // N*3 floats
    Span<const uint32_t> indices);  // triangle indices

// Smooth normals: area-weighted average at each vertex
// Returns vertex_count * 3 floats
std::vector<float> compute_smooth_normals(
    Span<const float> positions,    // N*3 floats
    Span<const uint32_t> indices);  // triangle indices

// ============================================================================
// Tangent Computation
// ============================================================================

// MikkTSpace-style, returns vec4 (xyz=tangent, w=handedness)
// Returns vertex_count * 4 floats
std::vector<float> compute_tangents(
    Span<const float> positions,    // N*3
    Span<const float> normals,      // N*3
    Span<const float> texcoords,    // N*2
    Span<const uint32_t> indices);  // triangle indices

// ============================================================================
// Bounds
// ============================================================================

struct AABB {
    float min[3];
    float max[3];
};

AABB compute_bounds(Span<const float> positions);  // N*3 floats

// ============================================================================
// Vertex Deduplication
// ============================================================================

struct AttributeArray {
    const float* data;
    uint32_t component_count;  // 2, 3, or 4
    uint32_t location;         // shader binding location
};

struct IndexedMesh {
    std::vector<float> vertex_data;     // deduplicated, interleaved
    std::vector<uint32_t> indices;      // index buffer
    VertexBufferLayout layout;          // describes vertex_data format
    uint32_t vertex_count;
    uint32_t index_count;
};

// Build indexed mesh from unindexed triangle soup
Result<IndexedMesh> build_indexed_mesh(
    Span<const AttributeArray> attributes,
    uint32_t vertex_count);

// ============================================================================
// Interleaved Buffer Packing
// ============================================================================

struct InterleavedBuffer {
    std::vector<uint8_t> data;
    VertexBufferLayout layout;
};

// Pack separate attribute arrays into a single interleaved buffer
Result<InterleavedBuffer> pack_interleaved(
    Span<const AttributeArray> attributes,
    uint32_t vertex_count);

// ============================================================================
// Index Buffer Utilities
// ============================================================================

// Returns R16_UINT if max_index fits uint16, else R32_UINT
Format select_index_format(uint32_t max_index);

// Convert uint32 indices to uint16 (caller must verify max < 65536)
std::vector<uint16_t> indices_to_u16(Span<const uint32_t> indices);

}  // namespace lydra
