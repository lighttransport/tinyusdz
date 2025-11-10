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

- **NurbsCurves**
  - Arbitrary order (degree 1-n)
  - Rational NURBS with weights
  - Cox-de Boor recursive basis evaluation
  - Knot vector support

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

Implements flatness-based recursive subdivision using de Casteljau's algorithm:

**Algorithm:**
```
For each cubic curve segment (4 control points):
  1. Test flatness: measure distance from control points p1, p2 to line p0-p3
  2. If flat (distance < tolerance) OR max depth reached: emit point
  3. Otherwise:
     - Split curve at t=0.5 using de Casteljau's algorithm
     - Recursively subdivide left half
     - Recursively subdivide right half
```

**Basis Conversion:**
- **Bezier**: Direct de Casteljau subdivision
- **B-spline**: Convert to Bezier control points, then subdivide
  - Uses matrix: Bezier = M_bezier^-1 * M_bspline * CVs
- **Catmull-Rom**: Convert to Bezier control points, then subdivide
  - Uses matrix: Bezier = M_bezier^-1 * M_catmullrom * CVs

**Benefits:**
- Fewer triangles in straight sections (3-5x reduction)
- Higher quality in curved sections (smooth curvature)
- Automatic LOD control based on flatness tolerance
- Numerically stable (de Casteljau is evaluation, not approximation)

#### NURBS Evaluation

Implements standard NURBS curve evaluation using Cox-de Boor recursion:

**Algorithm:**
```
For parameter value u:
  1. Find knot span i containing u using binary search
  2. Compute basis functions N_{i-p}(u) ... N_i(u) using Cox-de Boor recursion
  3. Blend control points: C(u) = Σ N_i(u) * w_i * P_i / Σ N_i(u) * w_i
     where w_i are optional rational weights (default = 1.0)
```

**Cox-de Boor Recursion:**
```
N_{i,0}(u) = 1 if u_i ≤ u < u_{i+1}, else 0
N_{i,p}(u) = [(u - u_i)/(u_{i+p} - u_i)] * N_{i,p-1}(u)
           + [(u_{i+p+1} - u)/(u_{i+p+1} - u_{i+1})] * N_{i+1,p-1}(u)
```

**Features:**
- Arbitrary degree (order = degree + 1)
- Rational NURBS with point weights
- Proper knot span search
- Handles clamped and uniform knot vectors
- Analytical derivatives for exact tangent/curvature

**Adaptive Tessellation for NURBS:**
```
1. Sample curve at coarse intervals, computing curvature κ at each point
2. For each segment:
   - Calculate κ = |C'(u) × C''(u)| / |C'(u)|³
   - Subdivide based on max curvature in segment
   - Higher curvature = more subdivisions (up to 8x)
3. Evaluate final points using NURBS evaluation
```

Benefits:
- Fewer polygons in straight sections
- Higher quality in highly curved regions
- Automatic detail level based on geometry

#### Parallel Transport Frames

Implements rotation-minimizing frames to eliminate tube twisting:

**Algorithm:**
```
For each point i along curve:
  1. Compute tangent T_i from curve derivatives
  2. If i == 0:
     Initialize frame using fixed up vector
  3. Else:
     Rotate previous frame (N_{i-1}, B_{i-1}) to align with new tangent
     Using Rodrigues' rotation formula:
       - Rotation axis k = T_{i-1} × T_i
       - Rotation angle θ = arccos(T_{i-1} · T_i)
       - Apply rotation to get new N_i, B_i
  4. Re-orthogonalize for numerical stability
```

**Benefits:**
- No visual twisting in cylindrical tubes
- Smooth, natural orientation along curves
- Numerically stable (uses Rodrigues' formula)

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

### NURBS Curves

```cpp
// Load NURBS curves
const auto *prim = stage.GetPrimAtPath(Path("/Curves/NurbsArc"));
const auto *nurbs = prim->as<GeomNurbsCurves>();

// Configure tessellation with adaptive mode
CurveTessellationOptions options;
options.mode = CurveTessellationMode::Cylinder;
options.radial_subdivisions = 8;
options.segments_per_span = 8;
options.adaptive = true;  // Enable curvature-based adaptive tessellation

std::vector<value::float3> points;
std::vector<int> faceVertexCounts;
std::vector<int> faceVertexIndices;
std::vector<value::float3> normals;
std::vector<value::float2> uvs;

bool success = NurbsCurvesToMesh(
    *nurbs, options,
    points, faceVertexCounts, faceVertexIndices,
    normals, uvs
);
// Result: Fewer triangles in straight sections, more in curved areas
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
- **BasisCurves tessellation:**
  - Cylindrical tessellation for linear curves
  - Adaptive tessellation for cubic curves (Bezier, B-spline, Catmull-Rom)
  - Flatness-based recursive subdivision using de Casteljau's algorithm
  - Basis function conversion (B-spline and Catmull-Rom to Bezier)
- **NurbsCurves tessellation:**
  - Arbitrary order/degree NURBS evaluation
  - Cox-de Boor recursive basis functions
  - Rational NURBS with point weights
  - Knot span search and parameter evaluation
  - NURBS derivative computation (analytical)
  - Adaptive tessellation based on curvature
- **Frame computation:**
  - Parallel transport frames (rotation minimizing frames)
  - Eliminates twisting artifacts in cylindrical tubes
  - Rodrigues' rotation formula for stable frame propagation
- **Ribbon mode:**
  - Normal-oriented flat ribbons
  - Uses provided curve normals for orientation
  - Automatic re-orthogonalization
- **Common features:**
  - Width variation support
  - UV generation
  - Normal generation
  - Configuration API

### 📋 Planned
- Cards/billboard mode for performance
- Mesh shader output format (for GPU)
- Strand variation (procedural jitter)
- Level-of-detail system
- Texture coordinate modes (polar, planar, spherical)
- NURBS derivative computation for better tangents

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
