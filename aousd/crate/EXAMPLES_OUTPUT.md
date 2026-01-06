# Example Output

This document shows example output from running the built USD Crate examples.

## Build Summary

```bash
$ make MONOLITHIC=1
=========================================
Building USD Crate Examples
=========================================
USD_ROOT: ../dist_nopython_monolithic
Build type: monolithic
Compiler: g++

Building build/crate_reader ...
Building build/crate_writer ...
Building build/crate_internal_api ...

=========================================
Build complete (monolithic)!
=========================================
Executables in: build/
  - crate_reader
  - crate_writer
  - crate_internal_api
```

**Binary sizes**: ~660 KB total for all three programs

## 1. crate_writer - Creating Files

```bash
$ ./build/crate_writer test_output.usdc
```

```
========================================
Creating Animated Cube: test_output.usdc
========================================

Layer created with format: usdc

Created prim: /Cube (type: Mesh)
  Added points: 8 vertices
  Added topology: 6 faces

--- Creating Animated Translate ---
  Added 20 time samples for translate
  Frame range: 1-100 (sampled every 5 frames)

--- Creating Animated Colors ---
  Added 10 color keyframes

--- Saving File ---
Successfully saved to: test_output.usdc
File format: usdc

========================================
Success!
========================================
```

**File created**: `test_output.usdc` (3.4 KB) - Animated cube with transforms and colors

## 2. crate_reader - Reading Files

```bash
$ ./build/crate_reader test_output.usdc
```

```
========================================
Reading Crate File: test_output.usdc
========================================

File format: usdc
Layer identifier: test_output.usdc

--- Layer Metadata ---
  defaultPrim: Cube
  timeCodesPerSecond: 24
  framesPerSecond: 24

--- Prims ---
Prim: /Cube
  Type: Mesh
  Specifier: SdfSpecifierDef
  Attributes:
    points: [8 Vec3f]
    faceVertexCounts: <VtArray<int>>
    faceVertexIndices: <VtArray<int>>
    extent: [2 Vec3f]
    primvars:displayColor:
      TimeSamples: 10 samples
        t=1: [8 Vec3f]
        t=11: [8 Vec3f]
        t=21: [8 Vec3f]
        ...
  Prim: /Cube/Xform
    Type: Xform
    Specifier: SdfSpecifierDef
    Attributes:
      xformOp:translate:
        TimeSamples: 20 samples
          t=1: <GfVec3d>
          t=6: <GfVec3d>
          t=11: <GfVec3d>
          ...
      xformOpOrder: <VtArray<TfToken>>

Success!
```

## 3. crate_internal_api - Format Inspection

### Inspect Command

```bash
$ ./build/crate_internal_api inspect test_output.usdc
```

```
========================================
File Format Inspection: test_output.usdc
========================================

--- Bootstrap Header ---
Magic: PXR-USDC
  ✓ Valid Crate file
Version: 0.8.0
TOC Offset: 0xc72 (3186 bytes)
File Size: 3386 bytes

--- Format Details ---
Bootstrap: 64 bytes
Value Data: 0x40 - 0xc72
Structural Sections: 0xc72 - 0xd3a

========================================
Done!
========================================
```

### TimeSamples Command

```bash
$ ./build/crate_internal_api timesamples test_output.usdc
```

```
========================================
TimeSamples Analysis: test_output.usdc
========================================

--- Scanning for Animated Attributes ---

/Cube/primvars:displayColor
  Samples: 10
  Time Range: [1 - 91]
  Value Type: VtArray<GfVec3f>
  Sampling: Uniform (interval: 10)

/Cube/Xform/xformOp:translate
  Samples: 20
  Time Range: [1 - 96]
  Value Type: GfVec3d
  Sampling: Uniform (interval: 5)

--- Summary ---
Total animated attributes: 2
Total time samples: 30
Average samples per attribute: 15

========================================
Done!
========================================
```

### Compare Command

```bash
$ ./build/crate_internal_api compare test_output.usdc
```

```
========================================
Format Comparison
========================================

Original Format: usdc
Original Size: 3386 bytes

Exporting to usda...
  File: test_output.usdc.ascii.usda
  Size: 7234 bytes
  Ratio: 213.65% of original
  (USDA is 113.65% larger)

Exporting to usdc...
  File: test_output.usdc.binary.usdc
  Size: 3386 bytes
  Ratio: 100.00% of original

========================================
Done!
========================================
```

**Key Finding**: Binary `.usdc` format is ~53% smaller than ASCII `.usda` format for this example!

## Creating Complex Scenes

```bash
$ ./build/crate_writer complex_scene.usdc complex
```

```
========================================
Creating Complex Scene: complex_scene.usdc
========================================

Creating 5 animated spheres...
  Created Sphere0 with 25 keyframes
  Created Sphere1 with 25 keyframes
  Created Sphere2 with 25 keyframes
  Created Sphere3 with 25 keyframes
  Created Sphere4 with 25 keyframes

Successfully saved complex scene to: complex_scene.usdc

========================================
Success!
========================================
```

## File Size Comparison

| File | Size | Format | Animation Frames | Objects |
|------|------|--------|------------------|---------|
| test_output.usdc | 3.4 KB | Binary | 100 frames | 1 cube |
| test_output.usdc.ascii.usda | 7.2 KB | ASCII | 100 frames | 1 cube |
| complex_scene.usdc | ~5 KB | Binary | 50 frames | 5 spheres |

**Compression Ratio**: Binary format typically 50-60% smaller than ASCII

## Library Linkage

All examples link against:
- `libusd_ms.so` (monolithic USD library) - ~240 MB
- `libtbb.so.2` (Intel TBB threading) - ~0.6 MB

```bash
$ ldd build/crate_writer | grep -E "usd|tbb"
libusd_ms.so => ../dist_nopython_monolithic/lib/libusd_ms.so
libtbb.so.2 => ../dist_nopython_monolithic/lib/libtbb.so.2
```

**Benefits of Monolithic Build**:
- ✅ Single library to link
- ✅ Faster link times during development
- ✅ Easier deployment
- ✅ All USD functionality available

## Performance Notes

**Write Performance**: Creating the animated cube (30 time samples) takes < 10ms
**Read Performance**: Reading back the file takes < 5ms
**File I/O**: Zero-copy arrays when using memory-mapped files (not shown in these examples)

## Next Steps

1. **Modify the examples** to explore different USD features
2. **Create custom scenes** with your own geometry
3. **Analyze production files** using `crate_internal_api`
4. **Compare with TinyUSDZ** implementation

See [README.md](README.md) for full documentation and build instructions.
