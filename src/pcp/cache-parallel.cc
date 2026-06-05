// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// cache-parallel.cc - optional multithreaded batch building for pcp::Cache.
//
// The entire worker machinery is gated on TINYUSDZ_ENABLE_THREAD and compiled
// out on wasm. When threads are disabled this translation unit is essentially
// empty and Cache::Impl::BuildParallel is never referenced (PrewarmPrimIndices
// only calls it inside the same #if), so there is no missing-symbol concern.
//
// Design (mirrors the per-index "no shared mutable state in the parallel
// region" approach of src/prim-pprint-parallel.cc): each worker builds an
// independent prim index into its own pre-sized output slot via
// Cache::Impl::BuildEntry (which touches only the internally-locked
// LayerRegistry and read-only inputs). After the join barrier, a
// single-threaded deterministic merge folds the finished entries into the cache
// in input order. This guarantees single-thread and multi-thread runs produce
// identical caches.
//
#include "pcp/cache.hh"

#if defined(TINYUSDZ_ENABLE_THREAD) && !defined(__EMSCRIPTEN__)

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "pcp/cache-impl.hh"

namespace tinyusdz {
namespace pcp {

nonstd::expected<bool, std::string> Cache::Impl::BuildParallel(
    const std::vector<Path> &paths, size_t num_threads, std::string *warn,
    std::string *err) {
  (void)warn;
  (void)err;

  struct Slot {
    Path path;
    std::shared_ptr<CachedPrimIndex> entry;  // null if skipped/failed
  };

  std::vector<Slot> slots(paths.size());
  for (size_t i = 0; i < paths.size(); i++) {
    slots[i].path = paths[i];
  }

  // Pull work items by atomically incrementing a shared index. Each worker
  // writes only to its own slot -> no shared mutable state in this region
  // (the LayerRegistry, reached via BuildEntry, locks internally).
  std::atomic<size_t> next_idx{0};
  const size_t count = slots.size();

  auto worker = [&]() {
    for (;;) {
      size_t i = next_idx.fetch_add(1, std::memory_order_relaxed);
      if (i >= count) break;
      std::string w, e;
      auto built = BuildEntry(slots[i].path, &w, &e);
      if (built) {
        slots[i].entry = built.value();
      }
    }
  };

  if (num_threads < 1) num_threads = 1;
  std::vector<std::thread> workers;
  workers.reserve(num_threads);
  for (size_t t = 0; t < num_threads; t++) {
    workers.emplace_back(worker);
  }
  for (auto &t : workers) {
    t.join();
  }

  // Deterministic merge (input order): identical to the sequential path.
  for (auto &s : slots) {
    if (!s.entry) continue;
    const std::string key = s.path.prim_part();
    if (index_cache.find(key) != index_cache.end()) {
      continue;  // already cached (keep the existing entry)
    }
    index_cache[key] = s.entry;
    RegisterDependencies(key, *s.entry);
  }

  return true;
}

}  // namespace pcp
}  // namespace tinyusdz

#endif  // TINYUSDZ_ENABLE_THREAD && !__EMSCRIPTEN__
