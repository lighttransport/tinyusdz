/*
 * lusd_value.c - Type-erased value container (scalar types)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_value.h"
#include "internal/lusd_internal.h"
#include <string.h>

/* -------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------- */

static const LusdAllocationCallbacks* inst_alloc(struct LusdInstance_T* inst) {
    return inst->alloc.pfnAllocation ? &inst->alloc : NULL;
}

static LusdValueData* alloc_value_data(struct LusdInstance_T* inst) {
    LusdValueData* vd = (LusdValueData*)lusd_alloc(
        inst_alloc(inst), sizeof(LusdValueData), sizeof(void*));
    if (vd) memset(vd, 0, sizeof(*vd));
    return vd;
}

static LusdValue value_to_handle(LusdValueData* vd) {
    return (LusdValue)(uintptr_t)vd;
}

static LusdValueData* handle_to_value(LusdValue handle) {
    return (LusdValueData*)(uintptr_t)handle;
}

/* Store a scalar value inline */
static LusdResult create_scalar_inline(
    struct LusdInstance_T* inst,
    LusdValueType type,
    const void* data,
    size_t size,
    LusdValue* pV)
{
    if (!inst || !pV) return LUSD_ERROR_INVALID_ARGUMENT;
    if (size > LUSD_VALUE_INLINE_SIZE) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdValueData* vd = alloc_value_data(inst);
    if (!vd) return LUSD_ERROR_OUT_OF_MEMORY;

    vd->type = type;
    vd->arrayCount = 0;
    vd->useHeap = false;
    if (data && size > 0) {
        memcpy(vd->storage.inlineData, data, size);
    }

    *pV = value_to_handle(vd);
    return LUSD_SUCCESS;
}

/* Store a scalar value on heap (for types > 24 bytes) */
static LusdResult create_scalar_heap(
    struct LusdInstance_T* inst,
    LusdValueType type,
    const void* data,
    size_t size,
    LusdValue* pV)
{
    if (!inst || !pV) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdValueData* vd = alloc_value_data(inst);
    if (!vd) return LUSD_ERROR_OUT_OF_MEMORY;

    vd->type = type;
    vd->arrayCount = 0;
    vd->useHeap = true;
    vd->storage.heap.size = size;
    vd->storage.heap.ptr = lusd_alloc(inst_alloc(inst), size, sizeof(void*));
    if (!vd->storage.heap.ptr) {
        lusd_free(inst_alloc(inst), vd);
        return LUSD_ERROR_OUT_OF_MEMORY;
    }
    memcpy(vd->storage.heap.ptr, data, size);

    *pV = value_to_handle(vd);
    return LUSD_SUCCESS;
}

/* Get pointer to scalar data */
static const void* get_scalar_data(const LusdValueData* vd) {
    if (vd->useHeap) return vd->storage.heap.ptr;
    return vd->storage.inlineData;
}

/* Type check for accessor */
static LusdResult check_type(LusdValue value, LusdValueType expected, const LusdValueData** ppVd) {
    if (!value) return LUSD_ERROR_INVALID_HANDLE;
    const LusdValueData* vd = handle_to_value(value);
    if (vd->type != expected) return LUSD_ERROR_TYPE_MISMATCH;
    if (vd->arrayCount > 0) return LUSD_ERROR_TYPE_MISMATCH;
    *ppVd = vd;
    return LUSD_SUCCESS;
}

/* -------------------------------------------------------------------
 * Query
 * ------------------------------------------------------------------- */

LusdValueType lusdValueGetType(LusdValue value) {
    if (!value) return LUSD_VALUE_TYPE_INVALID;
    return handle_to_value(value)->type;
}

bool lusdValueIsArray(LusdValue value) {
    if (!value) return false;
    return ((uint32_t)handle_to_value(value)->type & LUSD_VALUE_TYPE_ARRAY_BIT) != 0;
}

uint64_t lusdValueGetArraySize(LusdValue value) {
    if (!value) return 0;
    return handle_to_value(value)->arrayCount;
}

