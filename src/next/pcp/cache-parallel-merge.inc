// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache parallel worker merge helpers.
// Included inside Cache::Impl.

  void MergeWorkerIndex(Impl &w, const std::string &key) {
    auto wit = w.index_cache.find(key);
    if (wit == w.index_cache.end()) return;
    if (index_cache.count(key)) return;
    std::unique_ptr<PrimIndex> idx = std::move(wit->second);

    std::unordered_map<uint32_t, uint32_t> stack_remap, path_remap;
    const size_t n = idx->GetNodes().size();
    for (size_t i = 0; i < n; ++i) {
      CompNode &node = idx->MutableNode(static_cast<uint16_t>(i));
      auto sr = stack_remap.find(node.layer_stack_idx);
      if (sr == stack_remap.end()) {
        uint32_t m = AdoptStack(w.layer_stacks[node.layer_stack_idx]);
        stack_remap.emplace(node.layer_stack_idx, m);
        node.layer_stack_idx = m;
      } else {
        node.layer_stack_idx = sr->second;
      }
      auto pr = path_remap.find(node.site_path_idx);
      if (pr == path_remap.end()) {
        uint32_t m = InternPath(w.path_table[node.site_path_idx]);
        path_remap.emplace(node.site_path_idx, m);
        node.site_path_idx = m;
      } else {
        node.site_path_idx = pr->second;
      }
    }
    idx->SetLayerStacks(&layer_stacks);
    idx->SetPathTable(&path_table, path_table.size());

    auto sit = w.index_to_sites.find(key);
    if (sit != w.index_to_sites.end()) {
      for (const Site &s : sit->second) site_to_indices[s].insert(key);
      index_to_sites[key] = sit->second;
    }

    index_cache.emplace(key, std::move(idx));

    if (options.detect_instances) {
      auto piit = w.pending_instance_.find(key);
      if (piit != w.pending_instance_.end() && piit->second.first) {
        AssignPrototype(key, piit->second.second, /*prefer_min=*/true);
      }
    }
  }

  void MergeSources(Impl &w, size_t seed_stack_count, size_t seed_pool_count) {
    std::unordered_map<uint32_t, uint32_t> stack_remap;
    auto remap = [&](uint32_t ws) -> uint32_t {
      if (ws < seed_stack_count) return ws;
      auto it = stack_remap.find(ws);
      if (it != stack_remap.end()) return it->second;
      uint32_t m = AdoptStack(w.layer_stacks[ws]);
      stack_remap.emplace(ws, m);
      return m;
    };
    std::unordered_map<uint32_t, uint32_t> map_remap;
    auto remap_map = [&](uint32_t wm) -> uint32_t {
      if (wm < seed_pool_count) return wm;
      auto it = map_remap.find(wm);
      if (it != map_remap.end()) return it->second;
      uint32_t m = InternMapping(w.nm_pool_[wm]);
      map_remap.emplace(wm, m);
      return m;
    };
    for (auto &kv : w.sources_cache) {
      if (sources_cache.count(kv.first)) continue;
      // The worker Impl is discarded right after this merge and warms subtrees
      // disjoint from the other workers (shared seeded keys are skipped above),
      // so steal its Src vector instead of copying it.
      std::vector<Src> srcs = std::move(kv.second);
      for (Src &s : srcs) {
        s.stack_idx = remap(s.stack_idx);
        s.map_idx = remap_map(s.map_idx);
        // A worker-set specs_ points into the (discarded) worker spec cache and
        // the stack_idx just changed; drop it so main re-resolves via SpecsFor.
        s.specs_ = nullptr;
      }
      sources_cache.emplace(kv.first, std::move(srcs));
    }
  }
