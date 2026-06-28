// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache invalidation, payload loading, and load rules.
// Included inside Cache::Impl.

  std::string ComputeInstanceKey(const Path &path, std::string *warn,
                                 std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    const std::vector<Src> &srcs = SourcesForPath(path, warn, err);
    return ComputeInstanceKeyImpl(srcs);
  }

  // --- invalidation -------------------------------------------------------

  void DropIndex(const std::string &key) {
    auto it = index_to_sites.find(key);
    if (it != index_to_sites.end()) {
      for (const Site &s : it->second) {
        auto sit = site_to_indices.find(s);
        if (sit != site_to_indices.end()) sit->second.erase(key);
      }
      index_to_sites.erase(it);
    }
    index_cache.erase(key);
    sources_cache.erase(key);
    composed_cache_.erase(key);
    composed_children_.erase(key);
    DropInstancing(key);
  }

  void Invalidate(const Path &prim_path) {
    NEXT_PCP_LOCK(api_mu_);
    Invalidate_locked(prim_path);
  }

  // Assumes api_mu_ is already held (called by LoadPayload / UnloadPayload).
  void Invalidate_locked(const Path &prim_path) {
    const std::string base = prim_path.str();
    std::set<std::string> to_drop;

    // index_cache is a std::map (sorted). Use lower_bound to scan only entries
    // at/under base instead of the entire map (Fix #12).
    for (auto it = index_cache.lower_bound(base);
         it != index_cache.end() && IsPathAtOrUnder(it->first, base); ++it) {
      to_drop.insert(it->first);
    }
    for (const auto &kv : site_to_indices) {
      if (IsPathAtOrUnder(kv.first.prim_path, base)) {
        for (const std::string &dep : kv.second) to_drop.insert(dep);
      }
    }
    for (const std::string &k : to_drop) DropIndex(k);

    // sources_cache + deferred state may have descendants not in index_cache.
    for (auto it = sources_cache.begin(); it != sources_cache.end();) {
      if (IsPathAtOrUnder(it->first, base)) it = sources_cache.erase(it);
      else ++it;
    }
    for (auto it = deferred_payload_prims.begin();
         it != deferred_payload_prims.end();) {
      if (IsPathAtOrUnder(*it, base)) it = deferred_payload_prims.erase(it);
      else ++it;
    }
    // Phase 10: drop lazily-composed specs at/under the path.
    for (auto it = composed_cache_.begin(); it != composed_cache_.end();) {
      if (IsPathAtOrUnder(it->first, base)) it = composed_cache_.erase(it);
      else ++it;
    }
    for (auto it = composed_children_.begin(); it != composed_children_.end();) {
      if (IsPathAtOrUnder(it->first, base)) it = composed_children_.erase(it);
      else ++it;
    }
    // Diagnostics are a per-edit-cycle log: an invalidation begins a fresh
    // accumulation (matches GetCompositionIssues' contract; also bounds growth
    // across repeated recompositions). Issues are global, not path-keyed.
    issues_.clear();
  }

  void InvalidateLayer(const std::string &layer_id) {
    NEXT_PCP_LOCK(api_mu_);
    std::set<std::string> to_drop;
    for (const auto &kv : site_to_indices) {
      if (kv.first.layer_id == layer_id) {
        for (const std::string &dep : kv.second) to_drop.insert(dep);
      }
    }
    for (const std::string &k : to_drop) DropIndex(k);
    // spec_cache_ keys are (stack_idx, site), not prim paths; layer contents
    // changed so clear it entirely.
    spec_cache_.clear();
    // Sources and lazy composed specs can be populated without a PrimIndex, so
    // site_to_indices is not a complete dependency map for them. Be conservative
    // until lazy composition records its own layer-site dependencies.
    sources_cache.clear();
    composed_cache_.clear();
    composed_children_.clear();
    issues_.clear();  // fresh diagnostics for the recomposition.
    reg_->Drop(layer_id);
  }

  // --- payloads -----------------------------------------------------------

  bool LoadPayload(const Path &prim_path, bool with_descendants,
                   std::string *warn, std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    if (with_descendants) {
      load_rules_.LoadWithDescendants(prim_path.str());
    } else {
      load_rules_.LoadWithoutDescendants(prim_path.str());
    }
    Invalidate_locked(prim_path);
    ComputePrimIndex_locked(prim_path, warn, err);  // recompose now.
    return true;
  }

  bool UnloadPayload(const Path &prim_path) {
    NEXT_PCP_LOCK(api_mu_);
    load_rules_.Unload(prim_path.str());
    Invalidate_locked(prim_path);
    // Recompose so the deferred-payload set (and HasDeferredPayload) reflects
    // the unload immediately -- Invalidate() drops the deferred entries under
    // the path, and ExpandArcs repopulates them on the next composition.
    std::string warn, err;
    ComputePrimIndex_locked(prim_path, &warn, &err);
    return true;
  }

  void SetLoadRules(const LoadRules &rules) {
    NEXT_PCP_LOCK(api_mu_);
    load_rules_ = rules;
    // A wholesale rule change can affect any prim; drop everything (lazy
    // rebuild). Deferred state is recomputed as prims recompose.
    index_cache.clear();
    sources_cache.clear();
    site_to_indices.clear();
    index_to_sites.clear();
    deferred_payload_prims.clear();
    prototype_by_key.clear();
    prototype_of.clear();
    instances_by_prototype.clear();
    composed_cache_.clear();  // Phase 10
    composed_children_.clear();
    issues_.clear();  // fresh diagnostics for the recomposition.
  }

