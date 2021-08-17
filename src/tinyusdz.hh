/*
Copyright (c) 2019 - Present, Syoyo Fujita.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Syoyo Fujita nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef TINYUSDZ_HH_
#define TINYUSDZ_HH_

#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

#ifdef TINYUSDZ_ANDROID_LOAD_FROM_ASSETS
#include <android/asset_manager.h>
#endif

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
#include <iostream>  // dbg
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// TODO: Use std:: version for C++17
#include "nonstd/optional.hpp"
#include "nonstd/variant.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "prim-types.hh"

namespace tinyusdz {

#ifdef TINYUSDZ_ANDROID_LOAD_FROM_ASSETS
extern AAssetManager *asset_manager;
#endif

constexpr int version_major = 0;
constexpr int version_minor = 7;
constexpr int version_micro = 0;

// Simple image class.
// No colorspace conversion will be applied when decoding image data(e.g. from
// .jpg, .png).
// TODO(syoyo): Add option to decode image into linear space.
struct Image {
  std::string uri;  // filename or uri;

  int width{-1};     // -1 = invalid
  int height{-1};    // -1 = invalid
  int channels{-1};  // Image channels. 3=RGB, 4=RGBA. -1 = invalid
  int bpp{-1};       // bits per pixel. 8=LDR, 16=HDR

  std::vector<uint8_t> data;
};

#if 0
//
// Colum-major order(e.g. employed in OpenGL).
// For example, 12th([3][0]), 13th([3][1]), 14th([3][2]) element corresponds to the translation.
//
template <typename T, size_t N>
struct Matrix {
  T m[N][N];
  constexpr static uint32_t n = N;
};

template <typename T, size_t N>
void Identity(Matrix<T, N> *mat) {
  memset(mat->m, 0, sizeof(T) * N * N);
  for (size_t i = 0; i < N; i++) {
    mat->m[i][i] = static_cast<T>(1);
  }
};

// ret = m x n
template <typename T, size_t N>
Matrix<T, N> Mult(Matrix<T, N> &m, Matrix<T, N> &n) {
  Matrix<T, N> ret;
  memset(ret.m, 0, sizeof(T) * N * N);

  for (size_t j = 0; j < N; j++) {
    for (size_t i = 0; i < N; i++) {
      T value = static_cast<T>(0);
      for (size_t k = 0; k < N; k++) {
        value += m.m[k][i] * n.m[j][k];
      }
      ret.m[j][i] = value;
    }
  }

  return ret;
};



using Matrix2f = Matrix<float, 2>;
using Matrix2d = Matrix<double, 2>;
using Matrix3f = Matrix<float, 3>;
using Matrix3d = Matrix<double, 3>;
using Matrix4f = Matrix<float, 4>;
using Matrix4d = Matrix<double, 4>;

using Vec4i = std::array<int32_t, 4>;
using Vec3i = std::array<int32_t, 3>;
using Vec2i = std::array<int32_t, 2>;

// Use uint16_t for storage of half type.
// Need to decode/encode value through half converter functions
using Vec4h = std::array<uint16_t, 4>;
using Vec3h = std::array<uint16_t, 3>;
using Vec2h = std::array<uint16_t, 2>;

using Vec4f = std::array<float, 4>;
using Vec3f = std::array<float, 3>;
using Vec2f = std::array<float, 2>;

using Vec4d = std::array<double, 4>;
using Vec3d = std::array<double, 3>;
using Vec2d = std::array<double, 2>;

template<typename T>
struct Quat
{
  std::array<T, 4> v;
};

using Quath = Quat<uint16_t>;
using Quatf = Quat<float>;
using Quatd = Quat<double>;
//using Quaternion = Quat<double>;  // Storage layout is same with Quadd,
                                           // so we can delete this

// TODO(syoyo): Range, Interval, Rect2i, Frustum, MultiInterval

/*
#define VT_GFRANGE_VALUE_TYPES                 \
((      GfRange3f,           Range3f        )) \
((      GfRange3d,           Range3d        )) \
((      GfRange2f,           Range2f        )) \
((      GfRange2d,           Range2d        )) \
((      GfRange1f,           Range1f        )) \
((      GfRange1d,           Range1d        ))

#define VT_RANGE_VALUE_TYPES                   \
    VT_GFRANGE_VALUE_TYPES                     \
((      GfInterval,          Interval       )) \
((      GfRect2i,            Rect2i         ))

#define VT_STRING_VALUE_TYPES            \
((      std::string,           String )) \
((      TfToken,               Token  ))

#define VT_QUATERNION_VALUE_TYPES           \
((      GfQuath,             Quath ))       \
((      GfQuatf,             Quatf ))       \
((      GfQuatd,             Quatd ))       \
((      GfQuaternion,        Quaternion ))

#define VT_NONARRAY_VALUE_TYPES                 \
((      GfFrustum,           Frustum))          \
((      GfMultiInterval,     MultiInterval))

*/

#endif

enum ListEditQual {
  LIST_EDIT_QUAL_RESET_TO_EXPLICIT,	// "unqualified"(no qualifier)
  LIST_EDIT_QUAL_APPEND, // "append"
  LIST_EDIT_QUAL_ADD, // "add"
  LIST_EDIT_QUAL_DELETE, // "delete"
  LIST_EDIT_QUAL_PREPEND // "prepend"
};

static std::string to_string(ListEditQual qual)
{
  if (qual == LIST_EDIT_QUAL_RESET_TO_EXPLICIT) {
    return "";
  } else if (qual == LIST_EDIT_QUAL_APPEND) {
    return "append";
  } else if (qual == LIST_EDIT_QUAL_ADD) {
    return "add";
  } else if (qual == LIST_EDIT_QUAL_PREPEND) {
    return "prepend";
  } else if (qual == LIST_EDIT_QUAL_DELETE) {
    return "delete";
  } else {
    return "??? invalid ListEditQual";
  }
}

template <typename T>
class ListOp {
 public:
  ListOp() : is_explicit(false) {}

  void ClearAndMakeExplicit() {
    explicit_items.clear();
    added_items.clear();
    prepended_items.clear();
    appended_items.clear();
    deleted_items.clear();
    ordered_items.clear();

    is_explicit = true;
  }

  bool HasExplicitItems() const { return explicit_items.size(); }

  bool HasAddedItems() const { return added_items.size(); }

  bool HasPrependedItems() const { return prepended_items.size(); }

  bool HasAppendedItems() const { return appended_items.size(); }

  bool HasDeletedItems() const { return deleted_items.size(); }

  bool HasOrderedItems() const { return deleted_items.size(); }

  const std::vector<T> &GetExplicitItems() const { return explicit_items; }

  const std::vector<T> &GetAddedItems() const { return added_items; }

  const std::vector<T> &GetPrependedItems() const { return prepended_items; }

  const std::vector<T> &GetAppendedItems() const { return appended_items; }

  const std::vector<T> &GetDeletedItems() const { return deleted_items; }

  const std::vector<T> &GetOrderedItems() const { return ordered_items; }

  void SetExplicitItems(const std::vector<T> &v) { explicit_items = v; }

  void SetAddedItems(const std::vector<T> &v) { added_items = v; }

  void SetPrependedItems(const std::vector<T> &v) { prepended_items = v; }

  void SetAppendedItems(const std::vector<T> &v) { appended_items = v; }

  void SetDeletedItems(const std::vector<T> &v) { deleted_items = v; }

  void SetOrderedItems(const std::vector<T> &v) { ordered_items = v; }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  void Print() const {
    std::cout << "is_explicit:" << is_explicit << "\n";
    std::cout << "# of explicit_items" << explicit_items.size() << "\n";
    std::cout << "# of added_items" << added_items.size() << "\n";
    std::cout << "# of prepended_items" << prepended_items.size() << "\n";
    std::cout << "# of deleted_items" << deleted_items.size() << "\n";
    std::cout << "# of ordered_items" << ordered_items.size() << "\n";
  }
#endif

 private:
  bool is_explicit{false};
  std::vector<T> explicit_items;
  std::vector<T> added_items;
  std::vector<T> prepended_items;
  std::vector<T> appended_items;
  std::vector<T> deleted_items;
  std::vector<T> ordered_items;
};

struct ListOpHeader {
  enum Bits {
    IsExplicitBit = 1 << 0,
    HasExplicitItemsBit = 1 << 1,
    HasAddedItemsBit = 1 << 2,
    HasDeletedItemsBit = 1 << 3,
    HasOrderedItemsBit = 1 << 4,
    HasPrependedItemsBit = 1 << 5,
    HasAppendedItemsBit = 1 << 6
  };

  ListOpHeader() : bits(0) {}

  explicit ListOpHeader(uint8_t b) : bits(b) {}

