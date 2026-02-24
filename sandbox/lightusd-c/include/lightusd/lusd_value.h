/*
 * lusd_value.h - Type-erased value container
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_VALUE_H
#define LUSD_VALUE_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_enums.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

/* -------------------------------------------------------------------
 * Query
 * ------------------------------------------------------------------- */

/* Get the type of a value */
LUSD_API LusdValueType lusdValueGetType(LusdValue value);

/* Check if value holds an array */
LUSD_API bool lusdValueIsArray(LusdValue value);

/* Get array element count (0 for scalars) */
LUSD_API uint64_t lusdValueGetArraySize(LusdValue value);

/* Destroy a value. No-op if LUSD_NULL_HANDLE. */
LUSD_API void lusdDestroyValue(LusdInstance instance, LusdValue value);

/* -------------------------------------------------------------------
 * Scalar Factories
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdCreateValueBool(LusdInstance inst, bool val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueInt32(LusdInstance inst, int32_t val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueUInt32(LusdInstance inst, uint32_t val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueInt64(LusdInstance inst, int64_t val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueUInt64(LusdInstance inst, uint64_t val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueHalf(LusdInstance inst, uint16_t val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueFloat(LusdInstance inst, float val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueDouble(LusdInstance inst, double val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueString(LusdInstance inst, const char* val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueToken(LusdInstance inst, LusdToken val, LusdValue* pV);

LUSD_API LusdResult lusdCreateValueInt2(LusdInstance inst, LusdInt2 val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueInt3(LusdInstance inst, LusdInt3 val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueInt4(LusdInstance inst, LusdInt4 val, LusdValue* pV);

LUSD_API LusdResult lusdCreateValueHalf2(LusdInstance inst, LusdHalf2 val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueHalf3(LusdInstance inst, LusdHalf3 val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueHalf4(LusdInstance inst, LusdHalf4 val, LusdValue* pV);

LUSD_API LusdResult lusdCreateValueFloat2(LusdInstance inst, LusdFloat2 val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueFloat3(LusdInstance inst, LusdFloat3 val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueFloat4(LusdInstance inst, LusdFloat4 val, LusdValue* pV);

LUSD_API LusdResult lusdCreateValueDouble2(LusdInstance inst, LusdDouble2 val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueDouble3(LusdInstance inst, LusdDouble3 val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueDouble4(LusdInstance inst, LusdDouble4 val, LusdValue* pV);

LUSD_API LusdResult lusdCreateValueQuath(LusdInstance inst, LusdQuath val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueQuatf(LusdInstance inst, LusdQuatf val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueQuatd(LusdInstance inst, LusdQuatd val, LusdValue* pV);

LUSD_API LusdResult lusdCreateValueMatrix2f(LusdInstance inst, const LusdMatrix2f* val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueMatrix3f(LusdInstance inst, const LusdMatrix3f* val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueMatrix4f(LusdInstance inst, const LusdMatrix4f* val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueMatrix2d(LusdInstance inst, const LusdMatrix2d* val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueMatrix3d(LusdInstance inst, const LusdMatrix3d* val, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueMatrix4d(LusdInstance inst, const LusdMatrix4d* val, LusdValue* pV);

/* -------------------------------------------------------------------
 * Scalar Accessors
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdValueGetBool(LusdValue value, bool* pVal);
LUSD_API LusdResult lusdValueGetInt32(LusdValue value, int32_t* pVal);
LUSD_API LusdResult lusdValueGetUInt32(LusdValue value, uint32_t* pVal);
LUSD_API LusdResult lusdValueGetInt64(LusdValue value, int64_t* pVal);
LUSD_API LusdResult lusdValueGetUInt64(LusdValue value, uint64_t* pVal);
LUSD_API LusdResult lusdValueGetHalf(LusdValue value, uint16_t* pVal);
LUSD_API LusdResult lusdValueGetFloat(LusdValue value, float* pVal);
LUSD_API LusdResult lusdValueGetDouble(LusdValue value, double* pVal);
LUSD_API LusdResult lusdValueGetString(LusdValue value, const char** ppVal);
LUSD_API LusdResult lusdValueGetToken(LusdValue value, LusdToken* pVal);

LUSD_API LusdResult lusdValueGetInt2(LusdValue value, LusdInt2* pVal);
LUSD_API LusdResult lusdValueGetInt3(LusdValue value, LusdInt3* pVal);
LUSD_API LusdResult lusdValueGetInt4(LusdValue value, LusdInt4* pVal);

LUSD_API LusdResult lusdValueGetHalf2(LusdValue value, LusdHalf2* pVal);
LUSD_API LusdResult lusdValueGetHalf3(LusdValue value, LusdHalf3* pVal);
LUSD_API LusdResult lusdValueGetHalf4(LusdValue value, LusdHalf4* pVal);

LUSD_API LusdResult lusdValueGetFloat2(LusdValue value, LusdFloat2* pVal);
LUSD_API LusdResult lusdValueGetFloat3(LusdValue value, LusdFloat3* pVal);
LUSD_API LusdResult lusdValueGetFloat4(LusdValue value, LusdFloat4* pVal);

LUSD_API LusdResult lusdValueGetDouble2(LusdValue value, LusdDouble2* pVal);
LUSD_API LusdResult lusdValueGetDouble3(LusdValue value, LusdDouble3* pVal);
LUSD_API LusdResult lusdValueGetDouble4(LusdValue value, LusdDouble4* pVal);

LUSD_API LusdResult lusdValueGetQuath(LusdValue value, LusdQuath* pVal);
LUSD_API LusdResult lusdValueGetQuatf(LusdValue value, LusdQuatf* pVal);
LUSD_API LusdResult lusdValueGetQuatd(LusdValue value, LusdQuatd* pVal);

LUSD_API LusdResult lusdValueGetMatrix2f(LusdValue value, LusdMatrix2f* pVal);
LUSD_API LusdResult lusdValueGetMatrix3f(LusdValue value, LusdMatrix3f* pVal);
LUSD_API LusdResult lusdValueGetMatrix4f(LusdValue value, LusdMatrix4f* pVal);
LUSD_API LusdResult lusdValueGetMatrix2d(LusdValue value, LusdMatrix2d* pVal);
LUSD_API LusdResult lusdValueGetMatrix3d(LusdValue value, LusdMatrix3d* pVal);
LUSD_API LusdResult lusdValueGetMatrix4d(LusdValue value, LusdMatrix4d* pVal);

/* -------------------------------------------------------------------
 * Array Factories
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdCreateValueArrayBool(LusdInstance inst, uint64_t count, const bool* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayInt32(LusdInstance inst, uint64_t count, const int32_t* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayUInt32(LusdInstance inst, uint64_t count, const uint32_t* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayInt64(LusdInstance inst, uint64_t count, const int64_t* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayUInt64(LusdInstance inst, uint64_t count, const uint64_t* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayHalf(LusdInstance inst, uint64_t count, const uint16_t* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayFloat(LusdInstance inst, uint64_t count, const float* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayDouble(LusdInstance inst, uint64_t count, const double* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayFloat2(LusdInstance inst, uint64_t count, const LusdFloat2* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayFloat3(LusdInstance inst, uint64_t count, const LusdFloat3* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayFloat4(LusdInstance inst, uint64_t count, const LusdFloat4* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayDouble2(LusdInstance inst, uint64_t count, const LusdDouble2* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayDouble3(LusdInstance inst, uint64_t count, const LusdDouble3* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayDouble4(LusdInstance inst, uint64_t count, const LusdDouble4* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayInt2(LusdInstance inst, uint64_t count, const LusdInt2* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayInt3(LusdInstance inst, uint64_t count, const LusdInt3* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayInt4(LusdInstance inst, uint64_t count, const LusdInt4* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayMatrix4d(LusdInstance inst, uint64_t count, const LusdMatrix4d* pData, LusdValue* pV);
LUSD_API LusdResult lusdCreateValueArrayMatrix4f(LusdInstance inst, uint64_t count, const LusdMatrix4f* pData, LusdValue* pV);

/* -------------------------------------------------------------------
 * Array Accessors (zero-copy pointer access)
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdValueGetArrayPtrBool(LusdValue value, uint64_t* pCount, const bool** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrInt32(LusdValue value, uint64_t* pCount, const int32_t** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrUInt32(LusdValue value, uint64_t* pCount, const uint32_t** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrInt64(LusdValue value, uint64_t* pCount, const int64_t** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrUInt64(LusdValue value, uint64_t* pCount, const uint64_t** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrHalf(LusdValue value, uint64_t* pCount, const uint16_t** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrFloat(LusdValue value, uint64_t* pCount, const float** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrDouble(LusdValue value, uint64_t* pCount, const double** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrFloat2(LusdValue value, uint64_t* pCount, const LusdFloat2** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrFloat3(LusdValue value, uint64_t* pCount, const LusdFloat3** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrFloat4(LusdValue value, uint64_t* pCount, const LusdFloat4** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrDouble2(LusdValue value, uint64_t* pCount, const LusdDouble2** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrDouble3(LusdValue value, uint64_t* pCount, const LusdDouble3** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrDouble4(LusdValue value, uint64_t* pCount, const LusdDouble4** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrInt2(LusdValue value, uint64_t* pCount, const LusdInt2** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrInt3(LusdValue value, uint64_t* pCount, const LusdInt3** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrInt4(LusdValue value, uint64_t* pCount, const LusdInt4** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrMatrix4d(LusdValue value, uint64_t* pCount, const LusdMatrix4d** ppData);
LUSD_API LusdResult lusdValueGetArrayPtrMatrix4f(LusdValue value, uint64_t* pCount, const LusdMatrix4f** ppData);

/* -------------------------------------------------------------------
 * Value type name utility
 * ------------------------------------------------------------------- */

LUSD_API const char* lusdValueTypeGetName(LusdValueType type);

LUSD_EXTERN_C_END

#endif /* LUSD_VALUE_H */
