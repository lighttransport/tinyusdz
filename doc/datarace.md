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

### TaskQueue lock-free queue dropped/raced payloads — **FIXED**
The old lock-free `TaskQueue`/`TaskQueueFunc` advanced the shared `_write_pos`
*before* writing the slot payload (`_tasks[index] = TaskItem(...)` ran after the
CAS that published the slot). Because consumers key their emptiness check off
`_write_pos`, a consumer could claim and read a slot **before** the producer had
written it — reading a default `TaskItem{nullptr,nullptr}`, advancing past it,
and **silently dropping the task**. Independent of timing, the plain payload
store/load had no release/acquire edge, so it was also a data race (UB). Both
were reproduced: a plain `-O2` build dropped ~1 task per 2M transfers, and TSan
flagged `Push` (`task-queue.hh:100`) vs `Pop` (`:147`) on the same cell. The
only in-tree user is `src/prim-pprint-parallel.cc` (parallel Stage/Layer→USDA
text, gated on `TINYUSDZ_ENABLE_THREAD`, enabled by default at ≥4 prims), where
the symptom would be a rarely dropped/garbled prim in serialized output. The
default and wasm builds compile the parallel path out, so they were unaffected.

Fix: `src/task-queue.hh` is now a **Vyukov bounded MPMC queue** — each cell has
an `std::atomic<size_t> sequence`; a producer writes the payload then publishes
the cell with a release store of the sequence, and a consumer reads the payload
only after an acquire load observes that sequence. That release/acquire pairing
gives a clean happens-before edge, so payloads are never accessed concurrently
and a not-yet-published slot reads as empty (the consumer retries) instead of
being consumed. The buggy `__atomic_*` path and the mutex fallback were dropped
in favour of portable `std::atomic` (also fixes MSVC, which previously fell back
to a lock). Verified: 0 dropped tasks over ~7.2M transfers (`-O2`), TSan-clean,
and all `task_queue_*` unit tests pass.

### Layer composition-flag caches raced — **FIXED**
`Layer`'s `check_unresolved_references/payload/variant/inherits/specializes()`
and `check_over_primspec()` are `const` methods that compute a bool by walking
the (immutable) `_prim_specs` tree and then **cache** it into a `mutable bool`
on `LayerImpl` (`_has_unresolved_*` / `_has_over_primspec`); the matching
`has_*()` readers return those flags. Two threads calling these on the same
shared `Layer` would race on the plain `bool` (UB, even though the computed
value is deterministic/idempotent).

Not reachable today — the parallel pcp build reads `PrimSpec` metadata directly
and never calls these on shared layers — so this was a latent footgun. Fixed by
guarding each flag's store (in `check_*`, after the lock-free tree walk) and each
flag's read (in `has_*`) with the **same gated `LayerImpl::_cache_mu`** already
used for the lookup cache. None of the guarded methods call each other or
`find_primspec_at`, so the non-recursive mutex cannot self-deadlock.
`_has_class_primspec` is fixed at construction (no `check_class_primspec()`
exists) and is left unguarded. Gated on `TINYUSDZ_ENABLE_THREAD`; non-threaded /
wasm builds are byte-for-byte unaffected. Verified: full unit suite passes
threads-off and threads-on (no deadlock/regression).

---

## Audited issues (all resolved)

Numbering preserved for history; every item below is now fixed or documented.

### 1. ~~`TaskQueue::Push`/`Pop` data races~~ — **FIXED**
Resolved by rewriting `src/task-queue.hh` as a Vyukov bounded MPMC queue. See
the *Fixed* section above. `TaskQueue` is not used by pcp (`cache-parallel.cc`
uses `std::thread` + a `std::atomic<size_t>` work index), but the queue itself
is now correct. Kept here, struck through, so the item numbers below stay stable.

### 2. ~~Latent — `Path::variant_part()` / `element_name()` mutable buffers~~ — **FIXED**
Resolved by the `dedup-2026` merge (the `Path` single-buffer rewrite). `Path`
now has **no mutable members**: all parts are `tstring_view` slices into one
eager `_full` buffer (or by-value formatting), so `element_name()` /
`variant_part()` are pure lock-free reads. See the *Fixed* section above.

### 3. ~~Latent — `Layer::check_unresolved_*()` mutable flags~~ — **FIXED**
Resolved by guarding the cached flags with the gated `LayerImpl::_cache_mu`. See
the *Fixed* section above.

### 4. ~~Latent — `Layer` copy carries stale cache pointers~~ — **FIXED**
Resolved by the `dedup-2026` merge (`LayerImpl::reset_lookup_cache()` called
from the copy ctor/assignment, `src/layer.cc:255/297/310`). See the *Fixed*
section above. Kept here, struck through, so the item numbers below stay stable.

### 5. ~~By-design caveat — user callbacks under multithreading~~ — **DOCUMENTED**
The thread-safety contract is now recorded in `src/pcp/cache.hh` on
`CacheOptions::composition` (and cross-referenced from `num_threads`): when
`num_threads != 1`, `composition.payload_policy` and the `fileformats` handlers
run on worker threads, so any state a callback **captures** must be thread-safe.
Not a code bug; an API contract — now stated where users set the options.

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

