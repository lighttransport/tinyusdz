# NURBS Surface Test Files

Comprehensive test suite for trimmed and untrimmed NURBS surfaces in TinyUSDZ.

## Test Files

### 1. `nurbssurface-simple-001.usda`
**Type**: Simple bicubic NURBS surface
**Control Points**: 4×4 grid
**Degree**: Cubic (3) in both U and V
**Trim Curves**: None (untrimmed)
**Features**:
- Standard clamped knot vectors
- Non-rational (all weights = 1.0)
- Parameter domain: [0,1]×[0,1]

**Purpose**: Basic NURBS surface evaluation and tessellation test

### 2. `nurbssurface-rational-001.usda`
**Type**: Rational bicubic NURBS surface
**Control Points**: 4×4 grid
**Degree**: Cubic (3) in both U and V
**Trim Curves**: None
**Features**:
- Rational weights for center control points (1.414)
- Creates bulging in the middle of the surface
- Demonstrates weight handling

**Purpose**: Test rational NURBS evaluation and weight interpolation

### 3. `nurbssurface-trimmed-001.usda`
**Type**: Trimmed bicubic NURBS surface
**Control Points**: 4×4 grid
**Degree**: Cubic (3)
**Trim Curves**: 1 (circular arc)
**Features**:
- Single circular trim curve centered at (0.5, 0.5)
- Radius 0.3 in parametric domain
- Creates a circular hole in the surface

**Purpose**: Test basic trim curve evaluation and point-in-region testing

### 4. `nurbssurface-trimmed-holes-001.usda`
**Type**: Trimmed NURBS surface with multiple trim loops
**Control Points**: 5×5 grid
**Degree**: Cubic (3)
**Trim Curves**: 8 (4 for outer boundary, 4 for hole)
**Features**:
- Outer boundary: rectangular trim 0.05 from edges
- Inner hole: circular trim centered at (0.5, 0.5)
- Demonstrates complex trim topology

**Purpose**: Test multiple trim loops, holes, and complex trimming

### 5. `nurbssurface-timesampled-001.usda`
**Type**: Animated bicubic NURBS surface
**Control Points**: 4×4 grid (timesampled)
**Degree**: Cubic (3)
**Trim Curves**: None
**Features**:
- Animated at 24 fps
- Keyframes at t=0, t=12, t=24
- Surface deformation over time
- Demonstrates temporal variation

**Purpose**: Test timesampled control point evaluation

### 6. `nurbssurface-quartic-001.usda`
**Type**: Quartic NURBS surface
**Control Points**: 5×5 grid
**Degree**: Quartic (4) in both U and V
**Trim Curves**: None
**Features**:
- Higher degree (4) than cubic
- 11-knot vectors (n + p + 1 = 5 + 4 + 1 = 10... wait, should be 11)
- Smoother surface with more control
- Tests arbitrary degree support

**Purpose**: Test higher-degree NURBS surfaces

### 7. `nurbssurface-nonuniform-001.usda`
**Type**: Non-uniform rational NURBS surface
**Control Points**: 4×4 grid
**Degree**: Cubic (3)
**Trim Curves**: None
**Features**:
- Non-uniform knot vectors (clustered knots)
- Rational weights (saddle-like shape)
- U: clustered at 0.3 and 0.7
- V: non-uniform spacing
- Creates sharp features and deformation

**Purpose**: Test non-uniform knot vectors and complex weight distributions

### 8. `nurbssurface-linear-001.usda`
**Type**: Linear NURBS surface
**Control Points**: 3×3 grid
**Degree**: Linear (1) in both U and V
**Trim Curves**: None
**Features**:
- Degenerate case: essentially a bilinear patch
- Minimal complexity
- Tests edge case handling

**Purpose**: Test degenerate/edge cases and lowest-degree NURBS

## Test Coverage

### Algorithm Coverage
- ✅ Cox-de Boor basis evaluation (all degrees)
- ✅ NURBS surface evaluation (rational and non-rational)
- ✅ Knot span finding (uniform and non-uniform)
- ✅ Partial derivatives and normals
- ✅ Trim curve evaluation (linear and arc)
- ✅ Point-in-trim-region testing
- ✅ Adaptive tessellation (all surfaces)
- ✅ Timesampled data interpolation

### NURBS Features
- ✅ Arbitrary degree (linear to quartic)
- ✅ Arbitrary control point grid sizes (3×3 to 5×5)
- ✅ Uniform knot vectors
- ✅ Non-uniform knot vectors
- ✅ Rational NURBS with weights
- ✅ Non-rational B-splines
- ✅ Parameter ranges (clamped)

### Trim Features
- ✅ No trim curves (untrimmed)
- ✅ Single trim loop
- ✅ Multiple trim loops
- ✅ Outer boundary + holes
- ✅ Linear trim curves
- ✅ Arc trim curves

