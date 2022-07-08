// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#if defined(__wasi__)
#else
#include <thread>
#endif

#include "crate-format.hh"
#include "pprinter.hh"
#include "value-type.hh"

#ifndef TINYUSDZ_PRODUCTION_BUILD
#define TINYUSDZ_LOCAL_DEBUG_PRINT
#endif

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
#define DCOUT(x)                                               \
  do {                                                         \
    std::cout << __FILE__ << ":" << __func__ << ":"            \
              << std::to_string(__LINE__) << " " << x << "\n"; \
  } while (false)
#else
#define DCOUT(x)
#endif

namespace tinyusdz {
namespace crate {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
nonstd::expected<CrateDataType, std::string> GetCrateDataType(int32_t type_id) {
  static std::map<uint32_t, CrateDataType> table;
  DCOUT("type_id = " << type_id);

  if (table.size() == 0) {
    // Register data types
    // NOTE(syoyo): We can use C++11 template to create compile-time table for
    // data types, but this way(using std::map) is easier to read and maintain,
    // I think.

    // See <pxrUSD>/pxr/usd/usd/crateDataTypes.h

#define ADD_VALUE_TYPE(NAME_STR, TYPE_ID, SUPPORTS_ARRAY)     \
  {                                                           \
    assert(table.count(static_cast<uint32_t>(TYPE_ID)) == 0); \
    table[static_cast<uint32_t>(TYPE_ID)] =                   \
        CrateDataType(NAME_STR, TYPE_ID, SUPPORTS_ARRAY);     \
  }

    // (num_string, type_id(in crateData), supports_array)

    // 0 is reserved as `Invalid` type.
    ADD_VALUE_TYPE("Invald", CrateDataTypeId::CRATE_DATA_TYPE_INVALID, false)

    // Array types.
    ADD_VALUE_TYPE("Bool", CrateDataTypeId::CRATE_DATA_TYPE_BOOL, true)

    ADD_VALUE_TYPE("UChar", CrateDataTypeId::CRATE_DATA_TYPE_UCHAR, true)
    ADD_VALUE_TYPE("Int", CrateDataTypeId::CRATE_DATA_TYPE_INT, true)
    ADD_VALUE_TYPE("UInt", CrateDataTypeId::CRATE_DATA_TYPE_UINT, true)
    ADD_VALUE_TYPE("Int64", CrateDataTypeId::CRATE_DATA_TYPE_INT64, true)
    ADD_VALUE_TYPE("UInt64", CrateDataTypeId::CRATE_DATA_TYPE_UINT64, true)

    ADD_VALUE_TYPE("Half", CrateDataTypeId::CRATE_DATA_TYPE_HALF, true)
    ADD_VALUE_TYPE("Float", CrateDataTypeId::CRATE_DATA_TYPE_FLOAT, true)
    ADD_VALUE_TYPE("Double", CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE, true)

    ADD_VALUE_TYPE("String", CrateDataTypeId::CRATE_DATA_TYPE_STRING, true)
    ADD_VALUE_TYPE("Token", CrateDataTypeId::CRATE_DATA_TYPE_TOKEN, true)
    ADD_VALUE_TYPE("AssetPath", CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH,
                   true)

    ADD_VALUE_TYPE("Matrix2d", CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D, true)
    ADD_VALUE_TYPE("Matrix3d", CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D, true)
    ADD_VALUE_TYPE("Matrix4d", CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D, true)

    ADD_VALUE_TYPE("Quatd", CrateDataTypeId::CRATE_DATA_TYPE_QUATD, true)
    ADD_VALUE_TYPE("Quatf", CrateDataTypeId::CRATE_DATA_TYPE_QUATF, true)
    ADD_VALUE_TYPE("Quath", CrateDataTypeId::CRATE_DATA_TYPE_QUATH, true)

    ADD_VALUE_TYPE("Vec2d", CrateDataTypeId::CRATE_DATA_TYPE_VEC2D, true)
    ADD_VALUE_TYPE("Vec2f", CrateDataTypeId::CRATE_DATA_TYPE_VEC2F, true)
    ADD_VALUE_TYPE("Vec2h", CrateDataTypeId::CRATE_DATA_TYPE_VEC2H, true)
    ADD_VALUE_TYPE("Vec2i", CrateDataTypeId::CRATE_DATA_TYPE_VEC2I, true)

    ADD_VALUE_TYPE("Vec3d", CrateDataTypeId::CRATE_DATA_TYPE_VEC3D, true)
    ADD_VALUE_TYPE("Vec3f", CrateDataTypeId::CRATE_DATA_TYPE_VEC3F, true)
    ADD_VALUE_TYPE("Vec3h", CrateDataTypeId::CRATE_DATA_TYPE_VEC3H, true)
    ADD_VALUE_TYPE("Vec3i", CrateDataTypeId::CRATE_DATA_TYPE_VEC3I, true)

    ADD_VALUE_TYPE("Vec4d", CrateDataTypeId::CRATE_DATA_TYPE_VEC4D, true)
    ADD_VALUE_TYPE("Vec4f", CrateDataTypeId::CRATE_DATA_TYPE_VEC4F, true)
    ADD_VALUE_TYPE("Vec4h", CrateDataTypeId::CRATE_DATA_TYPE_VEC4H, true)
    ADD_VALUE_TYPE("Vec4i", CrateDataTypeId::CRATE_DATA_TYPE_VEC4I, true)

    // Non-array types.

    //
    // commented = TODO
    //
    ADD_VALUE_TYPE("Dictionary", CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY,
                   false)

    ADD_VALUE_TYPE("TokenListOp",
                   CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP, false)
    ADD_VALUE_TYPE("StringListOp",
                   CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP, false)
    ADD_VALUE_TYPE("PathListOp", CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP,
                   false)
    ADD_VALUE_TYPE("ReferenceListOp",
                   CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP, false)
    ADD_VALUE_TYPE("IntListOp", CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP,
                   false)
    ADD_VALUE_TYPE("Int64ListOp",
                   CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP, false)
    ADD_VALUE_TYPE("UIntListOp", CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP,
                   false)
    ADD_VALUE_TYPE("UInt64ListOp",
                   CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP, false)

    ADD_VALUE_TYPE("PathVector", CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR,
                   false)
    ADD_VALUE_TYPE("TokenVector", CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR,
                   false)

    ADD_VALUE_TYPE("Specifier", CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER,
                   false)
    ADD_VALUE_TYPE("Permission", CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION,
                   false)
    ADD_VALUE_TYPE("Variability", CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY,
                   false)

    ADD_VALUE_TYPE("VariantSelectionMap",
                   CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP,
                   false)
    ADD_VALUE_TYPE("TimeSamples", CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES,
                   false)
    ADD_VALUE_TYPE("Payload", CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD, false)
    ADD_VALUE_TYPE("DoubleVector",
                   CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE_VECTOR, false)
    ADD_VALUE_TYPE("LayerOffsetVector",
                   CrateDataTypeId::CRATE_DATA_TYPE_LAYER_OFFSET_VECTOR, false)
    ADD_VALUE_TYPE("StringVector",
                   CrateDataTypeId::CRATE_DATA_TYPE_STRING_VECTOR, false)
    ADD_VALUE_TYPE("ValueBlock", CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK,
                   false)
    ADD_VALUE_TYPE("Value", CrateDataTypeId::CRATE_DATA_TYPE_VALUE, false)
    ADD_VALUE_TYPE("UnregisteredValue",
                   CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE, false)
    ADD_VALUE_TYPE("UnregisteredValueListOp",
                   CrateDataTypeId::CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP,
                   false)
    ADD_VALUE_TYPE("PayloadListOp",
                   CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP, false)
    ADD_VALUE_TYPE("TimeCode", CrateDataTypeId::CRATE_DATA_TYPE_TIME_CODE, true)
  }
#undef ADD_VALUE_TYPE

  if (type_id < 0) {
    return nonstd::make_unexpected("Unknown type id: " +
                                   std::to_string(type_id));
  }

  if (!table.count(static_cast<uint32_t>(type_id))) {
    // Invalid or unsupported.
    return nonstd::make_unexpected("Unknown or unspported type id: " +
                                   std::to_string(type_id));
  }

  return table.at(static_cast<uint32_t>(type_id));
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

std::string GetCrateDataTypeRepr(CrateDataType dty) {
  auto tyRet = GetCrateDataType(static_cast<int32_t>(dty.dtype_id));
  if (!tyRet) {
    return "[Invalid]";
  }

  const CrateDataType ty = tyRet.value();

  std::stringstream ss;
  ss << "CrateDataType: " << ty.name << "("
     << static_cast<uint32_t>(ty.dtype_id)
     << "), supports_array = " << ty.supports_array;
  return ss.str();
}

std::string GetCrateDataTypeName(int32_t type_id) {
  auto tyRet = GetCrateDataType(type_id);
  if (!tyRet) {
    return "[Invalid]";
  }

  const CrateDataType dty = tyRet.value();
  return dty.name;
}

std::string GetCrateDataTypeName(CrateDataTypeId did) {
  return GetCrateDataTypeName(static_cast<int32_t>(did));
}

std::string CrateValue::GetTypeName() const { return value_.type_name(); }

uint32_t CrateValue::GetTypeId() const { return value_.type_id(); }

}  // namespace crate
}  // namespace tinyusdz
