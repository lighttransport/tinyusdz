// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <memory>

#include <iostream>


#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/optional.hpp"
#include "nonstd/variant.hpp"

#define any_CONFIG_NO_EXCEPTIONS (1)
#include "nonstd/any.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "primvar.hh"

namespace tinyusdz {

enum class SpecType {
  Attribute,
  Connection,
  Expression,
  Mapper,
  MapperArg,
  Prim,
  PseudoRoot,
  Relationship,
  RelationshipTarget,
  Variant,
  VariantSet,
  Invalid,
};

enum class Orientation {
  RightHanded,  // 0
  LeftHanded,
  Invalid
};

enum class Visibility {
  Inherited,  // 0
  Invisible,
  Invalid
};

enum class Purpose {
  Default,  // 0
  Render,
  Proxy,
  Guide,
  Invalid
};

enum class SubdivisionScheme {
  CatmullClark,  // 0
  Loop,
  Bilinear,
  None,
  Invalid
};

// Attribute interpolation
enum class Interpolation {
  Constant, // "constant"
  Uniform, // "uniform"
  Varying, // "varying"
  Vertex, // "vertex"
  FaceVarying, // "faceVarying"
  Invalid
};

enum class ListEditQual {
  ResetToExplicit,// "unqualified"(no qualifier)
  Append, // "append"
  Add, // "add"
  Delete, // "delete"
  Prepend, // "prepend"
  Invalid
};

enum class Axis {
  X,
  Y,
  Z,
  Invalid
};

// For PrimSpec
enum class Specifier {
  Def,  // 0
  Over,
  Class,
  Invalid
};

enum class Permission {
  Public,  // 0
  Private,
  Invalid
};

enum class Variability {
  Varying,  // 0
  Uniform,
  Config,
  Invalid
};

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

    char delimiter = '/';
    size_t pos{0};
    while ((pos = s.find(delimiter)) != std::string::npos) {
      std::string token = s.substr(0, pos);
      _tokens.push_back(token);
      s.erase(0, pos + sizeof(char));
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

  Extent() = default;

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
  static constexpr auto type_name = "float2";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC2F;
};

template <>
struct TypeTrait<Vec3f> {
  static constexpr auto type_name = "float3";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC3F;
};

template <>
struct TypeTrait<Vec4f> {
  static constexpr auto type_name = "float4";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC4F;
};

template <>
struct TypeTrait<Vec2d> {
  static constexpr auto type_name = "double2";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC2D;
};

template <>
struct TypeTrait<Vec3d> {
  static constexpr auto type_name = "double3";
  static constexpr ValueTypeId type_id = VALUE_TYPE_VEC3D;
};

template <>
struct TypeTrait<Vec4d> {
  static constexpr auto type_name = "double4";
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

#if 0
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
#endif

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

// For `token` type.
struct Token {
  std::string value; // TODO: Use string_view
  // TODO: token ID
};

#if 0
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

// Animatable token property. e.g. `visibility`
typedef std::vector<std::pair<uint64_t, nonstd::optional<Token>>>
    TimeSampledDataToken;

using TimeSampledValue =
    nonstd::variant<nonstd::monostate, TimeSampledDataFloat,
                    TimeSampledDataDouble, TimeSampledDataFloat3,
                    TimeSampledDataDouble3, TimeSampledDataMatrix4d, TimeSampledDataToken>;

using PrimBasicType =
    nonstd::variant<bool, int, uint32_t, float, Vec2f, Vec3f, Vec4f, double, Vec2d, Vec3d,
                    Vec4d, Matrix4d, std::string, Token, Path>;
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
template<typename T>
inline const T *as_basic(const PrimVar *v) {
  std::cout << "T is basic\n";
  if (is_basic_type(v)) {
    std::cout << "is_basic\n";
    // First cast to PrimArrayType.
    if (auto p = nonstd::get_if<PrimBasicType>(v)) {
      std::cout << "p got\n";
      return nonstd::get_if<T>(p);
    }
  }
  return nullptr;
}

// vector types
template<typename T>
inline const std::vector<T> *as_vector(const PrimVar *v) {
  std::cout << "T is vec\n";
  if (is_array_type(v)) {
    std::cout << "is_arary_type\n";
    // First cast to PrimArrayType.
    if (auto p = nonstd::get_if<PrimArrayType>(v)) {
      std::cout << "p got\n";
      return nonstd::get_if<std::vector<T>>(p);
    }
  }
  return nullptr;
}
#endif

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

#if 0
template<typename T>
inline const T *as_timesample(const PrimVar *v) {
  if (is_time_sampled(v)) {
    return nonstd::get_if<T>(v);
  }
  return nullptr;
}


inline std::vector<Vec3f> to_vec3(const std::vector<float> &v) {
  std::vector<Vec3f> buf;
  if ((v.size() % 3) != 0) {
    // std::cout << "to_vec3: not dividable by 3\n";
    return buf;
  }

  buf.resize(v.size() / 3);
  memcpy(buf.data(), v.data(), v.size() * sizeof(float));

  return buf;
}

//template<typename T>
//inline std::string type_name(const std::vector<T> &) {
//  return TypeTrait<T>::type_name + std::string("[]");
//}

//std::string type_name(const PrimBasicType &v);
//std::string type_name(const TimeSampleType &v);

} // namespace primvar
#endif

#if 0
std::string prim_basic_type_name(const PrimBasicType &v);

inline std::string get_type_name(const PrimVar &v) {
  if (auto p = nonstd::get_if<None>(&v)) {
    return "None";
  }

  if (auto p = nonstd::get_if<PrimBasicType>(&v)) {
    return prim_basic_type_name(*p);
  }
  return "TODO";
}
#endif


struct ConnectionPath {
  bool input{false};  // true: Input connection. false: Ouput connection.

