/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2024 Light Transport Entertainment Inc. */
/*
 * Lydra C API — Minimal OpenGL/Vulkan-friendly data conversion
 * Prefix: lydra for functions, Lydra for types, LYDRA_ for constants
 */

#ifndef LYDRA_C_H_
#define LYDRA_C_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Result codes                                                               */
/* ========================================================================== */

typedef enum LydraResult {
    LYDRA_SUCCESS = 0,
    LYDRA_ERROR_INVALID_ARGUMENT = -1,
    LYDRA_ERROR_OUT_OF_MEMORY = -2,
    LYDRA_ERROR_BUFFER_TOO_SMALL = -3,
    LYDRA_ERROR_INTERNAL = -4,
} LydraResult;

/* ========================================================================== */
/* Format enum (matches Vulkan conventions)                                   */
/* ========================================================================== */

typedef enum LydraFormat {
    LYDRA_FORMAT_R32_SFLOAT = 0,
    LYDRA_FORMAT_R32G32_SFLOAT,
    LYDRA_FORMAT_R32G32B32_SFLOAT,
    LYDRA_FORMAT_R32G32B32A32_SFLOAT,
    LYDRA_FORMAT_R16_SFLOAT,
    LYDRA_FORMAT_R16G16_SFLOAT,
    LYDRA_FORMAT_R16G16B16_SFLOAT,
    LYDRA_FORMAT_R16G16B16A16_SFLOAT,
    LYDRA_FORMAT_R8G8B8A8_UNORM,
    LYDRA_FORMAT_R16_UINT,
    LYDRA_FORMAT_R32_UINT,
    LYDRA_FORMAT_R32_SINT,
} LydraFormat;

/* ========================================================================== */
/* Attribute descriptor (for interleaved packing)                             */
/* ========================================================================== */

typedef struct LydraAttributeDesc {
    const float* pData;
    uint32_t     componentCount;  /* 1, 2, 3, or 4 */
    uint32_t     location;        /* shader binding location */
} LydraAttributeDesc;

/* ========================================================================== */
/* Context — owns temporary allocations                                       */
/* ========================================================================== */

typedef struct LydraContext_T* LydraContext;

LydraResult lydraCreateContext(LydraContext* pCtx);
void        lydraDestroyContext(LydraContext ctx);

/* ========================================================================== */
/* Triangulation                                                              */
/* ========================================================================== */

/*
 * Two-call pattern:
 *   1st call: pOutIndices = NULL -> writes triangle count to *pOutCount
 *   2nd call: pOutIndices != NULL -> fills buffer (must be >= *pOutCount)
 */
LydraResult lydraTriangulate(
    LydraContext ctx,
    uint32_t indexCount,    const uint32_t* pIndices,
    uint32_t faceCount,     const uint32_t* pFaceVertexCounts,
    uint32_t* pOutCount,    uint32_t* pOutIndices);

/* ========================================================================== */
/* Normal computation                                                         */
/* ========================================================================== */

/* Smooth normals: vertex_count*3 floats output (caller-allocated) */
LydraResult lydraComputeSmoothNormals(
    LydraContext ctx,
    uint32_t vertexCount,  const float* pPositions,
    uint32_t indexCount,   const uint32_t* pIndices,
    float* pOutNormals);

/*
 * Flat normals: expands vertices (one normal per triangle vertex)
 * Two-call pattern:
 *   1st call: pOutNormals = NULL -> writes output vertex count to *pOutVertexCount
 *   2nd call: pOutNormals != NULL -> fills (*pOutVertexCount * 3) floats
 */
LydraResult lydraComputeFlatNormals(
    LydraContext ctx,
    uint32_t vertexCount,  const float* pPositions,
    uint32_t indexCount,   const uint32_t* pIndices,
    uint32_t* pOutVertexCount, float* pOutNormals);

/* ========================================================================== */
/* Tangent computation                                                        */
/* ========================================================================== */

/* Output: vertex_count * 4 floats (xyz=tangent, w=handedness), caller-allocated */
LydraResult lydraComputeTangents(
    LydraContext ctx,
    uint32_t vertexCount,
    const float* pPositions,
    const float* pNormals,
    const float* pTexcoords,
    uint32_t indexCount,
    const uint32_t* pIndices,
    float* pOutTangents);

/* ========================================================================== */
/* Bounds                                                                     */
/* ========================================================================== */

/* No context needed — stateless */
LydraResult lydraComputeBounds(
    uint32_t vertexCount,
    const float* pPositions,
    float outMin[3],
    float outMax[3]);

/* ========================================================================== */
/* Interleaved packing                                                        */
/* ========================================================================== */

/*
 * Two-call pattern:
 *   1st call: pOutData = NULL -> writes stride and total size
 *   2nd call: pOutData != NULL -> fills buffer
 */
LydraResult lydraPackInterleaved(
    LydraContext ctx,
    uint32_t attrCount, const LydraAttributeDesc* pAttrs,
    uint32_t vertexCount,
    uint32_t* pOutStride, uint32_t* pOutSize, void* pOutData);

/* ========================================================================== */
/* Index format selection                                                     */
/* ========================================================================== */

LydraFormat lydraSelectIndexFormat(uint32_t maxIndex);

/* ========================================================================== */
/* Transform utilities                                                        */
/* ========================================================================== */

void lydraMat4Identity(float out[16]);
void lydraMat4Multiply(const float a[16], const float b[16], float out[16]);
void lydraMat4TransformPoints(const float m[16], uint32_t count,
                               const float* pIn, float* pOut);
void lydraMat4TransformNormals(const float m[16], uint32_t count,
                                const float* pIn, float* pOut);

#ifdef __cplusplus
}
#endif

#endif /* LYDRA_C_H_ */
