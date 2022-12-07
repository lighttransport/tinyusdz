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

// TODO: simple subtraction is not robust. Use more robust fp diff compare.
inline bool is_close(float a, float b, const float eps) {
  float d = a - b;
  return (d * d) <= (eps * eps);
}

inline bool is_close(double a, double b, const double eps) {
  double d = a - b;
  return (d * d) <= (eps * eps);
}

inline bool is_close(value::float3 a, value::float3 b, const float eps) {
  value::float3 d = a - b;
  d = d * d;

  return (d[0] <= (eps * eps)) && (d[1] <= (eps * eps)) && (d[2] <= (eps * eps));
}

inline bool is_close(value::double3 a, value::double3 b, const double eps) {
  value::double3 d = a - b;
  d = d * d;

  return (d[0] <= (eps * eps)) && (d[1] <= (eps * eps)) && (d[2] <= (eps * eps));
}

} // namespace math

} // namespace tinyusdz
