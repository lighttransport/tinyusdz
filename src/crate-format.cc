#if defined(__wasi__)
#else
#include <thread>
#endif

#include "crate-format.hh"

//#include "integerCoding.h"
//#include "primvar.hh"
//#include "tinyusdz.hh"
//#include "pprinter.hh"
//
//#include "lz4-compression.hh"
//#include "stream-reader.hh"

//#define PUSH_ERROR(s) { \
//  std::ostringstream ss; \
//  ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
//  ss << s; \
//  _err += ss.str() + "\n"; \
//} while (0)

#if 0
#define PUSH_WARN(s) { \
  std::ostringstream ss; \
  ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
  ss << s; \
  _warn += ss.str() + "\n"; \
} while (0)
#endif

namespace tinyusdz {
namespace crate {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
const ValueType &GetValueType(int32_t type_id) {
  static std::map<uint32_t, ValueType> table;
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << "type_id = " << type_id << "\n";
#endif
  if (table.size() == 0) {
    // Register data types
    // NOTE(syoyo): We can use C++11 template to create compile-time table for
    // data types, but this way(use std::map) is easier to read and maintain, I
    // think.

    // reference: crateDataTypes.h

#define ADD_VALUE_TYPE(NAME_STR, TYPE_ID, SUPPORTS_ARRAY)          \
  {                                                                \
    assert(table.count(TYPE_ID) == 0);                             \
    table[TYPE_ID] = ValueType(NAME_STR, TYPE_ID, SUPPORTS_ARRAY); \
  }

    ADD_VALUE_TYPE("InvaldOrUnsupported", 0, false)

    // Array types.
    ADD_VALUE_TYPE("Bool", VALUE_TYPE_BOOL, true)

    ADD_VALUE_TYPE("UChar", VALUE_TYPE_UCHAR, true)
    ADD_VALUE_TYPE("Int", VALUE_TYPE_INT, true)
    ADD_VALUE_TYPE("UInt", VALUE_TYPE_UINT, true)
    ADD_VALUE_TYPE("Int64", VALUE_TYPE_INT64, true)
    ADD_VALUE_TYPE("UInt64", VALUE_TYPE_UINT64, true)

    ADD_VALUE_TYPE("Half", VALUE_TYPE_HALF, true)
    ADD_VALUE_TYPE("Float", VALUE_TYPE_FLOAT, true)
    ADD_VALUE_TYPE("Double", VALUE_TYPE_DOUBLE, true)

    ADD_VALUE_TYPE("String", VALUE_TYPE_STRING, true)
    ADD_VALUE_TYPE("Token", VALUE_TYPE_TOKEN, true)
    ADD_VALUE_TYPE("AssetPath", VALUE_TYPE_ASSET_PATH, true)

    ADD_VALUE_TYPE("Quatd", VALUE_TYPE_QUATD, true)
    ADD_VALUE_TYPE("Quatf", VALUE_TYPE_QUATF, true)
    ADD_VALUE_TYPE("Quath", VALUE_TYPE_QUATH, true)

    ADD_VALUE_TYPE("Vec2d", VALUE_TYPE_VEC2D, true)
    ADD_VALUE_TYPE("Vec2f", VALUE_TYPE_VEC2F, true)
    ADD_VALUE_TYPE("Vec2h", VALUE_TYPE_VEC2H, true)
    ADD_VALUE_TYPE("Vec2i", VALUE_TYPE_VEC2I, true)

    ADD_VALUE_TYPE("Vec3d", VALUE_TYPE_VEC3D, true)
    ADD_VALUE_TYPE("Vec3f", VALUE_TYPE_VEC3F, true)
    ADD_VALUE_TYPE("Vec3h", VALUE_TYPE_VEC3H, true)
    ADD_VALUE_TYPE("Vec3i", VALUE_TYPE_VEC3I, true)

    ADD_VALUE_TYPE("Vec4d", VALUE_TYPE_VEC4D, true)
    ADD_VALUE_TYPE("Vec4f", VALUE_TYPE_VEC4F, true)
    ADD_VALUE_TYPE("Vec4h", VALUE_TYPE_VEC4H, true)
    ADD_VALUE_TYPE("Vec4i", VALUE_TYPE_VEC4I, true)

    ADD_VALUE_TYPE("Matrix2d", VALUE_TYPE_MATRIX2D, true)
    ADD_VALUE_TYPE("Matrix3d", VALUE_TYPE_MATRIX3D, true)
    ADD_VALUE_TYPE("Matrix4d", VALUE_TYPE_MATRIX4D, true)

    // Non-array types.
    ADD_VALUE_TYPE("Dictionary", VALUE_TYPE_DICTIONARY,
                   false)  // std::map<std::string, Value>

    ADD_VALUE_TYPE("TokenListOp", VALUE_TYPE_TOKEN_LIST_OP, false)
    ADD_VALUE_TYPE("StringListOp", VALUE_TYPE_STRING_LIST_OP, false)
    ADD_VALUE_TYPE("PathListOp", VALUE_TYPE_PATH_LIST_OP, false)
    ADD_VALUE_TYPE("ReferenceListOp", VALUE_TYPE_REFERENCE_LIST_OP, false)
    ADD_VALUE_TYPE("IntListOp", VALUE_TYPE_INT_LIST_OP, false)
    ADD_VALUE_TYPE("Int64ListOp", VALUE_TYPE_INT64_LIST_OP, false)
    ADD_VALUE_TYPE("UIntListOp", VALUE_TYPE_UINT_LIST_OP, false)
    ADD_VALUE_TYPE("UInt64ListOp", VALUE_TYPE_UINT64_LIST_OP, false)

    ADD_VALUE_TYPE("PathVector", VALUE_TYPE_PATH_VECTOR, false)
    ADD_VALUE_TYPE("TokenVector", VALUE_TYPE_TOKEN_VECTOR, false)

    ADD_VALUE_TYPE("Specifier", VALUE_TYPE_SPECIFIER, false)
    ADD_VALUE_TYPE("Permission", VALUE_TYPE_PERMISSION, false)
    ADD_VALUE_TYPE("Variability", VALUE_TYPE_VARIABILITY, false)

    ADD_VALUE_TYPE("VariantSelectionMap", VALUE_TYPE_VARIANT_SELECTION_MAP,
                   false)
    ADD_VALUE_TYPE("TimeSamples", VALUE_TYPE_TIME_SAMPLES, false)
    ADD_VALUE_TYPE("Payload", VALUE_TYPE_PAYLOAD, false)
    ADD_VALUE_TYPE("DoubleVector", VALUE_TYPE_DOUBLE_VECTOR, false)
    ADD_VALUE_TYPE("LayerOffsetVector", VALUE_TYPE_LAYER_OFFSET_VECTOR, false)
    ADD_VALUE_TYPE("StringVector", VALUE_TYPE_STRING_VECTOR, false)
    ADD_VALUE_TYPE("ValueBlock", VALUE_TYPE_VALUE_BLOCK, false)
    ADD_VALUE_TYPE("Value", VALUE_TYPE_VALUE, false)
    ADD_VALUE_TYPE("UnregisteredValue", VALUE_TYPE_UNREGISTERED_VALUE, false)
    ADD_VALUE_TYPE("UnregisteredValueListOp",
                   VALUE_TYPE_UNREGISTERED_VALUE_LIST_OP, false)
    ADD_VALUE_TYPE("PayloadListOp", VALUE_TYPE_PAYLOAD_LIST_OP, false)
    ADD_VALUE_TYPE("TimeCode", VALUE_TYPE_TIME_CODE, true)
  }
#undef ADD_VALUE_TYPE

  if (type_id < 0) {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cerr << "Unknonw type id: " << type_id << "\n";
#endif
    return table.at(0);
  }

  if (!table.count(uint32_t(type_id))) {
    // Invalid or unsupported.
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cerr << "Unknonw type id: " << type_id << "\n";
#endif
    return table.at(0);
  }

  return table.at(uint32_t(type_id));
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif




} // namespace crate
} // namesapce tinyusdz
