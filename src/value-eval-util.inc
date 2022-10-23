#pragma once

#include "value-types.hh"

inline tinyusdz::value::float2 operator+(const tinyusdz::value::float2 &a, const tinyusdz::value::float2 &b) {
  return {a[0] + b[0], a[1] + b[1]};
}

inline tinyusdz::value::float2 operator+(const float a, const tinyusdz::value::float2 &b) {
  return {a + b[0], a + b[1]};
}

inline tinyusdz::value::float2 operator+(const tinyusdz::value::float2 &a, const float b) {
  return {a[0] + b, a[1] + b};
}

inline tinyusdz::value::float3 operator+(const tinyusdz::value::float3 &a, const tinyusdz::value::float3 &b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

inline tinyusdz::value::float3 operator+(const float a, const tinyusdz::value::float3 &b) {
  return {a + b[0], a + b[1], a + b[2]};
}

inline tinyusdz::value::float3 operator+(const tinyusdz::value::float3 &a, const float b) {
  return {a[0] + b, a[1] + b, a[2] + b};
}

inline tinyusdz::value::float4 operator+(const tinyusdz::value::float4 &a, const tinyusdz::value::float4 &b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
}

inline tinyusdz::value::float4 operator+(const float a, const tinyusdz::value::float4 &b) {
  return {a + b[0], a + b[1], a + b[2], a + b[3]};
}

inline tinyusdz::value::float4 operator+(const tinyusdz::value::float4 &a, const float b) {
  return {a[0] + b, a[1] + b, a[2] + b, a[3] + b};
}

inline tinyusdz::value::float2 operator-(const tinyusdz::value::float2 &a, const tinyusdz::value::float2 &b) {
  return {a[0] - b[0], a[1] - b[1]};
}

inline tinyusdz::value::float2 operator-(const float a, const tinyusdz::value::float2 &b) {
  return {a - b[0], a - b[1]};
}

inline tinyusdz::value::float2 operator-(const tinyusdz::value::float2 &a, const float b) {
  return {a[0] - b, a[1] - b};
}

inline tinyusdz::value::float3 operator-(const tinyusdz::value::float3 &a, const tinyusdz::value::float3 &b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

inline tinyusdz::value::float3 operator-(const float a, const tinyusdz::value::float3 &b) {
  return {a - b[0], a - b[1], a - b[2]};
}

inline tinyusdz::value::float3 operator-(const tinyusdz::value::float3 &a, const float b) {
  return {a[0] - b, a[1] - b, a[2] - b};
}

inline tinyusdz::value::float4 operator-(const tinyusdz::value::float4 &a, const tinyusdz::value::float4 &b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]};
}

inline tinyusdz::value::float4 operator-(const float a, const tinyusdz::value::float4 &b) {
  return {a - b[0], a - b[1], a - b[2], a - b[3]};
}

inline tinyusdz::value::float4 operator-(const tinyusdz::value::float4 &a, const float b) {
  return {a[0] - b, a[1] - b, a[2] - b, a[3] - b};
}

inline tinyusdz::value::float2 operator*(const tinyusdz::value::float2 &a, const tinyusdz::value::float2 &b) {
  return {a[0] * b[0], a[1] * b[1]};
}

inline tinyusdz::value::float2 operator*(const float a, const tinyusdz::value::float2 &b) {
  return {a * b[0], a * b[1]};
}

inline tinyusdz::value::float2 operator*(const tinyusdz::value::float2 &a, const float b) {
  return {a[0] * b, a[1] * b};
}

inline tinyusdz::value::float3 operator*(const tinyusdz::value::float3 &a, const tinyusdz::value::float3 &b) {
  return {a[0] * b[0], a[1] * b[1], a[2] * b[2]};
}

inline tinyusdz::value::float3 operator*(const float a, const tinyusdz::value::float3 &b) {
  return {a * b[0], a * b[1], a * b[2]};
}

inline tinyusdz::value::float3 operator*(const tinyusdz::value::float3 &a, const float b) {
  return {a[0] * b, a[1] * b, a[2] * b};
}

inline tinyusdz::value::float4 operator*(const tinyusdz::value::float4 &a, const tinyusdz::value::float4 &b) {
  return {a[0] * b[0], a[1] * b[1], a[2] * b[2], a[3] * b[3]};
}

inline tinyusdz::value::float4 operator*(const float a, const tinyusdz::value::float4 &b) {
  return {a * b[0], a * b[1], a * b[2], a * b[3]};
}

inline tinyusdz::value::float4 operator*(const tinyusdz::value::float4 &a, const float b) {

  return {a[0] * b, a[1] * b, a[2] * b, a[3] * b};
}

namespace tinyusdz {

// TODO: Move to other header file.
template <typename T>
inline T lerp(const T &a, const T &b, const double t) {
  return (1.0 - t) * a + t * b;
}

template <>
inline float lerp(const float &a, const float &b, const double t) {
  return float(1.0 - t) * a + float(t) * b;
}

template <>
inline value::float2 lerp(const value::float2 &a, const value::float2 &b, const double t) {
  return float(1.0 - t) * a + float(t) * b;
}

template <>
inline value::float3 lerp(const value::float3 &a, const value::float3 &b, const double t) {
  return float(1.0 - t) * a + float(t) * b;
}

template <>
inline value::float4 lerp(const value::float4 &a, const value::float4 &b, const double t) {
  return float(1.0 - t) * a + float(t) * b;
}

template <typename T>
inline std::vector<T> lerp(const std::vector<T> &a, const std::vector<T> &b,
                           const double t) {
  std::vector<T> dst;

  // Choose shorter one
  size_t n = std::min(a.size(), b.size());
  if (n == 0) {
    return dst;
  }

  dst.resize(n);

  if (a.size() != b.size()) {
    return dst;
  }
  for (size_t i = 0; i < n; i++) {
    dst[i] = lerp(a[i], b[i], t);
  }

  return dst;
}

// specializations of lerp
template <>
inline value::AssetPath lerp(const value::AssetPath &a,
                             const value::AssetPath &b, const double t) {
  (void)b;
  (void)t;
  // no interpolation
  return a;
}

template <>
inline std::vector<value::AssetPath> lerp(
    const std::vector<value::AssetPath> &a,
    const std::vector<value::AssetPath> &b, const double t) {
  (void)b;
  (void)t;
  // no interpolation
  return a;
}


} // namespace tinyusdz
