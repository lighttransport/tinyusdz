# Tydra Tangent/Normal Computation and Quantization

Tydra provides multiple tangent-space computation methods and GPU-friendly quantized storage formats for tangent and normal vectors. This document covers the algorithms, API, quality/performance characteristics, and WebGL2/Three.js integration.

## Overview

Tangent-space normal mapping requires per-vertex tangent and bitangent (binormal) vectors. Tydra computes these from mesh geometry and UV coordinates, then optionally packs them into compact formats for GPU upload. Normal vectors can also be quantized to reduce memory usage.

**Source files:**

| File | Purpose |
|------|---------|
| `src/tydra/render-data.hh` | `MeshConverterConfig` — tangent/normal storage format enums |
| `src/tydra/render-data.cc` | `ComputeDeferredTangents()`, `QuantizeMeshNormals()` |
| `src/tydra/tangent-quantize.hh` | Pack/unpack for tangent and normal quantized formats |
| `src/tydra/fast-mikktspace.hh` | FastMikkTSpace and Hybrid implementations |
| `src/tydra/fast-math.hh` | Fast math (rsqrt, acos) for fp16-level precision |
| `src/tydra/mikktspace-tangent.hh` | Reference MikkTSpace wrapper |
| `src/tydra/tangent-quantize.hh` | Packed tangent formats for GPU |

## Computation Methods

### Selection

```cpp
tinyusdz::tydra::RenderSceneConverterEnv env(stage);
env.mesh_config.tangent_method = MeshConverterConfig::TangentComputationMethod::FastMikkTSpace;
env.mesh_config.compute_tangents_and_binormals = true;
```

Or use deferred computation:

```cpp
env.mesh_config.defer_tangent_computation = true;
// ... later ...
converter.ComputeDeferredTangents(env, &scene);
```

### Method Comparison

| Method | Speed (MTri/s) | Quality vs MikkTSpace | Working Memory | Algorithm |
|--------|---------------|----------------------|----------------|-----------|
| **Lengyel** | ~16 | avg 1-5 deg | O(N) small | Per-face Gram-Schmidt, no welding |
| **MikkTSpace** | ~0.7 | reference | O(N) large | Edge sort + flood fill + angle-weighted avg |
| **FastMikkTSpace** | ~2.0 | identical (< 0.01 deg) | O(N) large | Same as MikkTSpace, optimized data structures |
| **Hybrid** | ~3.5 | identical (< 0.01 deg) | O(N) medium | Attribute welding + angle-weighted avg, no edge sort |

Benchmarked on UV sphere (1M triangles, 3.1M face-vertices), single-threaded.

### Lengyel

The simplest and fastest method. Computes per-triangle tangent/bitangent directions (from UV gradients), accumulates them per vertex, then orthogonalizes via Gram-Schmidt. No vertex welding or angle weighting.

Good enough when tangent precision is not critical (e.g., no normal map, or low-frequency normal maps).

### MikkTSpace (Reference)

