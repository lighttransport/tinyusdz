// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// CrateWriter::TryInlineValue — split out of crate-writer-values.cc.
// Decides whether a CrateValue can be inlined directly into its ValueRep (small
// PODs, string/token/asset indices, empty arrays) rather than written out-of-line.
// Self-contained: its own CANNOT_INLINE / TRY_INLINE_EMPTY_ARRAY macros, no
// dependency on WriteValueData/ConvertValueToCrateValue. Isolated so the ~1450-line
// WriteValueData TU compiles on its own (faster single-TU rebuilds).
#include "crate-writer.hh"

#include <cstring>
#include <sstream>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wexceptions"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

namespace tinyusdz {
namespace experimental {

bool CrateWriter::TryInlineValue(const crate::CrateValue& value, crate::ValueRep* rep) {
  // Phase 1: String/Token/AssetPath values
  // Strings and tokens are always inlined as indices in USDC format

  // Try to get as token
  if (auto* token_val = value.as<value::token>()) {
    crate::TokenIndex idx = GetOrCreateToken(token_val->str());
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  // Try to get as string
  if (auto* str_val = value.as<std::string>()) {
    crate::StringIndex idx = GetOrCreateString(*str_val);
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  // Try to get as AssetPath
  if (auto* asset_val = value.as<value::AssetPath>()) {
    // Inlined AssetPath is stored as a TokenIndex per OpenUSD's crate
    // format (matches CrateReader at crate-reader-values.cc which reads
    // it via GetToken). Storing as StringIndex caused the reader to read
    // a token at the wrong index and surface garbage like ";-)".
    crate::TokenIndex idx = GetOrCreateToken(asset_val->GetAssetPath());
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  // Basic scalar types

  // Try to get as int32
  if (auto* int_val = value.as<int32_t>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*int_val));
    return true;
  }

  // Try to get as uint32
  if (auto* uint_val = value.as<uint32_t>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*uint_val));
    return true;
  }

  // Try to get as Specifier enum
  if (auto* spec_val = value.as<Specifier>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*spec_val));
    return true;
  }

  // Try to get as Permission enum
  if (auto* perm_val = value.as<Permission>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*perm_val));
    return true;
  }

  // Try to get as Variability enum
  if (auto* var_val = value.as<Variability>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*var_val));
    return true;
  }

  // Try to get as int64
  if (auto* int64_val = value.as<int64_t>()) {
    // int64 cannot be inlined if value doesn't fit in 48 bits
    // (48 bits is the payload size in ValueRep)
    if (*int64_val >= -(1LL << 47) && *int64_val < (1LL << 47)) {
      rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64));
      rep->SetIsInlined();
      rep->SetPayload(static_cast<uint64_t>(*int64_val));
      return true;
    }
    // Falls through to out-of-line storage
  }

  // Try to get as uint64
  if (auto* uint64_val = value.as<uint64_t>()) {
    // uint64 can only be inlined if value fits in 48 bits
    if (*uint64_val < (1ULL << 48)) {
      rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64));
      rep->SetIsInlined();
      rep->SetPayload(*uint64_val);
      return true;
    }
    // Falls through to out-of-line storage
  }

  // Try to get as float
  if (auto* float_val = value.as<float>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT));
    rep->SetIsInlined();
    // Copy float bits to uint32, then to uint64
    uint32_t float_bits;
    memcpy(&float_bits, float_val, sizeof(float));
    rep->SetPayload(static_cast<uint64_t>(float_bits));
    return true;
  }

  // Double cannot be inlined (64 bits > 48 bit payload)
  if (value.as<double>()) { return false; }

  // Try to get as half
  if (auto* half_val = value.as<value::half>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF));
    rep->SetIsInlined();
    // half is 16 bits, fits easily in payload
    uint16_t half_bits = half_val->value;
    rep->SetPayload(static_cast<uint64_t>(half_bits));
    return true;
  }

  // Try to get as bool
  if (auto* bool_val = value.as<bool>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL));
    rep->SetIsInlined();
    rep->SetPayload(*bool_val ? 1 : 0);
    return true;
  }

  // Try to get as ValueBlock (None)
  if (value.as<value::ValueBlock>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK));
    rep->SetIsInlined();
    rep->SetPayload(0);  // ValueBlock has no payload data
    return true;
  }

  // Try to get as uchar
  if (auto* uchar_val = value.as<uint8_t>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*uchar_val));
    return true;
  }

  // Phase 1: Vector types
  // Vectors can be inlined if they fit in 48 bits (6 bytes)
  // Vec2h (4 bytes), Vec2f/Vec2i (8 bytes) cannot be inlined
  // Vec3/Vec4 cannot be inlined (12+ bytes)

  // Vec2h - half2 (4 bytes = sizeof(uint32_t)): "always inlined" in Pixar's
  // crate format — raw half bit patterns stored directly via memcpy, NOT int8.
  if (auto* vec2h_val = value.as<value::half2>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H));
    rep->SetIsInlined();
    uint32_t packed = 0;
    uint16_t data[2] = {(*vec2h_val)[0].value, (*vec2h_val)[1].value};
    memcpy(&packed, data, sizeof(data));
    rep->SetPayload(static_cast<uint64_t>(packed));
    return true;
  }

  // Types that cannot be inlined (too large for 48-bit payload)
