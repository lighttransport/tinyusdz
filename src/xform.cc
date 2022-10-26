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
#include "tiny-format.hh"
#include "pprinter.hh"

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

namespace {

///
/// Xform evaluation with method chain style.
///
class XformEvaluator {
 public:
  XformEvaluator() { Identity(&m); }

  XformEvaluator &RotateX(const double angle) { // in degrees

    double rad = math::radian(angle);

    value::matrix4d rm;

    rm.m[1][1] = std::cos(rad);
    rm.m[1][2] = std::sin(rad);
    rm.m[2][1] = -std::sin(rad);
    rm.m[2][2] = std::cos(rad);

    m = Mult<value::matrix4d, double, 4>(m, rm);

    return (*this);
  }

  XformEvaluator &RotateY(const double angle) { // in degrees

    double rad = math::radian(angle);

    value::matrix4d rm;

    rm.m[0][0] = std::cos(rad);
    rm.m[0][2] = -std::sin(rad);
    rm.m[2][0] = std::sin(rad);
    rm.m[2][2] = std::cos(rad);

    m = Mult<value::matrix4d, double, 4>(m, rm);

    return (*this);
  }

  XformEvaluator &RotateZ(const double angle) { // in degrees

    double rad = math::radian(angle);

    value::matrix4d rm;

    rm.m[0][0] = std::cos(rad);
    rm.m[0][1] = std::sin(rad);
    rm.m[1][0] = -std::sin(rad);
    rm.m[1][1] = std::cos(rad);

    m = Mult<value::matrix4d, double, 4>(m, rm);

    return (*this);
  }

  std::string error() const {
    return err;
  }

  nonstd::expected<value::matrix4d, std::string> result() const {
    if (err.empty()) {
      return m;
    }

    return nonstd::make_unexpected(err);
  }

  std::string err;
  value::matrix4d m;
};

} // namespace local

