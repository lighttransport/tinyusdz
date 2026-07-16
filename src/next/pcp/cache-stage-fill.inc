// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache stage opinion-fill pass.
// Included inside Cache::Impl.

  // Per-prim opinion records collected by the structure pass, drained by
  // FillOpinions. Member (not local) so a single fill can cover every root.
  std::vector<std::pair<uint32_t, std::string>> fill_;

  // OPINION-FILL PASS: compose each slot's opinions from its (already cached)
  // sources. Slots are disjoint PrimSpec objects, sources_cache is only read, and
  // add_prim is done -- so distinct slots fill concurrently. Byte-identical to
  // the serial ComposeInto: the same ComposeOpinions runs on the same (slot,
  // srcs) pairs; only the order across independent slots changes.
  void FillOpinions(Layer *out) {
    // The serial structure pass has parsed every referenced layer, so the full
    // set of property names is now interned. Freeze the name table: the parallel
    // fill below does heavy find() (and intern() that all HIT) on it, and the
    // per-call shared_lock otherwise contends purely on the lock's cache line.
    // Frozen, those go lock-free. A genuinely new name would unfreeze and fall
    // back to locking.
    GetPropNameTable().freeze();
    auto do_range = [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        const std::vector<Src> *srcs = sources_cache.find(fill_[i].second);
        if (!srcs) continue;
        if (PrimSpec *ps = out->prim_mutable(fill_[i].first)) {
          ComposeOpinions(*srcs, ps);
        }
      }
    };
#if defined(TINYUSDZ_ENABLE_THREAD)
    unsigned nt = options.num_threads;
    if (nt > 1 && fill_.size() >= 2048) {
      if (nt > 64) nt = 64;
      std::vector<std::thread> pool;
      pool.reserve(nt);
      for (unsigned t = 0; t < nt; ++t) {
        size_t b = fill_.size() * t / nt;
        size_t e = fill_.size() * (t + 1) / nt;
        if (b < e) pool.emplace_back([&do_range, b, e]() { do_range(b, e); });
      }
      for (std::thread &th : pool) th.join();
      return;
    }
#endif
    do_range(0, fill_.size());
  }
