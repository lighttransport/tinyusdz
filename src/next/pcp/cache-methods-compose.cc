// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache method definitions (compose + stage build/fill + layer-stack + invalidation + merge).
// Split out of cache.cc so these translation units compile in parallel; see
// cache-internal.hh for the Cache::Impl declaration.

#include "cache-internal.hh"

#include <cstdlib>  // getenv (incremental-srcfree kill-switch)

namespace tinyusdz {
namespace next {
namespace pcp {

  std::vector<std::string> Cache::Impl::ComposeInto(const std::vector<Src> &srcs,
                                        PrimSpec *out) {
    ComposeOpinions(srcs, out);
    // Pass 2 (weak->strong): compose child-name ORDER.
    return ComposeChildNames(srcs);
  }

  void Cache::Impl::ComposeOpinions(const std::vector<Src> &srcs, PrimSpec *out) {
    bool specifier_set = false;

    // Pass 1 (strong->weak): compose opinions.
    for (const Src &s : srcs) {
      // Variant source: graft the selected variant's inline opinions
      // (properties + relationships) with fill-absent semantics. Variant child
      // prims are not yet modeled (VariantData has no child storage).
      if (s.variant) {
        for (const auto &pr : s.variant->properties) {
          const PropSlot *ts = out->property(pr.name);
          if (!ts) {
            out->add_property(pr.name, pr.value, pr.flags);
          } else if (ts->value_offset == UINT32_MAX) {
            // Field-level fill-absent (see CopyLocalOpinions): a variant default
            // fills a stronger connection-only / declared-only slot.
            out->fill_property_value_if_absent(
                GetPropNameTable().intern(pr.name), pr.value);
          }
        }
        for (const auto &rp : s.variant->relationships) {
          if (out->relationship(rp.first)) continue;
          for (const Path &t : rp.second) {
            out->add_relationship(rp.first, Path(Mapping(s.map_idx).Apply(t.str())));
          }
        }
        continue;
      }

      const std::vector<SpecRef> &specs = SpecsFor(s);
      if (specs.empty()) continue;

      for (const SpecRef &sr : specs) {
        const PrimSpec *spec = sr.spec;

        if (!specifier_set) {
          out->set_specifier(spec->specifier());
          specifier_set = true;
        }

        // Remap relationship/connection TARGET paths from this arc's
        // (site-local) namespace into the composed namespace, so a referenced
        // asset's internal targets (e.g. material:binding, .connect) resolve to
        // their flattened paths. Identity for local opinions.
        const NamespaceMapping &nm = Mapping(s.map_idx);
        Compositor::CopyLocalOpinions(
            *out, *spec, s.offset.offset, s.offset.scale,
            {[](const std::string &p, void *user) {
               return static_cast<const NamespaceMapping *>(user)->Apply(p);
             },
             const_cast<NamespaceMapping *>(&nm)});
      }
    }

    // Flatten drops variant SELECTION metadata: the selected variant's content
    // has already been grafted inline (ExpandArcs / variant Src), so pxr's
    // flattened output carries no `variants = {...}` or variantSets (usdcat emits
    // zero). Strip the vestigial selection that rode along via CopyLocalOpinions.
    // Use a const view for the emptiness checks so we never allocate the meta ext
    // just to clear an absent field.
    {
      const PrimSpecMeta &cm = out->meta();
      if (!cm.variantSelection.empty()) out->meta().variantSelection.clear();
      if (!cm.variantSets().empty()) out->meta().variantSets().clear();
      if (!cm.variantSelections().empty()) out->meta().variantSelections().clear();
    }
  }

