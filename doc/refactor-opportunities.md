# Core and Tydra Review Report

Date: 2026-03-14
Updated: 2026-03-15

## Scope

This note captures a focused code review of the C++ core storage/evaluation paths
and the main `tydra` query/conversion APIs. The audit concentrated on:

- `src/timesamples.*`
- `src/tydra/scene-access.*`
- `src/tydra/attribute-eval*`
- `src/tydra/layer-to-renderscene.*`

## Findings — All Resolved

### 1. Invalid attribute connections are not rejected early — FIXED

Severity: Critical → **Resolved** (prior commits)

Connection evaluators now return immediately on empty or multi-target connections.

### 2. `ConvertLayerInPlace()` data loss — FIXED

Severity: Critical → **Resolved** (commit ba541232)

In-place API disabled until transfer semantics are complete.

### 3. Unified small-POD `TimeSamples` sorting — FIXED

Severity: High → **Resolved** (prior commits)

`_small_values` is now reordered together with `_times` and `_blocked`.

### 4. Variable-length array timesamples — FIXED

Severity: High → **Resolved** (prior commits)

Per-sample `_array_counts` is now mandatory and reordered with times during sort.

### 5. Generic `GeomMesh` property lookup — FIXED

Severity: High → **Resolved** (commit 280424d1)

`faceVertexIndices` now correctly maps to `mesh.faceVertexIndices`.

### 6. Held interpolation for typed interpolatable timesamples — FIXED

Severity: High → **Resolved** (commit 51eb4767)

Both `TypedTimeSamples::get()` and generic `TimeSamples::get()` now properly
handle blocked samples: return first non-blocked for default time, return false
when nearest preceding sample is blocked, fall back to non-blocked endpoint in
linear mode. Tests added for all blocked sample scenarios.

### 7. In-place memory accounting underflows — FIXED

Severity: Medium → **Resolved** (commit c017d4b2)

## Refactoring Completed

### TimeSamples — header slimming and compile time

- Moved 10 non-template methods from `timesamples.hh` to `timesamples.cc`
  (`reconstruct_binary_sample`, `reconstruct_unified_sample`,
  `reconstruct_value_array_sample`, `get_samples`, `samples`, `add_sample(Sample)`,
  `add_sample(Value)`, `add_blocked_sample(Value)`, `add_value_array_sample`,
  `add_dedup_sample`, `estimate_memory_usage`)
- Header: 2853 → 2364 lines (−489, 17% reduction)
- Removed unused `logger.hh` include from header

### TimeSamples — runtime efficiency

- Adaptive insertion sort for `TypedTimeSamples::update()` (O(n) for nearly-sorted)
- `TimeSamples::reserve(n)` pre-allocates vectors; crate reader calls before unpack
- Avoid unnecessary sort index allocation in `TimeSamples::update()`

### TimeSamples — memory

- Member reordering: `sizeof(TimeSamples)` 320 → 312 bytes
- Removed dead `_samples` forwarding methods

### Crate reader — unpack function consolidation

- 15 of 26 `UnpackTimeSampleValue_*` functions consolidated via 3 macros:
  - `DEFINE_UNPACK_VECTOR_TIMESAMPLES` (HALF2/3/4, FLOAT3/4, DOUBLE2/3/4)
  - `DEFINE_UNPACK_NOINLINE_TIMESAMPLES` (QUATH, QUATD)
  - `DEFINE_UNPACK_MATRIX_TIMESAMPLES` (MATRIX2D/3D/4D)
- Function pointer dispatch consolidated via `UNPACK_CASE` macro
- Init type dispatch consolidated via `HANDLE_INIT_TYPE_CASE` macro
- Removed 6 dead `#if 0` blocks (344 lines)
- Crate reader: 3226 → 1976 lines (−1250, 39% reduction)
- Fixed blocked sample type mismatches (HALF, HALF2/3/4, FLOAT2, QUATF)
- Removed stale TODO for generic vector binary-storage path

### Crate reader — blocked sample type correctness

- 6 unpack functions were using `<float>` for blocked samples instead of their actual
  type (e.g. `<value::half2>`). Fixed to use correct types so that
  `ensure_initialized_type` doesn't reject blocked samples after initialization.

### ASCII parser — dedup and type dispatch

- Extracted shared 67-type PARSE_TYPE list to `ascii-parser-timesamples-type-list.inc`
- Consolidated dedup switch with `DEDUP_CASE` macro
- Enabled array dedup for all types (half, quat, color, point, normal, vector, texcoord)

### Value types — operator==

- Added `operator==`/`!=` to 30+ value types using `memcmp` (bitwise identity)
  for dedup support: `half`, `quath/f/d`, `vector3h/f/d`, `normal3h/f/d`,
  `point3h/f/d`, `color3h/f/d`, `color4h/f/d`, `texcoord2h/f/d`, `texcoord3h/f/d`

### Pretty printing

- Removed 642 lines of dead `#if 0` legacy print functions
- Removed dead `print_typed_array`/`try_print_typed_array` (130 lines)
- Consolidated type size dispatch with `SIZE_CASE` macro
- Deleted stale `timesamples-pprint.cc.bak`
- pprint: 1639 → 786 lines (−853, 48% reduction)

### Test coverage

- Added blocked sample tests for `TypedTimeSamples` (non-interp and interp types)
- Tests cover: default time, held at blocked time, linear with blocked endpoint,
  all-blocked, single-blocked

## Remaining Feature TODOs (not refactoring)

These are feature requests tracked in code comments, not bugs or refactoring items:

1. **Deferred TimeSamples loading** (`crate-reader-timesamples.cc:92`) — OpenUSD
   reads sample values lazily; tinyusdz reads eagerly.
2. **TimeSamples time deduplication** (`crate-reader-timesamples.cc:120`) — OpenUSD
   shares `SharedTimes` objects when multiple TimeSamples point to the same times
   array in the crate file.
3. **Type validation for times array** (`crate-reader-timesamples.cc:131`) — Validate
   that the `times` ValueRep is actually `double[]`.

## Other Modules — Review Candidates

The following modules have not been reviewed and may contain similar issues:

- `src/pprinter.cc` — Large file with potential type dispatch duplication
- `src/usdc-reader.cc` — Main crate reader with potential for similar consolidation
- `src/usda-writer.cc` / `src/usdc-writer.cc` — Serialization paths
- `src/composition.cc` — Layer composition logic
- `src/usdGeom.cc` / `src/usdShade.cc` — Schema implementations
