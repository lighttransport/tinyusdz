// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl VALUE encoding helpers.
// Included inside CrateWriter::Impl private section.

  // ============================================================
  // Value encoding
  // ============================================================

  /// Store data in VALUE section and return its offset.
  /// Returns the offset from the START of the VALUE section.
  uint64_t StoreValueData(const void* data, size_t size, TypeId type) {
    DataBlock block;
    block.type = type;
    block.data.resize(size);
    std::memcpy(block.data.data(), data, size);
    return InternBlock(std::move(block));
  }

  // Build an explicit PathListOp value_data_ block for relationship/connection
  // targets: [u8 header=IsExplicit|HasExplicitItems][u64 count][u32 path_idx...].
  // Each target path is interned into the path table. Returns the block index.
  uint64_t StorePathListOp(const std::vector<std::string>& targets) {
    std::vector<uint32_t> idxs;
    idxs.reserve(targets.size());
    for (const auto& t : targets) {
      if (t.empty()) continue;  // skip target-less marker
      idxs.push_back(InternPath(t));
    }
    const uint8_t header = 0x03;  // IsExplicit (0x01) | HasExplicitItems (0x02)
    uint64_t count = idxs.size();
    size_t items_bytes = 0;
    size_t blob_size = 0;
    if (!safe::mul(count, size_t(4), &items_bytes) ||
        !safe::add(size_t(9), items_bytes, &blob_size)) {
      result_.error = "PathListOp payload size overflow";
      return 0;
    }
    std::vector<uint8_t> blob(blob_size);
    blob[0] = header;
    std::memcpy(blob.data() + 1, &count, 8);
    for (size_t i = 0; i < idxs.size(); ++i) {
      std::memcpy(blob.data() + 9 + i * 4, &idxs[i], 4);
    }
    DataBlock block;
    block.type = TypeId::Invalid;
    block.data = std::move(blob);
    return InternBlock(std::move(block));
  }

  // Build a VariantSelectionMap value_data_ block (CrateTypeId 45):
  // [u64 count][(u32 key_string_idx, u32 val_string_idx) * count]. Strings are
  // interned into the string table. Returns the block index.
  uint64_t StoreVariantSelectionMap(
      const std::vector<std::pair<std::string, std::string>>& sels) {
    uint64_t count = sels.size();
    size_t items_bytes = 0;
    size_t blob_size = 0;
    if (!safe::mul(count, size_t(8), &items_bytes) ||
        !safe::add(size_t(8), items_bytes, &blob_size)) {
      result_.error = "VariantSelectionMap payload size overflow";
      return 0;
    }
    std::vector<uint8_t> blob(blob_size);
    std::memcpy(blob.data(), &count, 8);
    for (size_t i = 0; i < sels.size(); ++i) {
      uint32_t k = InternString(sels[i].first);
      uint32_t v = InternString(sels[i].second);
      std::memcpy(blob.data() + 8 + i * 8, &k, 4);
      std::memcpy(blob.data() + 8 + i * 8 + 4, &v, 4);
    }
    DataBlock block;
    block.type = TypeId::Invalid;
    block.data = std::move(blob);
    return InternBlock(std::move(block));
  }

  // Build a TokenVector field ([u64 count][u32 token_idx]*count) — used for
  // primChildren / variantSetChildren / variantChildren.
  CrateField MakeTokenVectorField(const std::string& field_name,
                                  const std::vector<std::string>& names) {
    size_t n = names.size();
    std::vector<uint8_t> raw(8 + n * 4);
    uint64_t cnt = n;
    std::memcpy(raw.data(), &cnt, 8);
    for (size_t i = 0; i < n; ++i) {
      uint32_t tok = InternToken(names[i]);
      std::memcpy(raw.data() + 8 + i * 4, &tok, 4);
    }
    uint64_t data_idx = InternBlock({TypeId::Token, std::move(raw)});
    CrateField f;
    f.token_index.value = InternToken(field_name);
    f.value_rep = ValueRep::Make(CrateTypeId::TokenVector, data_idx, false, false);
    return f;
  }

  // Derive the USD typeName token for an attribute. Prefer the type name the
  // reader recorded (exact role type, array suffix); otherwise fall back to the
  // value's runtime type, appending "[]" for arrays.
  std::string AttrTypeName(const PrimSpec& prim, const std::string& prop_name,
                           const Value* val, uint16_t slot_flags) {
    if (const std::string* tn = prim.property_type_name(prop_name)) {
      if (!tn->empty()) return *tn;
    }
    TypeId tid = val ? val->type_id()
                     : static_cast<TypeId>(TypeId::Invalid);
    const char* base = GetTypeName(tid);
    std::string name = base ? base : "token";
    const bool is_array = (slot_flags & PropSlot::kFlagArray) ||
                          (val && val->is_array());
    if (is_array) name += "[]";
    return name;
  }

  // Build the "timeSamples" CrateField (pxrUSD indirection format) for one
  // time-sampled property.
  CrateField BuildTimeSamplesField(const PrimSpec& prim, PropNameId prop_id) {
    auto* samples = prim.time_samples(prop_id);
    uint64_t num_samples = samples ? samples->size() : 0;

    // Times array block: [u64 count][double[count]]
    size_t times_bytes = 0;
    size_t times_total = 0;
    if (!safe::mul(num_samples, size_t(8), &times_bytes) ||
        !safe::add(size_t(8), times_bytes, &times_total)) {
      result_.error = "timeSamples times payload size overflow";
      return CrateField{};
    }
    std::vector<uint8_t> times_data(times_total);
    std::memcpy(times_data.data(), &num_samples, 8);
    size_t ti = 0;
    if (samples) {
      for (const auto& kv : *samples) {
        double t = kv.first;
        std::memcpy(times_data.data() + 8 + ti * 8, &t, 8);
        ti++;
      }
    }
    uint64_t times_block_idx = InternBlock({TypeId::Double, std::move(times_data)});

    std::vector<ValueRep> sample_reps(num_samples);
    ti = 0;
    if (samples) {
      for (const auto& kv : *samples) {
        const Value* ts_val = prim.time_sample_value(kv.second);
        sample_reps[ti] = ts_val
            ? EncodeValue(*ts_val)
            : ValueRep::Make(CrateTypeId::ValueBlock, 0, false, true);
        ti++;
      }
    }

    // Header block: [i64 fwd1][ValueRep times][i64 fwd2][u64 N][ValueRep vals[N]]
    size_t reps_bytes = 0;
    size_t header_size = 0;
    if (!safe::mul(num_samples, size_t(8), &reps_bytes) ||
        !safe::add(size_t(32), reps_bytes, &header_size)) {
      result_.error = "timeSamples header payload size overflow";
      return CrateField{};
    }
    std::vector<uint8_t> header_data(header_size, 0);
    size_t hp = 0;
    int64_t fwd1 = 8;
    std::memcpy(header_data.data() + hp, &fwd1, 8); hp += 8;
    uint64_t times_rep_raw =
        ValueRep::Make(CrateTypeId::Double, times_block_idx, true, false).raw();
    std::memcpy(header_data.data() + hp, &times_rep_raw, 8); hp += 8;
    int64_t fwd2 = 8;
    std::memcpy(header_data.data() + hp, &fwd2, 8); hp += 8;
    std::memcpy(header_data.data() + hp, &num_samples, 8); hp += 8;
    for (size_t si = 0; si < num_samples; si++) {
      uint64_t vr = sample_reps[si].raw();
      std::memcpy(header_data.data() + hp, &vr, 8); hp += 8;
    }
    uint64_t header_idx = value_data_.size();
    value_data_.push_back({TypeId::Invalid, std::move(header_data)});

    CrateField ts_field;
    ts_field.token_index.value = InternToken("timeSamples");
    ts_field.value_rep =
        ValueRep::Make(CrateTypeId::TimeSamples, header_idx, false, false);
    return ts_field;
  }

  // Emit one separate Attribute/Relationship spec at `prop_path` carrying
  // `field_list`. Appends to fields_/fieldsets_/specs_.
  void EmitPropertySpec(const std::string& prop_path, SpecType st,
                        const std::vector<CrateField>& field_list) {
    std::vector<uint32_t> fs;
    fs.reserve(field_list.size());
    for (const auto& f : field_list) {
      fs.push_back(static_cast<uint32_t>(fields_.size()));
      fields_.push_back(f);
    }
    uint32_t fs_idx = static_cast<uint32_t>(fieldsets_.size());
    fieldsets_.push_back(std::move(fs));
    CrateSpec spec;
    spec.path_index.value = InternPath(prop_path);
    spec.fieldset_index.value = fs_idx;
    spec.spec_type = st;
    specs_.push_back(spec);
  }

  // Emit pxrUSD-style separate Attribute/Relationship specs for every property,
  // relationship, connection and time-sampled attribute of `prim`. Returns the
  // ordered list of child property names (for the prim's "properties" field).
  std::vector<std::string> EmitPrimProperties(const PrimSpec& prim) {
    std::vector<std::string> prop_names;
    PropNameTable& name_table = GetPropNameTable();
    const std::string base = prim.path().str();
    std::unordered_set<std::string> emitted;

    for (const auto& slot : prim.properties().slots()) {
      const std::string& prop_name = name_table.get(slot.name_id);
      const Value* val = prim.property_value(slot.name_id);
      const bool has_ts = prim.has_time_samples(slot.name_id);
      std::vector<CrateField> flds;

      CrateField tf;
      tf.token_index.value = InternToken("typeName");
      std::string tn = AttrTypeName(prim, prop_name, val, slot.flags);
      tf.value_rep =
          ValueRep::Make(CrateTypeId::Token, InternToken(tn), false, true);
      flds.push_back(tf);

      if (slot.flags & PropSlot::kFlagUniform) {
        CrateField vf;
        vf.token_index.value = InternToken("variability");
        vf.value_rep = ValueRep::Make(CrateTypeId::Variability, 1, false, true);
        flds.push_back(vf);
      }

      // The legacy `custom` qualifier (pxr stores a bool field); round-trips
      // through the reader's `custom` branch. Inlined Bool=true.
      if (slot.flags & PropSlot::kFlagCustom) {
        CrateField cf;
        cf.token_index.value = InternToken("custom");
        cf.value_rep = ValueRep::Make(CrateTypeId::Bool, 1, false, true);
        flds.push_back(cf);
      }

      if (val) {
        CrateField df;
        df.token_index.value = InternToken("default");
        df.value_rep = EncodeValue(*val);
        flds.push_back(df);
      }

      if (slot.flags & PropSlot::kFlagConnection) {
        if (const std::vector<Path>* conns = prim.connection(prop_name)) {
          std::vector<std::string> targets;
          targets.reserve(conns->size());
          for (const auto& p : *conns)
            if (!p.str().empty()) targets.push_back(p.str());
          if (!targets.empty()) {
            uint64_t blk = StorePathListOp(targets);
            CrateField cf;
            cf.token_index.value = InternToken("connectionPaths");
            cf.value_rep =
                ValueRep::Make(CrateTypeId::PathListOp, blk, false, false);
            flds.push_back(cf);
          }
        }
      }

      if (has_ts) flds.push_back(BuildTimeSamplesField(prim, slot.name_id));

      // Per-property metadata (interpolation/elementSize/colorSpace/customData).
      if (const PropMeta* pm = prim.property_meta(slot.name_id)) {
        auto tok_field = [&](const char* name, const std::string& v) {
          CrateField f;
          f.token_index.value = InternToken(name);
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
          f.token_index.value = InternToken("elementSize");
          f.value_rep = ValueRep::Make(
              CrateTypeId::Int,
              static_cast<uint64_t>(static_cast<uint32_t>(pm->elementSize)),
              false, true);
          flds.push_back(f);
        }
        if ((pm->authored & PropMeta::kCustomData) &&
            pm->customData.is_dictionary()) {
          CrateField f;
          f.token_index.value = InternToken("customData");
          f.value_rep = EncodeValue(pm->customData);  // pxr binary VtDictionary
          flds.push_back(f);
        }
      }

      EmitPropertySpec(base + "." + prop_name, SpecType::Attribute, flds);
      prop_names.push_back(prop_name);
      emitted.insert(prop_name);
    }

    // Time-sampled properties without a slot (added via add_time_sample directly).
    for (auto prop_id : prim.time_sampled_properties()) {
      const std::string& prop_name = name_table.get(prop_id);
      if (emitted.count(prop_name)) continue;
      std::vector<CrateField> flds;
      const Value* sv = nullptr;
      if (auto* s = prim.time_samples(prop_id))
        if (!s->empty()) sv = prim.time_sample_value((*s)[0].second);
      CrateField tf;
      tf.token_index.value = InternToken("typeName");
      std::string tn = AttrTypeName(prim, prop_name, sv, 0);
      tf.value_rep =
          ValueRep::Make(CrateTypeId::Token, InternToken(tn), false, true);
      flds.push_back(tf);
      flds.push_back(BuildTimeSamplesField(prim, prop_id));
      EmitPropertySpec(base + "." + prop_name, SpecType::Attribute, flds);
      prop_names.push_back(prop_name);
      emitted.insert(prop_name);
    }

    // Relationships (target paths via PathListOp; uniform per USD convention).
    for (const auto& rel_name : prim.relationship_names()) {
      std::vector<CrateField> flds;
      CrateField vf;
      vf.token_index.value = InternToken("variability");
      vf.value_rep = ValueRep::Make(CrateTypeId::Variability, 1, false, true);
      flds.push_back(vf);
      if (const std::vector<Path>* tgts = prim.relationship(rel_name)) {
        std::vector<std::string> targets;
        for (const auto& p : *tgts)
          if (!p.str().empty()) targets.push_back(p.str());
        if (!targets.empty()) {
          uint64_t blk = StorePathListOp(targets);
          CrateField rf;
          rf.token_index.value = InternToken("targetPaths");
          rf.value_rep =
              ValueRep::Make(CrateTypeId::PathListOp, blk, false, false);
          flds.push_back(rf);
        }
      }
      EmitPropertySpec(base + "." + rel_name, SpecType::Relationship, flds);
      prop_names.push_back(rel_name);
    }

    return prop_names;
  }

  // Numeric POD array types whose on-disk block is self-contained (no token /
  // string indices into a source-specific table) and so can be copied verbatim.
  static bool IsPodPassThroughType(CrateTypeId t) {
    switch (t) {
      case CrateTypeId::Bool:
      case CrateTypeId::Int:
      case CrateTypeId::UInt:
      case CrateTypeId::Int64:
      case CrateTypeId::UInt64:
      case CrateTypeId::Half:
      case CrateTypeId::Float:
      case CrateTypeId::Double:
      case CrateTypeId::Vec2f:
      case CrateTypeId::Vec3f:
      case CrateTypeId::Vec4f:
      case CrateTypeId::Vec2d:
      case CrateTypeId::Vec3d:
      case CrateTypeId::Vec4d:
      case CrateTypeId::Vec2h:
      case CrateTypeId::Vec3h:
      case CrateTypeId::Vec4h:
      case CrateTypeId::Vec2i:
      case CrateTypeId::Vec3i:
      case CrateTypeId::Vec4i:
      case CrateTypeId::Quatf:
      case CrateTypeId::Quatd:
      case CrateTypeId::Quath:
      case CrateTypeId::Matrix2d:
      case CrateTypeId::Matrix3d:
      case CrateTypeId::Matrix4d:
        return true;
      default:
        return false;
    }
  }

  // The numeric-array block format ([u64 count][raw] and the integer
  // delta+LZ4 encoding) is stable across all supported crate versions, so a
  // block read from any supported source can be written verbatim.
  static bool VersionPassThroughOk(CrateVersion v) {
    return v.major == kCrateMinVersionMajor && v.minor >= kCrateMinVersionMinor &&
           v.minor <= kCrateMaxVersionMinor;
  }

  // Attempt to write an unmodified, crate-backed array by copying its source
  // block verbatim (no decode -> re-encode). Returns false (leaving the value
  // untouched and unmaterialized) when pass-through is not provably safe.
  bool TryPassThrough(const Value& val, ValueRep* out_rep) {
    const LazyArrayRef* lr = val.lazy_ref();
    if (!lr || !lr->source) return false;
    // Need a known, in-bounds block (empty arrays / unknown layouts decline).
    if (lr->block_len == 0) return false;
    // Only self-contained numeric POD blocks (never token/string/asset/dict).
    if (!IsPodPassThroughType(lr->crate_type)) return false;
    // The writer must emit the same on-disk type it would re-encode to.
    if (ToCrateTypeId(val.type_id()) != lr->crate_type) return false;
    // Encoding must be stable between source and the version we write.
    if (!VersionPassThroughOk(lr->source->version())) return false;

    const size_t sz = lr->source->size();
    if (lr->block_offset > sz || lr->block_len > sz - lr->block_offset) return false;

    // Reference the source block in place; bytes are copied to the output buffer
    // only at WriteValueSection time (no intermediate staging copy).
    DataBlock block;
    block.type = val.type_id();
    block.src = lr->source;  // pins the source buffer alive until the write
    block.src_offset = lr->block_offset;
    block.src_len = lr->block_len;
    uint64_t idx = InternBlock(std::move(block));

    *out_rep = ValueRep::Make(lr->crate_type, idx, /*array=*/true,
                              /*inlined=*/false, lr->rep.is_compressed());
    return true;
  }

  // Encode a Value into a ValueRep.
  // For inlinable scalars, payload contains the value directly.
  // For larger values, payload contains an index into value_data_.
  // Returns the ValueRep and records the field's VALUE section offset.
  // Encode a VtDictionary in pxr binary form (OpenUSD WriteMap + _RecursiveWrite
  // of each Write(VtValue)):
  //   [u64 count] then per entry [u32 keyStrIdx][i64 recOffset=8][u64 ValueRep]
  // The recursive forward offset is a constant 8: the ValueRep immediately
  // follows it and the rep's payload is an absolute offset to the value's data
  // block (pxr's reader follows that payload, so no inline data is required).
  // Embedded non-inline reps are relocated by _PatchDictBlock once block offsets
  // are known. The block is patched in place, so it must NOT be deduped/shared
  // (push directly, do not InternBlock).
  ValueRep EncodeDictionary(const Dict& d) {
    std::vector<uint8_t> blk;
    auto put_u64 = [&](uint64_t v) {
      for (int i = 0; i < 8; ++i) blk.push_back(static_cast<uint8_t>(v >> (i * 8)));
    };
    auto put_u32 = [&](uint32_t v) {
      for (int i = 0; i < 4; ++i) blk.push_back(static_cast<uint8_t>(v >> (i * 8)));
    };
    put_u64(static_cast<uint64_t>(d.entries.size()));
    for (const auto& kv : d.entries) {
      uint32_t kidx = InternString(kv.first);
      ValueRep vr = EncodeValue(kv.second);  // recursive (nested dicts/arrays)
      put_u32(kidx);
      put_u64(static_cast<uint64_t>(static_cast<int64_t>(8)));  // recOffset = 8
      put_u64(vr.raw());
    }
    uint64_t idx = value_data_.size();
    value_data_.push_back({TypeId::Dictionary, std::move(blk)});
    return ValueRep::Make(CrateTypeId::Dictionary, idx, false, false);
  }

  ValueRep EncodeValue(const Value& val) {
    if (val.is_dictionary()) {
      const Dict* d = val.as_dictionary();
      return d ? EncodeDictionary(*d)
               : ValueRep::Make(CrateTypeId::Dictionary, 0, false, false);
    }

    TypeId type_id = val.type_id();
    CrateTypeId crate_type = ToCrateTypeId(type_id);
    bool is_array = val.is_array();

    if (crate_type == CrateTypeId::Invalid) {
      return ValueRep::Make(CrateTypeId::Invalid, 0, is_array, false);
    }

    // Fast path: copy an unmodified, crate-backed array straight through from
    // its source buffer, skipping decode -> re-encode of large payloads. This
    // must run before any accessor below (which would materialize the array).
    if (is_array && val.is_lazy() && !val.is_dirty()) {
      ValueRep pt;
      if (TryPassThrough(val, &pt)) {
        ++arrays_passed_through_;
        return pt;
      }
      ++arrays_reencoded_;  // fell back to full decode + re-encode below
    }

    // Handle inline scalars
    if (!is_array && options_.inline_small_values) {
      uint64_t payload = 0;
      bool can_inline = false;

      switch (type_id) {
        case TypeId::Bool: {
          const bool* v = val.as_bool();
          if (v) { payload = *v ? 1 : 0; can_inline = true; }
          break;
        }
        case TypeId::Int: {
          const int32_t* v = val.as_int();
          if (v) { payload = static_cast<uint64_t>(*v); can_inline = true; }
          break;
        }
        case TypeId::UInt: {
          const uint32_t* v = val.as_uint();
          if (v) { payload = *v; can_inline = true; }
          break;
        }
        case TypeId::Float: {
          const float* v = val.as_float();
          if (v) { uint32_t bits; std::memcpy(&bits, v, 4); payload = bits; can_inline = true; }
          break;
        }
        case TypeId::Token: {
          const std::string* s = val.as_token();
          if (s) { payload = InternToken(*s); can_inline = true; }
          break;
        }
        default:
          break;
      }

      if (can_inline) {
        return ValueRep::Make(crate_type, payload, is_array, true);
      }
    }

    // String / AssetPath via string index (not inline)
    if (type_id == TypeId::String) {
      const std::string* s = val.as_string();
      if (s) {
        uint32_t str_idx = InternString(*s);
        return ValueRep::Make(crate_type, str_idx, false, true);
      }
    }
    if (type_id == TypeId::AssetPath) {
      const std::string* s = val.as_asset_path();
      if (s) {
        // AssetPath: type stays AssetPath; payload is a token index (the path
        // string interned as a token), matching the reader's UnpackAssetPath.
        uint32_t tok_idx = InternToken(*s);
        return ValueRep::Make(CrateTypeId::AssetPath, tok_idx, false, true);
      }
    }
    if (type_id == TypeId::Token) {
      const std::string* s = val.as_token();
      if (s) {
        uint32_t tok_idx = InternToken(*s);
        return ValueRep::Make(CrateTypeId::Token, tok_idx, false, true);
      }
    }

    // Double (always stored in VALUE section, not inlined)
    if (type_id == TypeId::Double) {
      const double* v = val.as_double();
      if (v) {
        uint64_t data_idx = StoreValueData(v, sizeof(double), type_id);
        return ValueRep::Make(crate_type, data_idx, false, false);
      }
      return ValueRep::Make(crate_type, 0, false, false);
    }

    // Non-inline scalars: store in data section
    if (!is_array) {
      size_t elem_size = CrateValueSize(crate_type, false);
      if (elem_size > 0 && elem_size <= 128) {
        uint8_t tmp[128];
        uint64_t data_idx = value_data_.size(); // placeholder
        switch (type_id) {
          case TypeId::Int64: {
            const int64_t* v = val.as_int64();
            if (v) { std::memcpy(tmp, v, 8); data_idx = StoreValueData(tmp, 8, type_id); }
            break;
          }
          case TypeId::UInt64: {
            const uint64_t* v = val.as_uint64();
            if (v) { std::memcpy(tmp, v, 8); data_idx = StoreValueData(tmp, 8, type_id); }
            break;
          }
          case TypeId::Float2: {
            const float* v = val.as_float2();
            if (v) { std::memcpy(tmp, v, 8); data_idx = StoreValueData(tmp, 8, type_id); }
            break;
          }
          case TypeId::Float3:
          case TypeId::Point3f:
          case TypeId::Vector3f:
          case TypeId::Normal3f:
          case TypeId::Color3f: {
            const float* v = val.as_float3();
            if (v) { std::memcpy(tmp, v, 12); data_idx = StoreValueData(tmp, 12, type_id); }
            break;
          }
          case TypeId::Float4:
          case TypeId::Color4f:
          case TypeId::Quatf: {
            const float* v = val.as_float4();
            if (v) { std::memcpy(tmp, v, 16); data_idx = StoreValueData(tmp, 16, type_id); }
            break;
          }
          case TypeId::Double2: {
            const double* v = val.as_double2();
            if (v) { std::memcpy(tmp, v, 16); data_idx = StoreValueData(tmp, 16, type_id); }
            break;
          }
          case TypeId::Double3:
          case TypeId::Point3d:
          case TypeId::Vector3d:
          case TypeId::Normal3d: {
            const double* v = val.as_double3();
            if (v) { std::memcpy(tmp, v, 24); data_idx = StoreValueData(tmp, 24, type_id); }
            break;
          }
          case TypeId::Double4:
          case TypeId::Quatd: {
            const double* v = val.as_double4();
            if (v) { std::memcpy(tmp, v, 32); data_idx = StoreValueData(tmp, 32, type_id); }
            break;
          }
          case TypeId::Matrix4d: {
            const double* v = val.as_matrix4d();
            if (v) { std::memcpy(tmp, v, 128); data_idx = StoreValueData(tmp, 128, type_id); }
            break;
          }
          case TypeId::Matrix4f: {
            const float* v = val.as_matrix4f();
            if (v) {
              double d[16];
              for (size_t i = 0; i < 16; ++i) d[i] = static_cast<double>(v[i]);
              std::memcpy(tmp, d, 128);
              data_idx = StoreValueData(tmp, 128, type_id);
            }
            break;
          }
          default:
            break;
        }
        return ValueRep::Make(crate_type, data_idx, false, false);
      }
      return ValueRep::Make(crate_type, 0, false, false);
    }

    // Array types: store count + elements in data section
    if (is_array) {
      uint64_t count = 0;
      bool data_compressed = false; // whether the array data was compressed
      size_t elem_size = CrateValueSize(crate_type, false);
      (void)elem_size;

      // Collect array data
      std::vector<uint8_t> arr_data;
      auto resize_counted_payload = [&](uint64_t item_count,
                                        size_t item_size) -> bool {
        size_t bytes = 0;
        size_t total = 0;
        if (!safe::mul(item_count, item_size, &bytes) ||
            !safe::add(size_t(8), bytes, &total)) {
          result_.error = "Array payload size overflow";
          return false;
        }
        arr_data.resize(total);
        std::memcpy(arr_data.data(), &item_count, 8);
        return true;
      };
      auto copy_counted_payload = [&](uint64_t item_count, const void* data,
                                      size_t item_size) -> bool {
        if (!resize_counted_payload(item_count, item_size)) {
          return false;
        }
        size_t bytes = 0;
        if (!safe::mul(item_count, item_size, &bytes)) {
          result_.error = "Array payload size overflow";
          return false;
        }
        if (bytes > 0) {
          std::memcpy(arr_data.data() + 8, data, bytes);
        }
        return true;
      };

      switch (type_id) {
        case TypeId::Float: {
          const std::vector<float>* arr = val.as_float_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(float))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::Int: {
          const std::vector<int32_t>* arr = val.as_int_array();
          if (arr) {
            count = arr->size();
            if (options_.compress_arrays && count >= 16) {
              // Compressed integer array: [u64 count][u64 comp_size][LZ4+delta data]
              std::vector<uint32_t> tmp(count);
              for (size_t k = 0; k < count; ++k) tmp[k] = static_cast<uint32_t>((*arr)[k]);
              std::vector<uint8_t> delta = EncodeDeltaU32(tmp.data(), count);
              CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
              if (cr.success) {
                size_t payload_size = 0;
                size_t total_size = 0;
                if (!safe::add(size_t(8), cr.data.size(), &payload_size) ||
                    !safe::add(size_t(8), payload_size, &total_size)) {
                  result_.error = "Compressed integer array payload size overflow";
                  return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
                }
                arr_data.resize(total_size);
                std::memcpy(arr_data.data(), &count, 8);
                uint64_t csz = static_cast<uint64_t>(cr.data.size());
                std::memcpy(arr_data.data() + 8, &csz, 8);
                std::memcpy(arr_data.data() + 16, cr.data.data(), cr.data.size());
                data_compressed = true;
              } else {
                // Fall back to uncompressed
                if (!copy_counted_payload(count, arr->data(),
                                          sizeof(int32_t))) {
                  return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
                }
              }
            } else {
              if (!copy_counted_payload(count, arr->data(), sizeof(int32_t))) {
                return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
              }
            }
          }
          break;
        }
        case TypeId::Float3:
        case TypeId::Point3f:
        case TypeId::Vector3f:
        case TypeId::Normal3f:
        case TypeId::Color3f: {
          const std::vector<float>* arr = val.as_float_array();
          if (arr && arr->size() % 3 == 0) {
            count = arr->size() / 3;
            if (!copy_counted_payload(count * uint64_t(3), arr->data(),
                                      sizeof(float))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
          }
          break;
        }
        case TypeId::Double: {
          const std::vector<double>* arr = val.as_double_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(double))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          } else {
            // Fallback: try reading as float array and convert
            const std::vector<float>* farr = val.as_float_array();
            if (farr) {
              count = farr->size();
              if (!resize_counted_payload(count, sizeof(double))) {
                return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
              }
              double* dptr = reinterpret_cast<double*>(arr_data.data() + 8);
              for (size_t i = 0; i < count; ++i) dptr[i] = static_cast<double>((*farr)[i]);
            }
          }
          break;
        }
        case TypeId::Int64: {
          const std::vector<int64_t>* arr = val.as_int64_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(int64_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::UInt: {
          const std::vector<uint32_t>* arr = val.as_uint_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(uint32_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::UInt64: {
          const std::vector<uint64_t>* arr = val.as_uint64_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(uint64_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::Bool: {
          const std::vector<uint8_t>* arr = val.as_bool_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(uint8_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::Token: {
          const std::vector<std::string>* arr = val.as_token_array();
          if (arr) {
            count = arr->size();
            // Store token indices
            if (!resize_counted_payload(count, sizeof(uint32_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            for (size_t k = 0; k < count; ++k) {
              uint32_t idx = InternToken((*arr)[k]);
              std::memcpy(arr_data.data() + 8 + k * 4, &idx, 4);
            }
          }
          break;
        }
        // Vector / quat / matrix arrays: flat float or double buffer. comps =
        // scalars per element (Vec2f=2, Vec4f/Quatf=4, Matrix4d=16, ...).
        case TypeId::Float2:
        case TypeId::Float4:
        case TypeId::Color4f:
        case TypeId::Texcoord2f:
        case TypeId::Quatf: {
          const std::vector<float>* arr = val.as_float_array();
          const uint32_t comps = ArrayComps(type_id);
          if (arr && comps && arr->size() % comps == 0) {
            count = arr->size() / comps;
            if (!copy_counted_payload(arr->size(), arr->data(),
                                      sizeof(float))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
          }
          break;
        }
        case TypeId::Matrix2f:
        case TypeId::Matrix3f:
        case TypeId::Matrix4f: {
          const std::vector<float>* arr = val.as_float_array();
          const uint32_t comps = ArrayComps(type_id);
          if (arr && comps && arr->size() % comps == 0) {
            count = arr->size() / comps;
            if (!resize_counted_payload(arr->size(), sizeof(double))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
            for (size_t i = 0; i < arr->size(); ++i) {
              double d = static_cast<double>((*arr)[i]);
              std::memcpy(arr_data.data() + 8 + i * sizeof(double), &d,
                          sizeof(double));
            }
          }
          break;
        }
        case TypeId::Double2:
        case TypeId::Double3:
        case TypeId::Double4:
        case TypeId::Point3d:
        case TypeId::Vector3d:
        case TypeId::Normal3d:
        case TypeId::Quatd:
        case TypeId::Matrix2d:
        case TypeId::Matrix3d:
        case TypeId::Matrix4d: {
          const std::vector<double>* arr = val.as_double_array();
          const uint32_t comps = ArrayComps(type_id);
          if (arr && comps && arr->size() % comps == 0) {
            count = arr->size() / comps;
            if (!copy_counted_payload(arr->size(), arr->data(),
                                      sizeof(double))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
          }
          break;
        }
        // Half arrays materialize as a float buffer; narrow back to uint16 half
        // for the crate (only reached when the array is materialized, not on the
        // lazy byte-passthrough path).
        case TypeId::Half:
        case TypeId::Half2:
        case TypeId::Half3:
        case TypeId::Half4:
        case TypeId::Quath: {
          const std::vector<float>* arr = val.as_float_array();
          const uint32_t comps = ArrayComps(type_id);
          if (arr && comps && arr->size() % comps == 0) {
            count = arr->size() / comps;
            if (!resize_counted_payload(arr->size(), sizeof(uint16_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
            for (size_t i = 0; i < arr->size(); ++i) {
              uint16_t h = FloatToHalf((*arr)[i]);
              std::memcpy(arr_data.data() + 8 + i * sizeof(uint16_t), &h,
                          sizeof(uint16_t));
            }
          }
          break;
        }
        default:
          break;
      }

      // Empty array (count == 0): pxrUSD encodes this as payload 0 with NO data
      // block (a count=0 block makes pxr/legacy readers reject it). The next
      // reader also treats payload==0 as an empty array.
      if (count == 0) {
        return ValueRep::Make(crate_type, 0, true, false);
      }

      if (!arr_data.empty()) {
        DataBlock block;
        block.type = type_id;
        block.data = std::move(arr_data);
        uint64_t idx = InternBlock(std::move(block));
        // Return non-inline value rep (index will be resolved to offset later)
        return ValueRep::Make(crate_type, idx, true, false, data_compressed);
      }
    }

    return ValueRep::Make(crate_type, 0, is_array, false);
  }

  // Build fields and specs from layer

