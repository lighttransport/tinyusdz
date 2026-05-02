# C API & Python binding — remaining tasks

Snapshot as of `2026-05-02` (post `1f4a7edb` "USDC writer: fix
sceneName typing and bool[]/uint*/int*/half[] customData").

Focused on outstanding work for the C API
(`src/c-tinyusd*.h`, `src/c-tinyusd-helpers.h`) and the Python binding
built on top of it (`src/python/module.c`, `python/tinyusdz/`).

## Recently shipped

The list below summarises items moved from "outstanding" to "done"
since the previous (`9032b730`) snapshot. Each entry names the commit
that landed it and the test file (or test name) that fences it.

### High-value additions

| Task | Commit | Tests |
|---|---|---|
| Composition arc authoring (Reference / Payload / Inherits / Specializes; per-qualifier listop) | `9ff86fb2` | `python/tests/test_composition_arcs.py` |
| Variant set + selection authoring (metadata level) | `7328b82f` | `python/tests/test_variant_authoring.py` |
| Variant content authoring (Prim subtrees + attributes inside variants) | `60200f7c` | `python/tests/test_variant_content_authoring.py` |
| Variant iteration helpers — `variant_sets()` / `variant_names()` / `variant_selection()` | (this commit) | `python/tests/test_variant_iteration.py` |
| `Prim.set_attribute_at_time` dtype propagation audit (color3f / point3f / matrix4d / quatf / texCoord2f / matrix4d) | `2c8b9e02` | `python/tests/test_timesample_dtype_propagation.py` |
| Stage typed-metadata setters: `set_up_axis`, `set_meters_per_unit`, `set_time_codes_per_second`, `set_frames_per_second`, `set_start_time_code`, `set_end_time_code` | `d7250cdf` | `python/tests/test_stage_metadata_typed_setters.py` |

### Medium-value additions

| Task | Commit | Tests |
|---|---|---|
| `texCoord2f/2d/3f/3d` scalar + array constructors and dispatch | `9f8c5475` | `python/tests/test_texcoord_authoring.py` |
| `frame4d` scalar/array writer + Python dispatch | `ad3a910d`, `0673f385` | `python/tests/test_authoring_extended_types.py` |
| `int64[]` / `uint64[]` / `bool[]` / `half[]` dtype dispatch | `a09a0a4a`, `ef1022f6` | `python/tests/test_array_dtype_dispatch.py` |
| `numpy.float16` / `float32` / `float64` ndarray accepted by `set_attribute(..., dtype="half[]")` (buffer-protocol fast path) | (this commit) | `python/tests/test_numpy_half_buffer.py` |
| No-dtype auto-detect: 1-D `numpy.float16` / `float32` / `float64` ndarray routes to `half[]` / `float[]` / `double[]` without an explicit `dtype=` hint | (this commit) | `python/tests/test_numpy_no_dtype_autodetect.py` |
| `tydra::GetPropertyNames` / `lookup_in_props` fall through to `Shader::value` (`ShaderNode::props`) when `Shader::props` is empty | `5a9e7055` | `python/tests/test_generic_shader_props.py` |
| `Attribute.value.to_string()` for asset paths emits `@…@` (was `@@@@…@@@@`) | (in earlier WIP, fenced by) | `python/tests/test_asset_path_normalization.py` |

### Parser / spec gaps closed

| Task | Commit | Tests |
|---|---|---|
| `allowedTokens = [...]` attribute meta — USDA parser, USDC writer field-emit, and USDC reader handler all wired (full USDA→USDC→USDA round-trip) | `6412505d`, (this commit) | `python/tests/test_allowed_tokens.py` |
| `customData = {…}` clause on `Reference` (USDA parser + USDC writer/reader; rejects on Payload) | `e1b56880`, `31516d6a`, `a1971db3` | `python/tests/test_reference_customdata.py` |
| `sceneName` (USDZ scene-library extension) typed as `string` end-to-end (was `token` in writer + Python C-API, causing silent drop and reload errors) | `1f4a7edb`, follow-up | `python/tests/test_scene_name_metadata.py`, `tests/usda/sceneLibrary-001.usda` (now passes `usdc-roundtrip-test`) |
| `customData` value packer covers `bool[]`, `uint32[]`, `int64[]`, `uint64[]`, `half[]` (was rejecting any prim authoring `bool[] zUp = [1]`) | `1f4a7edb` | `python/tests/test_customdata_array_types.py`, `tests/usda/customData-prim-003.usda` |

### Polish

| Task | Commit | Tests |
|---|---|---|
| `Stage.__repr__` surfaces `defaultPrim` / `upAxis` / `metersPerUnit` when authored; `Prim.__repr__` includes `children=N` | `cb23b1e6` | `python/tests/test_repr_polish.py` |
| USDZ packing with extra assets via `Stage.save(path, assets={archive_name: bytes})`; `tinyusdz.rewrite_asset_paths(stage, mapping)` helper to retarget asset paths before packing. Output is uncompressed Store-only ZIP, 64-byte aligned, root layer first. | (this commit) | `python/tests/test_usdz_packing.py` |

## Still outstanding

### Medium value

#### `kind` on relationships

USD spec says prim-only, but pxr accepts `kind = "..."` on a
relationship. Decide policy; if accepting, add to
`_supported_prop_metas` and route to `set_kind` on the relationship's
metas.

#### USDC writer: out-of-line packing in nested Dict-format positions

The Reference customData fix (`a1971db3`) currently rejects values
whose ValueRep does not inline (doubles, int64 above 2^47, large
arrays) — `PackValue` writes them out-of-line via `WriteValueData`,
which corrupts the surrounding stream. To accept arbitrary
customData values we'd need to either:
- buffer the customData entries and emit value data in the dedicated
  value-data section after the prim spec, threading offsets back
  through, or
- add a "scratch" buffered-write mode to `WriteValueData` that returns
  the bytes instead of writing them.

### Low value / nice to have

#### `python/AUTHORING.md` (or expanded `python/README.md`)

Document the dtype-hint table, the buffer-protocol shape (which dtypes
auto-detect, which require explicit `dtype=`), and the composition
arc / variant authoring APIs landed in this branch.

The `_core.pyi` stub is canonical for signatures; the missing piece is
prose around when each form is the right call. Especially:
- list-of-tuples vs numpy-2D vs flat-list for vec arrays
- authoring vs reading (zero-copy buffer on reads)
- variant authoring lifecycle (define_variant -> add_child / set_attribute)

#### Half-array reader buffer protocol

Already works (`Value.__buffer__` returns half-precision arrays as
numpy `float16`). Listed here because the previous snapshot flagged
it; closing it out as resolved.

## Out of scope (or larger projects)

These are noted to keep the discussion focused but should be tackled
as their own design exercises rather than rolled into this list.

- USDZ packaging integrity for external textures (writer side).
- MaterialX validation completeness.
- Full pxr-USD `defaultPrim` / `startTimeCode` validation semantics.
- Layer-offset support on `subLayers` as opposed to references /
  payloads (the parser currently consumes the syntax for refs /
  payloads only).
