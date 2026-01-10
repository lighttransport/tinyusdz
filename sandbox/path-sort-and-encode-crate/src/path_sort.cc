//
// Path sorting implementation
// SPDX-License-Identifier: Apache 2.0
//
#include "crate/path_sort.hh"
#include <algorithm>
#include <vector>
#include <string>

namespace crate {

// Internal helper to parse path into elements
struct PathElement {
  std::string name;
  bool is_absolute = false;
  bool is_property = false;
  int depth = 0;

  PathElement() = default;
  PathElement(const std::string& n, bool abs, bool prop, int d)
    : name(n), is_absolute(abs), is_property(prop), depth(d) {}
};

static std::vector<PathElement> ParsePath(const std::string& prim_part, const std::string& prop_part) {
  std::vector<PathElement> elements;

  bool is_absolute = !prim_part.empty() && prim_part[0] == '/';

  // Parse prim part
  if (!prim_part.empty()) {
    if (prim_part == "/") {
      elements.push_back(PathElement("", is_absolute, false, 0));
      return elements;
    }

    size_t start = is_absolute ? 1 : 0;
    int depth = 0;

    while (start < prim_part.size()) {
      size_t end = prim_part.find('/', start);
      if (end == std::string::npos) {
        end = prim_part.size();
      }

      std::string element = prim_part.substr(start, end - start);
      if (!element.empty()) {
        depth++;
        elements.push_back(PathElement(element, is_absolute, false, depth));
      }

      start = end + 1;
    }
  }

  // Parse property part
  if (!prop_part.empty()) {
    int depth = static_cast<int>(elements.size()) + 1;
    elements.push_back(PathElement(prop_part, is_absolute, true, depth));
  }

  return elements;
}

static int CompareElements(const PathElement& lhs, const PathElement& rhs) {
  if (lhs.name < rhs.name) {
    return -1;
  } else if (lhs.name > rhs.name) {
    return 1;
  }
  return 0;
}

static int ComparePathElements(
  const std::vector<PathElement>& lhs_elements,
  const std::vector<PathElement>& rhs_elements
) {
  int lhs_count = static_cast<int>(lhs_elements.size());
  int rhs_count = static_cast<int>(rhs_elements.size());

  // Root node handling
  if (lhs_count == 0 || rhs_count == 0) {
    if (lhs_count == 0 && rhs_count > 0) {
      return -1;
    } else if (lhs_count > 0 && rhs_count == 0) {
      return 1;
    }
    return 0;
  }

  int diff = rhs_count - lhs_count;
  int lhs_idx = lhs_count - 1;
  int rhs_idx = rhs_count - 1;

  // Walk to same depth
  while (diff < 0) {
    lhs_idx--;
    diff++;
  }
  while (diff > 0) {
    rhs_idx--;
    diff--;
  }

  // Check if same path up to this point
  bool same_prefix = true;
  if (lhs_idx >= 0 && rhs_idx >= 0) {
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
    // Differ only in tail - shorter is less
    if (lhs_count < rhs_count) {
      return -1;
    } else if (lhs_count > rhs_count) {
      return 1;
    }
    return 0;
  }

  // Find first differing elements with same parent
  lhs_idx = lhs_count - 1;
  rhs_idx = rhs_count - 1;

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

  // Compare elements at divergence point
  if (lhs_idx >= 0 && rhs_idx >= 0 &&
      lhs_idx < lhs_count && rhs_idx < rhs_count) {
    return CompareElements(lhs_elements[lhs_idx], rhs_elements[rhs_idx]);
  }

  // Fallback
  if (lhs_count < rhs_count) {
    return -1;
  } else if (lhs_count > rhs_count) {
    return 1;
  }

  return 0;
}

int ComparePaths(const IPath& lhs, const IPath& rhs) {
  // Parse paths
  auto lhs_elements = ParsePath(lhs.GetPrimPart(), lhs.GetPropertyPart());
  auto rhs_elements = ParsePath(rhs.GetPrimPart(), rhs.GetPropertyPart());

  // Check absolute vs relative
  bool lhs_is_abs = lhs.IsAbsolute();
  bool rhs_is_abs = rhs.IsAbsolute();

  // Absolute paths are less than relative paths
  if (lhs_is_abs != rhs_is_abs) {
    return lhs_is_abs ? -1 : 1;
  }

  // Compare prim parts
  int prim_cmp = ComparePathElements(lhs_elements, rhs_elements);
  if (prim_cmp != 0) {
    return prim_cmp;
  }

  // Prim parts equal, compare property parts
  const std::string& lhs_prop = lhs.GetPropertyPart();
  const std::string& rhs_prop = rhs.GetPropertyPart();

  if (lhs_prop.empty() && rhs_prop.empty()) {
    return 0;
  }

  if (lhs_prop.empty()) {
    return -1;
  }

  if (rhs_prop.empty()) {
    return 1;
  }

  if (lhs_prop < rhs_prop) {
    return -1;
  } else if (lhs_prop > rhs_prop) {
    return 1;
  }

  return 0;
}

void SortSimplePaths(std::vector<SimplePath>& paths) {
  std::sort(paths.begin(), paths.end(),
            [](const SimplePath& lhs, const SimplePath& rhs) {
              return ComparePaths(lhs, rhs) < 0;
            });
}

} // namespace crate
