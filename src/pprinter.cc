
#include <ctime>

//
#include "pprinter.hh"
#include "value-pprint.hh"
#include "str-util.hh"


namespace tinyusdz {

std::string Indent(uint32_t n) {
  std::stringstream ss;

  for (uint32_t i = 0; i < n; i++) {
    ss << kIndentString;
  }

  return ss.str();
}

namespace {


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

#if 0 // TODO
template<typename T>
std::string print_timesampled(const TypedTimeSamples<T> &v, const uint32_t indent) {
  std::stringstream ss;

  ss << "{\n";

  for (size_t i = 0; i < v.times.size(); i++) {
    ss << Indent(indent+2) << v.times[i] << " : " << to_string(v.values[i]) << ",\n";
  }

  ss << Indent(indent+1) << "}";

  return ss.str();
}

template<typename T>
std::string print_timesampled(const TypedTimeSamples<T> &v, const uint32_t indent) {
  std::stringstream ss;

  ss << "{\n";

  for (size_t i = 0; i < v.times.size(); i++) {
    ss << Indent(indent+2) << v.times[i] << " : " << to_string(v.values[i]) << ",\n";
  }

  ss << Indent(indent+1) << "}";

  return ss.str();
}
#endif

template<typename T>
std::string print_animatable(const Animatable<T> &v, const uint32_t indent = 0) {
  if (v.IsTimeSampled()) {
    // TODO: print all ranges
    return "TODO"; //print_timesampled(v.ranges.[0], indent);
  } else {
    return Indent(indent) + to_string(v.value);
  }
}

std::string print_prim_metas(const PrimMeta &meta, const uint32_t indent) {

  std::stringstream ss;

  if (meta.kind) {
    ss << Indent(indent) << "kind = " << quote(to_string(meta.kind.value())) << "\n";
  }

  if (meta.customData) {
    ss << Indent(indent) << "customData = {\n";
    ss << Indent(indent+1) << "TODO:\n";
    ss << Indent(indent) << "}\n";
  }

  for (const auto &item : meta.meta) {
    (void)item;
    // TODO:
  }

  return ss.str();
}

template<typename T>
std::string print_predefined(const T &gprim, const uint32_t indent) {
  std::stringstream ss;

  // properties
  if (gprim.doubleSided.authorized()) {
    ss << Indent(indent) << "  uniform bool doubleSided = " << gprim.doubleSided.get() << "\n";
  }

  if (gprim.orientation.authorized()) {
    ss << Indent(indent) << "  uniform token orientation = " << to_string(gprim.orientation.get())
       << "\n";
  }


  if (gprim.extent) {
    ss << Indent(indent) << "  float3[] extent" << prefix(gprim.extent.value()) << " = " << print_animatable(gprim.extent.value()) << "\n";
  }

  if (gprim.visibility.authorized()) {
    ss << Indent(indent) << "  token visibility" << prefix(gprim.visibility.get()) << " = " << print_animatable(gprim.visibility.get()) << "\n";
  }

  if (gprim.materialBinding) {
    auto m = gprim.materialBinding.value();
    if (m.binding.IsValid()) {
      ss << Indent(indent) << "  rel material:binding = " << wquote(to_string(m.binding), "<", ">") << "\n";
    }
  }

  // primvars
  if (gprim.displayColor) {
    ss << Indent(indent) << "  float3[] primvars:displayColor" << prefix(gprim.displayColor.value()) << " = " << print_animatable(gprim.displayColor.value()) << "\n";
  }

  return ss.str();
}

} // namespace

std::string to_string(tinyusdz::GeomMesh::InterpolateBoundary v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomMesh::InterpolateBoundary::None: { s = "none"; break; }
    case tinyusdz::GeomMesh::InterpolateBoundary::EdgeAndCorner: {s = "edgeAndCorner"; break; }
    case tinyusdz::GeomMesh::InterpolateBoundary::EdgeOnly: { s = "edgeOnly"; break; }
  }

  return s;
}

std::string to_string(tinyusdz::GeomMesh::SubdivisionScheme v) {
  std::string s;

  switch (v) {
    case tinyusdz::GeomMesh::SubdivisionScheme::CatmullClark: { s = "catmullClark"; break; }
    case tinyusdz::GeomMesh::SubdivisionScheme::Loop: { s = "loop"; break; }
    case tinyusdz::GeomMesh::SubdivisionScheme::Bilinear: { s = "bilinear"; break; }
    case tinyusdz::GeomMesh::SubdivisionScheme::None: { s = "none"; break; }
  }

  return s;
}

std::string to_string(tinyusdz::Kind v) {
  if (v == tinyusdz::Kind::Model) {
    return "model";
  } else if (v == tinyusdz::Kind::Group) {
    return "group";
  } else if (v == tinyusdz::Kind::Assembly) {
    return "assembly";
  } else if (v == tinyusdz::Kind::Component) {
    return "component";
  } else if (v == tinyusdz::Kind::Subcomponent) {
    return "subcomponent";
  } else {
    return "[[InvalidKind]]";
  }
}