  Path path;  // original Path information in USD

  std::string token;  // token(or string) in USD
  int64_t index{-1};  // corresponding array index(e.g. the array index to
                      // `Scene.shaders`)
};

// Relation
struct Rel {
  // TODO: Implement
  std::string path;
};


// PrimAttrib is a struct to hold attribute of a property(e.g. primvar)
struct PrimAttrib {
  std::string name;  // attrib name

  std::string type_name;  // name of attrib type(e.g. "float', "color3f")

  ListEditQual list_edit{ListEditQual::ResetToExplicit};

  Variability variability;
  Interpolation interpolation{Interpolation::Invalid};

  //
  // Qualifiers
  //
  bool uniform{false}; // `uniform`
  bool custom{false}; // `custom`

  primvar::PrimVar var;
};

struct Property
{
  PrimAttrib attrib;
  Rel rel;

  bool is_rel{false};
};

// UsdPrimvarReader_float2.

// Currently for UV texture coordinate
template<typename T>
struct PrimvarReader {
  std::string output_type = TypeTrait<T>::type_name;
  T fallback{};  // fallback value

  ConnectionPath
      varname;  // Name of the primvar to be fetched from the geometry.
};

using PrimvarReader_float2 = PrimvarReader<Vec2f>;
using PrimvarReader_float3 = PrimvarReader<Vec3f>;

using PrimvarReaderType = nonstd::variant<PrimvarReader_float2, PrimvarReader_float3>;

//using Property = nonstd::variant<PrimAttrib, Rel>;
//using Property = nonstd::variant<PrimAttrib, Rel>;

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
  std::string suffix;
  XformOpValueType value; // When you look up the value, select basic type based on `precision`
};


template<typename T>
inline T lerp(const T a, const T b, const double t) {
  return (1.0 - t) * a + t * b;
}


template<typename T>
struct TimeSampled {
  std::vector<double> times;
  std::vector<T> values;
  // TODO: Support `none`

  void Set(T value, double t) {
    times.push_back(t);
    values.push_back(value);
  }

  T Get(double t) const {
    // Linear-interpolation.
    // TODO: Support other interpolation method for example cubic.
    auto it = std::lower_bound(times.begin(), times.end(), t);
    size_t idx0 = size_t(std::max(int64_t(0), std::min(int64_t(times.size() - 1), int64_t(std::distance(times.begin(), it - 1)))));
    size_t idx1 = size_t(std::max(int64_t(0), std::min(int64_t(times.size() - 1), int64_t(idx0) + 1)));

    double tl = times[idx0];
    double tu = times[idx1];

    double dt = (t - tl);
    if (std::fabs(tu - tl) < std::numeric_limits<double>::epsilon()) {
      // slope is zero.
      dt = 0.0;
    } else {
      dt /= (tu - tl);
    }

    // Just in case.
    dt = std::max(0.0, std::min(1.0, dt));

    const T &p0 = values[idx0];
    const T &p1 = values[idx1];

    const T p = lerp(p0, p1, dt);

    return p;
  }

  bool Valid() const {
    return !times.empty() && (times.size() == values.size());
  }
};

