# Crate-Writer Test Suite

This directory contains test infrastructure for validating crate-writer output using OpenUSD's Python API.

## Test Files

### 1. `create_test_reference.py`
Creates reference USDC files using OpenUSD's official Python API. These serve as ground truth for what valid USDC files should contain.

**Output Files:**
- `openusd_reference.usdc` (1.2KB) - Simple scene with Xform, Sphere, and Cube
- `openusd_complex.usdc` (1.7KB) - Complex scene with Mesh, animation (TimeSamples), and metadata

**Usage:**
```bash
source ../../../aousd/setup_env_monolithic.sh
python3 create_test_reference.py
```

**Features Tested:**
- Basic geometry (Sphere, Cube, Mesh)
- Transform hierarchies (Xform)
- Attributes (displayColor, radius, size, points, normals, UVs)
- Animation (TimeSamples on translate operation)
- Metadata (time codes, frames per second)

### 2. `test_openusd_validation.py`
Comprehensive validation script that uses OpenUSD Python API to verify USDC files.

**Validation Tests:**
1. File Existence - Checks file is readable
2. USDC Format Header - Verifies `PXR-USDC` magic bytes
3. Stage Opening - Confirms OpenUSD can load the file
4. Layer Metadata - Inspects file format, version, identifiers
5. Prim Structure - Validates scene hierarchy
6. Attributes - Checks attribute values and types
7. Metadata - Verifies layer-level metadata (time codes, etc.)
8. Composition Arcs - Detects references, payloads, variants

**Usage:**
```bash
source ../../../aousd/setup_env_monolithic.sh
python3 test_openusd_validation.py <usdc_file>
```

**Example Output:**
```
======================================================================
OpenUSD Validation Test
======================================================================
File: openusd_reference.usdc
USD Version: 0.25.8
======================================================================

✓ File Existence
✓ USDC Format Header
✓ Stage Opening
✓ Layer Metadata
✓ Prim Structure
  Total prims: 4
✓ Attributes
  Total attributes: 17
✓ Metadata
✓ Composition Arcs

======================================================================
Validation Summary
======================================================================
Passed: 8
Warnings: 0
Errors: 0
======================================================================
RESULT: PASSED ✓
```

## Reference Files Created

### openusd_reference.usdc
**Size:** 1,162 bytes
**Prims:** 4
**Attributes:** 17

**Structure:**
```
/World [Xform]
├── /World/Geom [Xform]
│   ├── /World/Geom/Sphere [Sphere]
│   │   ├── radius: 1.5
│   │   └── primvars:displayColor: (0.8, 0.2, 0.2)
│   └── /World/Geom/Cube [Cube]
│       ├── size: 2.0
│       └── xformOp:translate: (5, 0, 0)
```

### openusd_complex.usdc
**Size:** 1,682 bytes
**Prims:** 4
**Attributes:** 32
**Time Range:** 1.0 - 10.0 (24 fps)

**Structure:**
```
/World [Xform]
├── /World/Mesh [Mesh]
│   ├── points: 4 vertices (quad)
│   ├── normals: 4 normals
│   └── primvars:st: 4 UVs
└── /World/AnimatedSphere [Xform]
    ├── xformOp:translate: animated (10 frames)
    └── /World/AnimatedSphere/Sphere [Sphere]
        └── radius: 0.5
```

**Features:**
- TimeSamples on xformOp:translate (frame 1-10)
- Mesh topology (points, faceVertexCounts, faceVertexIndices)
- Normals and UV coordinates
- Time metadata (startTimeCode, endTimeCode, timeCodesPerSecond, framesPerSecond)

## Testing Workflow

### 1. Create Reference Files
```bash
source ../../../aousd/setup_env_monolithic.sh
python3 create_test_reference.py
```

### 2. Validate Reference Files
```bash
python3 test_openusd_validation.py openusd_reference.usdc
python3 test_openusd_validation.py openusd_complex.usdc
```

### 3. Test Crate-Writer Output (Future)
```bash
# Build and run crate-writer to create test files
../../build/example_writer output_test.usdc

# Validate crate-writer output
python3 test_openusd_validation.py output_test.usdc
```

### 4. Compare Binary Format (Future)
```bash
# Compare crate-writer output with OpenUSD reference
hexdump -C openusd_reference.usdc > ref.hex
hexdump -C output_test.usdc > test.hex
diff ref.hex test.hex
```

## Environment Setup

All tests require the OpenUSD Python bindings. Set up the environment:

```bash
# From crate-writer-2025/sandbox/crate-writer/tests/
source ../../../aousd/setup_env_monolithic.sh
```

This configures:
- Python virtual environment (`aousd/venv`)
- OpenUSD Python module (`pxr`)
- USD libraries (monolithic `libusd_ms.so`)
- Required environment variables

**Verify Setup:**
```bash
python3 -c "from pxr import Usd; print(f'USD {Usd.GetVersion()}')"
# Expected: USD (0, 25, 8)
```

## Test Status

| Test | Status | Notes |
|------|--------|-------|
| Reference file creation | ✅ PASS | Both simple and complex files created |
| OpenUSD validation | ✅ PASS | All 8 validation tests pass |
| Crate-writer build | ⚠️ BLOCKED | Missing TinyUSDZ core library symbols |
| Round-trip testing | 🔜 TODO | Pending crate-writer build fix |
| Binary comparison | 🔜 TODO | Pending crate-writer output |

## Known Issues

### Crate-Writer Build
The crate-writer example fails to build due to missing TinyUSDZ core library symbols:
- `tinyusdz::Path` constructors
- `LZ4_compress_default` and related compression functions
- `tinyusdz::Usd_IntegerCompression` functions

**Workaround:** Use Python-based validation instead of C++ integration tests.

## Next Steps

1. Fix crate-writer build dependencies
2. Create USDC files using crate-writer
3. Run validation tests on crate-writer output
4. Compare binary format with OpenUSD reference files
5. Add automated test runner script
6. Test edge cases (large files, complex scenes, all USD types)

## File Reference

```
tests/
├── README.md                      # This file
├── create_test_reference.py       # Creates OpenUSD reference files
├── test_openusd_validation.py     # Validates USDC files
├── openusd_reference.usdc         # Simple reference (1.2KB)
└── openusd_complex.usdc           # Complex reference with animation (1.7KB)
```

## Documentation

For OpenUSD build details, see:
- `../../../aousd/BUILD_SUMMARY.md` - All OpenUSD build configurations
- `../../../aousd/QUICK_START.md` - Quick reference guide
