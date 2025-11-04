//
// Path sorting implementation compatible with OpenUSD SdfPath sorting
// SPDX-License-Identifier: Apache 2.0
//
#include "path-sort.hh"
#include "simple-path.hh"
#include <sstream>

namespace tinyusdz {
namespace pathsort {

std::vector<PathElement> ParsePath(const std::string& prim_part, const std::string& prop_part) {
  std::vector<PathElement> elements;

  // Check if absolute or relative
  bool is_absolute = !prim_part.empty() && prim_part[0] == '/';

  // Parse prim part
  if (!prim_part.empty()) {
    std::string path_str = prim_part;

    // Skip leading '/' for absolute paths
    size_t start = is_absolute ? 1 : 0;

    // Root path special case
    if (path_str == "/") {
      elements.push_back(PathElement("", is_absolute, false, 0));
      return elements;
    }

    // Split by '/'
    int depth = 0;
    size_t pos = start;
    while (pos < path_str.size()) {
      size_t next = path_str.find('/', pos);
      if (next == std::string::npos) {
        next = path_str.size();
      }

      std::string element = path_str.substr(pos, next - pos);
      if (!element.empty()) {
        depth++;
        elements.push_back(PathElement(element, is_absolute, false, depth));
      }

      pos = next + 1;
    }
  }

  // Parse property part
  if (!prop_part.empty()) {
    int depth = static_cast<int>(elements.size()) + 1;
    elements.push_back(PathElement(prop_part, is_absolute, true, depth));
  }

  return elements;
}

int ComparePathElements(const std::vector<PathElement>& lhs_elements,
                        const std::vector<PathElement>& rhs_elements) {
  // This implements the algorithm from OpenUSD's _LessThanCompareNodes

  int lhs_count = static_cast<int>(lhs_elements.size());
  int rhs_count = static_cast<int>(rhs_elements.size());

  // Root node handling - if either has no elements, it's the root
  if (lhs_count == 0 || rhs_count == 0) {
    if (lhs_count == 0 && rhs_count > 0) {
      return -1;  // lhs is root, rhs is not -> lhs < rhs
    } else if (lhs_count > 0 && rhs_count == 0) {
      return 1;   // rhs is root, lhs is not -> lhs > rhs
    }
    return 0;  // Both are root
  }

  int diff = rhs_count - lhs_count;

  // Walk indices to same depth
  int lhs_idx = lhs_count - 1;
  int rhs_idx = rhs_count - 1;

  // Walk up lhs if it's deeper
  while (diff < 0) {
    lhs_idx--;
    diff++;
  }

  // Walk up rhs if it's deeper
  while (diff > 0) {
    rhs_idx--;
    diff--;
  }

  // Now both are at the same depth
  // Check if they're the same path up to this point
  bool same_prefix = true;
  if (lhs_idx >= 0 && rhs_idx >= 0) {
    // Walk back to root comparing elements
    int l = lhs_idx;
    int r = rhs_idx;
    while (l >= 0 && r >= 0) {
      if (lhs_elements[l].name != rhs_elements[r].name ||
          lhs_elements[l].is_property != rhs_elements[r].is_property) {
        same_prefix = false;
        break;
      }
      l--;
      r--;
    }
  }

  if (same_prefix && lhs_idx >= 0 && rhs_idx >= 0) {
    // They differ only in the tail
    // The shorter path is less than the longer path
    if (lhs_count < rhs_count) {
      return -1;
    } else if (lhs_count > rhs_count) {
      return 1;
    }
    return 0;
  }

  // Find the first differing elements with the same parent
  lhs_idx = lhs_count - 1;
  rhs_idx = rhs_count - 1;

  // Walk up to same depth again
  diff = rhs_count - lhs_count;
  while (diff < 0) {
    lhs_idx--;
    diff++;
  }
  while (diff > 0) {
    rhs_idx--;
    diff--;
  }

  // Walk up both until parents match
  while (lhs_idx > 0 && rhs_idx > 0) {
    // Check if parents match (all elements before current index)
    bool parents_match = true;
    if (lhs_idx > 0 && rhs_idx > 0) {
      for (int i = 0; i < lhs_idx && i < rhs_idx; i++) {
        if (lhs_elements[i].name != rhs_elements[i].name ||
            lhs_elements[i].is_property != rhs_elements[i].is_property) {
          parents_match = false;
          break;
        }
      }
    }

    if (parents_match) {
      break;
    }

    lhs_idx--;
    rhs_idx--;
  }

  // Compare the elements at the divergence point
  if (lhs_idx >= 0 && rhs_idx >= 0 && lhs_idx < lhs_count && rhs_idx < rhs_count) {
    return CompareElements(lhs_elements[lhs_idx], rhs_elements[rhs_idx]);
  }

  // Fallback: compare sizes
  if (lhs_count < rhs_count) {
    return -1;
  } else if (lhs_count > rhs_count) {
    return 1;
  }

  return 0;
}

int CompareSimplePaths(const SimplePath& lhs, const SimplePath& rhs) {
  // Parse both paths into elements
  std::vector<PathElement> lhs_prim_elements = ParsePath(lhs.prim_part(), "");
  std::vector<PathElement> rhs_prim_elements = ParsePath(rhs.prim_part(), "");

  // Check absolute vs relative
  bool lhs_is_abs = !lhs.prim_part().empty() && lhs.prim_part()[0] == '/';
  bool rhs_is_abs = !rhs.prim_part().empty() && rhs.prim_part()[0] == '/';

  // Absolute paths are less than relative paths
  if (lhs_is_abs != rhs_is_abs) {
    return lhs_is_abs ? -1 : 1;
  }

  // Compare prim parts
  int prim_cmp = ComparePathElements(lhs_prim_elements, rhs_prim_elements);
  if (prim_cmp != 0) {
    return prim_cmp;
  }

  // Prim parts are equal, compare property parts
  if (lhs.prop_part().empty() && rhs.prop_part().empty()) {
    return 0;
  }

  if (lhs.prop_part().empty()) {
    return -1;  // No property is less than having a property
  }

  if (rhs.prop_part().empty()) {
    return 1;
  }

  // Both have properties, compare them
  if (lhs.prop_part() < rhs.prop_part()) {
    return -1;
  } else if (lhs.prop_part() > rhs.prop_part()) {
    return 1;
  }

  return 0;
}

} // namespace pathsort
} // namespace tinyusdz
