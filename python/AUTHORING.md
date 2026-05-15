# tinyusdz Python authoring guide

This document covers *writing* USD with the Python bindings: the value
shapes `Prim.set_attribute` accepts, when an explicit `dtype=` hint is
needed, how to author composition arcs and variants, and how to pack
USDZ archives.

The canonical signature reference is [`_core.pyi`](tinyusdz/_core.pyi).
This file is prose around *when* to use each form.

## `Prim.set_attribute(name, value, dtype=None)`

### dtype-hint table

| Python value | Inferred dtype | Notes |
| --- | --- | --- |
| `int` | `int` | Use `dtype="int64"`/`"uint32"`/`"uint64"` for wider widths |
| `bool` | `bool` | |
| `float` | `float` | Use `dtype="double"` for double-precision |
| `str` | `string` | Use `dtype="token"` / `"asset"` for typed scalars |
| `list[int]` | `int[]` | |
| `list[float]` | `float[]` | |
| `list[str]` | _no auto-detect_ | Pass `dtype="string[]"` or `dtype="token[]"` |
| `tuple[int, ...]` (length 2/3/4) | `int2` / `int3` / `int4` | |
| `tuple[float, ...]` (length 2/3/4) | `float2` / `float3` / `float4` | |
| `numpy.ndarray` (1-D, `float16`) | `half[]` | No hint needed |
| `numpy.ndarray` (1-D, `float32`) | `float[]` | No hint needed |
| `numpy.ndarray` (1-D, `float64`) | `double[]` | No hint needed |

`dtype=` is required to pick a typed alias (e.g. a length-3
`tuple[float, ...]` defaults to `float3` — pass `dtype="point3f"` /
`"color3f"` / `"normal3f"` / `"vector3f"` to get the role-typed form).

### Vector arrays — three equivalent shapes

For a `point3f[]` of N points, all three of these work:

```python
# 1. List of tuples (most common)
prim.set_attribute("points", [(0,0,0), (1,0,0), (1,1,0)], dtype="point3f[]")

# 2. NumPy 2-D array (zero-copy is not implied; values are flattened
#    and re-packed)
import numpy as np
pts = np.array([[0,0,0], [1,0,0], [1,1,0]], dtype=np.float32)
prim.set_attribute("points", pts, dtype="point3f[]")

# 3. Flat list (length must be a multiple of 3)
prim.set_attribute("points", [0,0,0, 1,0,0, 1,1,0], dtype="point3f[]")
```

The `dtype=` hint is what selects `point3f[]` over `float3[]`.

### Authoring vs reading

- **Reading** (`attr.value` + buffer protocol) is zero-copy: the
  returned `Value` exposes its underlying storage as a memoryview that
  NumPy can view without allocating.
- **Authoring** (`set_attribute`) always copies. The buffer-protocol
  fast paths for `numpy.float16/32/64` ndarrays save the per-element
  Python coercion, but tinyusdz still owns its own copy of the data
  after the call returns.

### Half-precision (`half`/`half[]`)

- A `float16` ndarray needs no hint — it auto-detects to `half[]`.
- A `float32`/`float64` ndarray with `dtype="half[]"` is widened on
  the way in (each element converted via `c_tinyusd_float_to_half`).
- A list/tuple of Python floats with `dtype="half[]"` works the same
  way.

## Time samples

```python
prim.set_attribute_at_time("translate", 0.0, (0, 0, 0), dtype="float3")
prim.set_attribute_at_time("translate", 24.0, (1, 0, 0))  # dtype inferred
samples = prim.get_attribute_timesamples("translate")
# samples is List[tuple[float, Value]] — each Value supports
# as_scalar() and the buffer protocol.
```

Pass `dtype=` on the *first* sample if you need a role-typed
attribute (e.g. `color3f`/`point3f`/`matrix4d`/`quatf`); it propagates
to subsequent samples without needing to re-specify.

## Composition arcs

```python
prim.add_reference("./other.usda", prim_path="/Foo",
                   offset=0.0, scale=1.0, qualifier="prepend")
prim.add_payload("./big.usda")
prim.add_inherit("/_class_Foo")
prim.add_specialize("/_specials_Bar")
```

`qualifier` is one of `"prepend"`, `"append"`, `"add"`, `"delete"`,
`"order"`, or `""` (explicit/`ResetToExplicit`). Each kind has a
matching `clear_*` (e.g. `prim.clear_references()`).

Reference `customData = {...}` is supported on the C/Python side via
the underlying USDA syntax — not a Python API yet. Round-trip works
through USDA→USDC→USDA for arbitrary value types (the USDC writer
handles out-of-line packing for doubles, large `int64`, etc.).

## Variants

Authoring is a four-step lifecycle:

```python
# 1. Register variant set names on the prim metadata.
prim.add_variant_set_name("shadingVariant")

# 2. Define each variant inside the set.
prim.define_variant("shadingVariant", "red")
prim.define_variant("shadingVariant", "blue")

# 3. Author content inside a variant (children + attributes).
prim.variant_set_attribute("shadingVariant", "red",
                            "color", (1, 0, 0), dtype="color3f")
child = tinyusdz.Prim("Sphere", name="sphere")
prim.variant_add_child("shadingVariant", "red", child)

# 4. Pick a selection.
prim.set_variant_selection("shadingVariant", "red")
```

Iteration:

```python
prim.variant_sets()              # List[str]
prim.variant_names("shadingVariant")  # List[str]
prim.variant_selection("shadingVariant")  # Optional[str]
```

## Stage metadata

```python
stage.set_up_axis("Y")
stage.set_meters_per_unit(0.01)
stage.set_time_codes_per_second(24.0)
stage.set_frames_per_second(24.0)
stage.set_start_time_code(0.0)
stage.set_end_time_code(120.0)
```

Generic key/value access (`stage.set_metadata`/`get_metadata`) is the
escape hatch for keys without a typed setter.

## USDZ packing

Save a stage to USDZ with extra archive entries (textures, audio,
arbitrary blobs) in a single call:

```python
import tinyusdz

stage = tinyusdz.load("scene.usda")
stage.save("scene.usdz", assets={
    "tex.png":   open("tex.png", "rb").read(),
    "audio.wav": some_bytes,
})
```

Output is an uncompressed (Store-only) ZIP with 64-byte aligned data
offsets — i.e. a USDZ archive per the AOUSD Core Spec section 17. The
root layer (`root.usdc` or `root.usda`) is always the first entry. The
`assets=` values accept anything that supports the buffer protocol
(`bytes`, `bytearray`, `memoryview`).

To rewrite asset paths in your scene to match the archive layout
*before* packing:

```python
n = tinyusdz.rewrite_asset_paths(stage, {
    "/abs/path/to/tex.png": "tex.png",
    "./external/normal.png": "normal.png",
})
# n = number of attribute values that were rewritten.
stage.save("scene.usdz", assets={...})
```

## Error types

- `tinyusdz.UsdParseError` — malformed USDA / unrecognized metadatum.
- `tinyusdz.UsdIoError` — file open failure, USDC write failure.
- `tinyusdz.UsdError` — base class.

## Pointers

- [`_core.pyi`](tinyusdz/_core.pyi) — full method signatures.
- [`tests/`](tests/) — runnable examples for every API path
  documented above.
- [`README.md`](README.md) — install + read-side quick start.
