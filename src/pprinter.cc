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

#if 0
std::string to_string(const float &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
#endif

std::string to_string(const double &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

template<typename T>
std::string to_string(const std::vector<T> &v) {
  std::stringstream ss;

  ss << "[";
  for (size_t i = 0; i < v.size(); i++) {
    ss << v[i];
    if (i != (v.size() - 1)) {
      ss << ", ";
    }
  }
  ss << "]";

  return ss.str();
}


template<typename T>
std::string prefix(const Animatable<T> &v) {
  if (v.IsTimeSampled()) {
    return ".timeSamples";
  }
  return "";
}

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
std::string print_animatable(const Animatable<T> &v, const uint32_t indent) {
  if (v.IsTimeSampled()) {
    return print_timesampled(v.timeSamples, indent);
  } else {
    return to_string(v.value);
  }
}

template<typename T>
std::string print_predefined(const T &gprim, const uint32_t indent) {
  std::stringstream ss;

  // properties
  if (gprim.doubleSided != false) {
    ss << Indent(indent) << "  uniform bool doubleSided = " << gprim.doubleSided << "\n";
  }

  if (gprim.orientation != Orientation::RightHanded) {
    ss << Indent(indent) << "  uniform token orientation = " << to_string(gprim.orientation)
       << "\n";
  }


  ss << Indent(indent) << "  float3[] extent" << prefix(gprim.extent) << " = " << print_animatable(gprim.extent, indent) << "\n";

  ss << Indent(indent) << "  token visibility" << prefix(gprim.visibility) << " = " << print_animatable(gprim.visibility, indent) << "\n";

  // primvars
  ss << Indent(indent) << "  float3[] primvars:displayColor" << prefix(gprim.displayColor) << " = " << print_animatable(gprim.displayColor, indent) << "\n";

#if 0
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
#endif

  return ss.str();
}

} // namespace

std::string to_string(tinyusdz::Visibility v) {
  if (v == tinyusdz::Visibility::Inherited) {
    return "\"inherited\"";
  } else {
    return "\"invisible\"";
  }
}

std::string to_string(tinyusdz::Orientation o) {
  if (o == tinyusdz::Orientation::RightHanded) {
    return "\"rightHanded\"";
  } else {
    return "\"leftHanded\"";
  }
}

std::string to_string(tinyusdz::ListEditQual v) {
  if (v == tinyusdz::ListEditQual::ResetToExplicit) {
    return "\"unqualified\"";
  } else if (v == tinyusdz::ListEditQual::Append) {
    return "\"append\"";
  } else if (v == tinyusdz::ListEditQual::Add) {
    return "\"add\"";
  } else if (v == tinyusdz::ListEditQual::Append) {
    return "\"append\"";
  } else if (v == tinyusdz::ListEditQual::Delete) {
    return "\"delete\"";
  } else if (v == tinyusdz::ListEditQual::Prepend) {
    return "\"prepend\"";
  } 

  return "\"[[Invalid ListEditQual value]]\"";
}

std::string to_string(tinyusdz::Interpolation interp) {
  switch (interp) {
    case Interpolation::Invalid:
      return "[[Invalid interpolation value]]";
    case Interpolation::Constant:
      return "constant";
    case Interpolation::Uniform:
      return "uniform";
    case Interpolation::Varying:
      return "varying";
    case Interpolation::Vertex:
      return "vertex";
    case Interpolation::FaceVarying:
      return "faceVarying";
  }

  // Never reach here though
  return "[[Invalid interpolation value]]";
}

std::string to_string(tinyusdz::SpecType ty) {
  if (SpecType::Unknown == ty) {
    return "SpecTypeUnknown";
  } else if (SpecType::Attribute == ty) {
    return "SpecTypeAttribute";
  } else if (SpecType::Connection == ty) {
    return "SpecTypeConection";
  } else if (SpecType::Expression == ty) {
    return "SpecTypeExpression";
  } else if (SpecType::Mapper == ty) {
    return "SpecTypeMapper";
  } else if (SpecType::MapperArg == ty) {
    return "SpecTypeMapperArg";
  } else if (SpecType::Prim == ty) {
    return "SpecTypePrim";
  } else if (SpecType::PseudoRoot == ty) {
    return "SpecTypePseudoRoot";
  } else if (SpecType::Relationship == ty) {
    return "SpecTypeRelationship";
  } else if (SpecType::RelationshipTarget == ty) {
    return "SpecTypeRelationshipTarget";
  } else if (SpecType::Variant == ty) {
    return "SpecTypeVariant";
  } else if (SpecType::VariantSet == ty) {
    return "SpecTypeVariantSet";
  }
  return "SpecTypeInvalid";
}


std::string to_string(tinyusdz::Extent e) {
  std::stringstream ss;

  ss << "[" << e.lower << ", " << e.upper << "]";

  return ss.str();
}

#if 0
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
#endif

std::string to_string(const tinyusdz::Klass &klass, uint32_t indent) {
  std::stringstream ss;

  ss << tinyusdz::Indent(indent) << "class " << klass.name << " (\n";
  ss << tinyusdz::Indent(indent) << ")\n";
  ss << tinyusdz::Indent(indent) << "{\n";

  for (auto prop : klass.props) {

    if (auto prel = nonstd::get_if<tinyusdz::Rel>(&prop.second)) {
        ss << "TODO: Rel\n";
    } else if (auto pattr = nonstd::get_if<tinyusdz::PrimAttrib>(&prop.second)) {
      if (auto p = tinyusdz::primvar::as_basic<double>(&pattr->var)) {
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

  ss << Indent(indent) << "  visibility" << prefix(gprim.visibility) << " = " << print_animatable(gprim.visibility, indent)
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

  ss << Indent(indent) << "  visibility" << prefix(xform.visibility) << " = " << print_animatable(xform.visibility, indent)
     << "\n";

  ss << Indent(indent) << "}\n";

  return ss.str();
}


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
  // FIXME
  //ss << Indent(indent) << "  " << primvar::get_type_name(mesh.points) << " points = " << mesh.points << "\n";

  ss << print_predefined(mesh, indent);

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const GeomPoints &points, const uint32_t indent) {
  std::stringstream ss;

  ss << Indent(indent) << "def Points \"" << points.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  // FIXME
  //ss << Indent(indent) << "  " << primvar::get_type_name(points.points) << " points = " << points.points << "\n";

  ss << print_predefined(points, indent);

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
  // FIXME
  //ss << Indent(indent) << "  " << primvar::type_name(geom.points) << " points = " << geom.points << "\n";

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

std::string to_string(const GeomCylinder &geom, const uint32_t indent) {
  std::stringstream ss;

  ss << Indent(indent) << "def Cylinder \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius, indent) << "\n";
  ss << Indent(indent) << "  double height" << prefix(geom.height) << " = " << print_animatable(geom.height, indent) << "\n";

  std::string axis;
  if (geom.axis == Axis::X) {
    axis = "x";
  } else if (geom.axis == Axis::Y) {
    axis = "y";
  } else {
    axis = "z";
  }
    
  ss << Indent(indent) << "  uniform token axis = " << axis << "\n";

  ss << print_predefined(geom, indent);

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const GeomCapsule &geom, const uint32_t indent) {
  std::stringstream ss;

  ss << Indent(indent) << "def Capsule \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius, indent) << "\n";
  ss << Indent(indent) << "  double height" << prefix(geom.height) << " = " << print_animatable(geom.height, indent) << "\n";

  std::string axis;
  if (geom.axis == Axis::X) {
    axis = "x";
  } else if (geom.axis == Axis::Y) {
    axis = "y";
  } else {
    axis = "z";
  }
    
  ss << Indent(indent) << "  uniform token axis = " << axis << "\n";

  ss << print_predefined(geom, indent);

  ss << Indent(indent) << "}\n";

  return ss.str();
}


} // tinyusdz
