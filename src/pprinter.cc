
#include <ctime>

//
#include "pprinter.hh"
#include "value-pprint.hh"
#include "str-util.hh"


namespace tinyusdz {

namespace pprint {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif

static std::string sIndentString = "    ";

#ifdef __clang__
#pragma clang diagnostic pop
#endif


std::string Indent(uint32_t n) {
  std::stringstream ss;

  for (uint32_t i = 0; i < n; i++) {
    ss << sIndentString;
  }

  return ss.str();
}

void SetIndentString(const std::string &s) {
  sIndentString = s;
}

} // namespace pprint

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
    return pprint::Indent(indent) + to_string(v.value);
  }
}

std::string print_prim_metas(const PrimMeta &meta, const uint32_t indent) {

  std::stringstream ss;

  if (meta.kind) {
    ss << pprint::Indent(indent) << "kind = " << quote(to_string(meta.kind.value())) << "\n";
  }

  if (meta.customData) {
    ss << pprint::Indent(indent) << "customData = {\n";
    ss << pprint::Indent(indent+1) << "TODO:\n";
    ss << pprint::Indent(indent) << "}\n";
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
    ss << pprint::Indent(indent) << "uniform bool doubleSided = " << gprim.doubleSided.get() << "\n";
  }

  if (gprim.orientation.authorized()) {
    ss << pprint::Indent(indent) << "uniform token orientation = " << to_string(gprim.orientation.get())
       << "\n";
  }


  if (gprim.extent) {
    ss << pprint::Indent(indent) << "float3[] extent" << prefix(gprim.extent.value()) << " = " << print_animatable(gprim.extent.value()) << "\n";
  }

  if (gprim.visibility.authorized()) {
    ss << pprint::Indent(indent) << "token visibility" << prefix(gprim.visibility.get()) << " = " << print_animatable(gprim.visibility.get()) << "\n";
  }

  if (gprim.materialBinding) {
    auto m = gprim.materialBinding.value();
    if (m.binding.IsValid()) {
      ss << pprint::Indent(indent) << "rel material:binding = " << wquote(to_string(m.binding), "<", ">") << "\n";
    }
  }

  // primvars
  if (gprim.displayColor) {
    ss << pprint::Indent(indent) << "float3[] primvars:displayColor" << prefix(gprim.displayColor.value()) << " = " << print_animatable(gprim.displayColor.value()) << "\n";
  }

  return ss.str();
}

std::string print_props(const std::map<std::string, Property> &props, uint32_t indent)
{
  std::stringstream ss;

  for (const auto &item : props) {

    ss << pprint::Indent(indent);

    const Property &prop = item.second;

    if (prop.IsRel()) {
      ss << "[TODO]: `rel`";
    } else {
      const PrimAttrib &attr = item.second.attrib;

      bool isUniform = attr.uniform;
      std::string ty = attr.var.type_name();

      if (isUniform) {
        ss << "uniform ";
      }

      ss << ty << " " << item.first;

      if (prop.IsConnection()) {
        ss << ".connect = <TODO: Connection>";
      } else if (prop.IsEmpty()) {
        ss << "\n";
      } else {
        // has value content
        ss << " = ";

        if (attr.var.is_timesample()) {
          ss << "[TODO: TimeSamples]";
        } else {
          // is_scalar
          ss << value::pprint_any(attr.var.var.values[0]);
        }
      }
    }

    ss << "\n";
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
      ss << pprint::Indent(indent+2) << p->times[i] << " : " << to_string(p->values[i]) << ", ";
      // TODO: indent and newline
    }

    ss << pprint::Indent(indent+1) << "}";

  }

  return "[[??? AnimatableVisibility]]";
}
#endif