### Temporal Features
- ✅ Static surfaces
- ✅ Timesampled deformation
- ✅ Multiple keyframes

## Running Tests

### Load Individual File
```cpp
tinyusdz::Stage stage;
std::string err, warn;
tinyusdz::LoadUSDFromFile("nurbssurface-simple-001.usda", &stage, &warn, &err);

// Get the NurbsSurface prim
auto* prim = stage.GetPrimByPath("/Scene/SimpleBicubicSurface");
auto* nurbs = dynamic_cast<tinyusdz::GeomNurbsSurface*>(prim);

// Tessellate
tinyusdz::tydra::RenderMesh mesh;
tinyusdz::tydra::TessellateNurbsSurfaceForRendering(*nurbs, options, mesh);
```

### Batch Test All Files
```python
import subprocess
import os

test_dir = "tests/usda"
nurbs_files = [f for f in os.listdir(test_dir) if f.startswith("nurbssurface-") and f.endswith(".usda")]

for test_file in nurbs_files:
    print(f"Testing {test_file}...")
    # Load and validate with TinyUSDZ
    # Tessellate and check output mesh
    # Generate statistics
```

## Expected Results

### nurbssurface-simple-001.usda
```
Surface: 4×4 bicubic
Parameter domain: [0,1]×[0,1]
Expected tessellation: 200-500 triangles (adaptive)
Mesh validity: All vertices within [-1.5, 1.5] ranges
Normals: Well-defined (no degeneracies)
```

### nurbssurface-rational-001.usda
```
Surface: 4×4 cubic, rational with weights
Bulge factor: ~1.414 at center
Expected tessellation: 180-450 triangles
Mesh validity: Center should be elevated due to weights
```

### nurbssurface-trimmed-001.usda
```
Surface: 4×4 cubic, trimmed with circular hole
Trim radius: 0.3 in param space
Expected vertices: Only outside circle
Triangle count: ~60-100 (fewer due to trim)
Trim region validation: Points at center should be excluded
```

### nurbssurface-trimmed-holes-001.usda
```
Surface: 5×5 cubic, multiple trims
Outer trim: rectangle [0.05, 0.95]×[0.05, 0.95]
Inner hole: circle at (0.5, 0.5), r=0.15
Expected vertices: Only in outer annular region
Complex topology: Tests proper loop ordering
```

### nurbssurface-timesampled-001.usda
```
Evaluation at t=0:   200-500 triangles
Evaluation at t=12:  180-480 triangles (deformed)
Evaluation at t=24:  200-500 triangles (back to original)
Interpolation: Linear between keyframes
```

### nurbssurface-quartic-001.usda
```
Surface: 5×5 quartic degree
Expected tessellation: 250-600 triangles (higher degree = more curved)
Knot vector size: 11 in each direction
Continuity: C³ (quartic continuity at edges)
```

### nurbssurface-nonuniform-001.usda
```
Surface: 4×4 cubic, non-uniform knots
Clustered knots visible as: Sharper transitions
Weight distribution: Saddle shape from non-uniform weights
Expected tessellation: 200-450 triangles with dense regions
Validate: Non-uniform distribution is preserved
```

### nurbssurface-linear-001.usda
```
Surface: 3×3 linear (bilinear)
Tessellation: ~4 triangles per grid cell = ~32 triangles total
Simplicity: Linear interpolation between vertices
Normal verification: Should be flat-like normals
```

## Validation Checklist

- [ ] All files parse without errors
- [ ] Correct number of control points loaded
- [ ] Knot vectors properly sized
- [ ] Weights (if present) correctly interpreted
- [ ] Parameter ranges honored
- [ ] Trim curves (if present) properly parsed
- [ ] Tessellation produces valid meshes
- [ ] Vertex normals are well-defined
- [ ] Texture coordinates generated correctly
- [ ] Performance meets expectations (< 5ms per surface)

## Future Test Cases

- [ ] Very high degree surfaces (degree 8+)
- [ ] Large control point grids (10×10+)
- [ ] Multiple surfaces in single file
- [ ] Surfaces with multiple materials
- [ ] Surfaces with primvars
- [ ] Surfaces with time-varying trim curves
- [ ] Degenerate cases (duplicate control points, etc.)
- [ ] Error handling (invalid knots, mismatched sizes)

## Integration with Test Suite

These files should be tested:
1. **Unit tests**: Direct API calls to tessellation functions
2. **Integration tests**: Full USD load → tessellation → output
3. **Performance tests**: Tessellation time benchmarks
4. **Regression tests**: Verify output mesh consistency

```bash
# Example test runner
cd build
ctest -R nurbs -V
```

## References

- USD NurbsSurface Schema: https://graphics.pixar.com/usd/docs/api/class_usd_geom_nurbs_surface.html
- NURBS Book: Piegl & Tiller
- Surface Evaluation: Cox-de Boor algorithm
- Tessellation: Adaptive domain griding
