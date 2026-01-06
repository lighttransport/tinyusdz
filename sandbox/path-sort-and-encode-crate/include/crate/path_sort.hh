//
// Path sorting for USD Crate format
// SPDX-License-Identifier: Apache 2.0
//
// Implements OpenUSD-compatible path sorting for Crate format encoding.
// Works with any implementation of the IPath interface.
//
#pragma once

#include "path_interface.hh"
#include <vector>
#include <memory>
#include <algorithm>

namespace crate {

///
/// Compare two paths following OpenUSD SdfPath comparison rules
///
/// Returns:
///   < 0 if lhs < rhs
///   = 0 if lhs == rhs
///   > 0 if lhs > rhs
///
/// Rules:
/// 1. Absolute paths are less than relative paths
/// 2. For paths at different depths, compare after normalizing to same depth
/// 3. At same depth, compare elements lexicographically
/// 4. Prim parts are compared before property parts
///
int ComparePaths(const IPath& lhs, const IPath& rhs);

///
/// Sort a vector of paths in-place using OpenUSD-compatible ordering
///
/// This modifies the input vector to be in sorted order.
/// Paths must remain valid for the duration of the sort.
///
template<typename PathPtr>
void SortPaths(std::vector<PathPtr>& paths) {
  std::sort(paths.begin(), paths.end(),
            [](const PathPtr& lhs, const PathPtr& rhs) {
              return ComparePaths(*lhs, *rhs) < 0;
            });
}

///
/// Specialization for SimplePath (direct comparison without pointers)
///
void SortSimplePaths(std::vector<SimplePath>& paths);

} // namespace crate