std::string to_string(const tinyusdz::Klass &klass, uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << tinyusdz::pprint::Indent(indent) << "class " << klass.name << " (\n";
  ss << tinyusdz::pprint::Indent(indent) << ")\n";
  ss << tinyusdz::pprint::Indent(indent) << "{\n";

  for (auto prop : klass.props) {

    if (prop.second.IsRel()) {
        ss << "TODO: Rel\n";
    } else {
      //const PrimAttrib &attrib = prop.second.attrib;
#if 0 // TODO
      if (auto p = tinyusdz::primvar::as_basic<double>(&pattr->var)) {
        ss << tinyusdz::pprint::Indent(indent);
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
    ss << tinyusdz::pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Model &model, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  // Currently, `Model` is used for typeless `def`
  ss << pprint::Indent(indent) << "def \"" << model.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(model.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // props
  // TODO:

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Scope &scope, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Scope \"" << scope.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(scope.meta, indent+1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // props
  // TODO:

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GPrim &gprim, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def GPrim \"" << gprim.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // props
  // TODO:

  if (gprim.visibility.authorized()) {
    ss << pprint::Indent(indent+1) << "visibility" << prefix(gprim.visibility.get()) << " = " << print_animatable(gprim.visibility.get())
       << "\n";
  }

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Xform &xform, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Xform \"" << xform.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // props
  if (xform.xformOps.size()) {
    for (size_t i = 0; i < xform.xformOps.size(); i++) {
      auto xformOp = xform.xformOps[i];
      ss << pprint::Indent(indent);
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
    ss << pprint::Indent(indent) << "uniform token[] xformOpOrder = [";
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
    ss << pprint::Indent(indent+1) << "visibility" << prefix(xform.visibility.get()) << " = " << print_animatable(xform.visibility.get())
     << "\n";
  }

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCamera &camera, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Camera \"" << camera.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "float2 clippingRange = " << camera.clippingRange << "\n";
  ss << pprint::Indent(indent+1) << "float focalLength = " << camera.focalLength << "\n";
  ss << pprint::Indent(indent+1) << "float horizontalAperture = " << camera.horizontalAperture << "\n";
  ss << pprint::Indent(indent+1) << "float horizontalApertureOffset = " << camera.horizontalApertureOffset << "\n";
  ss << pprint::Indent(indent+1) << "token projection = \"" << to_string(camera.projection) << "\"\n";
  ss << pprint::Indent(indent+1) << "float verticalAperture = " << camera.verticalAperture << "\n";
  ss << pprint::Indent(indent+1) << "float verticalApertureOffset = " << camera.verticalApertureOffset << "\n";

  //ss << print_predefined(camera, indent);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const GeomSphere &sphere, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Sphere \"" << sphere.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "double radius" << prefix(sphere.radius) << " = " << print_animatable(sphere.radius) << "\n";

  ss << print_predefined(sphere, indent);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomMesh &mesh, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Mesh \"" << mesh.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  if (mesh.points.size()) {
    ss << pprint::Indent(indent+1) << "point3[] points = " << mesh.points << "\n";
  }
  if (mesh.faceVertexIndices.size()) {
    ss << pprint::Indent(indent+1) << "int[] faceVertexIndices = " << mesh.faceVertexIndices << "\n";
  }
  if (mesh.faceVertexCounts.size()) {
    ss << pprint::Indent(indent+1) << "int[] faceVertexCounts = " << mesh.faceVertexCounts << "\n";
  }

  // subdiv
  if (mesh.cornerIndices.size()) {
    ss << pprint::Indent(indent+1) << "int[] cornerIndices = " << mesh.cornerIndices << "\n";
  }
  if (mesh.cornerSharpnesses.size()) {
    ss << pprint::Indent(indent+1) << "float[] cornerSharpnesses = " << mesh.cornerSharpnesses << "\n";
  }
  if (mesh.creaseIndices.size()) {
    ss << pprint::Indent(indent+1) << "int[] creaseIndices = " << mesh.creaseIndices << "\n";
  }
  if (mesh.creaseLengths.size()) {
    ss << pprint::Indent(indent+1) << "int[] creaseLengths = " << mesh.creaseLengths << "\n";
  }
  if (mesh.holeIndices.size()) {
    ss << pprint::Indent(indent+1) << "int[] holeIndices = " << mesh.holeIndices << "\n";
  }

  if (mesh.subdivisionScheme.authorized()) {
    ss << pprint::Indent(indent+1) << "uniform token subdivisionScheme = " << quote(to_string(mesh.subdivisionScheme.get())) << "\n";
  }
  if (mesh.interpolateBoundary.authorized()) {
    ss << pprint::Indent(indent+1) << "uniform token interpolateBoundary = " << to_string(mesh.interpolateBoundary.get()) << "\n";
  }

  ss << print_predefined(mesh, indent);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomPoints &points, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Points \"" << points.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "point3[] points = " << points.points << "\n";

  ss << print_predefined(points, indent);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomBasisCurves::Type &ty) {

  std::string s;

  switch(ty) {
    case GeomBasisCurves::Type::Cubic: { s = "cubic"; break; }
    case GeomBasisCurves::Type::Linear: { s = "linear"; break; }
  }

  return s;
}

std::string to_string(const GeomBasisCurves::Basis &ty) {

  std::string s;

  switch(ty) {
    case GeomBasisCurves::Basis::Bezier: { s = "bezier"; break; }
    case GeomBasisCurves::Basis::Bspline: { s = "bspline"; break; }
    case GeomBasisCurves::Basis::CatmullRom: { s = "catmullRom"; break; }
  }

  return s;
}

std::string to_string(const GeomBasisCurves::Wrap &ty) {

  std::string s;

  switch(ty) {
    case GeomBasisCurves::Wrap::Nonperiodic: { s = "nonperiodic"; break; }
    case GeomBasisCurves::Wrap::Periodic: { s = "periodic"; break; }
    case GeomBasisCurves::Wrap::Pinned: { s = "pinned"; break; }
  }

  return s;
}


std::string to_string(const GeomBasisCurves &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def BasisCurves \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  if (geom.type) {
    ss << pprint::Indent(indent+1) << "uniform token type = " << quote(to_string(geom.type.value())) << "\n";
  }

  if (geom.basis) {
    ss << pprint::Indent(indent+1) << "uniform token basis = " << quote(to_string(geom.basis.value())) << "\n";
  }

  if (geom.wrap) {
    ss << pprint::Indent(indent+1) << "uniform token wrap = " << quote(to_string(geom.wrap.value())) << "\n";
  }

  if (geom.points.size()) {
    ss << pprint::Indent(indent+1) << "point3f[] points = " << geom.points << "\n";
  }

  if (geom.widths.size()) {
    ss << pprint::Indent(indent+1) << "float[] widths = " << geom.widths << "\n";
  }

  if (geom.curveVertexCounts.size()) {
    ss << pprint::Indent(indent+1) << "int[] curveVertexCounts = " << geom.curveVertexCounts << "\n";
  }

  ss << print_predefined(geom, indent+1);

  ss << print_props(geom.props, indent+1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCube &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Cube \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "double size" << prefix(geom.size) << " = " << print_animatable(geom.size) << "\n";

  ss << print_predefined(geom, indent);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCone &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Cone \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius) << "\n";
  ss << pprint::Indent(indent+1) << "double height" << prefix(geom.height) << " = " << print_animatable(geom.height) << "\n";

  ss << print_predefined(geom, indent);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCylinder &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Cylinder \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius) << "\n";
  ss << pprint::Indent(indent+1) << "double height" << prefix(geom.height) << " = " << print_animatable(geom.height) << "\n";

  std::string axis;
  if (geom.axis == Axis::X) {
    axis = "x";
  } else if (geom.axis == Axis::Y) {
    axis = "y";
  } else {
    axis = "z";
  }

  ss << pprint::Indent(indent+1) << "uniform token axis = " << axis << "\n";

  ss << print_predefined(geom, indent);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCapsule &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Capsule \"" << geom.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "double radius" << prefix(geom.radius) << " = " << print_animatable(geom.radius) << "\n";
  ss << pprint::Indent(indent+1) << "double height" << prefix(geom.height) << " = " << print_animatable(geom.height) << "\n";

  std::string axis;
  if (geom.axis == Axis::X) {
    axis = "x";
  } else if (geom.axis == Axis::Y) {
    axis = "y";
  } else {
    axis = "z";
  }

  ss << pprint::Indent(indent+1) << "uniform token axis = " << axis << "\n";

  ss << print_predefined(geom, indent);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const SkelRoot &root, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def SkelRoot \"" << root.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // TODO
  // Skeleton id
  //ss << pprint::Indent(indent) << "skelroot.skeleton_id << "\n"

  ss << pprint::Indent(indent) << "[TODO]\n";

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const Material &material, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Material \"" << material.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  //print_prim_metas(material.metas, indent);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const UsdPrimvarReader_float &shader, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Shader \"" << shader.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  //print_prim_metas(shader.metas, indent);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "uniform token info:id = \"UsdPrimvarReader_float\"\n";

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();

}

std::string to_string(const Shader &shader, const uint32_t indent, bool closing_brace) {

  if (auto pv = shader.value.get_value<UsdPrimvarReader_float>()) {
    return to_string(pv.value());
  } else {
    // generic Shader class
    std::stringstream ss;

    ss << pprint::Indent(indent) << "def Shader \"" << shader.name << "\"\n";
    ss << pprint::Indent(indent) << "(\n";
    //print_prim_metas(shader.metas, indent);
    ss << pprint::Indent(indent) << ")\n";
    ss << pprint::Indent(indent) << "{\n";

    // members
    ss << pprint::Indent(indent+1) << "uniform token info:id = \"" << shader.info_id << "\"\n";

    if (closing_brace) {
      ss << pprint::Indent(indent) << "}\n";
    }

    return ss.str();
  }

}

std::string to_string(const Skeleton &skel, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def Skeleton \"" << skel.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // TODO
  ss << pprint::Indent(indent) << "[TODO]\n";

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}


std::string to_string(const LuxSphereLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def SphereLight \"" << light.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "color3f inputs:color = " << light.color << "\n";
  ss << pprint::Indent(indent+1) << "float inputs:intensity = " << light.intensity << "\n";
  ss << pprint::Indent(indent+1) << "float inputs:radius = " << light.radius << "\n";
  ss << pprint::Indent(indent+1) << "float inputs:specular = " << light.specular << "\n";

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const LuxDomeLight &light, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << "def DomeLight \"" << light.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  // args
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << pprint::Indent(indent+1) << "color3f inputs:color = " << light.color << "\n";
  ss << pprint::Indent(indent+1) << "float inputs:intensity = " << light.intensity << "\n";

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
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
