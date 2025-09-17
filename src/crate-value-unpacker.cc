// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Value unpacking operations for Crate reader - Implementation

#include "crate-value-unpacker.hh"
#include "crate-reader.hh"
#include "common-macros.inc"
#include "str-util.hh"
#include "tiny-format.hh"

#define kTag "[CrateValueUnpacker]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK(memory_manager_, (__nbytes), kTag)

#define REDUCE_MEMORY_USAGE(__nbytes) \
  memory_manager_.Release(__nbytes)

// Use existing macros from common-macros.inc
#undef PUSH_ERROR
#define PUSH_ERROR(__msg) PushError(__msg)

namespace tinyusdz {
namespace crate {

bool CrateValueUnpacker::UnpackInlinedValueRep(const crate::ValueRep &rep,
                                               crate::CrateValue *value,
                                               std::string* err) {
  if (!rep.IsInlined()) {
    PUSH_ERROR("ValueRep must be inlined value representation.");
    return false;
  }

  DCOUT("ValueRep type value = " << rep.GetType());
  auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
    return false;
  }

  const auto dty = tyRet.value();

  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID: {
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {
      bool v = rep.GetPayload() ? true : false;
      value->Set(v);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR: {
      uint8_t v = static_cast<uint8_t>(rep.GetPayload() & 0xff);
      value->Set(v);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT: {
      int32_t v = static_cast<int32_t>(rep.GetPayload() & 0xffffffff);
      value->Set(v);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT: {
      uint32_t v = static_cast<uint32_t>(rep.GetPayload() & 0xffffffff);
      value->Set(v);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64: {
      int64_t v = static_cast<int64_t>(rep.GetPayload());
      value->Set(v);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64: {
      uint64_t v = rep.GetPayload();
      value->Set(v);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF: {
      uint16_t h = static_cast<uint16_t>(rep.GetPayload() & 0xffff);
      value::half hval;
      hval.value = h;
      value->Set(hval);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT: {
      uint32_t ui = static_cast<uint32_t>(rep.GetPayload() & 0xffffffff);
      float f;
      memcpy(&f, &ui, sizeof(float));
      value->Set(f);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE: {
      uint64_t payload = rep.GetPayload();
      double d;
      memcpy(&d, &payload, sizeof(double));
      value->Set(d);
      return true;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN: {
      // TODO: Implement token lookup - requires access to reader's token table
      PUSH_ERROR("Token unpacking not yet implemented in modular value unpacker");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      // TODO: Implement string lookup - requires access to reader's string table
      PUSH_ERROR("String unpacking not yet implemented in modular value unpacker");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH: {
      // TODO: Implement asset path lookup - requires access to reader's string table
      PUSH_ERROR("AssetPath unpacking not yet implemented in modular value unpacker");
      return false;
    }
    default: {
      PUSH_ERROR("TODO: Inlined '" + crate::GetCrateDataTypeName(dty.dtype_id) + 
                "' data is not yet implemented.");
      return false;
    }
  }

  return false;
}

bool CrateValueUnpacker::UnpackValueRep(const crate::ValueRep &rep,
                                         crate::CrateValue *value,
                                         std::string* err) {
  if (rep.IsInlined()) {
    return UnpackInlinedValueRep(rep, value, err);
  }

  DCOUT("ValueRep type value = " << rep.GetType());
  auto tyRet = crate::GetCrateDataType(rep.GetType());
  if (!tyRet) {
    PUSH_ERROR(tyRet.error());
    return false;
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
  if (!_sr->seek_set(offset)) {
    PUSH_ERROR("Invalid offset.");
    return false;
  }

  switch (dty.dtype_id) {
    case crate::CrateDataTypeId::NumDataTypes:
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID: {
      DCOUT("dtype_id = " << to_string(uint32_t(dty.dtype_id)));
      PUSH_ERROR("`Invalid` DataType.");
      return false;
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      NON_ARRAY_UNSUPPORTED_CHECK(dty)

      if (rep.IsArray()) {
        std::vector<bool> v;

        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        // bool is encoded as 8bit value.
        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        CHECK_MEMORY_USAGE(n * sizeof(uint8_t));

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

        value->Set(v);
        return true;
      } else {
        bool v;
        if (!_sr->read1(reinterpret_cast<uint8_t *>(&v))) {
          PUSH_ERROR("Failed to read bool value.");
          return false;
        }
        value->Set(v);
        return true;
      }
    }
    case crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING: {
      COMPRESS_UNSUPPORTED_CHECK(dty)
      
      if (rep.IsArray()) {
        std::vector<std::string> v;

        if (rep.GetPayload() == 0) { // empty array
          value->Set(v);
          return true;
        }

        uint64_t n;
        if (!_sr->read8(&n)) {
          PUSH_ERROR("Failed to read the number of array elements.");
          return false;
        }

        v.resize(static_cast<size_t>(n));
        for (size_t i = 0; i < n; i++) {
          uint32_t idx;
          if (!_sr->read4(&idx)) {
            PUSH_ERROR("Failed to read string index.");
            return false;
          }
          // TODO: Implement string lookup
          v[i] = "TODO_STRING_" + std::to_string(idx);
        }

        value->Set(v);
        return true;
      } else {
        uint32_t idx;
        if (!_sr->read4(&idx)) {
          PUSH_ERROR("Failed to read string index.");
          return false;
        }
        // TODO: Implement string lookup
        std::string str = "TODO_STRING_" + std::to_string(idx);
        value->Set(str);
        return true;
      }
    }
    default: {
      TODO_IMPLEMENT(dty);
    }
  }

#undef TODO_IMPLEMENT
#undef COMPRESS_UNSUPPORTED_CHECK
#undef NON_ARRAY_UNSUPPORTED_CHECK
#undef ARRAY_UNSUPPORTED_CHECK

  // Never should reach here.
  return false;
}

} // namespace crate
} // namespace tinyusdz