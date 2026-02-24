// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Mesh utilities implementation

#include "lydra_mesh.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace lydra {

// ============================================================================
// Triangulation
// ============================================================================

std::vector<uint32_t> triangulate(
    Span<const uint32_t> face_vertex_indices,
    Span<const uint32_t> face_vertex_counts) {

    std::vector<uint32_t> triangles;

    // Estimate triangle count for reserve
    size_t estimated_tris = 0;
    for (size_t i = 0; i < face_vertex_counts.size(); i++) {
        uint32_t count = face_vertex_counts[i];
        if (count >= 3) {
            estimated_tris += count - 2;
        }
    }
    triangles.reserve(estimated_tris * 3);

    size_t index_offset = 0;
    for (size_t f = 0; f < face_vertex_counts.size(); f++) {
        uint32_t count = face_vertex_counts[f];
        if (count < 3) {
            index_offset += count;
            continue;
        }

        // Fan triangulation from first vertex
        uint32_t v0 = face_vertex_indices[index_offset];
        for (uint32_t i = 1; i < count - 1; i++) {
            uint32_t v1 = face_vertex_indices[index_offset + i];
            uint32_t v2 = face_vertex_indices[index_offset + i + 1];
            triangles.push_back(v0);
            triangles.push_back(v1);
            triangles.push_back(v2);
        }

        index_offset += count;
    }

    return triangles;
}

// ============================================================================
// Normal Computation
// ============================================================================

namespace {

struct Vec3 {
    float x, y, z;
};

Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 vec3_add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 vec3_normalize(Vec3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.0f) {
        return {v.x / len, v.y / len, v.z / len};
    }
    return {0.0f, 1.0f, 0.0f};  // Default up
}

float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 pos_at(const float* positions, uint32_t idx) {
    return {positions[idx * 3], positions[idx * 3 + 1], positions[idx * 3 + 2]};
}

}  // namespace

std::vector<float> compute_flat_normals(
    Span<const float> positions,
    Span<const uint32_t> indices) {

    if (indices.size() % 3 != 0) {
        return {};
    }

    // Output: one normal per index (3 floats per normal)
    std::vector<float> normals;
    normals.reserve(indices.size() * 3);

    for (size_t i = 0; i < indices.size(); i += 3) {
        Vec3 p0 = pos_at(positions.data(), indices[i]);
        Vec3 p1 = pos_at(positions.data(), indices[i + 1]);
        Vec3 p2 = pos_at(positions.data(), indices[i + 2]);

        Vec3 edge1 = vec3_sub(p1, p0);
        Vec3 edge2 = vec3_sub(p2, p0);
        Vec3 normal = vec3_normalize(vec3_cross(edge1, edge2));

        // Same normal for all three vertices
        for (int v = 0; v < 3; v++) {
            normals.push_back(normal.x);
            normals.push_back(normal.y);
            normals.push_back(normal.z);
        }
    }

    return normals;
}

std::vector<float> compute_smooth_normals(
    Span<const float> positions,
    Span<const uint32_t> indices) {

    if (indices.size() % 3 != 0 || positions.empty()) {
        return {};
    }

    uint32_t vertex_count = static_cast<uint32_t>(positions.size() / 3);

    // Accumulate face normals at each vertex (area-weighted)
    std::vector<float> normals(vertex_count * 3, 0.0f);

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        Vec3 p0 = pos_at(positions.data(), i0);
        Vec3 p1 = pos_at(positions.data(), i1);
        Vec3 p2 = pos_at(positions.data(), i2);

        Vec3 edge1 = vec3_sub(p1, p0);
        Vec3 edge2 = vec3_sub(p2, p0);
        Vec3 face_normal = vec3_cross(edge1, edge2);  // Not normalized = area-weighted

        normals[i0 * 3 + 0] += face_normal.x;
        normals[i0 * 3 + 1] += face_normal.y;
        normals[i0 * 3 + 2] += face_normal.z;

        normals[i1 * 3 + 0] += face_normal.x;
        normals[i1 * 3 + 1] += face_normal.y;
        normals[i1 * 3 + 2] += face_normal.z;

        normals[i2 * 3 + 0] += face_normal.x;
        normals[i2 * 3 + 1] += face_normal.y;
        normals[i2 * 3 + 2] += face_normal.z;
    }

    // Normalize accumulated normals
    for (uint32_t i = 0; i < vertex_count; i++) {
        Vec3 n = {normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]};
        n = vec3_normalize(n);
        normals[i * 3 + 0] = n.x;
        normals[i * 3 + 1] = n.y;
        normals[i * 3 + 2] = n.z;
    }

    return normals;
}

// ============================================================================
// Tangent Computation (MikkTSpace-like)
// ============================================================================

