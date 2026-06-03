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
at ~L476, released for the lock-free `_prim_specs` tree walk at ~L494, re-taken
for the insert at ~L505). Gated on `TINYUSDZ_ENABLE_THREAD`, so non-threaded /
wasm builds are byte-for-byte unaffected. `shared_ptr` keeps `Layer`
copyable/movable. Covered by `pcp_singlethread_vs_multithread_identical_test`
and `pcp_mt_shared_reference_test`.

### TaskQueue / TaskQueueFunc payload publication — **FIXED**
The lock-free ring claimed a slot via CAS on `_write_pos`/`_read_pos` and only
*then* stored/loaded the `TaskItem` payload (`src/task-queue.hh` Push/Pop), so
the payload access was unordered w.r.t. the position publish → a consumer could
read a slot before the producer wrote it. TSan flagged this in
`task_queue_multithreaded_test`. (pcp itself never uses `TaskQueue`; the
producer here is `src/prim-pprint-parallel.cc`.)

Fix: rewrote both queues as a Dmitry Vyukov bounded MPMC queue. Each ring slot
carries a `std::atomic<uint64_t> seq`; the producer stores the payload then
`seq.store(release)`, and the consumer `seq.load(acquire)` before reading — so
publication is correctly ordered. Single `std::atomic`-based implementation
(replaces the hand-rolled `__atomic_*` / mutex-fallback dual path); public API
(`Push`/`Pop`/`Size`/`Empty`/`Capacity`/`Clear`) unchanged. The i386 64-bit
atomic test skip stays (std::atomic is still correct there, just not lock-free).
TSan-clean in `task_queue_multithreaded_test`.

### Path::variant_part() / element_name() mutable buffers — **FIXED**
Both `const` accessors wrote `mutable` string buffers (`_variant_part_str`,
`_element`), racing if called on a shared `Path` from two threads.

Fix: both now return `std::string` **by value**. `variant_part()` builds the
string locally (the `mutable _variant_part_str` member is removed entirely).
`element_name()` returns the construction-time `_element` (now a plain,
non-`mutable` member written only by non-const `_update`/append paths) or
computes the fallback locally without writing — so no `const` mutation remains.
`Path::operator==` compares `full_path_name()` and never depended on the removed
lazy side-effect.

### Layer::check_unresolved_*() mutable flags — **FIXED**
The six `const` compute-and-cache methods (`check_unresolved_references` … and
`check_over_primspec`) and their `has_unresolved_*()` getters touched
`mutable bool _has_*` flags without synchronization. Now each write and each
getter read is wrapped in a `std::lock_guard` on the gated `LayerImpl::_cache_mu`
(`#if defined(TINYUSDZ_ENABLE_THREAD)`), reusing the same mutex as the lookup
cache. Zero overhead when threads are off.

### Layer copy carries stale cache pointers + shared mutex — **FIXED**
`Layer::Layer(const Layer&)` / `operator=` copied `*other._impl` wholesale,
which (a) copied `_primspec_path_cache` (`const PrimSpec*` into the *source's*
tree) and (b) copied the `shared_ptr<mutex> _cache_mu` **by value**, so a copy
*shared the source's mutex*. Both copy paths now reset the lookup cache on the
new impl: `_dirty = true`, `_primspec_path_cache.clear()`, and (gated) a fresh
`_cache_mu = std::make_shared<std::mutex>()`.

### Callback thread-safety contract — **DOCUMENTED**
`CompositionGraphOptions::payload_policy` and `::fileformats` (in
`src/composition-graph.hh`) and `pcp::CacheOptions::composition` /
`PrewarmPrimIndices` / `BuildStage` (in `src/pcp/cache.hh`) now carry doc
comments stating that, with `num_threads != 1`, these callbacks run concurrently
on worker threads and must be thread-safe. No code change.

---

## Remaining issues

None. All audited items are fixed or documented above. The full unit suite is
TSan-clean (see *Reproducing*).

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

The **entire** suite must now report **0 data races** — including
`task_queue_multithreaded_test`. Any race whose stack mentions `src/pcp/`,
`src/layer.cc`, `src/composition-graph.cc`, `src/task-queue.hh`, or
`src/tiny-hashmap.hh` is a regression in this work.

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

## Maintenance note

All audited items are resolved; the full unit suite is TSan-clean. Rules to keep
in mind when touching this code in the future:

- Gate all synchronization on `TINYUSDZ_ENABLE_THREAD`; keep non-threaded and
  wasm builds zero-overhead.
- Never add a mutex member that breaks `Layer` copy/move — use
  `shared_ptr<mutex>` (and reset it in `Layer`'s copy paths so copies don't share
  it).
- Don't add new `mutable` compute-and-cache state to `Path` / `Layer` / `PrimSpec`
  that a `const` accessor writes; either compute by value or guard it with the
  gated `LayerImpl::_cache_mu`.
- After any change, verify with ThreadSanitizer per the *Reproducing* section
  (build-tsan + `setarch -R`) and run the full unit suite threads-off and
  threads-on. Expect **0 races** across the whole suite.
