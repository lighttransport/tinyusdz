# Quick Start: Trimmed NURBS Surface Tessellation

Get tessellated trimmed NURBS surfaces in 5 minutes.

## Installation (1 minute)

### Files Added
- `src/tydra/trimmed-nurbs.hh` - Core implementation
- `src/tydra/trimmed-nurbs.cc` - Tessellation algorithm
- `src/tydra/trimmed-nurbs-integration.hh` - USD integration
- Modified: `src/usdGeom.hh`, `src/value-types.hh`

### Build
```bash
cd /path/to/tinyusdz-repo/curves
mkdir build && cd build
cmake -DTINYUSDZ_BUILD_TESTS=ON ..
make -j8

# Verify
./test_trimmed_nurbs
```

## Usage (2 minutes)

### Basic Example: Tessellate a NURBS Surface

```cpp
#include "src/tydra/trimmed-nurbs.hh"
#include "src/tydra/trimmed-nurbs-integration.hh"

using namespace tinyusdz::tydra;

// 1. Create NURBS surface data
NurbsSurfaceData surface;
surface.control_points = {
  // 16 control points for 4x4 grid
  {-1, -1, -1}, {-0.3, -1, -1.5}, {0.3, -1, -1.5}, {1, -1, -1},
  {-1, -0.3, -1.5}, {-0.5, -0.5, -2}, {0.5, -0.5, -2}, {1, -0.3, -1.5},
  {-1, 0.3, -1.5}, {-0.5, 0.5, -2}, {0.5, 0.5, -2}, {1, 0.3, -1.5},
  {-1, 1, -1}, {-0.3, 1, -1.5}, {0.3, 1, -1.5}, {1, 1, -1},
};
surface.degree_u = 3;
surface.degree_v = 3;
surface.num_ctrl_u = 4;
surface.num_ctrl_v = 4;
surface.knots_u = {0, 0, 0, 0, 1, 1, 1, 1};
surface.knots_v = {0, 0, 0, 0, 1, 1, 1, 1};

// 2. Create trimmed surface
TrimmedNurbsSurface trimmed;
trimmed.surface = surface;
// Optionally add trim loops here
// trimmed.trim_loops.push_back(trim_loop);

// 3. Configure tessellation
TrimmedNurbsTessellationOptions options;
options.adaptive = true;           // Enable adaptive tessellation
options.screen_space_error = 1.0f; // Pixel-accurate
options.generate_normals = true;
options.generate_uvs = true;

// 4. Tessellate
RenderMesh mesh;
TrimmedNurbsTessellator tessellator;
tessellator.Tessellate(trimmed, options, mesh);

// 5. Use mesh
std::cout << "Vertices: " << mesh.points.size() << "\n";
std::cout << "Triangles: " << mesh.faceVertexIndices.size() / 3 << "\n";

// Render or save mesh.points, mesh.normals, mesh.faceVertexIndices
```

### From USD: Load and Tessellate

```cpp
#include "src/usdGeom.hh"
#include "src/tydra/trimmed-nurbs-integration.hh"

// Assuming you've loaded a USD file with a NurbsSurface primitive
GeomNurbsSurface& nurbs = /* ... from stage ... */;

GeomNurbsSurfaceTessellationOptions opts;
opts.generate_normals = true;

RenderMesh mesh;
TessellateNurbsSurfaceForRendering(nurbs, opts, mesh);
```

## Configuration (1 minute)

### Real-time Rendering
```cpp
options.adaptive = true;
options.screen_space_error = 2.0f;  // Looser tolerance
options.max_edge_length = 0.2f;
```

### Film/High-Quality
```cpp
options.adaptive = true;
options.screen_space_error = 0.5f;  // Strict tolerance
options.max_edge_length = 0.05f;
```

### Fast Preview
```cpp
options.adaptive = false;      // Uniform tessellation
options.min_u_divisions = 4;
options.min_v_divisions = 4;
```

## Trim Curves (1 minute)

### Add a Circular Trim

```cpp
// Create a trim curve
TrimCurve2D trim_curve;
trim_curve.type = TrimCurve2D::CurveType::CircleArc;
trim_curve.circle_center = ParamPoint{0.5, 0.5};  // Center in param space
trim_curve.circle_radius = 0.3;                    // Radius
trim_curve.control_points = {
  {0.8, 0.5},  // Start point
  {0.2, 0.5},  // End point
};

// Create trim loop
TrimLoop trim_loop;
trim_loop.outer_boundary = true;
trim_loop.curves.push_back(trim_curve);

// Add to surface
trimmed.trim_loops.push_back(trim_loop);
```

### Multiple Trim Loops (Holes)

```cpp
// First loop: outer boundary (always included)
TrimLoop outer;
outer.outer_boundary = true;
outer.curves.push_back(outer_boundary_curve);
trimmed.trim_loops.push_back(outer);

// Second loop: hole
TrimLoop hole;
hole.outer_boundary = false;
hole.curves.push_back(hole_curve);
trimmed.trim_loops.push_back(hole);
```

