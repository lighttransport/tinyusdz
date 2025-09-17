// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Mathematical value types for TinyUSDZ
// Part of the value-types.hh modularization effort

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>

namespace tinyusdz {
namespace value {

// Half precision float
struct half {
  uint16_t value;
  
  half() : value(0) {}
  explicit half(uint16_t v) : value(v) {}
  
  bool operator==(const half &rhs) const { return value == rhs.value; }
  bool operator!=(const half &rhs) const { return value != rhs.value; }
};

// Half precision vector types
using half2 = std::array<half, 2>;
using half3 = std::array<half, 3>;
using half4 = std::array<half, 4>;

// Float vector types
using float2 = std::array<float, 2>;
using float3 = std::array<float, 3>;
using float4 = std::array<float, 4>;

// Double vector types
using double2 = std::array<double, 2>;
using double3 = std::array<double, 3>;
using double4 = std::array<double, 4>;

// Conversion functions for half precision
float half_to_float(value::half h);
half float_to_half_full(float f);

// Half precision operators
inline half operator+(const half &a, const half &b) {
  return float_to_half_full(half_to_float(a) + half_to_float(b));
}

inline half operator-(const half &a, const half &b) {
  return float_to_half_full(half_to_float(a) - half_to_float(b));
}

inline half operator*(const half &a, const half &b) {
  return float_to_half_full(half_to_float(a) * half_to_float(b));
}

inline half operator/(const half &a, const half &b) {
  return float_to_half_full(half_to_float(a) / half_to_float(b));
}

// Matrix types
template <typename T, size_t N>
class matrix {
 public:
  using value_type = T;
  static constexpr size_t dimension = N;
  
  matrix() { std::memset(m, 0, sizeof(m)); }
  
  // Column-major storage (OpenGL/USD convention)
  T m[N][N];
  
  T* operator[](size_t i) { return m[i]; }
  const T* operator[](size_t i) const { return m[i]; }
  
  static matrix<T, N> identity() {
    matrix<T, N> mat;
    for (size_t i = 0; i < N; ++i) {
      mat.m[i][i] = T(1);
    }
    return mat;
  }
  
  bool operator==(const matrix<T, N> &rhs) const {
    return std::memcmp(m, rhs.m, sizeof(m)) == 0;
  }
  
  bool operator!=(const matrix<T, N> &rhs) const {
    return !(*this == rhs);
  }
};

// Common matrix types
using matrix2d = matrix<double, 2>;
using matrix3d = matrix<double, 3>;
using matrix4d = matrix<double, 4>;

using matrix2f = matrix<float, 2>;
using matrix3f = matrix<float, 3>;
using matrix4f = matrix<float, 4>;

// Quaternion types
template <typename T>
class quaternion {
 public:
  using value_type = T;
  
  quaternion() : real(1), imaginary{0, 0, 0} {}
  quaternion(T r, T i, T j, T k) : real(r), imaginary{i, j, k} {}
  quaternion(T r, const std::array<T, 3> &im) : real(r), imaginary(im) {}
  
  T real;
  std::array<T, 3> imaginary;
  
  static quaternion<T> identity() {
    return quaternion<T>(T(1), T(0), T(0), T(0));
  }
  
  T GetReal() const { return real; }
  const std::array<T, 3>& GetImaginary() const { return imaginary; }
  
  void SetReal(T r) { real = r; }
  void SetImaginary(const std::array<T, 3> &im) { imaginary = im; }
  
  bool operator==(const quaternion<T> &rhs) const {
    return (real == rhs.real) && (imaginary == rhs.imaginary);
  }
  
  bool operator!=(const quaternion<T> &rhs) const {
    return !(*this == rhs);
  }
  
  // Normalize the quaternion
  void Normalize() {
    T norm = std::sqrt(real * real + 
                      imaginary[0] * imaginary[0] + 
                      imaginary[1] * imaginary[1] + 
                      imaginary[2] * imaginary[2]);
    if (norm > T(0)) {
      T inv = T(1) / norm;
      real *= inv;
      imaginary[0] *= inv;
      imaginary[1] *= inv;
      imaginary[2] *= inv;
    }
  }
};

// Common quaternion types
using quath = quaternion<half>;
using quatf = quaternion<float>;
using quatd = quaternion<double>;

// Normal and point types (semantically different from float3)
struct normal3h {
  std::array<half, 3> value;
  
  bool operator==(const normal3h &rhs) const { return value == rhs.value; }
  bool operator!=(const normal3h &rhs) const { return !(*this == rhs); }
};

struct normal3f {
  std::array<float, 3> value;
  
