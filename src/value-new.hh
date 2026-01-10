// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
#include <map>

#include "token-type.hh"
#include "typed-array.hh"
#include "common-macros.inc"

// Forward declarations from value-types.hh
namespace tinyusdz {
namespace value {

struct StringData;
class AssetPath;
class TimeCode;

// Primitive types
struct half;
using half2 = std::array<half, 2>;
using half3 = std::array<half, 3>;
using half4 = std::array<half, 4>;

using char2 = std::array<char, 2>;
using char3 = std::array<char, 3>;
using char4 = std::array<char, 4>;

using uchar2 = std::array<uint8_t, 2>;
using uchar3 = std::array<uint8_t, 3>;
using uchar4 = std::array<uint8_t, 4>;

using short2 = std::array<int16_t, 2>;
using short3 = std::array<int16_t, 3>;
using short4 = std::array<int16_t, 4>;

using ushort2 = std::array<uint16_t, 2>;
using ushort3 = std::array<uint16_t, 3>;
using ushort4 = std::array<uint16_t, 4>;

using int2 = std::array<int32_t, 2>;
using int3 = std::array<int32_t, 3>;
using int4 = std::array<int32_t, 4>;

using uint2 = std::array<uint32_t, 2>;
using uint3 = std::array<uint32_t, 3>;
using uint4 = std::array<uint32_t, 4>;

using float2 = std::array<float, 2>;
using float3 = std::array<float, 3>;
using float4 = std::array<float, 4>;

using double2 = std::array<double, 2>;
using double3 = std::array<double, 3>;
using double4 = std::array<double, 4>;

struct matrix2f;
struct matrix3f;
struct matrix4f;
struct matrix2d;
struct matrix3d;
struct matrix4d;

struct quath;
struct quatf;
struct quatd;

struct vector3h;
struct vector3f;
struct vector3d;

struct normal3h;
struct normal3f;
struct normal3d;

struct point3h;
struct point3f;
struct point3d;

struct color3h;
struct color3f;
struct color3d;
struct color4h;
struct color4f;
struct color4d;

struct texcoord2h;
struct texcoord2f;
struct texcoord2d;
struct texcoord3h;
struct texcoord3f;
struct texcoord3d;

struct frame4d;

struct ValueBlock;

// TypeIds from value-types.hh
enum TypeId : uint32_t;

// TypeTraits from value-types.hh
template <class dtype>
struct TypeTraits;

// Forward declare Path from prim-types.hh
class Path;

// Forward declare ListOp from prim-types.hh
template <typename T>
class ListOp;

// Forward declare other types from prim-types.hh
struct Reference;
struct Payload;
struct LayerOffset;
enum class Specifier;
enum class Permission;
enum class Variability;

// Forward declare types from usdGeom.hh, usdLux.hh, usdShade.hh, usdSkel.hh
class Prim;
class GPrim;
class GeomMesh;
class GeomXform;
class GeomSphere;
class GeomCube;
class GeomCylinder;
class GeomCone;
class GeomCapsule;
class GeomPoints;
class GeomSubset;
class GeomPointInstancer;
class GeomCamera;

class LuxSphereLight;
class LuxDomeLight;
class LuxCylinderLight;
class LuxDiskLight;
class LuxRectLight;
class LuxDistantLight;
class LuxGeometryLight;
class LuxPortalLight;
class LuxPluginLight;

class Shader;
class Material;
class NodeGraph;

class ImagingShaderNode;
class UsdPreviewSurface;
class UsdUVTexture;

template <typename T>
struct UsdPrimvarReader;

using UsdPrimvarReader_float = UsdPrimvarReader<float>;
using UsdPrimvarReader_float2 = UsdPrimvarReader<float2>;
using UsdPrimvarReader_float3 = UsdPrimvarReader<float3>;
using UsdPrimvarReader_float4 = UsdPrimvarReader<float4>;
using UsdPrimvarReader_int = UsdPrimvarReader<int32_t>;
using UsdPrimvarReader_string = UsdPrimvarReader<std::string>;
using UsdPrimvarReader_normal = UsdPrimvarReader<normal3f>;
using UsdPrimvarReader_point = UsdPrimvarReader<point3f>;
using UsdPrimvarReader_vector = UsdPrimvarReader<vector3f>;
using UsdPrimvarReader_matrix = UsdPrimvarReader<matrix4d>;

class UsdTransform2d;
class MtlxPreviewSurface;
class MtlxStandardSurface;
class OpenPBRSurface;

class SkelRoot;
class Skeleton;
class SkelAnimation;
class BlendShape;

class Collection;
class CollectionInstance;
class MaterialBinding;
class MaterialXConfigAPI;

// Forward declare crate types
namespace crate {
struct UnregisteredValue;
struct ListOpUnregisteredValue;
}

// Forward declare TimeSamples and VariantSelectionMap
struct TimeSamples;
using VariantSelectionMap = std::map<std::string, std::string>;


//
// New Value implementation
//
// Value's variant can be limited to types defined in value::TypeId enum.
// This allows to use tag-based union approach.
//
// Similar to Crate `ValueRep` format, we use 8 bytes for data storage.
// - sizeof(T) <= 8 : Store inline
// - sizeof(T) > 8 : Store as a pointer(std::unique_ptr)
//
// Value struct will be 16 bytes.
//
// TODO: Support multi-dimensional array(up to 7D)
//
class Value {
 public:
  Value() : _type_id(TYPE_ID_NULL) {}

