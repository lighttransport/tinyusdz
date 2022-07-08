// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#include "crate-pprint.hh"

#include <string>

namespace std {

std::ostream &operator<<(std::ostream &os, const tinyusdz::crate::Index &i) {
  os << std::to_string(i.value);
  return os;
}

} // namespace std

