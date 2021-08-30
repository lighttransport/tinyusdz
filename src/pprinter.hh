#pragma once

//
// pretty-print routine(using iostream)
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

} // namespace std
