# USD Curves Test Files

This directory contains synthetic test files for USD Curves primitives (BasisCurves and NurbsCurves).

## BasisCurves Tests

### Basic Basis Types

| File | Description | Type | Basis | Wrap | Features |
|------|-------------|------|-------|------|----------|
| `curves-basiscurves-bezier-001.usda` | Simple single cubic Bezier curve | cubic | bezier | nonperiodic | 4 control points, constant width, displayColor |
| `curves-basiscurves-bspline-001.usda` | Multiple B-spline curves (hair strands) | cubic | bspline | nonperiodic | 3 curves, 7 points each, vertex widths |
| `curves-basiscurves-catmullrom-001.usda` | Catmull-Rom spline curve | cubic | catmullRom | nonperiodic | 6 points, vertex widths |
| `curves-basiscurves-linear-001.usda` | Linear interpolation curves | linear | N/A | nonperiodic | 3 curves with varying point counts |

### Advanced Features

| File | Description | Features Tested |
|------|-------------|----------------|
| `curves-basiscurves-periodic-001.usda` | Closed periodic curve | `wrap="periodic"`, creates circle-like shape |
| `curves-basiscurves-normals-001.usda` | Curve with normals for ribbon rendering | Vertex normals, varying widths for ribbon orientation |
| `curves-basiscurves-primvars-001.usda` | Multiple primvar interpolation modes | Vertex, uniform, and varying interpolation; custom primvars |
| `curves-timesampled-001.usda` | Animated curve with timesamples | Time-sampled points, widths, and colors (frames 0-2) |
| `curves-comprehensive-001.usda` | **Complete scene with all curve types** | BasisCurves (bezier, bspline, linear, periodic), NurbsCurves, indexed primvars |

## NurbsCurves Tests

| File | Description | Features |
|------|-------------|----------|
| `curves-nurbscurves-001.usda` | Basic NURBS curve | Order 4, knot vector, 7 control points |
| `curves-nurbscurves-weighted-001.usda` | Rational NURBS with weights | Point weights for circular arc, ranges parameter |

## Test Coverage

### Attribute Coverage
- ✅ `points` (point3f[]) - all tests
- ✅ `curveVertexCounts` (int[]) - all tests
- ✅ `widths` (float[]) - all tests
- ✅ `normals` (normal3f[]) - normals-001
- ✅ `primvars:displayColor` (color3f[]) - all tests
- ✅ `type` (token: cubic/linear) - various tests
- ✅ `basis` (token: bezier/bspline/catmullRom) - various tests
- ✅ `wrap` (token: nonperiodic/periodic) - periodic-001
- ✅ NURBS `order` (int[]) - nurbscurves tests
- ✅ NURBS `knots` (double[]) - nurbscurves tests
- ✅ NURBS `pointWeights` (double[]) - weighted-001
- ✅ NURBS `ranges` (double2[]) - weighted-001

### Interpolation Modes Tested
- ✅ `constant` - single value for entire prim
- ✅ `uniform` - one value per curve
- ✅ `vertex` - one value per control point
- ✅ `varying` - one value per varying point (see USD spec)

### TimeSampling
- ✅ Default values (static)
- ✅ TimeSampled animation (timesampled-001)

## Usage Examples

### Loading and Querying BasisCurves
```cpp
#include "tinyusdz.hh"
using namespace tinyusdz;

Stage stage;
std::string warn, err;
LoadUSDFromFile("curves-basiscurves-bezier-001.usda", &stage, &warn, &err);

// Get the BasisCurves prim
const Prim *prim = stage.GetPrimAtPath(Path("/Curves/SimpleBezier"));
if (auto curves = prim->as<GeomBasisCurves>()) {
    // Use helper methods
    auto points = curves->get_points();
    auto counts = curves->get_curveVertexCounts();
    auto widths = curves->get_widths();

    // Access type/basis/wrap
    auto type = curves->type.get_value();  // Type::Cubic
    auto basis = curves->basis.get_value(); // Basis::Bezier
}
```

### Time-Sampled Queries
```cpp
// Query at specific time
double time = 1.0;
auto points_t1 = curves->get_points(time, value::TimeSampleInterpolationType::Linear);
auto widths_t1 = curves->get_widths(time);
```

## Validation

All test files should:
1. Parse without errors using TinyUSDZ
2. Validate according to OpenUSD schema specifications
3. Contain syntactically correct USDA format
4. Use proper interpolation modes for each primvar type

## References

- OpenUSD BasisCurves: https://openusd.org/dev/api/class_usd_geom_basis_curves.html
- OpenUSD Curves (base): https://openusd.org/dev/api/class_usd_geom_curves.html
- OpenUSD NurbsCurves: https://openusd.org/dev/api/class_usd_geom_nurbs_curves.html
