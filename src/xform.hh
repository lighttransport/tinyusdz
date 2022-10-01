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

//
// For usdGeom, usdLux
// TODO: Move to `xform.hh`?
//
struct Xformable {
  ///
  /// Evaluate XformOps and output evaluated(concatenated) matrix to `out_matrix`
  /// `resetXformStack` become true when xformOps[0] is !resetXformStack!
  /// Return error message when failed.
  ///
  bool EvaluateXformOps(value::matrix4d *out_matrix, bool *resetXformStack, std::string *err) const;

  ///
  /// Global = Parent x Local
  ///
  nonstd::expected<value::matrix4d, std::string> GetGlobalMatrix(
      const value::matrix4d &parentMatrix) const {
    bool resetXformStack{false};

    auto m = GetLocalMatrix(&resetXformStack);

    if (m) {
      if (resetXformStack) {
        // Ignore parent's transform
        // FIXME: Validate this is the correct way of handling !resetXformStack! op.
        return m.value();
      } else {
        value::matrix4d cm =
            Mult<value::matrix4d, double, 4>(parentMatrix, m.value());
        return cm;
      }
    } else {
      return nonstd::make_unexpected(m.error());
    }
  }

  ///
  /// Evaluate xformOps and get local matrix.
  ///
  nonstd::expected<value::matrix4d, std::string> GetLocalMatrix(bool *resetTransformStack = nullptr) const {
    if (_dirty) {
      value::matrix4d m;
      std::string err;
      if (EvaluateXformOps(&m, resetTransformStack, &err)) {
        _matrix = m;
        _dirty = false;
      } else {
        return nonstd::make_unexpected(err);
      }
    }

    return _matrix;
  }

  void SetDirty(bool onoff) { _dirty = onoff; }

  std::vector<XformOp> xformOps;

  mutable bool _dirty{true};
  mutable value::matrix4d _matrix;  // Matrix of this Xform(local matrix)
};


} // namespace tinyusdz