  std::vector<std::string> Cache::Impl::ComposeChildNames(const std::vector<Src> &srcs) {
    std::vector<std::string> child_names;
    std::set<std::string> seen_child;
    for (auto sit = srcs.rbegin(); sit != srcs.rend(); ++sit) {
      const Src &s = *sit;
      if (s.variant) continue;  // variant child prims not modeled yet
      const std::vector<SpecRef> &specs = SpecsFor(s);
      for (auto rit = specs.rbegin(); rit != specs.rend(); ++rit) {
        const PrimSpec *spec = rit->spec;
        const Layer *layer = rit->layer;
        for (uint32_t ci : spec->child_indices()) {
          const PrimSpec *cs = layer->prim(ci);
          if (!cs) continue;
          // Bracketed variant HOLDER prims ("{set=sel}") are namespace markers,
          // not real children -- the selected holder's descendants are grafted
          // onto the host as a Variant source (see ExpandArcs). Skip the markers
          // so they never leak into the composed child list.
          if (!cs->name().empty() && cs->name().front() == '{') continue;
          if (seen_child.insert(cs->name()).second) {
            child_names.push_back(cs->name());
          }
        }
      }
    }
    return child_names;
  }

  const PrimSpec *Cache::Impl::ComposePrim_locked(const Path &prim_path, std::string *warn,
                                     std::string *err) {
    const std::string key = prim_path.str();
    auto it = composed_cache_.find(key);
    if (it != composed_cache_.end()) return it->second.get();

    const std::vector<Src> &srcs = SourcesForPath(prim_path, warn, err);
    if (!AnyAuthors(srcs)) return nullptr;

    auto spec = std::unique_ptr<PrimSpec>(new PrimSpec(prim_path.name()));
    spec->set_path(prim_path);
    std::vector<std::string> children = ComposeInto(srcs, spec.get());

    const PrimSpec *ret = spec.get();
    composed_cache_.emplace(key, std::move(spec));
    composed_children_[key] = std::move(children);
    return ret;
  }

  const PrimIndex *Cache::Impl::ComputePrimIndex(const Path &prim_path, std::string *warn,
                                    std::string *err) {
#if defined(TINYUSDZ_ENABLE_THREAD) && defined(TINYUSDZ_NEXT_FINE_LOCKS)
    // F4 fast path: a shared lock lets cache hits resolve concurrently. On a
    // miss we fall through to the exclusive build; ComputePrimIndex_locked
    // re-checks index_cache under the write lock, so a prim contended by many
    // threads is still built exactly once.
    {
      NEXT_PCP_READ_LOCK(api_mu_);
      auto it = index_cache.find(prim_path.str());
      if (it != index_cache.end()) return it->second.get();
    }
#endif
    NEXT_PCP_WRITE_LOCK(api_mu_);
    return ComputePrimIndex_locked(prim_path, warn, err);
  }

  const PrimIndex *Cache::Impl::ComputePrimIndex_locked(const Path &prim_path,
                                           std::string *warn, std::string *err) {
    const std::string key = prim_path.str();
    auto it = index_cache.find(key);
    if (it != index_cache.end()) return it->second.get();

    const std::vector<Src> &srcs = SourcesForPath(prim_path, warn, err);
    if (!AnyAuthors(srcs)) {
      return nullptr;  // prim does not exist in the composed namespace.
    }

    auto index = std::unique_ptr<PrimIndex>(new PrimIndex());
    index->SetPath(prim_path);
    index->SetLayerStacks(&layer_stacks);

    std::vector<uint16_t> order;
    std::vector<Site> sites;
    for (size_t i = 0; i < srcs.size(); ++i) {
      const Src &s = srcs[i];
      CompNode n;
      n.arc_type = (i == 0) ? ArcType::Root : s.arc_kind;
      n.parent = (i == 0) ? 0xFFFF : 0;
      n.layer_stack_idx = s.stack_idx;
      n.site_path_idx = InternPath(s.site);
      n.map_to_root = Mapping(s.map_idx);
      n.offset = s.offset;
      const std::vector<SpecRef> &specs = Specs(s.stack_idx, s.site);
      if (!specs.empty()) {
        n.flags |= NodeFlags::HasSpecs;
        for (const SpecRef &sr : specs) {
          sites.push_back(Site{sr.layer_id.empty()
                                   ? layer_stacks[s.stack_idx].identifier
                                   : sr.layer_id,
                               s.site});
        }
      }
      uint16_t ni = index->AddNode(std::move(n));
      if (ni == PrimIndex::kInvalidNode) {
        AddIssue(ErrorCode::IndexCapacityExceeded, key,
                 "PrimIndex node count exceeds uint16 capacity for " + key, err);
        return nullptr;
      }
      if (i != 0) index->MutableNode(0).children.push_back(ni);
      order.push_back(ni);
    }
    index->SetStrengthOrder(std::move(order));
    // Bind the path table AFTER the node loop interned every site, so the
    // size snapshot covers all of this index's site_path_idx values (F3).
    index->SetPathTable(&path_table, path_table.size());

    // Per-prim cache: pick the prototype deterministically (min path) so the
    // grouping does not depend on the order/threads of ComputePrimIndex (F5).
    RegisterInstance(key, srcs, /*prefer_min=*/true);

    for (const Site &site : sites) site_to_indices[site].insert(key);
    index_to_sites[key] = std::move(sites);

    const PrimIndex *ret = index.get();
    index_cache.emplace(key, std::move(index));
    return ret;
  }

