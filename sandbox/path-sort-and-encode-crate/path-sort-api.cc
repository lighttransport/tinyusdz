//
// Public API implementation for path sorting
// SPDX-License-Identifier: Apache 2.0
//
#include "path-sort-api.hh"
#include "path-sort.hh"

namespace tinyusdz {
namespace pathsort {

bool SimplePathLessThan::operator()(const SimplePath& lhs, const SimplePath& rhs) const {
  return CompareSimplePaths(lhs, rhs) < 0;
}

void SortSimplePaths(std::vector<SimplePath>& paths) {
  std::sort(paths.begin(), paths.end(), SimplePathLessThan());
}

} // namespace pathsort
} // namespace tinyusdz
