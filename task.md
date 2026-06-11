# Task: Relax shader output terminal type check (`outputs:result` type mismatch)

## Problem

Loading an Unreal-exported USD asset fails in tinyusdz on a primvar-reader shader:

```usda
def Shader "PrimvarReader"
{
    uniform token info:id = "UsdPrimvarReader_float2"
    string inputs:varname.connect = </Root/SM_vhtmaifaw_tier_19/UnrealMaterial.inputs:stPrimvarName>
    token outputs:result          # <-- declared as `token`, canonical type is `float2`
}
```

tinyusdz raises a **hard error** and aborts the load:

> Parsing shader output property `outputs:result` failed. Error: Attribute type mismatch.
> outputs:result expects type `float2` but defined as type `token`(and its underlying types).

Unreal Engine's USD exporter routinely authors `token outputs:result` on primvar-reader nodes, so
this rejection blocks a common, real-world asset class.

## Verdict (judged against the spec AND OpenUSD)

Two things are both true; they answer different questions.

### 1. UsdPreviewSurface spec — the asset is non-conformant
The UsdPreviewSurface spec (https://openusd.org/release/spec_usdpreviewsurface.html#primvar-reader)
explicitly declares the port:

> `UsdPrimvarReader_float2` → output `result` — **float2**: "Result of the geometry fetch."

All variants follow the same template (`result – TYPE`): `_float`→float, `_float2`→float2,
`_float3`→float3, `_float4`→float4, `_int`→int, `_string`→string, `_normal`→normal3f,
`_point`→point3f, `_vector`→vector3f, `_matrix`→matrix4d.

So the correct type **is** `float2`; `token outputs:result` is *wrong*.
**But** the spec defines the node's *port interface* (its semantic type) — it does **not** mandate
that a reader validate the scene-description attribute's Sdf type or reject a mismatch.

### 2. OpenUSD's logic — the asset is valid; no error, not even a warning
OpenUSD never validates a shader **output** attribute's declared type against the registry. Verified
in the OpenUSD source (checkout at `/mnt/nvme02/work/OpenUSD`):

- **Canonical type lives in the Sdr registry**, not enforced on the layer:
  `pxr/usd/plugin/usdShaders/shaders/shaderDefs.usda:292-306` declares `float2 outputs:result`.
- **Output type is read straight from the layer attribute, never compared:**
  `pxr/usd/usdShade/output.cpp:44-48` — `UsdShadeOutput::GetTypeName()` just returns
  `_attr.GetTypeName()` (→ `token`). The constructor even carries a standing TODO at
  `output.cpp:61`: `// XXX what do we do if the type name doesn't match and it exists already?`
- **The official validator checks INPUTS ONLY:** `_ShaderPropertyTypeConformance`
  (`pxr/usdValidation/usdShadeValidators/validators.cpp:350-527`, the `shaderSdrCompliance`
  check). It loops over `shader.GetInputs(false)` (line 505) and emits `mismatchPropertyType`
  for inputs only — there is **no output loop and no output error path**.
- **At render time the declared type is ignored:** Hydra builds connections from the output *name*
  only (`pxr/usdImaging/usdImaging/dataSourceMaterial.cpp:373-398`), and resolves the real type
  from the registry (`pxr/imaging/hdSt/materialNetwork.cpp:223-232` →
  `sdrNode->GetShaderOutput()->GetTypeAsSdfType()`). The `token` declared in the file is inert.

### Does OpenUSD's logic alone suffice? Yes.
Because the real type is resolved from the registry (which encodes exactly the spec's `float2`), the
declared `token` never feeds connection resolution or rendering. A spec-faithful, OpenUSD-compatible
reader needs **only** OpenUSD's model: take the port's semantic type from the schema/registry and
tolerate whatever Sdf type the layer declares on the output. It does **not** additionally need to
enforce the declared attribute type.

### Conclusion
The asset is **spec-non-conformant but OpenUSD-valid**. tinyusdz's hard error is **over-strict** —
stricter than both the spec's intent and the reference implementation. tinyusdz should not fatally
reject it.

## Workaround / fix (allow, but print a warning)

Keep tinyusdz's known semantic type (`float2` from `TypedTerminalAttribute<T>`), accept the
author's declared type, record it, and downgrade the mismatch from error to **warning**.

### Where
- **Primary:** `src/prim-reconstruct-common.inc` — function
  `ParseShaderOutputTerminalAttribute<T>` (lines ~558-658) and the driving macro
  `PARSE_SHADER_TERMINAL_ATTRIBUTE` (lines 768-778). This `.inc` is `#include`d by the shader
  reconstruction TUs (`prim-reconstruct-shader.cc`, `prim-reconstruct-shader2/3/4.cc`), so it is the
  path the failing `UsdPrimvarReader_float2` takes.
- **Keep in sync:** `src/prim-reconstruct-impl.inc` has a near-identical duplicate
  (`ParseShaderOutputTerminalAttribute` ~lines 449-571, macro ~line 695). It is only `#include`d by
  `prim-reconstruct-skel.cc`, but the two files must stay consistent. Apply the same change there.

### Existing facilities to reuse (no new infra needed)
- `ParseResult` already has a `std::string warn` field (`prim-reconstruct-common.inc:34`).
- A `PUSH_WARN` / `PUSH_WARN_F` macro already exists in the reconstruction TUs
  (e.g. `prim-reconstruct-geom.cc`, and `PUSH_WARN(ret.warn)` is already used at
  `prim-reconstruct-impl.inc:720,734`).
- `TypedTerminalAttribute<T>::set_actual_type_name(...)` already exists and is used in the
  compatible-type branches (`prim-reconstruct-common.inc:615,635`) to record an alternate authored
  type while keeping the schema type `T`.

### Approach (preferred): accept-with-warning inside the parse function
In `ParseShaderOutputTerminalAttribute<T>`, replace the terminal `TypeMismatch` returns for output
terminal attributes with an accept path that records the actual type and sets `ret.warn`:

```cpp
// Instead of: ret.code = ParseResult::ResultCode::TypeMismatch; ret.err = "...";
// For shader OUTPUT terminal attributes, follow OpenUSD: tolerate the declared type.
target.set_authored(true);
target.set_actual_type_name(attr_type_name);          // remember what the author wrote
target.metas() = prop.get_attribute().metas();
table.insert(name);
ret.warn = fmt::format(
    "Shader output `{}` is declared as `{}` but `{}` is expected per the shader schema "
    "(e.g. UsdPreviewSurface). Accepting the declared type; the schema type is used for "
    "connection/render semantics (matches OpenUSD behavior).",
    name, attr_type_name, value::TypeTraits<T>::type_name());
ret.code = ParseResult::ResultCode::Success;
return ret;
```

Apply this to the two terminal-mismatch branches that currently fire for `float2` vs `token`
(`prim-reconstruct-common.inc:621-623`, `:640-643`, and the catch-all `:647-651`). The schema type
`T` is unchanged, so downstream Tydra/render code keeps treating the port as `float2`.

Then surface the warning in the macro (`PARSE_SHADER_TERMINAL_ATTRIBUTE`, line 768-778):

```cpp
if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) {
  if (!ret.warn.empty()) { PUSH_WARN(ret.warn); }   // <-- emit accumulated warning
  DCOUT("Added shader terminal attribute: " << __name);
  continue;
}
```

### Alternative (narrower): demote only in the macro
If a behavior flag is preferred, leave the function returning `TypeMismatch` and, in
`PARSE_SHADER_TERMINAL_ATTRIBUTE`, treat `TypeMismatch` as a warning instead of
`PUSH_ERROR_AND_RETURN`:

```cpp
} else if (ret.code == ParseResult::ResultCode::TypeMismatch) {
  PUSH_WARN(fmt::format("Shader output property `{}`: {} Accepting and continuing.", __name, ret.err));
  // NOTE: the attribute is NOT recorded as authored on this path — prefer the function-level
  // approach above if you want the actual type retained.
  continue;
} else {
  PUSH_ERROR_AND_RETURN(fmt::format("Parsing shader output property `{}` failed. Error: {}", __name, ret.err));
}
```
The function-level approach is preferred because it retains the authored attribute and its metadata.

### Scope guard (do NOT over-relax)
- This relaxation is for shader **output terminal** attributes only. **Inputs** must keep their
  strict type check — OpenUSD *does* validate inputs (`validators.cpp:505`), so leave
  `ParseShaderInputConnectionProperty` and input parsing untouched.
- Consider gating behind a permissive/strict flag if tinyusdz exposes one, defaulting to permissive
  (warn) to match OpenUSD.

## Verification
1. **Repro fixture:** add a minimal `.usda` with the snippet above to `tests/` (or a sandbox file),
   confirm it currently fails, and that after the change it loads with a warning, not an error.
2. **OpenUSD cross-check:** the same file loads cleanly under OpenUSD — `usdchecker sample.usda`
   reports no `outputs:result` type error (confirms the lenient behavior we're matching).
3. **Semantics preserved:** assert the primvar reader still connects to its consumer and that
   downstream Tydra/render treats `result` as `float2` (the schema type `T`), not `token`.
4. **Regression — inputs still strict:** a shader **input** authored with a genuinely wrong type
   must still error (mirrors OpenUSD's input validation). Add/keep a test for that.
5. Build the shader reconstruction TUs and run the existing shader/material tests to ensure the
   `common.inc` change (shared across many TUs) introduces no regressions.

## References
- UsdPreviewSurface spec — Primvar Reader:
  https://openusd.org/release/spec_usdpreviewsurface.html#primvar-reader
- OpenUSD evidence (checkout `/mnt/nvme02/work/OpenUSD`): `shaderDefs.usda:292-306`,
  `usdShade/output.cpp:44-48,61`, `usdShadeValidators/validators.cpp:350-527`,
  `usdImaging/dataSourceMaterial.cpp:373-398`, `hdSt/materialNetwork.cpp:223-232`.
- Full investigation write-up:
  `/home/syoyo/.claude/plans/investigate-def-shader-primvarreader-wobbly-russell.md`
