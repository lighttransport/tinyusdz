// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <nonstd/optional.hpp>
#include <nonstd/variant.hpp>

#include "nonstd/string_view.hpp"

// Cannot use std::string as WISE_ENUM_STRING_TYPE so use nonstd::string_view
#define WISE_ENUM_STRING_TYPE nonstd::string_view
#include <wise_enum/wise_enum.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

WISE_ENUM(SpecType,
  SpecTypeUnknown,
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
)

WISE_ENUM(Orientation,
  OrientationRightHanded,  // 0
  OrientationLeftHanded
)

WISE_ENUM(Visibility,
  VisibilityInherited,  // 0
  VisibilityInvisible
)

WISE_ENUM(Purpose,
  PurposeDefault,  // 0
  PurposeRender,
  PurposeProxy,
  PurposeGuide
)

WISE_ENUM(SubdivisionScheme,
  SubdivisionSchemeCatmullClark,  // 0
  SubdivisionSchemeLoop,
  SubdivisionSchemeBilinear,
  SubdivisionSchemeNone
)

// Attribute interpolation
WISE_ENUM(Interpolation,
  InterpolationInvalid, // 0
  InterpolationConstant, // "constant"
  InterpolationUniform, // "uniform"
  InterpolationVarying, // "varying"
  InterpolationVertex, // "vertex"
  InterpolationFaceVarying // "faceVarying"
)

WISE_ENUM(ListEditQual,
  LIST_EDIT_QUAL_RESET_TO_EXPLICIT,	// "unqualified"(no qualifier)
  LIST_EDIT_QUAL_APPEND, // "append"
  LIST_EDIT_QUAL_ADD, // "add"
  LIST_EDIT_QUAL_DELETE, // "delete"
  LIST_EDIT_QUAL_PREPEND // "prepend"
)

WISE_ENUM(Axis,
  AXIS_X,
  AXIS_Y,
  AXIS_Z
)

// For PrimSpec
WISE_ENUM(Specifier,
  SpecifierDef,  // 0
  SpecifierOver,
  SpecifierClass,
  NumSpecifiers
)

WISE_ENUM(Permission,
  PermissionPublic,  // 0
  PermissionPrivate,
  NumPermissions
)

WISE_ENUM(Variability,
  VariabilityVarying,  // 0
  VariabilityUniform,
  VariabilityConfig,
  NumVariabilities
)

struct AssetReference {
  std::string asset_reference;
  std::string prim_path;
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
class TokenizedPath {
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

//
// Colum-major order(e.g. employed in OpenGL).
// For example, 12th([3][0]), 13th([3][1]), 14th([3][2]) element corresponds to
// the translation.
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
}

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
}

typedef uint16_t float16;
float half_to_float(float16 h);
float16 float_to_half_full(float f);

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

template <typename T>
struct Quat {
  std::array<T, 4> v;
};

using Quath = Quat<uint16_t>;
using Quatf = Quat<float>;
using Quatd = Quat<double>;
// using Quaternion = Quat<double>;  // Storage layout is same with Quadd,
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

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#endif

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
  VALUE_TYPE_TIME_CODE = 56
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

struct Extent {
  Vec3f lower{{std::numeric_limits<float>::infinity(),
               std::numeric_limits<float>::infinity(),
               std::numeric_limits<float>::infinity()}};

  Vec3f upper{{-std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity()}};

  Extent() {}

  Extent(const Vec3f &l, const Vec3f &u) : lower(l), upper(u) {}

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


///
/// Simple type-erased primitive value class for frequently used data types(e.g.
/// `float[]`)
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
struct TypeTrait<bool> {
  static constexpr auto type_name = "bool";
  static constexpr ValueTypeId type_id = VALUE_TYPE_BOOL;
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
struct TypeTrait<Vec2d> {
  static constexpr auto type_name = "vec2d";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC2D;
};

template <>
struct TypeTrait<Vec3d> {
  static constexpr auto type_name = "vec3d";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC3D;
};

template <>
struct TypeTrait<Vec4d> {
  static constexpr auto type_name = "vec4d";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC4D;
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

#if 0
template<typename T>
struct GetDim {
  static constexpr size_t dim() {
    return 0;
  }
};

template<typename T>
struct GetDim<std::vector<T>> {
  static constexpr size_t dim() {
    return 1 + GetDim<T>::dim();
  }
};
#endif

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
  size_t array_dim() const { return 0; }
};

///
/// Array of PrimValue
///
template <class T>
class PrimValue<std::vector<T>> {
 private:
  std::vector<T> m_value;

 public:
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
  size_t array_dim() const { return 1; }
};

///
/// 2D array of PrimValue
///
template <class T>
class PrimValue<std::vector<std::vector<T>>> {
 private:
  std::vector<std::vector<T>> m_value;

 public:
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

///
/// 3D array of PrimValue
///
template <class T>
class PrimValue<std::vector<std::vector<std::vector<T>>>> {
 private:
  std::vector<std::vector<std::vector<T>>> m_value;

 public:
  template <typename U, typename std::enable_if<std::is_same<T, U>::value>::type
                            * = nullptr>
  PrimValue<T> &operator=(const std::vector<std::vector<std::vector<U>>> &u) {
    m_value = u;

    return (*this);
  }

  std::string type_name() {
    return std::string(TypeTrait<T>::type_name) + "[][][]";
  }