  explicit ListOpHeader(ListOpHeader const &op) : bits(0) {
    bits |= op.IsExplicit() ? IsExplicitBit : 0;
    bits |= op.HasExplicitItems() ? HasExplicitItemsBit : 0;
    bits |= op.HasAddedItems() ? HasAddedItemsBit : 0;
    bits |= op.HasPrependedItems() ? HasPrependedItemsBit : 0;
    bits |= op.HasAppendedItems() ? HasAppendedItemsBit : 0;
    bits |= op.HasDeletedItems() ? HasDeletedItemsBit : 0;
    bits |= op.HasOrderedItems() ? HasOrderedItemsBit : 0;
  }

  bool IsExplicit() const { return bits & IsExplicitBit; }

  bool HasExplicitItems() const { return bits & HasExplicitItemsBit; }
  bool HasAddedItems() const { return bits & HasAddedItemsBit; }
  bool HasPrependedItems() const { return bits & HasPrependedItemsBit; }
  bool HasAppendedItems() const { return bits & HasAppendedItemsBit; }
  bool HasDeletedItems() const { return bits & HasDeletedItemsBit; }
  bool HasOrderedItems() const { return bits & HasOrderedItemsBit; }

  uint8_t bits;
};

///
/// We don't need the performance for USDZ, so use naiive implementation
/// to represent Path.
/// Path is something like Unix path, delimited by `/`, ':' and '.'
///
/// Example:
///
/// `/muda/bora.dora` : prim_part is `/muda/bora`, prop_part is `.dora`.
///
/// ':' is a namespce delimiter(example `input:muda`).
///
/// Limitations:
///
/// Relational attribute path(`[` `]`. e.g. `/muda/bora[/ari].dora`) is not
/// supported.
///
/// variant chars('{' '}') is not supported.
/// '..' is not supported
///
/// and have more limitatons.
///
class Path {
 public:
  Path() : valid(false) {}
  Path(const std::string &prim)
      : prim_part(prim), local_part(prim), valid(true) {}
  // Path(const std::string &prim, const std::string &prop)
  //    : prim_part(prim), prop_part(prop) {}

  Path(const Path &rhs) = default;

  Path &operator=(const Path &rhs) {
    this->valid = rhs.valid;

    this->prim_part = rhs.prim_part;
    this->prop_part = rhs.prop_part;
    this->local_part = rhs.local_part;

    return (*this);
  }

  std::string full_path_name() const {
    std::string s;
    if (!valid) {
      s += "INVALID#";
    }

    s += prim_part;
    if (prop_part.empty()) {
      return s;
    }

    s += "." + prop_part;

    return s;
  }

  std::string local_path_name() const {
    std::string s;
    if (!valid) {
      s += "INVALID#";
    }

    s += local_part;

    return s;
  }

  std::string GetPrimPart() const { return prim_part; }

  std::string GetPropPart() const { return prop_part; }

  bool IsEmpty() { return (prim_part.empty() && prop_part.empty()); }

  static Path AbsoluteRootPath() { return Path("/"); }

  void SetLocalPath(const Path &rhs) {
    // assert(rhs.valid == true);

    this->local_part = rhs.local_part;
    this->valid = rhs.valid;
  }

  Path AppendProperty(const std::string &elem) {
    Path p = (*this);

    if (elem.empty()) {
      p.valid = false;
      return p;
    }

    if (elem[0] == '{') {
      // variant chars are not supported
      p.valid = false;
      return p;
    } else if (elem[0] == '[') {
      // relational attrib are not supported
      p.valid = false;
      return p;
    } else if (elem[0] == '.') {
      // std::cerr << "???. elem[0] is '.'\n";
      // For a while, make this valid.
      p.valid = false;
      return p;
    } else {
      p.prop_part = elem;

      return p;
    }
  }

  Path AppendElement(const std::string &elem) {
    Path p = (*this);

    if (elem.empty()) {
      p.valid = false;
      return p;
    }

    if (elem[0] == '{') {
      // variant chars are not supported
      p.valid = false;
      return p;
    } else if (elem[0] == '[') {
      // relational attrib are not supported
      p.valid = false;
      return p;
    } else if (elem[0] == '.') {
      // std::cerr << "???. elem[0] is '.'\n";
      // For a while, make this valid.
      p.valid = false;
      return p;
    } else {
      // std::cout << "elem " << elem << "\n";
      if ((p.prim_part.size() == 1) && (p.prim_part[0] == '/')) {
        p.prim_part += elem;
      } else {
        p.prim_part += '/' + elem;
      }

      return p;
    }
  }

  bool IsValid() const { return valid; }

 private:
  std::string prim_part;  // full path
  std::string prop_part;  // full path
  std::string local_part;
  bool valid{false};
};

///
/// Split Path by the delimiter(e.g. "/") then create lists.
///
class TokenizedPath
{
 public:

  TokenizedPath() {}

  TokenizedPath(const Path &path) {
    std::string s = path.GetPropPart();
    if (s.empty()) {
      // ???
      return;
    }

    if (s[0] != '/') {
      // Path must start with "/"
      return;
    }
    
    s.erase(0, 1);

    std::string delimiter = "/";
    size_t pos{0};
    while ((pos = s.find(delimiter)) != std::string::npos) {
        std::string token = s.substr(0, pos);
        _tokens.push_back(token);
        s.erase(0, pos + delimiter.length());
    }

    if (!s.empty()) {
      // leaf element
      _tokens.push_back(s);
    }
    
  }

 private:

  std::vector<std::string> _tokens;
};


class TimeCode {
  TimeCode(double tm = 0.0) : _time(tm) {}

  size_t hash() const { return std::hash<double>{}(_time); }

  double value() const { return _time; }

 private:
  double _time;
};

struct LayerOffset {
  double _offset;
  double _scale;
};

struct Payload {
  std::string _asset_path;
  Path _prim_path;
  LayerOffset _layer_offset;
};

#if 0
enum ValueTypeId {
  VALUE_TYPE_INVALID = 0,

  VALUE_TYPE_BOOL = 1,
  VALUE_TYPE_UCHAR = 2,
  VALUE_TYPE_INT = 3,
  VALUE_TYPE_UINT = 4,
  VALUE_TYPE_INT64 = 5,
  VALUE_TYPE_UINT64 = 6,

  VALUE_TYPE_HALF = 7,
  VALUE_TYPE_FLOAT = 8,
  VALUE_TYPE_DOUBLE = 9,

  VALUE_TYPE_STRING = 10,
  VALUE_TYPE_TOKEN = 11,
  VALUE_TYPE_ASSET_PATH = 12,

  VALUE_TYPE_MATRIX2D = 13,
  VALUE_TYPE_MATRIX3D = 14,
  VALUE_TYPE_MATRIX4D = 15,

  VALUE_TYPE_QUATD = 16,
  VALUE_TYPE_QUATF = 17,
  VALUE_TYPE_QUATH = 18,

  VALUE_TYPE_VEC2D = 19,
  VALUE_TYPE_VEC2F = 20,
  VALUE_TYPE_VEC2H = 21,
  VALUE_TYPE_VEC2I = 22,

  VALUE_TYPE_VEC3D = 23,
  VALUE_TYPE_VEC3F = 24,
  VALUE_TYPE_VEC3H = 25,
  VALUE_TYPE_VEC3I = 26,

  VALUE_TYPE_VEC4D = 27,
  VALUE_TYPE_VEC4F = 28,
  VALUE_TYPE_VEC4H = 29,
  VALUE_TYPE_VEC4I = 30,

  VALUE_TYPE_DICTIONARY = 31,
  VALUE_TYPE_TOKEN_LIST_OP = 32,
  VALUE_TYPE_STRING_LIST_OP = 33,
  VALUE_TYPE_PATH_LIST_OP = 34,
  VALUE_TYPE_REFERENCE_LIST_OP = 35,
  VALUE_TYPE_INT_LIST_OP = 36,
  VALUE_TYPE_INT64_LIST_OP = 37,
  VALUE_TYPE_UINT_LIST_OP = 38,
  VALUE_TYPE_UINT64_LIST_OP = 39,

  VALUE_TYPE_PATH_VECTOR = 40,
  VALUE_TYPE_TOKEN_VECTOR = 41,

  VALUE_TYPE_SPECIFIER = 42,
  VALUE_TYPE_PERMISSION = 43,
  VALUE_TYPE_VARIABILITY = 44,

  VALUE_TYPE_VARIANT_SELECTION_MAP = 45,
  VALUE_TYPE_TIME_SAMPLES = 46,
  VALUE_TYPE_PAYLOAD = 47,
  VALUE_TYPE_DOUBLE_VECTOR = 48,
  VALUE_TYPE_LAYER_OFFSET_VECTOR = 49,
  VALUE_TYPE_STRING_VECTOR = 50,
  VALUE_TYPE_VALUE_BLOCK = 51,
  VALUE_TYPE_VALUE = 52,
  VALUE_TYPE_UNREGISTERED_VALUE = 53,
  VALUE_TYPE_UNREGISTERED_VALUE_LIST_OP = 54,
  VALUE_TYPE_PAYLOAD_LIST_OP = 55,
  VALUE_TYPE_TIME_CODE = 56,
};

struct ValueType {
  ValueType()
      : name("Invalid"), id(VALUE_TYPE_INVALID), supports_array(false) {}
  ValueType(const std::string &n, uint32_t i, bool a)
      : name(n), id(ValueTypeId(i)), supports_array(a) {}

