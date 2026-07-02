// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl section emission and VALUE layout.
// Included inside CrateWriter::Impl private section.

  // ============================================================
  // Section writing
  // ============================================================

  void WriteBootstrap(int64_t toc_offset) {
    // Bootstrap is exactly 64 bytes: [ident(8)][version(8)][tocOffset(8)][reserved(40)]
    size_t pos = 0;
    std::memcpy(buffer_.data() + pos, kCrateMagic, 8); pos += 8;
    buffer_[pos++] = options_.version_major;
    buffer_[pos++] = options_.version_minor;
    buffer_[pos++] = options_.version_patch;
    buffer_[pos++] = 0; // pad
    buffer_[pos++] = 0; buffer_[pos++] = 0;
    buffer_[pos++] = 0; buffer_[pos++] = 0; // total 8 bytes for version
    writer_.patch_u64(pos, static_cast<uint64_t>(toc_offset)); pos += 8;
    // Reserved padding to 64 bytes
    while (pos < 64) buffer_[pos++] = 0;
  }

  void WriteTokensSection() {
    // Build uncompressed token data: concatenated null-terminated strings
    std::vector<uint8_t> raw;
    size_t total = 0;
    for (const auto& token : tokens_) total += token.size() + 1;
    raw.reserve(total);  // avoid incremental reallocations
    for (const auto& token : tokens_) {
      raw.insert(raw.end(), token.begin(), token.end());
      raw.push_back(0);  // null terminator
    }

    writer_.write_u64(tokens_.size());
    writer_.write_u64(raw.size());  // uncompressed_size

    if (raw.empty()) {
      writer_.write_u64(0);  // compressed_size = 0
      return;
    }

    CompressResult cr = CompressCrateBlob(raw.data(), raw.size());
    if (!cr.success) {
      // Fall back to storing uncompressed
      writer_.write_u64(raw.size());
      writer_.write_bytes(raw.data(), raw.size());
    } else {
      writer_.write_u64(cr.data.size());
      writer_.write_bytes(cr.data.data(), cr.data.size());
    }
  }

  void WriteStringsSection() {
    writer_.write_u64(string_indices_.size());
    for (uint32_t str_idx : string_indices_) {
      writer_.write_u32(str_idx);
    }
  }

  void WriteFieldsSection() {
    uint64_t num_fields = fields_.size();
    writer_.write_u64(num_fields);

    // Encode token indices using Usd_IntegerCompression (delta + LZ4)
    {
      std::vector<uint32_t> indices(num_fields);
      for (size_t i = 0; i < num_fields; i++) {
        indices[i] = fields_[i].token_index.value;
      }
      // Delta-encode
      std::vector<uint8_t> delta_encoded = EncodeDeltaU32(indices.data(), num_fields);
      // LZ4-compress (TfFastCompression format)
      CompressResult cr = CompressCrateBlob(delta_encoded.data(), delta_encoded.size());
      if (cr.success) {
        writer_.write_u64(cr.data.size());
        writer_.write_bytes(cr.data.data(), cr.data.size());
      } else {
        // Fallback: store raw delta-encoded data
        writer_.write_u64(delta_encoded.size());
        writer_.write_bytes(delta_encoded.data(), delta_encoded.size());
      }
    }

    // Write compressed value reps (TfFastCompression LZ4 format)
    size_t value_rep_bytes = 0;
    if (!safe::mul(static_cast<size_t>(num_fields), size_t(8), &value_rep_bytes)) {
      result_.error = "Field value-rep byte size overflow";
      return;
    }
    std::vector<uint8_t> raw_reps(value_rep_bytes);
    for (size_t i = 0; i < num_fields; i++) {
      uint64_t v = fields_[i].value_rep.raw();
      std::memcpy(raw_reps.data() + i * 8, &v, 8);
    }
    CompressResult cr_reps = CompressCrateBlob(raw_reps.data(), raw_reps.size());
    if (!cr_reps.success) {
      writer_.write_u64(raw_reps.size());
      writer_.write_bytes(raw_reps.data(), raw_reps.size());
    } else {
      writer_.write_u64(cr_reps.data.size());
      writer_.write_bytes(cr_reps.data.data(), cr_reps.data.size());
    }
  }

  void WriteFieldsetsSection() {
    // Build flat array: for each fieldset, write its field indices then a
    // 0xFFFFFFFF sentinel (matching pxrUSD and the old tinyusdz writer).
    std::vector<uint32_t> flat;
    flat.reserve(fields_.size() + FieldsetCount());
    for (uint32_t fs_idx = 0; fs_idx < FieldsetCount(); ++fs_idx) {
      ForEachFieldsetItem(fs_idx, [&](uint32_t field_idx) {
        flat.push_back(field_idx);
      });
      flat.push_back(0xFFFFFFFF);  // sentinel
    }

    uint64_t flat_count = flat.size();
    writer_.write_u64(flat_count);
    // Use Usd_IntegerCompression (delta + LZ4)
    std::vector<uint8_t> delta_encoded =
        EncodeDeltaU32WithCommon(flat.data(), static_cast<size_t>(flat_count), 1);
    CompressResult cr = CompressCrateBlob(delta_encoded.data(), delta_encoded.size());
    if (cr.success) {
      writer_.write_u64(cr.data.size());
      writer_.write_bytes(cr.data.data(), cr.data.size());
    } else {
      writer_.write_u64(delta_encoded.size());
      writer_.write_bytes(delta_encoded.data(), delta_encoded.size());
    }
  }

  void WritePathsSection() {
    auto path_phase_t0 = Clock::now();
    // pxr's compressed path tree requires EVERY ancestor of every path to be
    // present as a node. Flattened/streamed layers can contain specs for prims
    // whose intermediate namespace parents have no authored spec; if those
    // ancestors are omitted from PATHS, OpenUSD can load the layer but builds a
    // corrupted root child table and may crash when exporting the raw SdfLayer.
    {
      // Probe existing paths through path_to_index_ and keep a small synthesized
      // set instead of building a multi-M entry membership table for every scene.
      FlatStringViewU32Map synth_have;
      synth_have.reserve(1024);
      std::deque<std::string> synth;
      const size_t orig = paths_.size();
      auto add = [&](std::string_view v) -> bool {
        if (path_to_index_.find(v) != path_to_index_.end()) {
          return false;
        }
        if (synth_have.find(v) != synth_have.end()) return false;
        synth.emplace_back(v);
        synth_have.set(std::string_view(synth.back()), 0);
        return true;
      };
      constexpr size_t npos = std::string_view::npos;

      auto scan_one = [&](std::string_view cur) {
        const size_t dot = cur.rfind('.');
        const size_t slash = cur.rfind('/');
        if (dot != npos && (slash == npos || dot > slash)) {
          cur = cur.substr(0, dot);  // property path: start from its prim part
          if (!cur.empty() && cur != "/") add(cur);
        }
        while (cur.size() > 1) {
          const size_t sl = cur.rfind('/');
          if (sl == npos || sl == 0) break;
          cur = cur.substr(0, sl);
          add(cur);
        }
      };

      for (size_t i = 0; i < orig; ++i) {
        scan_one(std::string_view(paths_[i]));
      }

      std::vector<uint32_t> scan = ancestor_scan_path_indices_;
      std::sort(scan.begin(), scan.end());
      scan.erase(std::unique(scan.begin(), scan.end()), scan.end());
      for (uint32_t path_idx : scan) {
        if (path_idx >= orig) continue;
        scan_one(std::string_view(paths_[path_idx]));
      }
      for (auto& s : synth) {
        uint32_t idx = static_cast<uint32_t>(paths_.size());
        paths_.push_back(std::move(s));
        path_to_index_.set(std::string_view(paths_.back()), idx);
      }
    }
    timing_.paths_synth_ms = MsSince(path_phase_t0);

    // Build parent/child topology first, then emit pre-order from sorted child
    // lists. This avoids sorting all paths (O(N log N) over full path bytes) while
    // preserving deterministic USD tree order.
    static const uint32_t* const kCharRank = []() -> const uint32_t* {
      static uint32_t t[256];
      for (int c = 0; c < 256; ++c) t[c] = 2u + static_cast<uint32_t>(c);
      t[static_cast<unsigned char>('/')] = 0;
      t[static_cast<unsigned char>('.')] = 1;
      return t;
    }();
    const size_t path_count = paths_.size();
    // parent + local element span
    constexpr uint32_t kNoNode = 0xFFFFFFFF;
    std::vector<uint32_t> parent(path_count, kNoNode);
    std::vector<uint32_t> local_name_pos(path_count, 0);
    std::vector<uint32_t> local_name_len(path_count, 0);

    path_phase_t0 = Clock::now();
    for (uint32_t i = 0; i < static_cast<uint32_t>(path_count); ++i) {
      const std::string& p = paths_[i];
      if (p.empty() || p == "/") {
        local_name_pos[i] = 0;
        local_name_len[i] = 0;
        continue;
      }

      const size_t slash = p.rfind('/');
      const size_t dot = p.rfind('.');
      size_t parent_end;
      if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        parent_end = dot;
      } else {
        parent_end = slash;
      }

      const size_t name_pos = (parent_end == std::string::npos) ? 0u : parent_end + 1u;
      local_name_pos[i] = static_cast<uint32_t>(name_pos);
      local_name_len[i] = static_cast<uint32_t>(p.size() - name_pos);
      if (parent_end == std::string::npos) continue;

      std::string_view parent_path = (parent_end == 0u)
                                        ? std::string_view("/")
                                        : std::string_view(p.data(), parent_end);
      const uint32_t* parent_idx = path_to_index_.find(parent_path);
      if (parent_idx) {
        parent[i] = *parent_idx;
      }
    }
    timing_.paths_parent_ms = MsSince(path_phase_t0);

    // Count children for each parent plus a synthetic no-parent bucket.
    std::vector<uint32_t> child_counts(path_count + 1, 0);
    for (uint32_t i = 0; i < static_cast<uint32_t>(path_count); ++i) {
      const uint32_t key = (parent[i] == kNoNode) ? static_cast<uint32_t>(path_count) : parent[i];
      ++child_counts[key];
    }

    std::vector<uint32_t> child_offsets(path_count + 2, 0);
    for (uint32_t i = 0; i < child_counts.size(); ++i) {
      child_offsets[i + 1] = child_offsets[i] + child_counts[i];
    }

    auto local_name_less = [&](uint32_t a, uint32_t b) -> bool {
      const std::string_view a_name(
          paths_[a].data() + local_name_pos[a],
          local_name_len[a] ? local_name_len[a] : 0u);
      const std::string_view b_name(
          paths_[b].data() + local_name_pos[b],
          local_name_len[b] ? local_name_len[b] : 0u);

      const size_t n = (std::min)(a_name.size(), b_name.size());
      const unsigned char* pa = reinterpret_cast<const unsigned char*>(a_name.data());
      const unsigned char* pb = reinterpret_cast<const unsigned char*>(b_name.data());
      for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return kCharRank[pa[i]] < kCharRank[pb[i]];
      }
      return a_name.size() < b_name.size();
    };

    std::vector<uint32_t> children(path_count);
    path_phase_t0 = Clock::now();
    {
      std::vector<uint32_t> child_write = child_offsets;
      for (uint32_t i = 0; i < static_cast<uint32_t>(path_count); ++i) {
        const uint32_t key = (parent[i] == kNoNode) ? static_cast<uint32_t>(path_count) : parent[i];
        children[child_write[key]++] = i;
      }

      for (uint32_t p = 0; p <= static_cast<uint32_t>(path_count); ++p) {
        const uint32_t b = child_offsets[p];
        const uint32_t e = child_offsets[p + 1];
        if (e - b <= 1) continue;
        std::sort(children.data() + b, children.data() + e,
                  local_name_less);
      }
    }
    timing_.paths_sort_ms = MsSince(path_phase_t0);

    // Next-sibling links keyed by node id (used only for pre-order jump encoding).
    std::vector<uint32_t> next_sibling(path_count, kNoNode);
    {
      path_phase_t0 = Clock::now();
      for (uint32_t p = 0; p <= static_cast<uint32_t>(path_count); ++p) {
        const uint32_t b = child_offsets[p];
        const uint32_t e = child_offsets[p + 1];
        for (uint32_t i = b; i + 1 < e; ++i) {
          next_sibling[children[i]] = children[i + 1];
        }
      }
      timing_.paths_sibling_ms = MsSince(path_phase_t0);
    }

    // Emit nodes in pre-order using an explicit stack (root-level bucket is synthetic
    // parent key path_count, which includes "/" when present as expected).
    std::vector<uint32_t> sorted;
    sorted.reserve(path_count);
    struct Frame {
      uint32_t node;
      uint32_t child;
      uint32_t end;
    };
    std::vector<Frame> stack;
    stack.reserve(64);
    path_phase_t0 = Clock::now();
    stack.push_back({static_cast<uint32_t>(path_count), child_offsets[path_count],
                     child_offsets[path_count + 1]});
    while (!stack.empty()) {
      Frame& frame = stack.back();
      if (frame.child >= frame.end) {
        stack.pop_back();
        continue;
      }
      const uint32_t n = children[frame.child++];
      sorted.push_back(n);
      const uint32_t c_b = child_offsets[n];
      const uint32_t c_e = child_offsets[n + 1];
      if (c_b < c_e) {
        stack.push_back({n, c_b, c_e});
      }
    }
    timing_.paths_emit_ms = MsSince(path_phase_t0);

    const size_t num_paths = sorted.size();
    uint64_t num_encoded = static_cast<uint64_t>(num_paths);
    // Write total paths count AND encoded tree nodes count
    uint64_t total_paths = static_cast<uint64_t>(paths_.size());
    writer_.write_u64(total_paths);
    writer_.write_u64(num_encoded);

    if (num_paths == 0) {
      // No paths other than root, write empty arrays
      std::vector<uint8_t> empty_delta = EncodeDeltaU32(nullptr, 0);
      CompressResult empty_cr = CompressCrateBlob(empty_delta.data(), empty_delta.size());
      writer_.write_u64(empty_cr.data.size());
      writer_.write_bytes(empty_cr.data.data(), empty_cr.data.size());
      writer_.write_u64(empty_cr.data.size());
      writer_.write_bytes(empty_cr.data.data(), empty_cr.data.size());
      writer_.write_u64(empty_cr.data.size());
      writer_.write_bytes(empty_cr.data.data(), empty_cr.data.size());
      return;
    }

    // Build 3 arrays: path indices, element token indices, jump indices.
    std::vector<uint32_t> path_indices(num_paths);     // path index for each node
    std::vector<int32_t> element_tokens(num_paths);    // token index (negated for property)
    std::vector<int32_t> jump_indices(num_paths, -2);  // -2 = leaf (no child, no sibling)

    FlatStringViewU32Map token_view_to_index;
    token_view_to_index.reserve(tokens_.size() * 2 + 1);
    for (uint32_t i = 0; i < tokens_.size(); ++i) {
      token_view_to_index.set(tokens_[i], i);
    }

    // Last path element name ("" for root) and property marker in one scan.
    auto element_info = [](const std::string& p, bool* is_prop) -> std::string_view {
      *is_prop = false;
      if (p.empty() || p == "/") return std::string_view();
      const size_t slash = p.rfind('/');
      const size_t dot = p.rfind('.');
      if (dot != std::string::npos &&
          (slash == std::string::npos || dot > slash)) {
        *is_prop = true;
        return std::string_view(p.data() + dot + 1, p.size() - dot - 1);
      }
      const size_t pos = (slash == std::string::npos) ? 0 : slash + 1;
      return std::string_view(p.data() + pos, p.size() - pos);
    };
    auto token_for_path_element = [&](std::string_view name) -> uint32_t {
      auto it = token_view_to_index.find(name);
      if (it != token_view_to_index.end()) return *it;
      return InternToken(name);
    };
    // Emit each node. has_child == "the next node in pre-order is our child".
    std::vector<uint32_t> sorted_pos(path_count, 0xFFFFFFFFu);
    for (uint32_t i = 0; i < num_paths; ++i) {
      sorted_pos[sorted[i]] = i;
    }

    path_phase_t0 = Clock::now();
    for (uint32_t i = 0; i < num_paths; ++i) {
      const uint32_t node = sorted[i];
      path_indices[i] = node;
      const std::string& p = paths_[node];
      const bool is_root = (p == "/");
      bool is_prop = false;
      std::string_view elem = is_root ? std::string_view()
                                      : element_info(p, &is_prop);
      int32_t tok = static_cast<int32_t>(
          token_for_path_element(elem));
      // Property element tokens are negated (pxr's is_property marker).
      if (is_prop) tok = -tok;
      element_tokens[i] = tok;

      const bool has_child = (i + 1 < num_paths) && (parent[sorted[i + 1]] == node);
      const uint32_t nxt = next_sibling[node];
      const bool has_sibling = (nxt != kNoNode);
      if (has_sibling && has_child) {
        jump_indices[i] =
            static_cast<int32_t>(static_cast<int32_t>(sorted_pos[nxt]) - static_cast<int32_t>(i));
      } else if (has_sibling) {
        jump_indices[i] = 0;
      } else if (has_child) {
        jump_indices[i] = -1;
      } else {
        jump_indices[i] = -2;
      }
    }
    timing_.paths_emit_ms = MsSince(path_phase_t0);

    // Write 3 compressed arrays using Usd_IntegerCompression (delta + LZ4)
    path_phase_t0 = Clock::now();
    auto write_comp_u32 = [&](const uint32_t* vals, size_t count,
                              bool force_common_one = false) {
      std::vector<uint8_t> delta = force_common_one
          ? EncodeDeltaU32WithCommon(vals, count, 1)
          : EncodeDeltaU32(vals, count);
      CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
      if (cr.success) {
        writer_.write_u64(cr.data.size());
        writer_.write_bytes(cr.data.data(), cr.data.size());
      } else {
        writer_.write_u64(delta.size());
        writer_.write_bytes(delta.data(), delta.size());
      }
    };

    auto write_comp_i32 = [&](const int32_t* vals, size_t count,
                              bool force_common_zero = false) {
      std::vector<uint32_t> tmp(count);
      for (size_t i = 0; i < count; i++) {
        tmp[i] = static_cast<uint32_t>(static_cast<int32_t>(vals[i]));
      }
      std::vector<uint8_t> delta = force_common_zero
          ? EncodeDeltaU32WithCommon(tmp.data(), count, 0)
          : EncodeDeltaU32(tmp.data(), count);
      CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
      if (cr.success) {
        writer_.write_u64(cr.data.size());
        writer_.write_bytes(cr.data.data(), cr.data.size());
      } else {
        writer_.write_u64(delta.size());
        writer_.write_bytes(delta.data(), delta.size());
      }
    };

    write_comp_u32(path_indices.data(), num_paths, true);
    write_comp_i32(element_tokens.data(), num_paths, true);
    write_comp_i32(jump_indices.data(), num_paths, true);
    timing_.paths_compress_ms = MsSince(path_phase_t0);
  }

  void WriteSpecsSection() {
    uint64_t num_specs = specs_.size();
    writer_.write_u64(num_specs);

    // Convert fieldset numbers to flat array offsets (matching pxrUSD format).
    // Build offset mapping: for each fieldset, its offset in the flat array
    // is the sum of all prior fieldsets' sizes (including sentinels).
    std::vector<uint32_t> fieldset_number_to_offset;
    {
      fieldset_number_to_offset.reserve(FieldsetCount());
      uint32_t current_offset = 0;
      for (uint32_t fs_idx = 0; fs_idx < FieldsetCount(); ++fs_idx) {
        fieldset_number_to_offset.push_back(current_offset);
        current_offset += FieldsetSize(fs_idx) + 1; // +1 for sentinel
      }
    }

    // Write 3 compressed integer arrays (delta+LZ4 Usd_IntegerCompression)
    std::vector<uint32_t> path_indices(num_specs);
    std::vector<uint32_t> fieldset_indices(num_specs);
    std::vector<uint32_t> spec_types(num_specs);
    for (size_t i = 0; i < num_specs; i++) {
      path_indices[i] = specs_[i].path_index.value;
      // Convert fieldset number → flat array offset
      uint32_t fs_num = specs_[i].fieldset_index.value;
      fieldset_indices[i] = (fs_num < fieldset_number_to_offset.size())
                            ? fieldset_number_to_offset[fs_num]
                            : 0;
      spec_types[i] = static_cast<uint32_t>(specs_[i].spec_type);
    }

    auto write_comp = [&](const uint32_t* vals, size_t count,
                          bool force_common_zero = false) {
      std::vector<uint8_t> delta = force_common_zero
          ? EncodeDeltaU32WithCommon(vals, count, 0)
          : EncodeDeltaU32(vals, count);
      CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
      if (cr.success) {
        writer_.write_u64(cr.data.size());
        writer_.write_bytes(cr.data.data(), cr.data.size());
      } else {
        writer_.write_u64(delta.size());
        writer_.write_bytes(delta.data(), delta.size());
      }
    };

    write_comp(path_indices.data(), num_specs);
    write_comp(fieldset_indices.data(), num_specs);
    write_comp(spec_types.data(), num_specs, true);
  }

  void _PatchTimeSamplesBlock(uint64_t data_idx) {
    // Indirection format header:
    // [i64 fwd1=8][ValueRep times_rep][i64 fwd2=8][u64 count][ValueRep values[N]]
    // times_rep.payload and values[i].payload are block indices to patch to abs offsets.
    if (data_idx >= value_data_.size()) return;
    DataBlock& block = value_data_[data_idx];
    if (block.data.size() < 32) return; // minimum: 2*i64 + 1*u64 + times_rep

    // Patch times_rep at offset 8
    uint64_t vrep_raw;
    std::memcpy(&vrep_raw, block.data.data() + 8, 8);
    {
      ValueRep vrep(vrep_raw);
      if (!vrep.is_inlined()) {
        uint64_t payload = vrep.payload();
        if (payload != 0 && payload < value_offsets_.size()) {
          uint64_t abs_offset = value_start_offset_ + value_offsets_[static_cast<size_t>(payload)];
          vrep_raw = ValueRep::Make(vrep.type_id(), abs_offset,
                                     vrep.is_array(), false,
                                     vrep.is_compressed()).raw();
          std::memcpy(block.data.data() + 8, &vrep_raw, 8);
        }
      }
    }

    // Read num_values at offset 24
    uint64_t num_samples;
    std::memcpy(&num_samples, block.data.data() + 24, 8);
    if (num_samples == 0) return;

    // Patch each sample ValueRep starting at offset 32
    for (size_t i = 0; i < num_samples; i++) {
      size_t voff = 32 + i * 8;
      if (voff + 8 > block.data.size()) break;

      std::memcpy(&vrep_raw, block.data.data() + voff, 8);
      ValueRep vrep(vrep_raw);
      if (vrep.is_inlined()) continue;

      uint64_t payload = vrep.payload();
      if (payload != 0 && payload < value_offsets_.size()) {
        uint64_t abs_offset = value_start_offset_ + value_offsets_[static_cast<size_t>(payload)];
        vrep_raw = ValueRep::Make(vrep.type_id(), abs_offset,
                                   vrep.is_array(), false,
                                   vrep.is_compressed()).raw();
        std::memcpy(block.data.data() + voff, &vrep_raw, 8);
      }
    }
  }

  // Relocate the embedded value ValueReps inside a Dictionary block (built by
  // EncodeDictionary) from block-index payloads to absolute file offsets. Block
  // layout: [u64 count] then per entry [u32 keyStrIdx][i64 recOffset][u64 rep]
  // (20 bytes/entry); the rep sits 12 bytes into each entry.
  void _PatchDictBlock(uint64_t data_idx) {
    if (data_idx >= value_data_.size()) return;
    DataBlock& block = value_data_[data_idx];
    if (block.data.size() < 8) return;
    uint64_t count;
    std::memcpy(&count, block.data.data(), 8);
    for (uint64_t i = 0; i < count; ++i) {
      const size_t voff = 8 + static_cast<size_t>(i) * 20 + 12;
      if (voff + 8 > block.data.size()) break;
      uint64_t vrep_raw;
      std::memcpy(&vrep_raw, block.data.data() + voff, 8);
      ValueRep vrep(vrep_raw);
      if (vrep.is_inlined()) continue;
      uint64_t payload = vrep.payload();
      if (payload != 0 && payload < value_offsets_.size()) {
        uint64_t abs_offset =
            value_start_offset_ + value_offsets_[static_cast<size_t>(payload)];
        vrep_raw = ValueRep::Make(vrep.type_id(), abs_offset, vrep.is_array(),
                                  false, vrep.is_compressed()).raw();
        std::memcpy(block.data.data() + voff, &vrep_raw, 8);
      }
    }
  }

  // Compute the VALUE section layout (8-byte-aligned block offsets) and patch
  // every field/time-sample ValueRep with its absolute file offset. Returns the
  // total VALUE section size in bytes (== the offset just past the last block).
  // This is pure bookkeeping (no bytes emitted); the bytes are written later by
  // EmitValueBytesToBuffer() (in-memory) or StreamValueBytes() (streaming).
  uint64_t ComputeValueLayoutAndPatch() {
    // The VALUE section layout: [Block0][Block1]... each block is raw bytes.
    // Field ValueReps with is_inlined==false hold the offset from the START OF
    // VALUE SECTION to the block's data. Align each block to 8 bytes for
    // memory-mapped zero-copy reads.
    value_offsets_.resize(value_data_.size());
    uint64_t current_offset = 0;
    for (size_t i = 0; i < value_data_.size(); ++i) {
      if (current_offset % 8 != 0) {
        current_offset += 8 - (current_offset % 8);
      }
      value_offsets_[i] = current_offset;
      current_offset += value_data_[i].size();
    }

    // Patch all field ValueReps with actual file offsets (absolute).
    for (auto& field : fields_) {
      if (field.value_rep.is_inlined()) continue;

      CrateTypeId type = field.value_rep.type_id();
      uint64_t data_idx = field.value_rep.payload();

      // payload 0 (non-inlined) is the empty-array marker (block 0 is reserved
      // and never referenced) — leave it as 0 so it is not relocated.
      if (data_idx == 0) continue;

      if (data_idx < value_data_.size() && data_idx < value_offsets_.size()) {
        uint64_t offset = value_start_offset_ + value_offsets_[data_idx];
        field.value_rep = ValueRep::Make(type, offset,
                                         field.value_rep.is_array(), false,
                                         field.value_rep.is_compressed());
      }
      // Patch embedded ValueReps in TimeSamples data blocks
      if (type == CrateTypeId::TimeSamples) {
        _PatchTimeSamplesBlock(data_idx);
      }
    }

    // Patch embedded ValueReps in Dictionary blocks. Nested dictionaries are
    // referenced from other dict blocks (not from fields), so iterate every
    // block rather than only field-referenced ones.
    for (size_t i = 0; i < value_data_.size(); ++i) {
      if (value_data_[i].type == TypeId::Dictionary) {
        _PatchDictBlock(static_cast<uint64_t>(i));
      }
    }

    return current_offset;
  }

  // Emit the VALUE section data blocks into buffer_ (in-memory path), 8-byte
  // aligned. Must produce exactly ComputeValueLayoutAndPatch()'s byte count.
  void EmitValueBytesToBuffer() {
    // Place every value block at its precomputed 8-byte-aligned offset (from
    // ComputeValueLayoutAndPatch) into a single pre-sized, zero-filled region.
    // The blocks occupy disjoint sub-ranges, so the copies parallelize trivially.
    // Byte-identical to the old align()+append loop (align() also zero-pads).
    const size_t n = value_data_.size();
    if (n == 0) return;
    const size_t base = buffer_.size();  // == value_start_offset_ (post-bootstrap)
    const size_t total = static_cast<size_t>(value_offsets_.back()) +
                         value_data_.back().size();
    buffer_.resize(base + total, 0);
    uint8_t* const out = buffer_.data() + base;
    auto copy_range = [&](size_t lo, size_t hi) {
      for (size_t i = lo; i < hi; ++i) {
        const size_t sz = value_data_[i].size();
        if (sz) std::memcpy(out + value_offsets_[i], value_data_[i].bytes(), sz);
      }
    };
#if defined(TINYUSDZ_ENABLE_THREAD)
    int P = (std::min)(ResolveThreads(), 32);
    if (P > 1 && n >= 4096 && total >= (8u << 20)) {
      // Byte-balanced index boundaries (offsets are ascending).
      std::vector<size_t> ib(P + 1);
      ib[0] = 0;
      ib[P] = n;
      for (int t = 1; t < P; ++t) {
        const uint64_t target = static_cast<uint64_t>(total) * t / P;
        ib[t] = static_cast<size_t>(
            std::lower_bound(value_offsets_.begin(), value_offsets_.end(), target) -
            value_offsets_.begin());
      }
      std::vector<std::thread> pool;
      pool.reserve(P);
      for (int t = 0; t < P; ++t)
        pool.emplace_back([&copy_range, &ib, t] { copy_range(ib[t], ib[t + 1]); });
      for (auto& th : pool) th.join();
      return;
    }
#endif
    copy_range(0, n);
  }

  // Stream the VALUE section data blocks straight to `sink`, copying each block
  // verbatim from its source (the retained crate buffer for pass-through arrays,
  // or the re-encoded block) without ever materializing the section. Emits
  // exactly ComputeValueLayoutAndPatch()'s byte count (same 8-byte alignment).
  bool StreamValueBytes(const CrateWriteSink& sink) {
    static const uint8_t kZeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t pos = value_start_offset_;  // value section begins right after bootstrap
    for (size_t i = 0; i < value_data_.size(); ++i) {
      const uint64_t pad = (8 - (pos % 8)) % 8;
      if (pad) {
        if (!sink(kZeros, static_cast<size_t>(pad))) return false;
        pos += pad;
      }
      const size_t sz = value_data_[i].size();
      if (sz) {
        if (!sink(value_data_[i].bytes(), sz)) return false;
        pos += sz;
      }
    }
    return true;
  }

  void WriteTOC(const std::vector<CrateSection>& sections) {
    writer_.write_u64(sections.size());
    for (const auto& sec : sections) {
      writer_.write_bytes(sec.name, 16);
      writer_.write_i64(sec.start);
      writer_.write_i64(sec.size);
    }
  }
