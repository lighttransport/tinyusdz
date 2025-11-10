# TinyUSDZ C99 API - Quick Start Guide

Get up and running with the TinyUSDZ C API in 5 minutes.

## Installation

### Linux/macOS

```bash
cd sandbox/new-c-api
mkdir build && cd build
cmake ..
make
sudo make install
```

### Windows

```bash
cd sandbox\new-c-api
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Release
cmake --install .
```

## Basic C Program

Create `hello_usd.c`:

```c
#include <tinyusdz_c.h>
#include <stdio.h>

int main() {
    tusdz_init();

    // Load a USD file
    tusdz_stage stage = NULL;
    char error[256];

    tusdz_result result = tusdz_load_from_file(
        "model.usd", NULL, &stage, error, sizeof(error)
    );

    if (result != TUSDZ_SUCCESS) {
        fprintf(stderr, "Failed to load: %s\n", error);
        return 1;
    }

    // Get root prim
    tusdz_prim root = tusdz_stage_get_root_prim(stage);
    printf("Root prim: %s\n", tusdz_prim_get_name(root));

    // Traverse children
    size_t child_count = tusdz_prim_get_child_count(root);
    printf("Children: %zu\n", child_count);

    for (size_t i = 0; i < child_count; i++) {
        tusdz_prim child = tusdz_prim_get_child_at(root, i);
        printf("  - %s [%s]\n",
               tusdz_prim_get_name(child),
               tusdz_prim_get_type_name(child));
    }

    // Cleanup
    tusdz_stage_free(stage);
    tusdz_shutdown();

    return 0;
}
```

### Compile and Run

```bash
# With pkg-config
gcc hello_usd.c `pkg-config --cflags --libs tinyusdz_c` -o hello_usd

# Or manual
gcc hello_usd.c -I/usr/local/include/tinyusdz \
    -L/usr/local/lib -ltinyusdz_c -lm -lstdc++ -o hello_usd

# Run
./hello_usd model.usd
```

## Python Quick Start

Create `hello_usd.py`:

```python
#!/usr/bin/env python3

import tinyusdz

# Initialize
tinyusdz.init()

# Load USD file
stage = tinyusdz.load_from_file("model.usd")

# Get root prim
root = stage.root_prim
print(f"Root prim: {root.name}")

# Traverse children
print(f"Children: {root.child_count}")

for child in root.get_children():
    print(f"  - {child.name} [{child.type_name}]")

tinyusdz.shutdown()
```

### Run

```bash
python3 hello_usd.py model.usd
```

## Common Tasks

### Load and Print Hierarchy

**C:**
```c
tusdz_stage stage = NULL;
tusdz_load_from_file("model.usd", NULL, &stage, NULL, 0);
tusdz_stage_print_hierarchy(stage, -1);  // -1 = unlimited depth
tusdz_stage_free(stage);
```

**Python:**
```python
stage = tinyusdz.load_from_file("model.usd")
root = stage.root_prim
root.print_hierarchy()
```

### Extract Mesh Data

**C:**
```c
if (tusdz_prim_is_type(prim, TUSDZ_PRIM_MESH)) {
    const float* points;
    size_t point_count;

    tusdz_mesh_get_points(prim, &points, &point_count);

    size_t num_vertices = point_count / 3;
    for (size_t i = 0; i < num_vertices; i++) {
        printf("Point %zu: (%f, %f, %f)\n",
               i, points[i*3], points[i*3+1], points[i*3+2]);
    }
}
```

**Python:**
```python
if prim.is_mesh():
    points, count = tusdz_mesh_get_points(prim)
    num_vertices = count // 3
    for i in range(num_vertices):
        print(f"Point {i}: ({points[i*3]}, {points[i*3+1]}, {points[i*3+2]})")
```

### Find Prim by Path

**C:**
```c
tusdz_prim prim = tusdz_stage_get_prim_at_path(stage, "/World/Geo/Mesh");
if (prim) {
    printf("Found: %s\n", tusdz_prim_get_name(prim));
}
```

**Python:**
```python
prim = stage.get_prim_at_path("/World/Geo/Mesh")
if prim:
    print(f"Found: {prim.name}")
```

### Access Properties

**C:**
```c
size_t prop_count = tusdz_prim_get_property_count(prim);
for (size_t i = 0; i < prop_count; i++) {
    const char* name = tusdz_prim_get_property_name_at(prim, i);
    tusdz_value value = tusdz_prim_get_property(prim, name);

    if (value) {
        printf("%s: %s\n", name,
               tusdz_value_type_to_string(
                   tusdz_value_get_type(value)));

        tusdz_value_free(value);
    }
}
```

