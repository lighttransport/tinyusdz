# Subdivision Surface Library

A standalone, dependency-free C++14 subdivision surface library designed for USD (Universal Scene Description) workflows.

## Features

- **Catmull-Clark Subdivision**: For quad-dominant meshes with smooth surfaces
- **Loop Subdivision**: For triangular meshes with smooth surfaces
- **Bilinear Subdivision**: For any mesh with linear interpolation (no smoothing)
- **USD Primvar Interpolation**: Support for vertex, varying, uniform, and faceVarying attributes
- **Security-Focused**: Built-in validation and memory limits
- **Textbook Implementation**: Clear, readable code based on classic algorithms
- **Zero Dependencies**: C++14 standard library only

## Algorithm References

### Catmull-Clark Subdivision

Based on the classic 1978 algorithm by Catmull and Clark:

1. **Face Points**: Average of all vertices in each face
2. **Edge Points**: Average of edge endpoints and adjacent face points
3. **Vertex Points**: Weighted formula: `(Q/n) + (2R/n) + (S(n-3)/n)`
   - Q = average of surrounding face points
   - R = average of surrounding edge midpoints
   - S = original vertex position
   - n = vertex valence

References:
- OpenSubdiv: https://graphics.pixar.com/opensubdiv/
- Wikipedia: https://en.wikipedia.org/wiki/Catmull-Clark_subdivision_surface

### Loop Subdivision

Based on Charles Loop's 1987 algorithm for triangular meshes:

1. **Edge Vertices (Odd)**:
   - Interior: `(3/8)(A+B) + (1/8)(C+D)` where A,B are endpoints, C,D are opposite vertices
   - Boundary: Simple midpoint `(A+B)/2`

2. **Vertex Points (Even)**:
   - Interior: `(1 - nβ)S + β·Σ(neighbors)` where β = 3/(8n) for n>3
   - Boundary: `0.5·S + 0.5·average(boundary_neighbors)`

References:
- Loop, Charles T. (1987). "Smooth Subdivision Surfaces Based on Triangles"
- Wikipedia: https://en.wikipedia.org/wiki/Loop_subdivision_surface

### Bilinear Subdivision

The simplest subdivision scheme using only linear interpolation:

1. **Original Vertices**: Kept at their original positions (no smoothing)
2. **Edge Midpoints**: `(v0 + v1) / 2` - simple linear interpolation
3. **Face Centers**: Average of all face vertices
4. **Topology**: Each face with n vertices produces n sub-faces

Key properties:
- No smoothing applied - preserves sharp features
- Original vertices remain unchanged
- Suitable for simple tessellation or maintaining hard edges
- Works with any polygon type (triangles, quads, n-gons)
- Linear complexity

Use cases:
- Tessellating meshes without changing shape
- Maintaining sharp features in mechanical models
- Displacement mapping preparation
- Testing/debugging (simpler than smooth schemes)

## USD Primvar Interpolation Types

The library supports all USD primvar interpolation modes:

- **Constant**: Per-object, not subdivided
- **Uniform**: Per-face, replicated to new faces
- **Vertex**: Per-vertex with smooth interpolation
- **Varying**: Per-vertex (alias for vertex in this implementation)
- **FaceVarying**: Per-face-vertex for discontinuous attributes (partial implementation)

## Building

```bash
mkdir build
cd build
cmake ..
make
./test_subdiv
```

## Usage Example

### Catmull-Clark Subdivision

```cpp
#include "subdivision.hh"

using namespace tinyusdz::subdiv;

// Create input mesh (cube)
std::vector<float> points = {
  -1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1,
  -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1
};

std::vector<uint32_t> face_vertex_counts = {4, 4, 4, 4, 4, 4};
std::vector<uint32_t> face_vertex_indices = {
  0, 1, 2, 3,  4, 5, 6, 7,  0, 4, 7, 3,
  1, 5, 6, 2,  0, 1, 5, 4,  3, 2, 6, 7
};

// Convert to half-edge mesh
HalfEdgeMesh input_mesh;
ConvertToHalfEdgeMesh(face_vertex_counts, face_vertex_indices, points, input_mesh);

// Subdivide
CatmullClarkSubdivider subdivider;
HalfEdgeMesh output_mesh;
SubdivResult result = subdivider.Subdivide(input_mesh, output_mesh, 2);  // 2 levels

if (result.success) {
  std::cout << "Success! " << output_mesh.GetNumVertices() << " vertices\n";
}
```

