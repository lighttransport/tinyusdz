/*
 * lusd_value_rep.h - Lazy ValueRep 8-byte type matching USDC Crate format
 *
 * Mirrors tinyusdz crate-format.hh ValueRep / CrateDataTypeId.
 * Kept as a raw uint64_t so the C layer stores zero-copy references
 * into the parsed field table; actual decoding happens in Lydra (C++).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_VALUE_REP_H
#define LUSD_VALUE_REP_H

#include "lightusd/lusd_platform.h"

/*
 * LusdValueRep — 8-byte opaque representation of a field value.
 *
 * Bit layout (matches USD crate-format.h ValueRep):
 *   bit 63:    IsArray — value is an array type
 *   bit 62:    IsInlined — value fits in the 48-bit payload (no file seek needed)
 *   bit 61:    IsCompressed — array data is LZ4-compressed in the file
 *   bits 55-48: type id (8 bits, see LusdCrateTypeId)
 *   bits 47-0:  payload (48 bits)
 *                 IsInlined=1 → small value packed directly
 *                 IsInlined=0 → byte offset from start of file to value data
 */
typedef struct { uint64_t data; } LusdValueRep;

#define LUSD_VREP_IS_ARRAY_BIT       ((uint64_t)1 << 63)
#define LUSD_VREP_IS_INLINED_BIT     ((uint64_t)1 << 62)
#define LUSD_VREP_IS_COMPRESSED_BIT  ((uint64_t)1 << 61)

#define LUSD_VREP_TYPE_SHIFT   48
#define LUSD_VREP_TYPE_MASK    (0xFFULL << LUSD_VREP_TYPE_SHIFT)
#define LUSD_VREP_PAYLOAD_MASK 0x0000FFFFFFFFFFFFull

LUSD_INLINE int      lusd_vrep_type(LusdValueRep r) { return (int)((r.data & LUSD_VREP_TYPE_MASK) >> LUSD_VREP_TYPE_SHIFT); }
LUSD_INLINE uint64_t lusd_vrep_payload(LusdValueRep r) { return r.data & LUSD_VREP_PAYLOAD_MASK; }
LUSD_INLINE bool     lusd_vrep_is_array(LusdValueRep r) { return (r.data & LUSD_VREP_IS_ARRAY_BIT) != 0; }
LUSD_INLINE bool     lusd_vrep_is_inlined(LusdValueRep r) { return (r.data & LUSD_VREP_IS_INLINED_BIT) != 0; }
LUSD_INLINE bool     lusd_vrep_is_compressed(LusdValueRep r) { return (r.data & LUSD_VREP_IS_COMPRESSED_BIT) != 0; }

/* A null ValueRep has all bits zero */
LUSD_INLINE bool lusd_vrep_is_null(LusdValueRep r) { return r.data == 0; }
static const LusdValueRep LUSD_NULL_VREP = { 0 };

/*
 * USDC CrateDataTypeId — exact values from USD crate-format.h.
 * NOTE: these must match tinyusdz/pxrUSD exactly; do NOT renumber.
 */
typedef enum LusdCrateTypeId {
    LUSD_CRATE_INVALID              = 0,
    LUSD_CRATE_BOOL                 = 1,
    LUSD_CRATE_UCHAR                = 2,
    LUSD_CRATE_INT                  = 3,
    LUSD_CRATE_UINT                 = 4,
    LUSD_CRATE_INT64                = 5,
    LUSD_CRATE_UINT64               = 6,
    LUSD_CRATE_HALF                 = 7,
    LUSD_CRATE_FLOAT                = 8,
    LUSD_CRATE_DOUBLE               = 9,
    LUSD_CRATE_STRING               = 10,
    LUSD_CRATE_TOKEN                = 11,
    LUSD_CRATE_ASSET_PATH           = 12,
    LUSD_CRATE_MATRIX2D             = 13,
    LUSD_CRATE_MATRIX3D             = 14,
    LUSD_CRATE_MATRIX4D             = 15,
    LUSD_CRATE_QUATD                = 16,
    LUSD_CRATE_QUATF                = 17,
    LUSD_CRATE_QUATH                = 18,
    LUSD_CRATE_VEC2D                = 19,
    LUSD_CRATE_VEC2F                = 20,
    LUSD_CRATE_VEC2H                = 21,
    LUSD_CRATE_VEC2I                = 22,
    LUSD_CRATE_VEC3D                = 23,
    LUSD_CRATE_VEC3F                = 24,
    LUSD_CRATE_VEC3H                = 25,
    LUSD_CRATE_VEC3I                = 26,
    LUSD_CRATE_VEC4D                = 27,
    LUSD_CRATE_VEC4F                = 28,
    LUSD_CRATE_VEC4H                = 29,
    LUSD_CRATE_VEC4I                = 30,
    LUSD_CRATE_DICTIONARY           = 31,
    LUSD_CRATE_TOKEN_LIST_OP        = 32,
    LUSD_CRATE_STRING_LIST_OP       = 33,
    LUSD_CRATE_PATH_LIST_OP         = 34,
    LUSD_CRATE_REFERENCE_LIST_OP    = 35,
    LUSD_CRATE_INT_LIST_OP          = 36,
    LUSD_CRATE_INT64_LIST_OP        = 37,
    LUSD_CRATE_UINT_LIST_OP         = 38,
    LUSD_CRATE_UINT64_LIST_OP       = 39,
    LUSD_CRATE_PATH_VECTOR          = 40,
    LUSD_CRATE_TOKEN_VECTOR         = 41,
    LUSD_CRATE_SPECIFIER            = 42,
    LUSD_CRATE_PERMISSION           = 43,
    LUSD_CRATE_VARIABILITY          = 44,
    LUSD_CRATE_VARIANT_SELECTION_MAP= 45,
    LUSD_CRATE_TIME_SAMPLES         = 46,
    LUSD_CRATE_PAYLOAD              = 47,
    LUSD_CRATE_DOUBLE_VECTOR        = 48,
    LUSD_CRATE_LAYER_OFFSET_VECTOR  = 49,
    LUSD_CRATE_STRING_VECTOR        = 50,
    LUSD_CRATE_VALUE_BLOCK          = 51,
    LUSD_CRATE_VALUE                = 52,
    LUSD_CRATE_UNREGISTERED_VALUE   = 53,
    LUSD_CRATE_UNREGISTERED_VALUE_LIST_OP = 54,
    LUSD_CRATE_PAYLOAD_LIST_OP      = 55,
    LUSD_CRATE_TIME_CODE            = 56,
} LusdCrateTypeId;

/* USD SpecType values (from pxr/usd/sdf/types.h) */
typedef enum LusdSpecType {
    LUSD_SPEC_TYPE_UNKNOWN          = 0,
    LUSD_SPEC_TYPE_ATTRIBUTE        = 1,
    LUSD_SPEC_TYPE_CONNECTION       = 2,
    LUSD_SPEC_TYPE_EXPRESSION       = 3,
    LUSD_SPEC_TYPE_MAPPER           = 4,
    LUSD_SPEC_TYPE_MAPPER_ARG       = 5,
    LUSD_SPEC_TYPE_PRIM             = 6,
    LUSD_SPEC_TYPE_PSEUDO_ROOT      = 7,
    LUSD_SPEC_TYPE_RELATIONSHIP     = 8,
    LUSD_SPEC_TYPE_RELATIONSHIP_TARGET = 9,
    LUSD_SPEC_TYPE_VARIANT          = 10,
    LUSD_SPEC_TYPE_VARIANT_SET      = 11,
} LusdSpecType;

#endif /* LUSD_VALUE_REP_H */
