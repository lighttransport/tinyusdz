// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.

#pragma once

#include "value-types.hh"

namespace tinyusdz {

value::matrix4d to_matrix(const value::quath &q);
value::matrix4d to_matrix(const value::quatf &q);
value::matrix4d to_matrix(const value::quatd &q);

value::matrix4d to_matrix(const value::matrix3d &m, const value::double3 &tx);

value::matrix3d to_matrix3x3(const value::quath &q);
value::matrix3d to_matrix3x3(const value::quatf &q);
value::matrix3d to_matrix3x3(const value::quatd &q);

value::matrix4d invert(const value::matrix4d &m);
double determinant(const value::matrix4d &m);

value::matrix3d invert3x3(const value::matrix3d &m);
double determinant3x3(const value::matrix3d &m);

// Do singular matrix check.
// Return true and set `inv_m` when input matrix `m` can be inverted
bool invert(const value::matrix4d &m, value::matrix4d &inv_m);
bool invert3x3(const value::matrix3d &m, value::matrix3d &inv_m);


} // namespace tinyusdz