  std::string name;
  ValueTypeId id{VALUE_TYPE_INVALID};
  bool supports_array{false};
};

#endif

enum SpecType {
  SpecTypeUnknown = 0,
  SpecTypeAttribute,
  SpecTypeConnection,
  SpecTypeExpression,
  SpecTypeMapper,
  SpecTypeMapperArg,
  SpecTypePrim,
  SpecTypePseudoRoot,
  SpecTypeRelationship,
  SpecTypeRelationshipTarget,
  SpecTypeVariant,
  SpecTypeVariantSet,
  NumSpecTypes
};

enum Orientation {
  OrientationRightHanded,  // 0
  OrientationLeftHanded
};

enum Visibility {
  VisibilityInherited,  // 0
  VisibilityInvisible
};

enum Purpose {
  PurposeDefault,  // 0
  PurposeRender,
  PurposeProxy,
  PurposeGuide
};

enum SubdivisionScheme {
  SubdivisionSchemeCatmullClark,  // 0
  SubdivisionSchemeLoop,
  SubdivisionSchemeBilinear,
  SubdivisionSchemeNone
};

// For PrimSpec
enum Specifier {
  SpecifierDef,  // 0
  SpecifierOver,
  SpecifierClass,
  NumSpecifiers
};

enum Permission {
  PermissionPublic,  // 0
  PermissionPrivate,
  NumPermissions
};

enum Variability {
  VariabilityVarying,  // 0
  VariabilityUniform,
  VariabilityConfig,
  NumVariabilities
};

///
/// Curently we only support limited types for time sample values.
///
using TimeSampleType = nonstd::variant<double, Vec3f, Quatf, Matrix4d>;

struct TimeSamples {
  std::vector<double> times;
  std::vector<TimeSampleType> values;
};

#if 0
///
/// Simple type-erased primitive value class for frequently used data types(e.g. `float[]`)
///
template <class dtype>
struct TypeTrait;

// TODO(syoyo): Support `Token` type
template <>
struct TypeTrait<std::string> {
  static constexpr auto type_name = "string";
  static constexpr ValueTypeId type_id = VALUE_TYPE_STRING;
};

template <>
struct TypeTrait<int> {
  static constexpr auto type_name = "int";
  static constexpr ValueTypeId type_id = VALUE_TYPE_INT;
};

template <>
struct TypeTrait<float> {
  static constexpr auto type_name = "float";
  static constexpr ValueTypeId type_id = VALUE_TYPE_FLOAT;
};

template <>
struct TypeTrait<double> {
  static constexpr auto type_name = "double";
  static constexpr ValueTypeId type_id = VALUE_TYPE_DOUBLE;
};


template <>
struct TypeTrait<float16> {
  static constexpr auto type_name = "half";
  static constexpr ValueTypeId type_id = VALUE_TYPE_HALF;
};

template <>
struct TypeTrait<Vec2f> {
  static constexpr auto type_name = "vec2f";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC2F;
};

template <>
struct TypeTrait<Vec3f> {
  static constexpr auto type_name = "vec3f";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC3F;
};

template <>
struct TypeTrait<Vec4f> {
  static constexpr auto type_name = "vec4f";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC4F;
};

template <>
struct TypeTrait<Quatf> {
  static constexpr auto type_name = "quatf";
  static constexpr ValueTypeId type_id = VALUE_TYPE_QUATF;
};

template <>
struct TypeTrait<Quatd> {
  static constexpr auto type_name = "quatd";
  static constexpr ValueTypeId type_id = VALUE_TYPE_QUATD;
};

template <>
struct TypeTrait<Matrix4d> {
  static constexpr auto type_name = "matrix4d";
  static constexpr ValueTypeId type_id = VALUE_TYPE_MATRIX4D;
};

template <class T>
class PrimValue {
 private:
  T m_value;

 public:
  T value() const { return m_value; }

  template <typename U, typename std::enable_if<std::is_same<T, U>::value>::type
                            * = nullptr>
  PrimValue<T> &operator=(const U &u) {
    m_value = u;

    return (*this);
  }

  std::string type_name() { return std::string(TypeTrait<T>::type_name); }
  bool is_array() const { return false; }
  int array_dim() const { return 0; }
};

///
/// 1D array of PrimValue
///
template <class T>
class PrimValue<std::vector<T>> {
 private:
  std::vector<T> m_value;

  template <typename U, typename std::enable_if<std::is_same<T, U>::value>::type
                            * = nullptr>
  PrimValue<T> &operator=(const std::vector<U> &u) {
    m_value = u;

    return (*this);
  }

  std::string type_name() {
    return std::string(TypeTrait<T>::type_name) + "[]";
  }

  bool is_array() const { return true; }
  int array_dim() const { return 1; }
};

///
/// 2D array of PrimValue
/// TODO: Provide multidim_array implementation
///
template <class T>
class PrimValue<std::vector<std::vector<T>>> {
 private:
  std::vector<std::vector<T>> m_value;

  template <typename U, typename std::enable_if<std::is_same<T, U>::value>::type
                            * = nullptr>
  PrimValue<T> &operator=(const std::vector<std::vector<U>> &u) {
    m_value = u;

    return (*this);
  }

  std::string type_name() {
    return std::string(TypeTrait<T>::type_name) + "[][]";
  }

  bool is_array() const { return true; }
  int array_dim() const { return 2; }
};
#endif

///
/// Represent value.
/// array is up to 1D array.
///
class Value {
 public:
  typedef std::map<std::string, Value> Dictionary;

  Value() = default;

  Value(const ValueType &_dtype, const std::vector<uint8_t> &_data)
      : dtype(_dtype), data(_data), array_length(-1) {}
  Value(const ValueType &_dtype, const std::vector<uint8_t> &_data,
        uint64_t _array_length)
      : dtype(_dtype), data(_data), array_length(int64_t(_array_length)) {}

  bool IsArray() const {
    if ((array_length > 0) || string_array.size() ||
        (dtype.id == VALUE_TYPE_PATH_LIST_OP)) {
      return true;
    }
    return false;
  }

  // Setter for primitive types.
  void SetBool(const bool d) {
    dtype.name = "Bool";
    dtype.id = VALUE_TYPE_BOOL;

    uint8_t value = d ? 1 : 0;
    data.resize(1);
    data[0] = value;
  }

  void SetUChar(const unsigned char d) {
    dtype.name = "UChar";
    dtype.id = VALUE_TYPE_UCHAR;

    data.resize(1);
    data[0] = d;
  }

  void SetInt(const int32_t i) {
    static_assert(sizeof(int32_t) == 4, "");
    dtype.name = "Int";
    dtype.id = VALUE_TYPE_INT;
    data.resize(sizeof(int32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(int32_t));
  }

  void SetUInt(const uint32_t i) {
    static_assert(sizeof(uint32_t) == 4, "");
    dtype.name = "UInt";
    dtype.id = VALUE_TYPE_UINT;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(uint32_t));
  }

  void SetInt64(const int64_t i) {
    static_assert(sizeof(int64_t) == 8, "");
    dtype.name = "Int64";
    dtype.id = VALUE_TYPE_INT64;
    data.resize(sizeof(int64_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(int64_t));
  }

  void SetUInt64(const uint64_t i) {
    static_assert(sizeof(uint64_t) == 8, "");
    dtype.name = "UInt64";
    dtype.id = VALUE_TYPE_UINT64;
    data.resize(sizeof(uint64_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(uint64_t));
  }

  void SetDouble(const double d) {
    static_assert(sizeof(double) == 8, "");
    dtype.name = "Double";
    dtype.id = VALUE_TYPE_DOUBLE;
    data.resize(sizeof(double));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(double));
  }

  void SetFloat(const float d) {
    static_assert(sizeof(float) == 4, "");
    dtype.name = "Float";
    dtype.id = VALUE_TYPE_FLOAT;
    data.resize(sizeof(float));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(float));
  }

  void SetHalf(const float16 d) {
    dtype.name = "Half";
    dtype.id = VALUE_TYPE_HALF;
    data.resize(sizeof(uint16_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint16_t));
  }

