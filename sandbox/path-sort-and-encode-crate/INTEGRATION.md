# Integration Guide

## Overview

The Crate Path Encoding library is designed as a standalone, reusable module that can be integrated into any USD implementation. It provides:

1. **Path sorting** compatible with OpenUSD SdfPath ordering
2. **Tree encoding** for Crate v0.4.0+ compressed PATHS format
3. **Modular architecture** with clean interfaces

## Directory Structure

```
include/crate/          # Public headers (install these)
├── path_interface.hh   # Abstract path interface
├── path_sort.hh        # Path sorting API
└── tree_encode.hh      # Tree encoding/decoding API

src/                    # Implementation (compile these)
├── path_sort.cc
└── tree_encode.cc

adapters/               # Integration adapters
└── tinyusdz_adapter.hh # Example adapter for TinyUSDZ

tests/                  # Optional tests
├── test-tree-encode.cc
└── validate-path-sort.cc
```

## Integration Methods

### Method 1: Header-Only Integration (Simplest)

Copy the necessary files into your project:

```bash
# Copy public headers
cp -r include/crate /your/project/include/

# Copy implementation
cp src/*.cc /your/project/src/

# Add to your build system
```

In your CMakeLists.txt:
```cmake
add_library(your_lib
  ...
  src/path_sort.cc
  src/tree_encode.cc
)

target_include_directories(your_lib PUBLIC
  include
)
```

### Method 2: Static Library (Recommended)

Build as a static library and link:

```bash
cd sandbox/path-sort-and-encode-crate
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make install  # Installs to CMAKE_INSTALL_PREFIX
```

In your project:
```cmake
find_library(CRATE_ENCODING crate-encoding)
target_link_libraries(your_target ${CRATE_ENCODING})
target_include_directories(your_target PUBLIC /path/to/installed/include)
```

### Method 3: As a Git Submodule

```bash
cd your_project
git submodule add <repo_url> third_party/crate-encoding
```

In your CMakeLists.txt:
```cmake
add_subdirectory(third_party/crate-encoding)
target_link_libraries(your_target crate-encoding)
```

## Usage Examples

### Example 1: Using SimplePath (Built-in)

```cpp
#include "crate/path_interface.hh"
#include "crate/path_sort.hh"
#include "crate/tree_encode.hh"

using namespace crate;

// Create paths
std::vector<SimplePath> paths = {
  SimplePath("/World/Geom", ""),
  SimplePath("/World/Geom", "points"),
  SimplePath("/", ""),
  SimplePath("/World", ""),
};

// Sort paths (required before encoding)
SortSimplePaths(paths);

// Encode to compressed tree format
CompressedPathTree tree = EncodePaths(paths);

// Access encoded data
for (size_t i = 0; i < tree.size(); ++i) {
  PathIndex path_idx = tree.path_indexes[i];
  TokenIndex token_idx = tree.element_token_indexes[i];
  int32_t jump = tree.jumps[i];

  // Use for serialization...
}

// Decode back to paths
std::vector<SimplePath> decoded = DecodePaths(tree);

// Validate round-trip
std::vector<std::string> errors;
bool valid = ValidateRoundTrip(paths, tree, &errors);
```

### Example 2: Using Your Own Path Class

```cpp
#include "crate/path_interface.hh"
#include "crate/path_sort.hh"
#include "crate/tree_encode.hh"
#include "your_path.hh"  // Your path implementation

// Step 1: Create an adapter
class YourPathAdapter : public crate::IPath {
public:
  explicit YourPathAdapter(const YourPath& path) : path_(path) {}

  std::string GetString() const override {
    return path_.ToString();  // Adapt to your API
  }

  std::string GetPrimPart() const override {
    return path_.GetPrimPath();  // Adapt to your API
  }

  std::string GetPropertyPart() const override {
    return path_.GetPropertyName();  // Adapt to your API
  }

  bool IsAbsolute() const override {
    return path_.IsAbsolutePath();  // Adapt to your API
  }

  bool IsPrimPath() const override {
    return !path_.HasProperty();  // Adapt to your API
  }

  bool IsPropertyPath() const override {
    return path_.HasProperty();  // Adapt to your API
  }

  IPath* Clone() const override {
    return new YourPathAdapter(path_);
  }

private:
  YourPath path_;
};

// Step 2: Use with your paths
std::vector<YourPath> your_paths = {...};

// Convert to adapters
std::vector<std::unique_ptr<YourPathAdapter>> adapted;
for (const auto& p : your_paths) {
  adapted.push_back(std::make_unique<YourPathAdapter>(p));
}

// Sort using generic interface
crate::SortPaths(adapted);

// Encode using generic interface
crate::CompressedPathTree tree = crate::EncodePathsGeneric(adapted);
```

