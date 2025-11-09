# Curves to Mesh Tessellation

This module provides efficient algorithms for converting USD Curves primitives (BasisCurves and NurbsCurves) to triangle meshes suitable for real-time rendering.

## Features

### Tessellation Modes

1. **LineSegments** - Simplest mode, converts curves to line segments
   - Fastest, minimal memory
   - No volume, creates degenerate triangles
   - Best for: Debugging, wireframe visualization

2. **Cylinder** - Creates cylindrical tubes around curves (DEFAULT)
   - Full 3D volume with configurable radial subdivisions
   - Supports varying widths along curve
   - Best for: Hair, fur, cables, general purpose

3. **Ribbon** - Creates flat ribbons oriented by normals
   - Uses curve normals for orientation
   - Rectangular cross-section
   - Best for: Stylized rendering, trails, effects

4. **Cards** - Camera-facing billboards (NOT YET IMPLEMENTED)
   - Optimized for many thin curves
   - Requires view direction
   - Best for: Real-time hair/fur with thousands of strands

### Supported Curve Types

- **BasisCurves**
  - Linear interpolation
  - Cubic Bezier
  - Cubic B-spline
  - Cubic Catmull-Rom

- **NurbsCurves** (PLANNED)
  - Arbitrary order
  - Rational NURBS with weights

### Algorithm Details

#### Cylindrical Tessellation (Robust Method)

Based on research from SIGGRAPH 2024 "Real-Time Hair Rendering with Hair Meshes":

**Key Features:**
- Parallel transport frame computation for smooth orientation
- Configurable radial subdivisions (8-32 typical)
- Adaptive tessellation based on curvature
- Proper normal generation for lighting
- UV coordinates (U along curve, V around circumference)

**Process:**
1. Evaluate curve basis function to get dense spine points
2. Compute tangent vectors at each spine point
3. Build orthonormal frame (tangent, normal, binormal)
4. Generate ring of vertices around each spine point
5. Connect rings with quad faces

**Performance:**
- Time Complexity: O(n * r * s) where:
  - n = number of curves
  - r = radial subdivisions
  - s = segments per curve
- Memory: ~(r * s * 3 * 4) bytes per curve for positions

#### Adaptive Tessellation

Implements curvature-based subdivision from "Adaptive Tessellation of NURBS Surfaces":

**Algorithm:**
```
For each curve segment:
  1. Compute curvature κ = |d²p/ds²| / |dp/ds|³
  2. Calculate required segments: n = ceil(κ * length / tolerance)
  3. Clamp to [min_segments, max_segments]
  4. Subdivide uniformly within segment
```

**Benefits:**
- Fewer triangles in straight sections
- Higher quality in curved sections
- Automatic LOD control

## Usage

### Basic Example

```cpp
#include "tydra/curves-to-mesh.hh"

using namespace tinyusdz;
using namespace tinyusdz::tydra;

// Load USD stage
Stage stage;
LoadUSDFromFile("curves.usda", &stage);

// Get BasisCurves prim
const auto *prim = stage.GetPrimAtPath(Path("/Hair/Strands"));
const auto *curves = prim->as<GeomBasisCurves>();

// Configure tessellation
CurveTessellationOptions options;
options.mode = CurveTessellationMode::Cylinder;
options.radial_subdivisions = 8;
options.segments_per_span = 4;
options.adaptive = true;
options.max_edge_length = 0.1f;

// Convert to mesh
std::vector<value::float3> points;
std::vector<int> faceVertexCounts;
std::vector<int> faceVertexIndices;
std::vector<value::float3> normals;
std::vector<value::float2> uvs;

bool success = BasisCurvesToMesh(
    *curves, options,
    points, faceVertexCounts, faceVertexIndices,
    normals, uvs
);
```

### Advanced: Ribbon Mode with Normals

```cpp
// Ribbons require normals attribute
CurveTessellationOptions options;
options.mode = CurveTessellationMode::Ribbon;
options.radial_subdivisions = 2;  // Ribbons only need 2 for flat geometry
options.generate_uvs = true;

BasisCurvesToMesh(*curves, options, /* ... */);
```

### Performance Tuning

```cpp
// High quality (many triangles)
options.radial_subdivisions = 16;
options.segments_per_span = 8;
options.adaptive = true;
options.max_edge_length = 0.05f;

// Performance (fewer triangles)
options.radial_subdivisions = 6;
options.segments_per_span = 2;
options.adaptive = false;
options.max_edge_length = 0.2f;

// Memory constrained
options.max_segments = 64;  // Limit total segments per curve
```

## Implementation Status

### ✅ Implemented
- Cylindrical tessellation for linear curves
- Basic frame computation (fixed up vector)
- Width variation support
- UV generation
- Normal generation
- Configuration API

### 🚧 In Progress
- Proper basis function evaluation (Bezier, B-spline, Catmull-Rom)
- Parallel transport frame (minimum rotation)
- Adaptive tessellation based on curvature
- Ribbon mode with normal orientation

### 📋 Planned
- NurbsCurves support with arbitrary order
- Cards/billboard mode for performance
- Mesh shader output format (for GPU)
- Strand variation (procedural jitter)
- Level-of-detail system
- Texture coordinate modes (polar, planar, spherical)

## Performance Characteristics

### Triangle Count Estimation

For a single curve with N control points:
- **Linear**: (N-1) * radial_subdivisions * 2 triangles
- **Cubic**: (N-1) * segments_per_span * radial_subdivisions * 2 triangles

Example: Hair strand with 10 CVs, 8 radial subdivisions, 4 segments per span:
- Linear: 9 * 8 * 2 = 144 triangles
- Cubic: 9 * 4 * 8 * 2 = 576 triangles

### Recommended Settings by Use Case

| Use Case | Radial | Segments/Span | Adaptive | Notes |
|----------|--------|---------------|----------|-------|
| Preview | 4 | 1 | No | Fast, low quality |
| Realtime | 6-8 | 2-3 | Yes | Balanced |
| Film/Offline | 12-16 | 6-8 | Yes | High quality |
| Ribbons | 2 | 2-4 | No | Flat geometry |
| Cables | 8-12 | 4 | Yes | Smooth tubes |

## References

- **SIGGRAPH 2024**: "Real-Time Hair Rendering with Hair Meshes"
  Mesh shader approaches for hair geometry generation

- **Adaptive Tessellation of NURBS Surfaces** (CAGD 2008)
  Curvature-based subdivision algorithms

- **OpenUSD BasisCurves Schema**
  https://openusd.org/dev/api/class_usd_geom_basis_curves.html

- **Parallel Transport Frame**
  Minimum rotation frames for smooth curve orientation

## Future Optimizations

1. **GPU Acceleration**
   - Mesh shader implementation for on-the-fly generation
   - Compute shader pre-pass for curvature analysis

2. **Instancing**
   - Share geometry between similar curves
   - Template-based strand generation

3. **Compression**
   - Quantized vertex positions
   - Shared index buffers

4. **Streaming**
   - Progressive refinement
   - View-frustum culling
