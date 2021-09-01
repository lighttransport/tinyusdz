#pragma once

//
// pretty-print routine(using iostream) in non-intrusive way.
// Some build configuration may not want I/O module(e.g. mobile/embedded device), so provide print routines in separated file.
//
// TODO: Move `print` related code in other files to here.
//

#include <string>
#include <ostream>

#include "prim-types.hh"

namespace std {

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec2f &v) {
  os << "[" << v[0] << ", " << v[1] << "]";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec3f &v) {
  os << "[" << v[0] << ", " << v[1] << ", " << v[2] << "]";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec4f &v) {
  os << "[" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << "]";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec2d &v) {
  os << "[" << v[0] << ", " << v[1] << "]";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec3d &v) {
  os << "[" << v[0] << ", " << v[1] << ", " << v[2] << "]";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const tinyusdz::Vec4d &v) {
  os << "[" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << "]";
  return os;
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
