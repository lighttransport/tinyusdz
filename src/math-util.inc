#pragma once

#include "prim-types.hh"
#include "value-types.hh"

namespace tinyusdz {

namespace math {

inline float radian(float angle)
{
  return 3.141592f * angle / 180.0f;
}

inline double radian(double angle)
{
  return 3.141592653589793 * angle / 180.0;
}

inline float angle(float radian)
{
  return radian * 180.0f / 3.141592f;
}

inline double angle(double radian)
{
  return radian * 180.0 / 3.141592653589793;
}

// `theta` in radian
// Moved to xform.hh
#if 0
inline value::matrix3f RotateX(float theta)
{
  value::matrix3f m;

  auto sth = std::sin(theta);
  auto cth = std::cos(theta);

  m.m[0][0] = 1.0f;
  m.m[0][1] = 0.0f;
  m.m[0][2] = 0.0f;
  m.m[1][0] = 0.0f;
  m.m[1][1] = cth;
  m.m[1][2] = -sth;
  m.m[2][0] = 0.0f;
  m.m[2][1] = sth;
  m.m[2][2] = cth;

  return m;
}

inline value::matrix3d RotateX(double theta)
{
  value::matrix3d m;

  auto sth = std::sin(theta);
  auto cth = std::cos(theta);

  m.m[0][0] = 1.0;
  m.m[0][1] = 0.0;
  m.m[0][2] = 0.0;
  m.m[1][0] = 0.0;
  m.m[1][1] = cth;
  m.m[1][2] = -sth;
  m.m[2][0] = 0.0;
  m.m[2][1] = sth;
  m.m[2][2] = cth;

  return m;
}
#endif

inline value::float3 operator-(const value::float3 &a, const value::float3 &b)
{
  return value::float3({a[0] - b[0], a[1] - b[1], a[2] - b[2]});
}

inline value::double3 operator-(const value::double3 &a, const value::double3 &b)
{
  return value::double3({a[0] - b[0], a[1] - b[1], a[2] - b[2]});
}

inline float vlength(const value::float3 &a) {
  float d2 = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
  if (d2 > std::numeric_limits<float>::epsilon()) {
    return std::sqrt(d2);
  }
  return 0.0f;
}

inline value::float3 vnormalize(const value::float3 &a) {
  float len = vlength(a);
  return value::float3({a[0] / len, a[1] / len, a[2] / len});
}

inline double vlength(const value::double3 &a) {
  double d2 = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
  if (d2 > std::numeric_limits<double>::epsilon()) {
    return std::sqrt(d2);
  }
  return 0.0;
}

inline value::double3 vnormalize(const value::double3 &a) {
  double len = vlength(a);
  return value::double3({a[0] / len, a[1] / len, a[2] / len});
}

inline value::float3 vcross(const value::float3 &a, const value::float3 &b)
{
  value::float3 n;
  n[0] = a[1] * b[2] - a[2] * b[1];
  n[1] = a[2] * b[0] - a[0] * b[2];
  n[2] = a[0] * b[1] - a[1] * b[0];

  return n;
}

inline value::float3 geometric_normal(const value::float3 &p0, const value::float3 &p1, const value::float3 &p2)
{
  // CCW
  value::float3 n = vcross(p1 - p0, p2 - p0);

  return vnormalize(n);
}

inline value::double3 vcross(const value::double3 &a, const value::double3 &b)
{
  value::double3 n;
  n[0] = a[1] * b[2] - a[2] * b[1];
  n[1] = a[2] * b[0] - a[0] * b[2];
  n[2] = a[0] * b[1] - a[1] * b[0];

  return n;
}

inline value::double3 geometric_normal(const value::double3 &p0, const value::double3 &p1, const value::double3 &p2)
{
  // CCW
  value::double3 n = vcross(p1 - p0, p2 - p0);

  return vnormalize(n);
}

inline float vdot(const value::float3 &a, const value::float3 &b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline double vdot(const value::double3 &a, const value::double3 &b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace math

} // namespace tinyusdz
