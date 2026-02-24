/*
 * lusd_structs.h - POD math types and info structs (all with sType/pNext)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_STRUCTS_H
#define LUSD_STRUCTS_H

#include "lusd_platform.h"
#include "lusd_enums.h"

LUSD_EXTERN_C_BEGIN

/* ===================================================================
 * POD Math Types (same memory layout as lightusd C++)
 * =================================================================== */

typedef struct LusdInt2    { int32_t x, y; }       LusdInt2;
typedef struct LusdInt3    { int32_t x, y, z; }    LusdInt3;
typedef struct LusdInt4    { int32_t x, y, z, w; } LusdInt4;

typedef struct LusdHalf2   { uint16_t x, y; }       LusdHalf2;
typedef struct LusdHalf3   { uint16_t x, y, z; }    LusdHalf3;
typedef struct LusdHalf4   { uint16_t x, y, z, w; } LusdHalf4;

typedef struct LusdFloat2  { float x, y; }       LusdFloat2;
typedef struct LusdFloat3  { float x, y, z; }    LusdFloat3;
typedef struct LusdFloat4  { float x, y, z, w; } LusdFloat4;

typedef struct LusdDouble2 { double x, y; }       LusdDouble2;
typedef struct LusdDouble3 { double x, y, z; }    LusdDouble3;
typedef struct LusdDouble4 { double x, y, z, w; } LusdDouble4;

/* Quaternions: (imaginary x,y,z, real w) */
typedef struct LusdQuath { uint16_t x, y, z, w; } LusdQuath;
typedef struct LusdQuatf { float    x, y, z, w; } LusdQuatf;
typedef struct LusdQuatd { double   x, y, z, w; } LusdQuatd;

/* Matrices: row-major */
typedef struct LusdMatrix2f { float  m[2][2]; } LusdMatrix2f;
typedef struct LusdMatrix3f { float  m[3][3]; } LusdMatrix3f;
typedef struct LusdMatrix4f { float  m[4][4]; } LusdMatrix4f;
typedef struct LusdMatrix2d { double m[2][2]; } LusdMatrix2d;
typedef struct LusdMatrix3d { double m[3][3]; } LusdMatrix3d;
typedef struct LusdMatrix4d { double m[4][4]; } LusdMatrix4d;

/* Role type aliases (same layout, distinguished by LusdValueType) */
typedef LusdHalf3   LusdColor3h;
typedef LusdFloat3  LusdColor3f;
typedef LusdDouble3 LusdColor3d;
typedef LusdHalf4   LusdColor4h;
typedef LusdFloat4  LusdColor4f;
typedef LusdDouble4 LusdColor4d;

typedef LusdHalf3   LusdPoint3h;
typedef LusdFloat3  LusdPoint3f;
typedef LusdDouble3 LusdPoint3d;

typedef LusdHalf3   LusdVector3h;
typedef LusdFloat3  LusdVector3f;
typedef LusdDouble3 LusdVector3d;

typedef LusdHalf3   LusdNormal3h;
typedef LusdFloat3  LusdNormal3f;
typedef LusdDouble3 LusdNormal3d;

typedef LusdHalf2   LusdTexCoord2h;
typedef LusdFloat2  LusdTexCoord2f;
typedef LusdDouble2 LusdTexCoord2d;

typedef LusdHalf3   LusdTexCoord3h;
typedef LusdFloat3  LusdTexCoord3f;
typedef LusdDouble3 LusdTexCoord3d;

/* ===================================================================
 * Info Structs (all have sType + pNext for extensibility)
 * =================================================================== */

typedef struct LusdInstanceCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO */
    const void*        pNext;
    uint32_t           apiVersion;
    const char*        pApplicationName;
    uint32_t           applicationVersion;
} LusdInstanceCreateInfo;

typedef struct LusdLoadOptions {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_LOAD_OPTIONS */
    const void*        pNext;
    uint32_t           maxMemoryMB;       /* 0 = unlimited */
    uint32_t           maxRecursionDepth; /* 0 = default (256) */
    uint32_t           maxArraySize;      /* 0 = default (64M elements) */
    uint32_t           maxStringLength;   /* 0 = default (16MB) */
} LusdLoadOptions;

typedef struct LusdStageLoadInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_STAGE_LOAD_INFO */
    const void*        pNext;
    const char*        pFilePath;
    LusdFormat         format;    /* LUSD_FORMAT_AUTO to detect */
    const LusdLoadOptions* pLoadOptions; /* NULL for defaults */
} LusdStageLoadInfo;

typedef struct LusdStageLoadFromMemoryInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_STAGE_LOAD_FROM_MEMORY_INFO */
    const void*        pNext;
    const void*        pData;
    uint64_t           dataSize;
    LusdFormat         format;    /* Must not be AUTO */
    const LusdLoadOptions* pLoadOptions;
} LusdStageLoadFromMemoryInfo;

typedef struct LusdStageCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_STAGE_CREATE_INFO */
    const void*        pNext;
    LusdUpAxis         upAxis;
    double             metersPerUnit;
    double             startTimeCode;
    double             endTimeCode;
    double             framesPerSecond;
} LusdStageCreateInfo;

typedef struct LusdPrimCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_PRIM_CREATE_INFO */
    const void*        pNext;
    const char*        pName;
    const char*        pTypeName;  /* e.g. "Mesh", "Xform", NULL for typeless */
    LusdSpecifier      specifier;
} LusdPrimCreateInfo;

typedef struct LusdAttributeCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_ATTRIBUTE_CREATE_INFO */
    const void*        pNext;
    const char*        pName;
    LusdValueType      valueType;
    LusdVariability    variability;
    bool               custom;
} LusdAttributeCreateInfo;

typedef struct LusdRelationshipCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_RELATIONSHIP_CREATE_INFO */
    const void*        pNext;
    const char*        pName;
    bool               custom;
} LusdRelationshipCreateInfo;

typedef struct LusdWriterCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_WRITER_CREATE_INFO */
    const void*        pNext;
    LusdFormat         format;
    const char*        pFilePath;
} LusdWriterCreateInfo;

typedef struct LusdStreamLoaderCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_STREAM_LOADER_CREATE_INFO */
    const void*        pNext;
    LusdFormat         format;
    const LusdLoadOptions* pLoadOptions;
} LusdStreamLoaderCreateInfo;

typedef struct LusdArenaCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_ARENA_CREATE_INFO */
    const void*        pNext;
    uint64_t           initialSizeBytes;
} LusdArenaCreateInfo;

typedef struct LusdLayerCreateInfo {
    LusdStructureType  sType;     /* Must be LUSD_STRUCTURE_TYPE_LAYER_CREATE_INFO */
    const void*        pNext;
    const char*        pIdentifier;
} LusdLayerCreateInfo;

/* Layer offset for composition arcs */
typedef struct LusdLayerOffset {
    double offset;
    double scale;
} LusdLayerOffset;

/* Reference descriptor */
typedef struct LusdReference {
    const char*       pAssetPath;   /* NULL for internal reference */
    const char*       pPrimPath;    /* NULL for default prim */
    LusdLayerOffset   layerOffset;
} LusdReference;

/* Payload descriptor */
typedef struct LusdPayload {
    const char*       pAssetPath;
    const char*       pPrimPath;
    LusdLayerOffset   layerOffset;
} LusdPayload;

LUSD_EXTERN_C_END

#endif /* LUSD_STRUCTS_H */
