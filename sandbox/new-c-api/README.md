# TinyUSDZ C99 API

A minimal, clean C99 API for TinyUSDZ that provides USD file loading and scene traversal without requiring C++ knowledge or toolchains.

## Features

- **Pure C99 Interface**: No C++ dependencies in headers
- **Minimal Surface Area**: Focus on essential USD operations
- **Opaque Handles**: Implementation details hidden, ABI stable
- **Zero-Copy Design**: Minimize memory allocation where possible
- **Thread-Safe**: Immutable data access with explicit mutability
- **Type-Safe Enums**: Defined in C to avoid binding overhead

## Quick Start

### Building with CMake

```bash
mkdir build
cd build
cmake ..
make

# Run examples
./example_basic ../../models/simple_mesh.usda
./example_mesh ../../models/simple_mesh.usda
```

### Building with Make

```bash
make
make examples
make test
```

### Installation

```bash
# CMake
cd build
sudo make install

# Or with Make
sudo make install PREFIX=/usr/local
```

## Basic Usage

```c
#include <tinyusdz_c.h>
#include <stdio.h>

int main() {
    // Initialize library
    tusdz_init();

    // Load USD file
    tusdz_stage stage = NULL;
    char error[1024];
    tusdz_result result = tusdz_load_from_file(
        "model.usd", NULL, &stage, error, sizeof(error)
    );

    if (result != TUSDZ_SUCCESS) {
        fprintf(stderr, "Error: %s\n", error);
        return 1;
    }

    // Traverse hierarchy
    tusdz_prim root = tusdz_stage_get_root_prim(stage);
    size_t child_count = tusdz_prim_get_child_count(root);

    for (size_t i = 0; i < child_count; i++) {
        tusdz_prim child = tusdz_prim_get_child_at(root, i);
        const char* name = tusdz_prim_get_name(child);
        printf("Child: %s\n", name);
    }

    // Cleanup
    tusdz_stage_free(stage);
    tusdz_shutdown();
    return 0;
}
```

## API Tiers

### Tier 1: Minimal Viable API (10 functions)
Essential functions for loading and basic traversal:
- `tusdz_init()` / `tusdz_shutdown()`
- `tusdz_load_from_file()` / `tusdz_load_from_memory()`
- `tusdz_stage_free()`
- `tusdz_stage_get_root_prim()`
- `tusdz_prim_get_child_count()` / `tusdz_prim_get_child_at()`
- `tusdz_prim_get_name()` / `tusdz_prim_get_type()`

### Tier 2: Core Functionality (11 functions)
Path operations, properties, and value access:
- Path operations (`get_path`, `get_prim_at_path`)
- Type checking (`is_type`, `get_type_name`)
- Property access (`get_property_count`, `get_property`)
- Value extraction (`get_float3`, `get_string`, etc.)

### Tier 3: Extended API (15+ functions)
Mesh data, transforms, materials, and animation:
- Mesh data extraction (points, faces, normals, UVs)
- Transform matrices
- Material and shader access
- Animation and time samples

## Memory Management

The API uses three patterns:

1. **Borrowed References** (most common):
```c
const char* name = tusdz_prim_get_name(prim);  // Do NOT free
// name is valid as long as prim is valid
```

2. **Allocated Data** (for arrays):
```c
float* points = NULL;
size_t count = 0;
if (tusdz_mesh_get_points(mesh, &points, &count) == TUSDZ_SUCCESS) {
    // Use points...
    tusdz_free(points);  // Must free when done
}
```

3. **Handle Lifetime**:
```c
tusdz_stage stage = NULL;
tusdz_load_from_file("model.usd", NULL, &stage, NULL, 0);
// All prims from stage are valid only while stage exists
tusdz_stage_free(stage);  // Invalidates all prims
```

## Error Handling

```c
// Simple - ignore errors
tusdz_stage stage = NULL;
tusdz_load_from_file("model.usd", NULL, &stage, NULL, 0);
if (stage) {
    // Use stage...
}

// Detailed - capture errors
char error[1024];
tusdz_result result = tusdz_load_from_file(
    "model.usd", NULL, &stage, error, sizeof(error)
);
if (result != TUSDZ_SUCCESS) {
    fprintf(stderr, "Failed: %s (code: %d)\n", error, result);
}
```

## Load Options

```c
tusdz_load_options options = {
    .max_memory_limit_mb = 1024,  // 1GB limit
    .max_depth = 10,               // Composition depth
    .enable_composition = 1,        // Resolve references
    .strict_mode = 0,              // Don't fail on warnings
    .structure_only = 0,           // Load full data
    .asset_resolver = NULL         // Custom resolver
};

tusdz_load_from_file("model.usd", &options, &stage, NULL, 0);
```

## Thread Safety

- **Immutable Access**: Reading from stages/prims is thread-safe
- **No Global State**: No hidden global state modified by API calls
- **Explicit Ownership**: Clear ownership semantics for all data

## Examples

See the `example_basic.c` and `example_mesh.c` files for complete examples of:
- Loading USD files
- Traversing the scene hierarchy
- Extracting mesh data
- Accessing materials and shaders
- Querying animation data

## Design Rationale

This API was designed with the following goals:

1. **C99 Compliance**: Works with any C99 compiler, no C++ required
2. **Minimal Dependencies**: Only standard C library required
3. **ABI Stability**: Opaque handles allow implementation changes
4. **Clear Ownership**: Explicit memory management patterns
5. **Gradual Adoption**: Start with basic functions, add as needed
6. **Future Proof**: Extensible without breaking existing code

## Implementation Status

Currently implemented:
- ✅ Core loading and traversal (Tier 1)
- ✅ Property and value access (Tier 2)
- ✅ Basic mesh data extraction (Tier 3)
- ✅ Transform and material queries (Tier 3)

Not yet implemented:
- ⚠️ Full composition support
- ⚠️ Writing USD files
- ⚠️ Complete animation API
- ⚠️ Layer manipulation
- ⚠️ Custom schemas

## Building from Source

### Requirements

- C99 compiler (gcc, clang, msvc)
- C++14 compiler (for implementation only)
- CMake 3.10+ or GNU Make
- TinyUSDZ source code (in parent directory)

### Platform Notes

**Linux/macOS:**
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**Windows:**
```bash
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Release
```

**Cross-compilation:**
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake
```

## Integration

### With pkg-config
```bash
gcc myapp.c `pkg-config --cflags --libs tinyusdz_c`
```

### Manual compilation
```bash
gcc -I/usr/local/include/tinyusdz myapp.c -L/usr/local/lib -ltinyusdz_c -lm
```

### Python via ctypes
```python
import ctypes
lib = ctypes.CDLL("libtinyusdz_c.so")
lib.tusdz_init()
# ... use the API
```

## Testing

Run the test suite:
```bash
make test
# or
ctest
```

Memory leak checking:
```bash
valgrind --leak-check=full ./example_basic model.usd
```

Thread safety testing:
```bash
helgrind ./example_basic model.usd
```

## License

Same as TinyUSDZ - MIT License

## Contributing

Contributions welcome! Please ensure:
- C99 compliance (no C11/C++ in headers)
- Clear memory ownership
- Thread safety for read operations
- Comprehensive error handling
- Documentation for all public APIs

## Future Work

- WebAssembly support
- Python bindings generation
- Async/streaming API
- Custom prim type registration
- Performance optimizations