  bool is_array() const { return true; }
  int array_dim() const { return 3; }
};

// TODO: Privide generic multidimensional array type?

//
// TimeSample datatype
//

// monostate = `None`
struct None {};

using TimeSampleType =
    nonstd::variant<None, float, double, Vec3f, Quatf, Matrix4d>;

struct TimeSamples {
  std::vector<double> times;
  std::vector<TimeSampleType> values;
};

std::string type_name(const TimeSampleType &v);

//
// Simple NumPy like NDArray up to 4D
// Based on NumCpp
//

template <typename dtype, class Allocator = std::allocator<dtype>>
class ndarray {
  class Shape {};

 private:
  static_assert(std::is_same<dtype, typename Allocator::value_type>::value,
                "dtype and Allocator::value_type must match");

 public:
  using value_type = dtype;
  using allocator_type = Allocator;
  // using pointer = typename AllocTraits::pointer;

  ndarray() = default;
};

// Types which can be TimeSampledData are restricted to frequently used one in
// TinyUSDZ.
typedef std::vector<std::pair<uint64_t, nonstd::optional<float>>>
    TimeSampledDataFloat;
typedef std::vector<std::pair<uint64_t, nonstd::optional<double>>>
    TimeSampledDataDouble;
typedef std::vector<std::pair<uint64_t, nonstd::optional<std::array<float, 3>>>>
    TimeSampledDataFloat3;
typedef std::vector<
    std::pair<uint64_t, nonstd::optional<std::array<double, 3>>>>
    TimeSampledDataDouble3;
typedef std::vector<std::pair<uint64_t, nonstd::optional<Matrix4d>>>
    TimeSampledDataMatrix4d;

using TimeSampledValue =
    nonstd::variant<nonstd::monostate, TimeSampledDataFloat,
                    TimeSampledDataDouble, TimeSampledDataFloat3,
                    TimeSampledDataDouble3, TimeSampledDataMatrix4d>;

using PrimBasicType =
    nonstd::variant<bool, int, uint32_t, float, Vec2f, Vec3f, Vec4f, double, Vec2d, Vec3d,
                    Vec4d, Matrix4d, std::string, Path>;
using PrimArrayType =
    nonstd::variant<std::vector<bool>, std::vector<int>, std::vector<float>, std::vector<Vec2f>,
                    std::vector<Vec3f>, std::vector<Vec4f>, std::vector<double>,
                    std::vector<Vec2d>, std::vector<Vec3d>, std::vector<Vec4d>,
                    std::vector<Matrix4d>, std::vector<std::string>,
                    std::vector<Path>>;

using PrimVar =
    nonstd::variant<None, PrimBasicType, PrimArrayType, TimeSampledValue>;

namespace primvar {

inline bool is_basic_type(const PrimVar *v) {
  if (nonstd::get_if<PrimBasicType>(v)) {
    return true;
  }
  return false;
}

inline bool is_array_type(const PrimVar *v) {
  if (nonstd::get_if<PrimArrayType>(v)) {
    return true;
  }
  return false;
}

inline bool is_time_sampled(const PrimVar *v) {
  if (nonstd::get_if<TimeSampledValue>(v)) {
    return true;
  }
  return false;
}

template <typename T>
using is_vector = std::is_same<T, std::vector< typename T::value_type,
                                          typename T::allocator_type > >;


// non-vector types
template<typename T, typename _ = void>
inline const T *as(const PrimVar *v) {
  if (is_basic_type(v)) {
    return nonstd::get_if<T>(v);
  }
  return nullptr;
}

// vector types
template<typename T, typename std::enable_if<is_vector<T>::value>::type>
inline const T *as(const PrimVar *v) {
  if (is_array_type(v)) {
    return nonstd::get_if<T>(v);
  }
  return nullptr;
}

#if 0 
// https://stackoverflow.com/questions/17032310/how-to-make-a-variadic-is-same
template<typename T, typename... Rest>
struct is_any : std::false_type {};

template<typename T, typename First>
struct is_any<T, First> : std::is_same<T, First> {};

template<typename T, typename First, typename... Rest>
struct is_any<T, First, Rest...>
    : std::integral_constant<bool, std::is_same<T, First>::value || is_any<T, Rest...>::value>
{};
#endif

template<>
inline const TimeSampledDataFloat *as(const PrimVar *v) {
  if (is_time_sampled(v)) {
    return nonstd::get_if<TimeSampledDataFloat>(v);
  }
  return nullptr;
}

template<>
inline const TimeSampledDataDouble *as(const PrimVar *v) {
  if (is_time_sampled(v)) {
    return nonstd::get_if<TimeSampledDataDouble>(v);
  }
  return nullptr;
}

template<>
inline const TimeSampledDataMatrix4d *as(const PrimVar *v) {
  if (is_time_sampled(v)) {
    return nonstd::get_if<TimeSampledDataMatrix4d>(v);
  }
  return nullptr;
}

inline std::vector<Vec3f> to_vec3(const std::vector<float> &v) {
  std::vector<Vec3f> buf;
  if ((v.size() / 3) != 0) {
    return buf;
  }

  buf.resize(v.size() / 3);
  memcpy(buf.data(), v.data(), v.size() * sizeof(float));
  
  return buf;
}


} // namespace primvar

}  // namespace tinyusdz
