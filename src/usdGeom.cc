// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdGeom API implementations

#include "usdGeom.hh"

#include <sstream>

#include "common-macros.inc"
#include "pprinter.hh"
#include "prim-types.hh"
#include "tiny-format.hh"
#include "xform.hh"
//
#include "math-util.inc"

namespace tinyusdz {

namespace {

constexpr auto kPrimvarsNormals = "primvars:normals";

}  // namespace

const std::vector<value::point3f> GeomMesh::GetPoints(
    double time, TimeSampleInterpolationType interp) const {
  std::vector<value::point3f> dst;

  if (!points.authored() || points.IsBlocked()) {
    return dst;
  }

  if (points.IsConnection()) {
    // TODO: connection
    return dst;
  }

  if (auto pv = points.GetValue()) {
    if (pv.value().IsTimeSamples()) {
      if (auto tsv = pv.value().ts.TryGet(time, interp)) {
        dst = tsv.value();
      }
    } else if (pv.value().IsScalar()) {
      dst = pv.value().value;
    }
  }

  return dst;
}

const std::vector<value::normal3f> GeomMesh::GetNormals(
    double time, TimeSampleInterpolationType interp) const {
  std::vector<value::normal3f> dst;

  if (props.count(kPrimvarsNormals)) {
    const auto prop = props.at(kPrimvarsNormals);
    if (prop.IsRel()) {
      // TODO:
      return dst;
    }

    if (prop.GetAttrib().get_var().is_timesample()) {
      // TODO:
      return dst;
    }

    if (prop.GetAttrib().type_name() == "normal3f[]") {
      if (auto pv =
              prop.GetAttrib().get_value<std::vector<value::normal3f>>()) {
        dst = pv.value();
      }
    }
  } else if (normals.authored()) {
    if (normals.IsConnection()) {
      // TODO
      return dst;
    } else if (normals.IsBlocked()) {
      return dst;
    }

    if (normals.GetValue()) {
      if (normals.GetValue().value().IsTimeSamples()) {
        // TODO
        (void)time;
        (void)interp;
        return dst;
      }

      dst = normals.GetValue().value().value;
    }
  }

  return dst;
}

Interpolation GeomMesh::GetNormalsInterpolation() const {
  if (props.count(kPrimvarsNormals)) {
    const auto &prop = props.at(kPrimvarsNormals);
    if (prop.GetAttrib().type_name() == "normal3f[]") {
      if (prop.GetAttrib().meta.interpolation) {
        return prop.GetAttrib().meta.interpolation.value();
      }
    }
  } else if (normals.meta.interpolation) {
    return normals.meta.interpolation.value();
  }

  return Interpolation::Vertex;  // default 'vertex'
}

void GeomMesh::Initialize(const GPrim &gprim) {
  name = gprim.name;
  parent_id = gprim.parent_id;

  props = gprim.props;

#if 0
  for (auto &prop_item : gprim.props) {
    std::string attr_name = std::get<0>(prop_item);
    const Property &prop = std::get<1>(prop_item);

    if (prop.is_rel) {
      //LOG_INFO("TODO: Rel property:" + attr_name);
      continue;
    }

    const PrimAttrib &attr = prop.GetAttrib();

    if (attr_name == "points") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  points = *p;
      //}
    } else if (attr_name == "faceVertexIndices") {
      //if (auto p = primvar::as_vector<int>(&attr.var)) {
      //  faceVertexIndices = *p;
      //}
    } else if (attr_name == "faceVertexCounts") {
      //if (auto p = primvar::as_vector<int>(&attr.var)) {
      //  faceVertexCounts = *p;
      //}
    } else if (attr_name == "normals") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  normals.var = *p;
      //  normals.interpolation = attr.interpolation;
      //}
    } else if (attr_name == "velocitiess") {
      //if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
      //  velocitiess.var = (*p);
      //  velocitiess.interpolation = attr.interpolation;
      //}
    } else if (attr_name == "primvars:uv") {
      //if (auto pv2f = primvar::as_vector<Vec2f>(&attr.var)) {
      //  st.buffer = (*pv2f);
      //  st.interpolation = attr.interpolation;
      //} else if (auto pv3f = primvar::as_vector<value::float3>(&attr.var)) {
      //  st.buffer = (*pv3f);
      //  st.interpolation = attr.interpolation;
      //}
    } else {
      // Generic PrimAtrr
      props[attr_name] = attr;
    }

  }
#endif

  doubleSided = gprim.doubleSided;
  orientation = gprim.orientation;
  visibility = gprim.visibility;
  extent = gprim.extent;
  purpose = gprim.purpose;

  // displayColor = gprim.displayColor;
  // displayOpacity = gprim.displayOpacity;

#if 0  // TODO


  // PrimVar(TODO: Remove)
  UVCoords st;

  //
  // Properties
  //

#endif
};