std::vector<float> compute_tangents(
    Span<const float> positions,
    Span<const float> normals,
    Span<const float> texcoords,
    Span<const uint32_t> indices) {

    if (indices.size() % 3 != 0 || positions.empty()) {
        return {};
    }

    uint32_t vertex_count = static_cast<uint32_t>(positions.size() / 3);

    if (normals.size() != vertex_count * 3 ||
        texcoords.size() != vertex_count * 2) {
        return {};
    }

    // Accumulators for tangent and bitangent directions
    std::vector<Vec3> tan1(vertex_count, {0.0f, 0.0f, 0.0f});
    std::vector<Vec3> tan2(vertex_count, {0.0f, 0.0f, 0.0f});

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        Vec3 p0 = pos_at(positions.data(), i0);
        Vec3 p1 = pos_at(positions.data(), i1);
        Vec3 p2 = pos_at(positions.data(), i2);

        float u0 = texcoords[i0 * 2], v0 = texcoords[i0 * 2 + 1];
        float u1 = texcoords[i1 * 2], v1 = texcoords[i1 * 2 + 1];
        float u2 = texcoords[i2 * 2], v2 = texcoords[i2 * 2 + 1];

        Vec3 edge1 = vec3_sub(p1, p0);
        Vec3 edge2 = vec3_sub(p2, p0);

        float du1 = u1 - u0, dv1 = v1 - v0;
        float du2 = u2 - u0, dv2 = v2 - v0;

        float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-8f) {
            continue;
        }

        float inv_det = 1.0f / det;

        Vec3 tangent = {
            (dv2 * edge1.x - dv1 * edge2.x) * inv_det,
            (dv2 * edge1.y - dv1 * edge2.y) * inv_det,
            (dv2 * edge1.z - dv1 * edge2.z) * inv_det};

        Vec3 bitangent = {
            (-du2 * edge1.x + du1 * edge2.x) * inv_det,
            (-du2 * edge1.y + du1 * edge2.y) * inv_det,
            (-du2 * edge1.z + du1 * edge2.z) * inv_det};

        tan1[i0] = vec3_add(tan1[i0], tangent);
        tan1[i1] = vec3_add(tan1[i1], tangent);
        tan1[i2] = vec3_add(tan1[i2], tangent);

        tan2[i0] = vec3_add(tan2[i0], bitangent);
        tan2[i1] = vec3_add(tan2[i1], bitangent);
        tan2[i2] = vec3_add(tan2[i2], bitangent);
    }

    // Orthonormalize and compute handedness
    std::vector<float> tangents;
    tangents.reserve(vertex_count * 4);

    for (uint32_t i = 0; i < vertex_count; i++) {
        Vec3 n = {normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]};
        Vec3 t = tan1[i];

        // Gram-Schmidt: tangent = normalize(t - n * dot(n, t))
        float dot_nt = vec3_dot(n, t);
        Vec3 ortho = {t.x - n.x * dot_nt,
                      t.y - n.y * dot_nt,
                      t.z - n.z * dot_nt};
        ortho = vec3_normalize(ortho);

        // Handedness
        Vec3 c = vec3_cross(n, t);
        float dot_cb = vec3_dot(c, tan2[i]);
        float w = (dot_cb < 0.0f) ? -1.0f : 1.0f;

        tangents.push_back(ortho.x);
        tangents.push_back(ortho.y);
        tangents.push_back(ortho.z);
        tangents.push_back(w);
    }

    return tangents;
}

// ============================================================================
// Bounds
// ============================================================================

AABB compute_bounds(Span<const float> positions) {
    AABB bounds;
    bounds.min[0] = bounds.min[1] = bounds.min[2] = std::numeric_limits<float>::max();
    bounds.max[0] = bounds.max[1] = bounds.max[2] = -std::numeric_limits<float>::max();

    uint32_t vertex_count = static_cast<uint32_t>(positions.size() / 3);
    for (uint32_t i = 0; i < vertex_count; i++) {
        float x = positions[i * 3 + 0];
        float y = positions[i * 3 + 1];
        float z = positions[i * 3 + 2];
        bounds.min[0] = std::min(bounds.min[0], x);
        bounds.min[1] = std::min(bounds.min[1], y);
        bounds.min[2] = std::min(bounds.min[2], z);
        bounds.max[0] = std::max(bounds.max[0], x);
        bounds.max[1] = std::max(bounds.max[1], y);
        bounds.max[2] = std::max(bounds.max[2], z);
    }

    return bounds;
}

// ============================================================================
// Vertex Deduplication
// ============================================================================

namespace {

// FNV-1a hash for vertex data
uint64_t fnv1a_hash(const uint8_t* data, size_t size) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; i++) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

