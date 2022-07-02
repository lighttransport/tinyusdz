#include "primvar-pprint.hh"

namespace std {

std::ostream &operator<<(std::ostream &os, const tinyusdz::primvar::vector3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::vector3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::vector3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::normal3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::normal3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::normal3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::point3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::point3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::point3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::color3f &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::color3d &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::color4f &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ", " << v.a << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::color4d &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ", " << v.a << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::quath &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::quatf &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::quatd &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::primvar::texcoord2f &v) {
  os << "(" << v.s << ", " << v.t << ")";
  return os;
}

}