  bool operator==(const normal3f &rhs) const { return value == rhs.value; }
  bool operator!=(const normal3f &rhs) const { return !(*this == rhs); }
};

struct normal3d {
  std::array<double, 3> value;
  
  bool operator==(const normal3d &rhs) const { return value == rhs.value; }
  bool operator!=(const normal3d &rhs) const { return !(*this == rhs); }
};

struct point3h {
  std::array<half, 3> value;
  
  bool operator==(const point3h &rhs) const { return value == rhs.value; }
  bool operator!=(const point3h &rhs) const { return !(*this == rhs); }
};

struct point3f {
  std::array<float, 3> value;
  
  bool operator==(const point3f &rhs) const { return value == rhs.value; }
  bool operator!=(const point3f &rhs) const { return !(*this == rhs); }
};

struct point3d {
  std::array<double, 3> value;
  
  bool operator==(const point3d &rhs) const { return value == rhs.value; }
  bool operator!=(const point3d &rhs) const { return !(*this == rhs); }
};

struct vector3h {
  std::array<half, 3> value;
  
  bool operator==(const vector3h &rhs) const { return value == rhs.value; }
  bool operator!=(const vector3h &rhs) const { return !(*this == rhs); }
};

struct vector3f {
  std::array<float, 3> value;
  
  bool operator==(const vector3f &rhs) const { return value == rhs.value; }
  bool operator!=(const vector3f &rhs) const { return !(*this == rhs); }
};

struct vector3d {
  std::array<double, 3> value;
  
  bool operator==(const vector3d &rhs) const { return value == rhs.value; }
  bool operator!=(const vector3d &rhs) const { return !(*this == rhs); }
};

// Color types
using color3h = std::array<half, 3>;
using color3f = std::array<float, 3>;
using color3d = std::array<double, 3>;

using color4h = std::array<half, 4>;
using color4f = std::array<float, 4>;
using color4d = std::array<double, 4>;

// Frame type (4x4 matrix)
using frame4d = matrix4d;

// Type name constants
constexpr auto kHalf = "half";
constexpr auto kHalf2 = "half2";
constexpr auto kHalf3 = "half3";
constexpr auto kHalf4 = "half4";

constexpr auto kFloat = "float";
constexpr auto kFloat2 = "float2";
constexpr auto kFloat3 = "float3";
constexpr auto kFloat4 = "float4";

constexpr auto kDouble = "double";
constexpr auto kDouble2 = "double2";
constexpr auto kDouble3 = "double3";
constexpr auto kDouble4 = "double4";

constexpr auto kMatrix2d = "matrix2d";
constexpr auto kMatrix3d = "matrix3d";
constexpr auto kMatrix4d = "matrix4d";

constexpr auto kQuath = "quath";
constexpr auto kQuatf = "quatf";
constexpr auto kQuatd = "quatd";

constexpr auto kNormal3h = "normal3h";
constexpr auto kNormal3f = "normal3f";
constexpr auto kNormal3d = "normal3d";

constexpr auto kPoint3h = "point3h";
constexpr auto kPoint3f = "point3f";
constexpr auto kPoint3d = "point3d";

constexpr auto kVector3h = "vector3h";
constexpr auto kVector3f = "vector3f";
constexpr auto kVector3d = "vector3d";

constexpr auto kColor3h = "color3h";
constexpr auto kColor3f = "color3f";
constexpr auto kColor3d = "color3d";

constexpr auto kColor4h = "color4h";
constexpr auto kColor4f = "color4f";
constexpr auto kColor4d = "color4d";

constexpr auto kFrame4d = "frame4d";

// Rect type (bounding box)
template <typename T>
struct rect2 {
  std::array<T, 2> min;
  std::array<T, 2> max;
  
  bool operator==(const rect2<T> &rhs) const {
    return (min == rhs.min) && (max == rhs.max);
  }
};

using rect2i = rect2<int>;
using rect2f = rect2<float>;
using rect2d = rect2<double>;

// Texcoord types
using texcoord2h = std::array<half, 2>;
using texcoord2f = std::array<float, 2>;
using texcoord2d = std::array<double, 2>;

using texcoord3h = std::array<half, 3>;
using texcoord3f = std::array<float, 3>;
using texcoord3d = std::array<double, 3>;

constexpr auto kTexCoord2h = "texCoord2h";
constexpr auto kTexCoord2f = "texCoord2f";
constexpr auto kTexCoord2d = "texCoord2d";
constexpr auto kTexCoord3h = "texCoord3h";
constexpr auto kTexCoord3f = "texCoord3f";
constexpr auto kTexCoord3d = "texCoord3d";

} // namespace value
} // namespace tinyusdz