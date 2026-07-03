// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache method definitions (arc expansion + specs/instances + list-ops).
// Split out of cache.cc so these translation units compile in parallel; see
// cache-internal.hh for the Cache::Impl declaration.

#include "cache-internal.hh"

namespace tinyusdz {
namespace next {
namespace pcp {

  std::string Cache::Impl::RealAnchorOf(std::string id) {
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

  void Cache::Impl::ProcessArc(const Src &src, const CompositionArc &arc, ArcType kind,
                  const ExpansionFrame *frame, std::vector<Src> *out,
                  std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err, const std::string &authoring_layer_id ) {
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
      std::shared_ptr<Layer> arc_layer =
          reg_->GetOrLoad(*resolver, arc.asset_path, anchor, warn, err,
                          MakeLayerLoadOptions());
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
        const auto &roots = arc_layer->root_indices();
        arc_site = roots.empty() ? "/" : "/" + arc_layer->prim(roots[0])->name();
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
      std::set<uint32_t> seen_stack;
      for (uint32_t as : chain) {
        if (as == arc_stack_idx || !seen_stack.insert(as).second) continue;
        if (Specs(as, arc_site).empty()) continue;
        Src implied = arc_src;
        implied.stack_idx = as;
        std::vector<uint32_t> ichain{as};
        const ExpansionFrame iframe{as, &arc_site, frame,
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

  void Cache::Impl::ExpandArcs(const Src &src, const ExpansionFrame *frame,
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
      // selected variant, graft its inline opinions and/or its content subtree.
      if (!spec->meta().variantSets().empty()) {
        for (const SelectedVariant &sv : SelectVariants(*spec, *sels)) {
          const VariantData *vd = sv.vd;
          // Subtree content: model as a reference-style Variant source into the
          // content layer's "/__self__" root, so child prims compose normally.
          if (vd->content) {
            // Stable variant-content identity: (host layer-stack, host site,
            // variantSet, variantName). The previous pointer-derived id aliased
            // after a VariantData was freed and reallocated (stale instance
            // keys), and never matched across two parses of the same asset.
            const std::string vid = "variant:" +
                                    layer_stacks[src.stack_idx].identifier +
                                    ":" + src.site + ":" + *sv.set_name + ":" +
                                    vd->name;
            uint32_t cstack = InternLayerStack(vd->content, vid, warn, err);
            if (cstack == kInvalidStack) continue;
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
          // Inline opinions (properties/relationships on the host prim itself).
          if (!vd->properties.empty() || !vd->relationships.empty()) {
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
            for (const std::string &pl_str : vd->payloads) {
              CompositionArc arc = Compositor::ParsePayload(pl_str);
              if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
                deferred_payload_prims.insert(root_prim_path);
                continue;
              }
              deferred_payload_prims.erase(root_prim_path);
              ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                         chain, warn, err, sr.layer_id);
            }
          }
          for (const std::string &s : vd->specializes) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Specialize,
                       frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
          }
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

      // Payloads (deferrable, weaker than references).
      if (!spec->meta().payloads.empty()) {
        const std::string root_prim_path = Mapping(src.map_idx).Apply(src.site);
        for (const std::string &pl_str : spec->meta().payloads) {
          CompositionArc arc = Compositor::ParsePayload(pl_str);
          if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
            deferred_payload_prims.insert(root_prim_path);
            continue;  // deferred: contributes no opinions.
          }
          deferred_payload_prims.erase(root_prim_path);
          ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                     chain, warn, err, sr.layer_id);
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
        for (const auto &pl : mpay) {
          CompositionArc arc = Compositor::ParsePayload(pl.first);
          if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
            deferred_payload_prims.insert(root_prim_path);
            continue;
          }
          deferred_payload_prims.erase(root_prim_path);
          ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                     chain, warn, err, pl.second);
        }
      }
      for (const auto &s : MergeArcField(specs, ArcSel::Specializes)) {
        ProcessArc(src, Compositor::ParseReference(s.first), ArcType::Specialize,
                   frame, out, spec_out, sels, chain, warn, err, s.second);
      }
    }
  }

