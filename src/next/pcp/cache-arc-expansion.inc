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

  // An internal arc may name its target with a path RELATIVE to the prim that
  // authored it ("../_Y", "_Y", "./_Y"): resolve against the authoring site.
  // An external arc's target is always absolute in the referenced layer.
  static std::string ResolveArcPrimPath(const std::string &authoring_site,
                                        const std::string &prim_path) {
    if (prim_path.empty() || prim_path[0] == '/') return prim_path;
    std::vector<std::string> parts;
    size_t start = 1;  // authoring_site is absolute ("/A/B")
    while (start < authoring_site.size()) {
      const size_t sep = authoring_site.find('/', start);
      const size_t end = sep == std::string::npos ? authoring_site.size() : sep;
      if (end > start) parts.push_back(authoring_site.substr(start, end - start));
      if (sep == std::string::npos) break;
      start = sep + 1;
    }
    start = 0;
    while (start <= prim_path.size()) {
      const size_t sep = prim_path.find('/', start);
      const size_t end = sep == std::string::npos ? prim_path.size() : sep;
      const std::string part = prim_path.substr(start, end - start);
      if (part == "..") {
        if (parts.empty()) return std::string();  // above the pseudo-root
        parts.pop_back();
      } else if (!part.empty() && part != ".") {
        parts.push_back(part);
      }
      if (sep == std::string::npos) break;
      start = sep + 1;
    }
    std::string out;
    for (const std::string &part : parts) out += "/" + part;
    return out;
  }

  void ProcessArc(const Src &src, const CompositionArc &arc_in, ArcType kind,
                  const ExpansionFrame *frame, std::vector<Src> *out,
                  std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const ArcChain &chain, std::string *warn,
                  std::string *err, const std::string &authoring_layer_id = "") {
    CompositionArc arc = arc_in;
    if (arc.asset_path.empty() && !arc.prim_path.empty() &&
        arc.prim_path[0] != '/') {
      arc.prim_path = ResolveArcPrimPath(src.site, arc.prim_path);
      if (arc.prim_path.empty()) {
        AddIssue(ErrorCode::UnresolvedPrimPath, src.site,
                 "Relative arc target escapes the pseudo-root: " +
                     arc_in.prim_path,
                 warn);
        return;
      }
    }
    uint32_t arc_stack_idx;
    std::string arc_site;
    std::string evaluated_asset_path = arc.asset_path;
    if (!arc.asset_path.empty() &&
        options.expression_variable_policy !=
            ExpressionVariablePolicy::Disabled) {
      const Value empty = Value::MakeDictionary();
      const ExpressionEvaluation expression = EvaluateAssetPathExpression(
          arc.asset_path,
          src.expression_variables ? *src.expression_variables : empty);
      if (expression.is_expression && !expression.success) {
        AddIssue(ErrorCode::ExpressionVariableError, arc.asset_path,
                 expression.error,
                 options.expression_variable_policy ==
                         ExpressionVariablePolicy::RequireResolved
                     ? err
                     : warn);
        if (options.expression_variable_policy ==
            ExpressionVariablePolicy::RequireResolved) {
          return;
        }
      } else if (expression.is_expression && expression.is_none) {
        // The expression evaluated to None: the arc authors no opinion.
        return;
      } else if (expression.success) {
        evaluated_asset_path = expression.value;
      }
    }

    // An internal arc (class arc or internal reference) targets the COMPOSED
    // namespace, not a raw spec in this layer stack: the target prim may be
    // introduced by an arc of its own (SubrootReferenceNonCycle), delivered by
    // an ancestral inherit (TrickyLocalClassHierarchyWithRelocates), or carry
    // opinions from a VARIANT whose selection is authored in an outer layer
    // stack (TrickyInheritsInVariants). Reuse the target's composed sources and
    // remap them onto this site; the target path must be expressed in the
    // composed namespace to find them (this site's own mapping does that -- the
    // class path is authored in THIS stack's namespace).
    if ((arc.is_internal || evaluated_asset_path.empty()) &&
        !arc.prim_path.empty() &&
        Specs(src.stack_idx, arc.prim_path).empty()) {
      const std::string destination = Mapping(src.map_idx).Apply(src.site);
      const std::string composed_path = Mapping(src.map_idx).Apply(arc.prim_path);
      // A target at or under this site composes from this site -- reusing its
      // sources would graft the prim under itself.
      const bool self_target = IsPathAtOrUnder(destination, composed_path) ||
                               IsPathAtOrUnder(composed_path, destination);
      if (!self_target) {
        const std::vector<Src>& composed_target =
            SourcesForPath(Path(composed_path), warn, err);
        bool has_opinions = false;
        for (const Src& target_src : composed_target) {
          if (target_src.variant || !Specs(target_src.stack_idx, target_src.site).empty()) {
            has_opinions = true;
            break;
          }
        }
        if (has_opinions) {
          std::vector<Src>* target =
              (kind == ArcType::Specialize) ? spec_out : out;
          for (const Src& target_src : composed_target) {
            if (frame && frame->Contains(target_src.stack_idx, target_src.site))
              continue;
            if (target_src.stack_idx == src.stack_idx &&
                IsPathAtOrUnder(src.site, target_src.site)) {
              continue;
            }
            Src remapped = target_src;
            remapped.map_idx = InternMapping(NamespaceMapping::Compose(
                NamespaceMapping{composed_path, destination},
                Mapping(target_src.map_idx)));
            remapped.offset = src.offset.Compose(target_src.offset);
            remapped.arc_kind = kind;
            remapped.ancestral = false;  // reached via this prim's own arc
            remapped.expression_variables = src.expression_variables;
            target->push_back(std::move(remapped));
          }
          return;
        }
      }
    }
    if (arc.is_internal || evaluated_asset_path.empty()) {
      arc_stack_idx = src.stack_idx;
      if (!arc.prim_path.empty()) {
        arc_site = arc.prim_path;
      } else {
        // An explicitly empty internal target (`<>`) selects the defaultPrim
        // of the current layer stack, just as an external @asset@ with an
        // omitted path selects the target layer's defaultPrim. It does not
        // mean a self-reference to the authoring site.
        std::string default_prim;
        for (const std::shared_ptr<Layer>& candidate :
             layer_stacks[src.stack_idx].layers) {
          if (candidate->meta().defaultPrim_set ||
              !candidate->meta().defaultPrim.empty()) {
            default_prim = candidate->meta().defaultPrim;
            break;
          }
        }
        if (default_prim.empty()) {
          AddIssue(ErrorCode::UnresolvedPrimPath, src.site,
                   "Unresolved internal arc target <>: layer stack has no "
                   "defaultPrim", warn);
          return;
        }
        arc_site = "/" + default_prim;
      }
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
          reg_->GetOrLoad(*resolver, evaluated_asset_path, anchor, warn, err,
                          load_opts);
      if (!arc_layer) {
        if (options.error_when_asset_not_found) {
          AddIssue(ErrorCode::InvalidAssetPath, arc.asset_path,
                   "Arc asset not found: " + arc.asset_path, err);
        }
        return;
      }
      const std::string arc_id = resolver->ResolvePath(
          evaluated_asset_path, anchor, !options.strict_aousd_conformance);
      // The referencing site's composed expression variables become part of
      // the target stack's IDENTITY (pxr: (identifier, expression variables))
      // and feed its sublayer path expressions.
      arc_stack_idx = InternLayerStack(arc_layer, arc_id, warn, err,
                                       src.expression_variables);
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
    arc_src.map_idx = InternMapping(WithStackRelocates(
        arc_stack_idx,
        NamespaceMapping::Compose(Mapping(src.map_idx), local)));
    // Compose this arc's layer offset under the parent's (root..arc chain), so
    // the referenced content's time samples are mapped into root/stage time.
    LayerOffset arc_off;
    if (!arc.layer_offset.empty()) {
      double o = 0.0, s = 1.0;
      Compositor::ParseLayerOffset(arc.layer_offset, o, s);
      if (!std::isfinite(o) || !std::isfinite(s) || !(s > 0.0)) {
        AddIssue(ErrorCode::InvalidReferenceOffset, arc_site,
                 "Invalid reference/payload layer offset; using identity "
                 "mapping", err);
        o = 0.0;
        s = 1.0;
      }
      arc_off = LayerOffset{o, s};
    }
    // timeCodesPerSecond auto-scale between the referencing layer stack and
    // the referenced layer stack (pxr parity), composed UNDER the authored
    // offset — same rule as the sublayer edge in cache-layer-stack.inc.
    if (arc_stack_idx != src.stack_idx &&
        !layer_stacks[src.stack_idx].layers.empty() &&
        !layer_stacks[arc_stack_idx].layers.empty() &&
        layer_stacks[src.stack_idx].layers[0] &&
        layer_stacks[arc_stack_idx].layers[0]) {
      const double parent_tcps = layer_stacks[src.stack_idx]
                                     .layers[0]
                                     ->effective_timeCodesPerSecond();
      const double child_tcps = layer_stacks[arc_stack_idx]
                                    .layers[0]
                                    ->effective_timeCodesPerSecond();
      if (parent_tcps != child_tcps && child_tcps > 0.0 &&
          std::isfinite(parent_tcps) && std::isfinite(child_tcps)) {
        arc_off = arc_off.Compose(LayerOffset{0.0, parent_tcps / child_tcps});
      }
    }
    arc_src.offset = src.offset.Compose(arc_off);
    arc_src.arc_kind = kind;
    if (arc_stack_idx == src.stack_idx) {
      arc_src.expression_variables = src.expression_variables;
    } else {
      const Value inherited = src.expression_variables
                                  ? *src.expression_variables
                                  : Value::MakeDictionary();
      arc_src.expression_variables = std::make_shared<const Value>(
          ComposeExpressionVariables(
              layer_stacks[arc_stack_idx].expression_variables, inherited));
    }

    // A specialize subtree is globally weakest: it (and everything beneath it)
    // is collected into spec_out.
    std::vector<Src> *target = (kind == ArcType::Specialize) ? spec_out : out;
    const size_t target_mark = target->size();

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
      const std::string root_implied_site =
          Mapping(src.map_idx).Apply(arc_site);
      std::set<std::pair<uint32_t, uint32_t>> seen_node;
      for (const auto& node : chain) {
        const uint32_t as = node.first;
        const uint32_t ancestor_map_idx = node.second;
        if (as == arc_stack_idx || !seen_node.insert(node).second) continue;
        const std::string implied_site =
            Mapping(ancestor_map_idx).ReverseApply(root_implied_site);
        const std::vector<SpecRef>& implied_specs = Specs(as, implied_site);
        if (implied_specs.empty()) continue;
        // An implied class opinion in a closer-to-root layer stack is stronger
        // than selections authored at the referenced site that introduced the
        // inherit. Record that ancestor stack independently (strong-first),
        // then overlay it onto the accumulated selection context.
        std::map<std::string, std::string> ancestor_selections;
        for (const SpecRef& implied_spec : implied_specs) {
          RecordSelections(*implied_spec.spec, &ancestor_selections,
                           arc_src.expression_variables, warn, err);
        }
        for (const auto& selection : ancestor_selections) {
          (*sels)[selection.first] = selection.second;
        }
        Src implied = arc_src;
        implied.stack_idx = as;
        if (implied_site != arc_site) {
          implied.site = implied_site;
          // Implied specs live at implied_site and compose onto the SAME
          // destination the direct class arc maps to -- the INHERITING prim in
          // root space (`destination`), not the class prim's own composed path.
          // These differ when the inherit is authored inside a referenced layer
          // stack (src.map_idx non-identity); mapping to the class path would
          // land the implied opinions (and their relationship targets) on the
          // class instead of on the prim that inherits it.
          implied.map_idx = InternMapping(NamespaceMapping{
              implied_site, Mapping(src.map_idx).Apply(src.site)});
        }
        const ArcChain ichain{{as, implied.map_idx}};
        const ExpansionFrame iframe{as, implied.map_idx, &implied.site, frame,
                                    frame ? frame->depth + 1 : 0};
        ExpandArcs(implied, &iframe, target, spec_out, sels, ichain, warn, err);
      }
    }

    // The arc's target is not just the spec at `arc_site`: the target prim's own
    // ANCESTORS inside the target layer stack may carry arcs that deliver it (a
    // sub-root reference `@char.usd@</Char/Sub>` where `/Char` itself references
    // model.usd), and the target stack's relocates may have moved it there. So
    // derive the target's sources from its parent's stack-local expansion --
    // exactly how the composed namespace derives any child -- instead of seeding
    // a bare (stack, site). The site's own spec is always the first, strongest
    // entry, so the common (root-prim target) case is unchanged.
    //
    // Only the ANCESTORS resolve in the target stack's own context: variant
    // selections authored at the referencing site apply to the target prim and
    // below, so each target source is expanded here with the caller's `sels`.
    std::vector<Src> arc_srcs;
    {
      const Path arc_path(arc_site);
      const Path arc_parent = arc_path.parent();
      const bool root_prim = arc_parent.is_root() || arc_parent.empty() ||
                             arc_parent.str() == "/";
      if (root_prim) {
        Src seed;
        seed.stack_idx = arc_stack_idx;
        seed.site = arc_site;
        arc_srcs.push_back(std::move(seed));
        // A ROOT-prim target can still be a relocate ARRIVAL in the target
        // stack (`relocates = { </CharRig/.../Path>: </Path> }` moves a rig
        // scope to a NEW root prim that is then referenced directly). The
        // non-root branch gets this through DeriveChildSources; mirror the
        // arrival rule here so the relocation SOURCE's opinions (its def
        // specifier, type, and subtree arcs) compose into the arc target.
        const StackRelocates &rel = layer_stacks[arc_stack_idx].relocates;
        if (!rel.empty()) {
          if (const StackRelocates::Arrival *arrival =
                  rel.ArrivalOf("/", arc_path.name())) {
            // Pseudo-parent at the target stack's root: identity mapping, so
            // the arrival mappings stay expressed in the target stack's
            // namespace like every other arc_srcs entry (the seed above is
            // the dst-site spec itself; AddRelocatedSources would re-add it,
            // so only its relocate-source CONTENT expansion is appended).
            Src ps;
            ps.stack_idx = arc_stack_idx;
            std::vector<Src> with_content;
            AddRelocatedSources(ps, *arrival, arc_site, &with_content, warn,
                                err);
            for (Src &c : with_content) {
              // The dst-site spec itself is already seeded above.
              if (c.stack_idx == arc_stack_idx && c.site == arc_site) continue;
              arc_srcs.push_back(std::move(c));
            }
          }
        }
      } else {
        const std::vector<Src> &ancestral =
            SourcesForSite(arc_stack_idx, arc_parent, warn, err);
        arc_srcs = DeriveChildSources(ancestral, arc_path.name(), arc_site,
                                      warn, err);
      }
    }

    for (const Src &target_src : arc_srcs) {
      if (frame && frame->Contains(target_src.stack_idx, target_src.site))
        continue;
      if (target_src.stack_idx == src.stack_idx &&
          target_src.site != arc_site &&
          IsPathAtOrUnder(src.site, target_src.site)) {
        continue;  // would graft this prim under itself
      }
      Src s = arc_src;
      s.stack_idx = target_src.stack_idx;
      s.site = target_src.site;
      // target_src's mapping is expressed in the TARGET STACK's namespace (it
      // maps its own site to `arc_site`); compose it under this arc's mapping.
      s.map_idx = InternMapping(NamespaceMapping::Compose(
          Mapping(arc_src.map_idx), Mapping(target_src.map_idx)));
      s.offset = arc_src.offset.Compose(target_src.offset);
      if (s.stack_idx != arc_stack_idx) {
        const Value inherited = arc_src.expression_variables
                                    ? *arc_src.expression_variables
                                    : Value::MakeDictionary();
        s.expression_variables = std::make_shared<const Value>(
            ComposeExpressionVariables(
                layer_stacks[s.stack_idx].expression_variables, inherited));
      }

      // Reference/payload arcs descend into a new layer stack -> extend the chain.
      // The chain rides ALONG with the source (Src::arc_chain), so a child prim
      // re-rooted from it still knows every stack (and each stack's entry
      // mapping) that a class arc must be implied in.
      ArcChain child_chain = chain;
      if (s.stack_idx != src.stack_idx) {
        child_chain.emplace_back(s.stack_idx, s.map_idx);
      }
      s.arc_chain = std::make_shared<const ArcChain>(child_chain);

      const ExpansionFrame cframe{s.stack_idx, s.map_idx, &s.site, frame,
                                  frame ? frame->depth + 1 : 0};
      ExpandArcs(s, &cframe, target, spec_out, sels, child_chain, warn, err);
    }

    // A class prim can also carry opinions that only exist in the COMPOSED
    // namespace: an `over` on the class authored inside a VARIANT of one of its
    // ancestors, where the selection comes from an outer layer stack
    // (TrickyInheritsInVariants). The expansion above resolves the class in its
    // own stack, which cannot see that selection. Supplement it with the class
    // prim as the stage composes it, skipping everything already gathered.
    if (IsClassBasedArc(kind)) {
      const std::string destination = Mapping(src.map_idx).Apply(src.site);
      const std::string composed_path = Mapping(src.map_idx).Apply(arc_site);
      if (!IsPathAtOrUnder(destination, composed_path) &&
          !IsPathAtOrUnder(composed_path, destination)) {
        std::set<std::pair<uint32_t, std::string>> seen;
        for (size_t i = target_mark; i < target->size(); ++i) {
          seen.emplace((*target)[i].stack_idx, (*target)[i].site);
        }
        for (const Src &composed_src :
             SourcesForPath(Path(composed_path), warn, err)) {
          if (composed_src.variant) continue;  // inline opinions travel with their host
          if (!seen.insert({composed_src.stack_idx, composed_src.site}).second) {
            continue;
          }
          if (frame && frame->Contains(composed_src.stack_idx, composed_src.site))
            continue;
          if (Specs(composed_src.stack_idx, composed_src.site).empty()) continue;
          Src s = composed_src;
          s.map_idx = InternMapping(NamespaceMapping::Compose(
              NamespaceMapping{composed_path, destination},
              Mapping(composed_src.map_idx)));
          s.offset = src.offset.Compose(composed_src.offset);
          s.arc_kind = kind;
          s.ancestral = false;  // reached via this prim's own arc
          target->push_back(std::move(s));
        }
      }
    }
  }

  // Pre-order DFS == strength order. Per source: local (pushed) > inherits >
  // references > payloads; specializes routed to `spec_out` (globally weakest).
  void ExpandArcs(const Src &src, const ExpansionFrame *frame,
                  std::vector<Src> *out, std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const ArcChain &chain, std::string *warn,
                  std::string *err) {
    out->push_back(src);

    const std::vector<SpecRef> &specs = Specs(src.stack_idx, src.site);
    if (specs.empty()) return;

    // Record this source's variant selections (strong-first wins) so a selection
    // authored on a stronger source applies to a variantSet defined on a weaker
    // one (cross-source selection).
    for (const SpecRef &sr : specs) {
      RecordSelections(*sr.spec, sels, src.expression_variables, warn, err);
    }

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
        // A selected variant can author a selection for another variant set on
        // the same host. Propagate those opinions to a fixed point before
        // choosing all options (e.g. v1=a authors v2=b, while v2 is a sibling
        // set rather than nested inside v1).
        for (size_t pass = 0; pass < spec->meta().variantSets().size(); ++pass) {
          bool changed = false;
          for (const VariantSetData& set : spec->meta().variantSets()) {
            auto selected = sels->find(set.name);
            if (selected == sels->end()) continue;
            for (const VariantData& option : set.variants) {
              if (option.name != selected->second) continue;
              for (const auto& opinion : option.variantSelections) {
                auto existing = sels->find(opinion.first);
                if (existing == sels->end() || existing->second != opinion.second) {
                  (*sels)[opinion.first] = opinion.second;
                  changed = true;
                }
              }
              break;
            }
          }
          if (!changed) break;
        }
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
            const ExpansionFrame vframe{cstack, vsrc.map_idx, &vsrc.site, frame,
                                        frame ? frame->depth + 1 : 0};
            ExpandArcs(vsrc, &vframe, out, spec_out, sels, chain, warn, err);
          }
          // Inline opinions (properties/relationships on the host prim
          // itself), plus prim-state opinions (`active = false` on a variant
          // option deactivates the host — a common pruning pattern; authored
          // `hidden = false` / `active = true` are real opinions too, and an
          // option doc composes onto the prim).
          if (!vd->properties.empty() || !vd->relationships.empty() ||
              !vd->active || vd->hidden || vd->active_authored ||
              vd->hidden_authored || !vd->doc.empty()) {
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
          const std::string *selection =
              sit != sels->end() ? &sit->second : FallbackSelection(vss);
          if (!selection || selection->empty()) continue;
          const std::string holder =
              src.site + "/{" + vss.name + "=" + *selection + "}";
          if (Specs(src.stack_idx, holder).empty()) continue;  // not crate-style
          Src vsrc;
          vsrc.stack_idx = src.stack_idx;
          vsrc.site = holder;
          vsrc.map_idx = InternMapping(NamespaceMapping::Compose(
              Mapping(src.map_idx), NamespaceMapping{holder, src.site}));
          vsrc.offset = src.offset;
          vsrc.arc_kind = ArcType::Variant;
          const ExpansionFrame vframe{src.stack_idx, vsrc.map_idx, &vsrc.site, frame,
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

  std::vector<Src> ExpandList(const std::vector<Src> &base, std::string *warn,
                              std::string *err) {
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
    for (const Src &s : base) {
      // Carry a base source's own arc kind so a specialize re-rooted onto a
      // child stays globally weakest.
      std::vector<Src> *tgt = (s.arc_kind == ArcType::Specialize) ? &spec : &main;
      // Ancestor chain for implied class arcs: every layer stack (with the
      // mapping it was entered through) between the root stack and this source.
      // A re-rooted child source carries its parent's chain (Src::arc_chain), so
      // a class inherited by a referenced CHILD prim is still implied in the root
      // stack AND in every intermediate stack of the arc chain -- an override
      // authored on the class path at any of those levels composes.
      ArcChain chain;
      if (s.arc_chain) chain = *s.arc_chain;
      if (chain.empty() || chain.front().first != 0) {
        chain.insert(chain.begin(), {0u, 0u});
      }
      if (chain.back().first != s.stack_idx ||
          chain.back().second != s.map_idx) {
        chain.emplace_back(s.stack_idx, s.map_idx);
      }
      const ExpansionFrame seed{s.stack_idx, s.map_idx, &s.site, nullptr, 0};
      ExpandArcs(s, &seed, tgt, &spec, &sels, chain, warn, err);
    }
    main.insert(main.end(), spec.begin(), spec.end());
    return main;
  }

  // --- relocates ------------------------------------------------------------

  // Add a layer stack's namespace edits to a mapping FROM that stack into the
  // composed namespace, so paths this mapping translates (relationship and
  // connection TARGETS, implied class sites) land at the relocated address
  // rather than the vacated one: a connection authored in subrig.usd to
  // `/SubRig/Anim/AnimScope.x` must resolve to `/Char/Anim2/AnimScope.x` when
  // two enclosing layer stacks each relocated part of that path
  // (TrickyConnectionToRelocatedAttribute). Renames the ENCLOSING stacks
  // perform are already in `m` (NamespaceMapping::Compose pulls them into the
  // deeper namespace), so mapping each destination through `m` composes the
  // whole chain of edits.
  NamespaceMapping WithStackRelocates(uint32_t stack, NamespaceMapping m) const {
    const StackRelocates &rel = layer_stacks[stack].relocates;
    if (rel.empty()) return m;
    std::vector<NamespaceMapping::Pair> renames;
    renames.reserve(rel.src_to_dst.size());
    for (const auto &r : rel.src_to_dst) {
      // A relocate destination OUTSIDE the arc's namespace means the moved
      // content is not addressable through this arc: record a DROP-marker
      // pair (empty target) so relationship/connection targets under the
      // source are dropped like pxr, instead of leaking the raw dst path.
      renames.emplace_back(r.first,
                           m.crosses_arc ? m.ApplyTarget(r.second)
                                         : m.Apply(r.second));
    }
    for (auto &rename : renames) {
      m.AddRename(rename.first, rename.second);
    }
    return m;
  }

  // Sources-cache key. Stack 0 is the composed (root) namespace and keys by the
  // composed path alone, so every other consumer of the cache (ComposePrim,
  // Invalidate, the parallel warm) is unchanged. Sites expanded inside another
  // layer stack's own namespace (relocate sources) get a stack-qualified key.
  static std::string SourcesKey(uint32_t stack, const std::string &path) {
    return stack == 0 ? path : ("@" + UIntToStr(stack) + ":" + path);
  }

  // The namespace edits in effect at a composed prim, gathered from EVERY layer
  // stack that contributes a source to it: a relocate authored by any of them
  // (root layer stack, or one reached through a reference/payload/variant) edits
  // this prim's composed child namespace.
  struct ChildRelocations {
    std::set<std::string> departed;    // names the edit moved away
    std::vector<std::string> arrived;  // names the edit moved in (authored order)
    bool empty() const { return departed.empty() && arrived.empty(); }
  };
  ChildRelocations RelocationsAt(const std::vector<Src> &srcs) const {
    ChildRelocations out;
    std::set<std::string> seen;
    for (const Src &s : srcs) {
      if (s.variant) continue;
      const StackRelocates &rel = layer_stacks[s.stack_idx].relocates;
      if (rel.empty()) continue;
      if (const std::set<std::string> *d = rel.DepartedAt(s.site)) {
        out.departed.insert(d->begin(), d->end());
      }
      if (const std::vector<StackRelocates::Arrival> *a =
              rel.ArrivalsAt(s.site)) {
        for (const StackRelocates::Arrival &arrival : *a) {
          if (seen.insert(arrival.name).second) out.arrived.push_back(arrival.name);
        }
      }
    }
    return out;
  }

  // Graft a prim a relocate moved INTO this parent: first the opinions authored
  // AT the destination path in the same layer stack (strongest -- e.g. the root
  // layer's `over` on the new name), then the content from its pre-relocation
  // source site. `ps` is the parent source whose stack authored the relocate.
  void AddRelocatedSources(const Src &ps,
                           const StackRelocates::Arrival &arrival,
                           const std::string &child_composed,
                           std::vector<Src> *base, std::string *warn,
                           std::string *err) {
    // The parent's namespace mapping, plus the rename this relocate performs
    // (expressed in the parent stack's namespace -> composed namespace). Both
    // pairs are kept: paths outside the relocated subtree still map through the
    // arc (a connection to a non-relocated sibling), paths inside it land at the
    // new address (TrickyConnectionToRelocatedAttribute).
    const auto renamed_map = [&](const std::string &site) {
      NamespaceMapping m = Mapping(ps.map_idx);
      m.AddRename(site, child_composed);
      return m;
    };

    Src dst;
    dst.stack_idx = ps.stack_idx;
    dst.site = arrival.dst_site;
    dst.map_idx = InternMapping(renamed_map(arrival.dst_site));
    dst.offset = ps.offset;
    dst.arc_kind = ps.arc_kind;
    dst.expression_variables = ps.expression_variables;
    dst.arc_chain = ps.arc_chain;
    dst.ancestral = true;  // child derivation, like DeriveChildSources
    base->push_back(std::move(dst));

    const NamespaceMapping outer = renamed_map(arrival.src_site);
    const std::vector<Src> &content =
        SourcesForRelocateSource(ps.stack_idx, arrival.src_site, warn, err);
    for (const Src &s : content) {
      Src c = s;
      c.map_idx =
          InternMapping(NamespaceMapping::Compose(outer, Mapping(s.map_idx)));
      c.offset = ps.offset.Compose(s.offset);
      if (!c.expression_variables) c.expression_variables = ps.expression_variables;
      if (c.arc_kind == ArcType::Root) c.arc_kind = ArcType::Relocate;
      base->push_back(std::move(c));
    }
  }

  // Sources of the composed child `cn` (composed path `child_composed`) of a
  // prim whose sources are `psrcs`. Relocates authored by any contributing layer
  // stack apply here: a child the stack moved away contributes nothing from that
  // stack, and a child it moved in is pulled from its pre-relocation site.
  //
  // `raw_stack`/`raw_site` name a single source for which the departure rule is
  // suppressed -- used when the walk is deliberately addressing the relocate's
  // own source prim (see SourcesForRelocateSource).
  std::vector<Src> DeriveChildSources(const std::vector<Src> &psrcs,
                                      const std::string &cn,
                                      const std::string &child_composed,
                                      std::string *warn, std::string *err,
                                      uint32_t raw_stack = kInvalidStack,
                                      const std::string &raw_site = "") {
    std::vector<Src> base;
    base.reserve(psrcs.size());
    for (const Src &ps : psrcs) {
      // Variant sources are inline opinions on the prim itself; they do not
      // (yet) carry child prims, so they don't propagate to children.
      if (ps.variant) continue;
      const StackRelocates &rel = layer_stacks[ps.stack_idx].relocates;
      const bool raw = ps.stack_idx == raw_stack && ps.site == raw_site;
      if (!rel.empty() && !raw) {
        if (rel.IsDeparted(ps.site, cn)) continue;
        if (const StackRelocates::Arrival *arrival =
                rel.ArrivalOf(ps.site, cn)) {
          AddRelocatedSources(ps, *arrival, child_composed, &base, warn, err);
          continue;
        }
      }
      Src c;
      c.stack_idx = ps.stack_idx;
      c.site = ps.site + "/" + cn;
      c.map_idx = ps.map_idx;  // share the parent's mapping (no string copy)
      c.expression_variables = ps.expression_variables;
      c.offset = ps.offset;
      c.arc_kind = ps.arc_kind;
      c.arc_chain = ps.arc_chain;  // implied class arcs need the whole chain
      c.ancestral = true;          // arc was introduced at the parent
      base.push_back(std::move(c));
    }
    return base;
  }

  // Composition sources of a relocate's SOURCE prim, expanded in `stack`'s own
  // namespace. The composed namespace no longer holds this prim (its parent
  // there lists it as departed), and its ancestors inside `stack` may carry the
  // arcs that actually deliver it -- e.g. `/CharRig/Rig/SubRig` references
  // subrig.usd and the relocate moves `/CharRig/Rig/SubRig/Anim/AnimScope` up to
  // `/CharRig/Anim/AnimScope`. So the chain is expanded here rather than
  // re-rooted from the composed parent.
  const std::vector<Src> &SourcesForRelocateSource(uint32_t stack,
                                                   const std::string &site,
                                                   std::string *warn,
                                                   std::string *err) {
    const auto raw_key = [&](const std::string &path) {
      return "!" + UIntToStr(stack) + ":" + path;
    };
    const std::string key = raw_key(site);
    {
      auto it = sources_cache.find(key);
      if (it != sources_cache.end()) return it->second;
    }
    if (!sources_in_progress.insert(key).second) return empty_sources_;

    // The WHOLE ancestor chain is addressed raw in this stack, not just the leaf:
    // relocates chain, so an ancestor of this source may itself have been moved
    // away (`/Legs/RightRig/Leg_bone -> /Legs/LeftRig/Leg_bone/Leg_bone`, then
    // `.../Leg_bone/Leg_bone/Knee_bone -> /Knee_bone`). The composed namespace no
    // longer holds those ancestors, but the pre-relocation namespace this source
    // is expressed in does.
    std::vector<Path> pending;  // leaf -> topmost
    {
      Path cur(site);
      for (;;) {
        pending.push_back(cur);
        Path parent = cur.parent();
        if (parent.is_root() || parent.empty() || parent.str() == "/") break;
        if (sources_cache.count(raw_key(parent.str()))) break;
        cur = parent;
      }
    }

    const std::vector<Src> *result = nullptr;
    for (size_t i = pending.size(); i-- > 0;) {
      const Path &p = pending[i];
      const Path parent = p.parent();
      std::vector<Src> base;
      if (parent.is_root() || parent.empty() || parent.str() == "/") {
        Src s;
        s.stack_idx = stack;
        s.site = p.str();
        s.expression_variables = std::make_shared<const Value>(
            layer_stacks[stack].expression_variables);
        base.push_back(std::move(s));
      } else {
        const std::vector<Src> &psrc = sources_cache[raw_key(parent.str())];
        // Raw for this stack's own site (the departure that moved this prim out
        // of the composed namespace must not remove it here as well); sources
        // contributed by arcs into other stacks still honor their own relocates.
        base = DeriveChildSources(psrc, p.name(), p.str(), warn, err, stack,
                                  parent.str());
      }
      std::vector<Src> &slot = sources_cache[raw_key(p.str())];
      slot = ExpandList(base, warn, err);
      result = &slot;
    }
    sources_in_progress.erase(key);
    return *result;
  }

  // --- ancestral source resolution (cached) -------------------------------

  // The expanded composition sources for a composed prim `path`.
  const std::vector<Src> &SourcesForPath(const Path &path, std::string *warn,
                                         std::string *err) {
    return SourcesForSite(0, path, warn, err);
  }

  // The expanded composition sources for `path` in layer stack `stack`'s
  // namespace (stack 0 == the composed/root namespace). A root-level prim seeds
  // from that layer stack; a descendant derives each of its parent's expanded
  // sources at the child name (so arcs authored on an ancestor reach it, and so
  // relocates authored by any contributing stack apply), then expands that
  // child's own arcs. Cached per (stack, path).
  const std::vector<Src> &SourcesForSite(uint32_t stack, const Path &path,
                                         std::string *warn, std::string *err) {
    const std::string key = SourcesKey(stack, path.str());
    {
      auto it = sources_cache.find(key);
      if (it != sources_cache.end()) return it->second;
    }
    if (!sources_in_progress.insert(key).second) return empty_sources_;

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
        if (sources_cache.count(SourcesKey(stack, parent.str()))) break;
        cur = parent;
      }
    }

    const std::vector<Src> *result = nullptr;
    for (size_t i = pending.size(); i-- > 0;) {
      const Path &p = pending[i];
      const std::string key_i = SourcesKey(stack, p.str());

      std::vector<Src> base;
      Path parent = p.parent();
      const bool parent_is_root =
          parent.is_root() || parent.empty() || parent.str() == "/";
      if (parent_is_root) {
        // A relocate may move a prim to a NEW ROOT PRIM: its content comes from
        // the source site, while opinions authored at the new root name (an
        // `over` in the root layer) still compose on top.
        const StackRelocates &rel = layer_stacks[stack].relocates;
        const StackRelocates::Arrival *arrival = rel.ArrivalOf("/", p.name());
        Src s;
        s.stack_idx = stack;
        s.site = p.str();
        s.map_idx = InternMapping(WithStackRelocates(stack, NamespaceMapping{}));
        s.expression_variables = std::make_shared<const Value>(
            layer_stacks[stack].expression_variables);
        if (arrival) {
          AddRelocatedSources(s, *arrival, p.str(), &base, warn, err);
        } else {
          base.push_back(std::move(s));
        }
      } else {
        const std::vector<Src> &psrc = sources_cache[SourcesKey(stack, parent.str())];
        base = DeriveChildSources(psrc, p.name(), p.str(), warn, err);
      }

      // unordered_map element refs are stable across later inserts, and
      // pending[0] (the queried leaf) is filled last, so this captures it.
      std::vector<Src> &slot = sources_cache[key_i];
      slot = ExpandList(base, warn, err);
      result = &slot;
    }
    sources_in_progress.erase(key);
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
