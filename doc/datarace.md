# Thread-Safety & Data-Race Notes

Scope: concurrency in the `tinyusdz::pcp::Cache` engine (`src/pcp/`) and the
shared code its optional multithreaded build path touches. This is the working
record of what was audited, what was fixed, what remains, and how to verify.

Context: `pcp::Cache::PrewarmPrimIndices()` / `BuildStage()` can build
independent prim indices on worker threads when `CacheOptions::num_threads != 1`
and `TINYUSDZ_ENABLE_THREAD` is compiled in (never on wasm). Each worker runs
`Cache::Impl::BuildEntry() -> composition_graph::PrimIndexBuilder::Build()`
against the **shared** composited root layer and the registry-shared referenced
layers. That sharing is where data races live.

Audit method: read every shared object the workers touch concurrently, plus a
ThreadSanitizer run of the pcp tests and the full unit suite (see *Reproducing*
below).

---

## Fixed (for reference)

### Layer::find_primspec_at lookup cache — **FIXED**
`Layer::find_primspec_at()` is `const` but mutates a lazy lookup cache
(`LayerImpl::_primspec_path_cache` / `_dirty`, `src/layer.cc`). Parallel pcp
builds call it concurrently on the shared root layer and on registry-shared
referenced layers → unsynchronized `std::map` writes = UB. ThreadSanitizer
reported 16 races at `src/layer.cc` before the fix, 0 after.

Fix: a gated `std::shared_ptr<std::mutex> LayerImpl::_cache_mu` (`src/layer.cc`
~L246) guards only the cache read/write in `find_primspec_at()` (`cache_lock`
at L520, released for the lock-free `_prim_specs` tree walk at L538, re-taken
for the insert at L549). Gated on `TINYUSDZ_ENABLE_THREAD`, so non-threaded /
wasm builds are byte-for-byte unaffected. `shared_ptr` keeps `Layer`
copyable/movable. Covered by `pcp_singlethread_vs_multithread_identical_test`
and `pcp_mt_shared_reference_test`.

### Layer copy carried stale lookup-cache pointers — **FIXED**
`Layer`'s copy ctor / copy-assignment deep-copied `LayerImpl`, which carried
`_primspec_path_cache` (a map of `const PrimSpec*` into the **source** layer's
`_prim_specs` tree) and the source's `_cache_mu`. A copy made after the source
was queried (`_dirty == false`) would hand out pointers into the *source* layer
— dangling if the source died, wrong after either mutated; the shared mutex also
leaked across copies.

Fix: `LayerImpl::reset_lookup_cache()` (`src/layer.cc` ~L255) clears the cache,
sets `_dirty = true`, and installs a **fresh** `_cache_mu`; it is called from
the copy ctor (`src/layer.cc:297`) and copy-assignment (`:310`). The copy now
rebuilds its cache lazily against its own tree and owns an independent mutex.
This was item #4 below; resolved by the `dedup-2026` merge.

---

## Remaining issues

### 1. Pre-existing — `TaskQueue::Push`/`Pop` data races (NOT used by pcp)
- **Where:** `src/task-queue.hh:100` (`_tasks[index] = TaskItem(...)` store in
  `Push`) and `:147` (`task = _tasks[index]` load in `Pop`); the
  `TaskQueueFunc` variant has the same pattern at `:279`/`:297` and `:164`.
- **Why:** the lock-free ring orders slots with atomic `_write_pos`/`_read_pos`
  but the **payload** `TaskItem` store/load is a plain (non-atomic) access, so
  TSan flags producer/consumer overlap on the same slot. Exercised by
  `tests/unit/unit-task-queue.cc:163/176` (`task_queue_multithreaded_test`).
- **Impact on pcp:** none. `src/pcp/cache-parallel.cc` deliberately does **not**
  use `TaskQueue`; it uses `std::thread` + a `std::atomic<size_t>` work index
  and pre-sized per-worker output slots. This entry is recorded only because the
  full-suite TSan run surfaced it.
