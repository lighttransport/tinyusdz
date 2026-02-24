/*
 * lusd_value_array.c - Array value factories and accessors
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_value.h"
#include "internal/lusd_internal.h"
#include <string.h>

/* -------------------------------------------------------------------
 * Internal helpers (shared with lusd_value.c)
 * ------------------------------------------------------------------- */

static const LusdAllocationCallbacks* inst_alloc_arr(struct LusdInstance_T* inst) {
    return inst->alloc.pfnAllocation ? &inst->alloc : NULL;
}

static LusdValueData* alloc_value_data_arr(struct LusdInstance_T* inst) {
    LusdValueData* vd = (LusdValueData*)lusd_alloc(
        inst_alloc_arr(inst), sizeof(LusdValueData), sizeof(void*));
    if (vd) memset(vd, 0, sizeof(*vd));
    return vd;
}

static LusdValue value_to_handle_arr(LusdValueData* vd) {
    return (LusdValue)(uintptr_t)vd;
}

static LusdValueData* handle_to_value_arr(LusdValue handle) {
    return (LusdValueData*)(uintptr_t)handle;
}

/* Create an array value: always heap-allocated */
static LusdResult create_array_value(
    struct LusdInstance_T* inst,
    LusdValueType baseType,
    uint64_t count,
    const void* pData,
    size_t elemSize,
    LusdValue* pV)
{
    if (!inst || !pV) return LUSD_ERROR_INVALID_ARGUMENT;
    if (count > 0 && !pData) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdValueData* vd = alloc_value_data_arr(inst);
    if (!vd) return LUSD_ERROR_OUT_OF_MEMORY;

    vd->type = (LusdValueType)((uint32_t)baseType | LUSD_VALUE_TYPE_ARRAY_BIT);
    vd->arrayCount = count;
    vd->useHeap = true;

    if (count > 0) {
        size_t totalSize = count * elemSize;
        vd->storage.heap.size = totalSize;
        vd->storage.heap.ptr = lusd_alloc(inst_alloc_arr(inst), totalSize, sizeof(void*));
        if (!vd->storage.heap.ptr) {
            lusd_free(inst_alloc_arr(inst), vd);
            return LUSD_ERROR_OUT_OF_MEMORY;
        }
        memcpy(vd->storage.heap.ptr, pData, totalSize);
    } else {
        vd->storage.heap.ptr = NULL;
        vd->storage.heap.size = 0;
    }

    *pV = value_to_handle_arr(vd);
    return LUSD_SUCCESS;
}

/* Get array data pointer (zero-copy) */
static LusdResult get_array_ptr(
    LusdValue value,
    LusdValueType expectedBaseType,
    uint64_t* pCount,
    const void** ppData)
{
    if (!value || !pCount || !ppData) return LUSD_ERROR_INVALID_ARGUMENT;
    const LusdValueData* vd = handle_to_value_arr(value);

    LusdValueType expectedArrayType =
        (LusdValueType)((uint32_t)expectedBaseType | LUSD_VALUE_TYPE_ARRAY_BIT);
    if (vd->type != expectedArrayType) return LUSD_ERROR_TYPE_MISMATCH;

    *pCount = vd->arrayCount;
    *ppData = vd->storage.heap.ptr;
    return LUSD_SUCCESS;
}

/* -------------------------------------------------------------------
 * Array Factories
 * ------------------------------------------------------------------- */

#define ARRAY_FACTORY(suffix, ctype, enumval) \
LusdResult lusdCreateValueArray##suffix( \
    LusdInstance inst, uint64_t count, const ctype* pData, LusdValue* pV) \
{ \
    return create_array_value(inst, enumval, count, pData, sizeof(ctype), pV); \
}

