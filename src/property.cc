// SPDX-License-Identifier: Apache 2.0

///
/// @file property.cc
/// @brief USD Property class implementation
///

#include "prim-types.hh"  // Must be included first for type definitions
#include "property.hh"

namespace tinyusdz {

size_t Property::estimate_memory_usage() const {
  size_t total = sizeof(Property);

  // String storage
  total += _prop_value_type_name.capacity();

  total += _attrib.estimate_memory_usage();
  total += _rel.estimate_memory_usage();

  return total;
}

}  // namespace tinyusdz