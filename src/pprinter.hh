#pragma once

//
// pretty-print routine(using iostream) in non-intrusive way.
// Some build configuration may not want I/O module(e.g. mobile/embedded device), so provide print routines in separated file.
//
// TODO: Move `print` related code in other files to here.
//

#include <string>
#include <ostream>
#include <sstream>

#include "prim-types.hh"

namespace tinyusdz {
namespace {

std::string Indent(uint32_t n) {
  std::stringstream ss;

  for (uint32_t i = 0; i < n; i++) {
    ss << "  ";
  }

  return ss.str();
}

} // namespace

inline std::string to_string(tinyusdz::Interpolation interp) {
  switch (interp) {
    case InterpolationInvalid:
      return "[[Invalid interpolation value]]";
    case InterpolationConstant:
      return "constant";
    case InterpolationUniform:
      return "uniform";
    case InterpolationVarying:
      return "varying";
    case InterpolationVertex:
      return "vertex";
    case InterpolationFaceVarying:
      return "faceVarying";
  }

  // Never reach here though
  return "[[Invalid interpolation value]]";
}

} // namespace tinyusdz

namespace std {

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec2f &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec3f &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec4f &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec2d &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec3d &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec4d &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Matrix4f &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << ", " << m.m[0][2] << ", " << m.m[0][3] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ", " << m.m[1][2] << ", " << m.m[1][3] << "), ";
  ofs << "(" << m.m[2][0] << ", " << m.m[2][1] << ", " << m.m[2][2] << ", " << m.m[2][3] << "), ";
  ofs << "(" << m.m[3][0] << ", " << m.m[3][1] << ", " << m.m[3][2] << ", " << m.m[3][3] << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Matrix4d &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << ", " << m.m[0][2] << ", "
      << m.m[0][3] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ", " << m.m[1][2] << ", "
      << m.m[1][3] << "), ";
  ofs << "(" << m.m[2][0] << ", " << m.m[2][1] << ", " << m.m[2][2] << ", "
      << m.m[2][3] << "), ";
  ofs << "(" << m.m[3][0] << ", " << m.m[3][1] << ", " << m.m[3][2] << ", "
      << m.m[3][3] << ")";

  ofs << " )";

  return ofs;
}

template<typename T>
inline std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
  os << "[";
  for (size_t i = 0; i < v.size(); i++) {
    os << v[i];
    if (i != (v.size() -1)) {
      os << ", ";
    }
  }
  os << "]";
  return os;
}


} // namespace std

namespace tinyusdz {

inline std::string to_string(tinyusdz::Visibility v) {
  if (v == tinyusdz::VisibilityInherited) {
    return "\"inherited\"";
  } else {
    return "\"invisible\"";
  }
}

inline std::string to_string(tinyusdz::Orientation o) {
  if (o == tinyusdz::OrientationRightHanded) {
    return "\"rightHanded\"";
  } else {
    return "\"leftHanded\"";
  }
}



inline std::string to_string(tinyusdz::Extent e) {
  std::stringstream ss;

  ss << "[" << e.lower << ", " << e.upper << "]";

  return ss.str();
}

inline std::string to_string(const tinyusdz::Klass &klass, uint32_t indent = 0) {
  std::stringstream ss;

  ss << tinyusdz::Indent(indent) << "class " << klass.name << " (\n";
  ss << tinyusdz::Indent(indent) << ")\n";
  ss << tinyusdz::Indent(indent) << "{\n";

  for (auto prop : klass.props) {

    if (auto prel = nonstd::get_if<tinyusdz::Rel>(&prop.second)) {
        ss << "TODO: Rel\n";
    } else if (auto pattr = nonstd::get_if<tinyusdz::PrimAttrib>(&prop.second)) {
      if (auto p = tinyusdz::primvar::as<double>(&pattr->var)) {
        ss << tinyusdz::Indent(indent);
        if (pattr->custom) {
          ss << " custom ";
        }
        if (pattr->uniform) {
          ss << " uniform ";
        }
        ss << " double " << prop.first << " = " << *p;
      } else {
        ss << "TODO:" << pattr->type_name << "\n";
      }
    }

    ss << "\n";
  }

  ss << tinyusdz::Indent(indent) << "}\n";

  return ss.str();
}

static std::string to_string(const GPrim &gprim, const uint32_t indent = 0) {
  std::stringstream ss;

  ss << Indent(indent) << "def \"" << gprim.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // props
  // TODO:

  ss << Indent(indent) << "  visibility = " << to_string(gprim.visibility)
     << "\n";

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const Xform &xform, const uint32_t indent = 0) {
  std::stringstream ss;

  ss << Indent(indent) << "def Xform \"" << xform.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // props
  if (xform.xformOps.size()) {
    for (size_t i = 0; i < xform.xformOps.size(); i++) {
      auto xformOp = xform.xformOps[i];
      ss << Indent(indent);
      ss << tinyusdz::XformOp::GetOpTypeName(xformOp.op);
      if (!xformOp.suffix.empty()) {
        ss << ":" << xformOp.suffix;
      }

      // TODO
      // ss << " = " << to_string(xformOp.value);

      ss << "\n";
    }
  }

  // xformOpOrder
  if (xform.xformOps.size()) {
    ss << Indent(indent) << "uniform token[] xformOpOrder = [";
    for (size_t i = 0; i < xform.xformOps.size(); i++) {
      auto xformOp = xform.xformOps[i];
      ss << "\"" << tinyusdz::XformOp::GetOpTypeName(xformOp.op);
      if (!xformOp.suffix.empty()) {
        ss << ":" << xformOp.suffix;
      }
      ss << "\"";
      if (i != (xform.xformOps.size() - 1)) {
        ss << ",\n";
      }
    }
    ss << "]\n";
  }

  ss << Indent(indent) << "  visibility = " << to_string(xform.visibility)
     << "\n";

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const GeomSphere &sphere, const uint32_t indent = 0) {
  std::stringstream ss;

  ss << Indent(indent) << "def Sphere \"" << sphere.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius = " << sphere.radius << "\n";
  ss << Indent(indent) << "  float3[] extent = " << to_string(sphere.extent)
     << "\n";
  ss << Indent(indent) << "  orientation = " << to_string(sphere.orientation)
     << "\n";
  ss << Indent(indent) << "  visibility = " << to_string(sphere.visibility)
     << "\n";

  // primvars
  if (!sphere.displayColor.empty()) {
    ss << Indent(indent) << "  primvars:displayColor = [";
    for (size_t i = 0; i < sphere.displayColor.size(); i++) {
      ss << sphere.displayColor[i];
      if (i != (sphere.displayColor.size() - 1)) {
        ss << ", ";
      }
    }
    ss << "]\n";

    // TODO: print optional meta value(e.g. `interpolation`)
  }

  ss << Indent(indent) << "}\n";

  return ss.str();
}


} // namespace tinyusdz