// For run-time data structure(e.g. for GeomMesh)
// `None` value and `deleted` items are omitted in this data struct.
// e.g.
//
// double radius.timeSamples = { 0: 1.0, 1: None, 2: 3.0 }
//
// in .usd is stored as
//
// radius = { 0: 1.0, 2: 3.0 }
//
// for Animatable type.

template<typename T>
struct Animatable
{
  T value;
  TimeSampled<T> timeSamples;

  bool IsTimeSampled() const {
    return timeSamples.Valid();
  }

  T Get() const {
    return value;
  }

  T Get(double t) {
    if (IsTimeSampled()) {
      // TODO: lookup value by t
      return timeSamples.Get(t);
    }
    return value;
  }

  Animatable() {}
  Animatable(T v) : value(v) {}

};

//template<typename T>
//using Animatable = nonstd::variant<T, TimeSampled<T>>;

// Frequently used types
using AnimatableFloat = Animatable<float>;
using AnimatableDouble = Animatable<double>;
using AnimatableExtent = Animatable<Extent>;
using AnimatableVisibility = Animatable<Visibility>;
using AnimatableVec3f = Animatable<Vec3f>;
using AnimatableVec3fArray = Animatable<std::vector<Vec3f>>;
using AnimatableFloatArray = Animatable<std::vector<float>>;

// Generic "class" Node
// Mostly identical to GPrim
struct Klass {

  std::string name;
  int64_t parent_id{-1};  // Index to parent node

  std::vector<std::pair<ListEditQual, AssetReference>> references;

  std::map<std::string, Property> props;
};

struct MaterialBindingAPI
{
  std::string materialBinding; // rel material:binding
  std::string materialBindingCorrection; // rel material:binding:correction

  // TODO: allPurpose, preview, ...
};

//
// Predefined node classes
//

struct Xform {

  std::string name;
  int64_t parent_id{-1};  // Index to xform node

  std::vector<XformOp> xformOps;

  Orientation orientation{Orientation::RightHanded};
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

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

  using UVCoordType = nonstd::variant<std::vector<Vec2f>, std::vector<Vec3f>>;

  std::string name;
  UVCoordType buffer;

  Interpolation interpolation{Interpolation::Vertex};
  Variability variability;

  // non-empty when UV has its own indices.
  std::vector<uint32_t> indices;  // UV indices. Usually varying
};

struct GeomBoundable {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomCone {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{1.0};
  Axis axis{Axis::Z};

