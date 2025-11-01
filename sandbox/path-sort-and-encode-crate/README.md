# Path Sorting Implementation and Validation

This directory contains an implementation of USD path sorting compatible with OpenUSD's `SdfPath` sorting algorithm, along with validation tests.

## Overview

The path sorting algorithm is critical for the Crate format's PATHS encoding, which requires paths to be sorted in a specific hierarchical order for compression and tree representation.

## Files

- `path-sort.hh` - Header with path sorting interface
- `path-sort.cc` - Implementation of path comparison and sorting
- `validate-path-sort.cc` - Validation program comparing with OpenUSD SdfPath
- `CMakeLists.txt` - Build configuration
- `README.md` - This file

## Algorithm

The sorting follows OpenUSD's SdfPath comparison rules:

1. **Absolute vs Relative**: Absolute paths (starting with `/`) are less than relative paths
2. **Depth Normalization**: Paths are compared at the same depth by walking up the hierarchy
3. **Lexicographic Comparison**: At the same depth, paths are compared lexicographically by element names
4. **Property Handling**: Prim parts are compared first; property parts are compared only if prim parts match

### Example Sorted Order

```
/
/World
/World/Geom
/World/Geom/mesh
/World/Geom/mesh.normals
/World/Geom/mesh.points
/World/Lights
/aaa
/aaa/bbb
/zzz
```

## Building

### Prerequisites

- CMake 3.16+
- C++14 compiler
- OpenUSD built and installed in `aousd/dist` or `aousd/dist_monolithic`

### Build Steps

```bash
cd sandbox/path-sort
mkdir build && cd build
cmake ..
make
```

## Running Validation

```bash
./validate-path-sort
```

The validation program:
1. Creates a set of test paths (various prim and property paths)
2. Sorts them using both TinyUSDZ and OpenUSD implementations
3. Compares the sorted order element-by-element
4. Performs pairwise comparison validation
5. Reports SUCCESS or FAILURE with details

### Expected Output

```
============================================================
TinyUSDZ Path Sorting Validation
Comparing against OpenUSD SdfPath
============================================================

Creating N test paths...

Comparing sorted results...

[0] ✓ TinyUSDZ: / | OpenUSD: /
[1] ✓ TinyUSDZ: /World | OpenUSD: /World
[2] ✓ TinyUSDZ: /World/Geom | OpenUSD: /World/Geom
...

============================================================
SUCCESS: All paths sorted identically!
============================================================
```

## Implementation Details

### Key Functions

- `ParsePath()` - Parses path string into hierarchical elements
- `ComparePathElements()` - Compares element vectors (implements `_LessThanCompareNodes`)
- `ComparePaths()` - Main comparison function (implements `SdfPath::operator<`)
- `SortPaths()` - Convenience function to sort path vectors

### Comparison Algorithm

The implementation mirrors OpenUSD's `_LessThanCompareNodes` from `pxr/usd/sdf/path.cpp`:

```cpp
int ComparePathElements(lhs_elements, rhs_elements) {
  // 1. Handle root node cases
  if (lhs is root && rhs is not) return -1;

  // 2. Walk to same depth
  while (diff < 0) lhs_idx--;
  while (diff > 0) rhs_idx--;

  // 3. Check if same path up to depth
  if (same_prefix) {
    return compare_by_length();
  }

  // 4. Find first differing nodes with same parent
  while (parents_differ) {
    walk_up_both();
  }

  // 5. Compare elements lexicographically
  return CompareElements(lhs[idx], rhs[idx]);
}
```

## Integration with Crate Writer

This sorting implementation will be used in TinyUSDZ's `crate-writer.cc` when writing the PATHS section:

```cpp
// Sort paths for tree encoding
std::vector<Path> sorted_paths = all_paths;
tinyusdz::pathsort::SortPaths(sorted_paths);

// Build compressed tree representation
WriteCompressedPathData(sorted_paths);
```

## References

- OpenUSD source: `pxr/usd/sdf/path.cpp` (lines 2090-2158)
- OpenUSD source: `pxr/usd/sdf/pathNode.h` (lines 600-650)
- Documentation: `aousd/paths-encoding.md`
