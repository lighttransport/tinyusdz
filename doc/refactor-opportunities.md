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

### TimeSamples — storage simplification (2026-03-17)

Replaced 5 storage backends with 2:

**Before (5 backends):**
- `SmallScalar` — `_small_values: vector<uint64_t>` for sizeof(T) ≤ 8
- `OffsetScalar` — `_values: Buffer<16>` + `_offsets: vector<uint64_t>` for sizeof(T) > 8
- `ArrayOffset` — `_array_values: vector<unique_ptr<Buffer<16>>>` + `_offsets` with flag encoding
- `ValueArray` — `_value_array_storage: vector<Value>` + `_value_array_refs`
- Generic — `_samples: vector<Sample>`

**After (2 backends):**
- Binary — `_data: vector<uint8_t>` (flat byte buffer) + `_data_offsets: vector<uint32_t>`
- Generic — `_samples: vector<Sample>`

**Deleted types:** `StorageDescriptor`, `ScalarStorageDescriptor`, `ArrayStorageDescriptor`,
`UnifiedStorageBackend` enum, `ArrayLayoutKind` enum, offset flag constants
(`OFFSET_DEDUP_FLAG`, `OFFSET_ARRAY_FLAG`, `OFFSET_ARRAY_BUFFER_FLAG`, etc.),
`resolve_offset_static()`.

**API changes:**
- `init()` → `set_type_id()` (metadata-only; `add_sample<T>()` auto-detects on first call)
- Removed: `get_values()`, `get_offsets()`, `get_small_values()`, `add_value_array_sample()`,
  `is_stl_array()`, `is_typed_array()`, `get_array_size()`
- Added: `get_data()`, `get_data_offsets()`, `element_size()`, `BLOCKED_OFFSET`

**Bug fixed:** `token[]` timeSamples data loss caused by early `init()` call in ASCII parser.
Non-binary types (token, string, path) have dedicated VECTOR type_ids that don't use the
`TYPE_ID_1D_ARRAY_BIT` pattern, so early init with the wrong type_id caused `add_sample()`
to reject values silently.

**Dedup removed:** In-TimeSamples deduplication was removed entirely:
- ASCII parser: removed `arrays_equal()` (~100 lines) and O(n^2) dedup lambda (~120 lines)
- Crate reader: removed `get_timesamples_dedup_map()` global tracker, `TimeSamplesDedupKeyHash`
- Crate reader already deduplicates at ValueRep level; memory impact of storing full data is
  negligible for typical files

**Net result:** −1544 lines across 9 files. 0 USDA roundtrip failures, 41 pre-existing USDC
failures (unchanged from baseline).

### TimeSamples — header slimming and compile time (prior)

- Moved non-template methods from `timesamples.hh` to `timesamples.cc`
- Removed unused `logger.hh` include from header

### TimeSamples — runtime efficiency (prior)

- Adaptive insertion sort for `TypedTimeSamples::update()` (O(n) for nearly-sorted)
- `TimeSamples::reserve(n)` pre-allocates vectors; crate reader calls before unpack
- Sorting simplified from 5 strategies to 1 (index-based permutation)

### Crate reader — unpack function consolidation (prior)

- 15 of 26 `UnpackTimeSampleValue_*` functions consolidated via 3 macros
- Function pointer dispatch consolidated via `UNPACK_CASE` macro
- Init type dispatch consolidated via `HANDLE_INIT_TYPE_CASE` macro (now using `set_type_id()`)
- Removed dead `#if 0` blocks
- Fixed blocked sample type mismatches (HALF, HALF2/3/4, FLOAT2, QUATF)

### ASCII parser — type dispatch (prior)

- Extracted shared 67-type PARSE_TYPE list to `ascii-parser-timesamples-type-list.inc`
- Array dedup code removed (was O(n^2) comparison, rarely triggered)

### Value types — operator== (prior)

- Added `operator==`/`!=` to 30+ value types using `memcmp` (bitwise identity)

### Pretty printing (prior)

- Removed dead legacy print functions
- Consolidated type size dispatch with `SIZE_CASE` macro
- Updated to use `get_data()` / `get_data_offsets()` for binary storage diagnostics

### Test coverage (prior)

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