  void SetVec2i(const Vec2i v) {
    static_assert(sizeof(Vec2i) == 8, "");
    dtype.name = "Vec2i";
    dtype.id = VALUE_TYPE_VEC2I;
    data.resize(sizeof(Vec2i));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2i));
  }

  void SetVec2f(const Vec2f v) {
    static_assert(sizeof(Vec2f) == 8, "");
    dtype.name = "Vec2f";
    dtype.id = VALUE_TYPE_VEC2F;
    data.resize(sizeof(Vec2f));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2f));
  }

  void SetVec2d(const Vec2d v) {
    static_assert(sizeof(Vec2d) == 16, "");
    dtype.name = "Vec2d";
    dtype.id = VALUE_TYPE_VEC2D;
    data.resize(sizeof(Vec2d));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2d));
  }

  void SetVec2h(const Vec2h v) {
    static_assert(sizeof(Vec2h) == 4, "");
    dtype.name = "Vec2h";
    dtype.id = VALUE_TYPE_VEC2H;
    data.resize(sizeof(Vec2h));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2h));
  }

  void SetVec3i(const Vec3i v) {
    static_assert(sizeof(Vec3i) == 12, "");
    dtype.name = "Vec3i";
    dtype.id = VALUE_TYPE_VEC3I;
    data.resize(sizeof(Vec3i));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3i));
  }

  void SetVec3f(const Vec3f v) {
    static_assert(sizeof(Vec3f) == 12, "");
    dtype.name = "Vec3f";
    dtype.id = VALUE_TYPE_VEC3F;
    data.resize(sizeof(Vec3f));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3f));
  }

  void SetVec3d(const Vec3d v) {
    static_assert(sizeof(Vec3d) == 24, "");
    dtype.name = "Vec3d";
    dtype.id = VALUE_TYPE_VEC3D;
    data.resize(sizeof(Vec3d));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3d));
  }

  void SetVec3h(const Vec3h v) {
    static_assert(sizeof(Vec3h) == 6, "");
    dtype.name = "Vec3h";
    dtype.id = VALUE_TYPE_VEC3H;
    data.resize(sizeof(Vec3h));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3h));
  }

  void SetVec4i(const Vec4i v) {
    static_assert(sizeof(Vec4i) == 16, "");
    dtype.name = "Vec4i";
    dtype.id = VALUE_TYPE_VEC4I;
    data.resize(sizeof(Vec4i));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4i));
  }

  void SetVec4f(const Vec4f v) {
    static_assert(sizeof(Vec4f) == 16, "");
    dtype.name = "Vec4f";
    dtype.id = VALUE_TYPE_VEC4F;
    data.resize(sizeof(Vec4f));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4f));
  }

  void SetVec4d(const Vec4d v) {
    static_assert(sizeof(Vec4d) == 32, "");
    dtype.name = "Vec4d";
    dtype.id = VALUE_TYPE_VEC4D;
    data.resize(sizeof(Vec4d));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4d));
  }

  void SetVec4h(const Vec4h v) {
    static_assert(sizeof(Vec4h) == 8, "");
    dtype.name = "Vec4h";
    dtype.id = VALUE_TYPE_VEC4H;
    data.resize(sizeof(Vec4h));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3h));
  }

  void SetQuath(const Quath v) {
    static_assert(sizeof(Quath) == (2 * 4), "");
    dtype.name = "Quath";
    dtype.id = VALUE_TYPE_QUATH;
    data.resize(sizeof(Quath));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Quath));
  }

  void SetQuatf(const Quatf v) {
    static_assert(sizeof(Quatf) == (4 * 4), "");
    dtype.name = "Quatf";
    dtype.id = VALUE_TYPE_QUATF;
    data.resize(sizeof(Quatf));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Quatf));
  }

  void SetQuatd(const Quatd v) {
    static_assert(sizeof(Quatd) == (8 * 4), "");
    dtype.name = "Quatd";
    dtype.id = VALUE_TYPE_QUATD;
    data.resize(sizeof(Quatd));
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Quatd));
  }

  void SetMatrix2d(const Matrix2d v) {
    static_assert(sizeof(Matrix2d) == (2 * 2 * 8), "");
    dtype.name = "Matrix2d";
    dtype.id = VALUE_TYPE_MATRIX2D;
    data.resize(sizeof(Matrix2d));
    memcpy(data.data(), reinterpret_cast<const void *>(v.m), sizeof(Matrix2d));
  }

  void SetMatrix3d(const Matrix3d v) {
    static_assert(sizeof(Matrix3d) == (3 * 3 * 8), "");
    dtype.name = "Matrix3d";
    dtype.id = VALUE_TYPE_MATRIX3D;
    data.resize(sizeof(Matrix3d));
    memcpy(data.data(), reinterpret_cast<const void *>(v.m), sizeof(Matrix3d));
  }

  void SetMatrix4d(const Matrix4d v) {
    static_assert(sizeof(Matrix4d) == (4 * 4 * 8), "");
    dtype.name = "Matrix4d";
    dtype.id = VALUE_TYPE_MATRIX4D;
    data.resize(sizeof(Matrix4d));
    memcpy(data.data(), reinterpret_cast<const void *>(v.m), sizeof(Matrix4d));
  }

  void SetToken(const std::string &s) {
    dtype.name = "Token";
    dtype.id = VALUE_TYPE_TOKEN;
    data.resize(s.size());  // No '\0'
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetString(const std::string &s) {
    dtype.name = "String";
    dtype.id =
        VALUE_TYPE_STRING;  // we treat String as std::string, not StringIndex
    data.resize(s.size());  // No '\0'
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetAssetPath(const std::string &s) {
    dtype.name = "AssetPath";
    dtype.id = VALUE_TYPE_ASSET_PATH;  // we treat AssetPath as std::string, not
                                       // TokenIndex
    data.resize(s.size());             // No '\0'
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetPermission(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Permission";
    dtype.id = VALUE_TYPE_PERMISSION;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetSpecifier(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Specifier";
    dtype.id = VALUE_TYPE_SPECIFIER;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetVariability(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Variability";
    dtype.id = VALUE_TYPE_VARIABILITY;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetIntArray(const int *d, const size_t n) {
    dtype.name = "IntArray";
    dtype.id = VALUE_TYPE_INT;
    array_length = int64_t(n);
    data.resize(n * sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(d),
           n * sizeof(uint32_t));
  }

  void SetHalfArray(const uint16_t *d, const size_t n) {
    dtype.name = "HalfArray";
    dtype.id = VALUE_TYPE_HALF;
    array_length = int64_t(n);
    data.resize(n * sizeof(uint16_t));
    memcpy(data.data(), reinterpret_cast<const void *>(d),
           n * sizeof(uint16_t));
  }

  void SetFloatArray(const float *d, const size_t n) {
    dtype.name = "FloatArray";
    dtype.id = VALUE_TYPE_FLOAT;
    array_length = int64_t(n);
    data.resize(n * sizeof(float));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(float));
  }

  void SetDoubleArray(const double *d, const size_t n) {
    dtype.name = "DoubleArray";
    dtype.id = VALUE_TYPE_DOUBLE;
    array_length = int64_t(n);
    data.resize(n * sizeof(double));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(double));
  }

  void SetVec2fArray(const Vec2f *d, const size_t n) {
    static_assert(sizeof(Vec2f) == 8, "");
    dtype.name = "Vec2fArray";
    dtype.id = VALUE_TYPE_VEC2F;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec2f));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec2f));
  }

  void SetVec3fArray(const Vec3f *d, const size_t n) {
    static_assert(sizeof(Vec3f) == 12, "");
    dtype.name = "Vec3fArray";
    dtype.id = VALUE_TYPE_VEC3F;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec3f));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec3f));
  }

  void SetVec4fArray(const Vec4f *d, const size_t n) {
    static_assert(sizeof(Vec4f) == 16, "");
    dtype.name = "Vec4fArray";
    dtype.id = VALUE_TYPE_VEC4F;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec4f));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec4f));
  }

  void SetVec2dArray(const Vec2d *d, const size_t n) {
    static_assert(sizeof(Vec2d) == 16, "");
    dtype.name = "Vec2dArray";
    dtype.id = VALUE_TYPE_VEC2D;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec2d));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec2d));
  }

  void SetVec3dArray(const Vec3d *d, const size_t n) {
    static_assert(sizeof(Vec3d) == 24, "");
    dtype.name = "Vec3dArray";
    dtype.id = VALUE_TYPE_VEC3D;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec3d));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec3d));
  }

  void SetVec4dArray(const Vec4d *d, const size_t n) {
    static_assert(sizeof(Vec4d) == 32, "");
    dtype.name = "Vec4dArray";
    dtype.id = VALUE_TYPE_VEC4D;
    array_length = int64_t(n);
    data.resize(n * sizeof(Vec4d));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec4d));
  }

  void SetQuathArray(const Quath *d, const size_t n) {
    static_assert(sizeof(Quath) == 8, "");
    dtype.name = "QuathArray";
    dtype.id = VALUE_TYPE_QUATH;
    array_length = int64_t(n);
    data.resize(n * sizeof(Quath));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Quath));
  }

  void SetQuatfArray(const Quatf *d, const size_t n) {
    static_assert(sizeof(Quatf) == 16, "");
    dtype.name = "QuatfArray";
    dtype.id = VALUE_TYPE_QUATF;
    array_length = int64_t(n);
    data.resize(n * sizeof(Quatf));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Quatf));
  }

  void SetQuatdArray(const Quatd *d, const size_t n) {
    static_assert(sizeof(Quatd) == 32, "");
    dtype.name = "QuatdArray";
    dtype.id = VALUE_TYPE_QUATD;
    array_length = int64_t(n);
    data.resize(n * sizeof(Quatd));
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Quatd));
  }

  void SetTokenArray(const std::vector<std::string> &d) {
    dtype.name = "TokenArray";
    dtype.id = VALUE_TYPE_TOKEN_VECTOR;
    array_length = int64_t(d.size());
    string_array = d;
  }

  void SetPathListOp(const ListOp<Path> &d) {
    dtype.name = "PathListOp";
    dtype.id = VALUE_TYPE_PATH_LIST_OP;
    // FIXME(syoyo): How to determine array length?
    // array_length = int64_t(d.size());
    path_list_op = d;
  }

  void SetTimeSamples(const TimeSamples &d) {
    dtype.name = "TimeSamples";
    dtype.id = VALUE_TYPE_TIME_SAMPLES;
    // FIXME(syoyo): How to determine array length?
    // array_length = int64_t(d.size());
    time_samples = d;
  }

  const ListOp<Path> &GetPathListOp() const { return path_list_op; }

  // Getter for frequently used types.
  Specifier GetSpecifier() const {
    if (dtype.id == VALUE_TYPE_SPECIFIER) {
      uint32_t d = *reinterpret_cast<const uint32_t *>(data.data());
      return static_cast<Specifier>(d);
    }
    return NumSpecifiers;  // invalid
  }

  Variability GetVariability() const {
    if (dtype.id == VALUE_TYPE_VARIABILITY) {
      uint32_t d = *reinterpret_cast<const uint32_t *>(data.data());
      return static_cast<Variability>(d);
    }
    return NumVariabilities;  // invalid
  }

  bool GetBool(bool *ret) const {
    if (ret == nullptr) {
      return false;
    }

    if (dtype.id == VALUE_TYPE_BOOL) {
      uint8_t d = *reinterpret_cast<const uint8_t *>(data.data());
      (*ret) = bool(d);
      return true;
    }

    return false;
  }

  double GetDouble() const {
    if (dtype.id == VALUE_TYPE_DOUBLE) {
      double d = *reinterpret_cast<const double *>(data.data());
      return static_cast<double>(d);
    } else if (dtype.id == VALUE_TYPE_FLOAT) {
      float d = *reinterpret_cast<const float *>(data.data());
      return static_cast<double>(d);
    }
    return std::numeric_limits<double>::quiet_NaN();  // invalid
  }

  bool GetInt(int *ret) const {
    if (ret == nullptr) {
      return false;
    }

    if (dtype.id == VALUE_TYPE_INT) {
      int d = *reinterpret_cast<const int *>(data.data());
      (*ret) = d;
      return true;
    }
    return false;
  }

  float GetFloat() const {
    if (dtype.id == VALUE_TYPE_FLOAT) {
      float d = *reinterpret_cast<const float *>(data.data());
      return d;
    }
    return std::numeric_limits<float>::quiet_NaN();  // invalid
  }

  std::string GetToken() const {
    if (dtype.id == VALUE_TYPE_TOKEN) {
      std::string s(reinterpret_cast<const char *>(data.data()), data.size());
      return s;
    }
    return std::string();
  }

  std::string GetString() const {
    if (dtype.id == VALUE_TYPE_STRING) {
      std::string s(reinterpret_cast<const char *>(data.data()), data.size());
      return s;
    }
    return std::string();
  }

  size_t GetArrayLength() const { return size_t(array_length); }

  const std::vector<std::string> &GetStringArray() const {
    return string_array;
  }

  const std::vector<uint8_t> &GetData() const {
    // TODO(syoyo): Report error for Dictionary type.
    return data;
  }

  const std::string &GetTypeName() const { return dtype.name; }

  const ValueTypeId &GetTypeId() const { return dtype.id; }

  bool IsDictionary() const { return dtype.id == VALUE_TYPE_DICTIONARY; }

  void SetDictionary(const Dictionary &d) {
    // Dictonary has separated storage
    dict = d;
  }

  const Dictionary &GetDictionary() const { return dict; }

 private:
  ValueType dtype;
  std::string string_value;
  std::vector<uint8_t> data;  // value as opaque binary data.
  int64_t array_length{-1};

  // Dictonary, ListOp and array of string has separated storage
  std::vector<std::string> string_array;
  Dictionary dict;
  ListOp<Path> path_list_op;
  ListOp<std::string> token_list_op;
  // TODO(syoyo): Reference

  // TODO(syoyo): Use single representation for integral types
  ListOp<int32_t> int_list_op;
  ListOp<uint32_t> int64_list_op;
  ListOp<int64_t> uint_list_op;
  ListOp<uint64_t> uint64_list_op;

  TimeSamples time_samples;
};