The industry-standard algorithm by Morten S. Mikkelsen. Uses the original C implementation (`mikktspace.c`, mikktspace license(zlib-like), https://github.com/mmikk/MikkTSpace) via a callback wrapper.

Algorithm phases:
1. Vertex welding on pos+norm+uv (8 floats)
2. Per-triangle tangent derivative computation
3. Degenerate triangle detection and separation
4. Edge sorting for neighbor finding (O(N log N))
5. Flood-fill grouping by connectivity and UV orientation
6. Angle-weighted tangent averaging per group
7. Degenerate vertex epilogue (copy from good neighbors)

This is the reference for quality comparisons. Use when bit-exact MikkTSpace compatibility is required.

### FastMikkTSpace

A reimplementation of MikkTSpace with identical algorithm semantics but optimized data structures:

* Direct array access (no callback overhead)
* Flat open-addressing hash table for vertex welding
* Sorted edge array for neighbor finding (same O(N log N) as reference)
* Simplified O(N) tangent averaging at the default 180 deg threshold
  (the original does O(N^2) subgroup formation that is unnecessary at 180 deg)

Output matches MikkTSpace to floating-point tolerance. Approximately 2x faster.

### Hybrid

Combines MikkTSpace-quality grouping with a simplified pipeline that eliminates the expensive edge sort and flood fill:

1. **Per-face tangent derivatives** (same as MikkTSpace: normalized, with orientation sign)
2. **Attribute welding** via hash table (pos+norm+uv+orientation, 9-key equality)
3. **Angle-weighted accumulation** per weld group (projected tangent weighted by vertex angle)
4. **Output normalization** (exact `sqrt`, not `fast_rsqrt`, for unit-length output)

Key design decisions:
* Orientation sign is included in the weld key to prevent tangent flips at UV seams
* `fast_rsqrt` and `fast_acos` are used for intermediate computations (fp16-level precision)
* Final output uses exact normalization to guarantee unit-length tangent vectors
* No edge sort (saves ~25-30% of MikkTSpace's time)
* No flood fill (attribute-based grouping matches connectivity-based grouping on manifold meshes)

Approximately 3-4x faster than reference MikkTSpace with identical quality on well-formed meshes.

#### Standalone API

```cpp
#include "tydra/fast-mikktspace.hh"

std::vector<value::float3> tangents, binormals;
std::string err;

// FastMikkTSpace
tinyusdz::tydra::fast_mikkt::ComputeTangentsFastMikkTSpace(
    positions, normals, texcoords, faceVertexCounts,
    &tangents, &binormals, &err);

// Hybrid (also reports working memory and weld stats)
tinyusdz::tydra::fast_mikkt::HybridStats stats;
tinyusdz::tydra::fast_mikkt::ComputeTangentsHybrid(
    positions, normals, texcoords, faceVertexCounts,
    &tangents, &binormals, &stats, &err);
// stats.working_memory_bytes, stats.num_weld_groups, stats.total_vertices
```

All inputs must be facevarying (one entry per face-vertex). Only triangles and quads are supported.

## Fast Math Utilities

`src/tydra/fast-math.hh` provides approximations sufficient for fp16-level precision (~10-bit mantissa, ~0.1% relative error):

| Function | Max Error | Cost | Use |
|----------|-----------|------|-----|
| `fast_rsqrt(x)` | ~0.175% relative | bit-trick + 1 Newton-Raphson | Intermediate normalization |
| `fast_sqrt(x)` | ~0.175% relative | `x * fast_rsqrt(x)` | Intermediate computations |
| `fast_acos(x)` | ~0.017 deg | 4-coeff polynomial + sqrt | Angle weighting |
| `fast_acos_cheap(x)` | ~0.5 deg | 2-coeff polynomial + sqrt | Low-precision weighting |
| `fast_angle_weight(x)` | ~18% at obtuse | `sqrt(2*(1-x))` chord length | Trig-free alternative |
| `fast_normalize(v)` | ~0.175% magnitude | fast_rsqrt-based | Intermediate vec3 normalize |

These are designed for tangent computation internals where fp16-level precision is sufficient. Do **not** use `fast_rsqrt`-based normalization for final output vectors that must be unit-length. Use exact `sqrt` normalization instead.

## Tangent Quantization

`src/tydra/tangent-quantize.hh` provides three packed formats that store tangent direction + handedness sign in a single attribute, eliminating separate binormal storage. The bitangent is reconstructed in the shader:

```glsl
vec3 bitangent = cross(v_normal, a_tangent.xyz) * a_tangent.w;
```

### Packed Formats

| Format | Struct | Bytes | Max Error | Avg Error | GL Type |
|--------|--------|-------|-----------|-----------|---------|
| INT_2_10_10_10_REV | `PackedTangent1010102` | 4 | 0.08 deg | 0.02 deg | `GL_INT_2_10_10_10_REV` |
| SNORM8x4 | `PackedTangentSNorm8x4` | 4 | 0.34 deg | 0.11 deg | `GL_BYTE` normalized |
| FP16x4 | `PackedTangentFp16x4` | 8 | 0.03 deg | 0.001 deg | half-float |
| Float3+Float3 (baseline) | `value::float3` x2 | 24 | 0 | 0 | `GL_FLOAT` |

### Memory Savings

For a 1M-triangle mesh (3.1M face-vertices):

| Format | Tangent Storage | vs Baseline |
|--------|----------------|-------------|
| Float3 tangent + Float3 binormal | 72 MB | 1x |
| FP16x4 (tangent + sign) | 24 MB | 3x smaller |
| 10_10_10_2 or SNorm8x4 | 12 MB | 6x smaller |

### Pack / Unpack API

```cpp
#include "tydra/tangent-quantize.hh"
using namespace tinyusdz::tydra::tangent_quantize;

// Per-vertex
float sign = compute_tangent_sign(tangent, binormal, normal);
PackedTangent1010102 packed = pack_tangent_1010102(t.x, t.y, t.z, sign);

float tx, ty, tz, s;
unpack_tangent_1010102(packed, tx, ty, tz, s);
```

### Batch Conversion API

```cpp
std::vector<PackedTangent1010102> packed;
std::string err;
QuantizeTangents1010102(tangents, binormals, normals, &packed, &err);

// Also available:
// QuantizeTangentsSNorm8(...)
// QuantizeTangentsFp16(...)
```

### VertexAttribute Integration

When `render-data.hh` is included before `tangent-quantize.hh`, additional helpers are available to wrap packed data into `VertexAttribute` for use in `RenderMesh`:

```cpp
#include "tydra/render-data.hh"
#include "tydra/tangent-quantize.hh"

VertexAttribute packedAttr = PackToVertexAttribute(packed);
// packedAttr.format = VertexAttributeFormat::Uint (for 1010102)
// packedAttr.format = VertexAttributeFormat::Char4 (for SNorm8x4)
// packedAttr.format = VertexAttributeFormat::Half4 (for Fp16x4)
```

### Quality Measurement

```cpp
auto quality = MeasureQuantizeError<PackedTangent1010102>(
    tangents, binormals, normals,
    pack_tangent_1010102, unpack_tangent_1010102);
// quality.max_angle_deg, quality.avg_angle_deg, quality.rms_angle_deg
// quality.sign_mismatches — number of handedness sign errors
```

### FP16 Conversion

The header includes standalone IEEE 754 binary16 conversion functions:

```cpp
uint16_t h = float_to_half(1.0f);   // 0x3C00
float f = half_to_float(h);          // 1.0f
```

Handles all edge cases: NaN, infinity, denormals, zero, overflow, underflow.

## WebGL2 Integration

### WebGL2 Tangent Formats

**WASM default**: `PackedSNorm8` (SNorm8x4, 4 bytes/vertex). This is the widest-compatibility option — works with both WebGL1 (`GL_BYTE` normalized) and WebGL2.

For WebGL2, `GL_INT_2_10_10_10_REV` provides better precision (0.08 deg vs 0.34 deg) at the same 4 bytes, and is natively supported by `vertexAttribPointer`:

```javascript
// Upload packed tangent buffer
gl.bindBuffer(gl.ARRAY_BUFFER, tangentBuffer);
gl.bufferData(gl.ARRAY_BUFFER, packedTangentData, gl.STATIC_DRAW);

// Bind as normalized signed integer attribute
gl.vertexAttribPointer(
  tangentLocation,
  4,                          // 4 components (xyz + sign)
  gl.INT_2_10_10_10_REV,     // 0x8D9F
  true,                       // normalized to [-1, 1]
  4,                          // stride = 4 bytes
  0                           // offset
);
```

Note: `GL_INT_2_10_10_10_REV` is available in WebGL2 (OpenGL ES 3.0). For WebGL1, use SNorm8x4 with `GL_BYTE`.

### Vertex Shader Reconstruction

```glsl
// Tangent comes as vec4 from the packed attribute
attribute vec4 a_tangent;  // xyz = tangent dir, w = handedness sign

varying vec3 v_tangent;
varying vec3 v_bitangent;

void main() {
    vec3 N = normalize(normalMatrix * a_normal);
    vec3 T = normalize(normalMatrix * a_tangent.xyz);

    // Re-orthogonalize (optional, for safety)
    T = normalize(T - dot(T, N) * N);

    // Reconstruct bitangent from cross product + sign
    vec3 B = cross(N, T) * a_tangent.w;

    v_tangent = T;
    v_bitangent = B;
}
```

### Fallback: SNorm8x4

If `GL_INT_2_10_10_10_REV` is not available (WebGL1, some mobile GPUs):

```javascript
gl.vertexAttribPointer(tangentLocation, 4, gl.BYTE, true, 4, 0);
```

Max quantization error is 0.34 deg, well within visual acceptability for normal mapping.

## Normal Quantization

`src/tydra/tangent-quantize.hh` also provides packed formats for normal vectors (3-component, no handedness sign). These reduce normal storage from 12 bytes/vertex (float3) while remaining compatible with Three.js and glTF `KHR_mesh_quantization`.

### Normal Packed Formats

| Format | Struct | Bytes | Precision | Three.js Usage | glTF Equivalent |
|--------|--------|-------|-----------|---------------|-----------------|
| SNorm8x3 | `PackedNormalSNorm8x3` | 3 | ~1° (7-bit) | `BufferAttribute(Int8Array, 3, true)` | BYTE normalized |
| SNorm16x3 | `PackedNormalSNorm16x3` | 6 | ~0.003° (15-bit) | `BufferAttribute(Int16Array, 3, true)` | SHORT normalized |
| INT_2_10_10_10_REV | `uint32_t` | 4 | ~0.1° (10-bit) | Not supported natively | N/A |
| Float3 (baseline) | `value::float3` | 12 | full | `BufferAttribute(Float32Array, 3)` | FLOAT |

**WASM default**: `PackedSNorm8` (3 bytes/vertex, 75% savings). Can be changed via `MeshConverterConfig::normal_storage`.

### Normal Pack / Unpack API

```cpp
#include "tydra/tangent-quantize.hh"
using namespace tinyusdz::tydra::tangent_quantize;

// SNorm8
PackedNormalSNorm8x3 p8 = pack_normal_snorm8(nx, ny, nz);
float nx, ny, nz;
unpack_normal_snorm8(p8, nx, ny, nz);

// SNorm16 (higher precision)
PackedNormalSNorm16x3 p16 = pack_normal_snorm16(nx, ny, nz);
unpack_normal_snorm16(p16, nx, ny, nz);

// Batch
std::vector<PackedNormalSNorm8x3> packed8;
QuantizeNormalsSNorm8x3(normals, count, &packed8);

std::vector<PackedNormalSNorm16x3> packed16;
QuantizeNormalsSNorm16x3(normals, count, &packed16);
```

### MeshConverterConfig

```cpp
MeshConverterConfig config;
// Options: Float3, Packed1010102, PackedSNorm8 (WASM default), PackedSNorm16
config.normal_storage = MeshConverterConfig::NormalStorageFormat::PackedSNorm16;
```

### Three.js Integration

The WASM binding exports normals with a `normalsFormat` property (`"snorm8"`, `"snorm16"`, or `"float32"`). JS code creates the appropriate normalized `BufferAttribute`:

```javascript
if (mesh.normalsFormat === 'snorm8') {
    geometry.setAttribute('normal',
        new THREE.BufferAttribute(new Int8Array(mesh.normals), 3, true));
} else if (mesh.normalsFormat === 'snorm16') {
    geometry.setAttribute('normal',
        new THREE.BufferAttribute(new Int16Array(mesh.normals), 3, true));
} else {
    geometry.setAttribute('normal',
        new THREE.BufferAttribute(new Float32Array(mesh.normals), 3));
}
```

The `true` (normalized) flag tells the GPU to denormalize integer values to [-1, 1] float range via `vertexAttribPointer`. Three.js standard materials re-normalize in the fragment shader, so slight quantization-induced length changes are handled automatically.

## Benchmark Tool

`tests/feat/tangent/bench_tangent.cc` provides a standalone benchmark comparing all methods with quality measurement and quantization analysis.

```bash
cd tests/feat/tangent
make
./bench_tangent --quality --sizes 32,128,512 --ico-levels 3,5
```

Options:

| Flag | Description |
|------|-------------|
| `--sizes N,N,...` | UV sphere tessellation sizes (rings, sectors=rings*2) |
| `--ico-levels N,N,...` | Icosphere subdivision levels |
| `--quality` | Measure angular deviation from reference MikkTSpace |
| `--skip-ref` | Skip reference MikkTSpace (slow on large meshes) |
| `--skip-lengyel` | Skip Lengyel method |
| `--warmup N` | Warmup iterations (default 1) |
| `--repeat N` | Timed iterations (default 3) |

When `--quality` is enabled, the benchmark also reports quantization error for all three packed formats.

## Algorithm Details

### Why MikkTSpace is Slow

MikkTSpace's bottlenecks on large meshes:

1. **Edge sort** (~25-30% of time): O(N log N) sort of 3N edges for neighbor finding. Cache-hostile random access during comparison.
2. **`acos()` calls** (~10-15%): One `std::acos` per face-vertex for angle weighting. Transcendental function, ~50-100 cycles each.
3. **Hash table probing**: Vertex welding with chained hashing has poor cache locality on large meshes.
4. **Working memory**: For a 10M-triangle mesh: TriInfo ~1.4 GB, edges ~1.4 GB, hash tables ~3.6 GB. Total ~8+ GB.

The Hybrid method eliminates #1 entirely (no edge sort) and replaces #2 with `fast_acos` (~3 cycles, 4-coefficient polynomial approximation).

### Hybrid vs MikkTSpace Grouping

MikkTSpace groups vertices by **connectivity** (flood fill along edges sharing a welded vertex, same UV orientation). The Hybrid groups by **attribute equality** (same pos+norm+uv+orientation).

On manifold meshes, these produce identical groups: two face-vertices weld to the same group if and only if they are connected by edges and share all attributes. The only edge case is non-manifold vertices where the same attributes appear on disconnected mesh islands. In practice this is rare and produces negligible quality differences (~0.01 deg avg on icospheres with UV seam artifacts).

### Orientation in Weld Key

The Hybrid weld key includes UV orientation sign (whether the UV triangle is clockwise or counterclockwise). This is critical: without it, face-vertices at UV seams where orientation flips can be merged into the same group, causing tangent directions to cancel out (>90 deg errors on icospheres). MikkTSpace achieves the same separation through its flood-fill orientation check.