### Loop Subdivision

```cpp
// Triangle mesh (tetrahedron)
std::vector<float> points = {
  0, 0, 0, 1, 0, 0, 0.5, 1, 0, 0.5, 0.5, 1
};

std::vector<uint32_t> face_vertex_counts = {3, 3, 3, 3};
std::vector<uint32_t> face_vertex_indices = {
  0, 1, 2,  0, 1, 3,  1, 2, 3,  2, 0, 3
};

HalfEdgeMesh input_mesh;
ConvertToHalfEdgeMesh(face_vertex_counts, face_vertex_indices, points, input_mesh);

LoopSubdivider subdivider;
HalfEdgeMesh output_mesh;
SubdivResult result = subdivider.Subdivide(input_mesh, output_mesh, 1);
```

### Bilinear Subdivision

```cpp
// Works with any polygon type - no smoothing applied
std::vector<float> points = {
  0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0
};

std::vector<uint32_t> face_vertex_counts = {4};
std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 3};

HalfEdgeMesh input_mesh;
ConvertToHalfEdgeMesh(face_vertex_counts, face_vertex_indices, points, input_mesh);

BilinearSubdivider subdivider;
HalfEdgeMesh output_mesh;
SubdivResult result = subdivider.Subdivide(input_mesh, output_mesh, 1);

// Original vertices remain unchanged (no smoothing)
// Each quad becomes 4 smaller quads
```

### Primvar Interpolation

```cpp
#include "primvar-interpolation.hh"

// Vertex colors
std::vector<Vec3> colors = {
  Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(1, 1, 0)
};

PrimvarData<Vec3> input_colors(colors, InterpolationType::Vertex);

// Subdivide mesh and colors
CatmullClarkSubdivider subdivider;
HalfEdgeMesh output_mesh;
subdivider.Subdivide(input_mesh, output_mesh, 1);

PrimvarData<Vec3> output_colors;
subdivider.SubdividePrimvar(input_colors, input_mesh, output_mesh, output_colors);
```

## Boundary Interpolation Modes

```cpp
subdivider.SetBoundaryInterpolation(BoundaryInterpolation::EdgeAndCorner);
```

- **None**: Boundaries treated like interior vertices
- **EdgeOnly**: Sharpen edges, smooth corners
- **EdgeAndCorner**: Sharpen both edges and corners (default)

## Security Features

- Memory limits: Maximum 10M vertices and 10M faces
- Maximum subdivision levels: 10 (prevents exponential growth)
- Input validation: Checks for invalid indices, degenerate faces
- Bounds checking: All array accesses validated

## Implementation Notes

- **Textbook Approach**: Code prioritizes clarity and correctness over performance
- **Security-First**: Extensive validation and limits to prevent malformed input attacks
- **USD Compatible**: Data structures match USD conventions (faceVertexCounts, faceVertexIndices)
- **No Dependencies**: Pure C++14, no external libraries required

## Limitations

- FaceVarying primvar interpolation is partially implemented
- Not optimized for speed (suitable for offline processing)
- No GPU acceleration
- No adaptive subdivision

## Future Enhancements

- Complete FaceVarying interpolation
- Semi-sharp creases support
- Hierarchical edits
- Adaptive refinement
- SIMD optimizations

## License

MIT License - see parent TinyUSDZ project for details

## References

1. Catmull, E., & Clark, J. (1978). "Recursively generated B-spline surfaces on arbitrary topological meshes"
2. Loop, C. T. (1987). "Smooth Subdivision Surfaces Based on Triangles"
3. Pixar OpenSubdiv: https://graphics.pixar.com/opensubdiv/
4. Intel Embree Subdivision Surfaces: https://www.embree.org/
5. USD Documentation: https://graphics.pixar.com/usd/docs/