  void Cache::Impl::BuildStage(Layer *out, const BuildStageWork &root_work,
                  std::string *warn, std::string *err) {
    using ProfClock = std::chrono::steady_clock;
    const bool prof = options.enable_timing;
    std::vector<BuildStageWork> stack;
    stack.push_back(root_work);

    while (!stack.empty()) {
      BuildStageWork w = stack.back();
      stack.pop_back();

      // Namespace-depth backstop: an arc-induced namespace that keeps growing
      // (e.g. an ancestor cycle a stronger check missed) must surface as an
      // error, never as C++ stack exhaustion.
      if (w.depth > options.max_namespace_depth) {
        AddIssue(ErrorCode::MaxDepthExceeded, w.out_path.str(),
                 "BuildStage max namespace depth exceeded at: " + w.out_path.str(),
                 err);
        continue;
      }
      auto ps0 = prof ? ProfClock::now() : ProfClock::time_point{};
      const std::vector<Src> &srcs = SourcesForPath(w.src_path, warn, err);
      auto ps1 = prof ? ProfClock::now() : ProfClock::time_point{};

      // STRUCTURE PASS: fix this prim's slot, parent, child order and instancing
      // now, but DEFER opinion composition to the parallel fill below. Child-name
      // order (ComposeChildNames) and instancing (RegisterInstance) depend only on
      // `srcs`, never on the prim's own composed opinions, so deferring opinions
      // does not change the tree the walk produces.
      PrimSpec spec(w.out_path.name());
      spec.set_path(w.out_path);
      std::vector<std::string> children = ComposeChildNames(srcs);
      auto ps2 = prof ? ProfClock::now() : ProfClock::time_point{};
      if (prof) {
        prof_sources_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ps1 - ps0).count();
        prof_compose_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ps2 - ps1).count();
      }

      // Instancing: register this prim. If it is an instance (not the prototype
      // of its group), link it to the prototype and do NOT duplicate its subtree
      // -- children are provided by the prototype (UsdPrim follows
      // instance_prototype).
      auto pr0 = prof ? ProfClock::now() : ProfClock::time_point{};
      RegisterInstance(w.out_path.str(), srcs);
      if (prof) prof_reg_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ProfClock::now() - pr0).count();
      bool is_instance = false;
      {
        auto pit = prototype_of.find(w.out_path.str());
        if (pit != prototype_of.end() && pit->second != w.out_path.str()) {
          spec.meta().instance_prototype() = pit->second;
          is_instance = true;
        }
      }

      uint32_t idx = out->add_prim(std::move(spec));
      if (w.is_root) {
        out->add_root(idx);
      } else {
        out->set_parent(idx, w.parent_idx);
      }
      // Opinions for this slot are filled (in parallel) after the whole walk.
      fill_.push_back({idx, w.src_path.str()});

      if (is_instance) continue;  // prototype provides the subtree

