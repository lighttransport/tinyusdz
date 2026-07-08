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
    if (!AppendLayerAndSublayers(st, layer, identifier, &visiting, 0,
                                 LayerOffset{}, warn, err)) {
      return kInvalidStack;
    }

    uint32_t idx = static_cast<uint32_t>(layer_stacks.size());
    layer_stacks.push_back(std::move(st));
    stack_by_id.emplace(identifier, idx);
    return idx;
  }

  bool AppendLayerAndSublayers(LayerStack &st, std::shared_ptr<Layer> layer,
                               const std::string &identifier,
                               std::set<std::string> *visiting,
                               uint32_t depth, const LayerOffset &cum_offset,
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
      const std::string &sub = layer->meta().subLayers[sub_i];
      // Cumulative offset for this sublayer: parent's offset composed with the
      // authored per-sublayer (offset = ..; scale = ..).
      LayerOffset sub_cum = cum_offset;
      if (sub_i < sub_offsets.size()) {
        sub_cum = cum_offset.Compose(
            LayerOffset{sub_offsets[sub_i].first, sub_offsets[sub_i].second});
      }
      const std::string sub_id = resolver->ResolvePath(sub, anchor);
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
      std::shared_ptr<Layer> sl =
          reg_->GetOrLoad(*resolver, sub, anchor, warn, err,
                          LayerLoadOptions{options.max_layer_memory, 0});
      if (sl) {
        if (visiting) visiting->insert(sub_id);
        if (!AppendLayerAndSublayers(st, sl, sub_id, visiting, depth + 1,
                                     sub_cum, warn, err)) {
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