void lusdDestroyValue(LusdInstance instance, LusdValue value) {
    if (!instance || !value) return;
    LusdValueData* vd = handle_to_value(value);

    /* Free heap data if applicable */
    if (vd->useHeap && vd->storage.heap.ptr) {
        /* For string values, the inline/heap ptr is a strdup'd string */
        lusd_free(inst_alloc(instance), vd->storage.heap.ptr);
    }
    /* For LUSD_VALUE_TYPE_STRING stored inline, the inlineData holds a char* */
    if (!vd->useHeap && vd->type == LUSD_VALUE_TYPE_STRING) {
        char* str;
        memcpy(&str, vd->storage.inlineData, sizeof(char*));
        if (str) lusd_free(inst_alloc(instance), str);
    }

    lusd_free(inst_alloc(instance), vd);
}

/* -------------------------------------------------------------------
 * Scalar Factories
 * ------------------------------------------------------------------- */

#define SCALAR_FACTORY(suffix, ctype, enumval) \
LusdResult lusdCreateValue##suffix(LusdInstance inst, ctype val, LusdValue* pV) { \
    return create_scalar_inline(inst, enumval, &val, sizeof(ctype), pV); \
}

SCALAR_FACTORY(Bool,   bool,     LUSD_VALUE_TYPE_BOOL)
SCALAR_FACTORY(Int32,  int32_t,  LUSD_VALUE_TYPE_INT32)
SCALAR_FACTORY(UInt32, uint32_t, LUSD_VALUE_TYPE_UINT32)
SCALAR_FACTORY(Int64,  int64_t,  LUSD_VALUE_TYPE_INT64)
SCALAR_FACTORY(UInt64, uint64_t, LUSD_VALUE_TYPE_UINT64)
SCALAR_FACTORY(Half,   uint16_t, LUSD_VALUE_TYPE_HALF)
SCALAR_FACTORY(Float,  float,    LUSD_VALUE_TYPE_FLOAT)
SCALAR_FACTORY(Double, double,   LUSD_VALUE_TYPE_DOUBLE)

SCALAR_FACTORY(Int2, LusdInt2, LUSD_VALUE_TYPE_INT2)
SCALAR_FACTORY(Int3, LusdInt3, LUSD_VALUE_TYPE_INT3)
SCALAR_FACTORY(Int4, LusdInt4, LUSD_VALUE_TYPE_INT4)

SCALAR_FACTORY(Half2, LusdHalf2, LUSD_VALUE_TYPE_HALF2)
SCALAR_FACTORY(Half3, LusdHalf3, LUSD_VALUE_TYPE_HALF3)
SCALAR_FACTORY(Half4, LusdHalf4, LUSD_VALUE_TYPE_HALF4)

SCALAR_FACTORY(Float2, LusdFloat2, LUSD_VALUE_TYPE_FLOAT2)
SCALAR_FACTORY(Float3, LusdFloat3, LUSD_VALUE_TYPE_FLOAT3)
SCALAR_FACTORY(Float4, LusdFloat4, LUSD_VALUE_TYPE_FLOAT4)

SCALAR_FACTORY(Double2, LusdDouble2, LUSD_VALUE_TYPE_DOUBLE2)
SCALAR_FACTORY(Double3, LusdDouble3, LUSD_VALUE_TYPE_DOUBLE3)
SCALAR_FACTORY(Double4, LusdDouble4, LUSD_VALUE_TYPE_DOUBLE4)

SCALAR_FACTORY(Quath, LusdQuath, LUSD_VALUE_TYPE_QUATH)
SCALAR_FACTORY(Quatf, LusdQuatf, LUSD_VALUE_TYPE_QUATF)
SCALAR_FACTORY(Quatd, LusdQuatd, LUSD_VALUE_TYPE_QUATD)

#undef SCALAR_FACTORY