      // Push children in reverse order so the first child processes first
      // (preserving the original visitation order).
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const std::string &cn = *it;
        Path child_src = w.src_path.append_child(cn);
        Path child_out;
        auto rit = relocates_map.find(child_src.str());
        if (rit != relocates_map.end() &&
            Path(rit->second).parent().str() == w.out_path.str()) {
          child_out = Path(rit->second);
        } else {
          child_out = w.out_path.append_child(cn);
        }
        stack.push_back({child_src, child_out, idx, /*is_root=*/false,
                         w.depth + 1});
      }
    }
  }

  void Cache::Impl::FillOpinions(Layer *out) {
    // The serial structure pass has parsed every referenced layer, so the full
    // set of property names is now interned. Freeze the name table: the parallel
    // fill below does heavy find() (and intern() that all HIT) on it, and the
    // per-call shared_lock otherwise contends purely on the lock's cache line.
    // Frozen, those go lock-free. A genuinely new name would unfreeze and fall
    // back to locking.
    GetPropNameTable().freeze();

    // Move each slot's composition sources OUT of the shared sources_cache into a
    // fill-indexed vector, then drop the whole map. The structure walk is done,
    // so nothing calls SourcesForPath again; each fill slot's src_path is unique
    // (one namespace path visited once -> no two slots share an entry), so a move
    // is exact. Dropping the map frees its 280k+ path-string KEYS up front, and
    // the parallel fill below frees each slot's Src vector the instant its
    // opinions are composed -- so the ~600 MB of transient composition state
    // shrinks to zero as the output fills in, instead of coexisting in full with
    // the finished stage (the pcp compose-phase memory wall). Safe: sources_cache
    // is a pure cache (SourcesForPath rebuilds from root on any later miss), and a
    // moved Src keeps its specs_ pointer into the still-live spec_cache.
#if !defined(_WIN32)
    static const bool no_srcfree =
        std::getenv("NEXT_NO_INCREMENTAL_SRCFREE") != nullptr;
#else
    const bool no_srcfree = true;  // extraction pass below is skipped
#endif
    std::vector<std::vector<Src>> fill_srcs;
    if (!no_srcfree) {
      fill_srcs.resize(fill_.size());
      for (size_t i = 0; i < fill_.size(); ++i) {
        auto it = sources_cache.find(fill_[i].second);
        if (it != sources_cache.end()) fill_srcs[i] = std::move(it->second);
      }
      std::unordered_map<std::string, std::vector<Src>>().swap(sources_cache);
    }

    auto do_range = [&](size_t b, size_t e) {
      for (size_t i = b; i < e; ++i) {
        if (no_srcfree) {
          auto it = sources_cache.find(fill_[i].second);
          if (it == sources_cache.end()) continue;
          if (PrimSpec *ps = out->prim_mutable(fill_[i].first)) {
            ComposeOpinions(it->second, ps);
          }
          continue;
        }
        if (fill_srcs[i].empty()) continue;
        if (PrimSpec *ps = out->prim_mutable(fill_[i].first)) {
          ComposeOpinions(fill_srcs[i], ps);
        }
        std::vector<Src>().swap(fill_srcs[i]);  // free this slot's sources now
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

  void Cache::Impl::AddIssue(ErrorCode code, const std::string &site,
                const std::string &message, std::string *err) {
    issues_.push_back(CompositionIssue{code, site, message});
    if (err) *err += message + "\n";
  }

  LayerLoadOptions Cache::Impl::MakeLayerLoadOptions() const {
    LayerLoadOptions out;
    out.max_memory = options.max_layer_memory;
    out.enable_usdc_timing = options.enable_timing;
    return out;
  }

  uint32_t Cache::Impl::InternLayerStack(std::shared_ptr<Layer> layer,
                            const std::string &identifier, std::string *warn,
                            std::string *err) {
    auto it = stack_by_id.find(identifier);
    if (it != stack_by_id.end()) return it->second;

    LayerStack st;
    st.identifier = identifier;
    std::set<std::string> visiting;
    visiting.insert(identifier);
    if (!AppendLayerAndSublayers(st, layer, identifier, &visiting, 0, warn,
                                 err)) {
      return kInvalidStack;
    }

    uint32_t idx = static_cast<uint32_t>(layer_stacks.size());
    layer_stacks.push_back(std::move(st));
    stack_by_id.emplace(identifier, idx);
    return idx;
  }

  bool Cache::Impl::AppendLayerAndSublayers(LayerStack &st, std::shared_ptr<Layer> layer,
                               const std::string &identifier,
                               std::set<std::string> *visiting,
                               uint32_t depth, std::string *warn,
                               std::string *err) {
    if (!layer) return false;
    if (depth > options.max_depth) {
      AddIssue(ErrorCode::MaxDepthExceeded, identifier,
               "Sublayer max depth exceeded at: " + identifier, err);
      return false;
    }
    st.layers.push_back(layer);
    st.layer_identifiers.push_back(identifier);
    // Anchor = the referencing layer's FILE path; AssetResolver::Resolve takes
    // its directory internally. (Passing a pre-stripped dir double-strips and
    // breaks deep relative paths like `../../Materials/x.usd`.)
    const std::string &anchor = identifier;
    for (const std::string &sub : layer->meta().subLayers) {
      const std::string sub_id = resolver->ResolvePath(sub, anchor);
      if (sub_id.empty()) {
        AddIssue(ErrorCode::InvalidAssetPath, sub,
                 "Failed to resolve sublayer: " + sub, err);
        if (options.error_when_asset_not_found) return false;
        continue;
      }
      if (visiting && visiting->count(sub_id)) {
        AddIssue(ErrorCode::SublayerCycle, sub_id,
                 "Sublayer cycle detected at: " + sub_id, err);
        return false;
      }
      std::shared_ptr<Layer> sl =
          reg_->GetOrLoad(*resolver, sub, anchor, warn, err,
                          MakeLayerLoadOptions());
      if (sl) {
        if (visiting) visiting->insert(sub_id);
        if (!AppendLayerAndSublayers(st, sl, sub_id, visiting, depth + 1,
                                     warn, err)) {
          if (visiting) visiting->erase(sub_id);
          return false;
        }
        if (visiting) visiting->erase(sub_id);
      } else if (options.error_when_asset_not_found) {
        return false;
      }
    }
    return true;
  }

  std::string Cache::Impl::ComputeInstanceKey(const Path &path, std::string *warn,
                                 std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    const std::vector<Src> &srcs = SourcesForPath(path, warn, err);
    return ComputeInstanceKeyImpl(srcs);
  }

  void Cache::Impl::DropIndex(const std::string &key) {
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

  void Cache::Impl::Invalidate(const Path &prim_path) {
    NEXT_PCP_LOCK(api_mu_);
    Invalidate_locked(prim_path);
  }

  void Cache::Impl::Invalidate_locked(const Path &prim_path) {
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

  void Cache::Impl::InvalidateLayer(const std::string &layer_id) {
    NEXT_PCP_LOCK(api_mu_);
    std::set<std::string> to_drop;
    for (const auto &kv : site_to_indices) {
      if (kv.first.layer_id == layer_id) {
        for (const std::string &dep : kv.second) to_drop.insert(dep);
      }
    }
    for (const std::string &k : to_drop) DropIndex(k);
    // spec cache keys are (stack_idx, site), not prim paths; layer contents
    // changed so clear it entirely.
    spec_cache_by_stack_.clear();
    // Sources and lazy composed specs can be populated without a PrimIndex, so
    // site_to_indices is not a complete dependency map for them. Be conservative
    // until lazy composition records its own layer-site dependencies.
    sources_cache.clear();
    composed_cache_.clear();
    composed_children_.clear();
    issues_.clear();  // fresh diagnostics for the recomposition.
    reg_->Drop(layer_id);
  }

  bool Cache::Impl::LoadPayload(const Path &prim_path, bool with_descendants,
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

  bool Cache::Impl::UnloadPayload(const Path &prim_path) {
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

  void Cache::Impl::SetLoadRules(const LoadRules &rules) {
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

#if defined(TINYUSDZ_ENABLE_THREAD)
  void Cache::Impl::MergeWorkerIndex(Impl &w, const std::string &key) {
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

  void Cache::Impl::MergeSources(Impl &w, size_t seed_stack_count, size_t seed_pool_count) {
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
#endif

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