### Example 3: TinyUSDZ Integration

```cpp
#include "crate/path_interface.hh"
#include "crate/path_sort.hh"
#include "crate/tree_encode.hh"
#include "adapters/tinyusdz_adapter.hh"
#include "prim-types.hh"  // TinyUSDZ

using namespace tinyusdz;
using namespace crate;

// Your TinyUSDZ paths
std::vector<Path> tiny_paths = {...};

// Method A: Convert to SimplePath
std::vector<SimplePath> simple_paths;
for (const auto& p : tiny_paths) {
  simple_paths.emplace_back(p.prim_part(), p.prop_part());
}

SortSimplePaths(simple_paths);
CompressedPathTree tree = EncodePaths(simple_paths);

// Method B: Use adapter (for zero-copy scenarios)
std::vector<std::unique_ptr<adapters::TinyUSDZPathAdapter>> adapted;
for (const auto& p : tiny_paths) {
  adapted.push_back(
    std::make_unique<adapters::TinyUSDZPathAdapter>(p.prim_part(), p.prop_part())
  );
}

SortPaths(adapted);
tree = EncodePathsGeneric(adapted);
```

## Integration into Crate Writer

### Typical workflow:

```cpp
// 1. Collect all paths from your USD stage
std::vector<SimplePath> all_paths;
// ... collect from prims, properties, etc.

// 2. Sort paths (REQUIRED)
crate::SortSimplePaths(all_paths);

// 3. Encode to compressed format
crate::CompressedPathTree compressed = crate::EncodePaths(all_paths);

// 4. Write to file
WriteToFile(file, compressed.path_indexes);
WriteToFile(file, compressed.element_token_indexes);
WriteToFile(file, compressed.jumps);
WriteToFile(file, compressed.token_table);

// Optional: Apply integer compression (not included in this library)
// compressed_data = Sdf_IntegerCompression::Compress(compressed.path_indexes);
```

## API Reference

### Path Interface

```cpp
class IPath {
  virtual std::string GetString() const = 0;
  virtual std::string GetPrimPart() const = 0;
  virtual std::string GetPropertyPart() const = 0;
  virtual bool IsAbsolute() const = 0;
  virtual bool IsPrimPath() const = 0;
  virtual bool IsPropertyPath() const = 0;
  virtual IPath* Clone() const = 0;
};
```

### Sorting

```cpp
// Compare two paths (-1, 0, or 1)
int ComparePaths(const IPath& lhs, const IPath& rhs);

// Sort vector of paths
template<typename PathPtr>
void SortPaths(std::vector<PathPtr>& paths);

// Convenience for SimplePath
void SortSimplePaths(std::vector<SimplePath>& paths);
```

### Encoding

```cpp
// Encode sorted paths to compressed format
CompressedPathTree EncodePaths(const std::vector<SimplePath>& sorted_paths);

// Generic version for custom path types
template<typename PathPtr>
CompressedPathTree EncodePathsGeneric(const std::vector<PathPtr>& sorted_paths);

// Decode compressed format back to paths
std::vector<SimplePath> DecodePaths(const CompressedPathTree& compressed);

// Validate encode/decode round-trip
bool ValidateRoundTrip(
  const std::vector<SimplePath>& original,
  const CompressedPathTree& compressed,
  std::vector<std::string>* errors = nullptr
);
```

## Dependencies

**Core library**: NONE (C++17 standard library only)

**Optional**:
- OpenUSD (for validation tests only, not required for library use)

## Build Options

```cmake
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_VALIDATION_TESTS=OFF \  # ON to build OpenUSD validation
  ..
```

## Thread Safety

The library is **thread-safe** for read operations (sorting, encoding) but **not thread-safe** for TokenTable mutations. If using multiple threads:

- Use separate TokenTable instances per thread, OR
- Synchronize access to shared TokenTable

## Performance Notes

- **Sorting**: O(N log N) where N is number of paths
- **Encoding**: O(N) tree building + O(N) depth-first traversal
- **Memory**: O(N) for tree nodes + O(N) for output arrays

## License

Apache 2.0 - See LICENSE file

## Support

For issues or questions, refer to the main TinyUSDZ project or create an issue.
