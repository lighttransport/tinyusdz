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

using matrix44d = linalg::aliases::double4x4;
using matrix33d = linalg::aliases::double3x3;
using double3x3 = linalg::aliases::double3x3;

// linalg quat: (x, y, z, w)

value::matrix3d to_matrix3x3(const value::quath &q)
{
  double3x3 m33 = linalg::qmat<double>({double(half_to_float(q.imag[0])), double(half_to_float(q.imag[1])), double(half_to_float(q.imag[2])), double(half_to_float(q.real))});

  value::matrix3d m;
  Identity(&m);

  memcpy(m.m, &m33[0][0], sizeof(double) * 3 * 3);

  return m;
}

value::matrix3d to_matrix3x3(const value::quatf &q)
{
  double3x3 m33 = linalg::qmat<double>({double(q.imag[0]), double(q.imag[1]), double(q.imag[2]), double(q.real)});

  value::matrix3d m;
  Identity(&m);

  memcpy(m.m, &m33[0][0], sizeof(double) * 3 * 3);

  return m;
}

value::matrix3d to_matrix3x3(const value::quatd &q)
{
  double3x3 m33 = linalg::qmat<double>({q.imag[0], q.imag[1], q.imag[2], q.real});

  value::matrix3d m;
  Identity(&m);

  memcpy(m.m, &m33[0][0], sizeof(double) * 3 * 3);

  return m;
}

value::matrix4d to_matrix(const value::matrix3d &m33, const value::double3 &tx)
{

  value::matrix4d m;
  Identity(&m);

  m.m[0][0] = m33.m[0][0];
  m.m[0][1] = m33.m[0][1];
  m.m[0][2] = m33.m[0][2];
  m.m[1][0] = m33.m[1][0];
  m.m[1][1] = m33.m[1][1];
  m.m[1][2] = m33.m[1][2];
  m.m[2][0] = m33.m[2][0];
  m.m[2][1] = m33.m[2][1];
  m.m[2][2] = m33.m[2][2];

  m.m[3][0] = tx[0];
  m.m[3][1] = tx[1];
  m.m[3][2] = tx[2];

  return m;
}


value::matrix4d to_matrix(const value::quath &q)
{
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

value::matrix4d invert(const value::matrix4d &_m) {

  matrix44d m;
  // memory layout is same
  memcpy(&m[0][0], _m.m, sizeof(double) * 4 * 4);

  matrix44d inv_m = linalg::inverse(m);

  value::matrix4d outm;

  memcpy(outm.m, &inv_m[0][0], sizeof(double) * 4 * 4);

  return outm;
}

value::matrix3d invert3x3(const value::matrix3d &_m) {

  matrix33d m;
  // memory layout is same
  memcpy(&m[0][0], _m.m, sizeof(double) * 3 * 3);

  matrix33d inv_m = linalg::inverse(m);

  value::matrix3d outm;

  memcpy(outm.m, &inv_m[0][0], sizeof(double) * 3 * 3);

  return outm;
}

double determinant(const value::matrix4d &_m) {

  matrix44d m;
  // memory layout is same
  memcpy(&m[0][0], _m.m, sizeof(double) * 4 * 4);

  double det = linalg::determinant(m);

  return det;
}

double determinant3x3(const value::matrix3d &_m) {

  matrix33d m;
  // memory layout is same
  memcpy(&m[0][0], _m.m, sizeof(double) * 3 * 3);

  double det = linalg::determinant(m);

  return det;
}

bool invert(const value::matrix4d &_m, value::matrix4d &inv_m) {

  double det = determinant(_m);

  // 1e-9 comes from pxrUSD
  // determinant should be positive(absolute), but take a fabs() just in case.
  if (std::fabs(det) < 1e-9) {
    return false;
  }

  inv_m = invert(_m);
  return true;
}

bool invert3x3(const value::matrix3d &_m, value::matrix3d &inv_m) {

  double det = determinant3x3(_m);

  // 1e-9 comes from pxrUSD
  // determinant should be positive(absolute), but take a fabs() just in case.
  if (std::fabs(det) < 1e-9) {
    return false;
  }

  inv_m = invert3x3(_m);
  return true;
}

} // namespace tinyusdz