## Performance Tips

### Speed Optimization
```cpp
// Use fewer divisions for faster tessellation
options.min_u_divisions = 2;
options.min_v_divisions = 2;
options.adaptive = false;  // Skip curvature check
```

### Quality Optimization
```cpp
// Generate higher quality mesh
options.adaptive = true;
options.screen_space_error = 0.1f;  // Very strict
options.min_u_divisions = 8;
options.max_u_divisions = 64;
```

### Memory Optimization
```cpp
// Skip optional data
options.generate_normals = false;
options.generate_uvs = false;
// Only generate positions
```

## Testing

Run included comprehensive test:

```bash
cd build
./test_trimmed_nurbs
```

Output shows:
- ✅ Surface evaluation at 36 points
- ✅ Trim curve evaluation
- ✅ Point-in-region tests
- ✅ Full tessellation pipeline
- ✅ Mesh statistics

## Common Patterns

### Load from USD, Tessellate, Save Mesh

```cpp
// Load USD
Stage stage;
LoadUSDFromFile("model.usd", &stage);

// Get NURBS surface from stage
auto* prim = stage.GetPrimByPath("/Surface");
auto* nurbs = dynamic_cast<GeomNurbsSurface*>(prim);

// Tessellate
GeomNurbsSurfaceTessellationOptions opts;
opts.generate_normals = true;
RenderMesh mesh;
TessellateNurbsSurfaceForRendering(*nurbs, opts, mesh);

// Export to OBJ or other format
ExportToOBJ("output.obj", mesh);
```

### Batch Process Multiple Surfaces

```cpp
std::vector<RenderMesh> all_meshes;

for (const auto& nurbs_prim : nurbs_primitives) {
  RenderMesh mesh;
  TessellateNurbsSurfaceForRendering(nurbs_prim, opts, mesh);
  all_meshes.push_back(mesh);
}
```

### Adaptive LOD Based on Distance

```cpp
float distance = ComputeDistance(camera, surface_bounds);

if (distance > 100.0f) {
  options.screen_space_error = 5.0f;    // Coarse from far
} else if (distance > 50.0f) {
  options.screen_space_error = 2.0f;    // Medium
} else {
  options.screen_space_error = 0.5f;    // Fine up close
}
```

## Troubleshooting

### Tessellation produces empty mesh
```cpp
// Check surface validation
std::string err;
if (!trimmed.Validate(&err)) {
  std::cerr << "Surface error: " << err << "\n";
  return;
}
```

### Too many triangles
```cpp
// Increase error tolerance
options.screen_space_error = 5.0f;
options.max_edge_length = 0.5f;
```

### Trim curve not working
```cpp
// Verify trim loop is valid
if (!trim_loop.IsValid()) {
  std::cerr << "Invalid trim loop\n";
  return;
}

// Test point-in-region directly
ParamPoint test_pt{0.5, 0.5};
bool inside = IsPointInTrimRegion(test_pt, trim_loop);
std::cout << (inside ? "Inside" : "Outside") << "\n";
```

### Slow tessellation
```cpp
// Check adaptive settings
options.adaptive = true;  // Enables speed optimizations
options.camera_distance = /* realistic value */;
options.screen_space_error = /* increase for speed */;
```

## Next Steps

1. **Read TRIMMED_NURBS.md** for detailed algorithm explanations
2. **Review test_trimmed_nurbs.cc** for complete usage examples
3. **Check CMAKE_TRIMMED_NURBS.md** for build details
4. **See IMPLEMENTATION_SUMMARY.md** for architecture overview

## Key Files Reference

| File | Purpose |
|------|---------|
| `trimmed-nurbs.hh` | Data structures & algorithms |
| `trimmed-nurbs.cc` | Tessellation implementation |
| `trimmed-nurbs-integration.hh` | USD integration |
| `test_trimmed_nurbs.cc` | Examples & tests |
| `TRIMMED_NURBS.md` | Full documentation |

## API Quick Reference

### Main Classes
- `NurbsSurfaceData` - Surface definition
- `TrimmedNurbsSurface` - Surface with trim curves
- `TrimCurve2D` - Individual trim curve
- `TrimLoop` - Group of curves
- `TrimmedNurbsTessellator` - Main tessellator

### Core Functions
- `EvaluateNurbsSurface(surface, u, v)` - Get 3D point
- `ComputeSurfaceNormal(surface, u, v)` - Get normal
- `IsPointInTrimRegion(pt, trim_loop)` - Test trim
- `TessellateGeomNurbsSurface()` - Full pipeline

### Options
- `TrimmedNurbsTessellationOptions` - Configuration
- `GeomNurbsSurfaceTessellationOptions` - Tydra options

## Support

For detailed information:
- **Algorithm details**: TRIMMED_NURBS.md
- **Usage examples**: test_trimmed_nurbs.cc
- **Build issues**: CMAKE_TRIMMED_NURBS.md
- **Architecture**: IMPLEMENTATION_SUMMARY.md

---

**Happy tessellating! 🎉**
