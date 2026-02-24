// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra C API implementation

#include "lydra_c.h"
#include "lydra.hh"
#include "lydra_mesh.hh"
#include "lydra_transform.hh"

#include <cstring>
#include <new>
#include <vector>

// ============================================================================
// Context
// ============================================================================

struct LydraContext_T {
    // Cached results for two-call patterns
    std::vector<uint32_t> last_triangulated;
    std::vector<float> last_flat_normals;
    uint32_t last_flat_normal_vertex_count;
    std::vector<uint8_t> last_interleaved;
    uint32_t last_interleaved_stride;
};

extern "C" {

LydraResult lydraCreateContext(LydraContext* pCtx) {
    if (!pCtx) return LYDRA_ERROR_INVALID_ARGUMENT;
    try {
        *pCtx = new LydraContext_T();
        (*pCtx)->last_flat_normal_vertex_count = 0;
        (*pCtx)->last_interleaved_stride = 0;
    } catch (const std::bad_alloc&) {
        return LYDRA_ERROR_OUT_OF_MEMORY;
    }
    return LYDRA_SUCCESS;
}

void lydraDestroyContext(LydraContext ctx) {
    delete ctx;
}

// ============================================================================
// Triangulation
// ============================================================================

LydraResult lydraTriangulate(
    LydraContext ctx,
    uint32_t indexCount,    const uint32_t* pIndices,
    uint32_t faceCount,     const uint32_t* pFaceVertexCounts,
    uint32_t* pOutCount,    uint32_t* pOutIndices) {

    if (!ctx || !pIndices || !pFaceVertexCounts || !pOutCount) {
        return LYDRA_ERROR_INVALID_ARGUMENT;
    }

    if (!pOutIndices) {
        // First call: compute and cache
        lydra::Span<const uint32_t> indices(pIndices, indexCount);
        lydra::Span<const uint32_t> counts(pFaceVertexCounts, faceCount);
        ctx->last_triangulated = lydra::triangulate(indices, counts);
        *pOutCount = static_cast<uint32_t>(ctx->last_triangulated.size());
        return LYDRA_SUCCESS;
    }

    // Second call: copy cached result
    if (ctx->last_triangulated.empty()) {
        return LYDRA_ERROR_INTERNAL;
    }
    std::memcpy(pOutIndices, ctx->last_triangulated.data(),
                ctx->last_triangulated.size() * sizeof(uint32_t));
    *pOutCount = static_cast<uint32_t>(ctx->last_triangulated.size());
    return LYDRA_SUCCESS;
}

// ============================================================================
// Normal computation
// ============================================================================

LydraResult lydraComputeSmoothNormals(
    LydraContext ctx,
    uint32_t vertexCount,  const float* pPositions,
    uint32_t indexCount,   const uint32_t* pIndices,
    float* pOutNormals) {

    (void)ctx;
    if (!pPositions || !pIndices || !pOutNormals) {
        return LYDRA_ERROR_INVALID_ARGUMENT;
    }

    lydra::Span<const float> positions(pPositions, static_cast<size_t>(vertexCount) * 3);
    lydra::Span<const uint32_t> indices(pIndices, indexCount);

    std::vector<float> normals = lydra::compute_smooth_normals(positions, indices);
    if (normals.empty()) {
        return LYDRA_ERROR_INTERNAL;
    }

    std::memcpy(pOutNormals, normals.data(), normals.size() * sizeof(float));
    return LYDRA_SUCCESS;
}

LydraResult lydraComputeFlatNormals(
    LydraContext ctx,
    uint32_t vertexCount,  const float* pPositions,
    uint32_t indexCount,   const uint32_t* pIndices,
    uint32_t* pOutVertexCount, float* pOutNormals) {

    if (!ctx || !pPositions || !pIndices || !pOutVertexCount) {
        return LYDRA_ERROR_INVALID_ARGUMENT;
    }

    if (!pOutNormals) {
        // First call: compute and cache
        lydra::Span<const float> positions(pPositions, static_cast<size_t>(vertexCount) * 3);
        lydra::Span<const uint32_t> indices(pIndices, indexCount);
        ctx->last_flat_normals = lydra::compute_flat_normals(positions, indices);
        ctx->last_flat_normal_vertex_count = static_cast<uint32_t>(ctx->last_flat_normals.size() / 3);
        *pOutVertexCount = ctx->last_flat_normal_vertex_count;
        return LYDRA_SUCCESS;
    }

    // Second call: copy cached result
    if (ctx->last_flat_normals.empty()) {
        return LYDRA_ERROR_INTERNAL;
    }
    *pOutVertexCount = ctx->last_flat_normal_vertex_count;
    std::memcpy(pOutNormals, ctx->last_flat_normals.data(),
                ctx->last_flat_normals.size() * sizeof(float));
    return LYDRA_SUCCESS;
}

// ============================================================================
// Tangent computation
// ============================================================================

LydraResult lydraComputeTangents(
    LydraContext ctx,
    uint32_t vertexCount,
    const float* pPositions,
    const float* pNormals,
    const float* pTexcoords,
    uint32_t indexCount,
    const uint32_t* pIndices,
    float* pOutTangents) {

    (void)ctx;
    if (!pPositions || !pNormals || !pTexcoords || !pIndices || !pOutTangents) {
        return LYDRA_ERROR_INVALID_ARGUMENT;
    }

    lydra::Span<const float> positions(pPositions, static_cast<size_t>(vertexCount) * 3);
    lydra::Span<const float> normals(pNormals, static_cast<size_t>(vertexCount) * 3);
    lydra::Span<const float> texcoords(pTexcoords, static_cast<size_t>(vertexCount) * 2);
    lydra::Span<const uint32_t> indices(pIndices, indexCount);

    std::vector<float> tangents = lydra::compute_tangents(positions, normals, texcoords, indices);
    if (tangents.empty()) {
        return LYDRA_ERROR_INTERNAL;
    }

    std::memcpy(pOutTangents, tangents.data(), tangents.size() * sizeof(float));
    return LYDRA_SUCCESS;
}

// ============================================================================
// Bounds
// ============================================================================

LydraResult lydraComputeBounds(
    uint32_t vertexCount,
    const float* pPositions,
    float outMin[3],
    float outMax[3]) {

    if (!pPositions || !outMin || !outMax || vertexCount == 0) {
        return LYDRA_ERROR_INVALID_ARGUMENT;
    }

    lydra::Span<const float> positions(pPositions, static_cast<size_t>(vertexCount) * 3);
    lydra::AABB bounds = lydra::compute_bounds(positions);

    outMin[0] = bounds.min[0];
    outMin[1] = bounds.min[1];
    outMin[2] = bounds.min[2];
    outMax[0] = bounds.max[0];
    outMax[1] = bounds.max[1];
    outMax[2] = bounds.max[2];

    return LYDRA_SUCCESS;
}

// ============================================================================
// Interleaved packing
// ============================================================================

LydraResult lydraPackInterleaved(
    LydraContext ctx,
    uint32_t attrCount, const LydraAttributeDesc* pAttrs,
    uint32_t vertexCount,
    uint32_t* pOutStride, uint32_t* pOutSize, void* pOutData) {

    if (!ctx || !pAttrs || !pOutStride || !pOutSize || attrCount == 0 || vertexCount == 0) {
        return LYDRA_ERROR_INVALID_ARGUMENT;
    }

    if (!pOutData) {
        // First call: compute and cache
        std::vector<lydra::AttributeArray> attrs(attrCount);
        for (uint32_t i = 0; i < attrCount; i++) {
            attrs[i].data = pAttrs[i].pData;
            attrs[i].component_count = pAttrs[i].componentCount;
            attrs[i].location = pAttrs[i].location;
        }

        lydra::Span<const lydra::AttributeArray> attr_span(attrs.data(), attrs.size());
        auto result = lydra::pack_interleaved(attr_span, vertexCount);
        if (!result.ok()) {
            return LYDRA_ERROR_INTERNAL;
        }

        ctx->last_interleaved = std::move(result->data);
        ctx->last_interleaved_stride = result->layout.stride;
        *pOutStride = ctx->last_interleaved_stride;
        *pOutSize = static_cast<uint32_t>(ctx->last_interleaved.size());
        return LYDRA_SUCCESS;
    }

    // Second call: copy cached result
    if (ctx->last_interleaved.empty()) {
        return LYDRA_ERROR_INTERNAL;
    }
    *pOutStride = ctx->last_interleaved_stride;
    *pOutSize = static_cast<uint32_t>(ctx->last_interleaved.size());
    std::memcpy(pOutData, ctx->last_interleaved.data(), ctx->last_interleaved.size());
    return LYDRA_SUCCESS;
}

// ============================================================================
// Index format selection
// ============================================================================

LydraFormat lydraSelectIndexFormat(uint32_t maxIndex) {
    return (maxIndex <= 65535) ? LYDRA_FORMAT_R16_UINT : LYDRA_FORMAT_R32_UINT;
}

// ============================================================================
// Transform utilities
// ============================================================================

void lydraMat4Identity(float out[16]) {
    lydra::Mat4 m = lydra::Mat4::identity();
    std::memcpy(out, m.m, sizeof(m.m));
}

void lydraMat4Multiply(const float a[16], const float b[16], float out[16]) {
    lydra::Mat4 ma, mb;
    std::memcpy(ma.m, a, sizeof(ma.m));
    std::memcpy(mb.m, b, sizeof(mb.m));
    lydra::Mat4 result = ma * mb;
    std::memcpy(out, result.m, sizeof(result.m));
}

void lydraMat4TransformPoints(const float m[16], uint32_t count,
                               const float* pIn, float* pOut) {
    lydra::Mat4 mat;
    std::memcpy(mat.m, m, sizeof(mat.m));
    lydra::Span<const float> in_span(pIn, static_cast<size_t>(count) * 3);
    mat.transform_points(in_span, pOut, count);
}

void lydraMat4TransformNormals(const float m[16], uint32_t count,
                                const float* pIn, float* pOut) {
    lydra::Mat4 mat;
    std::memcpy(mat.m, m, sizeof(mat.m));
    lydra::Span<const float> in_span(pIn, static_cast<size_t>(count) * 3);
    mat.transform_normals(in_span, pOut, count);
}

}  // extern "C"