- **Severity:** medium (real on weak memory models). Pre-existing; the MT
  TaskQueue test is already skipped on i386 due to 64-bit atomics.
- **Fix sketch:** make the slot payload publication ordered w.r.t. the position
  CAS — e.g. store the `TaskItem` *before* the releasing `_write_pos` update and
  load it *after* the acquiring `_read_pos` check, with `std::atomic_ref` (C++20)
  or by making `_tasks[i]` an atomic/seqlock'd cell. Or gate the test out of TSan.

### 2. Latent — `Path::variant_part()` / `element_name()` mutable buffers
- **Where:** `src/core/path.hh` — `mutable std::string _variant_part_str` (L385)
  and `mutable std::string _element` (L386), written by the `const` accessors
  `variant_part()` (L116-119) and `element_name()` (L201, def in `path.cc`).
- **Why:** these `const` methods write the mutable buffers, so calling them on a
  **shared** `Path` from two threads races.
- **Triggered today?** **No.** The build path (`composition-graph.cc`,
  `layer.cc`, `namespace-mapping.*`) calls neither on shared `Path`s — it uses
  `prim_part()` (returns a member ref, pure) and `full_path_name()` (builds a
  local string, pure). Confirmed by grep.
- **Severity:** low now, but a footgun: any future build-path code that calls
  `variant_part()`/`element_name()` on a `Path` living inside a shared
  layer's `PrimSpec` metadata (e.g. `Reference::prim_path`, an inherit/specialize
  target) during a parallel build would introduce a fresh race.
- **Fix sketch:** make these return a computed `std::string` by value instead of
  caching into a mutable buffer (drops the `mutable` members entirely); or add a
  short comment at the call-free build sites warning not to call them on shared
  Paths under MT.

### 3. Latent — `Layer::check_unresolved_*()` mutable flags
- **Where:** `src/layer.cc` — `mutable bool _has_unresolved_references` … (L251-257)
  written by `check_unresolved_references()` (sets at L528), `…_payload` (L543),
  `…_over_primspec` (L603), etc. — all `const` methods that compute-and-cache.
- **Why:** unsynchronized writes to the mutable flags if two threads call
  `check_unresolved_*()` on the same shared `Layer` concurrently.
- **Triggered today?** **No.** The parallel build reads `PrimSpec` metadata
  directly (`ps->metas().references`, …) and never calls
  `Layer::has_unresolved_*()` / `check_unresolved_*()` on shared layers. These
  flags are computed single-threaded (sublayer composition in `Cache::Open()`,
  or during layer load under the registry lock).
- **Severity:** low; footgun if a future MT path calls these on a shared layer.
- **Fix sketch:** reuse the same `LayerImpl::_cache_mu` (gated) to guard these
  flags, or compute them eagerly at load time.

### 4. ~~Latent — `Layer` copy carries stale cache pointers~~ — **FIXED**
Resolved by the `dedup-2026` merge (`LayerImpl::reset_lookup_cache()` called
from the copy ctor/assignment, `src/layer.cc:255/297/310`). See the *Fixed*
section above. Kept here, struck through, so the item numbers below stay stable.

### 5. By-design caveat — user callbacks under multithreading
- `CacheOptions::composition.payload_policy` (a `std::function`) and the
  `fileformats` handlers are invoked from worker threads during a parallel
  build. Each worker uses its own *copy* of the `std::function`, but any state
  the closure **captures** may be shared.
- **Action:** document that these callbacks must be thread-safe when
  `num_threads != 1`. Not a code bug; an API contract to record (e.g. in
  `cache.hh` near `CacheOptions`).

---

## Confirmed safe (no action)

- `PrimSpec` / `PrimMeta` / `LayerMetas` / `Reference` / `Payload`: no `mutable`
  members → concurrent metadata reads are safe.
- `value::AssetPath::GetAssetPath()` returns a member ref (pure);
  `security_policy::ValidateAndNormalizeAssetPath()` and the namespace-mapping
  `Make*` / `Compose` helpers are pure (local copies, no static/global state);
  `Path::prim_part()` / `full_path_name()` / `is_valid()` are pure.
