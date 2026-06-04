// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2022 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Value unpacking and type dispatch for CrateReader
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "crate-reader.hh"

#ifdef __wasi__
#else
#include <thread>
#endif

#include <algorithm>
#include <unordered_set>
#include <stack>

#include "crate-format.hh"
#include "parser-timing.hh"
#include "crate-pprint.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "memory-budget.hh"
#include "path-util.hh"
#include "pprint-meta.hh"
#include "core/prim-spec.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"
#include "tiny-format.hh"
#include "str-util.hh"
#include "safe-arithmetic.hh"

//
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

#include "common-macros.inc"

namespace tinyusdz {
namespace crate {

#define kTag "[Crate]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK((*memory_manager_), (__nbytes), kTag)



#define VERSION_LESS_THAN_0_8_0(__version) ((_version[0] == 0) && (_version[1] < 7))

bool CrateReader::ReadVariantSelectionMap(VariantSelectionMap *d) {

  if (!d) {
    return false;
  }

  // map<string, string>

  // n
  // [key, value] * n

  uint64_t sz;
  if (!_sr->read8(&sz)) {
    _err += "Failed to read the number of elements for VariantsMap data.\n";
    return false;
  }

  if (sz > _config.maxVariantsMapElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "The number of elements for VariantsMap data is too large. Max = " << std::to_string(_config.maxVariantsMapElements) << ", but got " << std::to_string(sz));
  }

  for (size_t i = 0; i < sz; i++) {
    std::string key;
    if (!ReadString(&key)) {
      return false;
    }

    std::string value;
    if (!ReadString(&value)) {
      return false;
    }

    // TODO: Duplicate key check?
    d->emplace(key, value);
  }

  return true;
}

bool CrateReader::ReadCustomData(CustomDataType *d) {
  CustomDataType dict;
  uint64_t sz;
  if (!_sr->read8(&sz)) {
    _err += "Failed to read the number of elements for Dictionary data.\n";
    return false;
  }

  if (sz > _config.maxDictElements) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "The number of elements for Dictionary data is too large. Max = " << std::to_string(_config.maxDictElements) << ", but got " << std::to_string(sz));
  }

  DCOUT("# o elements in dict" << sz);

  while (sz--) {
    // key(StringIndex)
    std::string key;

    if (!ReadString(&key)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read key string for Dictionary element.");
    }

    // 8byte for the offset for recursive value. See RecursiveRead() in
    // crateFile.cpp for details.
    int64_t offset{0};
    if (!_sr->read8(&offset)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the offset for value in Dictionary.");
    }

    // -8 to compensate sizeof(offset). Guard against int64 underflow.
    if (offset < (std::numeric_limits<int64_t>::min)() + 8) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Dictionary value offset {} would underflow int64.", offset));
    }
    if (!_sr->seek_from_current(offset - 8)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek. Invalid offset value: " + std::to_string(offset));
    }

    DCOUT("key = " << key);

    crate::ValueRep rep{0};
    if (!ReadValueRep(&rep)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read value for Dictionary element.");
    }

    DCOUT("vrep =" << crate::GetCrateDataTypeName(rep.GetType()));

    auto saved_position = _sr->tell();

    crate::CrateValue value;
    if (!UnpackValueRep(rep, &value)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to unpack value of Dictionary element.");
    }

    // CrateValue -> MetaVariable (duplicated key is ok, later value wins)
    MetaVariable var;

    var.set_value(key, value.get_raw());

    dict[key] = var;

    if (!_sr->seek_set(saved_position)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to set seek.");
    }
  }

  (*d) = std::move(dict);
  return true;
}