**Python:**
```python
for i in range(prim.property_count):
    name = prim.get_property_name(i)
    prop = prim.get_property(name)
    if prop:
        print(f"{name}: {prop.type_name}")
```

### Get Transform Matrix

**C:**
```c
if (tusdz_prim_is_type(prim, TUSDZ_PRIM_XFORM)) {
    double matrix[16];
    tusdz_xform_get_local_matrix(prim, 0.0, matrix);

    // matrix is in column-major order
    printf("Transform matrix:\n");
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            printf("%f ", matrix[col * 4 + row]);
        }
        printf("\n");
    }
}
```

### Check for Animation

**C:**
```c
if (tusdz_stage_has_animation(stage)) {
    double start, end, fps;
    tusdz_stage_get_time_range(stage, &start, &end, &fps);
    printf("Animation: %.1f to %.1f @ %.1f fps\n", start, end, fps);
}
```

**Python:**
```python
if stage.has_animation:
    start, end, fps = stage.get_time_range()
    print(f"Animation: {start} to {end} @ {fps} fps")
```

### Handle Errors

**C:**
```c
char error[1024];
tusdz_result result = tusdz_load_from_file(
    filepath, NULL, &stage, error, sizeof(error)
);

if (result != TUSDZ_SUCCESS) {
    fprintf(stderr, "Error (%d): %s\n",
            result, tusdz_result_to_string(result));
    fprintf(stderr, "Details: %s\n", error);
}
```

**Python:**
```python
try:
    stage = tinyusdz.load_from_file("model.usd")
except RuntimeError as e:
    print(f"Error: {e}")
```

## API Documentation

For complete API reference, see:
- `API_REFERENCE.md` - Complete function reference
- `README.md` - Features and architecture
- `DESIGN.md` - Design philosophy

## Examples

Full working examples are provided:
- `example_basic.c` - Basic scene traversal
- `example_mesh.c` - Mesh data extraction

Compile and run:
```bash
# In build directory
make examples
./example_basic ../../models/simple_mesh.usda
./example_mesh ../../models/simple_mesh.usda
```

## Testing

Run the test suites:

```bash
# C tests
./test_c_api

# Python tests
python3 test_python_api.py
```

## Tips

1. **Always initialize and shutdown**
   - Call `tusdz_init()` before use
   - Call `tusdz_shutdown()` when done

2. **Check return codes**
   - Most functions return error codes
   - Use `tusdz_result_to_string()` for error messages

3. **Understand memory ownership**
   - Pointers from `get_*` functions are borrowed
   - Use `tusdz_*_free()` for allocated values
   - Stages must be freed with `tusdz_stage_free()`

4. **Use appropriate data types**
   - Check value type with `tusdz_value_get_type()`
   - Use corresponding `get_*` function for type

5. **Handle NULL safely**
   - Check function returns for NULL
   - Use NULL for optional parameters

## Troubleshooting

### "Cannot find libtinyusdz_c"
```bash
# Make sure to install:
cd build && sudo make install

# Or set library path:
export LD_LIBRARY_PATH=./build:$LD_LIBRARY_PATH
```

### "Cannot import tinyusdz"
```bash
# Python needs to find the library:
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
python3 test_python_api.py
```

### Import Error with pkg-config
```bash
# Make sure pkg-config can find the file:
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
pkg-config --cflags --libs tinyusdz_c
```

## Next Steps

1. Read `API_REFERENCE.md` for complete documentation
2. Study `example_basic.c` and `example_mesh.c`
3. Run tests to verify installation
4. Build your own application

## Getting Help

- Check `README.md` for features overview
- See `DESIGN.md` for architecture details
- Review `API_REFERENCE.md` for function details
- Look at examples for usage patterns
- Run tests for verification

## Platform-Specific Notes

### Linux
- Works on glibc and musl
- Requires g++/clang for building
- Use `sudo make install` for system-wide installation

### macOS
- Requires Command Line Tools
- Homebrew can provide dependencies
- Use `sudo make install` for system-wide installation

### Windows
- Requires Visual Studio 2015 or later
- Use CMake generator for your toolchain
- Installation differs from Unix platforms

## Performance Tips

1. **Batch operations**: Load once, process multiple times
2. **Minimize allocations**: Reuse buffers where possible
3. **Use structure_only flag**: Skip heavy data if just traversing
4. **Cache results**: Avoid redundant lookups
5. **Profile memory**: Use `tusdz_get_memory_stats()`

## License

Same as TinyUSDZ - MIT License

---

Ready to use TinyUSDZ! Start with the examples and build from there.

For advanced features, see the full API reference and design documentation.