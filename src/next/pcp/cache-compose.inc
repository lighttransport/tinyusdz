// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache opinion composition and lazy prim/index composition.
// Included inside Cache::Impl.

  std::vector<std::string> ComposeInto(const std::vector<Src> &srcs,
                                        PrimSpec *out) {
    ComposeOpinions(srcs, out);
    // Pass 2 (weak->strong): compose child-name ORDER.
    return ComposeChildNames(srcs);
  }
  // ComposeInto's Pass 1 (opinion merge), factored out so the parallel compose
  // can fill PrimSpec opinion slots independently after a serial structure pass
  // has fixed every prim's index/parent/child-order. Writes only into `out`
  // (and interns property names into the shared, locked PropNameTable), so
  // distinct slots fill concurrently without interfering.
  void ComposeOpinions(const std::vector<Src> &srcs, PrimSpec *out) {
    bool specifier_set = false;

    // Pass 1 (strong->weak): compose opinions.
    for (const Src &s : srcs) {
      // Variant source: graft the selected variant's inline opinions
      // (properties + relationships) with fill-absent semantics. Variant child
      // prims are not yet modeled (VariantData has no child storage).
      if (s.variant) {
        // Prim-state opinions from the selected option (authored, so a
        // weaker source's default cannot flip them back).
        if (!s.variant->active) {
          out->meta().active = false;
          out->meta().active_authored = true;
        }
        if (s.variant->hidden) {
          out->meta().hidden = true;
          out->meta().hidden_authored = true;
        }
        if (!s.variant->doc.empty() && out->meta().doc().empty()) {
          out->meta().doc() = s.variant->doc;
        }
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

      const std::vector<SpecRef> &specs = Specs(s.stack_idx, s.site);
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
        Compositor::CopyLocalOpinions(
            *out, *spec, s.offset.offset, s.offset.scale,
            [&](const std::string &p) {
              // Target paths outside a reference/payload arc's namespace are
              // unmappable: return "" so CopyLocalOpinions drops them (pxr
              // behavior). VARIANT arcs are namespace-preserving — targets in
              // a variant body are authored in the HOST's namespace (outside
              // the holder/content prefix by construction), so map leniently.
              if (s.arc_kind == ArcType::Variant) {
                return Mapping(s.map_idx).Apply(p);
              }
              return Mapping(s.map_idx).ApplyTarget(p);
            });
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

  // Composed child-name ORDER for a prim's sources (weak->strong): iterate sources
  // and their layer-stack specs in reverse (weakest first); within a single layer
  // the primChildren are in authored (forward) order. Append unseen names. This is
  // ComposeInto's Pass 2, factored out so the parallel source-warming pre-pass
  // discovers exactly the same child namespace the serial compose walks.
  std::vector<std::string> ComposeChildNames(const std::vector<Src> &srcs) {
    std::vector<std::string> child_names;
    std::set<std::string> seen_child;
    for (auto sit = srcs.rbegin(); sit != srcs.rend(); ++sit) {
      const Src &s = *sit;
      if (s.variant) continue;  // variant child prims not modeled yet
      const std::vector<SpecRef> &specs = Specs(s.stack_idx, s.site);
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

  // --- lazy per-prim composition (Phase 10) -------------------------------

  // Compose just `prim_path` on first access (cached), reusing the exact
  // source-resolution + opinion-merge of BuildStageRec's per-prim step, but
  // without walking/emitting the whole namespace. Returns nullptr if the prim
  // authors no opinions. Assumes api_mu_ is held.
  const PrimSpec *ComposePrim_locked(const Path &prim_path, std::string *warn,
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

  // --- ComputePrimIndex ---------------------------------------------------

  // Public entry: takes the lock once, then runs the lock-free worker.
  const PrimIndex *ComputePrimIndex(const Path &prim_path, std::string *warn,
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

  // Assumes api_mu_ is already held. Internal callers (LoadPayload /
  // UnloadPayload / the serial PrewarmPrimIndices path) use this directly so the
  // lock is never re-entered (F6: the mutex is non-recursive).
  const PrimIndex *ComputePrimIndex_locked(const Path &prim_path,
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