//
// Data structure for rendering pipeline.
//
// Similar to OpenGL BufferData
//
//
struct BufferData {
  enum DataType {
    BUFFER_DATA_TYPE_INVALID,
    BUFFER_DATA_TYPE_UNSIGNED_BYTE,
    BUFFER_DATA_TYPE_UNSIGNED_SHORT,
    BUFFER_DATA_TYPE_UNSIGNED_INT,
    BUFFER_DATA_TYPE_UNSIGNED_INT64,
    BUFFER_DATA_TYPE_BYTE,
    BUFFER_DATA_TYPE_SHORT,
    BUFFER_DATA_TYPE_INT,
    BUFFER_DATA_TYPE_INT64,
    BUFFER_DATA_TYPE_HALF,
    BUFFER_DATA_TYPE_FLOAT,
    BUFFER_DATA_TYPE_DOUBLE
  };

  std::vector<uint8_t> data;  // Opaque byte data.
  size_t stride{
      0};  // byte stride for each element. e.g. 12 for XYZXYZXYZ... data. 0 =
           // app should calculate byte stride from type and `num_coords`.
  int32_t num_coords{-1};  // The number of coordinates. e.g. 3 for XYZ, RGB
                           // data, 4 for RGBA. -1 = invalid
  DataType data_type{BUFFER_DATA_TYPE_INVALID};

  void Set(DataType ty, int32_t c, size_t _stride,
           const std::vector<uint8_t> &_data) {
    data_type = ty;
    num_coords = c;
    stride = _stride;
    data = _data;
  }

  bool Valid() {
    if (data_type == BUFFER_DATA_TYPE_INVALID) {
      return false;
    }

    return (data.size() > 0) && (num_coords > 0);
  }

  size_t GetDataTypeByteSize(DataType ty) const {
    switch (ty) {
      case BUFFER_DATA_TYPE_INVALID:
        return 0;
      case BUFFER_DATA_TYPE_BYTE:
        return 1;
      case BUFFER_DATA_TYPE_UNSIGNED_BYTE:
        return 1;
      case BUFFER_DATA_TYPE_SHORT:
        return 2;
      case BUFFER_DATA_TYPE_UNSIGNED_SHORT:
        return 2;
      case BUFFER_DATA_TYPE_INT:
        return 4;
      case BUFFER_DATA_TYPE_UNSIGNED_INT:
        return 4;
      case BUFFER_DATA_TYPE_INT64:
        return 8;
      case BUFFER_DATA_TYPE_UNSIGNED_INT64:
        return 8;
      case BUFFER_DATA_TYPE_HALF:
        return 2;
      case BUFFER_DATA_TYPE_FLOAT:
        return 4;
      case BUFFER_DATA_TYPE_DOUBLE:
        return 8;
    }
    return 0;  // Should not reach here.
  }

  size_t GetElementByteSize() const {
    if (num_coords <= 0) {
      // TODO(syoyo): Report error
      return 0;
    }

    return GetDataTypeByteSize(data_type) * size_t(num_coords);
  }

  size_t GetNumElements() const {
    if (num_coords <= 0) {
      // TODO(syoyo): Report error
      return 0;
    }

    size_t n = data.size() / GetElementByteSize();

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "num_coords = " << num_coords << "\n";
    std::cout << "ccc: num_elements = " << n << "\n";
#endif
    return n;
  }

  int32_t GetNumCoords() const { return num_coords; }

  DataType GetDataType() const { return data_type; }

  size_t GetStride() const { return stride; }

  //
  // Utility functions
  //

  float GetAsFloat() const {
    if (((GetStride() == 0) || (GetStride() == sizeof(float))) &&
        (GetNumCoords() == 1) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT) &&
        (GetNumElements() == 1)) {
      return *(reinterpret_cast<const float *>(data.data()));
    }

