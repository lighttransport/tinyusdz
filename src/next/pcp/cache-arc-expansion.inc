// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache arc expansion.
// Included inside Cache::Impl.

  // --- arc expansion (inherits/variants/references/payloads/specializes) ---

  // Resolve+recurse an external/internal arc target from source `src`.
  // `kind` is the arc type; `out` collects ordinary opinions and `spec_out`
  // collects specialize-derived opinions (globally weakest). A specialize arc
  // (and its entire subtree) routes into `spec_out`.
  // Unwrap a synthetic variant-content layer identifier
  // ("variant:<hostfile>:<site>:<vset>:<vname>", see the vid built in ExpandArcs)
  // back to the host layer's real FILE path, so a relative reference/payload
  // authored inside a variant anchors to the file that contains the variant.
  // Recurses for nested variants; a real file path passes through unchanged.
  static std::string RealAnchorOf(std::string id) {
    while (id.compare(0, 8, "variant:") == 0) {
      std::string s = id.substr(8);
      bool ok = true;
      for (int k = 0; k < 3; ++k) {  // strip trailing :site:vset:vname
        auto p = s.rfind(':');
        if (p == std::string::npos) { ok = false; break; }
        s.resize(p);
      }
      if (!ok) break;
      id.swap(s);
    }
    return id;
  }

  void ProcessArc(const Src &src, const CompositionArc &arc, ArcType kind,
                  const ExpansionFrame *frame, std::vector<Src> *out,
                  std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err, const std::string &authoring_layer_id = "") {
    uint32_t arc_stack_idx;
    std::string arc_site;
    if (arc.is_internal || arc.asset_path.empty()) {
      arc_stack_idx = src.stack_idx;
      arc_site = arc.prim_path.empty() ? src.site : arc.prim_path;
    } else {
      // Anchor = the FILE path of the LAYER that authored the arc (Resolve
      // derives its dir). A reference/payload authored in a SUBLAYER resolves
      // relative to that sublayer, not the layer-stack's root — fall back to the
      // stack identifier only when the authoring layer is unknown.
      // COPY, not a reference: GetOrLoad/InternLayerStack below can grow
      // layer_stacks and invalidate a reference into it.
      const std::string anchor = RealAnchorOf(
          !authoring_layer_id.empty()
              ? authoring_layer_id
              : layer_stacks[src.stack_idx].identifier);
      LayerLoadOptions load_opts;
      load_opts.max_memory = options.max_layer_memory;
      load_opts.usda_parse_options = options.usda_parse_options;
      load_opts.strict_aousd_conformance = options.strict_aousd_conformance;
      std::shared_ptr<Layer> arc_layer =
          reg_->GetOrLoad(*resolver, arc.asset_path, anchor, warn, err,
                          load_opts);
      if (!arc_layer) {
        if (options.error_when_asset_not_found) {
          AddIssue(ErrorCode::InvalidAssetPath, arc.asset_path,
                   "Arc asset not found: " + arc.asset_path, err);
        }
        return;
      }
      const std::string arc_id = resolver->ResolvePath(arc.asset_path, anchor);
      arc_stack_idx = InternLayerStack(arc_layer, arc_id, warn, err);
      if (arc_stack_idx == kInvalidStack) return;
      if (!arc.prim_path.empty()) {
        arc_site = arc.prim_path;
      } else if (!arc_layer->meta().defaultPrim.empty()) {
        arc_site = "/" + arc_layer->meta().defaultPrim;
      } else {
        // No target prim path and no authored defaultPrim: pxr reports
        // "Unresolved reference prim path @...@<defaultPrim>" and the arc
        // contributes NOTHING (the referencing prim composes empty). The
        // previous silent fallback to the first root prim composed content
        // pxr never would.
        AddIssue(ErrorCode::UnresolvedPrimPath, arc_id,
                 "Unresolved reference prim path @" + arc.asset_path +
                     "@<defaultPrim>: layer has no defaultPrim and the arc "
                     "names no prim path",
                 warn);
        return;
      }
    }

    if (frame && frame->Contains(arc_stack_idx, arc_site)) {
      const std::string site =
          layer_stacks[arc_stack_idx].identifier + ":" + arc_site;
      AddIssue(ErrorCode::ArcCycle, site,
               "Composition cycle detected at arc: " + site, err);
      return;
    }
    if (frame && frame->depth + 1 >= options.max_depth) {
      AddIssue(ErrorCode::MaxDepthExceeded,
               layer_stacks[arc_stack_idx].identifier + ":" + arc_site,
               "Composition max depth exceeded", err);
      return;
    }
    // A same-stack arc that targets a namespace ANCESTOR of its own source site
    // grafts the source under itself: the composed namespace grows without
    // bound (e.g. `def "A" { def "B" (references = </A>) {} }`). The frame
    // chain cannot see this (each child prim starts a fresh expansion), so
    // reject it here.
    if (arc_stack_idx == src.stack_idx && arc_site != src.site &&
        IsPathAtOrUnder(src.site, arc_site)) {
      AddIssue(ErrorCode::ArcCycle, src.site,
               "Composition cycle detected: arc at " + src.site +
                   " targets ancestor " + arc_site,
               err);
      return;
    }

    NamespaceMapping local{arc_site, src.site};
    Src arc_src;
    arc_src.stack_idx = arc_stack_idx;
    arc_src.site = arc_site;
    arc_src.map_idx =
        InternMapping(NamespaceMapping::Compose(Mapping(src.map_idx), local));
    // Compose this arc's layer offset under the parent's (root..arc chain), so
    // the referenced content's time samples are mapped into root/stage time.
    LayerOffset arc_off;
    if (!arc.layer_offset.empty()) {
      double o = 0.0, s = 1.0;
      Compositor::ParseLayerOffset(arc.layer_offset, o, s);
      arc_off = LayerOffset{o, s};
    }
    arc_src.offset = src.offset.Compose(arc_off);
    arc_src.arc_kind = kind;

    // A specialize subtree is globally weakest: it (and everything beneath it)
    // is collected into spec_out.
    std::vector<Src> *target = (kind == ArcType::Specialize) ? spec_out : out;

    // Implied class-arc propagation: a class (inherit/specialize) reached
    // through a reference chain is ALSO expressed in EVERY ancestor layer stack
    // on that chain (root + intermediate references), so an override authored on
    // the same class path at any level composes. Ancestors are pushed first
    // (root strongest) so they outrank the referenced-stack class opinions.
    // Class paths are global, so the site is unchanged across stacks.
    if (IsClassBasedArc(kind)) {
      // The implied class path must be expressed in the ANCESTOR stack's own
      // namespace: an inherit to a SUB-ROOT class (e.g. </Models/_class_X>
      // where /Models is referenced as /World) is implied at the MAPPED path
      // (/World/_class_X) in the referencing stack, so overrides authored
      // there compose (pxr maps implied classes through the arc's map
      // function). A root-level class path lies outside every arc's source
      // prefix, so Apply() leaves it unchanged (the previous behavior).
      // Limitation: src.map_idx maps to ROOT namespace; for a chain with
      // intermediate reference stacks the intermediate-namespace path is not
      // tracked, so those keep the root-mapped path (root-level class paths
      // are unaffected).
      const std::string implied_site = Mapping(src.map_idx).Apply(arc_site);
      std::set<uint32_t> seen_stack;
      for (uint32_t as : chain) {
        if (as == arc_stack_idx || !seen_stack.insert(as).second) continue;
        if (Specs(as, implied_site).empty()) continue;
        Src implied = arc_src;
        implied.stack_idx = as;
        if (implied_site != arc_site) {
          implied.site = implied_site;
          // Implied specs live at implied_site and compose onto the same
          // destination the direct class arc maps to.
          implied.map_idx = InternMapping(NamespaceMapping{
              implied_site, Mapping(arc_src.map_idx).Apply(arc_site)});
        }
        std::vector<uint32_t> ichain{as};
        const ExpansionFrame iframe{as, &implied.site, frame,
                                    frame ? frame->depth + 1 : 0};
        ExpandArcs(implied, &iframe, target, spec_out, sels, ichain, warn, err);
      }
    }

    // Reference/payload arcs descend into a new layer stack -> extend the chain.
    std::vector<uint32_t> child_chain = chain;
    if (arc_stack_idx != src.stack_idx) child_chain.push_back(arc_stack_idx);

    const ExpansionFrame cframe{arc_stack_idx, &arc_site, frame,
                                frame ? frame->depth + 1 : 0};
    ExpandArcs(arc_src, &cframe, target, spec_out, sels, child_chain, warn,
               err);
  }

  // Pre-order DFS == strength order. Per source: local (pushed) > inherits >
  // references > payloads; specializes routed to `spec_out` (globally weakest).
  void ExpandArcs(const Src &src, const ExpansionFrame *frame,
                  std::vector<Src> *out, std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err) {
    out->push_back(src);

    const std::vector<SpecRef> &specs = Specs(src.stack_idx, src.site);
    if (specs.empty()) return;

    // Record this source's variant selections (strong-first wins) so a selection
    // authored on a stronger source applies to a variantSet defined on a weaker
    // one (cross-source selection).
    for (const SpecRef &sr : specs) RecordSelections(*sr.spec, sels);

    // Phase 7 (S5): with apply_list_ops, gather each arc field once across the
    // whole site (cross-layer list-op merge: explicit-replace / prepend /
    // append / delete / dedup); otherwise expand each spec's arcs independently
    // (legacy strong-first concatenation). Variants are always per-spec.
    const bool merge = options.apply_list_ops;

    if (merge) {
      for (const auto &s : MergeArcField(specs, ArcSel::Inherits)) {
        ProcessArc(src, Compositor::ParseReference(s.first), ArcType::Inherit,
                   frame, out, spec_out, sels, chain, warn, err, s.second);
      }
    }

    for (const SpecRef &sr : specs) {
      const PrimSpec *spec = sr.spec;

      // Inherits (stronger than references) -- legacy per-spec path.
      if (!merge) {
        for (const std::string &s : spec->meta().inherits) {
          ProcessArc(src, Compositor::ParseReference(s), ArcType::Inherit, frame,
                     out, spec_out, sels, chain, warn, err);
        }
      }

      // Variants (weaker than inherits, stronger than references). For each
      // selected variant, graft its inline opinions and/or its content
      // subtree. `apply_variant` recurses into nested variant sets authored
      // on the option itself (selection from the accumulated map — which
      // includes caller overrides — falling back to the nested set's own
      // `selected`).
      if (!spec->meta().variantSets().empty()) {
        std::function<void(const std::string &, const VariantData *, int)>
            apply_variant = [&](const std::string &set_name,
                                const VariantData *vd, int vdepth) {
          // Subtree content: model as a reference-style Variant source into the
          // content layer's "/__self__" root, so child prims compose normally.
          if (vd->content) {
            // Stable variant-content identity: (host layer-stack, host site,
            // variantSet, variantName). The previous pointer-derived id aliased
            // after a VariantData was freed and reallocated (stale instance
            // keys), and never matched across two parses of the same asset.
            const std::string vid = "variant:" +
                                    layer_stacks[src.stack_idx].identifier +
                                    ":" + src.site + ":" + set_name + ":" +
                                    vd->name;
            uint32_t cstack = InternLayerStack(vd->content, vid, warn, err);
            if (cstack == kInvalidStack) return;
            Src vsrc;
            vsrc.stack_idx = cstack;
            vsrc.site = "/__self__";
            vsrc.map_idx = InternMapping(NamespaceMapping::Compose(
                Mapping(src.map_idx), NamespaceMapping{"/__self__", src.site}));
            vsrc.offset = src.offset;
            vsrc.arc_kind = ArcType::Variant;
            const ExpansionFrame vframe{cstack, &vsrc.site, frame,
                                        frame ? frame->depth + 1 : 0};
            ExpandArcs(vsrc, &vframe, out, spec_out, sels, chain, warn, err);
          }
          // Inline opinions (properties/relationships on the host prim
          // itself), plus prim-state opinions (`active = false` on a variant
          // option deactivates the host — a common pruning pattern).
          if (!vd->properties.empty() || !vd->relationships.empty() ||
              !vd->active || vd->hidden) {
            Src vsrc = src;
            vsrc.arc_kind = ArcType::Variant;
            vsrc.variant = vd;
            out->push_back(std::move(vsrc));
          }
          // Composition arcs authored on the variant OPTION (e.g. a `payload`
          // that supplies the def-Mesh geometry — XGen assets author geometry on
          // the selected variant, not in its body). Compose at the variant's
          // strength (after the variant's own content/inline, before the host's
          // references), anchored to the layer that authored the variant so a
          // relative `@./geo.usd@` resolves correctly.
          for (const std::string &s : vd->inherits) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Inherit,
                       frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
          }
          for (const std::string &s : vd->references) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Reference,
                       frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
          }
          if (!vd->payloads.empty()) {
            const std::string root_prim_path = Mapping(src.map_idx).Apply(src.site);
            bool any_deferred = false;
            for (const std::string &pl_str : vd->payloads) {
              CompositionArc arc = Compositor::ParsePayload(pl_str);
              if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
                any_deferred = true;
                continue;
              }
              ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                         chain, warn, err, sr.layer_id);
            }
            // insert-only here: another arc group's loaded payloads must not
            // clear a deferral recorded for this option's payloads.
            if (any_deferred) deferred_payload_prims.insert(root_prim_path);
          }
          for (const std::string &s : vd->specializes) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Specialize,
                       frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
          }

          // Nested variant sets authored on this option (inline / USDA
          // representation). Crate-representation nesting recurses naturally
          // through the holder graft below (the holder prim carries the
          // nested sets in its own meta).
          if (vdepth < 16) {
            for (const VariantSetData &nvs : vd->variantSets) {
              std::string chosen;
              auto nsit = sels->find(nvs.name);
              if (nsit != sels->end()) chosen = nsit->second;
              if (chosen.empty()) chosen = nvs.selected;
              if (chosen.empty()) continue;
              for (const VariantData &nvd : nvs.variants) {
                if (nvd.name == chosen) {
                  apply_variant(nvs.name, &nvd, vdepth + 1);
                  break;
                }
              }
            }
          }
        };

        for (const SelectedVariant &sv : SelectVariants(*spec, *sels)) {
          apply_variant(*sv.set_name, sv.vd, 0);
        }

        // Crate representation: pxr-authored crates store variant content as
        // bracketed HOLDER prims ("/Prim/{set=sel}/..." in the SAME layer stack)
        // and a VariantSetData that carries only {name, selected} -- no inline
        // VariantData.content, so SelectVariants() above finds nothing. For each
        // selected set, graft the selected holder as a Variant source mapped onto
        // the host, so its descendants compose as the host's children (matching
        // the USDA content path). USDA-authored variants have no holder prim at
        // this site, so Specs() is empty and this is a no-op for them.
        for (const VariantSetData &vss : spec->meta().variantSets()) {
          auto sit = sels->find(vss.name);
          if (sit == sels->end() || sit->second.empty()) continue;
          const std::string holder =
              src.site + "/{" + vss.name + "=" + sit->second + "}";
          if (Specs(src.stack_idx, holder).empty()) continue;  // not crate-style
          Src vsrc;
          vsrc.stack_idx = src.stack_idx;
          vsrc.site = holder;
          vsrc.map_idx = InternMapping(NamespaceMapping::Compose(
              Mapping(src.map_idx), NamespaceMapping{holder, src.site}));
          vsrc.offset = src.offset;
          vsrc.arc_kind = ArcType::Variant;
          const ExpansionFrame vframe{src.stack_idx, &vsrc.site, frame,
                                      frame ? frame->depth + 1 : 0};
          ExpandArcs(vsrc, &vframe, out, spec_out, sels, chain, warn, err);
        }
      }

      if (merge) continue;  // merged refs/payloads/specializes handled below

      // References. Anchor relative asset paths to THIS spec's authoring layer.
      for (const std::string &ref_str : spec->meta().references) {
        ProcessArc(src, Compositor::ParseReference(ref_str), ArcType::Reference,
                   frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
      }

      // Payloads (deferrable, weaker than references). See the merge path:
      // erase the deferral marker only when NO payload on this prim deferred.
      if (!spec->meta().payloads.empty()) {
        const std::string root_prim_path = Mapping(src.map_idx).Apply(src.site);
        bool any_deferred = false;
        for (const std::string &pl_str : spec->meta().payloads) {
          CompositionArc arc = Compositor::ParsePayload(pl_str);
          if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
            any_deferred = true;
            continue;  // deferred: contributes no opinions.
          }
          ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                     chain, warn, err, sr.layer_id);
        }
        if (any_deferred) {
          deferred_payload_prims.insert(root_prim_path);
        } else {
          deferred_payload_prims.erase(root_prim_path);
        }
      }

      // Specializes (globally weakest; routed into spec_out by ProcessArc).
      for (const std::string &s : spec->meta().specializes) {
        ProcessArc(src, Compositor::ParseReference(s), ArcType::Specialize,
                   frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
      }
    }

    if (merge) {
      // Cross-layer-merged references / payloads / specializes, processed once
      // in LIVRPS order (all inherits already emitted above, before variants).
      for (const auto &s : MergeArcField(specs, ArcSel::References)) {
        ProcessArc(src, Compositor::ParseReference(s.first), ArcType::Reference,
                   frame, out, spec_out, sels, chain, warn, err, s.second);
      }
      auto mpay = MergeArcField(specs, ArcSel::Payloads);
      if (!mpay.empty()) {
        const std::string root_prim_path = Mapping(src.map_idx).Apply(src.site);
        // Track deferral across ALL of this prim's payloads: erasing per
        // loaded arc let a later loaded payload clear the marker set by an
        // earlier deferred one (HasDeferredPayload then lied depending on
        // authored arc order). Erase only when nothing was deferred.
        bool any_deferred = false;
        for (const auto &pl : mpay) {
          CompositionArc arc = Compositor::ParsePayload(pl.first);
          if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
            any_deferred = true;
            continue;
          }
          ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                     chain, warn, err, pl.second);
        }
        if (any_deferred) {
          deferred_payload_prims.insert(root_prim_path);
        } else {
          deferred_payload_prims.erase(root_prim_path);
        }
      }
      for (const auto &s : MergeArcField(specs, ArcSel::Specializes)) {
        ProcessArc(src, Compositor::ParseReference(s.first), ArcType::Specialize,
                   frame, out, spec_out, sels, chain, warn, err, s.second);
      }
    }
  }

  std::vector<Src> ExpandList(const std::vector<Src> &base,
                              const std::string &root_prim_path,
                              std::string *warn, std::string *err) {
    std::vector<Src> main;
    std::vector<Src> spec;  // specialize-derived: globally weakest
    // Variant selections accumulated across the prim's sources (cross-source).
    // Seed with the caller's variant overrides BEFORE expansion: selection
    // recording uses first-wins emplace, so pre-seeding makes an override
    // beat every authored selection during grafting. (It was previously
    // applied after ExpandArcs, so the prim's own variant graft still used
    // the authored selection and only descendant expansion saw the override.)
    std::map<std::string, std::string> sels;
    for (const auto &ov : options.variant_overrides) {
      sels[ov.first] = ov.second;
    }
    const auto scoped = options.variant_overrides_by_path.find(root_prim_path);
    if (scoped != options.variant_overrides_by_path.end()) {
      for (const auto &ov : scoped->second) sels[ov.first] = ov.second;
    }
    for (const Src &s : base) {
      // Carry a base source's own arc kind so a specialize re-rooted onto a
      // child stays globally weakest.
      std::vector<Src> *tgt = (s.arc_kind == ArcType::Specialize) ? &spec : &main;
      // Ancestor chain for implied class arcs. A re-rooted child source (its
      // parent reached this stack through a reference/payload) descends from
      // the ROOT stack by construction, so seed the chain with it — otherwise
      // a class inherited by a referenced CHILD prim is never implied in the
      // root stack and root-layer overrides on the class path are dropped.
      // (Intermediate stacks of the parent's arc chain are not tracked; see
      // the implied-class note in ProcessArc.)
      std::vector<uint32_t> chain;
      if (s.stack_idx != 0) chain.push_back(0);
      chain.push_back(s.stack_idx);
      const ExpansionFrame seed{s.stack_idx, &s.site, nullptr, 0};
      ExpandArcs(s, &seed, tgt, &spec, &sels, chain, warn, err);
    }
    main.insert(main.end(), spec.begin(), spec.end());
    return main;
  }

  // --- ancestral source resolution (cached) -------------------------------

  // The expanded composition sources for `path`. A root-level prim seeds from
  // the root layer stack; a descendant re-roots each of its parent's expanded
  // sources at the child name (so arcs authored on an ancestor reach it), then
  // expands that child's own arcs. Cached per path.
  const std::vector<Src> &SourcesForPath(const Path &path, std::string *warn,
                                         std::string *err) {
    {
      auto it = sources_cache.find(path.str());
      if (it != sources_cache.end()) return it->second;
    }

    // Collect the uncached ancestor chain (leaf -> topmost uncached), then
    // expand top-down, so a pathologically deep query path walks a loop instead
    // of the C++ stack.
    std::vector<Path> pending;
    {
      Path cur = path;
      for (;;) {
        pending.push_back(cur);
        Path parent = cur.parent();
        if (parent.is_root() || parent.empty() || parent.str() == "/") break;
        if (sources_cache.count(parent.str())) break;
        cur = parent;
      }
    }

    const std::vector<Src> *result = nullptr;
    for (size_t i = pending.size(); i-- > 0;) {
      const Path &p = pending[i];
      const std::string &key = p.str();  // ref into Path; no copy

      std::vector<Src> base;
      Path parent = p.parent();
      const bool parent_is_root =
          parent.is_root() || parent.empty() || parent.str() == "/";
      if (parent_is_root) {
        Src s;
        s.stack_idx = 0;
        s.site = key;
        base.push_back(std::move(s));
      } else {
        const std::vector<Src> &psrc = sources_cache[parent.str()];
        const std::string cn = p.name();
        base.reserve(psrc.size());
        for (const Src &ps : psrc) {
          // Variant sources are inline opinions on the prim itself; they do not
          // (yet) carry child prims, so they don't propagate to children.
          if (ps.variant) continue;
          Src c;
          c.stack_idx = ps.stack_idx;
          c.site = ps.site + "/" + cn;
          c.map_idx = ps.map_idx;  // share the parent's mapping (no string copy)
          c.offset = ps.offset;
          c.arc_kind = ps.arc_kind;
          base.push_back(std::move(c));
        }
      }

      // unordered_map element refs are stable across later inserts, and
      // pending[0] (the queried leaf) is filled last, so this captures it.
      std::vector<Src> &slot = sources_cache[key];
      slot = ExpandList(base, key, warn, err);
      result = &slot;
    }
    return *result;
  }

  // --- value resolution ---------------------------------------------------

  // Merge all sources into a fresh PrimSpec; return the composed child names.
  //
  // OPINIONS compose STRONG->WEAK (fill-absent: the strongest source that
  // authored a field/property wins). CHILD-NAME ORDER, however, composes
  // WEAK->STRONG -- pxr's PcpComposeSiteChildNames appends each node's (and each
  // layer's) primChildren weakest-first, so the weakest source that introduces a
  // name fixes its position and stronger duplicates are skipped (e.g. a
  // reference's `[LOD0, Materials]` precedes a local-only `LOD1` ->
  // `[LOD0, Materials, LOD1]`). So we collect names in a separate reverse pass.
