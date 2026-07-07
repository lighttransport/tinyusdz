// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl fieldset/spec construction.
// Included inside CrateWriter::Impl private section.

  // One prim spec + its property specs. Operates on `this` Impl's tables, so a
  // worker Impl can build a disjoint prim range in parallel. `variant_names` is
  // a read-only prescan shared across workers; `path_to_fs_idx` is per-builder.
  void BuildOnePrim(
      const Layer& layer, size_t i,
      const std::unordered_map<std::string,
          std::map<std::string, std::vector<std::string>>>& variant_names,
      std::unordered_map<std::string, uint32_t>& path_to_fs_idx) {
      const PrimSpec& prim = layer.prims()[i];
      // Variant-namespace prims ("/Prim/{vset=sel}/..."): emit them ONLY for an
      // unflattened layer (round-trip), detected by their owning prim still
      // carrying variant sets. After flattening, the compositor bakes + clears
      // variantSets, so these become leftover and are dropped.
      {
        const std::string& pp = prim.path().str();
        // Nearest bracketed component owns this prim (nested variants):
        // "/P/{a=x}/{b=y}/Geo" is owned by "/P/{a=x}", not "/P".
        size_t lb = pp.rfind("/{");
        if (lb != std::string::npos) {
          const PrimSpec* owner = layer.prim_at_path(pp.substr(0, lb));
          if (!owner || owner->meta().variantSets().empty()) return;
        }
      }
      std::vector<uint32_t> fieldset;

      // A variant-namespace HOLDER ("/Prim/{vset=sel}" or "/Prim/{vset=}") is a
      // Variant/VariantSet spec, which (per pxrUSD) carries neither specifier
      // nor typeName — emitting them makes pxr warn. Variant CHILD prims
      // ("/Prim/{vset=sel}/Geo") have ordinary names and keep both.
      const std::string& prim_nm = prim.name();
      const bool is_variant_holder = prim_nm.size() >= 2 &&
          prim_nm.front() == '{' && prim_nm.back() == '}';

      // specifier (pxrUSD convention: specifier first, then typeName)
      if (!is_variant_holder) {
        CrateField f;
        f.token_index.value = InternToken("specifier");
        uint64_t spec_val = static_cast<uint64_t>(prim.specifier());
        f.value_rep = ValueRep::Make(CrateTypeId::Specifier, spec_val, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // typeName
      if (!is_variant_holder && !prim.type_name().empty()) {
        CrateField f;
        f.token_index.value = InternToken("typeName");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(prim.type_name()),
                                     false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // variantChildren on a VariantSet spec ("/Prim/{vset=}"): the names of the
      // variants in that set, so pxrUSD can enumerate them.
      if (is_variant_holder && prim_nm.size() >= 3 &&
          prim_nm[prim_nm.size() - 2] == '=') {  // "{vset=}"
        const std::string& pth = prim.path().str();
        // Owner = path before the LAST bracketed component (nested variants).
        size_t lb = pth.rfind("/{");
        std::string owning = (lb == std::string::npos) ? std::string()
                                                       : pth.substr(0, lb);
        std::string vset = prim_nm.substr(1, prim_nm.size() - 3);  // "{vset=}"->vset
        auto oit = variant_names.find(owning);
        if (oit != variant_names.end()) {
          auto vit = oit->second.find(vset);
          if (vit != oit->second.end() && !vit->second.empty()) {
            fieldset.push_back(static_cast<uint32_t>(fields_.size()));
            fields_.push_back(MakeTokenVectorField("variantChildren", vit->second));
          }
        }
      }

      // active
      if (!prim.meta().active) {
        CrateField f;
        f.token_index.value = InternToken("active");
        f.value_rep = ValueRep::Make(CrateTypeId::Bool, 0, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // hidden
      if (prim.meta().hidden) {
        CrateField f;
        f.token_index.value = InternToken("hidden");
        f.value_rep = ValueRep::Make(CrateTypeId::Bool, 1, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // doc
      if (!prim.meta().doc().empty()) {
        CrateField f;
        f.token_index.value = InternToken("doc");
        uint32_t str_idx = InternString(prim.meta().doc());
        f.value_rep = ValueRep::Make(CrateTypeId::String, str_idx, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // apiSchemas — a flattened (explicit) TokenListOp. The on-disk layout the
      // reader (DecodeTokenListOp) and pxrUSD expect is
      //   [u8 ListOpHeader=0x02 explicit][u64 count][u32 token_idx]*count
      // (the earlier code omitted the header byte, so the count's low byte was
      // misread as the header -> apiSchemas did not round-trip).
      if (!prim.meta().apiSchemas().empty()) {
        size_t count = prim.meta().apiSchemas().size();
        size_t items_bytes = 0;
        size_t api_data_size = 0;
        if (!safe::mul(count, size_t(4), &items_bytes) ||
            !safe::add(size_t(9), items_bytes, &api_data_size)) {
          result_.error = "apiSchemas payload size overflow";
          return;
        }
        std::vector<uint8_t> api_data(api_data_size);
        api_data[0] = 0x02;  // kHasExplicit
        uint64_t cnt = count;
        std::memcpy(api_data.data() + 1, &cnt, 8);
        for (size_t i = 0; i < count; ++i) {
          uint32_t tok_idx = InternToken(prim.meta().apiSchemas()[i]);
          std::memcpy(api_data.data() + 1 + 8 + i * 4, &tok_idx, 4);
        }
        uint64_t data_idx = InternBlock({TypeId::Token, std::move(api_data)});

        CrateField f;
        f.token_index.value = InternToken("apiSchemas");
        f.value_rep = ValueRep::Make(CrateTypeId::TokenListOp, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // Properties, relationships, connections and time samples are emitted as
      // separate pxrUSD-style Attribute/Relationship specs (below). Collect the
      // child property names here for the prim's "properties" field.
      std::vector<std::string> prop_names = EmitPrimProperties(prim);

      // Metadata fields from reference/payload/etc strings
      // (stored as regular attributes for now, matching reader behavior)
      auto add_string_field = [&](const std::string& name,
                                  const std::string& value) {
        if (value.empty()) return;
        CrateField f;
        f.token_index.value = InternToken(name);
        uint32_t tok_idx = InternToken(value);
        f.value_rep = ValueRep::Make(CrateTypeId::Token, tok_idx, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };

      // Write a list of strings as a token[] array field. Each element is a
      // separate token, so the reader reconstructs the full list — no lossy
      // separator joining and no first-element-only truncation.
      auto add_token_array_field = [&](const std::string& name,
                                       const std::vector<std::string>& values) {
        if (values.empty()) return;
        CrateField f;
        f.token_index.value = InternToken(name);
        f.value_rep = EncodeValue(Value::MakeTokenArray(values));
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };

      // Composition arcs + list metadata as token[] fields (full round-trip).
      // The token[] carries the within-spec effective (composed) list.
      add_token_array_field("references", prim.meta().references);
      add_token_array_field("payload", prim.meta().payloads);
      add_token_array_field("inherits", prim.meta().inherits);
      add_token_array_field("specializes", prim.meta().specializes);

      // Phase 7 S5: when an arc field carries a non-bare list-op qualifier,
      // also emit a companion `<arc>_listOp` token[] preserving the authored
      // prepend/append/delete/order sublists (marker-delimited). Absent =>
      // the token[] above is an explicit (bare) list. Next-private fields;
      // unknown fields are ignored by other readers.
      const ArcListOpEdits* edits = prim.meta().arc_edits();
      auto add_arc_listop = [&](const char* base, const ArcEdit* e) {
        if (!e || !e->authored || e->is_explicit) return;
        std::vector<std::string> enc;
        auto section = [&](const char* marker,
                           const std::vector<std::string>& items) {
          if (items.empty()) return;
          enc.push_back(marker);
          enc.insert(enc.end(), items.begin(), items.end());
        };
        section("\x01P", e->prepended);
        section("\x01A", e->appended);
        section("\x01D", e->deleted);
        section("\x01O", e->ordered);
        if (enc.empty()) enc.push_back("\x01N");  // non-explicit, no items
        add_token_array_field(std::string(base) + "_listOp", enc);
      };
      add_arc_listop("references", edits ? &edits->references : nullptr);
      add_arc_listop("payload", edits ? &edits->payloads : nullptr);
      add_arc_listop("inherits", edits ? &edits->inherits : nullptr);
      add_arc_listop("specializes", edits ? &edits->specializes : nullptr);
      if (!prim.meta().comment().empty()) {
        add_string_field("comment", prim.meta().comment());
      }

      // kind (token), displayName (string), instanceable (bool).
      if (!prim.meta().kind().empty()) {
        CrateField f;
        f.token_index.value = InternToken("kind");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(prim.meta().kind()), false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }
      if (!prim.meta().displayName().empty()) {
        add_string_field("displayName", prim.meta().displayName());
      }
      if (prim.meta().instanceable) {
        CrateField f;
        f.token_index.value = InternToken("instanceable");
        f.value_rep = ValueRep::Make(CrateTypeId::Bool, 1, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // Dictionary-valued metadata: pxr binary VtDictionary (round-trips through
      // both this reader and OpenUSD).
      auto add_dict_field = [&](const char* name, const Value& v) {
        if (!v.is_dictionary() || !v.as_dictionary() ||
            v.as_dictionary()->empty()) {
          return;
        }
        CrateField f;
        f.token_index.value = InternToken(name);
        f.value_rep = EncodeValue(v);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };
      add_dict_field("customData", prim.meta().customData());
      add_dict_field("assetInfo", prim.meta().assetInfo());
      add_dict_field("sdrMetadata", prim.meta().sdrMetadata());
      add_dict_field("clips", prim.meta().clips());
      // variantSelection: a VariantSelectionMap of each set's selected variant.
      // The reader rebuilds the VariantSetData from this plus the bracketed
      // holder prims, so no separate variantSets token field is emitted. (The
      // bracketed holder/child prims carry the variant CONTENT and are emitted
      // above for unflattened layers.)
      // NOTE: not gated on variantSets(): a prim may only SELECT a variant
      // defined by a referenced asset ("variants = {...}" next to a
      // reference), in which case it has selections but no local sets.
      {
        // Selections can live in three places depending on how the layer was
        // produced: VariantSetData::selected (crate reader / authoring API,
        // the authoritative per-set form), the plural variantSelections()
        // list (USDA parser), or the legacy single "set=sel" string. Merge
        // all three, per-set `selected` first (matches RecordSelections).
        std::vector<std::pair<std::string, std::string>> sels;
        auto add_sel = [&sels](const std::string& set, const std::string& v) {
          if (set.empty() || v.empty()) return;
          for (auto& kv : sels) {
            if (kv.first == set) return;  // first writer wins
          }
          sels.emplace_back(set, v);
        };
        for (const auto& vs : prim.meta().variantSets()) {
          add_sel(vs.name, vs.selected);
        }
        for (const auto& kv : prim.meta().variantSelections()) {
          add_sel(kv.first, kv.second);
        }
        if (!prim.meta().variantSelection.empty()) {
          size_t eq = prim.meta().variantSelection.find('=');
          if (eq != std::string::npos) {
            add_sel(prim.meta().variantSelection.substr(0, eq),
                    prim.meta().variantSelection.substr(eq + 1));
          }
        }
        if (!sels.empty()) {
          uint64_t blk = StoreVariantSelectionMap(sels);
          CrateField vf;
          vf.token_index.value = InternToken("variantSelection");
          vf.value_rep =
              ValueRep::Make(CrateTypeId::VariantSelectionMap, blk, false, false);
          fieldset.push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(vf);
        }
        // variantSetChildren: the set names on this prim (so pxr enumerates the
        // sets). Derived from the variant holder prims present in the layer.
        auto oit = variant_names.find(prim.path().str());
        if (oit != variant_names.end() && !oit->second.empty()) {
          std::vector<std::string> set_names;
          set_names.reserve(oit->second.size());
          for (const auto& kv : oit->second) set_names.push_back(kv.first);
          fieldset.push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(MakeTokenVectorField("variantSetChildren", set_names));
        }
      }

      // "properties" field: TokenVector listing this prim's child property and
      // relationship names (pxrUSD enumerates properties from it). The next
      // reader rebuilds properties from the separate specs, so it ignores this.
      if (!prop_names.empty()) {
        size_t n = prop_names.size();
        std::vector<uint8_t> raw(8 + n * 4);
        uint64_t cnt = n;
        std::memcpy(raw.data(), &cnt, 8);
        for (size_t i = 0; i < n; ++i) {
          uint32_t tok = InternToken(prop_names[i]);
          std::memcpy(raw.data() + 8 + i * 4, &tok, 4);
        }
        uint64_t data_idx = InternBlock({TypeId::Token, std::move(raw)});
        CrateField pf;
        pf.token_index.value = InternToken("properties");
        pf.value_rep =
            ValueRep::Make(CrateTypeId::TokenVector, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(pf);
      }

      // Record fieldset
      uint32_t fs_idx = static_cast<uint32_t>(fieldsets_.size());
      fieldsets_.push_back(std::move(fieldset));
      path_to_fs_idx[prim.path().str()] = fs_idx;

      // Create spec. A prim whose LAST path element is a variant selector
      // ("{vset=sel}") is a Variant spec; "{vset=}" is the VariantSet
      // declaration. (Variant CHILD prims like "/Prim/{vset=sel}/Geo" keep a
      // normal name -> Prim.) Using the right spec type keeps the unflattened
      // output readable by pxrUSD (which rejects a Prim named "{...}").
      CrateSpec spec;
      spec.path_index.value = InternPath(prim.path().str());
      spec.fieldset_index.value = fs_idx;
      spec.spec_type = SpecType::Prim;
      {
        const std::string& nm = prim.name();
        if (nm.size() >= 2 && nm.front() == '{' && nm.back() == '}') {
          spec.spec_type = (nm.find('=') == nm.size() - 2)
                               ? SpecType::VariantSet   // "{vset=}"
                               : SpecType::Variant;     // "{vset=sel}"
        }
      }
      specs_.push_back(spec);
  }

  void BuildFieldsAndSpecs(const Layer& layer) {
    // 1. PseudoRoot spec (index 0)
    {
      std::vector<uint32_t> fieldset;

      // defaultPrim
      if (!layer.meta().defaultPrim.empty()) {
        CrateField f;
        f.token_index.value = InternToken("defaultPrim");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(layer.meta().defaultPrim),
                                     false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // upAxis
      {
        CrateField f;
        f.token_index.value = InternToken("upAxis");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(layer.meta().upAxis),
                                     false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // metersPerUnit
      {
        CrateField f;
        f.token_index.value = InternToken("metersPerUnit");
        double v = layer.meta().metersPerUnit;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // timeCodesPerSecond
      {
        CrateField f;
        f.token_index.value = InternToken("timeCodesPerSecond");
        double v = layer.meta().timeCodesPerSecond;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // startTimeCode
      {
        CrateField f;
        f.token_index.value = InternToken("startTimeCode");
        double v = layer.meta().startTimeCode;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        // Store as Double (pxrUSD/legacy reader reject an uninlined TimeCode).
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // endTimeCode
      {
        CrateField f;
        f.token_index.value = InternToken("endTimeCode");
        double v = layer.meta().endTimeCode;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        // Store as Double (pxrUSD/legacy reader reject an uninlined TimeCode).
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // framesPerSecond / kilogramsPerUnit (only when authored).
      if (layer.meta().framesPerSecond_set) {
        CrateField f;
        f.token_index.value = InternToken("framesPerSecond");
        double v = layer.meta().framesPerSecond;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }
      if (layer.meta().kilogramsPerUnit_set) {
        CrateField f;
        f.token_index.value = InternToken("kilogramsPerUnit");
        double v = layer.meta().kilogramsPerUnit;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // Dictionary-valued stage metadata (pxr binary VtDictionary).
      auto add_layer_dict = [&](const char* name, const Value& v) {
        if (!v.is_dictionary() || !v.as_dictionary() ||
            v.as_dictionary()->empty()) {
          return;
        }
        CrateField f;
        f.token_index.value = InternToken(name);
        f.value_rep = EncodeValue(v);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };
      add_layer_dict("customLayerData", layer.meta().customLayerData);
      add_layer_dict("expressionVariables", layer.meta().expressionVariables);

      // doc
      if (!layer.meta().doc.empty()) {
        CrateField f;
        f.token_index.value = InternToken("doc");
        uint32_t str_idx = InternString(layer.meta().doc);
        f.value_rep = ValueRep::Make(CrateTypeId::String, str_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // comment
      if (!layer.meta().comment.empty()) {
        CrateField f;
        f.token_index.value = InternToken("comment");
        uint32_t str_idx = InternString(layer.meta().comment);
        f.value_rep = ValueRep::Make(CrateTypeId::String, str_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // subLayers (token[] — all entries round-trip; the reader reads them back)
      if (!layer.meta().subLayers.empty()) {
        CrateField f;
        f.token_index.value = InternToken("subLayers");
        f.value_rep = EncodeValue(Value::MakeTokenArray(layer.meta().subLayers));
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      uint32_t fs_idx = static_cast<uint32_t>(fieldsets_.size());
      fieldsets_.push_back(std::move(fieldset));

      CrateSpec spec;
      spec.path_index.value = InternPath("/");
      spec.fieldset_index.value = fs_idx;
      spec.spec_type = SpecType::PseudoRoot;
      specs_.push_back(spec);
    }

    // Pre-scan variant holder prims ("/owning/{vset=var}") to recover, per
    // owning prim, each variant set's variant names — needed to emit
    // variantSetChildren (on the owning prim) and variantChildren (on each
    // VariantSet spec) so pxrUSD can enumerate the variants of an unflattened
    // layer (tinyusdz reconstructs them from the paths, but pxr needs these).
    std::unordered_map<std::string,
        std::map<std::string, std::vector<std::string>>> variant_names;
    for (const auto& p : layer.prims()) {
      const std::string& nm = p.name();
      if (nm.size() < 3 || nm.front() != '{' || nm.back() != '}') continue;
      size_t eq = nm.find('=');
      if (eq == std::string::npos) continue;
      std::string var = nm.substr(eq + 1, nm.size() - eq - 2);  // between = and }
      if (var.empty()) continue;  // "{vset=}" is the VariantSet decl, not a variant
      const std::string& path = p.path().str();
      // The holder's own name is the LAST component; its owner is everything
      // before it. rfind so nested holders ("/P/{a=x}/{b=y}") attribute to
      // the inner owner ("/P/{a=x}"), not the outermost prim.
      size_t lb = path.rfind("/{");
      if (lb == std::string::npos) continue;
      variant_names[path.substr(0, lb)][nm.substr(1, eq - 1)].push_back(var);
    }

    // 2. Prim specs
    std::unordered_map<std::string, uint32_t> path_to_fs_idx;
    for (size_t i = 0; i < layer.prims().size(); ++i) {
      BuildOnePrim(layer, i, variant_names, path_to_fs_idx);
      if (!result_.error.empty()) return;
    }

    // Add primChildren to pseudo-root (fieldset 0) from root prims
    {
      std::vector<uint32_t> root_child_tokens;
      for (const auto& prim : layer.prims()) {
        // Root prims have exactly one '/' in their path (e.g., "/World")
        const std::string& path_str = prim.path().str();
        if (!path_str.empty() && path_str != "/" &&
            path_str.find('/', 1) == std::string::npos) {
          root_child_tokens.push_back(InternToken(prim.name()));
        }
      }
      if (!root_child_tokens.empty()) {
        // Store as TokenListOp matching pxrUSD format:
        // [uint8 header_bits][if HasExplicitItems: u64 count + token_idx * count]
        size_t n = root_child_tokens.size();
        std::vector<uint8_t> raw(8 + n * 4);
        uint64_t cnt = n;
        std::memcpy(raw.data(), &cnt, 8);
        for (size_t i = 0; i < n; ++i) {
          std::memcpy(raw.data() + 8 + i * 4, &root_child_tokens[i], 4);
        }
        uint64_t data_idx = InternBlock({TypeId::Token, std::move(raw)});

        CrateField pc_field;
        pc_field.token_index.value = InternToken("primChildren");
        // pxrUSD stores primChildren as TokenVector (41), not TokenListOp (32)
        // TokenVector is a simple array: [u64 count][u32 token_idx]*count (no header byte)
        pc_field.value_rep = ValueRep::Make(CrateTypeId::TokenVector,
                                            data_idx, false, false);
        if (!fieldsets_.empty()) {
          fieldsets_[0].push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(pc_field);
        }
      }
    }

    // Build parent-path -> ordered child name tokens in a single O(P) pass,
    // replacing the previous O(P^2) per-prim child scan. Children are appended
    // in prim-iteration order, matching the prior ordering (and prim names are
    // already interned via path interning, so token indices are unchanged).
    std::unordered_map<std::string, std::vector<uint32_t>> parent_children;
    for (const auto& prim : layer.prims()) {
      const std::string& p = prim.path().str();
      if (p.empty() || p == "/") continue;
      // A variant HOLDER ("{vset=sel}"/"{vset=}") is not a prim child of its
      // owner (it is listed in variantSetChildren), so don't add it here. But a
      // variant CHILD prim (a normal name under a holder, e.g. ".../Geo") IS a
      // child of that holder and must be listed in the holder's primChildren.
      const std::string& nm = prim.name();
      if (nm.size() >= 2 && nm.front() == '{' && nm.back() == '}') continue;
      size_t slash = p.rfind('/');
      if (slash == std::string::npos) continue;
      std::string parent = (slash == 0) ? "/" : p.substr(0, slash);
      parent_children[parent].push_back(InternToken(prim.name()));
    }

    // Add primChildren to non-root prims that have children. (Variant holders
    // get them too — so pxr enumerates a variant's sub-prims; the path_to_fs_idx
    // guard below skips prims that were not emitted, e.g. baked-away holders.)
    for (const auto& prim : layer.prims()) {
      const std::string& prim_path = prim.path().str();
      if (prim_path.empty() || prim_path == "/") continue;

      auto cit = parent_children.find(prim_path);
      if (cit == parent_children.end()) continue;
      const std::vector<uint32_t>& child_tokens = cit->second;

      if (!child_tokens.empty()) {
        std::vector<uint8_t> raw(8 + child_tokens.size() * 4);
        uint64_t cnt = child_tokens.size();
        std::memcpy(raw.data(), &cnt, 8);
        for (size_t k = 0; k < child_tokens.size(); ++k) {
          std::memcpy(raw.data() + 8 + k * 4, &child_tokens[k], 4);
        }
        uint64_t data_idx = InternBlock({TypeId::Token, std::move(raw)});

        CrateField f;
        f.token_index.value = InternToken("primChildren");
        f.value_rep = ValueRep::Make(CrateTypeId::TokenVector,
                                      data_idx, false, false);
        auto it = path_to_fs_idx.find(prim_path);
        if (it != path_to_fs_idx.end() && it->second < fieldsets_.size()) {
          fieldsets_[it->second].push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(f);
        }
      }
    }
  }