/* String: stores a strdup'd char* inline (pointer fits in 24 bytes) */
LusdResult lusdCreateValueString(LusdInstance inst, const char* val, LusdValue* pV) {
    if (!inst || !pV) return LUSD_ERROR_INVALID_ARGUMENT;
    LusdValueData* vd = alloc_value_data(inst);
    if (!vd) return LUSD_ERROR_OUT_OF_MEMORY;

    vd->type = LUSD_VALUE_TYPE_STRING;
    vd->arrayCount = 0;
    vd->useHeap = false;
    char* dup = lusd_strdup(inst_alloc(inst), val ? val : "");
    memcpy(vd->storage.inlineData, &dup, sizeof(char*));

    *pV = value_to_handle(vd);
    return LUSD_SUCCESS;
}

/* Token: stores LusdToken handle inline */
LusdResult lusdCreateValueToken(LusdInstance inst, LusdToken val, LusdValue* pV) {
    return create_scalar_inline(inst, LUSD_VALUE_TYPE_TOKEN, &val, sizeof(LusdToken), pV);
}

/* Matrix types: larger than 24 bytes, use heap storage */
#define MATRIX_FACTORY(suffix, ctype, enumval) \
LusdResult lusdCreateValue##suffix(LusdInstance inst, const ctype* val, LusdValue* pV) { \
    if (!val) return LUSD_ERROR_INVALID_ARGUMENT; \
    return create_scalar_heap(inst, enumval, val, sizeof(ctype), pV); \
}

MATRIX_FACTORY(Matrix2f, LusdMatrix2f, LUSD_VALUE_TYPE_MATRIX2F)
MATRIX_FACTORY(Matrix3f, LusdMatrix3f, LUSD_VALUE_TYPE_MATRIX3F)
MATRIX_FACTORY(Matrix4f, LusdMatrix4f, LUSD_VALUE_TYPE_MATRIX4F)
MATRIX_FACTORY(Matrix2d, LusdMatrix2d, LUSD_VALUE_TYPE_MATRIX2D)
MATRIX_FACTORY(Matrix3d, LusdMatrix3d, LUSD_VALUE_TYPE_MATRIX3D)
MATRIX_FACTORY(Matrix4d, LusdMatrix4d, LUSD_VALUE_TYPE_MATRIX4D)

#undef MATRIX_FACTORY

/* -------------------------------------------------------------------
 * Scalar Accessors
 * ------------------------------------------------------------------- */

#define SCALAR_ACCESSOR(suffix, ctype, enumval) \
LusdResult lusdValueGet##suffix(LusdValue value, ctype* pVal) { \
    if (!pVal) return LUSD_ERROR_INVALID_ARGUMENT; \
    const LusdValueData* vd; \
    LusdResult res = check_type(value, enumval, &vd); \
    if (res != LUSD_SUCCESS) return res; \
    memcpy(pVal, get_scalar_data(vd), sizeof(ctype)); \
    return LUSD_SUCCESS; \
}

SCALAR_ACCESSOR(Bool,   bool,     LUSD_VALUE_TYPE_BOOL)
SCALAR_ACCESSOR(Int32,  int32_t,  LUSD_VALUE_TYPE_INT32)
SCALAR_ACCESSOR(UInt32, uint32_t, LUSD_VALUE_TYPE_UINT32)
SCALAR_ACCESSOR(Int64,  int64_t,  LUSD_VALUE_TYPE_INT64)
SCALAR_ACCESSOR(UInt64, uint64_t, LUSD_VALUE_TYPE_UINT64)
SCALAR_ACCESSOR(Half,   uint16_t, LUSD_VALUE_TYPE_HALF)
SCALAR_ACCESSOR(Float,  float,    LUSD_VALUE_TYPE_FLOAT)
SCALAR_ACCESSOR(Double, double,   LUSD_VALUE_TYPE_DOUBLE)

SCALAR_ACCESSOR(Int2, LusdInt2, LUSD_VALUE_TYPE_INT2)
SCALAR_ACCESSOR(Int3, LusdInt3, LUSD_VALUE_TYPE_INT3)
SCALAR_ACCESSOR(Int4, LusdInt4, LUSD_VALUE_TYPE_INT4)

SCALAR_ACCESSOR(Half2, LusdHalf2, LUSD_VALUE_TYPE_HALF2)
SCALAR_ACCESSOR(Half3, LusdHalf3, LUSD_VALUE_TYPE_HALF3)
SCALAR_ACCESSOR(Half4, LusdHalf4, LUSD_VALUE_TYPE_HALF4)