  std::vector<Src> Cache::Impl::ExpandList(const std::vector<Src> &base, std::string *warn,
                              std::string *err) {
    std::vector<Src> main;
    std::vector<Src> spec;  // specialize-derived: globally weakest
    // Variant selections accumulated across the prim's sources (cross-source).
    std::map<std::string, std::string> sels;
    for (const Src &s : base) {
      // Carry a base source's own arc kind so a specialize re-rooted onto a
      // child stays globally weakest.
      std::vector<Src> *tgt = (s.arc_kind == ArcType::Specialize) ? &spec : &main;
      std::vector<uint32_t> chain{s.stack_idx};
      const ExpansionFrame seed{s.stack_idx, &s.site, nullptr, 0};
      ExpandArcs(s, &seed, tgt, &spec, &sels, chain, warn, err);
    }
    // Apply variant overrides (stronger than any authored selection) so the
    // caller can force a specific variant (e.g. --variant districtLod=full)
    // without modifying the layer files.
    for (const auto &ov : options.variant_overrides) {
      sels[ov.first] = ov.second;
    }
    main.insert(main.end(), spec.begin(), spec.end());
    return main;
  }

  const std::vector<Src> &Cache::Impl::SourcesForPath(const Path &path, std::string *warn,
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
      slot = ExpandList(base, warn, err);
      result = &slot;
    }
    return *result;
  }

  const std::vector<SpecRef> &Cache::Impl::Specs(uint32_t stack_idx,
                                    const std::string &site) const {
    if (stack_idx >= spec_cache_by_stack_.size())
      spec_cache_by_stack_.resize(stack_idx + 1);
    auto &m = spec_cache_by_stack_[stack_idx];
    auto it = m.find(site);  // keys by the caller's string, no key copy
    if (it != m.end()) return it->second;
    std::vector<SpecRef> refs = FindSpecs(layer_stacks[stack_idx], site);
    return m.emplace(site, std::move(refs)).first->second;
  }

  const std::vector<SpecRef> &Cache::Impl::SpecsFor(const Src &s) const {
    if (s.specs_) return *s.specs_;
    const std::vector<SpecRef> &r = Specs(s.stack_idx, s.site);
    s.specs_ = &r;
    return r;
  }

  std::vector<SpecRef> Cache::Impl::FindSpecs(const LayerStack &st,
                                 const std::string &site) const {
    std::vector<SpecRef> refs;
    Path p(site);
    for (size_t i = 0; i < st.layers.size(); ++i) {
      const auto &lp = st.layers[i];
      if (const PrimSpec *s = lp->prim_at_path(p)) {
        SpecRef r;
        r.spec = s;
        r.layer = lp.get();
        if (i < st.layer_identifiers.size()) r.layer_id = st.layer_identifiers[i];
        refs.push_back(std::move(r));
      }
    }
    return refs;
  }

  const PrimSpec *Cache::Impl::FindSpec(const LayerStack &st, const std::string &site,
                           const Layer **out_layer) const {
    std::vector<SpecRef> refs = FindSpecs(st, site);
    if (!refs.empty()) {
      if (out_layer) *out_layer = refs[0].layer;
      return refs[0].spec;
    }
    return nullptr;
  }

  bool Cache::Impl::AnyAuthors(const std::vector<Src> &srcs) const {
    for (const Src &s : srcs) {
      if (!SpecsFor(s).empty()) return true;
    }
    return false;
  }

  bool Cache::Impl::ShouldLoadPayload(const std::string &root_prim_path,
                         const std::string &asset) {
    // Explicit load rules win; a path with no governing rule falls back to the
    // base policy (payload_policy callback, else the load_payloads flag).
    switch (load_rules_.GetEffect(root_prim_path)) {
      case LoadRules::Effect::All:
      case LoadRules::Effect::Only:
        return true;
      case LoadRules::Effect::None:
        return false;
      case LoadRules::Effect::Default:
        break;
    }
    if (options.payload_policy) {
      return options.payload_policy(Path(root_prim_path), asset);
    }
    return options.load_payloads;
  }

