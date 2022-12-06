// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.

//
// NOTE: pxrUSD uses row-major format for matrix, so use row-major format in tinyusdz as well.
// 
#pragma once

#include "value-types.hh"

namespace tinyusdz {

value::matrix4d to_matrix(const value::quath &q);
value::matrix4d to_matrix(const value::quatf &q);
value::matrix4d to_matrix(const value::quatd &q);

value::matrix4d to_matrix(const value::matrix3d &m, const value::double3 &tx);

//
// | x x x 0 |
// | x x x 0 |
// | x x x 0 |
// | 0 0 0 1 |
// Remove [3][*](translation) and [*][3]
// [3][3] is set to 1.0.
value::matrix4d upper_left_3x3_only(const value::matrix4d &m);

// Decompose into Upper-left 3x3 matrix + translation
value::matrix3d to_matrix3x3(const value::matrix4d &m, value::double3 *tx = nullptr);

value::matrix3d to_matrix3x3(const value::quath &q);
value::matrix3d to_matrix3x3(const value::quatf &q);
value::matrix3d to_matrix3x3(const value::quatd &q);

value::matrix4d inverse(const value::matrix4d &m);
double determinant(const value::matrix4d &m);

value::matrix3d inverse(const value::matrix3d &m);
double determinant(const value::matrix3d &m);

// Do singular matrix check.
// Return true and set `inv_m` when input matrix `m` can be inverted
bool inverse(const value::matrix4d &m, value::matrix4d &inv_m);
bool inverse(const value::matrix3d &m, value::matrix3d &inv_m);

value::matrix2d transpose(const value::matrix2d &m);
value::matrix3d transpose(const value::matrix3d &m);

// NOTE: Full matrix transpose(i.e, translation elements are transposed). 
// So if you want to transform normal vector, first make input matrix elements upper-left 3x3 only,
// then transpose(inverse(upper_left_3x3_only(M)))
value::matrix4d transpose(const value::matrix4d &m); 

#if 0 // Use transform() or transform_dir() for a while.
value::float3 matmul(const value::matrix4d &m, const value::float3 &p);
value::point3f matmul(const value::matrix4d &m, const value::point3f &p);
value::normal3f matmul(const value::matrix4d &m, const value::normal3f &p);
value::vector3f matmul(const value::matrix4d &m, const value::vector3f &p);

value::point3d matmul(const value::matrix4d &m, const value::point3d &p);
value::normal3d matmul(const value::matrix4d &m, const value::normal3d &p);
value::vector3d matmul(const value::matrix4d &m, const value::vector3d &p);
value::double3 matmul(const value::matrix4d &m, const value::double3 &p);
#endif

value::float4 matmul(const value::matrix4d &m, const value::float4 &p);
value::double4 matmul(const value::matrix4d &m, const value::double4 &p);

//
// Transform 3d vector using 4x4 matrix.
// ([3][3] is not used)
//
value::float3 transform(const value::matrix4d &m, const value::float3 &p);
value::vector3f transform(const value::matrix4d &m, const value::vector3f &p);
value::normal3f transform(const value::matrix4d &m, const value::normal3f &p);
value::point3f transform(const value::matrix4d &m, const value::point3f &p);
value::double3 transform(const value::matrix4d &m, const value::double3 &p);
value::vector3d transform(const value::matrix4d &m, const value::vector3d &p);
value::normal3d transform(const value::matrix4d &m, const value::normal3d &p);
value::point3d transform(const value::matrix4d &m, const value::point3d &p);

//
// Transform 3d vector using upper-left 3x3 matrix elements.
// ([3][3] is not used)
//
value::float3 transform_dir(const value::matrix4d &m, const value::float3 &p);
value::vector3f transform_dir(const value::matrix4d &m, const value::vector3f &p);
value::normal3f transform_dir(const value::matrix4d &m, const value::normal3f &p);
value::point3f transform_dir(const value::matrix4d &m, const value::point3f &p);
value::double3 transform_dir(const value::matrix4d &m, const value::double3 &p);
value::vector3d transform_dir(const value::matrix4d &m, const value::vector3d &p);
value::normal3d transform_dir(const value::matrix4d &m, const value::normal3d &p);
value::point3d transform_dir(const value::matrix4d &m, const value::point3d &p);


///
/// For usdGeom, usdSkel, usdLux
///
/// TODO: TimeSample support.
struct Xformable {
  ///
  /// Evaluate XformOps and output evaluated(concatenated) matrix to `out_matrix`
  /// `resetXformStack` become true when xformOps[0] is !resetXformStack!
  /// Return error message when failed.
  ///
  /// @param[in] t Time
  /// @param[in] tinterp TimeSample interpolation type
  ///
  bool EvaluateXformOps(double t, value::TimeSampleInterpolationType tinterp, value::matrix4d *out_matrix, bool *resetXformStack, std::string *err) const;

  ///
  /// Global = Parent x Local
  ///
  nonstd::expected<value::matrix4d, std::string> GetGlobalMatrix(
      const value::matrix4d &parentMatrix, double t = value::TimeCode::Default(), value::TimeSampleInterpolationType tinterp = value::TimeSampleInterpolationType::Held) const {
    bool resetXformStack{false};

    auto m = GetLocalMatrix(t, tinterp, &resetXformStack);

    if (m) {
      if (resetXformStack) {
        // Ignore parent's transform
        // FIXME: Validate this is the correct way of handling !resetXformStack! op.
        return m.value();
      } else {
        value::matrix4d cm =
            value::Mult<value::matrix4d, double, 4>(parentMatrix, m.value());
        return cm;
      }
    } else {
      return nonstd::make_unexpected(m.error());
    }
  }

  ///
  /// Evaluate xformOps and get local matrix.
  ///
  /// @param[out] resetTransformStack Is xformOpOrder contains !resetTransformStack!? 
  ///
  nonstd::expected<value::matrix4d, std::string> GetLocalMatrix(double t = value::TimeCode::Default(), value::TimeSampleInterpolationType tinterp = value::TimeSampleInterpolationType::Held, bool *resetTransformStack = nullptr) const {
    if (_dirty) {
      value::matrix4d m;
      std::string err;
      if (EvaluateXformOps(t, tinterp, &m, resetTransformStack, &err)) {
        _matrix = m;
        _dirty = false;
      } else {
        return nonstd::make_unexpected(err);
      }
    }

    return _matrix;
  }

  void set_dirty(bool onoff) { _dirty = onoff; }

  // Return `token[]` representation of `xformOps`
  std::vector<value::token> xformOpOrder() const;

  std::vector<XformOp> xformOps;

  mutable bool _dirty{true};
  mutable value::matrix4d _matrix;  // Matrix of this Xform(local matrix)
};


} // namespace tinyusdz
