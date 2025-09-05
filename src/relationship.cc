// SPDX-License-Identifier: Apache 2.0

///
/// @file relationship.cc
/// @brief USD Relationship class implementation
///

#include "prim-types.hh"  // Must be included first for type definitions
#include "relationship.hh"

namespace tinyusdz {

AttrMeta &Relationship::metas() {
  if (!_metas) {
    _metas = new AttrMetas();
  }
  return *_metas;
}

size_t Relationship::estimate_memory_usage() const {
  size_t total = sizeof(Relationship);

  total += targetPath.full_path_name().size();
  for (const auto& path : targetPathVector) {
    // Path internally contains strings, estimate their capacity
    total += path.full_path_name().size();
  }

  if (_metas) {
    total += sizeof(AttrMeta);
  }

  return total;
}

}  // namespace tinyusdz