    return std::numeric_limits<float>::quiet_NaN();
  }

  std::array<float, 3> GetAsColor3f() const {
    std::array<float, 3> buf{{0.0f, 0.0f, 0.0f}};

    if (((GetStride() == 0) || (GetStride() == 3 * sizeof(float))) &&
        (GetNumCoords() == 3) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      memcpy(buf.data(), data.data(), buf.size() * sizeof(float));
    }

    return buf;
  }

  // Return empty array when required type mismatches.
  //
  std::vector<uint32_t> GetAsUInt32Array() const {
    std::vector<uint32_t> buf;

    if (((GetStride() == 0) || (GetStride() == sizeof(uint32_t))) &&
        (GetDataType() == BUFFER_DATA_TYPE_UNSIGNED_INT)) {
      buf.resize(GetNumElements() * size_t(GetNumCoords()));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "buf.size = " << buf.size() << "\n";
#endif
      memcpy(buf.data(), data.data(), buf.size() * sizeof(uint32_t));
    }

    return buf;
  }

  std::vector<int32_t> GetAsInt32Array() const {
    std::vector<int32_t> buf;

    if (((GetStride() == 0) || (GetStride() == sizeof(int32_t))) &&
        (GetDataType() == BUFFER_DATA_TYPE_INT)) {
      buf.resize(GetNumElements() * size_t(GetNumCoords()));
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "buf.size = " << buf.size() << "\n";
#endif
      memcpy(buf.data(), data.data(), buf.size() * sizeof(int32_t));
    }

    return buf;
  }

  // zero-copy version
  // tuple: <pointer, # of elements>
  nonstd::optional<std::tuple<const float *, size_t>> FloatArrayView() const {
    if (((GetStride() == 0) || (GetStride() == sizeof(float))) &&
        (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      return nonstd::optional<std::tuple<const float *, size_t>>(std::make_tuple(
          reinterpret_cast<const float *>(data.data()), data.size() / sizeof(float)));
    }

    return nonstd::nullopt;
  }

  // this creates new buffer.
  std::vector<float> GetAsFloatArray() const {
    std::vector<float> buf;

    if (((GetStride() == 0) || (GetStride() == sizeof(float))) &&
        (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      buf.resize(GetNumElements() * size_t(GetNumCoords()));
      memcpy(buf.data(), data.data(), buf.size() * sizeof(float));
    }

    return buf;
  }

  std::vector<float> GetAsVec2fArray() const {
    std::vector<float> buf;

    if (((GetStride() == 0) || (GetStride() == 2 * sizeof(float))) &&
        (GetNumCoords() == 2) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      buf.resize(GetNumElements() * 2);
      memcpy(buf.data(), data.data(), buf.size() * sizeof(float));
    }

    return buf;
  }

  std::vector<float> GetAsVec3fArray() const {
    std::vector<float> buf;

    // std::cout << "stride = " << GetStride() << ", num_coords = " <<
    // GetNumCoords() << ", dtype = " << GetDataType() << ", num_elements = " <<
    // GetNumElements() << "\n";

    if (((GetStride() == 0) || (GetStride() == 3 * sizeof(float))) &&
        (GetNumCoords() == 3) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      buf.resize(GetNumElements() * 3);
      memcpy(buf.data(), data.data(), buf.size() * sizeof(float));
    }

    return buf;
  }

  std::vector<float> GetAsVec4fArray() const {
    std::vector<float> buf;

    if (((GetStride() == 0) || (GetStride() == 4 * sizeof(float))) &&
        (GetNumCoords() == 4) && (GetDataType() == BUFFER_DATA_TYPE_FLOAT)) {
      buf.resize(GetNumElements());
      memcpy(buf.data(), data.data(), buf.size() * 4 * sizeof(float));
    }

    return buf;
  }
};

struct ConnectionPath {
  bool input{false};  // true: Input connection. false: Ouput connection.

  Path path;  // original Path information in USD

  std::string token;  // token(or string) in USD
  int64_t index{-1};  // corresponding array index(e.g. the array index to
                      // `Scene.shaders`)
};

// PrimAttrib is a struct to hold attributes of the object.
// (e.g. property, PrimVar).
// We treat PrimVar as PrimAttrib(attributes) at the moment.
struct PrimAttrib {
  std::string name;  // attrib name

  std::string type_name;  // name of attrib type(e.g. "float', "color3f")

  // For array data types(e.g. FloatArray)
  BufferData buffer;  // raw buffer data
  Variability variability;
  bool facevarying{false};

  // For basic types(e.g. Bool, Float).

  // "bool", "string", "float", "int", "uint", "int64", "uint64", "double" or
  // "path" empty = array data type
  std::string basic_type;

  // TODO: Use union struct
  bool boolVal;
  int intVal;
  uint32_t uintVal;
  int64_t int64Val;
  uint64_t uint64Val;
  float floatVal;
  double doubleVal;
  std::string stringVal;  // token, string
  Path path;
};

// UsdPrimvarReader_float2.
// Currently for UV texture coordinate
struct PrimvarReader {
  std::string output_type = "float2";           // currently "float2" only.
  std::array<float, 2> fallback{{0.0f, 0.0f}};  // fallback value

  ConnectionPath
      varname;  // Name of the primvar to be fetched from the geometry.
};

// Orient: axis/angle expressed as a quaternion.
// NOTE: no `matrix4f`
using XformOpValueType = nonstd::variant<float, Vec3f, Quatf, double, Vec3d, Quatd, Matrix4d>;

struct XformOp
{
  enum PrecisionType {
    PRECISION_DOUBLE,
    PRECISION_FLOAT
    // PRECISION_HALF // TODO
  };

  enum OpType {
    // 3D
    TRANSFORM, TRANSLATE, SCALE,

    // 1D
    ROTATE_X, ROTATE_Y, ROTATE_Z,

    // 3D
    ROTATE_XYZ, ROTATE_XZY, ROTATE_YXZ, ROTATE_YZX, ROTATE_ZXY, ROTATE_ZYX,

    // 4D
    ORIENT
  };

  static std::string GetOpTypeName(OpType op) {
    if (op == TRANSFORM) {
      return "xformOp:transform";
    } else if (op == TRANSLATE) {
      return "xformOp:translate";
    } else if (op == SCALE) {
      return "xformOp:scale";
    } else if (op == ROTATE_X) {
      return "xformOp:rotateX";
    } else if (op == ROTATE_Y) {
      return "xformOp:rotateY";
    } else if (op == ROTATE_Z) {
      return "xformOp:rotateZ";
    } else if (op == ROTATE_XYZ) {
      return "xformOp:rotateXYZ";
    } else if (op == ROTATE_XZY) {
      return "xformOp:rotateXZY";
    } else if (op == ROTATE_YXZ) {
      return "xformOp:rotateYXZ";
    } else if (op == ROTATE_YZX) {
      return "xformOp:rotateYZX";
    } else if (op == ROTATE_ZXY) {
      return "xformOp:rotateZXY";
    } else if (op == ROTATE_ZYX) {
      return "xformOp:rotateZYX";
    } else if (op == ORIENT) {
      return "xformOp:orient";
    } else {
      return "xformOp::???";
    }
  }

  OpType op;
  PrecisionType precision;
  XformOpValueType value; // When you look up the value, select basic type based on `precision`
};


Matrix4d GetTransform(XformOp xform);

// Predefined node class
struct Xform {

  std::string name;
  int64_t parent_id{-1};  // Index to xform node

  std::vector<XformOp> xformOps;

  Orientation orientation{OrientationRightHanded};
  Visibility visibility{VisibilityInherited};
  Purpose purpose{PurposeDefault};

  Xform() {
  }

  ///
  /// Evaluate XformOps
  ///
  bool EvaluateXformOps(Matrix4d *out_matrix) const;

  ///
  /// Get concatenated matrix.
  ///
  nonstd::optional<Matrix4d> GetMatrix() const {
    if (_dirty) {
      Matrix4d m;
      if (EvaluateXformOps(&m)) {
        _matrix = m;
        _dirty = false;
      } else {
        // TODO: Report an error.
        return nonstd::nullopt;
      }
    }
    return _matrix;
  }


  mutable bool _dirty{true};
  mutable Matrix4d _matrix; // Resulting matrix of evaluated XformOps.

};

struct UVCoords {
  std::string name;
  BufferData buffer;
  Variability variability;

  // non-empty when UV has its own indices.
  std::vector<uint32_t> indices;  // UV indices. Usually varying
};

struct Extent {
  Vec3f lower{{std::numeric_limits<float>::infinity(),
               std::numeric_limits<float>::infinity(),
               std::numeric_limits<float>::infinity()}};

  Vec3f upper{{-std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity()}};

  bool Valid() const {
    if (lower[0] > upper[0]) return false;
    if (lower[1] > upper[1]) return false;
    if (lower[2] > upper[2]) return false;

    return std::isfinite(lower[0]) && std::isfinite(lower[1]) && std::isfinite(lower[2])
           && std::isfinite(upper[0]) && std::isfinite(upper[1]) && std::isfinite(upper[2]);
  }

  std::array<std::array<float, 3>, 2> to_array() const {
    std::array<std::array<float, 3>, 2> ret;
    ret[0][0] = lower[0];
    ret[0][1] = lower[1];
    ret[0][2] = lower[2];
    ret[1][0] = upper[0];
    ret[1][1] = upper[1];
    ret[1][2] = upper[2];

    return ret;

  }
};

//
// Basis Curves(for hair/fur)
//
struct GeomBasisCurves {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  // Interpolation attribute
  std::string type = "cubic";  // "linear", "cubic"

  std::string basis =
      "bspline";  // "bezier", "catmullRom", "bspline" ("hermite" and "power" is
                  // not supported in TinyUSDZ)

  std::string wrap = "nonperiodic";  // "nonperiodic", "periodic", "pinned"

  //
  // Predefined attribs.
  //
  std::vector<float> points;   // float3
  std::vector<float> normals;  // normal3f
  std::vector<int> curveVertexCounts;
  std::vector<float> widths;
  std::vector<float> velocities;     // vector3f
  std::vector<float> accelerations;  // vector3f

  //
  // Properties
  //
  Extent extent;  // bounding extent(in local coord?).
  Visibility visibility{VisibilityInherited};
  Purpose purpose{PurposeDefault};

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

//
// Points primitive.
//
struct GeomPoints {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //
  std::vector<float> points;   // float3
  std::vector<float> normals;  // normal3f
  std::vector<float> widths;
  std::vector<int64_t> ids;          // per-point ids
  std::vector<float> velocities;     // vector3f
  std::vector<float> accelerations;  // vector3f

  //
  // Properties
  //
  Extent extent;  // bounding extent(in local coord?).
  Visibility visibility{VisibilityInherited};
  Purpose purpose{PurposeDefault};

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` may exist(in that ase, please ignore `widths`
  // parameter)
  std::map<std::string, PrimAttrib> attribs;
};

// BlendShapes
// TODO(syoyo): Blendshape
struct BlendShape {
  std::vector<float> offsets;        // required. position offsets. vec3f
  std::vector<float> normalOffsets;  // required. vec3f
  std::vector<int>
      pointIndices;  // optional. vertex indices to the original mesh for each
                     // values in `offsets` and `normalOffsets`.
};

struct SkelRoot {
  Extent extent;
  Purpose purpose{PurposeDefault};
  Visibility visibility{VisibilityInherited};

  // TODO
  // std::vector<std::string> xformOpOrder;
  // ref proxyPrim
};

// Skelton
struct Skelton {
  std::vector<Matrix4d>
      bindTransforms;  // bind-pose transform of each joint in world coordinate.
  Extent extent;

  std::vector<std::string> jointNames;
  std::vector<std::string> joints;

  std::vector<Matrix4d> restTransforms;  // rest-pose transforms of each joint
                                         // in local coordinate.

  Purpose purpose{PurposeDefault};
  Visibility visibility{VisibilityInherited};

  // TODO
  // std::vector<std::string> xformOpOrder;
  // ref proxyPrim
};

struct SkelAnimation {
  std::vector<std::string> blendShapes;
  std::vector<float> blendShapeWeights;
  std::vector<std::string> joints;
  std::vector<Quatf> rotations;  // Joint-local unit quaternion rotations
  std::vector<Vec3f> scales;  // Joint-local scaling. pxr USD schema uses half3,
                              // but we use float3 for convenience.
  std::vector<Vec3f> translations;  // Joint-local translation.
};

// W.I.P.
struct SkelBindingAPI {
  Matrix4d geomBindTransform;            // primvars:skel:geomBindTransform
  std::vector<int> jointIndices;         // primvars:skel:jointIndices
  std::vector<float> jointWeights;       // primvars:skel:jointWeights
  std::vector<std::string> blendShapes;  // optional?
  std::vector<std::string> joints;       // optional

  int64_t animationSource{
      -1};  // index to Scene.animations. ref skel:animationSource
  int64_t blendShapeTargets{
      -1};  // index to Scene.blendshapes. ref skel:bindShapeTargets
  int64_t skeleton{-1};  // index to Scene.skeltons. // ref skel:skelton
};

// Polygon mesh geometry
struct GeomMesh {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //
  std::vector<float> points;  // float3
  PrimAttrib normals;         // Usually float3[], varying

  //
  // Utility functions
  //

  size_t GetNumPoints() const;
  size_t GetNumFacevaryingNormals() const;

  // Get `normals` as float3 array + facevarying
  // Return false if `normals` is neither float3[] type nor `varying`
  bool GetFacevaryingNormals(std::vector<float> *v) const;

  // Get `texcoords` as float2 array + facevarying
  // Return false if `texcoords` is neither float2[] type nor `varying`
  bool GetFacevaryingTexcoords(std::vector<float> *v) const;

  // PrimVar(TODO: Remove)
  UVCoords st;

  PrimAttrib velocitiess;  // Usually float3[], varying

  std::vector<int32_t> faceVertexCounts;
  std::vector<int32_t> faceVertexIndices;

  //
  // Properties
  //
  Extent extent;  // bounding extent(in local coord?).
  std::string facevaryingLinearInterpolation = "cornerPlus1";
  bool doubleSided{true};
  Orientation orientation{OrientationRightHanded};
  Visibility visibility{VisibilityInherited};
  Purpose purpose{PurposeDefault};

  //
  // SubD attribs.
  //
  std::vector<int32_t> cornerIndices;
  std::vector<float> cornerSharpnesses;
  std::vector<int32_t> creaseIndices;
  std::vector<int32_t> creaseLengths;
  std::vector<float> creaseSharpnesses;
  std::vector<int32_t> holeIndices;
  std::string interpolateBoundary =
      "edgeAndCorner";  // "none", "edgeAndCorner" or "edgeOnly"
  SubdivisionScheme subdivisionScheme;

  // List of Primitive attributes(primvars)
  std::map<std::string, PrimAttrib> attribs;
};

//
// Similar to Maya's ShadingGroup
//
struct Material {
  std::string name;

  int64_t parent_id{-1};

  int64_t surface_shader_id{-1};  // Index to `Scene::shaders`
  int64_t volume_shader_id{-1};   // Index to `Scene::shaders`
  // int64_t displacement_shader_id{-1}; // Index to shader object. TODO(syoyo)
};


// TODO
//  - NodeGraph

// result = (texture_id == -1) ? use color : lookup texture
struct Color3OrTexture {
  Color3OrTexture(float x, float y, float z) {
    color[0] = x;
    color[1] = y;
    color[2] = z;
  }

  std::array<float, 3> color{{0.0f, 0.0f, 0.0f}};

  std::string path;  // path to .connect(We only support texture file connection
                     // at the moment)
  int64_t texture_id{-1};

  bool HasTexture() const { return texture_id > -1; }
};

struct FloatOrTexture {
  FloatOrTexture(float x) { value = x; }

  float value{0.0f};

  std::string path;  // path to .connect(We only support texture file connection
                     // at the moment)
  int64_t texture_id{-1};

  bool HasTexture() const { return texture_id > -1; }
};

enum TextureWrap {
  TextureWrapUseMetadata,  // look for wrapS and wrapT metadata in the texture
                           // file itself
  TextureWrapBlack,
  TextureWrapClamp,
  TextureWrapRepeat,
  TextureWrapMirror
};

// For texture transform
// result = in * scale * rotate * translation
struct UsdTranform2d {
  float rotation =
      0.0f;  // counter-clockwise rotation in degrees around the origin.
  std::array<float, 2> scale{{1.0f, 1.0f}};
  std::array<float, 2> translation{{0.0f, 0.0f}};
};

// UsdUvTexture
struct UVTexture {
  std::string asset;  // asset name(usually file path)
  int64_t image_id{
      -1};  // TODO(syoyo): Consider UDIM `@textures/occlusion.<UDIM>.tex@`

  TextureWrap wrapS;
  TextureWrap wrapT;

  std::array<float, 4> fallback{
      {0.0f, 0.0f, 0.0f,
       1.0f}};  // fallback color used when texture cannot be read.
  std::array<float, 4> scale{
      {1.0f, 1.0f, 1.0f, 1.0f}};  // scale to be applied to output texture value
  std::array<float, 4> bias{
      {0.0f, 0.0f, 0.0f, 0.0f}};  // bias to be applied to output texture value

  UsdTranform2d texture_transfom;  // texture coordinate orientation.

  // key = connection name: e.g. "outputs:rgb"
  // item = pair<type, name> : example: <"float3", "outputs:rgb">
  std::map<std::string, std::pair<std::string, std::string>> outputs;

  PrimvarReader st;  // texture coordinate(`inputs:st`). We assume there is a
                     // connection to this.

  // TODO: orientation?
  // https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html#UsdPreviewSurfaceProposal-TextureCoordinateOrientationinUSD
};

// USD's default? PBR shader
// https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html
// $USD/pxr/usdImaging/plugin/usdShaders/shaders/shaderDefs.usda
struct PreviewSurface {
  std::string doc;

  //
  // Inputs
  //
  // Currently we don't support nested shader description.
  //
  Color3OrTexture diffuseColor{0.18f, 0.18f, 0.18f};
  Color3OrTexture emissiveColor{0.0f, 0.0f, 0.0f};
  int usdSpecularWorkflow{0};  // 0 = metalness workflow, 1 = specular workflow

  // specular workflow
  Color3OrTexture specularColor{0.0f, 0.0f, 0.0f};

  // metalness workflow
  FloatOrTexture metallic{0.0f};

  FloatOrTexture roughness{0.5f};
  FloatOrTexture clearcoat{0.0f};
  FloatOrTexture clearcoatRoughness{0.01f};
  FloatOrTexture opacity{1.0f};
  FloatOrTexture opacityThreshold{0.0f};
  FloatOrTexture ior{1.5f};
  Color3OrTexture normal{0.0f, 0.0f, 1.0f};
  FloatOrTexture displacement{0.0f};
  FloatOrTexture occlusion{1.0f};

  //
  // Outputs
  //
  int64_t surface_id{-1};       // index to `Scene::shaders`
  int64_t displacement_id{-1};  // index to `Scene::shaders`
};


// USDZ Schemas for AR
// https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/schema_definitions_for_third-party_digital_content_creation_dcc

// UsdPhysics
struct Preliminary_PhysicsGravitationalForce
{
  // physics::gravitatioalForce::acceleration
  Vec3d acceleration{{0.0, -9.81, 0.0}}; // [m/s^2]

};

struct Preliminary_PhysicsMaterialAPI
{
  // preliminary:physics:material:restitution
  double restitution; // [0.0, 1.0]

  // preliminary:physics:material:friction:static
  double friction_static;

  // preliminary:physics:material:friction:dynamic
  double friction_dynamic;
};

struct Preliminary_PhysicsRigidBodyAPI
{
  // preliminary:physics:rigidBody:mass
  double mass{1.0};

  // preliminary:physics:rigidBody:initiallyActive
  bool initiallyActive{true};
};

struct Preliminary_PhysicsColliderAPI
{
  // preliminary::physics::collider::convexShape
  Path convexShape; 

};

struct Preliminary_InfiniteColliderPlane
{
  Vec3d position{{0.0, 0.0, 0.0}};
  Vec3d normal{{0.0, 0.0, 0.0}};

  Extent extent; // [-FLT_MAX, FLT_MAX]

  Preliminary_InfiniteColliderPlane() {
    extent.lower[0] = -std::numeric_limits<float>::max();
    extent.lower[1] = -std::numeric_limits<float>::max();
    extent.lower[2] = -std::numeric_limits<float>::max();
    extent.upper[0] = std::numeric_limits<float>::max();
    extent.upper[1] = std::numeric_limits<float>::max();
    extent.upper[2] = std::numeric_limits<float>::max();
  }

};

// UsdInteractive
struct Preliminary_AnchoringAPI
{
  // preliminary:anchoring:type
  std::string type; // "plane", "image", "face", "none";

  std::string alignment; // "horizontal", "vertical", "any";

  Path referenceImage;

};

struct Preliminary_ReferenceImage
{
  int64_t image_id{-1}; // asset image 

  double physicalWidth{0.0};
};

struct Preliminary_Behavior
{
  Path triggers;
  Path actions;
  bool exclusive{false}; 
};

struct Preliminary_Trigger
{
  // uniform token info:id
  std::string info; // Store decoded string from token id
};

struct Preliminary_Action
{
  // uniform token info:id
  std::string info;  // Store decoded string from token id

  std::string multiplePerformOperation{"ignore"}; // ["ignore", "allow", "stop"]
};

struct Preliminary_Text
{
  std::string content;
  std::vector<std::string> font; // An array of font names

  float pointSize{144.0f};
  float width;
  float height;
  float depth{0.0f};

  std::string wrapMode{"flowing"}; // ["singleLine", "hardBreaks", "flowing"]
  std::string horizontalAlignmment{"center"}; // ["left", "center", "right", "justified"]
  std::string verticalAlignmment{"middle"}; // ["top", "middle", "lowerMiddle", "baseline", "bottom"]

};

using StringOrId = std::pair<std::string, int32_t>;

// Simple bidirectional Path(string) <-> index lookup
struct StringAndIdMap {
  void add(int32_t key, const std::string &val) {
    _i_to_s[key] = val;
    _s_to_i[val] = key;
  }

  void add(const std::string &key, int32_t val) {
    _s_to_i[key] = val;
    _i_to_s[val] = key;
  }

  size_t count(int32_t i) const { return _i_to_s.count(i); }

  size_t count(const std::string &s) const { return _s_to_i.count(s); }

  std::string at(int32_t i) const { return _i_to_s.at(i); }

  int32_t at(std::string s) const { return _s_to_i.at(s); }

  std::map<int32_t, std::string> _i_to_s;  // index -> string
  std::map<std::string, int32_t> _s_to_i;  // string -> index
};

// Corresponds to USD's Scope.
// `Scope` is uncommon term in graphics community, so we use `Group`.
// From USD doc: Scope is the simplest grouping primitive, and does not carry
// the baggage of transformability.
struct Group {
  std::string name;

  int64_t parent_id{-1};

  Visibility visibility{VisibilityInherited};
  Purpose purpose{PurposeDefault};
};

enum NodeType {
  NODE_TYPE_NULL = 0,
  NODE_TYPE_XFORM,
  NODE_TYPE_GROUP,
  NODE_TYPE_SPHERE,
  NODE_TYPE_GEOM_MESH,
  NODE_TYPE_GEOM_BASISCURVES,
  NODE_TYPE_MATERIAL,
  NODE_TYPE_SHADER,
  NODE_TYPE_CUSTOM  // Uer defined custom node

};

struct Node {
  std::string name;

  NodeType type{NODE_TYPE_NULL};

  //
  // index to a scene object.
  // For example, Lookup `xforms[node_idx]` When node type is XFORM
  //
  int64_t index{-1};

  //int64_t parent;                 // parent node index. Example: `nodes[parent]`
  std::vector<Node> children;  // child nodes
};

struct Scene {
  std::string name;       // Scene name
  int64_t default_root_node{-1};  // index to default root node

  // Node hierarchies
  // Scene can have multiple nodes.
  std::vector<Node> nodes;

  // Scene global setting
  std::string upAxis = "Y";
  std::string defaultPrim;           // prim node name
  double metersPerUnit = 1.0;        // default [m]
  double timeCodesPerSecond = 24.0;  // default 24 fps

  //
  // glTF-like scene objects
  // TODO(syoyo): Use std::variant(C++17) like static polymorphism
  //
  std::vector<Xform> xforms;
  std::vector<GeomMesh> geom_meshes;
  std::vector<GeomBasisCurves> geom_basis_curves;
  std::vector<GeomPoints> geom_points;
  std::vector<Material> materials;
  std::vector<PreviewSurface> shaders;  // TODO(syoyo): Support othre shaders
  std::vector<Group> groups;

  StringAndIdMap geom_meshes_map;  // Path <-> array index map
  StringAndIdMap materials_map;    // Path <-> array index map

  // TODO(syoyo): User defined custom layer data
  // "customLayerData"
};

struct USDLoadOptions {
  ///
  /// Set the number of threads to use when parsing USD scene.
  /// -1 = use # of system threads(CPU cores/threads).
  ///
  int num_threads{-1};

  // Set the maximum memory limit advisorily(including image data).
  // This feature would be helpful if you want to load USDZ model in mobile
  // device.
  int32_t max_memory_limit_in_mb{10000};  // in [mb] Default 10GB

  ///
  /// Loads asset data(e.g. texture image, audio). Default is true.
  /// If you want to load asset data in your own way or don't need asset data to
  /// be loaded, Set this false.
  ///
  bool load_assets{true};
};

#if 0  // TODO
//struct USDWriteOptions
//{
//
//
//};
#endif

//

///
/// Load USDZ(zip) from a file.
///
/// @param[in] filename USDZ filename(UTF-8)
/// @param[out] scene USD scene.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDZFromFile(const std::string &filename, Scene *scene,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());


#ifdef _WIN32
// WideChar version
bool LoadUSDZFromFile(const std::wstring &filename, Scene *scene,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());
#endif

///
/// Load USDC(binary) from a file.
///
/// @param[in] filename USDC filename(UTF-8)
/// @param[out] scene USD scene.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDCFromFile(const std::string &filename, Scene *scene,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());

///
/// Load USDC(binary) from a memory.
///
/// @param[in] addr Memory address of USDC data
/// @param[in] length Byte length of USDC data
/// @param[out] scene USD scene.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDCFromMemory(const uint8_t *addr, const size_t length, Scene *scene,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options = USDLoadOptions());

///
/// Load USDA(ascii) from a file.
///
/// @param[in] filename USDA filename(UTF-8)
/// @param[out] scene USD scene.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDAFromFile(const std::string &filename, Scene *scene,
                      std::string *warn, std::string *err,
                      const USDLoadOptions &options = USDLoadOptions());

///
/// Load USDA(ascii) from a memory.
///
/// @param[in] addr Memory address of USDA data
/// @param[in] length Byte length of USDA data
/// @param[out] scene USD scene.
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDAFromMemory(const uint8_t *addr, const size_t length, Scene *scene,
                        std::string *warn, std::string *err,
                        const USDLoadOptions &options = USDLoadOptions());

#if 0  // TODO
///
/// Write scene as USDC to a file.
///
/// @param[in] filename USDC filename
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Write options(optional)
///
/// @return true upon success
///
bool WriteAsUSDCToFile(const std::string &filename, std::string *err, const USDCWriteOptions &options = USDCWriteOptions());

#endif

}  // namespace tinyusdz

#endif  // TINYUSDZ_HH_
