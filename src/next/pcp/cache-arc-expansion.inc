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

  // A variantSet whose selection was unknown when its spec expanded
  // (authored at a weaker site): grafted by ProcessDeferredVariants at the
  // recorded strength position once the whole prim's selections are known.
  struct DeferredVariant {
    std::vector<Src> *out;
    std::vector<Src> *spec_out;
    size_t main_pos;
    size_t spec_pos;
    Src src;
    const PrimSpec *spec;
    std::string set_name;
    std::string layer_id;
    ArcChain chain;
  };
  std::vector<DeferredVariant> deferred_variants_;

  // Graft every deferred variantSet owned by this ExpandList invocation whose
  // selection is now known (fixed-point: a grafted option's own selections can
  // enable further deferred sets), splicing the result at the recorded
  // position. Leftover entries (still no selection anywhere) are dropped.
  void ProcessDeferredVariants(std::vector<Src> *main_list,
                               std::vector<Src> *spec_list,
                               std::map<std::string, std::string> *sels,
                               std::string *warn, std::string *err) {
    const auto owned = [&](const DeferredVariant &e) {
      return e.out == main_list || e.out == spec_list;
    };
    for (int round = 0; round < 8; ++round) {
      bool applied_any = false;
      for (size_t ei = 0; ei < deferred_variants_.size(); ++ei) {
        if (!owned(deferred_variants_[ei])) continue;
        const VariantSetData *vss = nullptr;
        for (const VariantSetData &v :
             deferred_variants_[ei].spec->meta().variantSets()) {
          if (v.name == deferred_variants_[ei].set_name) {
            vss = &v;
            break;
          }
        }
        std::string chosen;
        if (vss) {
          auto sit = sels->find(deferred_variants_[ei].set_name);
          if (sit != sels->end()) {
            chosen = sit->second;
          } else if (const std::string *fb = FallbackSelection(*vss)) {
            chosen = *fb;
          }
        }
        if (!vss || chosen.empty()) continue;

        // Take the entry out BEFORE applying: the graft may push new deferred
        // entries (nested sets) and reallocate the vector.
        DeferredVariant e = std::move(deferred_variants_[ei]);
        deferred_variants_.erase(deferred_variants_.begin() +
                                 static_cast<ptrdiff_t>(ei));
        --ei;

        const VariantData *vd = nullptr;
        for (const VariantData &o : vss->variants) {
          if (o.name == chosen) {
            vd = &o;
            break;
          }
        }
        std::vector<Src> tmp_main;
        std::vector<Src> tmp_spec;
        if (vd) {
          // The chosen option's sibling-set selections become visible first
          // (first-wins: a stronger recorded selection stays).
          for (const auto &op : vd->variantSelections) {
            sels->emplace(op.first, op.second);
          }
          ApplyVariantOption(e.src, nullptr, e.set_name, vd, 0, e.layer_id,
                             &tmp_main, &tmp_spec, sels, e.chain, warn, err);
        }
        // Crate-holder representation of the same set.
        const std::string holder =
            e.src.site + "/{" + e.set_name + "=" + chosen + "}";
        if (!Specs(e.src.stack_idx, holder).empty()) {
          Src vsrc;
          vsrc.stack_idx = e.src.stack_idx;
          vsrc.site = holder;
          vsrc.map_idx = InternMapping(NamespaceMapping::Compose(
              Mapping(e.src.map_idx), NamespaceMapping{holder, e.src.site}));
          vsrc.offset = e.src.offset;
          vsrc.arc_kind = ArcType::Variant;
          vsrc.implied_anchor = e.src.implied_anchor;
          ExpandArcs(vsrc, nullptr, &tmp_main, &tmp_spec, sels, e.chain, warn,
                     err);
        }

        const auto splice = [&](std::vector<Src> *dst, size_t pos,
                                std::vector<Src> *tmp) {
          if (tmp->empty()) return;
          if (pos > dst->size()) pos = dst->size();
          const size_t n = tmp->size();
          // Shift positions of EXISTING entries in the destination list first,
          // then retarget entries recorded during this graft (they point into
          // the tmp vector, with tmp-relative positions).
          for (DeferredVariant &de : deferred_variants_) {
            if (de.out == dst && de.main_pos >= pos) de.main_pos += n;
            if (de.spec_out == dst && de.spec_pos >= pos) de.spec_pos += n;
          }
          for (DeferredVariant &de : deferred_variants_) {
            if (de.out == tmp) {
              de.out = dst;
              de.main_pos += pos;
            }
            if (de.spec_out == tmp) {
              de.spec_out = dst;
              de.spec_pos += pos;
            }
          }
          dst->insert(dst->begin() + static_cast<ptrdiff_t>(pos),
                      std::make_move_iterator(tmp->begin()),
                      std::make_move_iterator(tmp->end()));
          tmp->clear();
        };
        splice(e.out, e.main_pos, &tmp_main);
        splice(e.spec_out, e.spec_pos, &tmp_spec);
        applied_any = true;
      }
      if (!applied_any) break;
    }
    deferred_variants_.erase(
        std::remove_if(deferred_variants_.begin(), deferred_variants_.end(),
                       owned),
        deferred_variants_.end());
  }

  // One selected variant OPTION grafted onto its host: content subtree,
  // inline opinions, option-authored composition arcs, and nested sets.
  // (Extracted from ExpandArcs so DEFERRED variant sets — whose selection is
  // authored at a WEAKER site and only known once the whole prim expanded —
  // can be grafted afterwards at their recorded strength position.)
  void ApplyVariantOption(const Src &src, const ExpansionFrame *frame,
                          const std::string &set_name, const VariantData *vd,
                          int vdepth, const std::string &layer_id,
                          std::vector<Src> *out, std::vector<Src> *spec_out,
                          std::map<std::string, std::string> *sels,
                          const ArcChain &chain, std::string *warn,
                          std::string *err) {

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
              vd->hidden_authored || !vd->doc.empty() || !vd->kind.empty()) {
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
                       frame, out, spec_out, sels, chain, warn, err, layer_id);
          }
          for (const std::string &s : vd->references) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Reference,
                       frame, out, spec_out, sels, chain, warn, err, layer_id);
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
                         chain, warn, err, layer_id);
            }
            // insert-only here: another arc group's loaded payloads must not
            // clear a deferral recorded for this option's payloads.
            if (any_deferred) deferred_payload_prims.insert(root_prim_path);
          }
          for (const std::string &s : vd->specializes) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Specialize,
                       frame, out, spec_out, sels, chain, warn, err, layer_id);
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
                  ApplyVariantOption(src, frame, nvs.name, &nvd, vdepth + 1,
                                     layer_id, out, spec_out, sels, chain, warn,
                                     err);
                  break;
                }
              }
            }
          }
  }

  // Does `path` land in namespace edited by `stack`'s relocates? Source-side
  // hits are PROHIBITED arc targets (pxr: cannot address pre-relocation
  // namespace); target-side hits must resolve through the composed/arrival
  // machinery so relocation-source suppression applies.
  bool PathInRelocateSource(uint32_t stack, const std::string &path) const {
    const StackRelocates &rel = layer_stacks[stack].relocates;
    if (rel.empty()) return false;
    for (const auto &r : rel.src_to_dst) {
      if (IsPathAtOrUnder(path, r.first)) return true;
    }
    return false;
  }
  bool PathInRelocateTarget(uint32_t stack, const std::string &path) const {
    const StackRelocates &rel = layer_stacks[stack].relocates;
    if (rel.empty()) return false;
    for (const auto &r : rel.src_to_dst) {
      if (!r.second.empty() && IsPathAtOrUnder(path, r.second)) return true;
    }
    return false;
  }

  // Does any STRICT ancestor of `path` in `stack` author a variantSet? Such a
  // target's composition depends on a variant selection that may be resolved in
  // an OUTER layer stack (a reference selects the variant): the isolated
  // target-stack re-derive uses that stack's DEFAULT selection and grafts the
  // wrong variant's children into the arc target (TrickyInheritsInVariants:
  // /Model/Scope inherits /Model/Class, whose children come from /Model's
  // variantSet — model.usd defaults complexity=low but the root selects high).
  // Reusing the composed target instead picks up the authoritative selection.
  bool AncestorAuthorsVariantSet(uint32_t stack,
                                 const std::string &path) const {
    Path cur(path);
    for (;;) {
      Path parent = cur.parent();
      if (parent.is_root() || parent.empty() || parent.str() == "/") break;
      for (const SpecRef &sr : Specs(stack, parent.str())) {
        if (!sr.spec->meta().variantSets().empty()) return true;
      }
      cur = parent;
    }
    return false;
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
        PathInRelocateSource(src.stack_idx, arc.prim_path)) {
      // An internal arc addressing a relocation SOURCE path is invalid
      // ("salted earth": BadRigInstance inheriting the class's pre-move
      // path composes nothing in pxr).
      AddIssue(ErrorCode::UnresolvedPrimPath, src.site,
               "Arc targets a relocation source path (prohibited): " +
                   arc.prim_path,
               warn);
      return;
    }
    if ((arc.is_internal || evaluated_asset_path.empty()) &&
        !arc.prim_path.empty() &&
        (Specs(src.stack_idx, arc.prim_path).empty() ||
         PathInRelocateTarget(src.stack_idx, arc.prim_path) ||
         AncestorAuthorsVariantSet(src.stack_idx, arc.prim_path))) {
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

    // A reference/payload to a relocation SOURCE path in the TARGET layer stack
    // is prohibited (pxr "salted earth"): the arc composes nothing, exactly like
    // the internal-arc case checked above but for external @asset@ targets
    // (ErrorInvalidReferenceToRelocationSource /Bad_RefToPreRelo references
    // char.usd</Char/PreRelo>, which char.usd relocated away).
    if (!evaluated_asset_path.empty() &&
        PathInRelocateSource(arc_stack_idx, arc_site)) {
      AddIssue(ErrorCode::UnresolvedPrimPath, arc_site,
               "Reference targets a relocation source path (prohibited): " +
                   arc_site,
               warn);
      return;
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

    // Same rule in COMPOSED space: a same-stack internal arc whose target maps
    // to an ANCESTOR of this prim's own composed path is a cycle even when the
    // source sites live under different roots (co-recursive inherits across
    // sibling root prims: CoRecursiveParent1/Child1 inherits CoRecursiveParent2
    // whose child inherits back — the arc target composes to /CoRecursiveParent1,
    // an ancestor of the composed /CoRecursiveParent1/Child1/Child2). pxr drops
    // the arc, leaving the re-entrant prim as a bare `over`. The source-site
    // check above misses this because the sites are under different roots.
    if (arc_stack_idx == src.stack_idx) {
      const std::string cur_composed = Mapping(src.map_idx).Apply(src.site);
      const std::string arc_composed = Mapping(src.map_idx).Apply(arc_site);
      if (arc_composed != cur_composed &&
          IsPathAtOrUnder(cur_composed, arc_composed)) {
        AddIssue(ErrorCode::ArcCycle, src.site,
                 "Composition cycle detected: arc composed target " +
                     arc_composed + " is an ancestor of " + cur_composed,
                 err);
        return;
      }
    }

    NamespaceMapping local{arc_site, src.site};
    // A LOCAL class-based arc (class and instance in the same layer stack)
    // gets pxr's identity-plus-pair map function: targets outside the class
    // namespace pass through; targets under the instance are invalid
    // instance targets and drop (see NamespaceMapping::intra_stack).
    if (IsClassBasedArc(kind) && arc_stack_idx == src.stack_idx) {
      local.intra_stack = true;
    }
    Src arc_src;
    arc_src.stack_idx = arc_stack_idx;
    arc_src.site = arc_site;
    // A subtree expanded under an implied class stays anchored to the same
    // expressing stack (the whole block composes at that strength position).
    arc_src.implied_anchor = src.implied_anchor;
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
        // A variant selection the INHERITING prim (instance) authors on ITS OWN
        // path in this same ancestor stack beats the class's implied selection
        // (pxr: an instance's direct selection is stronger than the selection it
        // inherits from its class -- TrickySpookyVariantSelectionInClass, where
        // RightLegRig authors LegRigStyle=2Leg but inherits SymLegRig whose root
        // over selects 1Leg). Read the instance's own selections at this stack
        // (the class arc's destination, mapped back into `as`) and never let the
        // class overwrite them.
        std::map<std::string, std::string> own_selections;
        {
          const std::string dest = Mapping(src.map_idx).Apply(src.site);
          const std::string dest_in_as =
              Mapping(ancestor_map_idx).ReverseApply(dest);
          for (const SpecRef& own_spec : Specs(as, dest_in_as)) {
            RecordSelections(*own_spec.spec, &own_selections,
                             arc_src.expression_variables, warn, err);
          }
        }
        for (const auto& selection : ancestor_selections) {
          if (own_selections.count(selection.first)) continue;  // instance wins
          (*sels)[selection.first] = selection.second;
        }
        Src implied = arc_src;
        implied.stack_idx = as;
        implied.implied_anchor = as;  // composes at stack `as`'s position
        if (implied_site != arc_site) {
          implied.site = implied_site;
          // Implied specs live at implied_site and compose onto the SAME
          // destination the direct class arc maps to -- the INHERITING prim in
          // root space (`destination`), not the class prim's own composed path.
          // These differ when the inherit is authored inside a referenced layer
          // stack (src.map_idx non-identity); mapping to the class path would
          // land the implied opinions (and their relationship targets) on the
          // class instead of on the prim that inherits it.
          const std::string destination = Mapping(src.map_idx).Apply(src.site);
          NamespaceMapping im{implied_site, destination};
          // A relocate authored in the arc's (referenced) stack that moves a
          // DESCENDANT of the inheriting prim must chain into this implied map:
          // a connection/relationship target authored in the CLASS namespace
          // that points at a relocated descendant otherwise stops at the
          // pre-relocate inherited path. Add, per such relocate, a class-namespace
          // rename so one longest-prefix ApplyTarget does inherit-then-relocate
          // (TrickyConnectionToRelocatedAttribute: SymEyeRig/Anim.baz -> LEye.baz,
          // where LEyeRig/Anim->LEye lives in the FaceRig stack).
          const StackRelocates &arel = layer_stacks[arc_stack_idx].relocates;
          if (!arel.empty()) {
            const NamespaceMapping &amap = Mapping(src.map_idx);
            // Arc pairs only (relocate renames stripped): maps a relocate SOURCE
            // to its PRE-relocate composed path so it lands under `destination`;
            // the full amap (relocates baked in by WithStackRelocates) gives the
            // post-relocate destination.
            NamespaceMapping arc_only = amap;
            {
              std::vector<NamespaceMapping::Pair> kept;
              for (const auto &pr : arc_only.pairs)
                if (!arel.src_to_dst.count(pr.first)) kept.push_back(pr);
              arc_only.pairs = std::move(kept);
            }
            for (const auto &r : arel.src_to_dst) {
              if (r.second.empty()) continue;
              const std::string rsrc = arc_only.crosses_arc
                                           ? arc_only.ApplyTarget(r.first)
                                           : arc_only.Apply(r.first);
              const std::string rdst = amap.crosses_arc
                                           ? amap.ApplyTarget(r.first)
                                           : amap.Apply(r.first);
              if (rsrc.empty() || rdst.empty()) continue;
              if (!NamespaceMapping::AtOrUnder(rsrc, destination)) continue;
              im.AddRename(implied_site + rsrc.substr(destination.size()), rdst);
            }
          }
          implied.map_idx = InternMapping(std::move(im));
        }
        const ArcChain ichain{{as, implied.map_idx}};
        const ExpansionFrame iframe{as, implied.map_idx, &implied.site, frame,
                                    frame ? frame->depth + 1 : 0};
        ExpandArcs(implied, &iframe, target, spec_out, sels, ichain, warn, err);
      }

      // Same-stack implied class/specialize: this arc's target may map (through
      // an ANCESTOR class arc in THIS stack -- `/A specializes /_class_`) to a
      // DIFFERENT path that carries opinions. `/A/render` specializing
      // `/_class_/defaultImplementation` therefore ALSO implies a specialize to
      // `/A/defaultImplementation` (root_implied_site == Apply(arc_site)), whose
      // ancestor-context override composes STRONGER than the direct target
      // (SpecializesAndVariants4, VariantSpecializesAndReferenceSurprising-
      // Behavior). Add it before the direct target below, mirroring the chain
      // loop but for the current stack (which that loop skips via as==arc_stack).
      if (root_implied_site != arc_site &&
          !Specs(arc_stack_idx, root_implied_site).empty()) {
        Src implied = arc_src;
        implied.stack_idx = arc_stack_idx;
        implied.site = root_implied_site;
        implied.implied_anchor = arc_stack_idx;
        implied.map_idx = InternMapping(NamespaceMapping{
            root_implied_site, Mapping(src.map_idx).Apply(src.site)});
        const ArcChain ichain{{arc_stack_idx, implied.map_idx}};
        const ExpansionFrame iframe{arc_stack_idx, implied.map_idx,
                                    &implied.site, frame,
                                    frame ? frame->depth + 1 : 0};
        ExpandArcs(implied, &iframe, target, spec_out, sels, ichain, warn, err);
      } else if (root_implied_site != arc_site &&
                 kind == ArcType::Specialize) {
        // The implied specialize target (`root_implied_site`, e.g. /Model/Material)
        // composes via ARCS, with no raw spec of its own. pxr propagates the
        // specialize to it, and its contributors reached via an arc ANCESTRAL to
        // the specialize-declaration (a prepend reference on the INSTANCE, not
        // inside the selected variant) compose STRONGER than the DIRECT target —
        // the reverse of the target's own composition. But a contributor that is
        // also part of the DIRECT target's (`arc_site`) OWN composition (a
        // reference authored INSIDE the variant that carries the declaration) is
        // NOT ancestral and must stay in normal order.
        //   VariantSpecializesAndReferenceSurprisingBehavior: the reference is on
        //     /Model (ancestral) -> reference>variant, Material_Child.myInt=0.
        //   VariantSpecializesAndReference: the reference is inside the variant
        //     (on /New_Shading_Variant) -> variant>reference, myInt=1 (unchanged).
        // Route only the contributors of `root_implied_site` that the DIRECT
        // target does NOT already deliver, ahead of it (stronger).
        std::set<std::pair<uint32_t, std::string>> direct_sites;
        for (const Src &ds :
             SourcesForSite(arc_stack_idx, Path(arc_site), warn, err))
          direct_sites.emplace(ds.stack_idx, ds.site);
        const std::string destination = Mapping(src.map_idx).Apply(src.site);
        NamespaceMapping redirect;
        redirect.AddRename(root_implied_site, destination);
        for (const Src &cs :
             SourcesForSite(arc_stack_idx, Path(root_implied_site), warn, err)) {
          if (cs.variant) continue;  // the variant graft that carries the decl
          if (cs.arc_kind == ArcType::Root && cs.site == root_implied_site)
            continue;  // the empty composed-prim placeholder
          if (direct_sites.count({cs.stack_idx, cs.site}))
            continue;  // delivered by the DIRECT specialize (not ancestral)
          Src si = cs;
          si.map_idx =
              InternMapping(NamespaceMapping::Compose(redirect, Mapping(cs.map_idx)));
          si.arc_kind = ArcType::Specialize;
          si.implied_anchor = arc_stack_idx;
          si.ancestral = true;
          spec_out->push_back(std::move(si));
        }
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
      // A relocation-source anchor stays suppressed when reached THROUGH an arc
      // (a reference to a relocate arrival re-delivers the arrival's sources,
      // including the prohibited source-site opinions): without carrying the
      // flag, the source's specs and children leak into the referencing prim
      // (ErrorInvalidReferenceToRelocationSource /Good_RefToRelocated).
      s.suppress_site_specs = target_src.suppress_site_specs;
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

      // Extend the persisted arc-site trail with EVERY arc hop (cross-stack
      // references AND same-stack internal inherits/specializes), so both an
      // ancestral reference cycle (AnotherParent) and a same-stack co-recursive
      // inherit cycle (CoRecursiveParent1<->2) are detectable after the live
      // frame chain is gone (see Src::arc_sites). A benign re-reference does NOT
      // false-positive: a LOCAL child derives from its local parent with a clean
      // trail (only content reached THROUGH the arc carries the target), so a
      // hit means the arc genuinely re-enters its own derivation path.
      {
        auto sites = std::make_shared<std::vector<std::pair<uint32_t, std::string>>>();
        if (src.arc_sites) *sites = *src.arc_sites;
        sites->emplace_back(s.stack_idx, s.site);
        s.arc_sites = std::move(sites);
      }

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

    // A relocation-source anchor: its own specs (and thus their arcs and
    // variant selections) are prohibited opinions — expand nothing.
    if (src.suppress_site_specs) return;

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
        for (const SelectedVariant &sv : SelectVariants(*spec, *sels)) {
          ApplyVariantOption(src, frame, *sv.set_name, sv.vd, 0, sr.layer_id,
                             out, spec_out, sels, chain, warn, err);
        }
        // A variantSet whose selection is authored at a WEAKER site (not yet
        // expanded) cannot graft now, but its opinions must compose at THIS
        // position (variants sit between inherits and references). Record a
        // deferred entry; ExpandList grafts it once every selection is known
        // and splices the result back here (pxr computes selections across
        // the whole index before applying each node's sets —
        // TrickyVariantWeakerSelection).
        for (const VariantSetData &vss : spec->meta().variantSets()) {
          if (sels->count(vss.name) || FallbackSelection(vss)) continue;
          deferred_variants_.push_back(DeferredVariant{
              out, spec_out, out->size(), spec_out->size(), src, spec,
              vss.name, sr.layer_id, chain});
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
      // Re-seed the cycle-detection frame from the persisted arc-site trail:
      // the live frame chain reset when the BuildStage walk cached this source
      // and derived it as a child, so an arc that re-targets a site crossed to
      // REACH this source is otherwise invisible. Rebuilding synthetic frames
      // (only stack_idx + site matter to frame->Contains) lets ProcessArc drop
      // the cyclic arc — pxr's ancestral-reference-cycle behavior. The strings
      // live in s.arc_sites (held for this call), so the pointers stay valid.
      std::vector<ExpansionFrame> anc_frames;
      const ExpansionFrame *anc = nullptr;
      if (s.arc_sites && !s.arc_sites->empty()) {
        anc_frames.reserve(s.arc_sites->size());
        for (const auto &hop : *s.arc_sites) {
          anc_frames.push_back(
              ExpansionFrame{hop.first, 0u, &hop.second, anc, 0u});
          anc = &anc_frames.back();
        }
      }
      const ExpansionFrame seed{s.stack_idx, s.map_idx, &s.site, anc, 0};
      ExpandArcs(s, &seed, tgt, &spec, &sels, chain, warn, err);
    }
    // Variant sets whose selection was authored at a WEAKER site graft now,
    // at their recorded strength position (pxr resolves selections across the
    // whole prim index before applying each node's sets).
    ProcessDeferredVariants(&main, &spec, &sels, warn, err);
    // Shadow STALE variant content: a source whose site bakes in a variant
    // selector `{Set=Val}` that CONFLICTS with the finally-resolved selection
    // (sels[Set] != Val) is a superseded variant option's content. It reaches
    // here when a class's own (weaker) selection eagerly grafted its option at
    // the class site, then the INHERITING instance re-selected the same set to a
    // different value (TrickySpookyVariantSelectionInClass: SymLegRig's default
    // 1Leg content arrives via the inherit AND RightLegRig re-selects 2Leg;
    // Inherit outranks Variant, so the stale 1Leg would otherwise win). pxr
    // resolves ONE selection per set across the whole prim index, so drop the
    // conflicting content. Only fires when the set was actually re-selected --
    // an unmentioned set (sels has no entry) keeps all its content.
    const auto has_conflicting_selector = [&](const std::string &site) {
      for (size_t i = 0; (i = site.find('{', i)) != std::string::npos;) {
        const size_t close = site.find('}', i);
        if (close == std::string::npos) break;
        const size_t eq = site.find('=', i);
        if (eq != std::string::npos && eq < close) {
          const std::string set = site.substr(i + 1, eq - i - 1);
          const std::string val = site.substr(eq + 1, close - eq - 1);
          auto it = sels.find(set);
          if (it != sels.end() && it->second != val) return true;
        }
        i = close + 1;
      }
      return false;
    };
    for (std::vector<Src> *lst : {&main, &spec}) {
      lst->erase(std::remove_if(lst->begin(), lst->end(),
                                [&](const Src &s) {
                                  return has_conflicting_selector(s.site);
                                }),
                 lst->end());
    }
    // pxr strength for IMPLIED classes: the implied block composes at its
    // expressing ancestor stack's position — immediately after that stack's
    // own positional opinions — not at the position of the reference that
    // introduced the inherit. (BasicInstancing pcp.txt prim stack:
    // root-local, root-implied-class, set-ancestral, set-implied-class,
    // prop-ref, prop-class.) Move each contiguous implied block up to just
    // after the last earlier non-implied source of its anchor stack.
    for (size_t i = 0; i < main.size();) {
      const uint32_t anchor = main[i].implied_anchor;
      if (anchor == UINT32_MAX) {
        ++i;
        continue;
      }
      size_t k = i + 1;
      while (k < main.size() && main[k].implied_anchor == anchor) ++k;
      size_t ins = SIZE_MAX;
      for (size_t j = i; j-- > 0;) {
        // Insert just after the anchor stack's last positional opinion OR the
        // last ALREADY-RELOCATED implied sibling of the same anchor. The latter
        // keeps a chain of implied classes (A inherits B inherits C, all
        // implied into the same ancestor stack) in DFS strength order: without
        // it every block collapses to the same slot and the chain INVERTS
        // (weakest class ends up strongest — TrickyClassHierarchy).
        if ((main[j].implied_anchor == UINT32_MAX &&
             main[j].stack_idx == anchor) ||
            (main[j].implied_anchor == anchor && main[j].stack_idx == anchor)) {
          ins = j + 1;
          break;
        }
      }
      if (ins == SIZE_MAX || ins >= i) {
        i = k;
        continue;
      }
      std::rotate(main.begin() + static_cast<ptrdiff_t>(ins),
                  main.begin() + static_cast<ptrdiff_t>(i),
                  main.begin() + static_cast<ptrdiff_t>(k));
      i = k;
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
    // A relationship/connection target that points at a relocated prim maps to
    // the relocate destination. The pointing target may be expressed in EITHER
    // namespace, so emit a rename from both source spellings:
    //   * the RAW (pre-ancestral-relocation) source (`src_to_dst`) -- a target
    //     delivered by a reference that lands at the pre-relocation path
    //     (BasicRelocateToAnimInterface: pathRig.usd referenced at PathRig,
    //     before Path is relocated out);
    //   * the COMPOSED (as-authored) source (`arrivals.src_site`) -- a target
    //     delivered by a reference that lands at the POST-relocation path (an
    //     `over` on an ancestral arrival: TrickyMultipleRelocations2 rigRel from
    //     rig.usd referenced at the composed `over Model_2/Rig`).
    // Longest-prefix matching in Apply picks the right one; when raw==composed
    // (non-chained) the two pairs coincide harmlessly.
    const auto add = [&](const std::string &src, const std::string &dst) {
      // A DELETION edit (empty destination) contributes no rename pair: pxr
      // keeps targets to deleted paths as authored (RelocateToNone). A relocate
      // destination OUTSIDE the arc's namespace maps to "" (a DROP-marker) so
      // targets under the source are dropped like pxr, not leaked.
      if (dst.empty()) return;
      renames.emplace_back(src,
                           m.crosses_arc ? m.ApplyTarget(dst) : m.Apply(dst));
    };
    renames.reserve(rel.src_to_dst.size());
    for (const auto &r : rel.src_to_dst) add(r.first, r.second);
    for (const auto &kv : rel.arrivals) {
      for (const StackRelocates::Arrival &a : kv.second) {
        add(a.src_site, a.dst_site);
      }
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

  // The relocated CONTENT sources for an arrival (the source prim's own
  // composition sources, before the departure-suppression + outer rename that
  // AddRelocatedSources composes on top). `child_composed` is the arrival's
  // destination composed path (the recursion-guard key).
  //
  // Step 0: forwards to the isolated relocate-source walk. Step 1 will derive
  // this from the source's COMPOSED parent when not re-entrant, so the caller's
  // variant/implied-class context reaches the relocated prim; the guard
  // (`reloc_content_in_progress`) falls back to the isolated walk on the
  // re-entrant chained-relocate shapes that would otherwise recurse.
  // Same-stack INTERNAL-REFERENCE content: a relocate follows content brought in
  // via arcs into OTHER layer stacks, and same-stack content delivered by an
  // INHERIT (the relocated prim's own class opinions -- TrickyMultipleRelocations4
  // inherits /SymRig), but NOT content delivered by a same-stack (internal)
  // REFERENCE from a DIFFERENT subtree (pxr per-arc scoping). Salt the latter --
  // its specs (incl. a `def` specifier) must not compose, while it stays
  // traversable (RelocatePrimsWithSameName).
  void SaltInternalRefContent(std::vector<Src> &content, const Src &ps,
                              const StackRelocates::Arrival &arrival,
                              std::string *warn, std::string *err) {
    for (Src &c : content) {
      if (c.suppress_site_specs) continue;
      if (FlowsThroughRelocateSource(c, arrival) ||
          ContentSiteDeparted(c, ps, arrival, warn, err)) {
        c.suppress_site_specs = true;
      }
    }
  }

  // Is this content source's leaf name relocated AWAY (departed) under its
  // parent in the COMPOSED namespace -- e.g. via a reference to a relocating
  // stack (RelocatePrimsWithSameName: /ChainedRef_1/Child is departed because
  // /ChainedRef_1 references /ChainedRef_2 which relocates Child->Child_2)?
  // Such content does not compose. Distinct from an arc-introduced relocated
  // prim that is NOT departed (test_relocates /Lib/Model/Inner -> /World/New).
  // Only checked for same-stack (relocating stack) content whose site sits
  // outside the source subtree -- content brought in by an internal reference.
  bool ContentSiteDeparted(const Src &c, const Src &ps,
                           const StackRelocates::Arrival &arrival,
                           std::string *warn, std::string *err) {
    if (c.stack_idx != ps.stack_idx || c.arc_kind != ArcType::Reference)
      return false;
    if (IsPathAtOrUnder(c.site, arrival.src_site)) return false;
    const Path cp(c.site);
    const Path parent = cp.parent();
    if (parent.is_root() || parent.empty() || parent.str() == "/") return false;
    // Guard the re-entrant composition of the parent.
    const std::string gk = "$dep:" + UIntToStr(c.stack_idx) + ":" + parent.str();
    if (!reloc_content_in_progress.insert(gk).second) return false;
    const std::vector<Src> psrcs = SourcesForSite(0, parent, warn, err);
    reloc_content_in_progress.erase(gk);
    const ChildRelocations edits = RelocationsAt(psrcs);
    return edits.departed.count(cp.name()) != 0;
  }

  // Did this content source arrive THROUGH a relocation-source path in an
  // intermediate contributing stack? Its `arc_sites` trail records the (stack,
  // target) of every arc it crossed; if the source's leaf name under any
  // INTERMEDIATE arc target is a relocation source there, that stack relocated
  // the prim away, so the content DEPARTED and must not compose (chained
  // departure through a reference chain -- RelocatePrimsWithSameName:
  // base.usd/Base/Child arrives via /ChainedRef_2 where /ChainedRef_2/Child is a
  // relocation source). EXCLUDE the arrival's OWN source (composing the arrival
  // OF that same relocate legitimately pulls this content). Salted (not dropped)
  // so it stays traversable.
  bool FlowsThroughRelocateSource(const Src &c,
                                  const StackRelocates::Arrival &arrival) const {
    // Only REFERENCE-delivered content: the leaf-name-under-target suffix is
    // valid only across NAME-PRESERVING reference arcs. An INHERIT renames its
    // subtree (SymRig -> LRig), so the intermediate path would be mis-computed
    // (TrickyRelocationOfPrimFromPayload false-positive on inherited /Rig/SymRig).
    if (c.arc_kind != ArcType::Reference) return false;
    if (!c.arc_sites || c.arc_sites->size() < 2) return false;
    const auto &trail = *c.arc_sites;
    const std::string &leaf_tgt = trail.back().second;
    if (!NamespaceMapping::AtOrUnder(c.site, leaf_tgt)) return false;
    const std::string suffix = c.site.substr(leaf_tgt.size());  // e.g. "/Child"
    if (suffix.empty()) return false;
    for (size_t i = 0; i + 1 < trail.size(); ++i) {
      const std::string inter = trail[i].second + suffix;
      // The current arrival's OWN relocate source is not a departure here.
      if (inter == arrival.src_site ||
          NamespaceMapping::AtOrUnder(inter, arrival.src_site) ||
          NamespaceMapping::AtOrUnder(arrival.src_site, inter))
        continue;
      if (PathInRelocateSource(trail[i].first, inter)) return true;
    }
    return false;
  }

  std::vector<Src> SourcesForRelocatedContent(
      const Src &ps, const StackRelocates::Arrival &arrival,
      const std::string &child_composed, std::string *warn, std::string *err) {
    const std::string guard_key =
        "@" + UIntToStr(ps.stack_idx) + ":" + child_composed;
    // Cross-stack relocate validity: a relocate whose SOURCE is itself a
    // relocation source (departed) in a CONTRIBUTING REFERENCED stack -- not the
    // relocating stack itself -- is invalid; pxr composes nothing (an empty
    // `over`, ErrorInvalidReferenceToRelocationSource: the root relocates
    // /ReferencedChar/PreRelo, but char.usd already relocated Char/PreRelo away).
    // Detect by composing the source's parent and checking whether a referenced
    // stack departed this name. (Only for a root-authored relocate, where the
    // source path is already in the composed namespace.)
    if (ps.stack_idx == 0 && isolated_reloc_depth_ == 0 &&
        reloc_content_in_progress.empty()) {
      const Path src_p(arrival.src_site);
      const Path parent = src_p.parent();
      if (!(parent.is_root() || parent.empty() || parent.str() == "/")) {
        reloc_content_in_progress.insert(guard_key);
        const std::vector<Src> psrcs = SourcesForSite(0, parent, warn, err);
        reloc_content_in_progress.erase(guard_key);
        const std::string name = src_p.name();
        for (const Src &s : psrcs) {
          if (s.variant || s.stack_idx == ps.stack_idx) continue;
          const StackRelocates &rel = layer_stacks[s.stack_idx].relocates;
          if (!rel.empty() && rel.IsDeparted(s.site, name)) {
            return {};  // invalid relocate -> empty over
          }
        }
      }
    }
    if (ComposedRelocApplicable(ps, arrival)) {
      reloc_content_in_progress.insert(guard_key);
      std::vector<Src> comp =
          ComposedRelocatedContent(ps, arrival, child_composed, warn, err);
      reloc_content_in_progress.erase(guard_key);
      SaltInternalRefContent(comp, ps, arrival, warn, err);
      return comp;
    }
    std::vector<Src> iso =
        IsolatedRelocatedContent(ps, arrival, child_composed, warn, err);
    SaltInternalRefContent(iso, ps, arrival, warn, err);
    // Chained relocate: the isolated walk resolves in the relocating stack's own
    // namespace, so its implied-class propagation chain never reaches ROOT and it
    // MISSES a root over on an INHERITED class that the relocate moved (e.g.
    // JointBlend on /CharRig/Rig/LegsRig/SymLegRig/.../Seg2 inherited into the
    // relocated Knee -- TrickyMultipleRelocationsAndClasses2). The composed
    // walk-back derivation (FullyUnrelocate + SourcesForSite(0,..)) DOES compose
    // it. SUPPLEMENT the isolated content with the composed walk's ROOT-stack
    // sources it lacks -- without REPLACING the isolated content, which is
    // correct (incl. post-relocation `over` opinions the walk-back would drop,
    // TrickyMultipleRelocations2).
    if (ComposedBaseApplicable(ps, arrival) && IsChainedArrival(ps, arrival)) {
      reloc_content_in_progress.insert(guard_key);
      std::vector<Src> comp =
          ComposedRelocatedContent(ps, arrival, child_composed, warn, err);
      reloc_content_in_progress.erase(guard_key);
      for (Src &c : comp) {
        if (c.stack_idx != 0 || c.suppress_site_specs) continue;
        if (Specs(c.stack_idx, c.site).empty()) continue;
        bool present = false;
        for (const Src &i : iso)
          if (i.stack_idx == c.stack_idx && i.site == c.site) { present = true; break; }
        if (!present) iso.push_back(std::move(c));
      }
    }
    return iso;
  }

  // The composed-parent derivation is equivalent to (and richer than) the
  // isolated walk ONLY for the OUTERMOST, non-chained relocate:
  //   * ctx1: not inside an isolated SourcesForRelocateSource walk (a nested
  //     chained relocate resolved in a stack's own namespace).
  //   * outermost: no composed relocated-content derivation already active
  //     (its SourcesForSite(0,..) would re-enter this one).
  //   * non-chained: no ancestor of the source is itself a relocate
  //     destination of this stack -- deriving via the composed parent would
  //     re-expand that ancestor relocate (double count).
  //   * the source's composed parent is a real prim (not a root-level move).
  // Is an ANCESTOR of the source itself a relocate destination of this stack?
  // (a chained relocate: the source sits under another relocate's arrival).
  bool IsChainedArrival(const Src &ps,
                        const StackRelocates::Arrival &arrival) const {
    const StackRelocates &rel = layer_stacks[ps.stack_idx].relocates;
    for (Path a = Path(arrival.src_site).parent();
         !(a.is_root() || a.empty() || a.str() == "/"); a = a.parent()) {
      const Path ap = a.parent();
      const std::string aparent =
          (ap.is_root() || ap.empty()) ? "/" : ap.str();
      if (rel.ArrivalOf(aparent, a.name())) return true;
    }
    return false;
  }

  // Applicability of the composed-parent derivation INDEPENDENT of chaining:
  // outermost (not nested / not already deriving), the source maps cleanly into
  // ROOT namespace (a SUBROOT-ref source outside every arc pair does not resolve
  // via SourcesForSite(0,..)), and no salted-earth `over` is authored at the
  // source in the relocating stack (whose descendant-suppression only the
  // isolated walk models).
  bool ComposedBaseApplicable(const Src &ps,
                              const StackRelocates::Arrival &arrival) {
    if (isolated_reloc_depth_ != 0) return false;
    if (!reloc_content_in_progress.empty()) return false;
    if (ps.stack_idx != 0) {
      NamespaceMapping arc_only = ArcOnlyMapping(ps);
      if (arc_only.MatchSource(arrival.src_site) < 0) return false;
    }
    if (!Specs(ps.stack_idx, arrival.src_site).empty()) return false;
    return true;
  }

  // The composed-parent derivation REPLACES the isolated walk only for the
  // non-chained case (a chained source deriving via the composed parent would
  // re-expand the ancestor relocate / lose post-relocation opinions). Chained
  // cases instead SUPPLEMENT the isolated walk (see SourcesForRelocatedContent).
  bool ComposedRelocApplicable(const Src &ps,
                               const StackRelocates::Arrival &arrival) {
    return ComposedBaseApplicable(ps, arrival) && !IsChainedArrival(ps, arrival);
  }

  // ps.map_idx with this stack's relocate-rename pairs dropped (arc pairs only).
  NamespaceMapping ArcOnlyMapping(const Src &ps) const {
    NamespaceMapping arc_only = Mapping(ps.map_idx);
    const StackRelocates &rel = layer_stacks[ps.stack_idx].relocates;
    std::vector<NamespaceMapping::Pair> kept;
    for (const auto &pr : arc_only.pairs) {
      if (rel.src_to_dst.count(pr.first)) continue;  // relocate rename
      kept.push_back(pr);
    }
    arc_only.pairs = std::move(kept);
    return arc_only;
  }

  // Reverse the relocating stack's relocates repeatedly: if `path` lies under a
  // relocate DESTINATION, map it back to that relocate's SOURCE. Iterating this
  // un-does a CHAIN of relocates, returning the fully-pre-relocation path (in the
  // stack's own namespace). Bounded to avoid pathological loops.
  std::string FullyUnrelocate(uint32_t stack, std::string path) const {
    const StackRelocates &rel = layer_stacks[stack].relocates;
    if (rel.empty()) return path;
    for (int iter = 0; iter < 64; ++iter) {
      bool changed = false;
      for (const auto &r : rel.src_to_dst) {
        if (r.second.empty()) continue;
        if (NamespaceMapping::AtOrUnder(path, r.second)) {
          path = r.first + path.substr(r.second.size());
          changed = true;
          break;
        }
      }
      if (!changed) break;
    }
    return path;
  }

  // ISOLATED branch (historical behavior): resolve the source in the relocating
  // stack's OWN namespace, then compose `outer` -- ps's mapping to root plus the
  // src_site -> child_composed redirect rename -- on top of each content
  // source's own map. Offsets compose under ps; a source arriving as a Root
  // opinion becomes a Relocate; the relocation source's own raw-site opinions
  // are salted (suppress_site_specs).
  std::vector<Src> IsolatedRelocatedContent(
      const Src &ps, const StackRelocates::Arrival &arrival,
      const std::string &child_composed, std::string *warn, std::string *err) {
    std::vector<Src> out;
    NamespaceMapping outer = Mapping(ps.map_idx);
    outer.AddRename(arrival.src_site, child_composed);
    const std::vector<Src> &content =
        SourcesForRelocateSource(ps.stack_idx, arrival.src_site, warn, err);
    out.reserve(content.size());
    for (const Src &s : content) {
      Src c = s;
      if (c.stack_idx == ps.stack_idx && c.arc_kind == ArcType::Root &&
          c.site == arrival.src_site) {
        c.suppress_site_specs = true;
      }
      c.map_idx =
          InternMapping(NamespaceMapping::Compose(outer, Mapping(s.map_idx)));
      c.offset = ps.offset.Compose(s.offset);
      if (!c.expression_variables) c.expression_variables = ps.expression_variables;
      if (c.arc_kind == ArcType::Root) c.arc_kind = ArcType::Relocate;
      out.push_back(std::move(c));
    }
    return out;
  }

  // COMPOSED-PARENT branch (Step 1): derive the relocated content from the
  // source's parent resolved in ROOT (stack 0) namespace, so the caller's
  // variant-selection / implied-class context reaches the relocated prim. The
  // source path is mapped through ps's arc into root namespace; its parent is
  // composed there; the source child is derived (with its own departure
  // suppressed) and redirected to child_composed. Returns final composed-form
  // Srcs, directly comparable to IsolatedRelocatedContent.
  std::vector<Src> ComposedRelocatedContent(
      const Src &ps, const StackRelocates::Arrival &arrival,
      const std::string &child_composed, std::string *warn, std::string *err) {
    std::vector<Src> out;
    // Map the source path from the relocating stack's namespace into root using
    // ps's mapping WITHOUT this stack's relocate renames -- ps.map_idx bakes in
    // every relocate of the stack (WithStackRelocates), so a plain Apply would
    // send src_site to its own relocate DESTINATION (circular). Drop the pairs
    // that rename a relocate source of this stack, keeping only the arc pairs,
    // so src_site maps to its PRE-relocate composed location.
    const NamespaceMapping arc_only = ArcOnlyMapping(ps);
    // Walk back through the relocating stack's CHAINED relocates so the source
    // lands at its fully-un-relocated composed class-inheriting path (a chained
    // relocate leaves arrival.src_site under an ANCESTOR relocate's destination;
    // one arc_only.Apply only un-does the arc, not those ancestor relocates).
    // Composing THERE via SourcesForSite(0,..) pulls the inherited class + its
    // root implied-class over (TrickyMultipleRelocationsAndClasses2 JointBlend).
    const std::string src_unreloc =
        FullyUnrelocate(ps.stack_idx, arrival.src_site);
    const std::string src_root = arc_only.Apply(src_unreloc);
    const Path src_root_path(src_root);
    const Path src_root_parent = src_root_path.parent();
    const bool parent_is_root = src_root_parent.is_root() ||
                                src_root_parent.empty() ||
                                src_root_parent.str() == "/";
    if (parent_is_root)  // relocate to a new root prim: keep the isolated walk.
      return IsolatedRelocatedContent(ps, arrival, child_composed, warn, err);

    const std::vector<Src> &parent_srcs =
        SourcesForSite(0, src_root_parent, warn, err);
    // Suppress the relocating stack's own departure of this child so the source
    // stays resolvable. raw match is (stack, site) in the parent-source's ns;
    // for a root-authored relocate the relocating stack's site == src_root_parent.
    std::vector<Src> derived = DeriveChildSources(
        parent_srcs, src_root_path.name(), src_root, warn, err, ps.stack_idx,
        src_root_parent.str());
    // Salt BEFORE expansion: opinions authored in the RELOCATING stack AT the
    // relocation source are ignored by pxr ("opinions at relocation source"),
    // and so are the ARCS they author (ExpandList skips a salted spec's arcs).
    // Marking after ExpandList would be too late -- the source's own
    // (prohibited) reference/inherit would already have been expanded
    // (ErrorOpinionAtRelocationSource). The Src stays so arc-delivered content
    // and chained derivation still descend through it.
    for (Src &s : derived) {
      if (s.stack_idx == ps.stack_idx && s.arc_kind == ArcType::Root &&
          s.site == src_root) {
        s.suppress_site_specs = true;
      }
    }
    // Expand the source prim's own (non-salted) arcs -- the isolated walk does
    // this via ExpandList at each ancestor level. Arc-delivered content (a
    // reference on an ANCESTOR that carries the source, e.g. `/Group references
    // @g.usd@`) is what should follow the move (TrickyInheritsAndRelocates).
    derived = ExpandList(derived, warn, err);

    // Redirect the source subtree (root ns) onto the arrival destination.
    NamespaceMapping redirect;
    redirect.AddRename(src_root, child_composed);
    out.reserve(derived.size());
    for (const Src &s : derived) {
      Src c = s;
      c.map_idx =
          InternMapping(NamespaceMapping::Compose(redirect, Mapping(s.map_idx)));
      if (!c.expression_variables) c.expression_variables = ps.expression_variables;
      if (c.arc_kind == ArcType::Root) c.arc_kind = ArcType::Relocate;
      out.push_back(std::move(c));
    }
    return out;
  }

  // Graft a prim a relocate moved INTO this parent: the opinions authored AT the
  // destination path (`dst`, pushed to `base`) and the content from its pre-
  // relocation source site (pushed to `content_out`). `ps` is the parent source
  // whose stack authored the relocate. When `content_out != base` the caller
  // DEFERS the relocated content below every arrival-local destination-site
  // opinion for the arrival name -- pxr composes arrival-local opinions
  // (including a sibling variant graft) STRONGER than the relocated content
  // (TrickyVariantOverrideOfRelocatedPrim). `content_out == nullptr` keeps the
  // historical inline order.
  void AddRelocatedSources(const Src &ps,
                           const StackRelocates::Arrival &arrival,
                           const std::string &child_composed,
                           std::vector<Src> *base, std::string *warn,
                           std::string *err,
                           std::vector<Src> *content_out = nullptr) {
    if (!content_out) content_out = base;
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
    dst.implied_anchor = ps.implied_anchor;
    dst.suppress_site_specs = false;  // the TARGET address is a legal site
    dst.expression_variables = ps.expression_variables;
    dst.arc_chain = ps.arc_chain;
    dst.arc_sites = ps.arc_sites;
    dst.ancestral = true;  // child derivation, like DeriveChildSources
    base->push_back(std::move(dst));

    // Relocated content, already in final composed form (see
    // SourcesForRelocatedContent). Deferred below arrival-local dst opinions
    // when content_out != base.
    std::vector<Src> content =
        SourcesForRelocatedContent(ps, arrival, child_composed, warn, err);
    for (Src &c : content) content_out->push_back(std::move(c));
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
    // A relocate DESTINATION owns the arrival name. pxr composes, for that name:
    // (a) destination-site opinions authored AT-OR-STRONGER-THAN the relocating
    // stack (the relocating stack's own `over`/variant graft, and stronger
    // stacks' overs), then (b) the relocated content, WEAKEST. A coincidental
    // same-name child in a stack WEAKER than the relocating stack is a COLLISION
    // pxr shadows ("should not show up"). `arrival_stacks` accumulates the
    // relocating stacks as we walk psrcs strong->weak, so a NORMAL child from a
    // weaker DIFFERENT stack is shadowed while stronger pre-arrival opinions and
    // same-stack grafts are kept. `deferred_content` holds (b), appended last.
    // (TrickyVariantOverrideOfRelocatedPrim / TrickyMultipleRelocations /
    // TrickySpookyVariantSelection, verified vs pcp.txt prim stacks.)
    std::vector<Src> deferred_content;
    std::set<uint32_t> arrival_stacks;
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
          // dst-site opinions inline (strong); relocated content deferred.
          AddRelocatedSources(ps, *arrival, child_composed, &base, warn, err,
                              &deferred_content);
          arrival_stacks.insert(ps.stack_idx);
          continue;
        }
      }
      // Same-name collision with a relocate destination owned by another
      // (stronger) stack: shadowed. Opinions stronger than the arrival (seen
      // before it here) and same-stack grafts pass through.
      if (!arrival_stacks.empty() && !arrival_stacks.count(ps.stack_idx))
        continue;
      Src c;
      c.stack_idx = ps.stack_idx;
      c.site = ps.site + "/" + cn;
      c.map_idx = ps.map_idx;  // share the parent's mapping (no string copy)
      c.expression_variables = ps.expression_variables;
      c.offset = ps.offset;
      c.arc_kind = ps.arc_kind;
      c.implied_anchor = ps.implied_anchor;
      // Prohibited relocation-source namespace extends to DESCENDANTS: their
      // raw-site specs (a_src-style overrides at pre-relocation paths) are
      // ignored, while the Srcs themselves stay traversable for chained
      // relocations (arrivals registered under another relocate's source).
      c.suppress_site_specs = ps.suppress_site_specs;
      c.arc_chain = ps.arc_chain;  // implied class arcs need the whole chain
      c.arc_sites = ps.arc_sites;  // ancestral cycle trail (see Src::arc_sites)
      c.ancestral = true;          // arc was introduced at the parent
      base.push_back(std::move(c));
    }
    // Relocated content composes weakest, after every arrival-local dst opinion.
    for (Src &c : deferred_content) base.push_back(std::move(c));
    return base;
  }

  // Composition sources of a relocate's SOURCE prim, expanded in `stack`'s own
  // namespace. The composed namespace no longer holds this prim (its parent
  // there lists it as departed), but its ANCESTORS do (relocate paths are keyed
  // in the composed namespace -- BuildStackRelocates), either as normal prims or
  // as arrivals of ancestral relocates. So the ancestor chain is composed with
  // arrival handling ON (bringing each arrival's `over` opinions AND its
  // relocated content) and only the LEAF's own departure is suppressed. Handles
  // both an arc-delivered source (`/CharRig/Rig/SubRig` references subrig.usd,
  // relocate moves `.../Anim/AnimScope` up -- content via the reference) and a
  // source authored at a post-relocation path (`over Model_2 { Rig }` on an
  // ancestral arrival -- TrickyMultipleRelocations2).
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
    ++isolated_reloc_depth_;

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
      // Relocate paths are keyed in the COMPOSED namespace (see BuildStack-
      // Relocates), so the source's ANCESTORS exist there -- either normal prims
      // or ARRIVALS of ancestral relocates carrying an `over` plus the relocated
      // content. Compose them normally (arrival handling ON). Only the LEAF (the
      // relocate source itself) departed to the relocate target: suppress its
      // departure so it stays resolvable, and its own spec opinions (salted
      // earth) below.
      const bool is_leaf = (p.str() == site);
      std::vector<Src> base;
      if (parent.is_root() || parent.empty() || parent.str() == "/") {
        // A non-leaf root ancestor may itself be a relocate ARRIVAL (chained
        // relocate whose source sits under a root-level arrival): compose it.
        const StackRelocates &rel = layer_stacks[stack].relocates;
        const StackRelocates::Arrival *arrival =
            is_leaf ? nullptr : rel.ArrivalOf("/", p.name());
        Src s;
        s.stack_idx = stack;
        s.site = p.str();
        s.expression_variables = std::make_shared<const Value>(
            layer_stacks[stack].expression_variables);
        if (arrival) {
          AddRelocatedSources(s, *arrival, p.str(), &base, warn, err);
        } else {
          base.push_back(std::move(s));
        }
      } else {
        const std::vector<Src> &psrc = sources_cache[raw_key(parent.str())];
        // raw_stack/raw_site suppresses BOTH departure and arrival for the
        // matching source; use it ONLY for the leaf (whose departure we cancel)
        // -- intermediate ancestors keep arrival handling so their `over` and
        // relocated content compose (TrickyMultipleRelocations2 over Model_2).
        if (is_leaf) {
          base = DeriveChildSources(psrc, p.name(), p.str(), warn, err, stack,
                                    parent.str());
        } else {
          base = DeriveChildSources(psrc, p.name(), p.str(), warn, err);
        }
      }
      // The relocation SOURCE itself: this stack's own opinions there are
      // prohibited (pxr "opinions at relocation source") — including the
      // spec's ARCS and variant selections, so mark before expansion.
      if (is_leaf) {
        for (Src &b : base) {
          if (b.stack_idx == stack && b.arc_kind == ArcType::Root &&
              b.site == site) {
            b.suppress_site_specs = true;
          }
        }
      }
      std::vector<Src> &slot = sources_cache[raw_key(p.str())];
      slot = ExpandList(base, warn, err);
      result = &slot;
    }
    --isolated_reloc_depth_;
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
