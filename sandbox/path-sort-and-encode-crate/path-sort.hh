//
// Path sorting implementation compatible with OpenUSD SdfPath sorting
// SPDX-License-Identifier: Apache 2.0
//
#pragma once

#include <string>
#include <vector>
#include <algorithm>

namespace tinyusdz {

namespace pathsort {

///
/// Path element representation for sorting
/// Mirrors the hierarchical structure used in OpenUSD's Sdf_PathNode
///
struct PathElement {
  std::string name;           // Element name (prim or property name)
  bool is_absolute = false;   // Is this an absolute path?
  bool is_property = false;   // Is this a property element?
  int depth = 0;              // Depth in the path hierarchy

  PathElement() = default;
  PathElement(const std::string& n, bool abs, bool prop, int d)
    : name(n), is_absolute(abs), is_property(prop), depth(d) {}
};

///
/// Parse a path string into hierarchical elements
/// Examples:
///   "/" -> [{"", absolute=true, depth=0}]
///   "/foo/bar" -> [{"foo", absolute=true, depth=1}, {"bar", absolute=true, depth=2}]
///   "/foo.prop" -> [{"foo", absolute=true, depth=1}, {"prop", property=true, depth=2}]
///
std::vector<PathElement> ParsePath(const std::string& prim_part, const std::string& prop_part);

///
/// Compare two paths following OpenUSD SdfPath comparison rules:
///
/// 1. Absolute paths are less than relative paths
/// 2. For paths with different prim parts, compare prim hierarchy
/// 3. For same prim parts, property parts are compared
/// 4. Comparison walks up to same depth, then compares lexicographically
///
/// Returns:
///   < 0 if lhs < rhs
///   = 0 if lhs == rhs
///   > 0 if lhs > rhs
///

///
/// Compare path elements at the same depth
/// Elements are compared lexicographically by name
///
inline int CompareElements(const PathElement& lhs, const PathElement& rhs) {
  // Compare names lexicographically
  if (lhs.name < rhs.name) {
    return -1;
  } else if (lhs.name > rhs.name) {
    return 1;
  }
  return 0;
}

///
/// Internal comparison for path element vectors
/// This implements the core algorithm from OpenUSD's _LessThanCompareNodes
///
int ComparePathElements(const std::vector<PathElement>& lhs_elements,
                        const std::vector<PathElement>& rhs_elements);

} // namespace pathsort
} // namespace tinyusdz
