//
// Public API for path sorting using SimplePath
// SPDX-License-Identifier: Apache 2.0
//
#pragma once

#include "simple-path.hh"
#include <vector>
#include <algorithm>

namespace tinyusdz {
namespace pathsort {

// Forward declarations from path-sort.hh
int CompareSimplePaths(const SimplePath& lhs, const SimplePath& rhs);

///
/// Less-than comparator for SimplePath sorting
///
struct SimplePathLessThan {
  bool operator()(const SimplePath& lhs, const SimplePath& rhs) const;
};

///
/// Sort a vector of paths in-place using OpenUSD-compatible ordering
///
void SortSimplePaths(std::vector<SimplePath>& paths);

} // namespace pathsort
} // namespace tinyusdz
