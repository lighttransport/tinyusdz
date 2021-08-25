#pragma once

#include "prim-types.hh"

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
inline Matrix3f RotateX(float theta)
{
  Matrix3f m;

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

inline Matrix3d RotateX(double theta)
{
  Matrix3d m;

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

} // namespace math

} // namespace tinyusdz
