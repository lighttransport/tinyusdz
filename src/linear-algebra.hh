// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
#pragma once

#include "value-types.hh"

namespace tinyusdz {

value::quath slerp(const value::quath &a, const value::quath &b, const float t);
value::quatf slerp(const value::quatf &a, const value::quatf &b, const float t);
value::quatd slerp(const value::quatd &a, const value::quatd &b, const double t);

float vlength(const value::float3 &a);
float vlength(const value::normal3f &a);
float vlength(const value::vector3f &a);
double vlength(const value::double3 &a);

value::float3 vnormalize(const value::float3 &a);
value::double3 vnormalize(const value::double3 &a);
value::normal3f vnormalize(const value::normal3f &a);
value::vector3f vnormalize(const value::vector3f &a);

// Assume CCW(Counter ClockWise)
value::float3 vcross(const value::float3 &a, const value::float3 &b);
value::double3 vcross(const value::double3 &a, const value::double3 &b);
value::float3 geometric_normal(const value::float3 &p0, const value::float3 &p1, const value::float3 &p2);
value::double3 geometric_normal(const value::double3 &p0, const value::double3 &p1, const value::double3 &p2);


inline float vdot(const value::float3 &a, const value::float3 &b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline double vdot(const value::double3 &a, const value::double3 &b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace tinyusdz
