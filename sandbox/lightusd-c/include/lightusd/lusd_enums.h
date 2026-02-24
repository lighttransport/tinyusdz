/*
 * lusd_enums.h - All enumeration types
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_ENUMS_H
#define LUSD_ENUMS_H

#include "lusd_platform.h"

LUSD_EXTERN_C_BEGIN

/* -------------------------------------------------------------------
 * Structure type tag for sType/pNext extensibility
 * ------------------------------------------------------------------- */
typedef enum LusdStructureType {
    LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO       = 1,
    LUSD_STRUCTURE_TYPE_STAGE_LOAD_INFO            = 2,
    LUSD_STRUCTURE_TYPE_STAGE_LOAD_FROM_MEMORY_INFO= 3,
    LUSD_STRUCTURE_TYPE_STAGE_CREATE_INFO           = 4,
    LUSD_STRUCTURE_TYPE_PRIM_CREATE_INFO            = 5,
    LUSD_STRUCTURE_TYPE_ATTRIBUTE_CREATE_INFO       = 6,
    LUSD_STRUCTURE_TYPE_RELATIONSHIP_CREATE_INFO    = 7,
    LUSD_STRUCTURE_TYPE_WRITER_CREATE_INFO          = 8,
    LUSD_STRUCTURE_TYPE_STREAM_LOADER_CREATE_INFO   = 9,
    LUSD_STRUCTURE_TYPE_ARENA_CREATE_INFO           = 10,
    LUSD_STRUCTURE_TYPE_LAYER_CREATE_INFO           = 11,
    LUSD_STRUCTURE_TYPE_LOAD_OPTIONS               = 12,
    LUSD_STRUCTURE_TYPE_MAX_ENUM                    = 0x7FFFFFFF
} LusdStructureType;

/* -------------------------------------------------------------------
 * Value type IDs (matches lightusd TypeId)
 * ------------------------------------------------------------------- */
#define LUSD_VALUE_TYPE_ARRAY_BIT 0x80000000U