bool Xformable::EvaluateXformOps(value::matrix4d *out_matrix,
                                 bool *resetXformStack,
                                 std::string *err) const {
  const auto RotateABC = [](const XformOp &x) -> nonstd::expected<value::matrix4d, std::string>  {

    value::double3 v;
    if (auto h = x.get_scalar_value<value::half3>()) {
      v[0] = double(half_to_float(h.value()[0]));
      v[1] = double(half_to_float(h.value()[1]));
      v[2] = double(half_to_float(h.value()[2]));
    } else if (auto f = x.get_scalar_value<value::float3>()) {
      v[0] = double(f.value()[0]);
      v[1] = double(f.value()[1]);
      v[2] = double(f.value()[2]);
    } else if (auto d = x.get_scalar_value<value::double3>()) {
      v = d.value();
    } else {
      if (x.suffix.empty()) {
        return nonstd::make_unexpected(fmt::format("`{}` is not half3, float3 or double3 type.\n", to_string(x.op_type)));
      } else {
        return nonstd::make_unexpected(fmt::format("`{}:{}` is not half3, float3 or double3 type.\n", to_string(x.op_type), x.suffix));
      }
    }

    // invert input, and compute concatenated matrix
    // inv(ABC) = inv(A) x inv(B) x inv(C)
    // as done in pxrUSD.

    if (x.inverted) {
      v[0] = -v[0];
      v[1] = -v[1];
      v[2] = -v[2];
    }

    double xAngle = v[0];
    double yAngle = v[1];
    double zAngle = v[2];

    XformEvaluator eval;

    if (x.inverted) {
      if (x.op_type == XformOp::OpType::RotateXYZ) {
        eval.RotateZ(zAngle);
        eval.RotateY(yAngle);
        eval.RotateX(xAngle);
      } else if (x.op_type == XformOp::OpType::RotateXZY) {
        eval.RotateY(yAngle);
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
      } else if (x.op_type == XformOp::OpType::RotateYXZ) {
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
      } else if (x.op_type == XformOp::OpType::RotateYZX) {
        eval.RotateX(xAngle);
        eval.RotateZ(zAngle);
        eval.RotateY(yAngle);
      } else if (x.op_type == XformOp::OpType::RotateZYX) {
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
        eval.RotateZ(zAngle);
      } else if (x.op_type == XformOp::OpType::RotateZXY) {
        eval.RotateY(yAngle);
        eval.RotateX(xAngle);
        eval.RotateZ(zAngle);
      } else {
        /// ???
        return nonstd::make_unexpected("[InternalError] RotateABC");
      }
    } else {
      if (x.op_type == XformOp::OpType::RotateXYZ) {
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
        eval.RotateZ(zAngle);
      } else if (x.op_type == XformOp::OpType::RotateXZY) {
        eval.RotateX(xAngle);
        eval.RotateZ(zAngle);
        eval.RotateY(yAngle);
      } else if (x.op_type == XformOp::OpType::RotateYXZ) {
        eval.RotateY(yAngle);
        eval.RotateX(xAngle);
        eval.RotateZ(zAngle);
      } else if (x.op_type == XformOp::OpType::RotateYZX) {
        eval.RotateY(yAngle);
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
      } else if (x.op_type == XformOp::OpType::RotateZYX) {
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
      } else if (x.op_type == XformOp::OpType::RotateZXY) {
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
      } else {
        /// ???
        return nonstd::make_unexpected("[InternalError] RotateABC");
      }
    }

    return eval.result();

  };


  // Concat matrices
  //
  // Matrix concatenation ordering is its appearance order(right to left)
  // This is same with a notation in math equation: i.e,
  //
  // xformOpOrder = [A, B, C]
  //
  // M = A x B x C
  //
  value::matrix4d cm;
  Identity(&cm);

  for (size_t i = 0; i < xformOps.size(); i++) {
    const auto x = xformOps[i];

    value::matrix4d m; // local matrix
    Identity(&m);

    if (x.is_timesamples()) {
      if (err) {
        (*err) += "TODO: xformOp property with timeSamples.\n";
      }
      return false;
    }

    switch (x.op_type) {
      case XformOp::OpType::ResetXformStack: {
        if (i != 0) {
          if (err) {
            (*err) +=
                "!resetXformStack! should only appear at the first element of "
                "xformOps\n";
          }
          return false;
        }

        // Notify resetting previous(parent node's) matrices
        if (resetXformStack) {
          (*resetXformStack) = true;
        }
        break;
      }
      case XformOp::OpType::Transform: {
        if (auto sxf = x.get_scalar_value<value::matrix4f>()) {
          value::matrix4f mf = sxf.value();
          for (size_t j = 0; j < 4; j++) {
            for (size_t k = 0; k < 4; k++) {
              m.m[j][k] = double(mf.m[j][k]);
            }
          }
        } else if (auto sxd = x.get_scalar_value<value::matrix4d>()) {
          m = sxd.value();
        } else {
          if (err) {
            (*err) += "`xformOp:transform` is not matrix4f or matrix4d type.\n";
          }
          return false;
        }

        if (x.inverted) {

          // Singular check.
          // pxrUSD uses 1e-9
          double det = determinant(m);

          if (std::fabs(det) < 1e-9) {
            if (err) {
              if (x.suffix.empty()) {
                (*err) += "`xformOp:transform` is singular matrix and cannot be inverted.\n";
              } else {
                (*err) += fmt::format("`xformOp:transform:{}` is singular matrix and cannot be inverted.\n", x.suffix);
              }
            }

            return false;
          }

          m = invert(m);
        }

        break;
      }
      case XformOp::OpType::Scale: {
        double sx, sy, sz;

        if (auto sxh = x.get_scalar_value<value::half3>()) {
          sx = double(half_to_float(sxh.value()[0]));
          sy = double(half_to_float(sxh.value()[1]));
          sz = double(half_to_float(sxh.value()[2]));
        } else if (auto sxf = x.get_scalar_value<value::float3>()) {
          sx = double(sxf.value()[0]);
          sy = double(sxf.value()[1]);
          sz = double(sxf.value()[2]);
        } else if (auto sxd = x.get_scalar_value<value::double3>()) {
          sx = sxd.value()[0];
          sy = sxd.value()[1];
          sz = sxd.value()[2];
        } else {
          if (err) {
            (*err) += "`xformOp:scale` is not half3, float3 or double3 type.\n";
          }
          return false;
        }

        if (x.inverted) {
          // FIXME: Safe division
          sx = 1.0 / sx;
          sy = 1.0 / sy;
          sz = 1.0 / sz;
        }

        m.m[0][0] = sx;
        m.m[1][1] = sy;
        m.m[2][2] = sz;

        break;
      }
      case XformOp::OpType::Translate: {
        double tx, ty, tz;
        if (auto txh = x.get_scalar_value<value::half3>()) {
          tx = double(half_to_float(txh.value()[0]));
          ty = double(half_to_float(txh.value()[1]));
          tz = double(half_to_float(txh.value()[2]));
        } else if (auto txf = x.get_scalar_value<value::float3>()) {
          tx = double(txf.value()[0]);
          ty = double(txf.value()[1]);
          tz = double(txf.value()[2]);
        } else if (auto txd = x.get_scalar_value<value::double3>()) {
          tx = txd.value()[0];
          ty = txd.value()[1];
          tz = txd.value()[2];
        } else {
          if (err) {
            (*err) += "`xformOp:translate` is not half3, float3 or double3 type.\n";
          }
          return false;
        }

        if (x.inverted) {
          tx = -tx;
          ty = -ty;
          tz = -tz;
        }

        m.m[3][0] = tx;
        m.m[3][1] = ty;
        m.m[3][2] = tz;

        break;
      }
      // FIXME: Validate ROTATE_X, _Y, _Z implementation
      case XformOp::OpType::RotateX: {

        double angle; // in degrees
        if (auto h = x.get_scalar_value<value::half>()) {
          angle = double(half_to_float(h.value()));
        } else if (auto f = x.get_scalar_value<float>()) {
          angle = double(f.value());
        } else if (auto d = x.get_scalar_value<double>()) {
          angle = d.value();
        } else {
          if (err) {
            if (x.suffix.empty()) {
              (*err) += "`xformOp:rotateX` is not half, float or double type.\n";
            } else {
              (*err) += fmt::format("`xformOp:rotateX:{}` is not half, float or double type.\n", x.suffix);
            }
          }
          return false;
        }

        XformEvaluator xe;
        xe.RotateX(angle);
        auto ret = xe.result();

        if (ret) {
          m = ret.value();
        } else {
          if (err) {
            (*err) += ret.error();
          }
          return false;
        }
        break;
      }
      case XformOp::OpType::RotateY: {
        double angle; // in degrees
        if (auto h = x.get_scalar_value<value::half>()) {
          angle = double(half_to_float(h.value()));
        } else if (auto f = x.get_scalar_value<float>()) {
          angle = double(f.value());
        } else if (auto d = x.get_scalar_value<double>()) {
          angle = d.value();
        } else {
          if (err) {
            if (x.suffix.empty()) {
              (*err) += "`xformOp:rotateY` is not half, float or double type.\n";
            } else {
              (*err) += fmt::format("`xformOp:rotateY:{}` is not half, float or double type.\n", x.suffix);
            }
          }
          return false;
        }

        XformEvaluator xe;
        xe.RotateY(angle);
        auto ret = xe.result();

        if (ret) {
          m = ret.value();
        } else {
          if (err) {
            (*err) += ret.error();
          }
          return false;
        }
        break;
      }
      case XformOp::OpType::RotateZ: {
        double angle; // in degrees
        if (auto h = x.get_scalar_value<value::half>()) {
          angle = double(half_to_float(h.value()));
        } else if (auto f = x.get_scalar_value<float>()) {
          angle = double(f.value());
        } else if (auto d = x.get_scalar_value<double>()) {
          angle = d.value();
        } else {
          if (err) {
            if (x.suffix.empty()) {
              (*err) += "`xformOp:rotateZ` is not half, float or double type.\n";
            } else {
              (*err) += fmt::format("`xformOp:rotateZ:{}` is not half, float or double type.\n", x.suffix);
            }
          }
          return false;
        }

        XformEvaluator xe;
        xe.RotateZ(angle);
        auto ret = xe.result();

        if (ret) {
          m = ret.value();
        } else {
          if (err) {
            (*err) += ret.error();
          }
          return false;
        }
        break;
      }
      case XformOp::OpType::Orient: {
        // value::quat stores elements in (x, y, z, w)
        // linalg::quat also stores elements in (x, y, z, w)

        value::matrix3d rm;
        if (auto h = x.get_scalar_value<value::quath>()) {
          rm = to_matrix3x3(h.value());
        } else if (auto f = x.get_scalar_value<value::quatf>()) {
          rm = to_matrix3x3(f.value());
        } else if (auto d = x.get_scalar_value<value::quatd>()) {
          rm = to_matrix3x3(d.value());
        } else {
          if (err) {
            if (x.suffix.empty()) {
              (*err) += "`xformOp:orient` is not quath, quatf or quatd type.\n";
            } else {
              (*err) += fmt::format("`xformOp:orient:{}` is not quath, quatf or quatd type.\n", x.suffix);
            }
          }
          return false;
        }

        // FIXME: invert before getting matrix.
        if (x.inverted) {
          value::matrix3d inv_rm;
          if (invert3x3(rm, inv_rm)) {

          } else {
            if (err) {
              if (x.suffix.empty()) {
                (*err) += "`xformOp:orient` is singular and cannot be inverted.\n";
              } else {
                (*err) += fmt::format("`xformOp:orient:{}` is singular and cannot be inverted.\n", x.suffix);
              }
            }
          }

          rm = inv_rm;
        }

        m = to_matrix(rm, {0.0, 0.0, 0.0});

        break;
      }

      case XformOp::OpType::RotateXYZ:
      case XformOp::OpType::RotateXZY:
      case XformOp::OpType::RotateYXZ:
      case XformOp::OpType::RotateYZX:
      case XformOp::OpType::RotateZXY:
      case XformOp::OpType::RotateZYX: {
        auto ret = RotateABC(x);

        if (!ret) {
          (*err) += ret.error();
          return false;
        }

        m = ret.value();

      }
    }

    cm = Mult<value::matrix4d, double, 4>(cm, m);
  }

  (*out_matrix) = cm;

  return true;
}

std::vector<value::token> Xformable::xformOpOrder() const {
  std::vector<value::token> toks;

  for (size_t i = 0; i < xformOps.size(); i++) {
    std::string ss;

    auto xformOp = xformOps[i];

    if (xformOp.inverted) {
      ss += "!invert!";
    }
    ss += to_string(xformOp.op_type);
    if (!xformOp.suffix.empty()) {
      ss += ":" + xformOp.suffix;
    }

    toks.push_back(value::token(ss));
  }

  return toks;

}

} // namespace tinyusdz