SCALAR_ACCESSOR(Float2, LusdFloat2, LUSD_VALUE_TYPE_FLOAT2)
SCALAR_ACCESSOR(Float3, LusdFloat3, LUSD_VALUE_TYPE_FLOAT3)
SCALAR_ACCESSOR(Float4, LusdFloat4, LUSD_VALUE_TYPE_FLOAT4)

SCALAR_ACCESSOR(Double2, LusdDouble2, LUSD_VALUE_TYPE_DOUBLE2)
SCALAR_ACCESSOR(Double3, LusdDouble3, LUSD_VALUE_TYPE_DOUBLE3)
SCALAR_ACCESSOR(Double4, LusdDouble4, LUSD_VALUE_TYPE_DOUBLE4)

SCALAR_ACCESSOR(Quath, LusdQuath, LUSD_VALUE_TYPE_QUATH)
SCALAR_ACCESSOR(Quatf, LusdQuatf, LUSD_VALUE_TYPE_QUATF)
SCALAR_ACCESSOR(Quatd, LusdQuatd, LUSD_VALUE_TYPE_QUATD)

SCALAR_ACCESSOR(Matrix2f, LusdMatrix2f, LUSD_VALUE_TYPE_MATRIX2F)
SCALAR_ACCESSOR(Matrix3f, LusdMatrix3f, LUSD_VALUE_TYPE_MATRIX3F)
SCALAR_ACCESSOR(Matrix4f, LusdMatrix4f, LUSD_VALUE_TYPE_MATRIX4F)
SCALAR_ACCESSOR(Matrix2d, LusdMatrix2d, LUSD_VALUE_TYPE_MATRIX2D)
SCALAR_ACCESSOR(Matrix3d, LusdMatrix3d, LUSD_VALUE_TYPE_MATRIX3D)
SCALAR_ACCESSOR(Matrix4d, LusdMatrix4d, LUSD_VALUE_TYPE_MATRIX4D)

#undef SCALAR_ACCESSOR

/* String accessor */
LusdResult lusdValueGetString(LusdValue value, const char** ppVal) {
    if (!ppVal) return LUSD_ERROR_INVALID_ARGUMENT;
    const LusdValueData* vd;
    LusdResult res = check_type(value, LUSD_VALUE_TYPE_STRING, &vd);
    if (res != LUSD_SUCCESS) return res;
    char* str;
    memcpy(&str, vd->storage.inlineData, sizeof(char*));
    *ppVal = str;
    return LUSD_SUCCESS;
}

/* Token accessor */
LusdResult lusdValueGetToken(LusdValue value, LusdToken* pVal) {
    if (!pVal) return LUSD_ERROR_INVALID_ARGUMENT;
    const LusdValueData* vd;
    LusdResult res = check_type(value, LUSD_VALUE_TYPE_TOKEN, &vd);
    if (res != LUSD_SUCCESS) return res;
    memcpy(pVal, vd->storage.inlineData, sizeof(LusdToken));
    return LUSD_SUCCESS;
}

/* -------------------------------------------------------------------
 * Value type name utility
 * ------------------------------------------------------------------- */

