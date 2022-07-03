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

namespace std {

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec2i &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec3i &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec4i &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec2h &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec3h &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec4h &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

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

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Token &v) {
  os << "\"" << v.str() << "\"";
  return os;
}

inline std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Matrix4f &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << ", " << m.m[0][2] << ", " << m.m[0][3] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ", " << m.m[1][2] << ", " << m.m[1][3] << "), ";
  ofs << "(" << m.m[2][0] << ", " << m.m[2][1] << ", " << m.m[2][2] << ", " << m.m[2][3] << "), ";
  ofs << "(" << m.m[3][0] << ", " << m.m[3][1] << ", " << m.m[3][2] << ", " << m.m[3][3] << ")";

  ofs << " )";

  return ofs;
}

inline std::ostream &operator<<(std::ostream &ofs, const tinyusdz::Matrix4d &m) {
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

#if 0
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::point3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::point3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::point3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::normal3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::normal3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::normal3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::vector3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::vector3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::vector3d &v);


std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::color3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::color3d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::color4f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::color4d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord2h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord2f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord2d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord3h &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord3f &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::texcoord3d &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::quath &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::quatf &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::quatd &v);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::token &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::dict &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::TimeSample &ts);

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::matrix2d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::matrix3d &v);
std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::matrix4d &v);
#endif


} // namespace std

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

// primvar type

//std::ostream &operator<<(std::ostream &os, const primvar::int2 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::int3 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::int4 &v);
//
//std::ostream &operator<<(std::ostream &os, const primvar::uint2 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::uint3 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::uint4 &v);
//
//std::ostream &operator<<(std::ostream &os, const primvar::half2 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::half3 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::half4 &v);
//
//std::ostream &operator<<(std::ostream &os, const primvar::float2 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::float3 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::float4 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::double2 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::double3 &v);
//std::ostream &operator<<(std::ostream &os, const primvar::double4 &v);


//

std::string to_string(const Klass &klass, const uint32_t indent = 0);
std::string to_string(const GPrim &gprim, const uint32_t indent = 0);
std::string to_string(const Xform &xform, const uint32_t indent = 0);
std::string to_string(const GeomSphere &sphere, const uint32_t indent = 0);
std::string to_string(const GeomMesh &mesh, const uint32_t indent = 0);
std::string to_string(const GeomPoints &pts, const uint32_t indent = 0);
std::string to_string(const GeomBasisCurves &curves, const uint32_t indent = 0);
std::string to_string(const GeomCapsule &geom, const uint32_t indent = 0);
std::string to_string(const GeomCone &geom, const uint32_t indent = 0);
std::string to_string(const GeomCylinder &geom, const uint32_t indent = 0);
std::string to_string(const GeomCube &geom, const uint32_t indent = 0);
std::string to_string(const GeomCamera &camera, const uint32_t indent = 0);

std::string to_string(const LuxSphereLight &light, const uint32_t indent = 0);
std::string to_string(const LuxDomeLight &light, const uint32_t indent = 0);
std::string to_string(const Shader &shader, const uint32_t indent = 0);

std::string to_string(const tinyusdz::AnimatableVisibility &v, const uint32_t );

} // namespace tinyusdz
