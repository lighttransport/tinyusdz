// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache diagnostics and layer-stack setup.
// Included inside Cache::Impl.

  void AddIssue(ErrorCode code, const std::string &site,
                const std::string &message, std::string *err) {
    issues_.push_back(CompositionIssue{code, site, message});
    if (err) *err += message + "\n";
  }
  // --- layer stacks -------------------------------------------------------

  static constexpr uint32_t kInvalidStack =
      (std::numeric_limits<uint32_t>::max)();

  uint32_t InternLayerStack(std::shared_ptr<Layer> layer,
                            const std::string &identifier, std::string *warn,
                            std::string *err) {
    auto it = stack_by_id.find(identifier);
    if (it != stack_by_id.end()) return it->second;

    LayerStack st;
    st.identifier = identifier;
    std::set<std::string> visiting;
    visiting.insert(identifier);
    // Sublayer asset-path expressions evaluate against the STACK ROOT layer's
    // expressionVariables (pxr consults only the root/session layer of the
    // stack for expression variables; sublayers' own expressionVariables do
    // not feed sibling sublayer paths).
    if (!AppendLayerAndSublayers(st, layer, identifier, &visiting, 0,
                                 LayerOffset{}, layer->meta().expressionVariables,
                                 warn, err)) {
      return kInvalidStack;
    }
    Value vars = Value::MakeDictionary();
    for (size_t i = st.layers.size(); i-- > 0;) {
      vars = ComposeExpressionVariables(
          vars, st.layers[i]->meta().expressionVariables);
    }
    st.expression_variables = std::move(vars);
    BuildStackRelocates(&st);

    uint32_t idx = static_cast<uint32_t>(layer_stacks.size());
    layer_stacks.push_back(std::move(st));
    stack_by_id.emplace(identifier, idx);
    return idx;
  }

  // Resolve the `relocates` authored by every layer of `st` (layer metadata and
  // per-prim `relocates`) into this stack's namespace-edit tables. Relocates are
  // a LAYER-STACK property: a referenced layer's relocates rename prims inside
  // the referenced namespace, and the rename maps through the arc into root
  // space (see DeriveChildSources).
  static void BuildStackRelocates(LayerStack *st) {
    std::vector<std::pair<std::string, std::string>> candidates;
    std::set<std::pair<std::string, std::string>> seen_edit;
    const auto add_candidate = [&](const std::pair<std::string, std::string> &r) {
      // A layer can be reached along two sublayer paths (so it appears in
      // `layers` twice), and the same edit may be authored in a layer and one of
      // its sublayers. Deduping IDENTICAL (src, dst) pairs keeps the pairwise
      // conflict scan below from invalidating an edit against its own copy
      // (a.second == b.second would else fire on the duplicate).
      if (seen_edit.insert(r).second) candidates.push_back(r);
    };
    for (const auto &lp : st->layers) {
      for (const auto &r : lp->meta().relocates) add_candidate(r);
      for (const PrimSpec &ps : lp->prims()) {
        for (const auto &r : ps.meta().relocates()) add_candidate(r);
      }
    }
    if (candidates.empty()) return;

    auto strict_descendant = [](const std::string &path,
                                const std::string &ancestor) {
      return path.size() > ancestor.size() &&
             path.compare(0, ancestor.size(), ancestor) == 0 &&
             path[ancestor.size()] == '/';
    };
    std::vector<bool> invalid(candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i) {
      const std::string &src = candidates[i].first;
      const std::string &dst = candidates[i].second;
      const Path sp(src);
      // Invalid authored relocates are composition errors but contribute no
      // namespace edit: identity, ancestor/descendant moves, and root-prim
      // moves are all ignored by Pcp. Applying them would delete valid subtrees
      // (the supplemental ErrorInvalidAuthoredRelocates cases).
      if (src.empty() || dst.empty() || src == dst ||
          IsPathAtOrUnder(src, dst) || IsPathAtOrUnder(dst, src) ||
          sp.parent().str() == "/") {
        invalid[i] = true;
      }
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
      for (size_t j = i + 1; j < candidates.size(); ++j) {
        const auto &a = candidates[i];
        const auto &b = candidates[j];
        // Two sources cannot claim the same target; and a relocate chain whose
        // target is another relocate's source invalidates both opinions.
        if (a.second == b.second || a.second == b.first ||
            b.second == a.first) {
          invalid[i] = true;
          invalid[j] = true;
        }
        // A source authored beneath another source is expressed in the
        // prohibited pre-relocation namespace. Likewise a target beneath a
        // different source would itself be ancestrally relocated.
        if (strict_descendant(a.first, b.first) ||
            strict_descendant(a.second, b.first)) invalid[i] = true;
        if (strict_descendant(b.first, a.first) ||
            strict_descendant(b.second, a.first)) invalid[j] = true;
      }
    }
    // A relocate may be authored in a namespace an ANCESTRAL relocate already
    // produced (`/A -> /W`, then `/W/B -> /W/X`). Composed prims address their
    // opinions by raw (pre-relocation) site, so normalize such paths back:
    // `/W/B` -> `/A/B`.
    auto raw_path = [&](std::string path) {
      for (size_t pass = 0; pass < candidates.size(); ++pass) {
        size_t best = candidates.size();
        size_t best_len = 0;
        for (size_t k = 0; k < candidates.size(); ++k) {
          if (invalid[k]) continue;
          const std::string &target = candidates[k].second;
          if ((path == target || strict_descendant(path, target)) &&
              target.size() > best_len) {
            best = k;
            best_len = target.size();
          }
        }
        if (best == candidates.size()) break;
        path = candidates[best].first + path.substr(best_len);
      }
      return path;
    };

    StackRelocates &rel = st->relocates;
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (invalid[i]) continue;
      const std::string src = raw_path(candidates[i].first);
      const std::string &dst = candidates[i].second;
      const Path sp(src), dp(dst);
      rel.src_to_dst[src] = dst;
      rel.departed[sp.parent().str()].insert(sp.name());
      StackRelocates::Arrival arrival;
      arrival.name = dp.name();
      arrival.src_site = src;
      arrival.dst_site = dst;
      rel.arrivals[raw_path(dp.parent().str())].push_back(std::move(arrival));
    }
  }

  bool AppendLayerAndSublayers(LayerStack &st, std::shared_ptr<Layer> layer,
                               const std::string &identifier,
                               std::set<std::string> *visiting,
                               uint32_t depth, const LayerOffset &cum_offset,
                               const Value &expr_vars,
                               std::string *warn, std::string *err) {
    if (!layer) return false;
    if (depth > options.max_depth) {
      AddIssue(ErrorCode::MaxDepthExceeded, identifier,
               "Sublayer max depth exceeded at: " + identifier, err);
      return false;
    }
    st.layers.push_back(layer);
    st.layer_identifiers.push_back(identifier);
    st.layer_offsets.push_back(cum_offset);
    // Anchor = the referencing layer's FILE path; AssetResolver::Resolve takes
    // its directory internally. (Passing a pre-stripped dir double-strips and
    // breaks deep relative paths like `../../Materials/x.usd`.)
    const std::string &anchor = identifier;
    const auto &sub_offsets = layer->meta().subLayerOffsets;
    for (size_t sub_i = 0; sub_i < layer->meta().subLayers.size(); ++sub_i) {
      std::string sub = layer->meta().subLayers[sub_i];
      if (!sub.empty() && sub.front() == '`' &&
          options.expression_variable_policy !=
              ExpressionVariablePolicy::Disabled) {
        const ExpressionEvaluation expression =
            EvaluateAssetPathExpression(sub, expr_vars);
        if (expression.is_expression && !expression.success) {
          AddIssue(ErrorCode::ExpressionVariableError, sub, expression.error,
                   options.expression_variable_policy ==
                           ExpressionVariablePolicy::RequireResolved
                       ? err
                       : warn);
          continue;  // the sublayer contributes nothing
        }
        if (expression.is_none) continue;  // no opinion
        if (expression.success) sub = expression.value;
      }
      // Cumulative offset for this sublayer: parent's offset composed with the
      // authored per-sublayer (offset = ..; scale = ..).
      LayerOffset sub_cum = cum_offset;
      if (sub_i < sub_offsets.size()) {
        double offset = sub_offsets[sub_i].first;
        double scale = sub_offsets[sub_i].second;
        if (!std::isfinite(offset) || !std::isfinite(scale) ||
            !(scale > 0.0)) {
          AddIssue(ErrorCode::InvalidReferenceOffset, sub,
                   "Invalid sublayer offset; using identity mapping", err);
          offset = 0.0;
          scale = 1.0;
        }
        sub_cum = cum_offset.Compose(LayerOffset{offset, scale});
      }
      const std::string sub_id = resolver->ResolvePath(
          sub, anchor, !options.strict_aousd_conformance);
      if (sub_id.empty()) {
        AddIssue(ErrorCode::InvalidAssetPath, sub,
                 "Failed to resolve sublayer: " + sub, err);
        if (options.error_when_asset_not_found) return false;
        continue;
      }
      if (visiting && visiting->count(sub_id)) {
        // pxr warns and breaks the cycle, composing the rest — failing the
        // whole load here dropped otherwise-valid scenes.
        AddIssue(ErrorCode::SublayerCycle, sub_id,
                 "Sublayer cycle detected (skipped): " + sub_id, warn);
        continue;
      }
      LayerLoadOptions load_opts;
      load_opts.max_memory = options.max_layer_memory;
      load_opts.usda_parse_options = options.usda_parse_options;
      load_opts.strict_aousd_conformance = options.strict_aousd_conformance;
      std::shared_ptr<Layer> sl =
          reg_->GetOrLoad(*resolver, sub, anchor, warn, err, load_opts);
      if (sl) {
        if (visiting) visiting->insert(sub_id);
        if (!AppendLayerAndSublayers(st, sl, sub_id, visiting, depth + 1,
                                     sub_cum, expr_vars, warn, err)) {
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
