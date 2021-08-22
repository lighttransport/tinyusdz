// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <vector>
#include <cstdlib>
#include <string>

namespace tinyusdz {

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
struct None
{

};

using TimeSampleType = nonstd::variant<None, float, double, Vec3f, Quatf, Matrix4d>;

struct TimeSamples {
  std::vector<double> times;
  std::vector<TimeSampleType> values;
};


// Types which can be TimeSampledData are restricted to frequently used one in TinyUSDZ.
//typedef std::vector<std::pair<uint64_t, nonstd::optional<float>>> TimeSampledDataFloat;
//typedef std::vector<std::pair<uint64_t, nonstd::optional<double>>> TimeSampledDataDouble;
//typedef std::vector<std::pair<uint64_t, nonstd::optional<std::array<float, 3>>>> TimeSampledDataFloat3;
//typedef std::vector<std::pair<uint64_t, nonstd::optional<std::array<double, 3>>>> TimeSampledDataDouble3;
//typedef std::vector<std::pair<uint64_t, nonstd::optional<Matrix4d>>> TimeSampledDataMatrix4d;

//using TimeSampledValue = nonstd::variant<nonstd::monostate, TimeSampledDataFloat, TimeSampledDataDouble, TimeSampledDataFloat3, TimeSampledDataDouble3, TimeSampledDataMatrix4d>;



} // namespace tinyusdz