const char* lusdValueTypeGetName(LusdValueType type) {
    /* Mask out array bit for base type name */
    uint32_t base = (uint32_t)type & ~LUSD_VALUE_TYPE_ARRAY_BIT;
    switch ((LusdValueType)base) {
    case LUSD_VALUE_TYPE_INVALID:    return "invalid";
    case LUSD_VALUE_TYPE_NULL:       return "null";
    case LUSD_VALUE_TYPE_BOOL:       return "bool";
    case LUSD_VALUE_TYPE_INT32:      return "int";
    case LUSD_VALUE_TYPE_UINT32:     return "uint";
    case LUSD_VALUE_TYPE_INT64:      return "int64";
    case LUSD_VALUE_TYPE_UINT64:     return "uint64";
    case LUSD_VALUE_TYPE_HALF:       return "half";
    case LUSD_VALUE_TYPE_FLOAT:      return "float";
    case LUSD_VALUE_TYPE_DOUBLE:     return "double";
    case LUSD_VALUE_TYPE_STRING:     return "string";
    case LUSD_VALUE_TYPE_TOKEN:      return "token";
    case LUSD_VALUE_TYPE_ASSET_PATH: return "asset";
    case LUSD_VALUE_TYPE_PATH:       return "path";
    case LUSD_VALUE_TYPE_TIMECODE:   return "timecode";
    case LUSD_VALUE_TYPE_INT2:       return "int2";
    case LUSD_VALUE_TYPE_INT3:       return "int3";
    case LUSD_VALUE_TYPE_INT4:       return "int4";
    case LUSD_VALUE_TYPE_HALF2:      return "half2";
    case LUSD_VALUE_TYPE_HALF3:      return "half3";
    case LUSD_VALUE_TYPE_HALF4:      return "half4";
    case LUSD_VALUE_TYPE_FLOAT2:     return "float2";
    case LUSD_VALUE_TYPE_FLOAT3:     return "float3";
    case LUSD_VALUE_TYPE_FLOAT4:     return "float4";
    case LUSD_VALUE_TYPE_DOUBLE2:    return "double2";
    case LUSD_VALUE_TYPE_DOUBLE3:    return "double3";
    case LUSD_VALUE_TYPE_DOUBLE4:    return "double4";
    case LUSD_VALUE_TYPE_QUATH:      return "quath";
    case LUSD_VALUE_TYPE_QUATF:      return "quatf";
    case LUSD_VALUE_TYPE_QUATD:      return "quatd";
    case LUSD_VALUE_TYPE_MATRIX2F:   return "matrix2f";
    case LUSD_VALUE_TYPE_MATRIX3F:   return "matrix3f";
    case LUSD_VALUE_TYPE_MATRIX4F:   return "matrix4f";
    case LUSD_VALUE_TYPE_MATRIX2D:   return "matrix2d";
    case LUSD_VALUE_TYPE_MATRIX3D:   return "matrix3d";
    case LUSD_VALUE_TYPE_MATRIX4D:   return "matrix4d";
    case LUSD_VALUE_TYPE_COLOR3H:    return "color3h";
    case LUSD_VALUE_TYPE_COLOR3F:    return "color3f";
    case LUSD_VALUE_TYPE_COLOR3D:    return "color3d";
    case LUSD_VALUE_TYPE_COLOR4H:    return "color4h";
    case LUSD_VALUE_TYPE_COLOR4F:    return "color4f";
    case LUSD_VALUE_TYPE_COLOR4D:    return "color4d";
    case LUSD_VALUE_TYPE_POINT3H:    return "point3h";
    case LUSD_VALUE_TYPE_POINT3F:    return "point3f";
    case LUSD_VALUE_TYPE_POINT3D:    return "point3d";
    case LUSD_VALUE_TYPE_VECTOR3H:   return "vector3h";
    case LUSD_VALUE_TYPE_VECTOR3F:   return "vector3f";
    case LUSD_VALUE_TYPE_VECTOR3D:   return "vector3d";
    case LUSD_VALUE_TYPE_NORMAL3H:   return "normal3h";
    case LUSD_VALUE_TYPE_NORMAL3F:   return "normal3f";
    case LUSD_VALUE_TYPE_NORMAL3D:   return "normal3d";
    case LUSD_VALUE_TYPE_TEXCOORD2H: return "texCoord2h";
    case LUSD_VALUE_TYPE_TEXCOORD2F: return "texCoord2f";
    case LUSD_VALUE_TYPE_TEXCOORD2D: return "texCoord2d";
    case LUSD_VALUE_TYPE_TEXCOORD3H: return "texCoord3h";
    case LUSD_VALUE_TYPE_TEXCOORD3F: return "texCoord3f";
    case LUSD_VALUE_TYPE_TEXCOORD3D: return "texCoord3d";
    case LUSD_VALUE_TYPE_DICTIONARY: return "dictionary";
    case LUSD_VALUE_TYPE_VALUE_BLOCK: return "ValueBlock";
    default: return "unknown";
    }
}
