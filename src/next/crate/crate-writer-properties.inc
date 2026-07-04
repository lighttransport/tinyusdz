// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl property/spec emission helpers.
// Included inside CrateWriter::Impl private section.

  // Build an explicit PathListOp value_data_ block for relationship/connection
  // targets: [u8 header=IsExplicit|HasExplicitItems][u64 count][u32 path_idx...].
  // Each target path is interned into the path table. Returns the block index.
  uint64_t StorePathListOp(const std::vector<Path>& targets) {
    size_t target_count = targets.size();
    if (target_count == 0) return 0;
    const uint8_t header = 0x03;  // IsExplicit (0x01) | HasExplicitItems (0x02)
    size_t items_bytes = 0;
    size_t blob_size = 0;
    if (!safe::mul(target_count, size_t(4), &items_bytes) ||
        !safe::add(size_t(9), items_bytes, &blob_size)) {
      result_.error = "PathListOp payload size overflow";
      return 0;
    }
    std::vector<uint8_t> blob(blob_size);
    blob[0] = header;
    size_t cursor = 9;
    size_t count = 0;
    uint64_t u64_count = 0;
    for (const auto& t : targets) {
      const std::string& path = t.str();
      if (path.empty()) continue;
      const uint32_t idx = InternPathStable(path);  // Path owned by the Layer
      ancestor_scan_path_indices_.push_back(idx);
      std::memcpy(blob.data() + cursor, &idx, 4);
      cursor += 4;
      ++count;
      ++u64_count;
    }
    if (count == 0) return 0;
    std::memcpy(blob.data() + 1, &u64_count, 8);
    if (count < target_count) {
      blob.resize(9 + count * 4);
    }
    DataBlock block;
    block.type = TypeId::Invalid;
    block.data = std::move(blob);
    CacheBlockHash(&block);
    return InternBlock(std::move(block));
  }

  // Build a VariantSelectionMap value_data_ block (CrateTypeId 45):
  // [u64 count][(u32 key_string_idx, u32 val_string_idx) * count]. Strings are
  // interned into the string table. Returns the block index.
  // Build a TokenVector field ([u64 count][u32 token_idx]*count) -- used for
  // primChildren / variantSetChildren / variantChildren.
  template <typename NameAccessor>
  CrateField MakeTokenVectorField(std::string_view field_name, size_t n,
                                 NameAccessor&& get_name) {
    std::vector<uint8_t> raw(8 + n * 4);
    uint64_t cnt = n;
    std::memcpy(raw.data(), &cnt, 8);
    for (size_t i = 0; i < n; ++i) {
      uint32_t tok = InternToken(get_name(i));
      std::memcpy(raw.data() + 8 + i * 4, &tok, 4);
    }
    DataBlock block{TypeId::Token, std::move(raw)};
    CacheBlockHash(&block);
    uint64_t data_idx = InternBlock(std::move(block));
    CrateField f;
    f.token_index.value = InternToken(field_name);
    f.value_rep = ValueRep::Make(CrateTypeId::TokenVector, data_idx, false, false);
    return f;
  }

  CrateField MakeTokenVectorField(std::string_view field_name,
                                 const std::vector<std::string>& names) {
    return MakeTokenVectorField(field_name, names.size(),
                               [&](size_t i) -> std::string_view {
                                 return names[i];
                               });
  }

  CrateField MakeTokenVectorField(std::string_view field_name,
                                 const std::vector<std::string_view>& names) {
    return MakeTokenVectorField(field_name, names.size(),
                               [&](size_t i) -> std::string_view {
                                 return names[i];
                               });
  }

  uint32_t AttrTypeToken(const PrimSpec& prim, const std::string& prop_name,
                         const Value* val, uint16_t slot_flags) {
    if (const std::string* tn = prim.property_type_name(prop_name)) {
      if (!tn->empty()) return InternToken(*tn);
    }
    TypeId tid = val ? val->type_id() : static_cast<TypeId>(TypeId::Invalid);
    const char* base = GetTypeName(tid);
    if (!base) base = "token";
    const bool is_array = (slot_flags & PropSlot::kFlagArray) ||
                          (val && val->is_array());
    const size_t type_idx = static_cast<size_t>(tid);
    static constexpr size_t kTypeIdxCount =
        static_cast<size_t>(TypeId::Count);
    if (type_idx < kTypeIdxCount) {
      if (is_array) {
        if (array_type_token_cache_[type_idx] == 0) {
          const size_t base_len = std::strlen(base);
          char name[32];
          if (base_len + 2u < sizeof(name)) {
            std::memcpy(name, base, base_len);
            name[base_len] = '[';
            name[base_len + 1] = ']';
            name[base_len + 2] = '\0';
            array_type_token_cache_[type_idx] = InternToken(name);
          } else {
            std::string tmp(base);
            tmp += "[]";
            array_type_token_cache_[type_idx] = InternToken(tmp);
          }
        }
        return array_type_token_cache_[type_idx];
      }
      if (scalar_type_token_cache_[type_idx] == 0) {
        scalar_type_token_cache_[type_idx] = InternToken(base);
      }
      return scalar_type_token_cache_[type_idx];
    }
    if (!is_array) return InternToken(base);
    std::string name(base);
    name += "[]";
    return InternToken(name);
  }

  CrateField BuildTimeSamplesField(const PrimSpec& prim, PropNameId prop_id) {
    auto* samples = prim.time_samples(prop_id);
    uint64_t num_samples = samples ? samples->size() : 0;

    size_t times_bytes = 0;
    size_t times_total = 0;
    if (!safe::mul(num_samples, size_t(8), &times_bytes) ||
        !safe::add(size_t(8), times_bytes, &times_total)) {
      result_.error = "timeSamples times payload size overflow";
      return CrateField{};
    }
    std::vector<uint8_t> times_data(times_total);
    std::memcpy(times_data.data(), &num_samples, 8);
    size_t rep_i = 0;

    size_t reps_bytes = 0;
    size_t header_size = 0;
    if (!safe::mul(num_samples, size_t(8), &reps_bytes) ||
        !safe::add(size_t(32), reps_bytes, &header_size)) {
      result_.error = "timeSamples header payload size overflow";
      return CrateField{};
    }
    std::vector<uint8_t> header_data(header_size, 0);
    int64_t fwd1 = 8;
    std::memcpy(header_data.data(), &fwd1, 8);
    int64_t fwd2 = 8;
    std::memcpy(header_data.data() + 16, &fwd2, 8);
    std::memcpy(header_data.data() + 24, &num_samples, 8);
    if (samples) {
      for (const auto& kv : *samples) {
        double t = kv.first;
        std::memcpy(times_data.data() + 8 + rep_i * 8, &t, 8);
        const Value* ts_val = prim.time_sample_value(kv.second);
        uint64_t vr = ts_val
                           ? EncodeValue(*ts_val).raw()
                           : ValueRep::Make(CrateTypeId::ValueBlock, 0, false, true)
                                 .raw();
        std::memcpy(header_data.data() + 32 + rep_i * 8, &vr, 8);
        ++rep_i;
      }
    }

    while (rep_i < num_samples) {
      uint64_t vr = ValueRep::Make(CrateTypeId::ValueBlock, 0, false, true).raw();
      std::memcpy(header_data.data() + 32 + rep_i * 8, &vr, 8);
      ++rep_i;
    }
    DataBlock times_block{TypeId::Double, std::move(times_data)};
    CacheBlockHash(&times_block);
    uint64_t times_block_idx = InternBlock(std::move(times_block));
    uint64_t times_rep_raw =
        ValueRep::Make(CrateTypeId::Double, times_block_idx, true, false).raw();
    std::memcpy(header_data.data() + 8, &times_rep_raw, 8);
    uint64_t header_idx = value_data_.size();
    value_data_.push_back({TypeId::Invalid, std::move(header_data)});

    CrateField ts_field;
    ts_field.token_index.value = CommonToken(common_.timeSamples, "timeSamples");
    ts_field.value_rep =
        ValueRep::Make(CrateTypeId::TimeSamples, header_idx, false, false);
    return ts_field;
  }

  void EmitPropertySpec(uint32_t path_index, SpecType st,
                        const std::vector<CrateField>& field_list) {
    SmallU32List fs;
    for (const auto& f : field_list) {
      fs.push_back(static_cast<uint32_t>(fields_.size()));
      fields_.push_back(f);
    }
    uint32_t fs_idx = AddFieldset(fs);
    CrateSpec spec;
    spec.path_index.value = path_index;
    spec.fieldset_index.value = fs_idx;
    spec.spec_type = st;
    specs_.push_back(spec);
  }

  void EmitPropertySpec(uint32_t path_index, SpecType st,
                        const SmallFieldList& field_list) {
    SmallU32List fs;
    field_list.ForEach([&](const CrateField& f) {
      fs.push_back(static_cast<uint32_t>(fields_.size()));
      fields_.push_back(f);
    });
    uint32_t fs_idx = AddFieldset(std::move(fs));
    CrateSpec spec;
    spec.path_index.value = path_index;
    spec.fieldset_index.value = fs_idx;
    spec.spec_type = st;
    specs_.push_back(spec);
  }

  SmallU32List EmitPrimProperties(const PrimSpec& prim) {
    SmallU32List prop_name_tokens;
    std::vector<uint32_t> emitted_prop_tokens;
    PropNameTable& name_table = GetPropNameTable();
    const std::string& base = prim.path().str();
    const std::string_view base_view(base);
    // One stem hash per prim: every property-path intern below then costs a
    // short name hash + one probe instead of hashing the whole prefixed path.
    const uint64_t base_hash = HashView(base_view);
    auto intern_prop = [&](std::string_view name) -> uint32_t {
      if (name.find_first_of("./") != std::string_view::npos) {
        // Property/relationship names never contain path separators in
        // practice; canonicalize defensively if one ever does.
        std::string full;
        full.reserve(base_view.size() + 1 + name.size());
        full.assign(base_view.data(), base_view.size());
        full.push_back('.');
        full.append(name.data(), name.size());
        return InternPathCopy(full);
      }
      return InternPropPath(base_view, name, base_hash);
    };

    // Slots are name-sorted (PropIndex::sort sorts by name, not the run-unstable
    // PropNameId), so this emits the `properties` field, specs and value blocks
    // in a deterministic, canonical order.
    for (const auto& slot : prim.properties().slots()) {
      const std::string& prop_name = name_table.get(slot.name_id);
      const uint32_t prop_tok = InternToken(prop_name);
      const Value* val = prim.property_value(slot.name_id);
      const bool has_ts = prim.has_time_samples(slot.name_id);
      SmallFieldList flds;

      CrateField tf;
      tf.token_index.value = CommonToken(common_.typeName, "typeName");
      tf.value_rep =
          ValueRep::Make(CrateTypeId::Token,
                         AttrTypeToken(prim, prop_name, val, slot.flags),
                         false, true);
      flds.push_back(tf);

      if (slot.flags & PropSlot::kFlagUniform) {
        CrateField vf;
        vf.token_index.value = CommonToken(common_.variability, "variability");
        vf.value_rep = ValueRep::Make(CrateTypeId::Variability, 1, false, true);
        flds.push_back(vf);
      }

      if (slot.flags & PropSlot::kFlagCustom) {
        CrateField cf;
        cf.token_index.value = CommonToken(common_.custom, "custom");
        cf.value_rep = ValueRep::Make(CrateTypeId::Bool, 1, false, true);
        flds.push_back(cf);
      }

      if (val) {
        CrateField df;
        df.token_index.value = CommonToken(common_.default_value, "default");
        df.value_rep = EncodeValue(*val);
        flds.push_back(df);
      }

      if (slot.flags & PropSlot::kFlagConnection) {
        if (const std::vector<Path>* conns = prim.connection(slot.name_id)) {
          uint64_t blk = StorePathListOp(*conns);
          if (blk != 0) {
            CrateField cf;
            cf.token_index.value =
                CommonToken(common_.connectionPaths, "connectionPaths");
            cf.value_rep =
                ValueRep::Make(CrateTypeId::PathListOp, blk, false, false);
            flds.push_back(cf);
          }
        }
      }

      if (has_ts) flds.push_back(BuildTimeSamplesField(prim, slot.name_id));

      if (const PropMeta* pm = prim.property_meta(slot.name_id)) {
        auto tok_field = [&](const char* name, const std::string& v) {
          CrateField f;
          f.token_index.value =
              (name[0] == 'i') ? CommonToken(common_.interpolation,
                                             "interpolation")
                               : CommonToken(common_.colorSpace, "colorSpace");
          f.value_rep =
              ValueRep::Make(CrateTypeId::Token, InternToken(v), false, true);
          flds.push_back(f);
        };
        if (pm->authored & PropMeta::kInterpolation)
          tok_field("interpolation", pm->interpolation);
        if (pm->authored & PropMeta::kColorSpace)
          tok_field("colorSpace", pm->colorSpace);
        if (pm->authored & PropMeta::kElementSize) {
          CrateField f;
          f.token_index.value = CommonToken(common_.elementSize, "elementSize");
          f.value_rep = ValueRep::Make(
              CrateTypeId::Int,
              static_cast<uint64_t>(static_cast<uint32_t>(pm->elementSize)),
              false, true);
          flds.push_back(f);
        }
        if ((pm->authored & PropMeta::kCustomData) &&
            pm->customData.is_dictionary()) {
          CrateField f;
          f.token_index.value = CommonToken(common_.customData, "customData");
          f.value_rep = EncodeValue(pm->customData);
          flds.push_back(f);
        }
      }

      EmitPropertySpec(intern_prop(prop_name), SpecType::Attribute, flds);
      prop_name_tokens.push_back(prop_tok);
      emitted_prop_tokens.push_back(prop_tok);
    }

    const std::vector<PropNameId> time_sampled_props = prim.time_sampled_properties();
    if (!emitted_prop_tokens.empty() && !time_sampled_props.empty()) {
      std::sort(emitted_prop_tokens.begin(), emitted_prop_tokens.end());
    }

    for (auto prop_id : time_sampled_props) {
      const std::string& prop_name = name_table.get(prop_id);
      if (prim.property_value(prop_id)) {
        continue;
      }
      const uint32_t prop_tok = InternToken(prop_name);
      if (std::binary_search(emitted_prop_tokens.begin(),
                             emitted_prop_tokens.end(),
                             prop_tok)) {
        continue;
      }
      SmallFieldList flds;
      const Value* sv = nullptr;
      if (auto* s = prim.time_samples(prop_id))
        if (!s->empty()) sv = prim.time_sample_value((*s)[0].second);
      CrateField tf;
      tf.token_index.value = CommonToken(common_.typeName, "typeName");
      tf.value_rep =
          ValueRep::Make(CrateTypeId::Token,
                         AttrTypeToken(prim, prop_name, sv, 0),
                         false, true);
      flds.push_back(tf);
      flds.push_back(BuildTimeSamplesField(prim, prop_id));
      EmitPropertySpec(intern_prop(prop_name), SpecType::Attribute, flds);
      prop_name_tokens.push_back(prop_tok);
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
    for (size_t ri = 0; ri < rels.size(); ++ri) {
      const auto& rel = rels[ri];
      const std::string_view rel_name = rel.name;
      const uint32_t rel_tok = InternToken(rel_name);
      SmallFieldList flds;
      CrateField vf;
      vf.token_index.value = CommonToken(common_.variability, "variability");
      vf.value_rep = ValueRep::Make(CrateTypeId::Variability, 1, false, true);
      flds.push_back(vf);
      if (rel.targets && !rel.targets->empty()) {
        uint64_t blk = StorePathListOp(*rel.targets);
        if (blk != 0) {
          CrateField rf;
          rf.token_index.value = CommonToken(common_.targetPaths, "targetPaths");
          rf.value_rep = ValueRep::Make(CrateTypeId::PathListOp, blk, false, false);
          flds.push_back(rf);
        }
      }
      EmitPropertySpec(intern_prop(rel_name), SpecType::Relationship, flds);
      prop_name_tokens.push_back(rel_tok);
    }

    return prop_name_tokens;
  }