nonstd::expected<bool, std::string> GeomMesh::ValidateGeomSubset() {
  std::stringstream ss;

  if (geom_subset_children.empty()) {
    return true;
  }

  auto CheckFaceIds = [](const size_t nfaces, const std::vector<uint32_t> ids) {
    if (std::any_of(ids.begin(), ids.end(),
                    [&nfaces](uint32_t id) { return id >= nfaces; })) {
      return false;
    }

    return true;
  };

  if (!faceVertexCounts.authored()) {
    // No `faceVertexCounts` definition
    ss << "`faceVerexCounts` attribute is not present in GeomMesh.\n";
    return nonstd::make_unexpected(ss.str());
  }

  if (faceVertexCounts.authored()) {
    return nonstd::make_unexpected("TODO: Support faceVertexCounts.connect\n");
  }

  if (faceVertexCounts.GetValue()) {
    const auto fvp = faceVertexCounts.GetValue();
    const auto &fv = fvp.value().value;
    size_t n = fv.size();

    // Currently we only check if face ids are valid.
    for (size_t i = 0; i < geom_subset_children.size(); i++) {
      const GeomSubset &subset = geom_subset_children[i];

      if (!CheckFaceIds(n, subset.indices)) {
        ss << "Face index out-of-range.\n";
        return nonstd::make_unexpected(ss.str());
      }
    }
  }

  // TODO
  return nonstd::make_unexpected(
      "TODO: Implent GeomMesh::ValidateGeomSubset\n");
}

namespace {

#if 0
value::matrix4d GetTransform(XformOp xform)
{
  value::matrix4d m;
  Identity(&m);

  if (xform.op == XformOp::OpType::TRANSFORM) {
    if (auto v = xform.value.get<value::matrix4d>()) {
      m = v.value();
    }
  } else if (xform.op == XformOp::OpType::TRANSLATE) {
      if (auto sf = xform.value.get<value::float3>()) {
        m.m[3][0] = double(sf.value()[0]);
        m.m[3][1] = double(sf.value()[1]);
        m.m[3][2] = double(sf.value()[2]);
      } else if (auto sd = xform.value.get<value::double3>()) {
        m.m[3][0] = sd.value()[0];
        m.m[3][1] = sd.value()[1];
        m.m[3][2] = sd.value()[2];
      }
  } else if (xform.op == XformOp::OpType::SCALE) {
      if (auto sf = xform.value.get<value::float3>()) {
        m.m[0][0] = double(sf.value()[0]);
        m.m[1][1] = double(sf.value()[1]);
        m.m[2][2] = double(sf.value()[2]);
      } else if (auto sd = xform.value.get<value::double3>()) {
        m.m[0][0] = sd.value()[0];
        m.m[1][1] = sd.value()[1];
        m.m[2][2] = sd.value()[2];
      }
  } else {
    DCOUT("TODO: xform.op = " << XformOp::GetOpTypeName(xform.op));
  }

  return m;
}
#endif

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

}  // namespace

bool Xformable::EvaluateXformOps(value::matrix4d *out_matrix,
                                 bool *resetXformStack,
                                 std::string *err) const {
  value::matrix4d cm;

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
        return nonstd::make_unexpected(fmt::format("`{}` is not half3, float3 or double3 type.\n", to_string(x.op)));
      } else {
        return nonstd::make_unexpected(fmt::format("`{}:{}` is not half3, float3 or double3 type.\n", to_string(x.op), x.suffix));
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
      if (x.op == XformOp::OpType::RotateXYZ) {
        eval.RotateZ(zAngle);
        eval.RotateY(yAngle);
        eval.RotateX(xAngle);
      } else if (x.op == XformOp::OpType::RotateXZY) {
        eval.RotateY(yAngle);
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
      } else if (x.op == XformOp::OpType::RotateYXZ) {
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
      } else if (x.op == XformOp::OpType::RotateYZX) {
        eval.RotateX(xAngle);
        eval.RotateZ(zAngle);
        eval.RotateY(yAngle);
      } else if (x.op == XformOp::OpType::RotateZYX) {
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
        eval.RotateZ(zAngle);
      } else if (x.op == XformOp::OpType::RotateZXY) {
        eval.RotateY(yAngle);
        eval.RotateX(xAngle);
        eval.RotateZ(zAngle);
      } else {
        /// ???
        return nonstd::make_unexpected("[InternalError] RotateABC");
      }
    } else {
      if (x.op == XformOp::OpType::RotateXYZ) {
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
        eval.RotateZ(zAngle);
      } else if (x.op == XformOp::OpType::RotateXZY) {
        eval.RotateX(xAngle);
        eval.RotateZ(zAngle);
        eval.RotateY(yAngle);
      } else if (x.op == XformOp::OpType::RotateYXZ) {
        eval.RotateY(yAngle);
        eval.RotateX(xAngle);
        eval.RotateZ(zAngle);
      } else if (x.op == XformOp::OpType::RotateYZX) {
        eval.RotateY(yAngle);
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
      } else if (x.op == XformOp::OpType::RotateZYX) {
        eval.RotateZ(zAngle);
        eval.RotateX(xAngle);
        eval.RotateY(yAngle);
      } else if (x.op == XformOp::OpType::RotateZXY) {
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
  for (size_t i = 0; i < xformOps.size(); i++) {
    const auto x = xformOps[i];

    value::matrix4d m;
    Identity(&m);
    (void)x;

    if (x.IsTimeSamples()) {
      if (err) {
        (*err) += "TODO: xformOp property with timeSamples.\n";
      }
      return false;
    }

    switch (x.op) {
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

}  // namespace tinyusdz
