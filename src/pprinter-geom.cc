// Geometry pretty-printer implementations split from pprinter.cc
// SPDX-License-Identifier: Apache 2.0
#include "pprinter.hh"
#include "pprinter-util.hh"
#include "prim-pprint.hh"
#include "value-pprint.hh" // for dtos
#include "str-util.hh" // for quote
#include <sstream>

namespace tinyusdz {

std::string to_string(const GeomCamera &camera, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(camera.spec) << " Camera \""
     << camera.name << "\"\n";
  if (camera.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(camera.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(camera.clippingRange, "clippingRange", indent + 1);
  ss << print_typed_attr(camera.clippingPlanes, "clippingPlanes", indent + 1);
  ss << print_typed_attr(camera.focalLength, "focalLength", indent + 1);
  ss << print_typed_attr(camera.horizontalAperture, "horizontalAperture",
                         indent + 1);
  ss << print_typed_attr(camera.horizontalApertureOffset,
                         "horizontalApertureOffset", indent + 1);
  ss << print_typed_attr(camera.verticalAperture, "verticalAperture",
                         indent + 1);
  ss << print_typed_attr(camera.verticalApertureOffset,
                         "verticalApertureOffset", indent + 1);

  ss << print_typed_token_attr(camera.projection, "projection", indent + 1);
  ss << print_typed_token_attr(camera.stereoRole, "stereoRole", indent + 1);

  ss << print_typed_attr(camera.shutterOpen, "shutter:open", indent + 1);
  ss << print_typed_attr(camera.shutterClose, "shutter:close", indent + 1);

  ss << print_gprim_predefined(camera, indent + 1);

  ss << print_props(camera.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomSphere &sphere, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(sphere.spec) << " Sphere \""
     << sphere.name << "\"\n";
  if (sphere.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(sphere.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(sphere.radius, "radius", indent + 1);
  ss << print_gprim_predefined(sphere, indent + 1);
  ss << print_props(sphere.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomMesh &mesh, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(mesh.spec) << " Mesh \""
     << mesh.name << "\"\n";
  if (mesh.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(mesh.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(mesh.points, "points", indent + 1);
  ss << print_typed_attr(mesh.normals, "normals", indent + 1);
  ss << print_typed_attr(mesh.faceVertexIndices, "faceVertexIndices",
                         indent + 1);
  ss << print_typed_attr(mesh.faceVertexCounts, "faceVertexCounts", indent + 1);

  if (mesh.skeleton) {
    ss << print_relationship(mesh.skeleton.value(),
                             mesh.skeleton.value().get_listedit_qual(),
                             /* custom */ false, "skel:skeleton", indent + 1);
  }

  ss << print_typed_attr(mesh.blendShapes, "skel:blendShapes", indent + 1);
  if (mesh.blendShapeTargets) {
    ss << print_relationship(mesh.blendShapeTargets.value(),
                             mesh.blendShapeTargets.value().get_listedit_qual(),
                             /* custom */ false, "skel:blendShapeTargets",
                             indent + 1);
  }

  for (const auto &item : mesh.subsetFamilyTypeMap) {
    std::string attr_name = "subsetFamily:" + item.first.str() + ":familyType";
    ss << pprint::Indent(indent+1) << "uniform token " << attr_name << " = " << quote(to_string(item.second)) << "\n";
  }

  // subdiv
  ss << print_typed_attr(mesh.cornerIndices, "cornerIndices", indent + 1);
  ss << print_typed_attr(mesh.cornerSharpnesses, "cornerSharpnesses",
                         indent + 1);
  ss << print_typed_attr(mesh.creaseIndices, "creaseIndices", indent + 1);
  ss << print_typed_attr(mesh.creaseLengths, "creaseLengths", indent + 1);
  ss << print_typed_attr(mesh.creaseSharpnesses, "creaseSharpnesses",
                         indent + 1);
  ss << print_typed_attr(mesh.holeIndices, "holeIndices", indent + 1);

  ss << print_typed_token_attr(mesh.subdivisionScheme, "subdivisonScheme",
                               indent + 1);
  ss << print_typed_token_attr(mesh.interpolateBoundary, "interpolateBoundary",
                               indent + 1);
  ss << print_typed_token_attr(mesh.faceVaryingLinearInterpolation,
                               "faceVaryingLinearInterpolation", indent + 1);

  ss << print_gprim_predefined(mesh, indent + 1);
  ss << print_props(mesh.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomSubset &subset, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(subset.spec) << " GeomSubset \""
     << subset.name << "\"\n";
  ss << pprint::Indent(indent) << "(\n";
  ss << print_prim_metas(subset.meta, indent + 1);
  ss << pprint::Indent(indent) << ")\n";
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_token_attr(subset.elementType, "elementType", indent + 1);
  ss << print_typed_attr(subset.familyName, "familyName", indent + 1);
  ss << print_typed_attr(subset.indices, "indices", indent + 1);

  ss << print_material_binding(&subset, indent + 1);
  ss << print_collection(&subset, indent + 1);
  ss << print_props(subset.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomPoints &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " Points \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.points, "points", indent + 1);
  ss << print_typed_attr(geom.normals, "normals", indent + 1);
  ss << print_typed_attr(geom.widths, "widths", indent + 1);
  ss << print_typed_attr(geom.ids, "ids", indent + 1);
  ss << print_typed_attr(geom.velocities, "velocities", indent + 1);
  ss << print_typed_attr(geom.accelerations, "accelerations", indent + 1);

  ss << print_gprim_predefined(geom, indent + 1);
  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomBasisCurves::Type &ty) {
  std::string s;
  switch (ty) {
    case GeomBasisCurves::Type::Cubic: s = "cubic"; break;
    case GeomBasisCurves::Type::Linear: s = "linear"; break;
  }
  return s;
}

std::string to_string(const GeomBasisCurves::Basis &ty) {
  std::string s;
  switch (ty) {
    case GeomBasisCurves::Basis::Bezier: s = "bezier"; break;
    case GeomBasisCurves::Basis::Bspline: s = "bspline"; break;
    case GeomBasisCurves::Basis::CatmullRom: s = "catmullRom"; break;
  }
  return s;
}

std::string to_string(const GeomBasisCurves::Wrap &ty) {
  std::string s;
  switch (ty) {
    case GeomBasisCurves::Wrap::Nonperiodic: s = "nonperiodic"; break;
    case GeomBasisCurves::Wrap::Periodic: s = "periodic"; break;
    case GeomBasisCurves::Wrap::Pinned: s = "pinned"; break;
  }
  return s;
}

std::string to_string(const GeomBasisCurves &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " BasisCurves \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_token_attr(geom.type, "type", indent + 1);
  ss << print_typed_token_attr(geom.basis, "basis", indent + 1);
  ss << print_typed_token_attr(geom.wrap, "wrap", indent + 1);

  ss << print_typed_attr(geom.points, "points", indent + 1);
  ss << print_typed_attr(geom.normals, "normals", indent + 1);
  ss << print_typed_attr(geom.widths, "widths", indent + 1);
  ss << print_typed_attr(geom.velocities, "velocites", indent + 1);
  ss << print_typed_attr(geom.accelerations, "accelerations", indent + 1);
  ss << print_typed_attr(geom.curveVertexCounts, "curveVertexCounts", indent + 1);

  ss << print_gprim_predefined(geom, indent + 1);
  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomNurbsCurves &geom, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(geom.spec) << " NurbsCurves \""
     << geom.name << "\"\n";
  if (geom.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(geom.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  // members
  ss << print_typed_attr(geom.points, "points", indent + 1);
  ss << print_typed_attr(geom.normals, "normals", indent + 1);
  ss << print_typed_attr(geom.widths, "widths", indent + 1);
  ss << print_typed_attr(geom.velocities, "velocites", indent + 1);
  ss << print_typed_attr(geom.accelerations, "accelerations", indent + 1);
  ss << print_typed_attr(geom.curveVertexCounts, "curveVertexCounts", indent + 1);

  ss << print_typed_attr(geom.order, "order", indent + 1);
  ss << print_typed_attr(geom.knots, "knots", indent + 1);
  ss << print_typed_attr(geom.ranges, "ranges", indent + 1);
  ss << print_typed_attr(geom.pointWeights, "pointWeights", indent + 1);

  ss << print_gprim_predefined(geom, indent + 1);
  ss << print_props(geom.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCube &cube, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(cube.spec) << " Cube \""
     << cube.name << "\"\n";
  if (cube.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(cube.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(cube.size, "size", indent + 1);
  ss << print_gprim_predefined(cube, indent + 1);
  ss << print_props(cube.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCone &cone, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(cone.spec) << " Cone \""
     << cone.name << "\"\n";
  if (cone.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(cone.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(cone.radius, "radius", indent + 1);
  ss << print_typed_attr(cone.height, "height", indent + 1);
  ss << print_typed_token_attr(cone.axis, "axis", indent + 1);
  ss << print_gprim_predefined(cone, indent + 1);
  ss << print_props(cone.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCylinder &cylinder, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(cylinder.spec) << " Cylinder \""
     << cylinder.name << "\"\n";
  if (cylinder.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(cylinder.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(cylinder.radius, "radius", indent + 1);
  ss << print_typed_attr(cylinder.height, "height", indent + 1);
  ss << print_typed_token_attr(cylinder.axis, "axis", indent + 1);
  ss << print_gprim_predefined(cylinder, indent + 1);
  ss << print_props(cylinder.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

std::string to_string(const GeomCapsule &capsule, const uint32_t indent, bool closing_brace) {
  std::stringstream ss;

  ss << pprint::Indent(indent) << to_string(capsule.spec) << " Capsule \""
     << capsule.name << "\"\n";
  if (capsule.meta.authored()) {
    ss << pprint::Indent(indent) << "(\n";
    ss << print_prim_metas(capsule.meta, indent + 1);
    ss << pprint::Indent(indent) << ")\n";
  }
  ss << pprint::Indent(indent) << "{\n";

  ss << print_typed_attr(capsule.radius, "radius", indent + 1);
  ss << print_typed_attr(capsule.height, "height", indent + 1);
  ss << print_typed_token_attr(capsule.axis, "axis", indent + 1);
  ss << print_gprim_predefined(capsule, indent + 1);
  ss << print_props(capsule.props, indent + 1);

  if (closing_brace) {
    ss << pprint::Indent(indent) << "}\n";
  }

  return ss.str();
}

} // namespace tinyusdz
