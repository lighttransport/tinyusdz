# C API & Python binding — remaining tasks

Snapshot as of `9032b730` (2026-05-02). Focused on outstanding work for
the C API (`src/c-tinyusd*.h`, `src/c-tinyusd-helpers.h`) and the Python
binding built on top of it (`src/python/module.c`, `python/tinyusdz/`).

## High value

### Composition arc authoring (Python — needs C API)

The C API has no constructors for `Reference`, `Payload`, `Inherits`,
`Specializes`, or `SubLayer` entries. Python therefore can only set
arcs by going through the USDA parser. The reader/writer round-trip is
solid, but a programmatic Python API is missing.

Plan:
- C API: `c_tinyusd_reference_new(asset_path, prim_path, offset, scale,
  custom_data)`, `c_tinyusd_payload_new(...)`, `c_tinyusd_prim_meta_set_references(prim,
  list_op_qualifier, refs[])`, similarly `payload`, `inheritPaths`,
  `specializes`. Use the existing `Reference` / `Payload` structs in
  `src/core/composition-types.hh` plus `ListEditQual`.
- Python: `Prim.add_reference(asset, prim_path=None, offset=0, scale=1,
  qualifier="prepend")`, `Prim.add_payload(...)`, `Prim.add_inherit(path)`,
  `Prim.add_specialize(path)`. Mirror with `clear_*` helpers.
- pyi: extend with the new methods and an `Arc` literal type for the
  qualifier string.

### Variant set / variant selection authoring

Today's authoring path can't create variant sets at all. Reader/writer
round-trip is correct, so the gap is purely on the authoring side.

Plan:
- C API: `c_tinyusd_prim_add_variant_set(prim, name)` returning a
  `CTinyUSDVariantSet` handle. Then
  `c_tinyusd_variant_set_add_variant(vs, variant_name) -> CTinyUSDVariant*`
  which exposes a `props` map you can author into via the existing
  attribute API. Plus `c_tinyusd_prim_set_variant_selection(prim,
  set_name, variant_name)` and `c_tinyusd_prim_prepend_variant_set_name(prim,
  set_name)` for the `prepend variantSets = "..."` field.
- Python: `prim.add_variant_set(name) -> VariantSet`,
  `vs.add_variant(name) -> VariantPrim` (acts like Prim with
  set_attribute/add_child), `prim.set_variant_selection(set, value)`,
  `prim.set_variant_set_order([...])`.

### Time-sampled value-type fidelity

`Prim.set_attribute_at_time` already exists but wraps the same
`py_to_value` dispatch. Some dtype hints don't propagate at sample
authoring time (notably `color3f`/`point3f` aliases, `matrix4d`,
`quat*`). Reader-side round-trip works for these via USDA but not via
the Python time-samples path.

Plan:
- Audit `Prim_set_attribute_at_time` (`src/python/module.c:1040`) and
  ensure it threads the `dtype`/`type_name` through
  `c_tinyusd_prim_set_attribute_timesample` so the typed-vec/matrix
  path is honored.
- Add tests parameterised over `(dtype, time)` round-tripping through
  USDC.

## Medium value

### USD value types still missing a C API constructor

- `texCoord2f`, `texCoord2d`, `texCoord3f`, `texCoord3d` (scalar +
  array). Same pattern as `color3f`/`point3f` aliases — three-line
  macro entries in `c-tinyusd-helpers.cc`.
- `frame4d`. Already has a C typedef
  (`c_tinyusd_frame4d_t = c_tinyusd_matrix4d_t`); needs the
  `value_new_frame4d` / `value_new_array_frame4d` wrappers and
  `ConvertValue` cases on the writer side.
- `int64[]`, `uint64[]` already constructed but the dtype dispatch
  in `py_to_value` does not yet route them; today only the scalar
  forms are reachable from Python. Wire the `int64[]`/`uint64[]`/`half[]`
  branches in `py_to_value` for `list`-of-int / `list`-of-float with the
  matching `dtype=`.