#define CANNOT_INLINE(Type) \
  if (value.as<Type>()) { return false; }

  CANNOT_INLINE(value::float2)   // 8 bytes
  CANNOT_INLINE(value::double2)  // 16 bytes
  CANNOT_INLINE(value::int2)     // 8 bytes

  // Vec3h - half3: inline only if each component is exactly int8
  if (auto* vec3h_val = value.as<value::half3>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H));
    float f[3];
    f[0] = value::half_to_float((*vec3h_val)[0]);
    f[1] = value::half_to_float((*vec3h_val)[1]);
    f[2] = value::half_to_float((*vec3h_val)[2]);
    bool can_inline = true;
    int8_t ivec[3];
    for (int i = 0; i < 3; ++i) {
      float roundtripped = static_cast<float>(static_cast<int8_t>(f[i]));
      if (f[i] < -128.0f || f[i] > 127.0f ||
          std::memcmp(&roundtripped, &f[i], sizeof(float)) != 0) {
        can_inline = false;
        break;
      }
      ivec[i] = static_cast<int8_t>(f[i]);
    }
    if (can_inline) {
      uint32_t packed = 0;
      memcpy(&packed, ivec, sizeof(ivec));
      rep->SetIsInlined();
      rep->SetPayload(static_cast<uint64_t>(packed));
      return true;
    }
    return false;  // can't inline, write out-of-line
  }

  CANNOT_INLINE(value::float3)   // 12 bytes
  CANNOT_INLINE(value::double3)  // 24 bytes
  CANNOT_INLINE(value::int3)     // 12 bytes
  CANNOT_INLINE(value::half4)    // 8 bytes
  CANNOT_INLINE(value::float4)   // 16 bytes
  CANNOT_INLINE(value::double4)  // 32 bytes
  CANNOT_INLINE(value::int4)     // 16 bytes
  CANNOT_INLINE(value::matrix2d) // 32 bytes
  CANNOT_INLINE(value::matrix3d) // 72 bytes
  CANNOT_INLINE(value::matrix4d) // 128 bytes
  CANNOT_INLINE(value::quath)    // 8 bytes
  CANNOT_INLINE(value::quatf)    // 16 bytes
  CANNOT_INLINE(value::quatd)    // 32 bytes

#undef CANNOT_INLINE

  // Phase 2: Dictionary, ListOps, Reference, and Payload are NEVER inlined - always out-of-line storage
  if (value.as<value::dict>()) {
    return false;
  }

  if (value.as<ListOp<value::token>>() ||
      value.as<ListOp<std::string>>() ||
      value.as<ListOp<Path>>() ||
      value.as<ListOp<Reference>>() ||
      value.as<ListOp<Payload>>()) {
    return false;
  }

  if (value.as<Reference>() || value.as<Payload>() || value.as<VariantSelectionMap>()) {
    return false;
  }

  // Phase 1: Empty arrays are inlined with payload=0
  // Non-empty arrays require out-of-line storage with size prefix + data
  //
  // IMPORTANT: The Crate reader expects empty arrays to have payload=0.
  // When the reader sees payload=0 for an array type, it creates an empty array
  // without seeking to any offset.

  // Helper macro to check if array is empty and inline it
#define TRY_INLINE_EMPTY_ARRAY(VecType, CrateTypeId) \
  if (auto* arr = value.as<VecType>()) { \
    if (arr->empty()) { \
      rep->SetType(static_cast<int32_t>(CrateTypeId)); \
      rep->SetIsArray(); \
      rep->SetPayload(0); /* Empty array marker */ \
      return true; \
    } \
    return false; /* Non-empty array needs out-of-line storage */ \
  }

  // Check all array types - inline if empty, otherwise out-of-line
  TRY_INLINE_EMPTY_ARRAY(std::vector<bool>, crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL)
  TRY_INLINE_EMPTY_ARRAY(std::vector<uint8_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR)
  TRY_INLINE_EMPTY_ARRAY(std::vector<int32_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_INT)
  TRY_INLINE_EMPTY_ARRAY(std::vector<uint32_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT)
  TRY_INLINE_EMPTY_ARRAY(std::vector<int64_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64)
  TRY_INLINE_EMPTY_ARRAY(std::vector<uint64_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::half>, crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF)
  TRY_INLINE_EMPTY_ARRAY(std::vector<float>, crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT)
  TRY_INLINE_EMPTY_ARRAY(std::vector<double>, crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::half2>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::half3>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::half4>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::float2>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::float3>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::float4>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::double2>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::double3>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::double4>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::int2>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::int3>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::int4>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::matrix2d>, crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::matrix3d>, crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::matrix4d>, crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::quath>, crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::quatf>, crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::quatd>, crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD)
  TRY_INLINE_EMPTY_ARRAY(std::vector<std::string>, crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::token>, crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::AssetPath>, crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH)

#undef TRY_INLINE_EMPTY_ARRAY

  // Cannot inline - need out-of-line storage
  return false;
}

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