Result<IndexedMesh> build_indexed_mesh(
    Span<const AttributeArray> attributes,
    uint32_t vertex_count) {

    if (attributes.empty() || vertex_count == 0) {
        return Result<IndexedMesh>::err("Empty input");
    }

    // Compute stride (total floats per vertex)
    uint32_t floats_per_vertex = 0;
    for (size_t i = 0; i < attributes.size(); i++) {
        floats_per_vertex += attributes[i].component_count;
    }

    uint32_t stride_bytes = floats_per_vertex * sizeof(float);

    // Build temporary interleaved buffer for hashing
    std::vector<float> temp(static_cast<size_t>(vertex_count) * floats_per_vertex);
    for (uint32_t v = 0; v < vertex_count; v++) {
        uint32_t dst = v * floats_per_vertex;
        for (size_t a = 0; a < attributes.size(); a++) {
            uint32_t cc = attributes[a].component_count;
            for (uint32_t c = 0; c < cc; c++) {
                temp[dst++] = attributes[a].data[v * cc + c];
            }
        }
    }

    // Deduplicate using hash map
    std::unordered_map<uint64_t, uint32_t> hash_to_index;
    hash_to_index.reserve(vertex_count);

    IndexedMesh result;
    result.vertex_data.reserve(vertex_count * floats_per_vertex);
    result.indices.reserve(vertex_count);

    uint32_t unique_count = 0;

    for (uint32_t v = 0; v < vertex_count; v++) {
        const uint8_t* vdata = reinterpret_cast<const uint8_t*>(&temp[v * floats_per_vertex]);
        uint64_t hash = fnv1a_hash(vdata, stride_bytes);

        auto it = hash_to_index.find(hash);
        if (it != hash_to_index.end()) {
            // Verify exact match (handle hash collisions)
            uint32_t existing = it->second;
            bool match = std::memcmp(
                &result.vertex_data[existing * floats_per_vertex],
                &temp[v * floats_per_vertex],
                stride_bytes) == 0;

            if (match) {
                result.indices.push_back(existing);
                continue;
            }
        }

        // New unique vertex
        hash_to_index[hash] = unique_count;
        result.indices.push_back(unique_count);
        for (uint32_t c = 0; c < floats_per_vertex; c++) {
            result.vertex_data.push_back(temp[v * floats_per_vertex + c]);
        }
        unique_count++;
    }

    result.vertex_count = unique_count;
    result.index_count = vertex_count;

    // Build layout
    result.layout.stride = stride_bytes;
    uint32_t offset = 0;
    for (size_t a = 0; a < attributes.size(); a++) {
        VertexAttribute attr;
        attr.location = attributes[a].location;
        attr.offset = offset;
        switch (attributes[a].component_count) {
        case 1: attr.format = Format::R32_SFLOAT; break;
        case 2: attr.format = Format::R32G32_SFLOAT; break;
        case 3: attr.format = Format::R32G32B32_SFLOAT; break;
        case 4: attr.format = Format::R32G32B32A32_SFLOAT; break;
        default: return Result<IndexedMesh>::err("Invalid component count");
        }
        offset += attributes[a].component_count * sizeof(float);
        result.layout.attributes.push_back(attr);
    }

    return Result<IndexedMesh>::ok_value(std::move(result));
}

// ============================================================================
// Interleaved Buffer Packing
// ============================================================================

Result<InterleavedBuffer> pack_interleaved(
    Span<const AttributeArray> attributes,
    uint32_t vertex_count) {

    if (attributes.empty() || vertex_count == 0) {
        return Result<InterleavedBuffer>::err("Empty input");
    }

    // Compute stride
    uint32_t floats_per_vertex = 0;
    for (size_t i = 0; i < attributes.size(); i++) {
        floats_per_vertex += attributes[i].component_count;
    }
    uint32_t stride_bytes = floats_per_vertex * sizeof(float);

    InterleavedBuffer result;
    result.data.resize(static_cast<size_t>(vertex_count) * stride_bytes);

    // Interleave
    for (uint32_t v = 0; v < vertex_count; v++) {
        uint32_t dst_float = v * floats_per_vertex;
        float* dst = reinterpret_cast<float*>(result.data.data()) + dst_float;
        for (size_t a = 0; a < attributes.size(); a++) {
            uint32_t cc = attributes[a].component_count;
            for (uint32_t c = 0; c < cc; c++) {
                *dst++ = attributes[a].data[v * cc + c];
            }
        }
    }

    // Build layout
    result.layout.stride = stride_bytes;
    uint32_t offset = 0;
    for (size_t a = 0; a < attributes.size(); a++) {
        VertexAttribute attr;
        attr.location = attributes[a].location;
        attr.offset = offset;
        switch (attributes[a].component_count) {
        case 1: attr.format = Format::R32_SFLOAT; break;
        case 2: attr.format = Format::R32G32_SFLOAT; break;
        case 3: attr.format = Format::R32G32B32_SFLOAT; break;
        case 4: attr.format = Format::R32G32B32A32_SFLOAT; break;
        default: return Result<InterleavedBuffer>::err("Invalid component count");
        }
        offset += attributes[a].component_count * sizeof(float);
        result.layout.attributes.push_back(attr);
    }

    return Result<InterleavedBuffer>::ok_value(std::move(result));
}

// ============================================================================
// Index Buffer Utilities
// ============================================================================

Format select_index_format(uint32_t max_index) {
    return (max_index <= 65535) ? Format::R16_UINT : Format::R32_UINT;
}

std::vector<uint16_t> indices_to_u16(Span<const uint32_t> indices) {
    std::vector<uint16_t> result(indices.size());
    for (size_t i = 0; i < indices.size(); i++) {
        result[i] = static_cast<uint16_t>(indices[i]);
    }
    return result;
}

}  // namespace lydra