ARRAY_FACTORY(Bool,    bool,         LUSD_VALUE_TYPE_BOOL)
ARRAY_FACTORY(Int32,   int32_t,      LUSD_VALUE_TYPE_INT32)
ARRAY_FACTORY(UInt32,  uint32_t,     LUSD_VALUE_TYPE_UINT32)
ARRAY_FACTORY(Int64,   int64_t,      LUSD_VALUE_TYPE_INT64)
ARRAY_FACTORY(UInt64,  uint64_t,     LUSD_VALUE_TYPE_UINT64)
ARRAY_FACTORY(Half,    uint16_t,     LUSD_VALUE_TYPE_HALF)
ARRAY_FACTORY(Float,   float,        LUSD_VALUE_TYPE_FLOAT)
ARRAY_FACTORY(Double,  double,       LUSD_VALUE_TYPE_DOUBLE)
ARRAY_FACTORY(Float2,  LusdFloat2,   LUSD_VALUE_TYPE_FLOAT2)
ARRAY_FACTORY(Float3,  LusdFloat3,   LUSD_VALUE_TYPE_FLOAT3)
ARRAY_FACTORY(Float4,  LusdFloat4,   LUSD_VALUE_TYPE_FLOAT4)
ARRAY_FACTORY(Double2, LusdDouble2,  LUSD_VALUE_TYPE_DOUBLE2)
ARRAY_FACTORY(Double3, LusdDouble3,  LUSD_VALUE_TYPE_DOUBLE3)
ARRAY_FACTORY(Double4, LusdDouble4,  LUSD_VALUE_TYPE_DOUBLE4)
ARRAY_FACTORY(Int2,    LusdInt2,     LUSD_VALUE_TYPE_INT2)
ARRAY_FACTORY(Int3,    LusdInt3,     LUSD_VALUE_TYPE_INT3)
ARRAY_FACTORY(Int4,    LusdInt4,     LUSD_VALUE_TYPE_INT4)
ARRAY_FACTORY(Matrix4d, LusdMatrix4d, LUSD_VALUE_TYPE_MATRIX4D)
ARRAY_FACTORY(Matrix4f, LusdMatrix4f, LUSD_VALUE_TYPE_MATRIX4F)

#undef ARRAY_FACTORY

/* -------------------------------------------------------------------
 * Array Accessors (zero-copy)
 * ------------------------------------------------------------------- */

#define ARRAY_ACCESSOR(suffix, ctype, enumval) \
LusdResult lusdValueGetArrayPtr##suffix( \
    LusdValue value, uint64_t* pCount, const ctype** ppData) \
{ \
    return get_array_ptr(value, enumval, pCount, (const void**)ppData); \
}

ARRAY_ACCESSOR(Bool,    bool,         LUSD_VALUE_TYPE_BOOL)
ARRAY_ACCESSOR(Int32,   int32_t,      LUSD_VALUE_TYPE_INT32)
ARRAY_ACCESSOR(UInt32,  uint32_t,     LUSD_VALUE_TYPE_UINT32)
ARRAY_ACCESSOR(Int64,   int64_t,      LUSD_VALUE_TYPE_INT64)
ARRAY_ACCESSOR(UInt64,  uint64_t,     LUSD_VALUE_TYPE_UINT64)
ARRAY_ACCESSOR(Half,    uint16_t,     LUSD_VALUE_TYPE_HALF)
ARRAY_ACCESSOR(Float,   float,        LUSD_VALUE_TYPE_FLOAT)
ARRAY_ACCESSOR(Double,  double,       LUSD_VALUE_TYPE_DOUBLE)
ARRAY_ACCESSOR(Float2,  LusdFloat2,   LUSD_VALUE_TYPE_FLOAT2)
ARRAY_ACCESSOR(Float3,  LusdFloat3,   LUSD_VALUE_TYPE_FLOAT3)
ARRAY_ACCESSOR(Float4,  LusdFloat4,   LUSD_VALUE_TYPE_FLOAT4)
ARRAY_ACCESSOR(Double2, LusdDouble2,  LUSD_VALUE_TYPE_DOUBLE2)
ARRAY_ACCESSOR(Double3, LusdDouble3,  LUSD_VALUE_TYPE_DOUBLE3)
ARRAY_ACCESSOR(Double4, LusdDouble4,  LUSD_VALUE_TYPE_DOUBLE4)
ARRAY_ACCESSOR(Int2,    LusdInt2,     LUSD_VALUE_TYPE_INT2)
ARRAY_ACCESSOR(Int3,    LusdInt3,     LUSD_VALUE_TYPE_INT3)
ARRAY_ACCESSOR(Int4,    LusdInt4,     LUSD_VALUE_TYPE_INT4)
ARRAY_ACCESSOR(Matrix4d, LusdMatrix4d, LUSD_VALUE_TYPE_MATRIX4D)
ARRAY_ACCESSOR(Matrix4f, LusdMatrix4f, LUSD_VALUE_TYPE_MATRIX4F)

#undef ARRAY_ACCESSOR