std::string to_string(tinyusdz::Axis v) {
  if (v == tinyusdz::Axis::X) {
    return "X";
  } else if (v == tinyusdz::Axis::Y) {
    return "Y";
  } else if (v == tinyusdz::Axis::Z) {
    return "Z";
  } else {
    return "[[InvalidAxis]]";
  }
}

std::string to_string(tinyusdz::Visibility v) {
  if (v == tinyusdz::Visibility::Inherited) {
    return "inherited";
  } else {
    return "invisible";
  }
}

std::string to_string(tinyusdz::Orientation o) {
  if (o == tinyusdz::Orientation::RightHanded) {
    return "rightHanded";
  } else {
    return "leftHanded";
  }
}

std::string to_string(tinyusdz::ListEditQual v) {
  if (v == tinyusdz::ListEditQual::ResetToExplicit) {
    return "unqualified";
  } else if (v == tinyusdz::ListEditQual::Append) {
    return "append";
  } else if (v == tinyusdz::ListEditQual::Add) {
    return "add";
  } else if (v == tinyusdz::ListEditQual::Append) {
    return "append";
  } else if (v == tinyusdz::ListEditQual::Delete) {
    return "delete";
  } else if (v == tinyusdz::ListEditQual::Prepend) {
    return "prepend";
  }

  return "[[Invalid ListEditQual value]]";
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
  if (SpecType::Attribute == ty) {
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

std::string to_string(tinyusdz::Specifier s) {
  if (s == tinyusdz::Specifier::Def) {
    return "def";
  } else if (s == tinyusdz::Specifier::Over) {
    return "over";
  } else if (s == tinyusdz::Specifier::Class) {
    return "class";
  } else {
    return "[[SpecifierInvalid]]";
  }
}

std::string to_string(tinyusdz::Permission s) {
  if (s == tinyusdz::Permission::Public) {
    return "public";
  } else if (s == tinyusdz::Permission::Private) {
    return "private";
  } else {
    return "[[PermissionInvalid]]";
  }
}

std::string to_string(tinyusdz::Variability v) {
  if (v == tinyusdz::Variability::Varying) {
    return "varying";
  } else if (v == tinyusdz::Variability::Uniform) {
    return "uniform";
  } else if (v == tinyusdz::Variability::Config) {
    return "config";
  } else {
    return "\"[[VariabilityInvalid]]\"";
  }
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

std::string to_string(const tinyusdz::Klass &klass, uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << tinyusdz::Indent(indent) << "class " << klass.name << " (\n";
  ss << tinyusdz::Indent(indent) << ")\n";
  ss << tinyusdz::Indent(indent) << "{\n";

  for (auto prop : klass.props) {

    if (prop.second.IsRel()) {
        ss << "TODO: Rel\n";
    } else {
      //const PrimAttrib &attrib = prop.second.attrib;
#if 0 // TODO
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
#endif
    }

    ss << "\n";
  }

  if (closing_brace) {
    ss << tinyusdz::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Model &model, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  // Currently, `Model` is used for typeless `def`
  ss << Indent(indent) << "def \"" << model.name << "\"\n";
  ss << Indent(indent) << "(\n";
  ss << print_prim_metas(model.meta, indent+1);
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // props
  // TODO:

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Scope &scope, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Scope \"" << scope.name << "\"\n";
  ss << Indent(indent) << "(\n";
  ss << print_prim_metas(scope.meta, indent+1);
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // props
  // TODO:

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GPrim &gprim, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def GPrim \"" << gprim.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // props
  // TODO:

  if (gprim.visibility.authorized()) {
    ss << Indent(indent) << "  visibility" << prefix(gprim.visibility.get()) << " = " << print_animatable(gprim.visibility.get())
       << "\n";
  }

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Xform &xform, const uint32_t indent, bool closing_brace) {
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

  if (xform.visibility.authorized()) {
    ss << Indent(indent) << "  visibility" << prefix(xform.visibility.get()) << " = " << print_animatable(xform.visibility.get())
     << "\n";
  }

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCamera &camera, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Camera \"" << camera.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "   float2 clippingRange = " << camera.clippingRange << "\n";
  ss << Indent(indent) << "   float focalLength = " << camera.focalLength << "\n";
  ss << Indent(indent) << "   float horizontalAperture = " << camera.horizontalAperture << "\n";
  ss << Indent(indent) << "   float horizontalApertureOffset = " << camera.horizontalApertureOffset << "\n";
  ss << Indent(indent) << "   token projection = \"" << to_string(camera.projection) << "\"\n";
  ss << Indent(indent) << "   float verticalAperture = " << camera.verticalAperture << "\n";
  ss << Indent(indent) << "   float verticalApertureOffset = " << camera.verticalApertureOffset << "\n";

  //ss << print_predefined(camera, indent);

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const GeomSphere &sphere, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Sphere \"" << sphere.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius" << prefix(sphere.radius) << " = " << print_animatable(sphere.radius) << "\n";

  ss << print_predefined(sphere, indent);

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomMesh &mesh, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Mesh \"" << mesh.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  if (mesh.points.size()) {
    ss << Indent(indent) << "  point3[] points = " << mesh.points << "\n";
  }
  if (mesh.faceVertexIndices.size()) {
    ss << Indent(indent) << "  int[] faceVertexIndices = " << mesh.faceVertexIndices << "\n";
  }
  if (mesh.faceVertexCounts.size()) {
    ss << Indent(indent) << "  int[] faceVertexCounts = " << mesh.faceVertexCounts << "\n";
  }

  // subdiv
  if (mesh.cornerIndices.size()) {
    ss << Indent(indent) << "  int[] cornerIndices = " << mesh.cornerIndices << "\n";
  }
  if (mesh.cornerSharpnesses.size()) {
    ss << Indent(indent) << "  float[] cornerSharpnesses = " << mesh.cornerSharpnesses << "\n";
  }
  if (mesh.creaseIndices.size()) {
    ss << Indent(indent) << "  int[] creaseIndices = " << mesh.creaseIndices << "\n";
  }
  if (mesh.creaseLengths.size()) {
    ss << Indent(indent) << "  int[] creaseLengths = " << mesh.creaseLengths << "\n";
  }
  if (mesh.holeIndices.size()) {
    ss << Indent(indent) << "  int[] holeIndices = " << mesh.holeIndices << "\n";
  }

  if (mesh.subdivisionScheme.authorized()) {
    ss << Indent(indent) << "  uniform token subdivisionScheme = " << quote(to_string(mesh.subdivisionScheme.get())) << "\n";
  }
  if (mesh.interpolateBoundary.authorized()) {
    ss << Indent(indent) << "  uniform token interpolateBoundary = " << to_string(mesh.interpolateBoundary.get()) << "\n";
  }

  ss << print_predefined(mesh, indent);

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomPoints &points, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Points \"" << points.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  point3[] points = " << points.points << "\n";

  ss << print_predefined(points, indent);

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const GeomBasisCurves &geom, const uint32_t indent, bool closing_brace) {
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

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCube &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Cube \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double size" << prefix(geom.size) << " = " << print_animatable(geom.size) << "\n";

  ss << print_predefined(geom, indent);

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCone &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Cone \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius) << "\n";
  ss << Indent(indent) << "  double height" << prefix(geom.height) << " = " << print_animatable(geom.height) << "\n";

  ss << print_predefined(geom, indent);

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCylinder &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Cylinder \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius) << "\n";
  ss << Indent(indent) << "  double height" << prefix(geom.height) << " = " << print_animatable(geom.height) << "\n";

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

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCapsule &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Capsule \"" << geom.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "  double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius) << "\n";
  ss << Indent(indent) << "  double height" << prefix(geom.height) << " = " << print_animatable(geom.height) << "\n";

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

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const SkelRoot &root, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def SkelRoot \"" << root.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // TODO
  // Skeleton id
  //ss << Indent(indent) << "skelroot.skeleton_id << "\n"

  ss << Indent(indent) << "[TODO]\n";

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Material &material, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Material \"" << material.name << "\"\n";
  ss << Indent(indent) << "(\n";
  //print_prim_metas(material.metas, indent);
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Shader &shader, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Shader \"" << shader.name << "\"\n";
  ss << Indent(indent) << "(\n";
  //print_prim_metas(shader.metas, indent);
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "   uniform token info:id = \"" << shader.info_id << "\"\n";

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Skeleton &skel, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def Skeleton \"" << skel.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // TODO
  ss << Indent(indent) << "[TODO]\n";

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const LuxSphereLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def SphereLight \"" << light.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "   color3f inputs:color = " << light.color << "\n";
  ss << Indent(indent) << "   float inputs:intensity = " << light.intensity << "\n";
  ss << Indent(indent) << "   float inputs:radius = " << light.radius << "\n";
  ss << Indent(indent) << "   float inputs:specular = " << light.specular << "\n";

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const LuxDomeLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << Indent(indent) << "def DomeLight \"" << light.name << "\"\n";
  ss << Indent(indent) << "(\n";
  // args
  ss << Indent(indent) << ")\n";
  ss << Indent(indent) << "{\n";

  // members
  ss << Indent(indent) << "   color3f inputs:color = " << light.color << "\n";
  ss << Indent(indent) << "   float inputs:intensity = " << light.intensity << "\n";

  if (closing_brace) {
    ss << Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const GeomCamera::Projection &proj, uint32_t indent, bool closing_brace) {
  (void)closing_brace;
  (void)indent;

  if (proj == GeomCamera::Projection::orthographic) {
    return "orthographic";
  } else {
    return "perspective";
  }
}

std::string to_string(const Path &path, bool show_full_path) {
  if (show_full_path) {
    return path.full_path_name();
  } else {
    // TODO
    return path.full_path_name();
  }
}

std::string to_string(const std::vector<Path> &v, bool show_full_path) {

  // TODO(syoyo): indent
  std::stringstream ss;
  ss << "[";

  for (size_t i = 0; i < v.size(); i++) {
    ss << to_string(v[i], show_full_path);
    if (i != (v.size() -1)) {
      ss << ", ";
    }
  }
  ss << "]";
  return ss.str();
}



} // tinyusdz
