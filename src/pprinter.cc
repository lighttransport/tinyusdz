#include "pprinter.hh"

namespace tinyusdz {

namespace {

std::string Indent(uint32_t n) {
  std::stringstream ss;

  for (uint32_t i = 0; i < n; i++) {
    ss << "  ";
  }

  return ss.str();
}

template<typename T>
std::string prefix(const Animatable<T> &v) {
  if (nonstd::get_if<TimeSampled<T>>(&v)) {
    return ".timeSamples";
  }
  return "";
}

} // namespace

std::string to_string(tinyusdz::Visibility v) {
  if (v == tinyusdz::VisibilityInherited) {
    return "\"inherited\"";
  } else {
    return "\"invisible\"";
  }
}

std::string to_string(tinyusdz::Orientation o) {
  if (o == tinyusdz::OrientationRightHanded) {
    return "\"rightHanded\"";
  } else {
    return "\"leftHanded\"";
  }
}

std::string to_string(tinyusdz::Interpolation interp) {
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


std::string to_string(tinyusdz::Extent e) {
  std::stringstream ss;

  ss << "[" << e.lower << ", " << e.upper << "]";

  return ss.str();
}

std::string to_string(const tinyusdz::AnimatableVisibility &v, const uint32_t indent) {
  if (auto p = nonstd::get_if<Visibility>(&v)) {
    return to_string(*p);
  }

  if (auto p = nonstd::get_if<TimeSampled<Visibility>>(&v)) {

    std::stringstream ss;

    ss << "{";

    for (size_t i = 0; i < p->times.size(); i++) {
      ss << Indent(indent+2) << p->times[i] << " : " << to_string(p->values[i]) << ", ";
      // TODO: indent and newline
    }

    ss << Indent(indent+1) << "}";

  }

  return "[[??? AnimatableVisibility]]";
}

std::string to_string(const tinyusdz::Klass &klass, uint32_t indent) {
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

std::string to_string(const GPrim &gprim, const uint32_t indent) {
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

std::string to_string(const Xform &xform, const uint32_t indent) {
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

  ss << Indent(indent) << "  visibility = " << to_string(xform.visibility, indent)
     << "\n";

  ss << Indent(indent) << "}\n";

  return ss.str();
}

namespace {

template<typename T>
std::string print_timesampled(const TimeSampled<T> &v, const uint32_t indent) {
  std::stringstream ss;

  ss << "{\n";

  for (size_t i = 0; i < v.times.size(); i++) {
    ss << Indent(indent+2) << v.times[i] << " : " << to_string(v.values[i]) << ",\n";
  }

  ss << Indent(indent+1) << "}";

  return ss.str();
}

template<typename T>
std::string print_animatable(const nonstd::variant<T, TimeSampled<T>> &v, const uint32_t indent) {
  if (auto p = nonstd::get_if<T>(&v)) {
    return to_string(*p, indent);
  }

  if (auto p = nonstd::get_if<TimeSampled<T>>(&v)) {
    return print_timesampled(*p, indent);
  }

  return "[[??? print_animatable]]";
}

template<typename T>
std::string print_predefined(const T &gprim, const uint32_t indent) {
  std::stringstream ss;

  // properties
  if (gprim.doubleSided != false) {
    ss << Indent(indent) << "  uniform bool doubleSided = " << gprim.doubleSided << "\n";
  }

  if (gprim.orientation != OrientationRightHanded) {
    ss << Indent(indent) << "  uniform token orientation = " << to_string(gprim.orientation)
       << "\n";
  }

  ss << Indent(indent) << "  float3[] extent = " << to_string(gprim.extent) << "\n";

  ss << Indent(indent) << "  token visibility" << prefix(gprim.visibility) << " = " << print_animatable(gprim.visibility);

  // primvars
  if (!gprim.displayColor.empty()) {
    ss << Indent(indent) << "  primvars:displayColor = [";
    for (size_t i = 0; i < gprim.displayColor.size(); i++) {
      ss << gprim.displayColor[i];
      if (i != (gprim.displayColor.size() - 1)) {
        ss << ", ";
      }
    }
    ss << "]\n";

    // TODO: print optional meta value(e.g. `interpolation`)
  }

  return ss.str();
}

} // namespace

std::string to_string(const GeomSphere &sphere, const uint32_t indent) {
  std::stringstream ss;

  ss << Indent(indent) << "def Sphere \"" << sphere.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius" << prefix(sphere.radius) << " = " << print_animatable(sphere.radius, indent) << "\n";

  ss << print_predefined(sphere, indent);

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const GeomMesh &mesh, const uint32_t indent) {
  std::stringstream ss;

  ss << Indent(indent) << "def Mesh \"" << mesh.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  " << primvar::type_name(mesh.points) << " points = " << mesh.points << "\n";

  ss << print_predefined(mesh, indent);

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const GeomBasisCurves &geom, const uint32_t indent) {
  std::stringstream ss;

  ss << Indent(indent) << "def BasisCurves \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  " << primvar::type_name(geom.points) << " points = " << geom.points << "\n";

  ss << print_predefined(geom, indent);

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const GeomCube &geom, const uint32_t indent) {
  std::stringstream ss;

  ss << Indent(indent) << "def Cube \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double size" << prefix(geom.size) << " = " << print_animatable(geom.size, indent) << "\n";

  ss << print_predefined(geom, indent);

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const GeomCone &geom, const uint32_t indent) {
  std::stringstream ss;

  ss << Indent(indent) << "def Cone \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius, indent) << "\n";
  ss << Indent(indent) << "  double height" << prefix(geom.height) << " = " << print_animatable(geom.height, indent) << "\n";

  ss << print_predefined(geom, indent);

  ss << Indent(indent) << "}\n";

  return ss.str();
}

} // tinyusdz
