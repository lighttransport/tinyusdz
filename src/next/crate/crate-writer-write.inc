// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl top-level write orchestration.
// Included inside CrateWriter::Impl public section.

  explicit Impl(const CrateWriteOptions& options)
      : options_(options), writer_(buffer_) {
    // Initialize buffer
  }

  bool enable_timing() const { return options_.enable_timing; }

  CrateWriteResult Write(const Layer& layer, const CrateWriteSink* sink = nullptr) {
    result_ = CrateWriteResult();
    buffer_.clear();
    timing_ = TimingState();
    timing_.enabled = options_.enable_timing;
    common_ = CommonTokenIds();

    // Clear all accumulated table state from previous writes
    tokens_.clear();
    token_storage_.clear();
    token_to_index_.clear();
    string_indices_.clear();
    string_storage_.clear();
    string_to_index_.clear();
    paths_.clear();
    ancestor_scan_path_indices_.clear();
    path_to_index_.clear();
    fields_.clear();
    fieldset_items_.clear();
    fieldset_offsets_.clear();
    fieldset_sizes_.clear();
    fieldset_extra_head_.clear();
    fieldset_extra_tail_.clear();
    fieldset_extra_count_.clear();
    fieldset_extra_values_.clear();
    fieldset_extra_next_.clear();
    specs_.clear();
    value_data_.clear();
    value_offsets_.clear();
    arrays_passed_through_ = 0;
    arrays_reencoded_ = 0;
    block_dedup_.clear();
    blocks_deduped_ = 0;
    // Reserve block index 0 (empty placeholder). The writer emits empty arrays
    // as a ValueRep with payload==0 / non-inlined (pxrUSD's empty-array marker);
    // reserving index 0 ensures no real block is ever referenced by payload 0,
    // so the offset-patch pass can safely leave payload-0 reps untouched.
    value_data_.push_back({TypeId::Invalid, std::vector<uint8_t>()});
    // writer_ already references buffer_; just clear buffer

    // Reserve space for bootstrap header
    buffer_.resize(kCrateBootstrapSize, 0);

    // Reserve table capacity up front: a big scene appends millions of
    // paths/specs/fields and dedup entries. Pre-allocating these avoids
    // repeated geometric growth and hash-table rehash storms in the hot build
    // path (~18% on Island before this optimization).
    // Each prim yields ~1 prim spec + 1 spec per property; sizes are heuristic
    // (over-reserve is cheaper than rehashing), so this never changes output.
    {
      size_t nprim = layer.prims().size();
      size_t nprop = 0;
      for (const auto& p : layer.prims()) nprop += p.properties().slots().size();
      const size_t nspec = nprim + nprop + 16;
      ancestor_scan_path_indices_.reserve(nprim / 8 + 16);
      path_to_index_.reserve(nspec * 2);
      specs_.reserve(nspec);
      fieldset_offsets_.reserve(nspec);
      fieldset_sizes_.reserve(nspec);
      fieldset_extra_head_.reserve(nspec);
      fieldset_extra_tail_.reserve(nspec);
      fieldset_extra_count_.reserve(nspec);
      fieldset_items_.reserve(nspec * 3);
      fieldset_extra_values_.reserve(nprim);
      fieldset_extra_next_.reserve(nprim);
      fields_.reserve(nspec * 3);
      value_data_.reserve(nprop + nprim + 16);
      block_dedup_.reserve(nprop + nprim + 16);
    }

    // Build tables from layer
    auto t0 = Clock::now();
    BuildTables(layer);
    timing_.build_tables_ms = MsSince(t0);

    // Build fields, fieldsets, and specs from layer
    t0 = Clock::now();
    BuildFieldsAndSpecs(layer);
    timing_.build_fields_ms = MsSince(t0);
    if (!result_.error.empty()) {
      result_.success = false;
      return result_;
    }

    // Pre-register path element tokens so they're included in TOKENS section.
    // WritePathsSection calls InternToken() for path elements, but that happens
    // AFTER WriteTokensSection. Pre-registering ensures they're in the output.
    {
      t0 = Clock::now();
      auto intern_token_view = [&](std::string_view comp) {
        if (comp.empty()) return;
        if (token_to_index_.find(comp) != nullptr) return;
        InternToken(comp);
      };
      for (const auto& path : paths_) {
        if (path.empty() || path == "/") {
          InternToken("");  // Token 0 = empty string for root path marker
        } else {
          // Split into path-element tokens and register each. Property paths use
          // a '.' separator between the prim path and the property name
          // ("/a/b.prop"), so split on BOTH '/' and '.' — otherwise the trailing
          // "b.prop" would be interned as one bogus token (and "prop" would be
          // missing, only to be re-added later, after the TOKENS section).
          size_t start = 1;
          for (size_t pos = start; pos <= path.size(); ++pos) {
            if (pos == path.size() || path[pos] == '/' || path[pos] == '.') {
              intern_token_view(
                  std::string_view(path.data() + start, pos - start));
              start = pos + 1;
            }
          }
        }
      }
      timing_.prereg_path_tokens_ms = MsSince(t0);
    }

    // Write all sections and record offsets
    std::vector<CrateSection> sections;

    // VALUE DATA section must come FIRST (before structural sections), NOT in TOC.
    // pxrUSD stores VALUE data between bootstrap and structural sections.
    // The bootstrap TOC offset points past the VALUE data to the TOC itself.
    // Bootstrap is 64 bytes, so reserve 64 bytes before VALUE section.
    // WriteBootstrap at the end patches these first 64 bytes.
    buffer_.resize(64, 0);
    value_start_offset_ = 64;

    // Compute the VALUE section layout and patch field offsets (always needed).
    t0 = Clock::now();
    const uint64_t value_section_size = ComputeValueLayoutAndPatch();
    timing_.value_layout_ms = MsSince(t0);

    // Streaming mode keeps the VALUE bytes OUT of buffer_ (they are streamed
    // straight from their source at the end), so the structural sections sit at
    // physical buffer offset 64 but their TRUE file offset is shifted past the
    // (absent) value section. struct_base is that shift; 0 in the in-memory path
    // where the value bytes are physically present in buffer_.
    const uint64_t struct_base = sink ? value_section_size : 0;

    if (!sink) {
      // Reserve the output buffer up-front. The VALUE section dominates the
      // output; without a reserve std::vector growth would double its capacity
      // repeatedly, transiently needing ~2x the output size — expensive and a
      // real risk near the wasm32 2 GB ceiling. Over-estimating only wastes
      // capacity; under-estimating just falls back to normal growth.
      {
        size_t est = 64;
        for (const auto& b : value_data_) est += b.size() + 8;  // +8 align slack
        est += est / 8 + (size_t(1) << 20);  // ~12% for structural sections + 1MB
        if (est > buffer_.capacity()) buffer_.reserve(est);
      }
      t0 = Clock::now();
      EmitValueBytesToBuffer();
      timing_.value_emit_ms = MsSince(t0);
    }

    // TOKENS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "TOKENS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size() + struct_base);
      t0 = Clock::now();
      WriteTokensSection();
      timing_.tokens_ms = MsSince(t0);
      sec.size = static_cast<int64_t>(buffer_.size() + struct_base) - sec.start;
      sections.push_back(sec);
    }

    // STRINGS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "STRINGS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size() + struct_base);
      t0 = Clock::now();
      WriteStringsSection();
      timing_.strings_ms = MsSince(t0);
      sec.size = static_cast<int64_t>(buffer_.size() + struct_base) - sec.start;
      sections.push_back(sec);
    }

    // FIELDS section (written before VALUE so we know the offsets)
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "FIELDS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size() + struct_base);
      t0 = Clock::now();
      WriteFieldsSection();
      timing_.fields_ms = MsSince(t0);
      if (!result_.error.empty()) {
        result_.success = false;
        return result_;
      }
      sec.size = static_cast<int64_t>(buffer_.size() + struct_base) - sec.start;
      sections.push_back(sec);
    }

    // FIELDSETS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "FIELDSETS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size() + struct_base);
      t0 = Clock::now();
      WriteFieldsetsSection();
      timing_.fieldsets_ms = MsSince(t0);
      sec.size = static_cast<int64_t>(buffer_.size() + struct_base) - sec.start;
      sections.push_back(sec);
    }

    // PATHS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "PATHS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size() + struct_base);
      t0 = Clock::now();
      WritePathsSection();
      timing_.paths_ms = MsSince(t0);
      sec.size = static_cast<int64_t>(buffer_.size() + struct_base) - sec.start;
      sections.push_back(sec);
    }

    // SPECS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "SPECS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size() + struct_base);
      t0 = Clock::now();
      WriteSpecsSection();
      timing_.specs_ms = MsSince(t0);
      sec.size = static_cast<int64_t>(buffer_.size() + struct_base) - sec.start;
      sections.push_back(sec);
    }

    // Write TOC at the end (VALUE section is NOT included in TOC)
    int64_t toc_offset = static_cast<int64_t>(buffer_.size() + struct_base);
    t0 = Clock::now();
    WriteTOC(sections);
    timing_.toc_ms = MsSince(t0);

    // Write bootstrap header at the beginning
    WriteBootstrap(toc_offset);

    // Streaming emit: bootstrap (64 B) -> VALUE bytes (streamed from source) ->
    // structural sections + TOC (the only part staged in buffer_). buffer_ holds
    // [bootstrap(64)][TOKENS..SPECS][TOC] with the VALUE section logically spliced
    // in after the bootstrap; stream the three parts in file order.
    if (sink) {
      t0 = Clock::now();
      if (!(*sink)(buffer_.data(), 64)) {
        result_.error = "sink aborted (bootstrap)";
        return result_;
      }
      if (!StreamValueBytes(*sink)) {
        result_.error = "sink aborted (value section)";
        return result_;
      }
      if (buffer_.size() > 64 &&
          !(*sink)(buffer_.data() + 64, buffer_.size() - 64)) {
        result_.error = "sink aborted (structural sections)";
        return result_;
      }
      timing_.stream_ms = MsSince(t0);
    }

    result_.success = true;
    result_.bytes_written =
        sink ? (64 + value_section_size + (buffer_.size() - 64)) : buffer_.size();
    result_.token_count = tokens_.size();
    result_.string_count = string_indices_.size();
    result_.path_count = paths_.size();
    result_.spec_count = specs_.size();
    result_.field_count = fields_.size();
    result_.arrays_passed_through = arrays_passed_through_;
    result_.arrays_reencoded = arrays_reencoded_;
    result_.blocks_deduped = blocks_deduped_;

    PrintTiming();

    return result_;
  }

  const std::vector<uint8_t>& buffer() const { return buffer_; }
  // Move the finished output buffer out (avoids copying the whole crate -- the
  // VALUE section alone is multi-GB on large scenes). The Impl is not reused after
  // a Write() without a fresh clear(), so moving here is safe.
  std::vector<uint8_t> take_buffer() { return std::move(buffer_); }