  void Cache::Impl::RecordSelections(const PrimSpec &spec,
                        std::map<std::string, std::string> *sels) const {
    // Per-set `selected` field FIRST: it is the authoritative per-set form (what
    // the crate reader sets for EVERY selected set, and what the Compositor's
    // ApplyVariants reads first). The legacy variantSelection string carries only
    // ONE set, so without this a MULTI-set selection loaded from USDC loses every
    // set after the first. (emplace keeps the first/strongest opinion per set.)
    for (const VariantSetData &vss : spec.meta().variantSets()) {
      if (!vss.selected.empty()) sels->emplace(vss.name, vss.selected);
    }
    if (!spec.meta().variantSelection.empty()) {
      VariantSelection s =
          Compositor::ParseVariantSelection(spec.meta().variantSelection);
      if (!s.variant_set.empty()) sels->emplace(s.variant_set, s.variant_name);
    }
    for (const auto &sel : spec.meta().variantSelections()) {
      sels->emplace(sel.first, sel.second);
    }
  }

  std::vector<Cache::Impl::SelectedVariant> Cache::Impl::SelectVariants(
      const PrimSpec &spec,
      const std::map<std::string, std::string> &sels) const {
    std::vector<SelectedVariant> out;
    for (const VariantSetData &vss : spec.meta().variantSets()) {
      auto sit = sels.find(vss.name);
      if (sit == sels.end()) continue;  // no selection for this set
      for (const VariantData &vd : vss.variants) {
        if (vd.name == sit->second) {
          out.push_back({&vss.name, &vd});
          break;
        }
      }
    }
    return out;
  }

  bool Cache::Impl::IsInstanceableSources(const std::vector<Src> &srcs) const {
    if (srcs.size() <= 1) return false;
    for (const Src &s : srcs) {
      for (const SpecRef &sr : SpecsFor(s)) {
        if (sr.spec->meta().instanceable) return true;
      }
    }
    return false;
  }

  std::string Cache::Impl::ComputeInstanceKeyImpl(const std::vector<Src> &srcs) const {
    std::string key;
    for (const Src &s : srcs) {
      bool found_type = false;
      for (const SpecRef &sr : SpecsFor(s)) {
        if (!sr.spec->type_name().empty()) {
          key += "T:" + sr.spec->type_name() + ";";
          found_type = true;
          break;
        }
      }
      if (found_type) break;
    }
    for (size_t i = 1; i < srcs.size(); ++i) {
      const Src &s = srcs[i];
      if (s.site != Mapping(s.map_idx).source_prefix) continue;  // ancestral/positional
      key += ArcTypeName(s.arc_kind);
      key += "|" + layer_stacks[s.stack_idx].identifier + "|" + s.site;
      if (s.variant) key += "|v:" + s.variant->name;
      // A non-identity layer offset bakes different sample times, so instances
      // with different offsets must not share a prototype.
      if (!s.offset.is_identity()) {
        key += "|o:" + std::to_string(s.offset.offset) + ":" +
               std::to_string(s.offset.scale);
      }
      key += ";";
    }
    return key;
  }

  void Cache::Impl::AssignPrototype(const std::string &prim_path, const std::string &ik,
                       bool prefer_min ) {
    auto pit = prototype_by_key.find(ik);
    if (pit == prototype_by_key.end()) {
      prototype_by_key[ik] = prim_path;  // first of its key seeds the prototype
      prototype_of[prim_path] = prim_path;
      instances_by_prototype[prim_path].push_back(prim_path);
      return;
    }
    const std::string old_proto = pit->second;
    if (prim_path == old_proto) return;  // re-registering the prototype itself

    if (!prefer_min || old_proto < prim_path) {
      // prim_path is a (non-prototype) instance of the existing prototype.
      prototype_of[prim_path] = old_proto;
      auto &vec = instances_by_prototype[old_proto];
      if (std::find(vec.begin(), vec.end(), prim_path) == vec.end()) {
        vec.push_back(prim_path);
      }
      return;
    }

    // prefer_min && prim_path < old_proto: prim_path becomes the new prototype.
    // Demote old_proto and re-point the whole group under the new prototype.
    pit->second = prim_path;
    std::vector<std::string> members;
    auto oit = instances_by_prototype.find(old_proto);
    if (oit != instances_by_prototype.end()) {
      members = std::move(oit->second);
      instances_by_prototype.erase(oit);
    }
    if (std::find(members.begin(), members.end(), prim_path) == members.end()) {
      members.push_back(prim_path);
    }
    for (const std::string &m : members) prototype_of[m] = prim_path;
    instances_by_prototype[prim_path] = std::move(members);
  }

