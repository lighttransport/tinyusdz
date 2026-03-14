# Core and Tydra Review Report

Date: 2026-03-14

## Scope

This note captures a focused code review of the C++ core storage/evaluation paths
and the main `tydra` query/conversion APIs. The audit concentrated on:

- `src/timesamples.*`
- `src/tydra/scene-access.*`
- `src/tydra/attribute-eval*`
- `src/tydra/layer-to-renderscene.*`

I also ran `ctest --output-on-failure` from `build/`. All currently registered
tests passed, so the issues below are mainly in uncovered or lightly covered
paths.

## Findings

### 1. Invalid attribute connections are not rejected early

Severity: Critical

Affected files:

- `src/tydra/attribute-eval.cc`
- `src/tydra/attribute-eval-typed.cc`
- `src/tydra/attribute-eval-typed-animatable.cc`

Problem:

Several evaluators append an error when `connections()` is empty or contains
multiple targets, but then continue execution anyway. The empty case still
dereferences `pv[0]`, and the multi-target case silently follows the first
target after already reporting an error.

Impact:

- Out-of-bounds access on malformed USD input
- Silent mis-evaluation of attributes with multiple targets
- Error handling that looks safe to callers but is not

Refactor direction:

- Return immediately after detecting empty or multi-target connection lists
- Centralize connection validation in one helper and reuse it across all
  `EvaluateAttribute*` variants
- Add negative tests for empty and multi-target connection payloads

### 2. `ConvertLayerInPlace()` can delete source data without actually moving it

Severity: Critical

Affected file:

- `src/tydra/layer-to-renderscene.cc`

Problem:

`ConvertLayerInPlace()` erases converted `PrimSpec`s from the source layer after
successful conversion, but the mesh conversion path does not currently transfer
real payload data. `ConvertGeomMeshPrimSpec(..., true)` explicitly ignores
`free_source`, and the actual mesh-attribute extraction is still commented out.

Impact:

- A populated source mesh can be replaced by an effectively empty `RenderMesh`
- The source `PrimSpec` is then erased, so the operation is destructive
- Public API semantics imply a low-memory transfer path, but the implementation
  is not safe enough to expose as one

Refactor direction:

- Disable or guard the in-place API until transfer semantics are complete
- Do not erase source prims unless a converter can prove that it transferred all
  required data
- Split "convert" from "destroy source" into separate explicit steps

### 3. Unified small-POD `TimeSamples` sorting corrupts value/time pairing

Severity: High

Affected files:

- `src/timesamples.cc`
- `src/timesamples.hh`

Problem:

Small scalar POD samples are stored in `_small_values` without offsets.
`TimeSamples::update()` sorts `_times` when samples arrive out of order, but the
no-offset branch does not reorder `_small_values` or preserve blocked/value
alignment. Reconstruction later walks `_small_values` in original insertion
order.

Impact:

- Silent data corruption after sorting
- Wrong values associated with otherwise correct time stamps
- Hard-to-debug animation bugs because the corruption is deterministic but quiet

Refactor direction:

- Treat `_small_values` as a parallel array and reorder it with `_times`
- Reorder blocked-state in the same pass
- Add unit tests for out-of-order small-POD samples

### 4. Variable-length array timesamples are not represented safely

Severity: High

Affected file:

- `src/timesamples.hh`

Problem:

Unified array storage tracks a single `_array_size` for all samples, while array
samples can vary in length over time. `get_vector_at()` reads `data + _array_size`
for every sample, which means the last authored size effectively becomes global.
The class already has `_array_counts`, but the generic insertion/access path does
not use it.

Impact:

- Truncated reads when earlier samples are larger
- Potential out-of-bounds reads when earlier samples are smaller
- Incorrect semantics for valid USD with varying array lengths over time

Refactor direction:

- Make per-sample counts mandatory for unified array storage
- Use `_array_counts[idx]` during access instead of `_array_size`
- Reorder `_array_counts` together with times when sorting

### 5. Generic `GeomMesh` property lookup returns the wrong topology attribute

Severity: High

Affected file:

- `src/tydra/scene-access.cc`

Problem:

`GetPrimProperty(const GeomMesh&, ...)` maps `"faceVertexIndices"` to
`mesh.faceVertexCounts` instead of `mesh.faceVertexIndices`.

Impact:

- Any generic property-based mesh inspection receives the wrong data
- Downstream tools using `GetProperty()` or `GetAttribute()` can mis-handle
  topology
- This is the kind of schema API bug that spreads incorrect assumptions quickly

Refactor direction:

- Fix the mapping immediately
- Add a regression test for both `"faceVertexCounts"` and
  `"faceVertexIndices"`

### 6. Held interpolation is wrong for typed interpolatable timesamples

Severity: High

Affected file:

- `src/timesamples.cc`

Problem:

For interpolatable typed samples, the non-linear branch uses `lower_bound()` and
returns the current iterator element instead of the nearest preceding sample. It
also returns `false` for times after the last sample instead of holding the last
value.

Impact:

- Held interpolation disagrees with USD semantics
- Typed animation evaluation can differ from the generic `PrimVar` path
- Behavior after the last sample is especially dangerous because callers may
  interpret `false` as "no data" instead of "hold final value"

Refactor direction:

- Use the same preceding-sample logic as the non-interpolatable path
- Add tests for exact, between-sample, pre-first, and post-last times

### 7. In-place memory accounting underflows

Severity: Medium

Affected file:

- `src/tydra/layer-to-renderscene.cc`

Problem:

`memory_freed` is calculated as `memory_before - current_memory_usage_` using
`size_t`. In the current implementation the converter mostly increments memory
usage, so the subtraction wraps and reports a huge positive number.

Impact:

- Bogus telemetry for any caller using `memory_freed_callback`
- Misleading low-memory behavior when trying to measure the benefit of in-place
  conversion

Refactor direction:

- Use saturating subtraction for usage deltas
- Track allocations and deallocations separately
- Consider reusing the `MemoryTracker` utility pattern already present in
  `src/tydra/common-types.hh`

## Coverage Gaps

The current test surface does not match the risk profile of the code reviewed.

Observed gaps:

- `tests/unit/unit-timesamples.cc` exercises generic `PrimVar` interpolation but
  does not cover the broken `TypedTimeSamples<T>` held path
- No ctest-registered coverage appears to exercise `layer-to-renderscene`
- Malformed connection lists in `tydra::EvaluateAttribute*` do not appear to
  have regression coverage
- Unified small-POD sorting and variable-sized unified array samples are not
  covered

## Recommended Short-Term Plan

1. Fix the connection-handling bugs first. They are the most likely to produce
   crashes or undefined behavior on malformed data.
2. Disable or harden `ConvertLayerInPlace()` before expanding its use.
3. Repair `TimeSamples` correctness next, starting with small-POD sorting and
   held interpolation.
4. Add regression tests for every issue above before any broader refactor.

## Recommended Medium-Term Refactor Themes

### Consolidate attribute evaluation

The `attribute-eval` family repeats nearly identical connection-following and
error-reporting logic across several translation units. This should be moved
behind one internal resolver that returns a validated target or a typed error.

### Separate storage policy from query policy in `TimeSamples`

`TimeSamples` currently mixes:

- storage layout
- sort/update behavior
- typed reconstruction
- interpolation policy
- array shape management

This is making invariants hard to preserve. A cleaner split would reduce the
chance of introducing parallel-array drift bugs again.

### Make destructive conversion APIs explicit

`ConvertLayerInPlace()` currently reads like a performance optimization but acts
like a destructive ownership transfer. The API should make the destructive
contract explicit and should not be enabled until the converter is feature-complete.
