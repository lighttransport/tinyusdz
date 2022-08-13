#pragma once

//
// pretty-print routine(using iostream) in non-intrusive way.
// Some build configuration may not want I/O module(e.g. mobile/embedded device), so provide print routines in separated file.
//
//

#include <string>
#include <ostream>
#include <sstream>

#include "prim-types.hh"
#include "usdGeom.hh"
#include "usdSkel.hh"
#include "usdShade.hh"
#include "usdLux.hh"

namespace tinyusdz {

constexpr char kIndentString[] = "  ";

std::string Indent(uint32_t n);

std::string to_string(Visibility v);
std::string to_string(Orientation o);
std::string to_string(Extent e);
std::string to_string(Interpolation interp);
std::string to_string(Axis axis);
std::string to_string(ListEditQual axis);
std::string to_string(Specifier specifier);
std::string to_string(SubdivisionScheme subd_scheme);
std::string to_string(Purpose purpose);
std::string to_string(Permission permission);
std::string to_string(Variability variability);
std::string to_string(SpecType spec_type);

std::string to_string(const Path &path, bool show_full_path = true);
std::string to_string(const std::vector<Path> &v, bool show_full_path = true);

template<typename T>
std::string to_string(const std::vector<T> &v, const uint32_t level = 0) {
  std::stringstream ss;
  ss << Indent(level) << "[";

  // TODO(syoyo): indent
  for (size_t i = 0; i < v.size(); i++) {
    ss << to_string(v[i]);
    if (i != (v.size() -1)) {
      ss << ", ";
    }
  }
  ss << "]";
  return ss.str();
}

template<typename T>
std::string to_string(const ListOp<T> &op, const uint32_t indent_level = 0) {
  std::stringstream ss;
  ss << Indent(indent_level) << "ListOp(isExplicit " << op.IsExplicit() << ") {\n";
  ss << Indent(indent_level) << "  explicit_items = " << to_string(op.GetExplicitItems()) << "\n";
  ss << Indent(indent_level) << "  added_items = " << to_string(op.GetAddedItems()) << "\n";
  ss << Indent(indent_level) << "  prepended_items = " << to_string(op.GetPrependedItems()) << "\n";
  ss << Indent(indent_level) << "  deleted_items = " << to_string(op.GetDeletedItems()) << "\n";
  ss << Indent(indent_level) << "  ordered_items = " << to_string(op.GetOrderedItems()) << "\n";
  ss << Indent(indent_level) << "}";

  return ss.str();
}

// Do not recursively print Node info.
//
// Setting `closing_brace` false won't emit `}`(for printing USD scene graph recursively).
//

std::string to_string(const Klass &klass, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GPrim &gprim, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const Xform &xform, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomSphere &sphere, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomMesh &mesh, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomPoints &pts, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomBasisCurves &curves, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomCapsule &geom, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomCone &geom, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomCylinder &geom, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomCube &geom, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const GeomCamera &camera, const uint32_t indent = 0, bool closing_brace = true);

std::string to_string(const SkelRoot &root, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const Skeleton &root, const uint32_t indent = 0, bool closing_brace = true);

std::string to_string(const LuxSphereLight &light, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const LuxDomeLight &light, const uint32_t indent = 0, bool closing_brace = true);
std::string to_string(const Shader &shader, const uint32_t indent = 0, bool closing_brace = true);

std::string to_string(const GeomCamera::Projection &proj, const uint32_t indent = 0, bool closing_brace = true);

std::string to_string(const tinyusdz::AnimatableVisibility &v, const uint32_t indent = 0, bool closing_brace = true);

} // namespace tinyusdz