- No static mutable variables in `composition-graph.cc` / `pcp/*.cc`.
- `LayerRegistry` mutates only under its own mutex (which also serializes the
  shared `AssetResolutionResolver`'s working-path); each `CompositionContext` is
  per-entry/private; `BuildParallel` writes pre-sized `slots[]` at distinct
  indices; the cache maps (`index_cache`, `site_to_indices`, `index_to_sites`)
  are mutated only in the single-threaded merge after the join barrier.

---

## Reproducing with ThreadSanitizer

```bash
# Build a TSan unit-test binary (threads on).
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DTINYUSDZ_BUILD_TESTS=ON \
  -DTINYUSDZ_ENABLE_THREAD=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan --target unit-test-tinyusdz -j16

# NOTE: modern Linux kernels make TSan abort at startup with
#   "FATAL: ThreadSanitizer: unexpected memory mapping"
# due to high ASLR entropy. Run under `setarch -R` (disable randomization),
# or lower vm.mmap_rnd_bits (needs root).
setarch -R ./build-tsan/unit-test-tinyusdz pcp_mt_shared_reference_test \
                                            pcp_singlethread_vs_multithread_identical_test
# Full suite (will surface the pre-existing TaskQueue races, item #1):
setarch -R ./build-tsan/unit-test-tinyusdz
```

The pcp tests must report **0 data races**. Any race whose stack mentions
`src/pcp/`, `src/layer.cc`, `src/composition-graph.cc`, or `src/tiny-hashmap.hh`
is a regression in this work; races in `src/task-queue.hh` are item #1
(pre-existing, not used by pcp).

After any change, also run the normal builds:
```bash
cd build && cmake --build . --target unit-test-tinyusdz -j16 && ./unit-test-tinyusdz   # threads off (canonical)
# threads on: configure a separate build dir with -DTINYUSDZ_ENABLE_THREAD=ON
cd web && cmake --build build -j16                                                     # wasm (single-threaded path)
```

Constraint for all fixes: keep non-threaded and wasm builds zero-overhead — gate
synchronization on `TINYUSDZ_ENABLE_THREAD` and never add a mutex member that
breaks `Layer` copy/move.

---

## Resume prompt

Paste this to continue the work in a fresh session:

```
Continue the thread-safety hardening on branch pcp-2026. Read doc/datarace.md
first — it records the audited state. Background: tinyusdz::pcp::Cache (src/pcp/)
has an optional multithreaded build path; the one race that mattered
(Layer::find_primspec_at lookup cache) is already fixed and TSan-clean.

Remaining items, in priority order:
  1. Pre-existing TaskQueue::Push/Pop races (src/task-queue.hh:100/147) — make
     the slot payload publication ordered w.r.t. the position CAS, or gate the
     task_queue_multithreaded_test out of TSan. pcp does not use TaskQueue.
  2. Latent: Path::variant_part()/element_name() mutable buffers
     (src/core/path.hh:385-386) — prefer returning by value to drop the mutable
     members; not currently called on shared Paths in the build path.
  3. Latent: Layer::check_unresolved_*() mutable flags (src/layer.cc:251-257) —
     guard with the gated LayerImpl::_cache_mu or compute eagerly at load.
  4. (DONE) Layer copy stale lookup-cache pointers — fixed via
     LayerImpl::reset_lookup_cache() on copy (merged from dedup-2026).
  5. Document the payload_policy/fileformats thread-safety contract near
     CacheOptions in src/pcp/cache.hh.

Rules: gate all synchronization on TINYUSDZ_ENABLE_THREAD; keep non-threaded and
wasm builds zero-overhead; never add a mutex member that breaks Layer copy/move
(use shared_ptr<mutex> if needed). After each change, verify with ThreadSanitizer
per doc/datarace.md (build-tsan + `setarch -R`), and run the full unit suite
threads-off and threads-on (expect 0 races in pcp/layer/composition-graph/
tiny-hashmap; the only acceptable races are item #1 in task-queue.hh).
```
