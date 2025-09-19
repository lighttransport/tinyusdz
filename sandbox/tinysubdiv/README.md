# TinySubdiv

A portable, efficient C++14 implementation of subdivision surfaces with SIMD optimizations.

## Features

- **Portable C++14 implementation** - No external dependencies
- **Efficient memory management** - ChunkedTypedArray for cache-friendly data layout
- **SIMD optimizations** - Support for SSE, AVX, AVX512, and ARM NEON
- **Multiple subdivision schemes**:
  - **Catmull-Clark** - For quad and mixed topology meshes
  - **Loop** - For triangle meshes
- **Crease and corner support** - Sharp features preservation
- **Mixed topology support** - Handles triangles and quads
- **Boundary handling** - Proper boundary edge and vertex treatment

## Architecture

### ChunkedTypedArray

A custom container that stores data in fixed-size chunks (default 4KB) to:
- Improve cache locality
- Reduce memory fragmentation
- Enable efficient SIMD processing on chunk boundaries
- Avoid large contiguous allocations

### Subdivision Algorithms

#### Catmull-Clark (for quads and mixed topology)
1. **Face Points** - Compute centroids of each face
2. **Edge Points** - Compute points on edges using face and vertex data
3. **Vertex Points** - Update original vertices based on valence
4. **Face Generation** - Create new quad faces from the subdivided points

#### Loop (for triangles only)
1. **Edge Points** - Compute new vertices on edges using 3/8 - 1/8 rule
2. **Vertex Points** - Update original vertices using Warren's beta weights
3. **Face Generation** - Split each triangle into 4 triangles

### SIMD Optimizations

SIMD versions are provided for critical operations:
- Face point computation (parallel centroid calculation)
- Edge point computation (vectorized averaging)
- Vertex updates (bulk operations on vertex data)

## Building

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### Build Options

- `TINYSUBDIV_USE_SSE` - Enable SSE 4.1 optimizations
- `TINYSUBDIV_USE_AVX` - Enable AVX2 optimizations
- `TINYSUBDIV_USE_AVX512` - Enable AVX512 optimizations
- `TINYSUBDIV_USE_NEON` - Enable ARM NEON optimizations
- `TINYSUBDIV_BUILD_EXAMPLES` - Build example programs
- `TINYSUBDIV_BUILD_TESTS` - Build unit tests

### Example with SIMD

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DTINYSUBDIV_USE_SSE=ON
```

## Usage

### Catmull-Clark Subdivision (Quads/Mixed)

```cpp
#include "tinysubdiv.hh"

using namespace tinysubdiv;

// Create a cube
float vertices[] = { /* ... */ };
uint32_t indices[] = { /* ... */ };
uint32_t face_counts[] = {4, 4, 4, 4, 4, 4}; // 6 quad faces

// Setup mesh
SubdivisionMesh mesh;
mesh.set_vertices(vertices, num_vertices);
mesh.set_faces(indices, face_counts, num_faces);

// Optional: Add creases
uint32_t crease_indices[] = {0, 1, 1, 2}; // Two edges
float crease_sharpness[] = {10.0f, 10.0f};
mesh.set_creases(crease_indices, crease_sharpness, 2);

// Subdivide using Catmull-Clark
mesh.subdivide_catmull_clark(2); // 2 levels

// Get triangulated output
std::vector<float> out_vertices;
std::vector<uint32_t> out_indices;
mesh.get_triangulated(out_vertices, out_indices);
```

### Loop Subdivision (Triangles Only)

```cpp
// Create a tetrahedron
float vertices[] = { /* ... */ };
uint32_t indices[] = { /* ... */ };
uint32_t face_counts[] = {3, 3, 3, 3}; // 4 triangle faces

SubdivisionMesh mesh;
mesh.set_vertices(vertices, num_vertices);
mesh.set_faces(indices, face_counts, num_faces);

// Subdivide using Loop
mesh.subdivide_loop(2); // 2 levels

// Get output
std::vector<float> out_vertices;
std::vector<uint32_t> out_indices;
mesh.get_triangulated(out_vertices, out_indices);
```

## Performance

The implementation is optimized for:

1. **Memory efficiency** - ChunkedTypedArray reduces allocation overhead
2. **Cache locality** - Data is processed in cache-friendly chunks
3. **SIMD vectorization** - Critical loops use SIMD instructions when available
4. **Minimal allocations** - Reuses memory where possible

Benchmark results (example on Intel i7):
- 20x20 grid (400 quads) → 1 subdivision level: ~2ms
- 20x20 grid (400 quads) → 2 subdivision levels: ~8ms
- Cube → 3 subdivision levels: ~0.5ms

## Implementation Notes

The design is inspired by Intel Embree's efficient subdivision implementation:
- Lazy topology building
- Edge-centric data structures
- SIMD-friendly memory layout
- Efficient crease handling

## Limitations

- No adaptive subdivision support
- Loop subdivision requires triangle meshes only
- Catmull-Clark converts triangles to quads

## License

This implementation is provided as part of the TinyUSDZ project.