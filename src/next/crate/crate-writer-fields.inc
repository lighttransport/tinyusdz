// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl fieldset/spec construction.
// Included inside CrateWriter::Impl private section.

  // One prim spec + its property specs. Operates on `this` Impl's tables, so a
  // worker Impl can build a disjoint prim range in parallel. `variant_names` is
  // a read-only prescan shared across workers; `prim_to_fs_idx` is per-builder.
  struct VariantSetChildren {
    std::string_view variant_set;
    uint32_t first_variant = 0;
    uint32_t variant_count = 0;
  };
  struct VariantOwnerRange {
    uint32_t first_set = 0;
    uint32_t set_count = 0;
  };
  struct VariantSetLookupKey {
    std::string_view owning;
    std::string_view variant_set;
  };
  struct VariantSetLookupHash {
    size_t operator()(const VariantSetLookupKey& key) const noexcept {
      const size_t h1 = std::hash<std::string_view>{}(key.owning);
      const size_t h2 = std::hash<std::string_view>{}(key.variant_set);
      return h1 * 1315423911u + h2 + 0x9e3779b97f4a7c15ULL;
    }
  };
  struct VariantSetLookupEq {
    bool operator()(const VariantSetLookupKey& a,
                    const VariantSetLookupKey& b) const noexcept {
      return a.owning == b.owning && a.variant_set == b.variant_set;
    }
  };
  void BuildOnePrim(
      const Layer& layer, size_t i,
      const std::vector<VariantSetChildren>& variant_set_names,
      const std::unordered_map<std::string_view, VariantOwnerRange,
                               StringViewHash, StringViewEq>& variant_owner_ranges,
      const std::unordered_map<VariantSetLookupKey, uint32_t,
                               VariantSetLookupHash, VariantSetLookupEq>&
          variant_set_lookup,
      const std::vector<std::string_view>& variant_children,
      std::vector<uint32_t>& prim_to_fs_idx) {
      const PrimSpec& prim = layer.prims()[i];
      const std::string& prim_path = prim.path().str();
      const std::string& prim_nm = prim.name();
      const PrimSpecMeta& prim_meta = prim.meta();
      const bool is_variant_holder = prim_nm.size() >= 2 &&
          prim_nm.front() == '{' && prim_nm.back() == '}';
      const bool is_variant_set = is_variant_holder &&
          prim_nm.size() >= 3 && prim_nm[prim_nm.size() - 2] == '=';
      // Variant-namespace prims ("/Prim/{vset=sel}/..."): emit them ONLY for an
      // unflattened layer (round-trip), detected by their owning prim still
      // carrying variant sets. After flattening, the compositor bakes + clears
      // variantSets, so these become leftover and are dropped.
      {
        size_t lb = prim_path.find("/{");
        if (lb != std::string::npos) {
          const PrimSpec* owner = layer.prim_at_path(prim_path.substr(0, lb));
          if (!owner || owner->meta().variantSets().empty()) return;
        }
      }
      SmallU32List fieldset;

      // A variant-namespace HOLDER ("/Prim/{vset=sel}" or "/Prim/{vset=}") is a
      // Variant/VariantSet spec, which (per pxrUSD) carries neither specifier
      // nor typeName — emitting them makes pxr warn. Variant CHILD prims
      // ("/Prim/{vset=sel}/Geo") have ordinary names and keep both.
      // specifier (pxrUSD convention: specifier first, then typeName)
      if (!is_variant_holder) {
        CrateField f;
        f.token_index.value = CommonToken(common_.specifier, "specifier");
        uint64_t spec_val = static_cast<uint64_t>(prim.specifier());
        f.value_rep = ValueRep::Make(CrateTypeId::Specifier, spec_val, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // typeName
      if (!is_variant_holder && !prim.type_name().empty()) {
        CrateField f;
        f.token_index.value = CommonToken(common_.typeName, "typeName");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(prim.type_name()),
                                     false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // variantChildren on a VariantSet spec ("/Prim/{vset=}"): the names of the
      // variants in that set, so pxrUSD can enumerate them.
      if (is_variant_set) {  // "{vset=}"
        const std::string_view pth(prim_path);
        size_t lb = pth.find("/{");
        const std::string_view owning =
            (lb == std::string_view::npos) ? std::string_view() : pth.substr(0, lb);
        const std::string_view vset = prim_nm.substr(1, prim_nm.size() - 3);  // "{vset=}"->vset
        if (!owning.empty()) {
          const auto it = variant_set_lookup.find({owning, vset});
          if (it != variant_set_lookup.end()) {
        const auto& variant_entry = variant_set_names[it->second];
            if (variant_entry.variant_count > 0) {
              const size_t begin = static_cast<size_t>(variant_entry.first_variant);
              const size_t count = static_cast<size_t>(variant_entry.variant_count);
              fieldset.push_back(static_cast<uint32_t>(fields_.size()));
              fields_.push_back(MakeTokenVectorField(
                  "variantChildren", count,
                  [&](size_t j) -> std::string_view {
                    return variant_children[begin + j];
                  }));
            }
          }
        }
      }

      // active
      if (!prim_meta.active) {
        CrateField f;
        f.token_index.value = CommonToken(common_.active, "active");
        f.value_rep = ValueRep::Make(CrateTypeId::Bool, 0, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // hidden
      if (prim_meta.hidden) {
        CrateField f;
        f.token_index.value = CommonToken(common_.hidden, "hidden");
        f.value_rep = ValueRep::Make(CrateTypeId::Bool, 1, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // doc
      if (!prim_meta.doc().empty()) {
        CrateField f;
        f.token_index.value = CommonToken(common_.doc, "doc");
        uint32_t str_idx = InternString(prim_meta.doc());
        f.value_rep = ValueRep::Make(CrateTypeId::String, str_idx, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // apiSchemas — a flattened (explicit) TokenListOp. The on-disk layout the
      // reader (DecodeTokenListOp) and pxrUSD expect is
      //   [u8 ListOpHeader=0x02 explicit][u64 count][u32 token_idx]*count
      // (the earlier code omitted the header byte, so the count's low byte was
      // misread as the header -> apiSchemas did not round-trip).
      if (!prim_meta.apiSchemas().empty()) {
        size_t count = prim_meta.apiSchemas().size();
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
          uint32_t tok_idx = InternToken(prim_meta.apiSchemas()[i].sv());
          std::memcpy(api_data.data() + 1 + 8 + i * 4, &tok_idx, 4);
        }
        DataBlock block{TypeId::Token, std::move(api_data)};
        CacheBlockHash(&block);
        uint64_t data_idx = InternBlock(std::move(block));

        CrateField f;
        f.token_index.value = CommonToken(common_.apiSchemas, "apiSchemas");
        f.value_rep = ValueRep::Make(CrateTypeId::TokenListOp, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // Properties, relationships, connections and time samples are emitted as
      // separate pxrUSD-style Attribute/Relationship specs (below). Collect the
      // child property names here for the prim's "properties" field.
      SmallU32List prop_name_tokens = EmitPrimProperties(prim);

      // Metadata fields from reference/payload/etc strings.
      // Keep the composed list in canonical crate arc list-op form for pxrUSD
      // compatibility and emit next-private `<arc>_listOp` companions for
      // authored (prepend/append/delete/reorder) metadata, which `next` still
      // uses to preserve list-op edit provenance.
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

      auto add_pathlistop_field = [&](const char* name,
                                      const std::vector<std::string>& values) {
        if (values.empty()) return;
        size_t n = values.size();
        size_t items_bytes = 0;
        size_t data_bytes = 0;
        if (!safe::mul(n, size_t(4), &items_bytes) ||
            !safe::add(size_t(9), items_bytes, &data_bytes)) {
          result_.error = "PathListOp payload size overflow";
          return;
        }

        std::vector<uint8_t> raw(data_bytes);
        raw[0] = 0x03;  // Explicit + has explicit items.
        uint64_t cnt = n;
        std::memcpy(raw.data() + 1, &cnt, 8);

        size_t cursor = 9;
        for (const auto& s : values) {
          std::string path;
          if (!s.empty()) {
            CompositionArc arc = Compositor::ParseReference(s);
            path = arc.prim_path.empty() ? s : arc.prim_path;
          }
          uint32_t path_idx = path.empty() ? 0u : InternPath(path);
          std::memcpy(raw.data() + cursor, &path_idx, 4);
          cursor += 4;
        }

        DataBlock block{TypeId::Invalid, std::move(raw)};
        CacheBlockHash(&block);
        uint64_t data_idx = InternBlock(std::move(block));
        CrateField f;
        f.token_index.value = InternToken(name);
        f.value_rep = ValueRep::Make(CrateTypeId::PathListOp, data_idx, false,
                                    false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };

      auto add_reference_listop_field = [&](const char* name,
                                           const std::vector<std::string>& values,
                                           bool is_payload) {
        if (values.empty()) return;

        const bool payload_has_offset =
            !is_payload || (options_.version_minor >= 8);

        const size_t bytes_per_item = payload_has_offset ? 32u : 8u;

        size_t n = values.size();
        size_t items_bytes = 0;
        size_t data_bytes = 0;
        if (!safe::mul(n, bytes_per_item, &items_bytes) ||
            !safe::add(size_t(9), items_bytes, &data_bytes)) {
          result_.error = is_payload
                               ? "PayloadListOp payload size overflow"
                               : "ReferenceListOp payload size overflow";
          return;
        }

        std::vector<uint8_t> raw(data_bytes);
        raw[0] = 0x03;  // Explicit + has explicit items.
        uint64_t cnt = n;
        std::memcpy(raw.data() + 1, &cnt, 8);

        size_t cursor = 9;
        for (const auto& s : values) {
          CompositionArc arc =
              is_payload ? Compositor::ParsePayload(s) : Compositor::ParseReference(s);

          if (arc.asset_path.empty() && arc.prim_path.empty()) {
            result_.error = "Invalid composition arc string in prim metadata";
            return;
          }

          uint32_t asset_idx = InternString(arc.asset_path);
          uint32_t path_idx = arc.prim_path.empty() ? 0u : InternPath(arc.prim_path);
          std::memcpy(raw.data() + cursor, &asset_idx, 4);
          std::memcpy(raw.data() + cursor + 4, &path_idx, 4);
          cursor += 8;

          if (payload_has_offset) {
            uint64_t dict_count = 0;
            std::memcpy(raw.data() + cursor, &dict_count, 8);
            cursor += 8;
            double offset = 0.0;
            double scale = 1.0;
            if (!arc.layer_offset.empty()) {
              Compositor::ParseLayerOffset(arc.layer_offset, offset, scale);
            }
            std::memcpy(raw.data() + cursor, &offset, 8);
            std::memcpy(raw.data() + cursor + 8, &scale, 8);
            cursor += 16;
          }
        }

        DataBlock block{TypeId::Invalid, std::move(raw)};
        CacheBlockHash(&block);
        uint64_t data_idx = InternBlock(std::move(block));
        CrateField f;
        f.token_index.value = InternToken(name);
        f.value_rep = ValueRep::Make(is_payload ? CrateTypeId::PayloadListOp
                                                  : CrateTypeId::ReferenceListOp,
                                     data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };

      add_reference_listop_field("references", prim_meta.references, false);
      add_reference_listop_field("payload", prim_meta.payloads, true);
      add_pathlistop_field("inherits", prim_meta.inherits);
      add_pathlistop_field("specializes", prim_meta.specializes);

      // Phase 7 S5: when an arc field carries a non-bare list-op qualifier,
      // emit a companion `<arc>_listOp` token[] preserving the authored
      // prepend/append/delete/order sublists (marker-delimited). Unknown fields are
      // ignored by other readers.
      const ArcListOpEdits* edits = prim_meta.arc_edits();
      auto add_arc_listop = [&](const char* base, const ArcEdit* e) {
        if (!e || !e->authored || e->is_explicit) return;
        size_t item_count = 0;
        auto add_count = [&](const std::vector<std::string>& items) {
          if (items.empty()) return;
          item_count += 1 + items.size();
        };
        add_count(e->prepended);
        add_count(e->appended);
        add_count(e->deleted);
        add_count(e->ordered);

        if (item_count == 0) item_count = 1;
        std::vector<uint8_t> raw(8 + item_count * 4);
        uint64_t cnt = static_cast<uint64_t>(item_count);
        std::memcpy(raw.data(), &cnt, 8);
        size_t cursor = 8;
        auto write_section = [&](const char* marker,
                                const std::vector<std::string>& items) {
          if (items.empty()) return;
          uint32_t marker_tok = InternToken(marker);
          std::memcpy(raw.data() + cursor, &marker_tok, 4);
          cursor += 4;
          for (const auto& v : items) {
            uint32_t tok = InternToken(v);
            std::memcpy(raw.data() + cursor, &tok, 4);
            cursor += 4;
          }
        };
        if (item_count == 1) {
          uint32_t marker_tok = InternToken("\x01N");
          std::memcpy(raw.data() + 8, &marker_tok, 4);
          cursor = 12;
          cnt = 1;
          std::memcpy(raw.data(), &cnt, 8);
        } else {
          write_section("\x01P", e->prepended);
          write_section("\x01A", e->appended);
          write_section("\x01D", e->deleted);
          write_section("\x01O", e->ordered);
        }

        DataBlock block{TypeId::Token, std::move(raw)};
        CacheBlockHash(&block);
        uint64_t data_idx = InternBlock(std::move(block));
        CrateField f;
        const char* opname = nullptr;
        if (std::strcmp(base, "references") == 0) {
          opname = "references_listOp";
        } else if (std::strcmp(base, "payload") == 0) {
          opname = "payload_listOp";
        } else if (std::strcmp(base, "inherits") == 0) {
          opname = "inherits_listOp";
        } else {
          opname = "specializes_listOp";
        }
        f.token_index.value = InternToken(opname);
        f.value_rep = ValueRep::Make(CrateTypeId::Token, data_idx, true, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };
      add_arc_listop("references", edits ? &edits->references : nullptr);
      add_arc_listop("payload", edits ? &edits->payloads : nullptr);
      add_arc_listop("inherits", edits ? &edits->inherits : nullptr);
      add_arc_listop("specializes", edits ? &edits->specializes : nullptr);
      if (!prim_meta.comment().empty()) {
        add_string_field("comment", prim_meta.comment());
      }

      // kind (token), displayName (string), instanceable (bool).
      if (!prim_meta.kind().empty()) {
        CrateField f;
        f.token_index.value = CommonToken(common_.kind, "kind");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(prim_meta.kind()), false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }
      if (!prim_meta.displayName().empty()) {
        add_string_field("displayName", prim_meta.displayName());
      }
      if (prim_meta.instanceable) {
        CrateField f;
        f.token_index.value = CommonToken(common_.instanceable, "instanceable");
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
      add_dict_field("customData", prim_meta.customData());
      add_dict_field("assetInfo", prim_meta.assetInfo());
      add_dict_field("sdrMetadata", prim_meta.sdrMetadata());
      add_dict_field("clips", prim_meta.clips());
      // variantSelection: a VariantSelectionMap of each set's selected variant.
      // The reader rebuilds the VariantSetData from this plus the bracketed
      // holder prims, so no separate variantSets token field is emitted. (The
      // bracketed holder/child prims carry the variant CONTENT and are emitted
      // above for unflattened layers.)
      if (!prim_meta.variantSets().empty()) {
        const auto& vsets = prim_meta.variantSets();
        size_t sel_count = 0;
        for (const auto& vs : vsets) {
          if (!vs.selected.empty()) ++sel_count;
        }
        if (sel_count > 0) {
          size_t items_bytes = 0;
          size_t blob_size = 0;
          if (!safe::mul(sel_count, size_t(8), &items_bytes) ||
              !safe::add(size_t(8), items_bytes, &blob_size)) {
            result_.error = "VariantSelectionMap payload size overflow";
            return;
          }
          std::vector<uint8_t> blob(blob_size);
          const uint64_t cnt = static_cast<uint64_t>(sel_count);
          std::memcpy(blob.data(), &cnt, 8);
          size_t cursor = 8;
          for (const auto& vs : vsets) {
            if (vs.selected.empty()) continue;
            const uint32_t key_idx = InternString(vs.name);
            const uint32_t val_idx = InternString(vs.selected);
            std::memcpy(blob.data() + cursor, &key_idx, 4);
            std::memcpy(blob.data() + cursor + 4, &val_idx, 4);
            cursor += 8;
          }
          DataBlock block;
          block.type = TypeId::Invalid;
          block.data = std::move(blob);
          CacheBlockHash(&block);
          uint64_t blk = InternBlock(std::move(block));
          CrateField vf;
          vf.token_index.value =
              CommonToken(common_.variantSelection, "variantSelection");
          vf.value_rep =
              ValueRep::Make(CrateTypeId::VariantSelectionMap, blk, false, false);
          fieldset.push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(vf);
        }
        // variantSetChildren: the set names on this prim (so pxr enumerates the
        // sets). Derived from the variant holder prims present in the layer.
        const std::string_view pth(prim_path);
        auto owner_it = variant_owner_ranges.find(pth);
        if (owner_it != variant_owner_ranges.end() &&
            owner_it->second.set_count > 0) {
          const size_t begin =
              static_cast<size_t>(owner_it->second.first_set);
          const size_t count =
              static_cast<size_t>(owner_it->second.set_count);
          fieldset.push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(MakeTokenVectorField(
              "variantSetChildren", count,
              [&](size_t j) -> std::string_view {
                return variant_set_names[begin + j].variant_set;
              }));
        }
      }

      // "properties" field: TokenVector listing this prim's child property and
      // relationship names (pxrUSD enumerates properties from it). The next
      // reader rebuilds properties from the separate specs, so it ignores this.
      if (!prop_name_tokens.empty()) {
        size_t n = prop_name_tokens.size();
        std::vector<uint8_t> raw(8 + n * 4);
        uint64_t cnt = n;
        std::memcpy(raw.data(), &cnt, 8);
        for (size_t i = 0; i < n; ++i) {
          const uint32_t tok = prop_name_tokens[i];
          std::memcpy(raw.data() + 8 + i * 4, &tok, 4);
        }
        DataBlock block{TypeId::Token, std::move(raw)};
        CacheBlockHash(&block);
        uint64_t data_idx = InternBlock(std::move(block));
        CrateField pf;
        pf.token_index.value = CommonToken(common_.properties, "properties");
        pf.value_rep =
            ValueRep::Make(CrateTypeId::TokenVector, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(pf);
      }

      // Record fieldset
      uint32_t fs_idx = AddFieldset(std::move(fieldset));
      if (i < prim_to_fs_idx.size()) prim_to_fs_idx[i] = fs_idx;

      // Create spec. A prim whose LAST path element is a variant selector
      // ("{vset=sel}") is a Variant spec; "{vset=}" is the VariantSet
      // declaration. (Variant CHILD prims like "/Prim/{vset=sel}/Geo" keep a
      // normal name -> Prim.) Using the right spec type keeps the unflattened
      // output readable by pxrUSD (which rejects a Prim named "{...}").
      CrateSpec spec;
      spec.path_index.value = InternPath(prim_path);
      spec.fieldset_index.value = fs_idx;
      spec.spec_type = SpecType::Prim;
      {
        if (prim_nm.size() >= 2 && prim_nm.front() == '{' && prim_nm.back() == '}') {
          spec.spec_type = is_variant_set
                               ? SpecType::VariantSet   // "{vset=}"
                               : SpecType::Variant;     // "{vset=sel}"
        }
      }
      specs_.push_back(spec);
  }

  void BuildFieldsAndSpecs(const Layer& layer) {
    auto fields_phase_t0 = Clock::now();
    // 1. PseudoRoot spec (index 0)
    {
      SmallU32List fieldset;

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

      uint32_t fs_idx = AddFieldset(std::move(fieldset));

      CrateSpec spec;
      spec.path_index.value = InternPath("/");
      spec.fieldset_index.value = fs_idx;
      spec.spec_type = SpecType::PseudoRoot;
      specs_.push_back(spec);
    }
    timing_.build_fields_layer_ms = MsSince(fields_phase_t0);

    // Pre-scan variant holder prims ("/owning/{vset=var}") to recover, per
    // owning prim, each variant set's variant names — needed to emit
    // variantSetChildren (on the owning prim) and variantChildren (on each
    // VariantSet spec) so pxrUSD can enumerate the variants of an unflattened
    // layer (tinyusdz reconstructs them from the paths, but pxr needs these).
    std::vector<std::string_view> variant_children;
    std::vector<VariantSetChildren> variant_set_names;
    std::unordered_map<std::string_view, VariantOwnerRange,
                       StringViewHash, StringViewEq>
        variant_owner_ranges;
    variant_owner_ranges.reserve(layer.prims().size() / 8 + 16);
    std::unordered_map<VariantSetLookupKey, uint32_t,
                       VariantSetLookupHash, VariantSetLookupEq>
        variant_set_lookup;
    variant_set_lookup.reserve(layer.prims().size() / 8 + 16);
    variant_set_names.reserve(layer.prims().size() / 8 + 16);

    for (const auto& p : layer.prims()) {
      const std::string_view nm = p.name();
      if (nm.size() < 3 || nm.front() != '{' || nm.back() != '}') continue;
      const size_t eq = nm.find('=');
      if (eq == std::string::npos || eq <= 1 || eq + 1 >= nm.size()) continue;
      const std::string_view var = nm.substr(eq + 1, nm.size() - eq - 2);  // between = and }
      if (var.empty()) continue;  // "{vset=}" is the VariantSet decl, not a variant
      const std::string_view path = p.path().str();
      const size_t lb = path.find("/{");
      if (lb == std::string::npos) continue;
      const std::string_view owning_path = path.substr(0, lb);
      const std::string_view vset = nm.substr(1, eq - 1);
      auto owner_it = variant_owner_ranges.find(owning_path);
      if (owner_it == variant_owner_ranges.end()) {
        owner_it = variant_owner_ranges.emplace(
            owning_path,
            VariantOwnerRange{
                static_cast<uint32_t>(variant_set_names.size()), 0})
                      .first;
      }

      VariantSetLookupKey set_key{owning_path, vset};
      auto set_result =
          variant_set_lookup.emplace(set_key, static_cast<uint32_t>(variant_set_names.size()));
      if (set_result.second) {
        VariantSetChildren set_entry;
        set_entry.variant_set = vset;
        set_entry.first_variant = static_cast<uint32_t>(variant_children.size());
        set_entry.variant_count = 0;
        variant_set_names.push_back(set_entry);
        ++owner_it->second.set_count;
      }

      const uint32_t set_idx = set_result.first->second;
      variant_set_names[set_idx].variant_count++;
      variant_children.push_back(var);
    }

    std::vector<uint32_t> prim_to_fs_idx(layer.prims().size(), UINT32_MAX);

    auto build_prims_serial = [&]() {
      for (size_t i = 0; i < layer.prims().size(); ++i) {
        BuildOnePrim(layer, i, variant_set_names, variant_owner_ranges,
                    variant_set_lookup, variant_children, prim_to_fs_idx);
        if (!result_.error.empty()) return;
      }
    };

    bool used_parallel = false;
#if defined(TINYUSDZ_ENABLE_THREAD)
    auto merge_worker = [&](Impl& worker,
                           const std::vector<uint32_t>& worker_fs_idx,
                           size_t worker_lo) {
      std::vector<uint64_t> local_to_global(worker.value_data_.size(), UINT64_MAX);
      if (!local_to_global.empty()) local_to_global[0] = 0;

      const uint32_t field_base = static_cast<uint32_t>(fields_.size());
      fields_.reserve(fields_.size() + worker.fields_.size());
      for (const auto& wf : worker.fields_) {
        CrateField f = wf;
        f.value_rep = RemapLocalValueRep(worker, f.value_rep, local_to_global);
        fields_.push_back(f);
      }

      const uint32_t fieldset_base = static_cast<uint32_t>(FieldsetCount());
      for (uint32_t fs = 0; fs < worker.FieldsetCount(); ++fs) {
        SmallU32List items;
        worker.ForEachFieldsetItem(fs, [&](uint32_t local_field_idx) {
          items.push_back(field_base + local_field_idx);
        });
        AddFieldset(items);
      }

      specs_.reserve(specs_.size() + worker.specs_.size());
      for (const auto& ws : worker.specs_) {
        CrateSpec spec = ws;
        if (!worker.frozen_path_ && ws.path_index.value < worker.paths_.size()) {
          spec.path_index.value = InternPath(worker.paths_[ws.path_index.value]);
        }
        spec.fieldset_index.value = fieldset_base + ws.fieldset_index.value;
        specs_.push_back(spec);
      }

      for (size_t j = 0; j < worker_fs_idx.size(); ++j) {
        const uint32_t local_fs_idx = worker_fs_idx[j];
        if (local_fs_idx == UINT32_MAX) continue;
        const size_t prim_index = worker_lo + j;
        if (prim_index < prim_to_fs_idx.size()) {
          prim_to_fs_idx[prim_index] = fieldset_base + local_fs_idx;
        }
      }
      ancestor_scan_path_indices_.insert(
          ancestor_scan_path_indices_.end(),
          worker.ancestor_scan_path_indices_.begin(),
          worker.ancestor_scan_path_indices_.end());

      arrays_passed_through_ += worker.arrays_passed_through_;
      arrays_reencoded_ += worker.arrays_reencoded_;
      blocks_deduped_ += worker.blocks_deduped_;
    };
#endif

    fields_phase_t0 = Clock::now();
#if defined(TINYUSDZ_ENABLE_THREAD)
    const int nthreads = ResolveThreads();
    constexpr bool kEnableParallelPrimBuild = false;
    if (kEnableParallelPrimBuild && nthreads > 1 && layer.prims().size() >= 4096) {
      PropNameTable& name_table = GetPropNameTable();
      std::string prop_path;
      auto build_prop_path = [&](std::string_view base, std::string_view name) {
        const size_t needed = base.size() + 1 + name.size();
        if (prop_path.capacity() < needed) prop_path.reserve(needed);
        prop_path.assign(base);
        prop_path.push_back('.');
        prop_path.append(name);
      };
      for (const auto& prim : layer.prims()) {
        const std::string& base = prim.path().str();
        InternPath(base);
        for (const auto& slot : prim.properties().slots()) {
          const std::string& prop_name = name_table.get(slot.name_id);
          build_prop_path(base, prop_name);
          InternPath(prop_path);
          if (slot.flags & PropSlot::kFlagConnection) {
            if (const std::vector<Path>* conns =
                    prim.connection(slot.name_id)) {
              for (const auto& p : *conns) {
                if (!p.str().empty()) InternPath(p.str());
              }
            }
          }
        }
        for (auto prop_id : prim.time_sampled_properties()) {
          const std::string& prop_name = name_table.get(prop_id);
          build_prop_path(base, prop_name);
          InternPath(prop_path);
        }
        struct RelationshipEntry {
          std::string_view name;
          const std::vector<Path>* targets;
        };
        SmallList<RelationshipEntry, 8> rels;
        prim.ForEachRelationship([&](const std::string& rel_name,
                                    const std::vector<Path>& tgts) {
          rels.push_back({std::string_view(rel_name), &tgts});
          for (size_t i = rels.size(); i > 1; --i) {
            RelationshipEntry& prev = rels[i - 2];
            RelationshipEntry& cur = rels[i - 1];
            if (prev.name <= cur.name) break;
            RelationshipEntry tmp = prev;
            prev = cur;
            cur = tmp;
          }
        });
        if (!rels.empty()) {
          for (size_t ri = 0; ri < rels.size(); ++ri) {
            const auto& rel = rels[ri];
            build_prop_path(base, rel.name);
            InternPath(prop_path);
            for (const auto& p : *rel.targets) {
              if (!p.str().empty()) InternPath(p.str());
            }
          }
        }
      }

      int P = std::min(nthreads, 16);
      P = std::min(P, static_cast<int>(layer.prims().size()));
      std::vector<std::unique_ptr<Impl>> workers;
      std::vector<std::vector<uint32_t>> worker_fs_idx(P);
      workers.reserve(static_cast<size_t>(P));
      for (int t = 0; t < P; ++t) {
        workers.emplace_back(new Impl(options_));
        workers.back()->ResetWorkerState();
        workers.back()->common_ = common_;
        workers.back()->frozen_tok_ = &token_to_index_;
        workers.back()->frozen_str_ = &string_to_index_;
        workers.back()->frozen_path_ = &path_to_index_;

        const size_t lo = layer.prims().size() * static_cast<size_t>(t) /
                          static_cast<size_t>(P);
        const size_t hi = layer.prims().size() * static_cast<size_t>(t + 1) /
                          static_cast<size_t>(P);
        worker_fs_idx[t].assign(static_cast<size_t>(hi - lo), UINT32_MAX);
      }

      std::vector<std::thread> pool;
      pool.reserve(static_cast<size_t>(P));
      for (int t = 0; t < P; ++t) {
        const size_t lo = layer.prims().size() * static_cast<size_t>(t) /
                          static_cast<size_t>(P);
        const size_t hi = layer.prims().size() * static_cast<size_t>(t + 1) /
                          static_cast<size_t>(P);
        pool.emplace_back([&, t, lo, hi] {
          for (size_t i = lo; i < hi; ++i) {
            workers[t]->BuildOnePrim(layer, i, variant_set_names,
                                    variant_owner_ranges, variant_set_lookup,
                                    variant_children, worker_fs_idx[t]);
            if (!workers[t]->result_.error.empty()) break;
          }
        });
      }
      for (auto& th : pool) th.join();

      bool ok = true;
      for (int t = 0; t < P; ++t) {
        if (workers[t]->frozen_miss_ || !workers[t]->result_.error.empty()) {
          ok = false;
          break;
        }
      }
      if (ok) {
        for (int t = 0; t < P; ++t) {
          const size_t lo = layer.prims().size() * static_cast<size_t>(t) /
                              static_cast<size_t>(P);
          merge_worker(*workers[t], worker_fs_idx[t], lo);
        }
        used_parallel = true;
      }
    }
#endif

    // 2. Prim specs
    if (!used_parallel) {
      build_prims_serial();
      if (!result_.error.empty()) return;
    }
    timing_.build_fields_prims_ms = MsSince(fields_phase_t0);

    // Build primChildren from emitted prim paths. Some streamed/flattened
    // builders author a full path while leaving Layer::root_indices() or
    // PrimSpec::child_indices() stale; trusting those arrays emits nested prim
    // names as pseudo-root children, and OpenUSD creates null Sdf child handles
    // that crash raw layer export. The crate hierarchy must follow path parents.
    auto is_variant_holder = [](std::string_view nm) {
      return nm.size() >= 2 && nm.front() == '{' && nm.back() == '}';
    };
    auto parent_and_name = [](std::string_view path,
                              std::string_view* parent,
                              std::string_view* name) -> bool {
      if (path.empty() || path == "/" || path[0] != '/') return false;
      const size_t slash = path.rfind('/');
      if (slash == std::string_view::npos) return false;
      *parent = (slash == 0) ? std::string_view("/")
                             : path.substr(0, slash);
      *name = path.substr(slash + 1);
      return !name->empty();
    };

    std::vector<uint32_t> path_parent(layer.prims().size(), UINT32_MAX);
    std::vector<uint32_t> root_path_children;
    {
      FlatStringViewU32Map prim_path_to_index;
      prim_path_to_index.reserve(layer.prims().size() + 16);
      for (uint32_t prim_index = 0;
           prim_index < static_cast<uint32_t>(layer.prims().size());
           ++prim_index) {
        if (prim_to_fs_idx[prim_index] == UINT32_MAX) continue;
        const PrimSpec& prim = layer.prims()[prim_index];
        if (is_variant_holder(prim.name())) continue;
        const std::string& path = prim.path().str();
        if (!path.empty()) {
          prim_path_to_index.set(std::string_view(path), prim_index);
        }
      }

      root_path_children.reserve(layer.root_indices().size());
      for (uint32_t prim_index = 0;
           prim_index < static_cast<uint32_t>(layer.prims().size());
           ++prim_index) {
        if (prim_to_fs_idx[prim_index] == UINT32_MAX) continue;
        const PrimSpec& prim = layer.prims()[prim_index];
        if (is_variant_holder(prim.name())) continue;
        std::string_view parent;
        std::string_view name;
        if (!parent_and_name(std::string_view(prim.path().str()), &parent,
                             &name)) {
          continue;
        }
        if (parent == "/") {
          root_path_children.push_back(prim_index);
          continue;
        }
        if (const uint32_t* pidx = prim_path_to_index.find(parent)) {
          path_parent[prim_index] = *pidx;
        }
      }
    }

    // Add primChildren to pseudo-root from actual root-level prim paths.
    {
      fields_phase_t0 = Clock::now();
      SmallU32List root_child_tokens;
      for (uint32_t root_idx : root_path_children) {
        const PrimSpec* prim = layer.prim(root_idx);
        if (prim) {
          root_child_tokens.push_back(InternToken(prim->name()));
        }
      }
      if (!root_child_tokens.empty()) {
        size_t n = root_child_tokens.size();
        std::vector<uint8_t> raw(8 + n * 4);
        uint64_t cnt = n;
        std::memcpy(raw.data(), &cnt, 8);
        for (size_t i = 0; i < n; ++i) {
          std::memcpy(raw.data() + 8 + i * 4, &root_child_tokens[i], 4);
        }
        DataBlock block{TypeId::Token, std::move(raw)};
        CacheBlockHash(&block);
        uint64_t data_idx = InternBlock(std::move(block));

        CrateField pc_field;
        pc_field.token_index.value =
            CommonToken(common_.primChildren, "primChildren");
        pc_field.value_rep = ValueRep::Make(CrateTypeId::TokenVector,
                                            data_idx, false, false);
        if (HasFieldsets()) {
          AppendFieldsetItem(0, static_cast<uint32_t>(fields_.size()));
          fields_.push_back(pc_field);
        }
      }
      timing_.build_fields_root_children_ms = MsSince(fields_phase_t0);
    }

    std::vector<uint32_t> prim_child_offsets(layer.prims().size() + 1, 0);
    size_t total_child_refs = 0;
    fields_phase_t0 = Clock::now();
    for (uint32_t child_index = 0;
         child_index < static_cast<uint32_t>(path_parent.size());
         ++child_index) {
      const uint32_t parent_index = path_parent[child_index];
      if (parent_index == UINT32_MAX || parent_index >= layer.prims().size()) {
        continue;
      }
      ++prim_child_offsets[parent_index + 1];
      ++total_child_refs;
    }
    timing_.build_fields_child_scan_ms = MsSince(fields_phase_t0);

    for (size_t i = 0; i < prim_child_offsets.size() - 1; ++i) {
      prim_child_offsets[i + 1] += prim_child_offsets[i];
    }
    std::vector<uint32_t> child_tokens(total_child_refs);
    std::vector<uint32_t> child_write_offsets = prim_child_offsets;
    fields_phase_t0 = Clock::now();
    for (uint32_t child_index = 0;
         child_index < static_cast<uint32_t>(path_parent.size());
         ++child_index) {
      const uint32_t parent_index = path_parent[child_index];
      if (parent_index == UINT32_MAX || parent_index >= layer.prims().size()) {
        continue;
      }
      const PrimSpec& child = layer.prims()[child_index];
      child_tokens[child_write_offsets[parent_index]++] =
          InternToken(child.name());
    }

    for (uint32_t prim_index = 0; prim_index < layer.prims().size(); ++prim_index) {
      const PrimSpec& prim = layer.prims()[prim_index];
      const uint32_t fs_idx = prim_to_fs_idx[prim_index];
      if (fs_idx == UINT32_MAX || is_variant_holder(prim.name())) {
        continue;
      }

      if (prim.path().str().empty() || prim.path().str() == "/") {
        continue;
      }

      size_t child_begin = prim_child_offsets[prim_index];
      size_t child_end = prim_child_offsets[prim_index + 1];
      if (child_begin == child_end) {
        continue;
      }

      const size_t child_count = child_end - child_begin;
      if (child_count == 0) {
        continue;
      }
      std::vector<uint8_t> raw(8 + child_count * 4);
      uint64_t cnt = child_count;
      std::memcpy(raw.data(), &cnt, 8);
      for (size_t k = 0; k < child_count; ++k) {
        const uint32_t tok = child_tokens[child_begin + k];
        std::memcpy(raw.data() + 8 + k * 4, &tok, 4);
      }
      DataBlock block{TypeId::Token, std::move(raw)};
      CacheBlockHash(&block);
      uint64_t data_idx = InternBlock(std::move(block));

      CrateField f;
      f.token_index.value = CommonToken(common_.primChildren, "primChildren");
      f.value_rep = ValueRep::Make(CrateTypeId::TokenVector,
                                   data_idx, false, false);
      if (fs_idx < FieldsetCount()) {
        AppendFieldsetItem(fs_idx, static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }
    }
    timing_.build_fields_child_emit_ms = MsSince(fields_phase_t0);
  }