  AnimatableExtent extent{Extent({-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomCapsule {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{0.5};
  Axis axis{Axis::Z};

  AnimatableExtent extent{Extent({-0.5, -0.5, -1.0}, {0.5, 0.5, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};


struct GeomCylinder {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableDouble height{2.0};
  AnimatableDouble radius{1.0};
  Axis axis{Axis::Z};
  AnimatableExtent extent{Extent({-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomCube {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Properties
  //
  AnimatableDouble size{2.0};
  AnimatableExtent extent{Extent({-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

struct GeomSphere {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Predefined attribs.
  //
  AnimatableDouble radius{1.0};

  //
  // Properties
  //
  AnimatableExtent extent{Extent({-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0})};  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` are not stored here(stored in `widths`)
  std::map<std::string, PrimAttrib> attribs;
};

//
// Basis Curves(for hair/fur)
//
struct GeomBasisCurves {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  // Interpolation attribute
  std::string type = "cubic";  // "linear", "cubic"

  std::string basis =
      "bspline";  // "bezier", "catmullRom", "bspline" ("hermite" and "power" is
                  // not supported in TinyUSDZ)

  std::string wrap = "nonperiodic";  // "nonperiodic", "periodic", "pinned"

  //
  // Predefined attribs.
  //
  std::vector<Vec3f> points;
  std::vector<Vec3f> normals;  // normal3f
  std::vector<int> curveVertexCounts;
  std::vector<float> widths;
  std::vector<Vec3f> velocities;     // vector3f
  std::vector<Vec3f> accelerations;  // vector3f

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

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
  std::vector<Vec3f> points;   // float3
  std::vector<Vec3f> normals;  // normal3f
  std::vector<float> widths;
  std::vector<int64_t> ids;          // per-point ids
  std::vector<Vec3f> velocities;     // vector3f
  std::vector<Vec3f> accelerations;  // vector3f

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

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
  AnimatableExtent extent;
  Purpose purpose{Purpose::Default};
  AnimatableVisibility visibility{Visibility::Inherited};

  // TODO
  // std::vector<std::string> xformOpOrder;
  // ref proxyPrim
};

// Skelton
struct Skelton {
  std::vector<Matrix4d>
      bindTransforms;  // bind-pose transform of each joint in world coordinate.
  AnimatableExtent extent;

  std::vector<std::string> jointNames;
  std::vector<std::string> joints;

  std::vector<Matrix4d> restTransforms;  // rest-pose transforms of each joint
                                         // in local coordinate.

  Purpose purpose{Purpose::Default};
  AnimatableVisibility visibility{Visibility::Inherited};

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

// Generic Prim
struct GPrim {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  std::string prim_type; // Primitive type(if specified by `def`)

  // Gprim
  AnimatableExtent extent;  // bounding extent(in local coord?).
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

  std::map<std::string, Property> props;

  bool _valid{true}; // default behavior is valid(allow empty GPrim)

  // child nodes
  std::vector<GPrim> children;
};

// Polygon mesh geometry
struct GeomMesh {
  std::string name;

  int64_t parent_id{-1};  // Index to parent node

  //
  // Predefined attribs.
  //
  std::vector<primvar::point3f> points;  // point3f
  PrimAttrib normals;         // float3f[]

  //
  // Utility functions
  //

  // Initialize GeomMesh by GPrim(prepend references)
  void Initialize(const GPrim &pprim);

  // Update GeomMesh by GPrim(append references)
  void UpdateBy(const GPrim &pprim);

  size_t GetNumPoints() const;
  size_t GetNumFacevaryingNormals() const;

  // Get `normals` as float3 array + facevarying
  // Return false if `normals` is neither float3[] type nor `varying`
  bool GetFacevaryingNormals(std::vector<float> *v) const;

  // Get `texcoords` as float2 array + facevarying
  // Return false if `texcoords` is neither float2[] type nor `varying`
  bool GetFacevaryingTexcoords(std::vector<float> *v) const;

  // Primary UV coords(TODO: Remove. Read uv coords through PrimVarReader)
  UVCoords st;

  PrimAttrib velocitiess;  // Usually float3[], varying

  std::vector<int32_t> faceVertexCounts;
  std::vector<int32_t> faceVertexIndices;

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  std::string facevaryingLinearInterpolation = "cornerPlus1";
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // Gprim
  bool doubleSided{false};
  Orientation orientation{Orientation::RightHanded};
  AnimatableVec3fArray displayColor; // primvars:displayColor
  AnimatableFloatArray displayOpacity; // primvars:displaOpacity

  MaterialBindingAPI materialBinding;

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
  SubdivisionScheme subdivisionScheme{SubdivisionScheme::CatmullClark};

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

  TextureWrap wrapS{};
  TextureWrap wrapT{};

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

  PrimvarReaderType st;  // texture coordinate(`inputs:st`). We assume there is a
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
    extent.lower[0] = -(std::numeric_limits<float>::max)();
    extent.lower[1] = -(std::numeric_limits<float>::max)();
    extent.lower[2] = -(std::numeric_limits<float>::max)();
    extent.upper[0] = (std::numeric_limits<float>::max)();
    extent.upper[1] = (std::numeric_limits<float>::max)();
    extent.upper[2] = (std::numeric_limits<float>::max)();
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

// Simple volume class.
// Currently this is just an placeholder. Not implemented.

struct OpenVDBAsset {
  std::string fieldDataType{"float"};
  std::string fieldName{"density"};
  std::string filePath; // asset
};

// MagicaVoxel Vox 
struct VoxAsset {
  std::string fieldDataType{"float"};
  std::string fieldName{"density"};
  std::string filePath; // asset
};

struct Volume {
  OpenVDBAsset vdb;
  VoxAsset vox;
};



// Corresponds to USD's Scope.
// `Scope` is uncommon in graphics community, so we use `Group`.
// From USD doc: Scope is the simplest grouping primitive, and does not carry
// the baggage of transformability.
struct Group {
  std::string name;

  int64_t parent_id{-1};

  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};
};

#if 0 // TODO: Remove
///
/// Represent value(TODO: Remove).
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

  bool GetFloat(float *ret) const {
    if (dtype.id == VALUE_TYPE_FLOAT) {
      float d = *reinterpret_cast<const float *>(data.data());
      (*ret) = d;
      return true;
    }
    return false;
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
#endif


Interpolation InterpolationFromString(const std::string &v);
Orientation OrientationFromString(const std::string &v);


}  // namespace tinyusdz
