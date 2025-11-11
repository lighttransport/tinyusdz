# Build Configuration for Trimmed NURBS Support

Instructions for building TinyUSDZ with Trimmed NURBS surface tessellation support.

## Files to Include in CMakeLists.txt

Add the following to your main CMakeLists.txt:

### Source Files
```cmake
# Core Trimmed NURBS implementation
list(APPEND TINYUSDZ_SOURCES
  src/tydra/trimmed-nurbs.cc
)

# Header files (automatically included)
# src/tydra/trimmed-nurbs.hh
# src/tydra/trimmed-nurbs-integration.hh
```

### Test Executable
```cmake
if(TINYUSDZ_BUILD_TESTS)
  add_executable(test_trimmed_nurbs
    test_trimmed_nurbs.cc
  )

  target_include_directories(test_trimmed_nurbs
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )

  target_link_libraries(test_trimmed_nurbs
    PRIVATE
    tinyusdz
  )

  add_test(
    NAME test_trimmed_nurbs
    COMMAND test_trimmed_nurbs
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
endif()
```

## Complete Build Example

```bash
# Create build directory
mkdir build
cd build

# Configure with tests enabled
cmake .. -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_WITH_TYDRA=ON

# Build
make -j8

# Run tests
ctest --output-on-failure

# Or run directly
./test_trimmed_nurbs
```

## Compiler Requirements

### Minimum Requirements
- C++14 compiler (gcc 5+, clang 3.5+, MSVC 2015+)
- Standard library with `<cmath>`, `<vector>`, `<algorithm>`

### Recommended for Best Performance
- C++17 or later
- Modern CPU with good floating-point support
- Optional: SIMD support for vectorized operations

### Known Compatible Compilers
- GCC 9+
- Clang 10+
- MSVC 2019+
- Apple Clang 12+

## Optional Optimizations

### Enable Optimized Builds
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### Add SIMD Support
If available, add to CMakeLists.txt:
```cmake
if(NOT MSVC)
  target_compile_options(tinyusdz PRIVATE -march=native)
else()
  target_compile_options(tinyusdz PRIVATE /arch:AVX2)
endif()
```

### Parallel Tessellation (Future)
When GPU support is added:
```cmake
# Add CUDA support
find_package(CUDA REQUIRED)
target_compile_definitions(tinyusdz PRIVATE WITH_TRIMMED_NURBS_GPU)
```

## Verification

After building, verify trimmed NURBS support:

```bash
# Run test
./test_trimmed_nurbs

# Expected output:
# ===================================================
# Trimmed NURBS Surface Tessellation Test Suite
# ===================================================
# === Surface Evaluation Test ===
# Control points: 16
# Degree: U=3 V=3
# ...
# === Tessellation Test ===
# Tessellating NURBS surface...
# Tessellation complete!
#   Vertices: [number]
#   Normals: [number]
#   UVs: [number]
#   Faces: [number]
#   Indices: [number]
# ...
# All tests completed successfully!
```

## Integration with Existing CMake

### In your CMakeLists.txt:

```cmake
# After other source file declarations

# Trimmed NURBS support (requires C++14)
if(CMAKE_CXX_STANDARD GREATER_EQUAL 14)
  list(APPEND TINYUSDZ_SOURCES
    src/tydra/trimmed-nurbs.cc
  )

  set(TINYUSDZ_TRIMMED_NURBS_SUPPORT TRUE)
  message(STATUS "Trimmed NURBS support: ENABLED")
else()
  set(TINYUSDZ_TRIMMED_NURBS_SUPPORT FALSE)
  message(STATUS "Trimmed NURBS support: DISABLED (requires C++14+)")
endif()

# Mark headers as interface for header-only components
target_sources(tinyusdz PRIVATE
  ${TINYUSDZ_SOURCES}
)

# Public headers
target_include_directories(tinyusdz PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
```

### Feature Detection

Use this in code to conditionally enable features:

```cpp
#ifdef TINYUSDZ_TRIMMED_NURBS_SUPPORT
  // Trimmed NURBS code
#endif
```

## Troubleshooting

### Compiler Errors

**Error: "BSplineBasis not defined"**
- Ensure `trimmed-nurbs.hh` is included
- Check include path is correct

**Error: "undefined reference to TrimmedNurbsTessellator"**
- Add `src/tydra/trimmed-nurbs.cc` to CMakeLists.txt
- Rebuild clean: `rm -rf build && mkdir build && cd build && cmake .. && make`

### Runtime Issues

**Test crashes in EvaluateNurbsSurface**
- Verify control point count matches num_u × num_v
- Check knot vectors are properly sized
- Validate surface.Validate() returns true

**Tessellation produces empty mesh**
- Check surface is within parameter domain [0,1]
- Verify trim curves are properly formed
- Enable more verbose output for debugging

## Performance Tuning

### For Small Surfaces (< 1000 triangles)
```cpp
options.adaptive = false;
options.min_u_divisions = 4;
options.min_v_divisions = 4;
```

### For Large Surfaces (> 10000 triangles)
```cpp
options.adaptive = true;
options.screen_space_error = 2.0f;  // Less strict
options.max_edge_length = 0.2f;
```

### Profile Your Code
```bash
# With profiling
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
perf record ./test_trimmed_nurbs
perf report
```

## Integration Checklist

- [ ] Add `trimmed-nurbs.cc` to source list
- [ ] Include headers in required files
- [ ] Build and verify test passes
- [ ] Check no compiler warnings
- [ ] Validate with your NURBS data
- [ ] Profile for performance targets
- [ ] Test with trim curves if needed

## Next Steps

Once build is verified:

1. **Load USD NURBS** - Use existing USD loader
2. **Convert to Internal Format** - Use `ConvertGeomNurbsSurfaceToTrimmed()`
3. **Tessellate** - Call `TessellateGeomNurbsSurface()`
4. **Render** - Use resulting triangle mesh

See TRIMMED_NURBS.md for detailed usage examples.