typedef enum LusdValueType {
    LUSD_VALUE_TYPE_INVALID     = 0,
    LUSD_VALUE_TYPE_NULL        = 1,

    /* Scalars */
    LUSD_VALUE_TYPE_BOOL        = 2,
    LUSD_VALUE_TYPE_INT32       = 3,
    LUSD_VALUE_TYPE_UINT32      = 4,
    LUSD_VALUE_TYPE_INT64       = 5,
    LUSD_VALUE_TYPE_UINT64      = 6,
    LUSD_VALUE_TYPE_HALF        = 7,
    LUSD_VALUE_TYPE_FLOAT       = 8,
    LUSD_VALUE_TYPE_DOUBLE      = 9,

    /* String-like */
    LUSD_VALUE_TYPE_STRING      = 10,
    LUSD_VALUE_TYPE_TOKEN       = 11,
    LUSD_VALUE_TYPE_ASSET_PATH  = 12,
    LUSD_VALUE_TYPE_PATH        = 13,
    LUSD_VALUE_TYPE_TIMECODE    = 14,

    /* Integer vectors */
    LUSD_VALUE_TYPE_INT2        = 20,
    LUSD_VALUE_TYPE_INT3        = 21,
    LUSD_VALUE_TYPE_INT4        = 22,

    /* Half vectors */
    LUSD_VALUE_TYPE_HALF2       = 25,
    LUSD_VALUE_TYPE_HALF3       = 26,
    LUSD_VALUE_TYPE_HALF4       = 27,

    /* Float vectors */
    LUSD_VALUE_TYPE_FLOAT2      = 30,
    LUSD_VALUE_TYPE_FLOAT3      = 31,
    LUSD_VALUE_TYPE_FLOAT4      = 32,

    /* Double vectors */
    LUSD_VALUE_TYPE_DOUBLE2     = 35,
    LUSD_VALUE_TYPE_DOUBLE3     = 36,
    LUSD_VALUE_TYPE_DOUBLE4     = 37,

    /* Quaternions */
    LUSD_VALUE_TYPE_QUATH       = 40,
    LUSD_VALUE_TYPE_QUATF       = 41,
    LUSD_VALUE_TYPE_QUATD       = 42,

    /* Matrices (row-major) */
    LUSD_VALUE_TYPE_MATRIX2F    = 50,
    LUSD_VALUE_TYPE_MATRIX3F    = 51,
    LUSD_VALUE_TYPE_MATRIX4F    = 52,
    LUSD_VALUE_TYPE_MATRIX2D    = 55,
    LUSD_VALUE_TYPE_MATRIX3D    = 56,
    LUSD_VALUE_TYPE_MATRIX4D    = 57,

    /* Role types (semantic aliases - same storage as base type) */
    LUSD_VALUE_TYPE_COLOR3H     = 70,
    LUSD_VALUE_TYPE_COLOR3F     = 71,
    LUSD_VALUE_TYPE_COLOR3D     = 72,
    LUSD_VALUE_TYPE_COLOR4H     = 73,
    LUSD_VALUE_TYPE_COLOR4F     = 74,
    LUSD_VALUE_TYPE_COLOR4D     = 75,
    LUSD_VALUE_TYPE_POINT3H     = 80,
    LUSD_VALUE_TYPE_POINT3F     = 81,
    LUSD_VALUE_TYPE_POINT3D     = 82,
    LUSD_VALUE_TYPE_VECTOR3H    = 85,
    LUSD_VALUE_TYPE_VECTOR3F    = 86,
    LUSD_VALUE_TYPE_VECTOR3D    = 87,
    LUSD_VALUE_TYPE_NORMAL3H    = 90,
    LUSD_VALUE_TYPE_NORMAL3F    = 91,
    LUSD_VALUE_TYPE_NORMAL3D    = 92,
    LUSD_VALUE_TYPE_TEXCOORD2H  = 95,
    LUSD_VALUE_TYPE_TEXCOORD2F  = 96,
    LUSD_VALUE_TYPE_TEXCOORD2D  = 97,
    LUSD_VALUE_TYPE_TEXCOORD3H  = 100,
    LUSD_VALUE_TYPE_TEXCOORD3F  = 101,
    LUSD_VALUE_TYPE_TEXCOORD3D  = 102,

    /* Special */
    LUSD_VALUE_TYPE_DICTIONARY  = 110,
    LUSD_VALUE_TYPE_VALUE_BLOCK = 111,

    LUSD_VALUE_TYPE_MAX_ENUM    = 0x7FFFFFFF
} LusdValueType;

/* -------------------------------------------------------------------
 * Specifier
 * ------------------------------------------------------------------- */
typedef enum LusdSpecifier {
    LUSD_SPECIFIER_DEF   = 0,
    LUSD_SPECIFIER_OVER  = 1,
    LUSD_SPECIFIER_CLASS = 2,
    LUSD_SPECIFIER_MAX_ENUM = 0x7FFFFFFF
} LusdSpecifier;

/* -------------------------------------------------------------------
 * Variability
 * ------------------------------------------------------------------- */
typedef enum LusdVariability {
    LUSD_VARIABILITY_VARYING = 0,
    LUSD_VARIABILITY_UNIFORM = 1,
    LUSD_VARIABILITY_MAX_ENUM = 0x7FFFFFFF
} LusdVariability;

/* -------------------------------------------------------------------
 * Property kind
 * ------------------------------------------------------------------- */
typedef enum LusdPropertyKind {
    LUSD_PROPERTY_KIND_NONE         = 0,
    LUSD_PROPERTY_KIND_ATTRIBUTE    = 1,
    LUSD_PROPERTY_KIND_RELATIONSHIP = 2,
    LUSD_PROPERTY_KIND_MAX_ENUM     = 0x7FFFFFFF
} LusdPropertyKind;

/* -------------------------------------------------------------------
 * Up axis
 * ------------------------------------------------------------------- */
typedef enum LusdUpAxis {
    LUSD_UP_AXIS_Y = 0,
    LUSD_UP_AXIS_Z = 1,
    LUSD_UP_AXIS_X = 2,
    LUSD_UP_AXIS_MAX_ENUM = 0x7FFFFFFF
} LusdUpAxis;

