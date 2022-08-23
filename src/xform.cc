// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/linalg.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "value-types.hh"
#include "prim-types.hh"
#include "math-util.inc"
#include "xform.hh"

namespace tinyusdz {

// linalg quat: (x, y, z, w)

value::matrix4d to_matrix(const value::quath &q)
{
  using double3x3 = linalg::aliases::double3x3;
  //using double4 = linalg::aliases::double4;
  
  double3x3 m33 = linalg::qmat<double>({double(half_to_float(q.imag[0])), double(half_to_float(q.imag[1])), double(half_to_float(q.imag[2])), double(half_to_float(q.real))});

  value::matrix4d m;
  Identity(&m);

  m.m[0][0] = m33[0][0];
  m.m[0][1] = m33[0][1];
  m.m[0][2] = m33[0][2];
  m.m[1][0] = m33[1][0];
  m.m[1][1] = m33[1][1];
  m.m[1][2] = m33[1][2];
  m.m[2][0] = m33[2][0];
  m.m[2][1] = m33[2][1];
  m.m[2][2] = m33[2][2];

  return m;
}

value::matrix4d to_matrix(const value::quatf &q)
{
  using double3x3 = linalg::aliases::double3x3;
  
  double3x3 m33 = linalg::qmat<double>({double(q.imag[0]), double(q.imag[1]), double(q.imag[2]), double(q.real)});

  value::matrix4d m;
  Identity(&m);

  m.m[0][0] = m33[0][0];
  m.m[0][1] = m33[0][1];
  m.m[0][2] = m33[0][2];
  m.m[1][0] = m33[1][0];
  m.m[1][1] = m33[1][1];
  m.m[1][2] = m33[1][2];
  m.m[2][0] = m33[2][0];
  m.m[2][1] = m33[2][1];
  m.m[2][2] = m33[2][2];

  return m;
}

value::matrix4d to_matrix(const value::quatd &q)
{
  using double3x3 = linalg::aliases::double3x3;
  
  double3x3 m33 = linalg::qmat<double>({q.imag[0], q.imag[1], q.imag[2], q.real});

  value::matrix4d m;
  Identity(&m);

  m.m[0][0] = m33[0][0];
  m.m[0][1] = m33[0][1];
  m.m[0][2] = m33[0][2];
  m.m[1][0] = m33[1][0];
  m.m[1][1] = m33[1][1];
  m.m[1][2] = m33[1][2];
  m.m[2][0] = m33[2][0];
  m.m[2][1] = m33[2][1];
  m.m[2][2] = m33[2][2];

  return m;
}



} // namespace tinyusdz