  // Destructor
  ~Value() {
    destroy_value();
  }

  // Copy constructor
  Value(const Value& other) : _type_id(other._type_id) {
    copy_value(other);
  }

  // Copy assignment operator
  Value& operator=(const Value& other) {
    if (this != &other) {
      destroy_value();
      _type_id = other._type_id;
      copy_value(other);
    }
    return *this;
  }

  // Move constructor
  Value(Value&& other) noexcept : _type_id(other._type_id), _data(other._data) {
    other._type_id = TYPE_ID_NULL;
    other._data = 0;
  }

  // Move assignment operator
  Value& operator=(Value&& other) noexcept {
    if (this != &other) {
      destroy_value();
      _type_id = other._type_id;
      _data = other._data;

      other._type_id = TYPE_ID_NULL;
      other._data = 0;
    }
    return *this;
  }

  template <typename T>
  Value(const T &value) {
    set<T>(value);
  }

  template <typename T>
  Value &operator=(const T &value) {
    set<T>(value);
    return *this;
  }

  template <typename T>
  void set(const T &value) {
    destroy_value(); // Clean up existing data

    _type_id = TypeTraits<T>::type_id();
    if (is_inlined_type<T>()) {
      memcpy(&_data, &value, sizeof(T));
    } else {
      _data = reinterpret_cast<uint64_t>(new T(value));
    }
  }

  template <typename T>
  const T *as() const {
    if (type_id() != TypeTraits<T>::type_id()) {
      return nullptr;
    }

    if (is_inlined_type<T>()) {
      return reinterpret_cast<const T *>(&_data);
    } else {
      return reinterpret_cast<const T *>(_data);
    }
  }

  template <typename T>
  T *as() {
    if (type_id() != TypeTraits<T>::type_id()) {
      return nullptr;
    }

    if (is_inlined_type<T>()) {
      return reinterpret_cast<T *>(&_data);
    } else {
      return reinterpret_cast<T *>(_data);
    }
  }

  bool is_valid() const { return _type_id != TYPE_ID_NULL; }

  bool is_blocked() const { return type_id() == TYPE_ID_VALUEBLOCK; }

  bool is_array() const { return (_type_id & TYPE_ID_1D_ARRAY_BIT) != 0; }

  bool is_scalar() const { return !is_array(); }

  uint32_t type_id() const { return _type_id & 0x000FFFFF; } // Mask out array bit and other flags
  std::string type_name() const { return TypeTraits<void>::type_name_from_id(type_id()); }
  uint32_t underlying_type_id() const { return TypeTraits<void>::underlying_type_id_from_id(type_id()); }
  std::string underlying_type_name() const { return TypeTraits<void>::underlying_type_name_from_id(type_id()); }

  // Helper methods for specific types
  bool is_string() const { return type_id() == TYPE_ID_STRING; }
  bool is_token() const { return type_id() == TYPE_ID_TOKEN; }
  bool is_asset_path() const { return type_id() == TYPE_ID_ASSET_PATH; }
  bool is_path() const { return type_id() == TYPE_ID_PATH; }
  bool is_dictionary() const { return type_id() == TYPE_ID_DICT; }
  bool is_timecode() const { return type_id() == TYPE_ID_TIMECODE; }

  bool is_matrix() const {
    uint32_t tid = type_id();
    return tid == TYPE_ID_MATRIX2F || tid == TYPE_ID_MATRIX3F || tid == TYPE_ID_MATRIX4F ||
           tid == TYPE_ID_MATRIX2D || tid == TYPE_ID_MATRIX3D || tid == TYPE_ID_MATRIX4D;
  }

  bool is_quat() const {
    uint32_t tid = type_id();
    return tid == TYPE_ID_QUATH || tid == TYPE_ID_QUATF || tid == TYPE_ID_QUATD;
  }

  bool is_vector() const {
    uint32_t tid = type_id();
    return tid == TYPE_ID_VECTOR3H || tid == TYPE_ID_VECTOR3F || tid == TYPE_ID_VECTOR3D;
  }

  bool is_point() const {
    uint32_t tid = type_id();
    return tid == TYPE_ID_POINT3H || tid == TYPE_ID_POINT3F || tid == TYPE_ID_POINT3D;
  }