/* -------------------------------------------------------------------
 * File format
 * ------------------------------------------------------------------- */
typedef enum LusdFormat {
    LUSD_FORMAT_AUTO = 0,  /* Detect from extension/magic */
    LUSD_FORMAT_USDA = 1,
    LUSD_FORMAT_USDC = 2,
    LUSD_FORMAT_USDZ = 3,
    LUSD_FORMAT_MAX_ENUM = 0x7FFFFFFF
} LusdFormat;

/* -------------------------------------------------------------------
 * List edit operation
 * ------------------------------------------------------------------- */
typedef enum LusdListEditOp {
    LUSD_LIST_EDIT_OP_EXPLICIT = 0,
    LUSD_LIST_EDIT_OP_PREPEND  = 1,
    LUSD_LIST_EDIT_OP_APPEND   = 2,
    LUSD_LIST_EDIT_OP_DELETE   = 3,
    LUSD_LIST_EDIT_OP_ADD      = 4,
    LUSD_LIST_EDIT_OP_REORDER  = 5,
    LUSD_LIST_EDIT_OP_MAX_ENUM = 0x7FFFFFFF
} LusdListEditOp;

/* -------------------------------------------------------------------
 * Composition arc type
 * ------------------------------------------------------------------- */
typedef enum LusdCompositionArcType {
    LUSD_COMPOSITION_ARC_SUBLAYER    = 0,
    LUSD_COMPOSITION_ARC_INHERIT     = 1,
    LUSD_COMPOSITION_ARC_VARIANT_SET = 2,
    LUSD_COMPOSITION_ARC_REFERENCE   = 3,
    LUSD_COMPOSITION_ARC_PAYLOAD     = 4,
    LUSD_COMPOSITION_ARC_SPECIALIZE  = 5,
    LUSD_COMPOSITION_ARC_MAX_ENUM    = 0x7FFFFFFF
} LusdCompositionArcType;

/* -------------------------------------------------------------------
 * Traversal action (returned by traversal callback)
 * ------------------------------------------------------------------- */
typedef enum LusdTraversalAction {
    LUSD_TRAVERSAL_CONTINUE = 0,
    LUSD_TRAVERSAL_SKIP     = 1,
    LUSD_TRAVERSAL_STOP     = 2
} LusdTraversalAction;

/* -------------------------------------------------------------------
 * Traversal flags
 * ------------------------------------------------------------------- */
typedef uint32_t LusdTraversalFlags;
#define LUSD_TRAVERSAL_FLAG_NONE              0x00000000U
#define LUSD_TRAVERSAL_FLAG_DEPTH_FIRST       0x00000001U
#define LUSD_TRAVERSAL_FLAG_BREADTH_FIRST     0x00000002U
#define LUSD_TRAVERSAL_FLAG_INCLUDE_INACTIVE  0x00000004U

/* -------------------------------------------------------------------
 * Diagnostic severity
 * ------------------------------------------------------------------- */
typedef enum LusdDiagnosticSeverity {
    LUSD_DIAGNOSTIC_SEVERITY_INFO    = 0,
    LUSD_DIAGNOSTIC_SEVERITY_WARNING = 1,
    LUSD_DIAGNOSTIC_SEVERITY_ERROR   = 2,
    LUSD_DIAGNOSTIC_SEVERITY_MAX_ENUM = 0x7FFFFFFF
} LusdDiagnosticSeverity;

/* -------------------------------------------------------------------
 * Interpolation type
 * ------------------------------------------------------------------- */
typedef enum LusdInterpolation {
    LUSD_INTERPOLATION_CONSTANT  = 0,
    LUSD_INTERPOLATION_UNIFORM   = 1,
    LUSD_INTERPOLATION_VARYING   = 2,
    LUSD_INTERPOLATION_VERTEX    = 3,
    LUSD_INTERPOLATION_FACE_VARYING = 4,
    LUSD_INTERPOLATION_MAX_ENUM  = 0x7FFFFFFF
} LusdInterpolation;

LUSD_EXTERN_C_END

#endif /* LUSD_ENUMS_H */
