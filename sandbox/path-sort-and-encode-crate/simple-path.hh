//
// Simplified Path class for validation purposes
// SPDX-License-Identifier: Apache 2.0
//
#pragma once

#include <string>

namespace tinyusdz {

// Simplified Path class for testing sorting algorithm
class SimplePath {
 public:
  SimplePath() = default;
  SimplePath(const std::string& prim, const std::string& prop)
    : _prim_part(prim), _prop_part(prop) {}

  const std::string& prim_part() const { return _prim_part; }
  const std::string& prop_part() const { return _prop_part; }

  std::string full_path_name() const {
    if (_prop_part.empty()) {
      return _prim_part;
    }
    return _prim_part + "." + _prop_part;
  }

 private:
  std::string _prim_part;
  std::string _prop_part;
};

} // namespace tinyusdz
