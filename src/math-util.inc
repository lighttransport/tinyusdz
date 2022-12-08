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

// Simple subtraction is not robust. Use more robust fp diff compare.
// https://embeddeduse.com/2019/08/26/qt-compare-two-floats/
// https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
// TODO: ulp based comparison for more robust compare
inline bool is_close(float a, float b, const float eps) {
  float d = a - b;
  if (std::fabs(d) <= eps) {
    return true;
  }

  return std::fabs(d) <= (eps * std::fmax(std::fabs(a), std::fabs(b))); 
}

inline bool is_close(double a, double b, const double eps) {
  double d = a - b;
  if (std::fabs(d) <= eps) {
    return true;
  }
  return std::fabs(d) <= (eps * std::fmax(std::fabs(a), std::fabs(b))); 
}

inline bool is_close(value::float3 a, value::float3 b, const float eps) {
  return is_close(a[0], b[0], eps) && is_close(a[1], b[1], eps) && is_close(a[2], b[2], eps);
}

inline bool is_close(value::double3 a, value::double3 b, const double eps) {
  return is_close(a[0], b[0], eps) && is_close(a[1], b[1], eps) && is_close(a[2], b[2], eps);
}

} // namespace math

} // namespace tinyusdz