- `bool[]` constructor exists but no `dtype="bool[]"` path in
  `py_to_value` — currently a Python `[True, False]` becomes `int[]`.

### Half-precision array via numpy buffer

`set_attribute("widths", np.array([...], dtype=np.float16))` should
work without a `dtype="half[]"` hint. Today only `float32` arrays are
recognised in the buffer-protocol fast path.

Plan:
- Extend the `PyObject_GetBuffer` zero-copy branch in `py_to_value` to
  inspect `format == 'e'` (half) and dispatch
  `c_tinyusd_value_new_array_half`.

### `Attribute.value` repr for asset paths

`Value.to_string()` returns `@@@@./img.png@@@@` with quadrupled `@`
delimiters. The underlying C++ `to_string` for `AssetPath` is double-`@`,
and the Python wrapper appears to be wrapping again.

Plan:
- Trace `Value_to_string` in `src/python/module.c`; if it adds `@…@`
  unconditionally, drop that for asset values whose `to_string()`
  already includes them.

### `property_names()` for generic Shader

Symmetric to the writer-side fix in `391d90a8`: `tydra::GetPropertyNames`
returns `[]` for a `Shader` whose inputs live in `Shader::value`
(`ShaderNode`) instead of `Shader::props`.

Plan:
- In `tydra::GetPropertyNames` (`src/tydra/scene-access.cc`), when the
  prim is a `Shader` and `props` is empty, fall through to the inner
  `ShaderNode::props`.

### Stage metadata convenience setters

`set_metadata("upAxis", "Y")` works but is stringly-typed. Add typed
shortcuts mirroring `set_default_prim`:

- `Stage.set_up_axis("Y" | "Z")`
- `Stage.set_meters_per_unit(1.0)`
- `Stage.set_time_codes_per_second(24.0)`
- `Stage.set_start_time_code(0.0)` / `set_end_time_code(...)`

C API: thin wrappers around `s->metas().upAxis = value::token(...)`,
`s->metas().metersPerUnit = ...`, etc.

## Low value / nice to have

### Parser / spec gaps

- `allowedTokens = [...]` on attribute meta — parser rejects. Common
  in shader/schema definitions. `src/ascii-parser-props.cc` first-class
  case + `AttrMetas::set_allowedTokens` accessor (already exists in
  `core/attr-metas.hh`).
- `kind` on relationships — USD spec says prim-only, but pxr accepts
  it. Decide policy; if accepting, add to `_supported_prop_metas` and
  route to `set_kind` on the relationship's metas.
- `customData` on Reference/Payload — TODO comment still in
  `src/ascii-parser.cc:ParseReference`. `Reference::customData` already
  exists in the struct; add a parsed `customData = {...}` clause to
  `ParseReference` similar to the new `ParseOptionalLayerOffset`.

### Python repr / docstring polish

- `Stage.__repr__` should include `defaultPrim`, `upAxis`,
  `metersPerUnit` if authored.
- `Prim.__repr__` should hint at attribute count and child count.
- Update `python/README.md` (or create `python/AUTHORING.md`) with the
  full dtype hint table from `_core.pyi`.

### Variant authoring discoverability

Once variant authoring lands, expose iteration too: `prim.variant_sets()
-> List[VariantSet]`, `vs.variant_names() -> List[str]`, so end users
can introspect imported stages without parsing USDA themselves.

### Half-array reader buffer protocol

`Value.__buffer__` already supports float32 via `'f'` format; extend to
`'e'` (binary16) so numpy can zero-copy half arrays.

## Out of scope (or larger projects)

These are noted to keep the discussion focused but should be tackled as
their own design exercises rather than rolled into this list.

- USDZ packaging integrity for external textures (writer side).
- MaterialX validation completeness.
- Full pxr-USD `defaultPrim`/`startTimeCode` validation semantics.
- Layer-offset support on `subLayers` as opposed to references/payloads
  (the parser currently consumes the syntax for refs/payloads only).