  void Cache::Impl::RegisterInstance(const std::string &prim_path,
                        const std::vector<Src> &srcs, bool prefer_min ) {
    if (!options.detect_instances || !IsInstanceableSources(srcs)) return;
    const std::string ik = ComputeInstanceKeyImpl(srcs);
#if defined(TINYUSDZ_ENABLE_THREAD)
    if (defer_instances_) {
      // Worker mode: stash for the deterministic input-order merge.
      pending_instance_[prim_path] = {true, ik};
      return;
    }
#endif
    AssignPrototype(prim_path, ik, prefer_min);
  }

  void Cache::Impl::DropInstancing(const std::string &prim_path) {
    prototype_of.erase(prim_path);
    for (auto it = instances_by_prototype.begin();
         it != instances_by_prototype.end();) {
      auto &v = it->second;
      v.erase(std::remove(v.begin(), v.end(), prim_path), v.end());
      const bool was_prototype = (it->first == prim_path);
      if (was_prototype || v.empty()) {
        // The group's prototype is gone: orphan the remaining siblings so their
        // prototype_of doesn't dangle to a dropped prototype. They re-group on
        // the next recomposition. (Without this, IsInstance/GetPrototype could
        // report a removed prototype after invalidating its subtree.)
        for (const std::string &m : v) prototype_of.erase(m);
        for (auto kit = prototype_by_key.begin(); kit != prototype_by_key.end();) {
          if (kit->second == it->first) kit = prototype_by_key.erase(kit);
          else ++kit;
        }
        it = instances_by_prototype.erase(it);
      } else {
        ++it;
      }
    }
  }

  const std::vector<std::string> &Cache::Impl::SelectInlineArc(const PrimSpecMeta &m,
                                                         ArcSel f) {
    switch (f) {
      case ArcSel::References: return m.references;
      case ArcSel::Payloads: return m.payloads;
      case ArcSel::Inherits: return m.inherits;
      default: return m.specializes;
    }
  }

  const ArcEdit *Cache::Impl::SelectArcEdit(const PrimSpecMeta &m, ArcSel f) {
    const ArcListOpEdits *e = m.arc_edits();
    if (!e) return nullptr;
    switch (f) {
      case ArcSel::References: return &e->references;
      case ArcSel::Payloads: return &e->payloads;
      case ArcSel::Inherits: return &e->inherits;
      default: return &e->specializes;
    }
  }

  std::vector<std::pair<std::string, std::string>> Cache::Impl::MergeArcField(
      const std::vector<SpecRef> &specs, ArcSel f) const {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto it = specs.rbegin(); it != specs.rend(); ++it) {
      const PrimSpecMeta &m = it->spec->meta();
      const std::string &lid = it->layer_id;
      const ArcEdit *e = SelectArcEdit(m, f);
      const std::vector<std::string> &inl = SelectInlineArc(m, f);
      if ((!e || !e->authored) && inl.empty()) {
        continue;
      }
      auto tag = [&](const std::vector<std::string> &v) {
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(v.size());
        for (const std::string &s : v) out.emplace_back(s, lid);
        return out;
      };
      if (!e || !e->authored || e->is_explicit) {
        result = tag(inl);
        continue;
      }
      auto removeAll = [&](const std::vector<std::string> &rm) {
        for (const std::string &x : rm) {
          result.erase(std::remove_if(result.begin(), result.end(),
                                      [&](const auto &p) { return p.first == x; }),
                       result.end());
        }
      };
      removeAll(e->deleted);
      removeAll(e->prepended);
      removeAll(e->appended);
      std::vector<std::pair<std::string, std::string>> merged;
      merged.reserve(e->prepended.size() + result.size() + e->appended.size());
      auto pre = tag(e->prepended), app = tag(e->appended);
      merged.insert(merged.end(), pre.begin(), pre.end());
      merged.insert(merged.end(), result.begin(), result.end());
      merged.insert(merged.end(), app.begin(), app.end());
      result.swap(merged);
    }
    return result;
  }

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
