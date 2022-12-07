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

inline bool is_close(float a, float b, const float eps) {
  // TODO: Use more robust fp diff compare.
  float d = a - b;
  return (d * d) <= (eps * eps);
}

inline bool is_close(double a, double b, const double eps) {
  // TODO: Use more robust fp diff compare.
  double d = a - b;
  return (d * d) <= (eps * eps);
}

} // namespace math

} // namespace tinyusdz