## Hardening review (round 2)

A second deep pass over the whole parallel surface found **no live data race or
correctness bug**. Re-verified by inspection: Vyukov queue memory ordering and
recycle math; `cache-parallel.cc` per-worker `slots[]` + atomic pull + single-
threaded merge; `BuildEntry` writing only a private `CompositionContext`; the
parallel build touching the shared resolver ONLY via the locked
`LoadLayerThunk → LayerRegistry::GetOrLoad` seam (`DefaultLoadAndOwnLayer` is
bypassed when `load_layer_fn` is set, and `CompositionGraph::LoadPayload` is the
legacy single-threaded engine, not reachable from a worker); `LayerRegistry`
returning a `const Layer*` to a `make_shared`-heap Layer (stable across map
rehash because the map stores `shared_ptr`, not the Layer by value).

Hardening applied:
- **TaskQueue ring is now power-of-two + bitmask** (`src/task-queue.hh`). The
  requested capacity is rounded up to the next power of two and the cell index is
  `pos & (cap-1)` instead of `pos % cap`. This removes the (theoretical) index
  discontinuity when the free-running position counter wraps `size_t` with a
  non-pow2 capacity, drops the modulo, and matches the canonical Vyukov form.
  `capacity == 0/1` round to 1 (a valid SPSC ring). Covered by
  `task_queue_nonpow2_capacity_test`.
- **Cache threading model documented** (`src/pcp/cache.hh` header): a single
  `Cache` is not for concurrent use; only the internal
  `PrewarmPrimIndices()`/`BuildStage()` fan-out parallelizes (and needs
  thread-safe `composition` callbacks); the mutating methods are one-thread-only.

Watch items (safe today; would need attention if a future MT path reaches them):
1. `MapExpr::GetComposed()` writes a `mutable _composed_cache` unsynchronized
   (`src/composition-graph.hh`). Currently dead in the build path (no call-sites)
   and per-worker if exercised. If lazy value resolution later reads SHARED
   cached indices from multiple threads, eager-compose at `AddMapExpression`
   time and drop the mutable. Commented at the declaration.
2. `Layer::_cache_mu` is a `shared_ptr<std::mutex>` that `reset_lookup_cache()`
   REPLACES on copy; lockers bind to `*_cache_mu` without holding a ref. A Layer
   shared across threads for composition must not be copied/copy-assigned/
   mutated/destroyed during that window. We deliberately do not copy the
   shared_ptr per lock (hot-path atomic contention); the contract is documented
   at the member and is honored by the engine (it never copies/mutates a shared
   layer mid-build).
3. `Layer::set/get_asset_resolution_state()` write the mutable
   `_current_working_path` / `_asset_search_paths` / `_asset_resolution_userdata`
   members; only set single-threaded at layer load (under the registry lock), so
   not a live race.

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
# Full suite:
setarch -R ./build-tsan/unit-test-tinyusdz
```

The pcp tests must report **0 data races**. Any race whose stack mentions
`src/pcp/`, `src/layer.cc`, `src/composition-graph.cc`, `src/task-queue.hh`, or
`src/tiny-hashmap.hh` is a regression — including `task_queue_multithreaded_test`,
which is now TSan-clean after the Vyukov rewrite (was item #1).

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
Thread-safety hardening on branch pcp-2026 is COMPLETE. Read doc/datarace.md
first — it records the audited state. Background: tinyusdz::pcp::Cache (src/pcp/)
has an optional multithreaded build path. Every audited item is resolved:

  1. (DONE) TaskQueue::Push/Pop races — fixed by rewriting src/task-queue.hh as
     a Vyukov bounded MPMC queue (per-cell atomic sequence; payload published
     release / consumed acquire). TSan-clean. pcp does not use TaskQueue.
  2. (DONE) Path::variant_part()/element_name() mutable buffers — eliminated by
     the dedup-2026 Path rewrite (single eager _full buffer + tstring_view
     slices; no mutable members, pure lock-free reads).
  3. (DONE) Layer::check_unresolved_*() mutable flags — guarded with the gated
     LayerImpl::_cache_mu (stores in check_*, reads in has_*).
  4. (DONE) Layer copy stale lookup-cache pointers — fixed via
     LayerImpl::reset_lookup_cache() on copy (merged from dedup-2026).
  5. (DONE) payload_policy/fileformats thread-safety contract documented on
     CacheOptions in src/pcp/cache.hh.

There are no known open data races. If you ADD a multithreaded path, the rules
still apply: gate all synchronization on TINYUSDZ_ENABLE_THREAD; keep
non-threaded and wasm builds zero-overhead; never add a mutex MEMBER that breaks
Layer copy/move (use shared_ptr<mutex>). After any change, verify with
ThreadSanitizer per the recipe above (build-tsan + `setarch -R`) and run the full
unit suite threads-off and threads-on; expect 0 races in
pcp/layer/composition-graph/task-queue/tiny-hashmap.

Watch items (safe today, would need guarding if a future MT path touches them on
a SHARED Layer): Layer::set/get_asset_resolution_state() write the mutable
_current_working_path / _asset_search_paths / _asset_resolution_userdata members
— currently only set single-threaded at layer load (registry lock), so not a
live race.
```