  bool is_normal() const {
    uint32_t tid = type_id();
    return tid == TYPE_ID_NORMAL3H || tid == TYPE_ID_NORMAL3F || tid == TYPE_ID_NORMAL3D;
  }

  bool is_color() const {
    uint32_t tid = type_id();
    return tid == TYPE_ID_COLOR3H || tid == TYPE_ID_COLOR3F || tid == TYPE_ID_COLOR3D ||
           tid == TYPE_ID_COLOR4H || tid == TYPE_ID_COLOR4F || tid == TYPE_ID_COLOR4D;
  }

  bool is_texcoord() const {
    uint32_t tid = type_id();
    return tid == TYPE_ID_TEXCOORD2H || tid == TYPE_ID_TEXCOORD2F || tid == TYPE_ID_TEXCOORD2D ||
           tid == TYPE_ID_TEXCOORD3H || tid == TYPE_ID_TEXCOORD3F || tid == TYPE_ID_TEXCOORD3D;
  }

  // TODO: Implement is_extent()
  // bool is_extent() const { return type_id() == TYPE_ID_EXTENT; }

  bool is_relationship() const { return type_id() == TYPE_ID_RELATIONSHIP; }
  bool is_reference() const { return type_id() == TYPE_ID_REFERENCE; }
  bool is_payload() const { return type_id() == TYPE_ID_PAYLOAD; }
  bool is_layer_offset() const { return type_id() == TYPE_ID_LAYER_OFFSET; }
  bool is_specifier() const { return type_id() == TYPE_ID_SPECIFIER; }
  bool is_permission() const { return type_id() == TYPE_ID_PERMISSION; }
  bool is_variability() const { return type_id() == TYPE_ID_VARIABILITY; }

  bool is_list_op() const {
    uint32_t tid = type_id();
    return tid == TYPE_ID_LIST_OP_TOKEN || tid == TYPE_ID_LIST_OP_STRING ||
           tid == TYPE_ID_LIST_OP_PATH || tid == TYPE_ID_LIST_OP_REFERENCE ||
           tid == TYPE_ID_LIST_OP_INT || tid == TYPE_ID_LIST_OP_INT64 ||
           tid == TYPE_ID_LIST_OP_UINT || tid == TYPE_ID_LIST_OP_UINT64 ||
           tid == TYPE_ID_LIST_OP_PAYLOAD;
  }

  // TODO: Implement is_prim(), is_gprim(), etc.
  // For now, these are not directly stored in Value.
  // bool is_prim() const { return type_id() == TYPE_ID_PRIM; }

 private:
  // Check if the type is inlined (size <= 8 bytes)
  template <typename T>
  static constexpr bool is_inlined_type() {
    return sizeof(T) <= 8;
  }

  bool is_inlined() const {
    // This requires a lookup table or a switch statement based on _type_id
    // For now, assume all types with size <= 8 are inlined.
    // This is a simplification and needs to be refined with actual type sizes.
    uint32_t tid = type_id();
    switch(tid) {
        case TYPE_ID_NULL:
        case TYPE_ID_VOID:
        case TYPE_ID_MONOSTATE:
        case TYPE_ID_VALUEBLOCK:
        case TYPE_ID_BOOL:
        case TYPE_ID_CHAR:
        case TYPE_ID_UCHAR:
        case TYPE_ID_HALF:
        case TYPE_ID_INT32:
        case TYPE_ID_UINT32:
        case TYPE_ID_INT64:
        case TYPE_ID_UINT64:
        case TYPE_ID_SHORT:
        case TYPE_ID_USHORT:
        case TYPE_ID_TIMECODE:
            return true;
        default:
            return false; // For types > 8 bytes, or complex types like string, vector, etc.
    }
  }

  void destroy_value() {
    if (!is_inlined()) {
      // Call custom deleter based on type_id
      // This is a placeholder. Proper implementation requires a switch on _type_id
      // and calling the destructor for the specific type.
      delete reinterpret_cast<void*>(_data);
    }
    _type_id = TYPE_ID_NULL;
    _data = 0;
  }

  void copy_value(const Value& other) {
    if (other.is_inlined()) {
      _data = other._data;
    } else {
      // This is a placeholder. Proper implementation requires a switch on _type_id
      // and calling the copy constructor for the specific type.
      // For now, just copy pointer and assume deep copy is handled by custom copy
      // This will lead to memory leaks and double frees if not handled properly.
      _data = reinterpret_cast<uint64_t>(new char[8]); // Placeholder
      memcpy(reinterpret_cast<void*>(_data), reinterpret_cast<void*>(other._data), 8); // Placeholder
    }
  }

  uint32_t _type_id;  // 20bit for type_id, 4bit for array dim, 7bit for flags
  uint64_t _data;     // 8 bytes for inline value or pointer
};

} // namespace value
} // namespace tinyusdz