bool CrateReader::UnpackInlinedValueRep(const crate::ValueRep &rep,
                                        crate::CrateValue *value) {
  if (!rep.IsInlined()) {
    PUSH_ERROR("ValueRep must be inlined value representation.");
    return false;
  }

  const auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
    return false;
  }

  if (rep.IsCompressed()) {
    PUSH_ERROR("Inlinved value must not be compressed.");
    return false;
  }

  if (rep.IsArray()) {
    PUSH_ERROR("Inlined value must not be an array.");
    return false;
  }

  const auto dty = tyRet.value();
  DCOUT(crate::GetCrateDataTypeRepr(dty));

  uint32_t d = (rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
  DCOUT("d = " << d);

  // TODO(syoyo): Use template SFINE?
  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::NumDataTypes:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID: {
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {
      value->Set(d ? true : false);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: {
      // AssetPath = TokenIndex for inlined value.
      if (auto v = GetToken(crate::Index(d))) {
        std::string str = v.value().str();

        value::AssetPath assetp(str);
        value->Set(assetp);
        return true;
      } else {
        PUSH_ERROR("Invalid Index for AssetPath.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {
      if (auto v = GetToken(crate::Index(d))) {
        value::token tok = v.value();

        DCOUT("value.token = " << tok);

        value->Set(tok);

        return true;
      } else {
        PUSH_ERROR("Invalid Index for Token.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      if (auto v = GetStringToken(crate::Index(d))) {
        std::string str = v.value().str();

        DCOUT("value.string = " << str);

        value->Set(str);

        return true;
      } else {
        PUSH_ERROR("Invalid Index for StringToken.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER: {
      if (d >= static_cast<int>(Specifier::Invalid)) {
        _err += "Invalid value for Specifier\n";
        return false;
      }
      Specifier val = static_cast<Specifier>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION: {
      if (d >= static_cast<int>(Permission::Invalid)) {
        _err += "Invalid value for Permission\n";
        return false;
      }
      Permission val = static_cast<Permission>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY: {
      if (d >= static_cast<int>(Variability::Invalid)) {
        _err += "Invalid value for Variability\n";
        return false;
      }
      Variability val = static_cast<Variability>(d);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR: {
      uint8_t val;
      memcpy(&val, &d, 1);

      DCOUT("value.uchar = " << val);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT: {
      int ival;
      memcpy(&ival, &d, sizeof(int));

      DCOUT("value.int = " << ival);

      value->Set(ival);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      uint32_t val;
      memcpy(&val, &d, sizeof(uint32_t));

      DCOUT("value.uint = " << val);

      value->Set(val);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      // Inline payload is 48 bits. Sign-extend from bit 47 to int64_t.
      uint64_t payload48 = rep.GetPayload() & ((1ull << 48) - 1);
      int64_t ival;
      if (payload48 & (1ull << 47)) {
        // Negative: extend the upper 16 bits with 1s.
        ival = static_cast<int64_t>(payload48 | (~((1ull << 48) - 1)));
      } else {
        ival = static_cast<int64_t>(payload48);
      }
      DCOUT("value.int64 = " << ival);
      value->Set(ival);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64: {
      // Inline payload is 48 bits, zero-extended.
      uint64_t ival = rep.GetPayload() & ((1ull << 48) - 1);
      DCOUT("value.uint64 = " << ival);
      value->Set(ival);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF: {
      value::half f;
      memcpy(&f, &d, sizeof(value::half));

      DCOUT("value.half = " << f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {
      float f;
      memcpy(&f, &d, sizeof(float));

      DCOUT("value.float = " << f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      // stored as float
      float _f;
      memcpy(&_f, &d, sizeof(float));

      double f = static_cast<double>(_f);

      value->Set(f);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::matrix2d v;
      memset(v.m, 0, sizeof(value::matrix2d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << "\n");

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::matrix3d v;
      memset(v.m, 0, sizeof(value::matrix3d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << ", "
                                    << v.m[2][2] << "\n");

      value->Set(v);

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D: {
      // Matrix contains diagnonal components only, and values are represented
      // in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::matrix4d v;
      memset(v.m, 0, sizeof(value::matrix4d));
      v.m[0][0] = static_cast<double>(data[0]);
      v.m[1][1] = static_cast<double>(data[1]);
      v.m[2][2] = static_cast<double>(data[2]);
      v.m[3][3] = static_cast<double>(data[3]);

      DCOUT("value.matrix(diag) = " << v.m[0][0] << ", " << v.m[1][1] << ", "
                                    << v.m[2][2] << ", " << v.m[3][3]);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH: {
      // Seems quaternion type is not allowed for Inlined Value.
      PUSH_ERROR("Quaternion type is not allowed for Inlined Value.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D: {
      // Value is represented in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::double2 v;
      v[0] = double(data[0]);
      v[1] = double(data[1]);

      DCOUT("value.double2 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F: {
      // Value is represented in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::float2 v;
      v[0] = float(data[0]);
      v[1] = float(data[1]);

      DCOUT("value.float2 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H: {
      // half2 (4 bytes) fits in uint32 → "always inlined" in Pixar's crate:
      // raw half bit patterns stored directly, NOT int8 compact encoding.
      uint16_t data[2];
      memcpy(data, &d, sizeof(data));

      value::half2 v;
      v[0].value = data[0];
      v[1].value = data[1];

      DCOUT("value.half2 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I: {
      // Value is represented in int8
      int8_t data[2];
      memcpy(&data, &d, 2);

      value::int2 v;
      v[0] = int(data[0]);
      v[1] = int(data[1]);

      DCOUT("value.int2 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::double3 v;
      v[0] = double(data[0]);
      v[1] = double(data[1]);
      v[2] = double(data[2]);

      DCOUT("value.double3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::float3 v;
      v[0] = float(data[0]);
      v[1] = float(data[1]);
      v[2] = float(data[2]);

      DCOUT("value.float3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H: {
      // Value is represented in int8 (Pixar convention: small integer compact)
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::half3 v;
      v[0] = value::float_to_half_full(float(data[0]));
      v[1] = value::float_to_half_full(float(data[1]));
      v[2] = value::float_to_half_full(float(data[2]));

      DCOUT("value.half3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I: {
      // Value is represented in int8
      int8_t data[3];
      memcpy(&data, &d, 3);

      value::int3 v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);

      DCOUT("value.int3 = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::double4 v;
      v[0] = static_cast<double>(data[0]);
      v[1] = static_cast<double>(data[1]);
      v[2] = static_cast<double>(data[2]);
      v[3] = static_cast<double>(data[3]);

      DCOUT("value.doublef = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::float4 v;
      v[0] = static_cast<float>(data[0]);
      v[1] = static_cast<float>(data[1]);
      v[2] = static_cast<float>(data[2]);
      v[3] = static_cast<float>(data[3]);

      DCOUT("value.vec4f = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::half4 v;
      v[0] = value::float_to_half_full(float(data[0]));
      v[1] = value::float_to_half_full(float(data[1]));
      v[2] = value::float_to_half_full(float(data[2]));
      v[3] = value::float_to_half_full(float(data[3]));

      DCOUT("value.vec4h = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I: {
      // Value is represented in int8
      int8_t data[4];
      memcpy(&data, &d, 4);

      value::int4 v;
      v[0] = static_cast<int32_t>(data[0]);
      v[1] = static_cast<int32_t>(data[1]);
      v[2] = static_cast<int32_t>(data[2]);
      v[3] = static_cast<int32_t>(data[3]);

      DCOUT("value.vec4i = " << v);

      value->Set(v);

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY: {
      // empty dict is allowed
      // TODO: empty(zero value) check?
      //crate::CrateValue::Dictionary dict;
      CustomDataType dict; // use CustomDataType for Dict
      value->Set(dict);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK: {
      // Guess No content for ValueBlock
      value::ValueBlock block;
      value->Set(block);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("ListOp data type `{}` cannot be inlined.",
          crate::GetCrateDataTypeName(dty.dtype_id)));
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_VECTOR: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Data type `{}` cannot be inlined.",
          crate::GetCrateDataTypeName(dty.dtype_id)));
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE: {
      PUSH_ERROR(
          "Invalid data type(or maybe not supported in TinyUSDZ yet) for "
          "Inlined value: " +
          crate::GetCrateDataTypeName(dty.dtype_id));
      return false;
    }
  }

  // Should never reach here.
  return false;
}



bool CrateReader::DescribeValueRep(const crate::ValueRep &rep,
                                   MMapArrayRef *ref,
                                   crate::CrateValue *value) {
  // Only eligible for non-inlined, non-compressed arrays
  if (!rep.IsArray() || rep.IsInlined() || rep.IsCompressed()) {
    return false;
  }

  // Empty array (payload == 0)
  if (rep.GetPayload() == 0) {
    return false;
  }

  auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    return false;
  }
  const auto dty = tyRet.value();

  // Only float/double/half vector types (never compressed in USDC)
  uint32_t elem_size = 0;
  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF:
      elem_size = sizeof(value::half);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT:
      elem_size = sizeof(float);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE:
      elem_size = sizeof(double);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H:
      elem_size = sizeof(value::half2);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F:
      elem_size = sizeof(value::float2);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D:
      elem_size = sizeof(value::double2);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H:
      elem_size = sizeof(value::half3);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F:
      elem_size = sizeof(value::float3);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D:
      elem_size = sizeof(value::double3);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H:
      elem_size = sizeof(value::half4);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F:
      elem_size = sizeof(value::float4);
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D:
      elem_size = sizeof(value::double4);
      break;
    // Matrix types are never compressed in USDC (fall through to uncompressed
    // path in OpenUSD's _WritePossiblyCompressedArray).  Scalar matrices can
    // be inlined when diagonal with int8 elements, but arrays are always raw.
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D:
      elem_size = sizeof(value::matrix2d);  // 32 bytes (2x2 double)
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D:
      elem_size = sizeof(value::matrix3d);  // 72 bytes (3x3 double)
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D:
      elem_size = sizeof(value::matrix4d);  // 128 bytes (4x4 double)
      break;
    default:
      return false;  // Not eligible (int types use compression, etc.)
  }

  // Seek to the payload offset
  uint64_t offset = rep.GetPayload();
  if (!_sr->seek_set(offset)) {
    return false;
  }

  // Read element count
  uint64_t n = 0;
  if (VERSION_LESS_THAN_0_8_0(_version)) {
    uint32_t shapesize;
    if (!_sr->read4(&shapesize)) return false;
    uint32_t n32;
    if (!_sr->read4(&n32)) return false;
    n = n32;
  } else {
    if (!_sr->read8(&n)) return false;
  }

  if (n > _config.maxArrayElements) {
    return false;
  }

  // Only defer large arrays — small arrays like `extent` (2 elements)
  // must be fully materialized for reconstruction to work correctly.
  static constexpr uint64_t kMinDeferElements = 1024;
  if (n < kMinDeferElements) {
    return false;
  }

  // Record the byte offset where data starts (right after the count)
  ref->byte_offset = _sr->tell();
  ref->element_count = n;
  ref->element_size = elem_size;
  ref->type_id = uint32_t(dty.dtype_id);

  // Skip past the data
  uint64_t data_size = n * elem_size;
  if (!_sr->seek_set(ref->byte_offset + data_size)) {
    return false;
  }

  // Set value to an empty typed vector for correct type propagation
  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF:
      value->Set(std::vector<value::half>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT:
      value->Set(std::vector<float>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE:
      value->Set(std::vector<double>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H:
      value->Set(std::vector<value::half2>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F:
      value->Set(std::vector<value::float2>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D:
      value->Set(std::vector<value::double2>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H:
      value->Set(std::vector<value::half3>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F:
      value->Set(std::vector<value::float3>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D:
      value->Set(std::vector<value::double3>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H:
      value->Set(std::vector<value::half4>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F:
      value->Set(std::vector<value::float4>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D:
      value->Set(std::vector<value::double4>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D:
      value->Set(std::vector<value::matrix2d>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D:
      value->Set(std::vector<value::matrix3d>());
      break;
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D:
      value->Set(std::vector<value::matrix4d>());
      break;
    default:
      return false;
  }

  return true;
}

bool CrateReader::UnpackValueRep(const crate::ValueRep &rep,
                                 crate::CrateValue *value) {

  if (rep.IsInlined()) {
    return UnpackInlinedValueRep(rep, value);
  }

  // mmap zero-copy V2: for eligible large arrays, store only the 24-byte
  // MMapArrayRef and an empty typed array. Data is read on demand by
  // Tydra's TryReadMMapArray from the mmap'd buffer.
  if (_config.use_mmap && rep.IsArray() && !rep.IsCompressed()) {
    MMapArrayRef mmap_ref;
    crate::CrateValue dummy;
    if (DescribeValueRep(rep, &mmap_ref, &dummy)) {
      // Use the empty typed array from DescribeValueRep as the value.
      // DescribeValueRep has already seeked past the data bytes.
      *value = std::move(dummy);
      value->set_mmap_ref(mmap_ref);
      return true;  // Skip full data unpacking
    }
    // DescribeValueRep returned false (too small, wrong type, etc.)
    // Fall through to normal unpacking.
  }

  DCOUT("ValueRep type value = " << rep.GetType());
  auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
  }

  const auto dty = tyRet.value();

#define TODO_IMPLEMENT(__dty)                                            \
  {                                                                      \
    PUSH_ERROR("TODO: '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data is not yet implemented.");                        \
    return false;                                                        \
  }

#define COMPRESS_UNSUPPORTED_CHECK(__dty)                                     \
  if (rep.IsCompressed()) {                                                   \
    PUSH_ERROR("Compressed [" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data is not yet supported.");                               \
    return false;                                                             \
  }

#define NON_ARRAY_UNSUPPORTED_CHECK(__dty)                                   \
  if (!rep.IsArray()) {                                                      \
    PUSH_ERROR("Non array '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data is not yet supported.");                              \
    return false;                                                            \
  }

#define ARRAY_UNSUPPORTED_CHECK(__dty)                                      \
  if (rep.IsArray()) {                                                      \
    PUSH_ERROR("Array of '" + crate::GetCrateDataTypeName(__dty.dtype_id) + \
               "' data type is not yet supported.");                        \
    return false;                                                           \
  }

  // payload is the offset to data.
  uint64_t offset = rep.GetPayload();

  // Handle empty arrays: non-inlined array with payload=0 means no data was
  // written (Pixar uses this encoding for empty arrays like `string[] = []`).
  if (rep.IsArray() && offset == 0 && !rep.IsInlined()) {
    auto tyRet0 = crate::GetCrateDataType(rep.GetType());
    if (!tyRet0) {
      PUSH_ERROR("Invalid type for empty array.");
      return false;
    }

    // Set a properly-typed empty array value
    switch (tyRet0.value().dtype_id) {
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL:   value->Set(std::vector<bool>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT:    value->Set(std::vector<int32_t>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT:   value->Set(std::vector<uint32_t>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64:  value->Set(std::vector<int64_t>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64: value->Set(std::vector<uint64_t>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF:   value->Set(std::vector<value::half>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT:  value->Set(std::vector<float>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: value->Set(std::vector<double>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: value->Set(std::vector<std::string>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN:  value->Set(std::vector<value::token>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: value->Set(std::vector<value::AssetPath>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F:  value->Set(std::vector<value::float2>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F:  value->Set(std::vector<value::float3>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F:  value->Set(std::vector<value::float4>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D:  value->Set(std::vector<value::double2>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D:  value->Set(std::vector<value::double3>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D:  value->Set(std::vector<value::double4>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I:  value->Set(std::vector<value::int2>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I:  value->Set(std::vector<value::int3>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I:  value->Set(std::vector<value::int4>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H:  value->Set(std::vector<value::half2>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H:  value->Set(std::vector<value::half3>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H:  value->Set(std::vector<value::half4>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF:  value->Set(std::vector<value::quatf>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD:  value->Set(std::vector<value::quatd>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH:  value->Set(std::vector<value::quath>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D: value->Set(std::vector<value::matrix2d>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D: value->Set(std::vector<value::matrix3d>()); break;
      case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D: value->Set(std::vector<value::matrix4d>()); break;
      default:
        DCOUT("Empty array: unhandled type " << crate::GetCrateDataTypeName(tyRet0.value().dtype_id));
        break;  // leave as void — pprint will show TODO
    }
    return true;
  }

  if (!_sr->seek_set(offset)) {
    PUSH_ERROR("Invalid offset.");
    return false;
  }

  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::NumDataTypes:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID: {
      DCOUT("dtype_id = " << std::to_string(uint32_t(dty.dtype_id)));
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<bool> v;

        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }

        // bool is encoded as 8bit value.

        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("# of bool array too large. TinyUSDZ limites it up to {}", _config.maxArrayElements));
        }

        size_t uint8_t_size;
        if (!safe::n_to_size<uint8_t>(n, &uint8_t_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(uint8_t)");
        }
        CHECK_MEMORY_USAGE(uint8_t_size);

        std::vector<uint8_t> data(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(uint8_t),
                       size_t(n) * sizeof(uint8_t),
                       reinterpret_cast<uint8_t *>(data.data()))) {
          PUSH_ERROR("Failed to read bool array.");
          return false;
        }

        // to std::vector<bool>, whose underlying storage may use 1bit.
        v.resize(size_t(n));
        for (size_t i = 0; i < n; i++) {
          v[i] = data[i] ? true : false;
        }

        value->Set(std::move(v));
        return true;

      } else {
        // non array bool should be inline encoded.
        PUSH_ERROR_AND_RETURN_TAG(kTag, "bool value must be inlined.");
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      // AssetPath is encoded as StringIndex for uninlined and array value
      // NOTE: inlined value uses TokenIndex.

      if (rep.IsArray()) {

        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::vector<value::AssetPath>());
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxAssetPathElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("# of AssetPaths too large. TinyUSDZ limites it up to {}", _config.maxAssetPathElements));
        }

        size_t crate_Index_size;
        if (!safe::n_to_size<crate::Index>(n, &crate_Index_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(crate::Index)");
        }
        CHECK_MEMORY_USAGE(crate_Index_size);

        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read StringIndex array.");
          return false;
        }

        std::vector<value::AssetPath> apaths(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          if (auto tokv = GetStringToken(v[i])) {
            DCOUT("StringToken[" << i << "] = " << tokv.value());
            apaths[i] = value::AssetPath(tokv.value().str());
          } else {
            return false;
          }
        }

        value->Set(std::move(apaths));
        return true;
      } else {

        CHECK_MEMORY_USAGE(sizeof(crate::Index));

        crate::Index v;
        if (!_sr->read(sizeof(crate::Index), sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read uint64 data.");
          return false;
        }

        DCOUT("StrIndex = " << v);

        if (auto tokv = GetStringToken(v)) {
          DCOUT("StringToken = " << tokv.value());
          value::AssetPath apath(tokv.value().str());
          value->Set(apath);
        } else {
          PUSH_ERROR_AND_RETURN("Invalid StringToken found.");
          return false;
        }

        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {

        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::vector<value::token>());
          return true;
        }

        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Token array too large. TinyUSDZ limits it up to {}", _config.maxArrayElements));
        }

        size_t crate_Index_size;
        if (!safe::n_to_size<crate::Index>(n, &crate_Index_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(crate::Index)");
        }
        CHECK_MEMORY_USAGE(crate_Index_size);

        std::vector<crate::Index> v;
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }

        std::vector<value::token> tokens(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          if (auto tokv = GetToken(v[i])) {
            DCOUT("Token[" << i << "] = " << tokv.value());
            tokens[i] = tokv.value();
          } else {
            return false;
          }
        }

        value->Set(tokens);
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("String array too large. TinyUSDZ limites it up to {}", _config.maxArrayElements));
        }

        size_t crate_Index_size;
        if (!safe::n_to_size<crate::Index>(n, &crate_Index_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(crate::Index)");
        }
        CHECK_MEMORY_USAGE(crate_Index_size);

        std::vector<crate::Index> v(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(crate::Index),
                       size_t(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read TokenIndex array.");
          return false;
        }

        std::vector<std::string> stringArray(static_cast<size_t>(n));

        for (size_t i = 0; i < n; i++) {
          if (auto stok = GetStringToken(v[i])) {
            stringArray[i] = stok.value().str();
          } else {
            return false;
          }
        }

        DCOUT("stringArray = " << stringArray);

        // TODO: Use token type?
        value->Set(std::move(stringArray));

        return true;
      } else {
        // TODO: support non-array string?
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY: {
      PUSH_ERROR("TODO: Specifier/Permission/Variability. isArray "
                 << rep.IsArray() << ", isCompressed " << rep.IsCompressed());
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)
      TODO_IMPLEMENT(dty)
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<int32_t> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty int array.");
          return false;
        }

        DCOUT("IntArray = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<uint32_t> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty uint array.");
          return false;
        }

        DCOUT("UIntArray = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      if (rep.IsArray()) {
        std::vector<int64_t> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }
        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Int64 array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty int64 array.");
          return false;
        }

        DCOUT("Int64Array = " << v);

        value->Set(std::move(v));
        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(int64_t));

        int64_t v;
        if (!_sr->read(sizeof(int64_t), sizeof(int64_t),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int64 data.");
          return false;
        }

        DCOUT("int64 = " << v);

        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64: {
      if (rep.IsArray()) {
        std::vector<uint64_t> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }

        if (!ReadIntArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read UInt64 array.");
          return false;
        }

        if (v.empty()) {
          PUSH_ERROR("Empty uint64 array.");
          return false;
        }

        DCOUT("UInt64Array = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(uint64_t));

        uint64_t v;
        if (!_sr->read(sizeof(uint64_t), sizeof(uint64_t),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read uint64 data.");
          return false;
        }

        DCOUT("uint64 = " << v);

        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF: {
      if (rep.IsArray()) {
        std::vector<value::half> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }
        if (!ReadHalfArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read half array value.");
          return false;
        }

        value->Set(std::move(v));

        return true;
      } else {
        PUSH_ERROR("Non-inlined, non-array Half value is invalid.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {
      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<float> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        
        std::vector<float> v;
        if (!ReadFloatArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read float array value.");
          return false;
        }

        DCOUT("FloatArray = " << value::print_array_snipped(v));

        value->Set(std::move(v));

        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        PUSH_ERROR("Non-inlined, non-array Float value is not supported.");
        return false;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<double> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        
        std::vector<double> v;
        if (!ReadDoubleArray(rep.IsCompressed(), &v)) {
          PUSH_ERROR("Failed to read Double value.");
          return false;
        }

        DCOUT("DoubleArray = " << value::print_array_snipped(v));
        value->Set(std::move(v));

        return true;
      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(double));

        double v{0.0};
        if (!_sr->read_double(&v)) {
          PUSH_ERROR("Failed to read Double value.");
          return false;
        }

        DCOUT("Double " << v);

        value->Set(v);

        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::matrix2d> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(std::move(v));
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_matrix2d_size;
        if (!safe::n_to_size<value::matrix2d>(n, &value_matrix2d_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::matrix2d)");
        }
        CHECK_MEMORY_USAGE(value_matrix2d_size);


        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix2d),
                       size_t(n) * sizeof(value::matrix2d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix2d array.");
          return false;
        }

        value->Set(std::move(v));

      } else {
        static_assert(sizeof(value::matrix2d) == (8 * 4), "");

        CHECK_MEMORY_USAGE(sizeof(value::matrix2d));

        value::matrix2d v;
        if (!_sr->read(sizeof(value::matrix2d), sizeof(value::matrix2d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix2d` type\n";
          return false;
        }

        DCOUT("value.matrix2d = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::matrix3d> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(std::move(v));
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_matrix3d_size;
        if (!safe::n_to_size<value::matrix3d>(n, &value_matrix3d_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::matrix3d)");
        }
        CHECK_MEMORY_USAGE(value_matrix3d_size);

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix3d),
                       size_t(n) * sizeof(value::matrix3d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix3d array.");
          return false;
        }

        value->Set(std::move(v));

      } else {
        static_assert(sizeof(value::matrix3d) == (8 * 9), "");

        CHECK_MEMORY_USAGE(sizeof(value::matrix3d));

        value::matrix3d v;
        if (!_sr->read(sizeof(value::matrix3d), sizeof(value::matrix3d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix3d` type\n";
          return false;
        }

        DCOUT("value.matrix3d = " << v);

        value->Set(v);
      }

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::matrix4d> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(std::move(v));
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_matrix4d_size;
        if (!safe::n_to_size<value::matrix4d>(n, &value_matrix4d_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::matrix4d)");
        }
        CHECK_MEMORY_USAGE(value_matrix4d_size);

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::matrix4d),
                       size_t(n) * sizeof(value::matrix4d),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Matrix4d array.");
          return false;
        }

        value->Set(std::move(v));

      } else {
        static_assert(sizeof(value::matrix4d) == (8 * 16), "");

        CHECK_MEMORY_USAGE(sizeof(value::matrix4d));

        value::matrix4d v;
        if (!_sr->read(sizeof(value::matrix4d), sizeof(value::matrix4d),
                       reinterpret_cast<uint8_t *>(v.m))) {
          _err += "Failed to read value of `matrix4d` type\n";
          return false;
        }

        DCOUT("value.matrix4d = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD: {
      if (rep.IsArray()) {
        std::vector<value::quatd> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(std::move(v));
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_quatd_size;
        if (!safe::n_to_size<value::quatd>(n, &value_quatd_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::quatd)");
        }
        CHECK_MEMORY_USAGE(value_quatd_size);

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quatd),
                       size_t(n) * sizeof(value::quatd),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quatf array.");
          return false;
        }

        DCOUT("Quatf[] = " << v);

        value->Set(std::move(v));

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(value::quatd));

        // Crate wire layout is [x, y, z, w] = (imag, real); see
        // value-types.hh:957. (USDA uses the opposite [w, x, y, z]
        // order — that's a *display* convention, not a wire one.)
        // tinyusdz's value::quatd struct matches the Crate layout, so
        // memcpy reads the bytes directly.
        value::quatd v;
        if (!_sr->read(sizeof(v), sizeof(v),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quatd value\n";
          return false;
        }

        DCOUT("Quatd = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF: {
      if (rep.IsArray()) {
        std::vector<value::quatf> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(std::move(v));
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_quatf_size;
        if (!safe::n_to_size<value::quatf>(n, &value_quatf_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::quatf)");
        }
        CHECK_MEMORY_USAGE(value_quatf_size);

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quatf),
                       size_t(n) * sizeof(value::quatf),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quatf array.");
          return false;
        }

        DCOUT("Quatf[] = " << v);

        value->Set(std::move(v));

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(value::quatf));

        // Crate wire layout is [x, y, z, w] = (imag, real). See
        // QUATD note above and value-types.hh:957.
        value::quatf v;
        if (!_sr->read(sizeof(v), sizeof(v),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quatf value\n";
          return false;
        }

        DCOUT("Quatf = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH: {
      if (rep.IsArray()) {
        std::vector<value::quath> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          value->Set(std::move(v));
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_quath_size;
        if (!safe::n_to_size<value::quath>(n, &value_quath_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::quath)");
        }
        CHECK_MEMORY_USAGE(value_quath_size);

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::quath),
                       size_t(n) * sizeof(value::quath),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read Quath array.");
          return false;
        }

        DCOUT("Quath[] = " << v);

        value->Set(std::move(v));

      } else {
        COMPRESS_UNSUPPORTED_CHECK(dty)

        CHECK_MEMORY_USAGE(sizeof(value::quath));

        // Crate wire layout is [x, y, z, w] = (imag, real); half
        // components stored as raw uint16 bit patterns. See QUATD note
        // above and value-types.hh:957.
        value::quath v;
        if (!_sr->read(sizeof(v), sizeof(v),
                       reinterpret_cast<uint8_t *>(&v))) {
          _err += "Failed to read Quath value\n";
          return false;
        }

        DCOUT("Quath = " << v);
        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::double2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n == 0) {
          std::vector<value::double2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_double2_size;
        if (!safe::n_to_size<value::double2>(n, &value_double2_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::double2)");
        }
        CHECK_MEMORY_USAGE(value_double2_size);

        std::vector<value::double2> v;
        // Always use std::vector - no mmap view mode
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::double2),
                       size_t(n) * sizeof(value::double2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read double2 array.");
          return false;
        }

        DCOUT("double2[] = " << value::print_array_snipped(v));

        value->Set(std::move(v));
        return true;
      } else {
        CHECK_MEMORY_USAGE(sizeof(value::double2));
        value::double2 v;
        if (!_sr->read(sizeof(value::double2), sizeof(value::double2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double2 data.");
          return false;
        }

        DCOUT("double2 = " << v);

        value->Set(v);
        return true;
      }
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::float2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        if (n == 0) {
          std::vector<value::float2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        size_t value_float2_size;
        if (!safe::n_to_size<value::float2>(n, &value_float2_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::float2)");
        }
        CHECK_MEMORY_USAGE(value_float2_size);

        std::vector<value::float2> v;
        // Always use std::vector - no mmap view mode
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::float2),
                       size_t(n) * sizeof(value::float2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read float2 array.");
          return false;
        }

        DCOUT("float2[] = " << value::print_array_snipped(v));
        //TUSDZ_LOG_D("float2[] = " << value::print_array_snipped(v));
        //TUSDZ_LOG_I("float2[].size" << v.size());

        value->Set(std::move(v));
        return true;
      } else {
        CHECK_MEMORY_USAGE(sizeof(value::float2));
        value::float2 v;
        if (!_sr->read(sizeof(value::float2), sizeof(value::float2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float2 data.");
          return false;
        }

        DCOUT("float2 = " << v);

        value->Set(v);
        return true;
      }
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::half2> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_half2_size;
        if (!safe::n_to_size<value::half2>(n, &value_half2_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::half2)");
        }
        CHECK_MEMORY_USAGE(value_half2_size);

        std::vector<value::half2> v;
        // Always use std::vector - no mmap view mode
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::half2),
                       size_t(n) * sizeof(value::half2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read half2 array.");
          return false;
        }

        DCOUT("half2[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::half2));
        value::half2 v;
        if (!_sr->read(sizeof(value::half2), sizeof(value::half2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half2");
          return false;
        }

        DCOUT("half2 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::int2> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_int2_size;
        if (!safe::n_to_size<value::int2>(n, &value_int2_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::int2)");
        }
        CHECK_MEMORY_USAGE(value_int2_size);

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int2),
                       size_t(n) * sizeof(value::int2),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int2 array.");
          return false;
        }

        DCOUT("int2[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::int2));
        value::int2 v;
        if (!_sr->read(sizeof(value::int2), sizeof(value::int2),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int2");
          return false;
        }

        DCOUT("int2 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::double3> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_double3_size;
        if (!safe::n_to_size<value::double3>(n, &value_double3_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::double3)");
        }
        CHECK_MEMORY_USAGE(value_double3_size);

        std::vector<value::double3> v;
        // Always use std::vector - no mmap view mode
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::double3),
                       size_t(n) * sizeof(value::double3),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read double3 array.");
          return false;
        }

        DCOUT("double3[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::double3));
        value::double3 v;
        if (!_sr->read(sizeof(value::double3), sizeof(value::double3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double3");
          return false;
        }

        DCOUT("double3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::float3> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_float3_size;
        if (!safe::n_to_size<value::float3>(n, &value_float3_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::float3)");
        }
        CHECK_MEMORY_USAGE(value_float3_size);

        {
          // Regular allocation for compressed data or when mmap is disabled
          // TODO: Chunked
          std::vector<value::float3> v;
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::float3),
                         size_t(n) * sizeof(value::float3),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read float3 array.");
            return false;
          }
          DCOUT("float3f[] = " << value::print_array_snipped(v));
          value->Set(std::move(v));
        }


      } else {
        CHECK_MEMORY_USAGE(sizeof(value::float3));
        value::float3 v;
        if (!_sr->read(sizeof(value::float3), sizeof(value::float3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float3");
          return false;
        }

        DCOUT("float3 = " << v);

        value->Set(v);
      }

      return true;
    }

    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          //TypedArray<value::half3> empty_v;
          std::vector<value::half3> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_half3_size;
        if (!safe::n_to_size<value::half3>(n, &value_half3_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::half3)");
        }
        CHECK_MEMORY_USAGE(value_half3_size);

        std::vector<value::half3> v;
        {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::half3),
                         size_t(n) * sizeof(value::half3),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read half3 array.");
            return false;
          }
        }

        DCOUT("half3[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::half3));
        value::half3 v;
        if (!_sr->read(sizeof(value::half3), sizeof(value::half3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half3");
          return false;
        }

        DCOUT("half3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::int3> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_int3_size;
        if (!safe::n_to_size<value::int3>(n, &value_int3_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::int3)");
        }
        CHECK_MEMORY_USAGE(value_int3_size);

        std::vector<value::int3> v;
        {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::int3),
                         size_t(n) * sizeof(value::int3),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read int3 array.");
            return false;
          }
        }

        DCOUT("int3[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::int3));
        value::int3 v;
        if (!_sr->read(sizeof(value::int3), sizeof(value::int3),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int3");
          return false;
        }

        DCOUT("int3 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::double4> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }

        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_double4_size;
        if (!safe::n_to_size<value::double4>(n, &value_double4_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::double4)");
        }
        CHECK_MEMORY_USAGE(value_double4_size);

        std::vector<value::double4> v;
        {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::double4),
                         size_t(n) * sizeof(value::double4),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read double4 array.");
            return false;
          }
        }

        DCOUT("double4[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::double4));
        value::double4 v;
        if (!_sr->read(sizeof(value::double4), sizeof(value::double4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read double4");
          return false;
        }

        DCOUT("double4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::float4> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_float4_size;
        if (!safe::n_to_size<value::float4>(n, &value_float4_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::float4)");
        }
        CHECK_MEMORY_USAGE(value_float4_size);

        std::vector<value::float4> v;
        {
          // Regular allocation for compressed data or when mmap is disabled
          v.resize(static_cast<size_t>(n));
          if (!_sr->read(size_t(n) * sizeof(value::float4),
                         size_t(n) * sizeof(value::float4),
                         reinterpret_cast<uint8_t *>(v.data()))) {
            PUSH_ERROR("Failed to read float4 array.");
            return false;
          }
        }

        DCOUT("float4[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::float4));
        value::float4 v;
        if (!_sr->read(sizeof(value::float4), sizeof(value::float4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read float4");
          return false;
        }

        DCOUT("float4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        if (rep.GetPayload() == 0) { // empty array
          std::vector<value::half4> empty_v;
          value->Set(std::move(empty_v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
          uint32_t shapesize; // not used
          if (!_sr->read4(&shapesize)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_half4_size;
        if (!safe::n_to_size<value::half4>(n, &value_half4_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::half4)");
        }
        CHECK_MEMORY_USAGE(value_half4_size);

        std::vector<value::half4> v;
        // Always use std::vector - no mmap view mode
        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::half4),
                       size_t(n) * sizeof(value::half4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read half4 array.");
          return false;
        }

        DCOUT("half4[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::half4));
        value::half4 v;
        if (!_sr->read(sizeof(value::half4), sizeof(value::half4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read half4");
          return false;
        }

        DCOUT("half4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<value::int4> v;
        if (rep.GetPayload() == 0) { // empty array
          value->Set(std::move(v));
          return true;
        }
        uint64_t n{0};
        if (VERSION_LESS_THAN_0_8_0(_version)) {
      uint32_t shapesize; // not used
      if (!_sr->read4(&shapesize)) {
        PUSH_ERROR("Failed to read the number of array elements.");
        return false;
      }
          uint32_t _n;
          if (!_sr->read4(&_n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
          n = _n;
        } else {
          if (!_sr->read8(&n)) {
            PUSH_ERROR("Failed to read the number of array elements.");
            return false;
          }
        }

        if (n > _config.maxArrayElements) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
        }

        size_t value_int4_size;
        if (!safe::n_to_size<value::int4>(n, &value_int4_size)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(value::int4)");
        }
        CHECK_MEMORY_USAGE(value_int4_size);

        v.resize(static_cast<size_t>(n));
        if (!_sr->read(size_t(n) * sizeof(value::int4),
                       size_t(n) * sizeof(value::int4),
                       reinterpret_cast<uint8_t *>(v.data()))) {
          PUSH_ERROR("Failed to read int4 array.");
          return false;
        }

        DCOUT("int4[] = " << value::print_array_snipped(v));
        value->Set(std::move(v));

      } else {
        CHECK_MEMORY_USAGE(sizeof(value::int4));
        value::int4 v;
        if (!_sr->read(sizeof(value::int4), sizeof(value::int4),
                       reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read int4");
          return false;
        }

        DCOUT("int4 = " << v);

        value->Set(v);
      }

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      ARRAY_UNSUPPORTED_CHECK(dty)

      //crate::CrateValue::Dictionary dict;
      CustomDataType dict;

      if (!ReadCustomData(&dict)) {
        _err += "Failed to read Dictionary value\n";
        return false;
      }

      DCOUT("Dict. nelems = " << dict.size());

      value->Set(std::move(dict));

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP: {
      ListOp<value::token> lst;

      if (!ReadTokenListOp(&lst)) {
        PUSH_ERROR("Failed to read TokenListOp data");
        return false;
      }

      value->Set(std::move(lst));
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      // SdfListOp<class SdfPath>
      // => underliying storage is the array of ListOp[PathIndex]
      ListOp<Path> lst;

      if (!ReadPathListOp(&lst)) {
        PUSH_ERROR("Failed to read PathListOp data.");
        return false;
      }

      value->Set(std::move(lst));

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP: {
      ListOp<std::string> lst;

      if (!ReadStringListOp(&lst)) {
        PUSH_ERROR("Failed to read StringListOp data");
        return false;
      }

      value->Set(lst);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      std::vector<Path> v;
      if (!ReadPathArray(&v)) {
        _err += "Failed to read PathVector value\n";
        return false;
      }

      DCOUT("PathVector = " << to_string(v));

      value->Set(std::move(v));

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      // std::vector<Index>
      uint64_t n{0};
      if (!_sr->read8(&n)) {
        PUSH_ERROR("Failed to read the number of TokenVector elements (offset=" +
                   std::to_string(offset) + ").");
        return false;
      }
      DCOUT("TokenVector: n=" << n << " at offset=" << offset);

      if (n > _config.maxArrayElements) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Array size {} too large. maxArrayElements is set to {}. Please increase maxArrayElements in CrateReaderConfig.", n, _config.maxArrayElements));
      }

      size_t crate_Index_size;
      if (!safe::n_to_size<crate::Index>(n, &crate_Index_size)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Integer overflow: n * sizeof(crate::Index)");
      }
      CHECK_MEMORY_USAGE(crate_Index_size);

      std::vector<crate::Index> indices(static_cast<size_t>(n));
      if (n > 0) {
        if (!_sr->read(static_cast<size_t>(n) * sizeof(crate::Index),
                       static_cast<size_t>(n) * sizeof(crate::Index),
                       reinterpret_cast<uint8_t *>(indices.data()))) {
          PUSH_ERROR("Failed to read TokenVector value.");
          return false;
        }
      }

      DCOUT("TokenVector(index) = " << indices);

      std::vector<value::token> tokens(indices.size());
      for (size_t i = 0; i < indices.size(); i++) {
        if (auto tokv = GetToken(indices[i])) {
          tokens[i] = tokv.value();
        } else {
          return false;
        }
      }

      DCOUT("TokenVector = " << tokens);

      value->Set(std::move(tokens));

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      value::TimeSamples ts;
      if (!ReadTimeSamples(&ts)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read TimeSamples data");
      }

      // Finalize (sort) once, single-threaded, so later const reads are pure and
      // the TimeSamples can be safely shared across threads. See
      // value::TimeSamples::update().
      ts.update();

      //TUSDZ_LOG_I("Set TimeSamples begin\n");
      value->Set(std::move(ts));
      //TUSDZ_LOG_I("Set TimeSamples end\n");

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR: {
      std::vector<double> v;
      if (!ReadDoubleVector(&v)) {
        _err += "Failed to read DoubleVector value\n";
        return false;
      }

      DCOUT("DoubleArray = " << v);

      value->Set(std::move(v));

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      std::vector<std::string> v;
      if (!ReadStringArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read StringVector value");
      }

      DCOUT("StringArray = " << v);

      value->Set(std::move(v));

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      VariantSelectionMap m;
      if (!ReadVariantSelectionMap(&m)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read VariantSelectionMap value");
      }

      DCOUT("VariantSelectionMap = " << print_variantSelectionMap(m, 0));

      value->Set(std::move(m));

      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      // LayerOffset[]

      std::vector<LayerOffset> v;
      if (!ReadLayerOffsetArray(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read LayerOffsetVector value");
      }

      DCOUT("LayerOffsetVector = " << v);

      value->Set(std::move(v));

      return true;

    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD: {
      COMPRESS_UNSUPPORTED_CHECK(dty)

      // Payload
      Payload v;
      if (!ReadPayload(&v)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read Payload value");
      }

      DCOUT("Payload = " << v);

      value->Set(v);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP: {
      ListOp<Payload> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read PayloadListOp data");
        return false;
      }

      value->Set(std::move(lst));
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP: {
      ListOp<Reference> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read ReferenceListOp data");
        return false;
      }

      value->Set(std::move(lst));
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP: {
      ListOp<int32_t> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read IntListOp data");
        return false;
      }

      value->Set(std::move(lst));
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP: {
      ListOp<int64_t> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read Int64ListOp data");
        return false;
      }

      value->Set(std::move(lst));
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP: {
      ListOp<uint32_t> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read UIntListOp data");
        return false;
      }

      value->Set(std::move(lst));
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP: {
      ListOp<uint64_t> lst;

      if (!ReadListOp(&lst)) {
        PUSH_ERROR("Failed to read UInt64ListOp data");
        return false;
      }

      value->Set(std::move(lst));
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK: {
      PUSH_ERROR(
          "ValueBlock must be defined in Inlined ValueRep.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE: {

      crate::ValueRep local_rep{0};
      if (!ReadValueRep(&local_rep)) {
        PUSH_ERROR(
            "Failed to read ValueRep for VALUE type.");
        return false;
      }

      if (unpackRecursionGuard.size() > _config.maxValueRecursion) {
        // To many recursive stacks. We report error
        PUSH_ERROR(
            "Too many recursion when decoding generic VALUE data.");
        return false;
      }

      // TODO: use crate::ValueRep for set container type.
      if (unpackRecursionGuard.count(local_rep.GetData())) {
        // Recursion detected.
        PUSH_ERROR(
            "Corrupted Value data detected.");
        return false;
      } else {
        crate::CrateValue local_val;
        bool ret = UnpackValueRep(local_rep, &local_val);
        if (!ret) {
          return false;
        }

        (*value) = std::move(local_val);

        unpackRecursionGuard.erase(local_rep.GetData());
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      ARRAY_UNSUPPORTED_CHECK(dty)

      // 8byte for the offset for recursive value. See RecursiveRead() in
      // https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usd/usd/crateFile.cpp for details.
      int64_t local_offset{0};
      if (!_sr->read8(&local_offset)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to read the offset for value in Dictionary.");
        return false;
      }

      DCOUT("UnregisteredValue  offset = " << local_offset);
      DCOUT("tell = " << _sr->tell());

      // -8 to compensate sizeof(offset). Guard against int64 underflow.
      if (local_offset < (std::numeric_limits<int64_t>::min)() + 8) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("UNREGISTERED_VALUE offset {} would underflow int64.", local_offset));
      }
      if (!_sr->seek_from_current(local_offset - 8)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to seek to UNREGISTERD_VALUE content. Invalid offset value: " +
                std::to_string(local_offset));
      }

      uint64_t saved_position = _sr->tell();

      crate::ValueRep local_rep{0};
      if (!ReadValueRep(&local_rep)) {
        PUSH_ERROR(
            "Failed to read ValueRep for UNREGISTERED_VALUE type.");
        return false;
      }

      auto local_tyRet = crate::GetCrateDataType(local_rep.GetType());
      if (!local_tyRet) {
        PUSH_ERROR(local_tyRet.error());
        return false;
      }

      const auto local_dty = local_tyRet.value();

      // Should be STRING or DICTIONARY for UNREGISTERED_VALUE.
      if (local_dty.dtype_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING) {
        COMPRESS_UNSUPPORTED_CHECK(local_dty)
        ARRAY_UNSUPPORTED_CHECK(local_dty)

        if (local_rep.IsInlined()) {
          uint32_t local_d = (local_rep.GetPayload() & ((1ull << (sizeof(uint32_t) * 8)) - 1));
          if (auto v = GetStringToken(crate::Index(local_d))) {
            std::string str = v.value().str();

            DCOUT("UNREGISTERED_VALUE.string = " << str);

            // Preserve the string exactly as stored in the crate file.
            // Quotes are part of the value for string-typed unregistered
            // metadata and must roundtrip losslessly.
            value->Set(str);

            if (!_sr->seek_set(saved_position)) {
              PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to set seek.");
            }
            return true;
          } else {
            PUSH_ERROR("Failed to decode String.");
            return false;
          }
        } else {
          PUSH_ERROR("String value must be inlined.");
          return false;
        }

      } else if (local_dty.dtype_id == crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY) {
        COMPRESS_UNSUPPORTED_CHECK(local_dty)
        ARRAY_UNSUPPORTED_CHECK(local_dty)

        CustomDataType dict;

        if (local_rep.IsInlined()) {
          // empty dict
        }  else{
          if (!ReadCustomData(&dict)) {
            _err += "Failed to read Dictionary value\n";
            return false;
          }
        }
        value->Set(std::move(dict));
        if (!_sr->seek_set(saved_position)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to set seek.");
        }
        return true;

      } else {
        PUSH_ERROR_AND_RETURN(fmt::format("UNREGISTERD_VALUE type must be string or dictionary, but got other data type: {}(id {}).", GetCrateDataTypeName(local_dty.dtype_id), local_rep.GetType()));
      }

    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE: {
      PUSH_ERROR(
          "Invalid data type(or maybe not supported in TinyUSDZ yet) for "
          "Uninlined value: " +
          crate::GetCrateDataTypeName(dty.dtype_id));
      return false;
    }
  }

#undef TODO_IMPLEMENT
#undef COMPRESS_UNSUPPORTED_CHECK
#undef NON_ARRAY_UNSUPPORTED_CHECK

  // Never should reach here.
  return false;
}

} // namespace crate
} // namespace tinyusdz
