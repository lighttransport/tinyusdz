// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - private CrateWriter implementation body.
// Included by crate-writer.cc; kept separate to make the public wrapper
// translation unit navigable while the writer internals are being split.

// ============================================================
// CrateWriter Implementation
// ============================================================

class CrateWriter::Impl {
public:
  #include "crate-writer-write.inc"

private:
  struct FlatStringViewU32Map {
    struct Entry {
      std::string_view key;
      uint32_t value = 0;
      bool used = false;
    };
    std::vector<Entry> slots;
    size_t count = 0;

    void reserve(size_t n) {
      size_t cap = 1;
      while (cap < n * 2 + 16) cap <<= 1;
      slots.assign(cap, {});
      count = 0;
    }

    void clear() {
      slots.clear();
      count = 0;
    }

    bool needs_rehash() const {
      if (slots.empty()) return false;
      return (count + 1) * 2 > slots.size();
    }

    void rehash() {
      const size_t old_cap = slots.size();
      std::vector<Entry> old_slots = std::move(slots);
      const size_t cap = (old_cap == 0) ? 16 : old_cap * 2;
      slots.assign(cap, {});
      count = 0;
      for (const auto& entry : old_slots) {
        if (!entry.used) continue;
        set(entry.key, entry.value);
      }
    }

    void ensure_room() {
      if (slots.empty()) {
        reserve(16);
        return;
      }
      if (!needs_rehash()) return;
      rehash();
    }

    void set(std::string_view key, uint32_t value) {
      ensure_room();
      size_t mask = slots.size() - 1;
      size_t h = static_cast<size_t>(HashView(key)) & mask;
      for (;;) {
        Entry& e = slots[h];
        if (!e.used) {
          e.key = key;
          e.value = value;
          e.used = true;
          ++count;
          return;
        }
        if (e.key.size() == key.size() &&
            (e.key.empty() || std::memcmp(e.key.data(), key.data(), key.size()) == 0)) {
          e.value = value;
          return;
        }
        h = (h + 1) & mask;
      }
    }

    uint32_t get_or_insert(std::string_view key, uint32_t value_if_missing,
                           bool* inserted = nullptr) {
      ensure_room();
      size_t mask = slots.size() - 1;
      size_t h = static_cast<size_t>(HashView(key)) & mask;
      for (;;) {
        Entry& e = slots[h];
        if (!e.used) {
          e.key = key;
          e.value = value_if_missing;
          e.used = true;
          ++count;
          if (inserted) *inserted = true;
          return e.value;
        }
        if (e.key.size() == key.size() &&
            (e.key.empty() || std::memcmp(e.key.data(), key.data(), key.size()) == 0)) {
          if (inserted) *inserted = false;
          return e.value;
        }
        h = (h + 1) & mask;
      }
    }

    uint32_t* find(std::string_view key) {
      if (slots.empty()) return nullptr;
      const size_t mask = slots.size() - 1;
      size_t h = static_cast<size_t>(HashView(key)) & mask;
      while (slots[h].used) {
        const Entry& e = slots[h];
        if (e.key.size() == key.size() &&
            (e.key.empty() || std::memcmp(e.key.data(), key.data(), key.size()) == 0)) {
          return const_cast<uint32_t*>(&e.value);
        }
        h = (h + 1) & mask;
      }
      return nullptr;
    }

    // Single-probe find-or-prepare-insert: returns the entry for `key`, either
    // the existing one (*found=true) or the empty slot it hashes to
    // (*found=false; the caller MUST fill key/value/used and did ++count via
    // commit_insert). Rehash happens up front so the returned pointer stays
    // valid. Saves the second full hash+probe chain of a find()-then-set()
    // pair on long keys (multi-million-path intern loops).
    Entry* probe(std::string_view key, bool* found) {
      ensure_room();
      const size_t mask = slots.size() - 1;
      size_t h = static_cast<size_t>(HashView(key)) & mask;
      for (;;) {
        Entry& e = slots[h];
        if (!e.used) {
          *found = false;
          return &e;
        }
        if (e.key.size() == key.size() &&
            (e.key.empty() ||
             std::memcmp(e.key.data(), key.data(), key.size()) == 0)) {
          *found = true;
          return &e;
        }
        h = (h + 1) & mask;
      }
    }
    void commit_insert(Entry* e, std::string_view stored_key, uint32_t value) {
      e->key = stored_key;
      e->value = value;
      e->used = true;
      ++count;
    }

    const uint32_t* find(std::string_view key) const {
      if (slots.empty()) return nullptr;
      const size_t mask = slots.size() - 1;
      size_t h = static_cast<size_t>(HashView(key)) & mask;
      while (slots[h].used) {
        const Entry& e = slots[h];
        if (e.key.size() == key.size() &&
            (e.key.empty() || std::memcmp(e.key.data(), key.data(), key.size()) == 0)) {
          return &e.value;
        }
        h = (h + 1) & mask;
      }
      return nullptr;
    }

    const uint32_t* end() const { return nullptr; }
  };

  CrateWriteOptions options_;
  CrateWriteResult result_;
  std::vector<uint8_t> buffer_;
  StreamWriter writer_;

  struct StringViewHash {
    size_t operator()(std::string_view s) const noexcept {
      return std::hash<std::string_view>{}(s);
    }
  };

  struct StringViewEq {
    bool operator()(std::string_view a, std::string_view b) const noexcept {
      return a == b;
    }
  };

  template <typename T, size_t K>
  struct SmallList {
    static constexpr size_t kInlineCapacity = K;
    T inline_items[K];
    size_t inline_count = 0;
    std::vector<T> overflow;

    void push_back(const T& value) {
      if (inline_count < K) {
        inline_items[inline_count++] = value;
      } else {
        overflow.push_back(value);
      }
    }

    size_t size() const { return inline_count + overflow.size(); }
    bool empty() const { return inline_count == 0 && overflow.empty(); }

    T& operator[](size_t i) {
      return i < inline_count ? inline_items[i] : overflow[i - inline_count];
    }

    const T& operator[](size_t i) const {
      return i < inline_count ? inline_items[i] : overflow[i - inline_count];
    }

    template <typename Fn>
    void ForEach(Fn&& fn) const {
      for (size_t i = 0; i < inline_count; ++i) fn(inline_items[i]);
      for (const auto& item : overflow) fn(item);
    }
  };

  using SmallFieldList = SmallList<CrateField, 8>;
  using SmallU32List = SmallList<uint32_t, 8>;

  // Table storage
  std::deque<std::string> token_storage_;
  std::vector<std::string_view> tokens_;
  FlatStringViewU32Map token_to_index_;
  struct CommonTokenIds {
    uint32_t active = 0;
    uint32_t apiSchemas = 0;
    uint32_t colorSpace = 0;
    uint32_t connectionPaths = 0;
    uint32_t custom = 0;
    uint32_t customData = 0;
    uint32_t default_value = 0;
    uint32_t doc = 0;
    uint32_t elementSize = 0;
    uint32_t hidden = 0;
    uint32_t instanceable = 0;
    uint32_t interpolation = 0;
    uint32_t kind = 0;
    uint32_t primChildren = 0;
    uint32_t properties = 0;
    uint32_t specifier = 0;
    uint32_t targetPaths = 0;
    uint32_t timeSamples = 0;
    uint32_t typeName = 0;
    uint32_t variability = 0;
    uint32_t variantSelection = 0;
  } common_;

  static constexpr size_t kTypeIdCount = static_cast<size_t>(TypeId::Count);
  mutable std::array<uint32_t, kTypeIdCount> scalar_type_token_cache_{};
  mutable std::array<uint32_t, kTypeIdCount> array_type_token_cache_{};

  // STRINGS: each entry is a token index
  std::deque<std::string> string_storage_;
  std::vector<uint32_t> string_indices_;
  FlatStringViewU32Map string_to_index_;

  // Paths, stored as canonical (stem, name) view pairs instead of owned
  // concatenated strings: `name` is the part after the LAST '.' when that dot
  // follows the last '/' (the property separator — exactly the split every
  // consumer of this table already applied to the full string), empty
  // otherwise. Stems/names VIEW stable storage: the input Layer's own path
  // strings and the global PropNameTable for spec paths, `path_owned_` for
  // strings we must copy (parsed arc targets, synthesized ancestors). This
  // removes the per-property concatenation + per-string allocation of the old
  // deque<std::string> (Island: 3.35M paths), and lets property interning
  // reuse one cached stem hash per prim.
  struct PathRec {
    std::string_view stem;
    std::string_view name;  // empty => the path is just `stem`
  };
  static void CanonicalSplitPath(std::string_view full, std::string_view* stem,
                                 std::string_view* name) {
    const size_t dot = full.rfind('.');
    if (dot != std::string_view::npos) {
      const size_t slash = full.rfind('/');
      if (slash == std::string_view::npos || dot > slash) {
        *stem = full.substr(0, dot);
        *name = full.substr(dot + 1);
        return;
      }
    }
    *stem = full;
    *name = std::string_view();
  }
  static uint64_t PathPairHash(uint64_t stem_hash, std::string_view name) {
    if (name.empty()) return stem_hash;
    const uint64_t hn = HashView(name);
    return stem_hash ^ (hn * 0x9E3779B97F4A7C15ull + (stem_hash << 6) +
                        (stem_hash >> 2));
  }
  // Materialized full path (stem [+ '.' + name]) for the rare consumers that
  // need a contiguous string (debug attribution, worker-merge re-intern).
  std::string PathFullString(uint32_t idx) const {
    std::string out;
    if (idx < paths_.size()) {
      const PathRec& r = paths_[idx];
      out.assign(r.stem.data(), r.stem.size());
      if (!r.name.empty()) {
        out.push_back('.');
        out.append(r.name.data(), r.name.size());
      }
    }
    return out;
  }

  // Open-addressed (stem, name) -> path index map. Same scheme as
  // FlatStringViewU32Map; equality is componentwise (canonical splits of equal
  // full paths are equal), with a data-pointer fast path for the repeated
  // same-prim probes of the property intern loop.
  struct FlatPathKeyMap {
    struct Entry {
      std::string_view stem;
      std::string_view name;
      uint32_t value = 0;
      bool used = false;
    };
    std::vector<Entry> slots;
    size_t count = 0;

    void reserve(size_t n) {
      size_t cap = 1;
      while (cap < n * 2 + 16) cap <<= 1;
      slots.assign(cap, {});
      count = 0;
    }
    void clear() {
      slots.clear();
      count = 0;
    }
    bool needs_rehash() const {
      if (slots.empty()) return false;
      return (count + 1) * 2 > slots.size();
    }
    void rehash() {
      const size_t old_cap = slots.size();
      std::vector<Entry> old_slots = std::move(slots);
      const size_t cap = (old_cap == 0) ? 16 : old_cap * 2;
      slots.assign(cap, {});
      count = 0;
      for (const auto& e : old_slots) {
        if (!e.used) continue;
        bool found = false;
        Entry* ne = probe(e.stem, e.name, PathPairHash(HashView(e.stem), e.name),
                          &found);
        ne->stem = e.stem;
        ne->name = e.name;
        ne->value = e.value;
        ne->used = true;
        ++count;
      }
    }
    static bool KeyEq(const Entry& e, std::string_view stem,
                      std::string_view name) {
      if (e.stem.size() != stem.size() || e.name.size() != name.size()) {
        return false;
      }
      if (e.stem.data() != stem.data() &&
          (!stem.empty() &&
           std::memcmp(e.stem.data(), stem.data(), stem.size()) != 0)) {
        return false;
      }
      if (e.name.empty()) return true;
      return e.name.data() == name.data() ||
             std::memcmp(e.name.data(), name.data(), name.size()) == 0;
    }
    // Single-probe find-or-prepare-insert (see FlatStringViewU32Map::probe).
    Entry* probe(std::string_view stem, std::string_view name, uint64_t hash,
                 bool* found) {
      if (slots.empty() || needs_rehash()) {
        if (slots.empty()) {
          reserve(16);
        } else {
          rehash();
        }
      }
      const size_t mask = slots.size() - 1;
      size_t h = static_cast<size_t>(hash) & mask;
      for (;;) {
        Entry& e = slots[h];
        if (!e.used) {
          *found = false;
          return &e;
        }
        if (KeyEq(e, stem, name)) {
          *found = true;
          return &e;
        }
        h = (h + 1) & mask;
      }
    }
    void commit(Entry* e, std::string_view stem, std::string_view name,
                uint32_t value) {
      e->stem = stem;
      e->name = name;
      e->value = value;
      e->used = true;
      ++count;
    }
    const uint32_t* find(std::string_view stem, std::string_view name,
                         uint64_t hash) const {
      if (slots.empty()) return nullptr;
      const size_t mask = slots.size() - 1;
      size_t h = static_cast<size_t>(hash) & mask;
      while (slots[h].used) {
        const Entry& e = slots[h];
        if (KeyEq(e, stem, name)) return &e.value;
        h = (h + 1) & mask;
      }
      return nullptr;
    }
  };

  std::deque<PathRec> paths_;
  std::deque<std::string> path_owned_;
  std::vector<uint32_t> ancestor_scan_path_indices_;
  FlatPathKeyMap path_to_index_;

  // Fields and fieldsets
  std::vector<CrateField> fields_;
  std::vector<uint32_t> fieldset_items_;
  std::vector<uint32_t> fieldset_offsets_;
  std::vector<uint32_t> fieldset_sizes_;
  std::vector<uint32_t> fieldset_extra_head_;
  std::vector<uint32_t> fieldset_extra_tail_;
  std::vector<uint32_t> fieldset_extra_count_;
  std::vector<uint32_t> fieldset_extra_values_;
  std::vector<uint32_t> fieldset_extra_next_;

  // Specs
  std::vector<CrateSpec> specs_;

  // VALUE data section payloads. A block is either inline (`data`) or a verbatim
  // pass-through reference into a retained source crate buffer (`src` pins the
  // buffer alive; bytes are copied to the output only in WriteValueSection, so
  // the large array payload is never staged in a second heap allocation).
  struct DataBlock {
    TypeId type;
    std::vector<uint8_t> data;
    std::shared_ptr<const CrateDataSource> src;  // null => inline `data`
    uint64_t src_offset = 0;
    uint64_t src_len = 0;
    uint64_t hash = 0;
    bool has_hash = false;
    DataBlock() : type(TypeId::Invalid) {}
    DataBlock(TypeId t, std::vector<uint8_t>&& bytes)
        : type(t), data(std::move(bytes)) {}
    bool is_ref() const { return static_cast<bool>(src); }
    size_t size() const { return is_ref() ? static_cast<size_t>(src_len) : data.size(); }
    const uint8_t* bytes() const {
      return is_ref() ? src->base() + src_offset : data.data();
    }
  };

  struct BlockDedupMap {
    static constexpr uint32_t kInvalid = uint32_t(~0u);

    struct Entry {
      uint64_t hash = 0;
      uint64_t block_index = 0;
      uint32_t next = kInvalid;
    };

    std::vector<uint32_t> buckets;
    std::vector<Entry> entries;

    void clear() {
      if (!buckets.empty()) {
        std::fill(buckets.begin(), buckets.end(), kInvalid);
      }
      entries.clear();
    }

    void reserve(size_t n) {
      size_t cap = 1;
      while (cap < n * 2 + 16) cap <<= 1;
      buckets.assign(cap, kInvalid);
      entries.clear();
      entries.reserve(n);
    }

    uint32_t Find(uint64_t hash, size_t n, const uint8_t* bytes,
                  const std::vector<DataBlock>& value_data) const {
      if (buckets.empty()) return kInvalid;
      const size_t mask = buckets.size() - 1;
      uint32_t i = static_cast<uint32_t>(hash & mask);
      for (uint32_t e = buckets[i]; e != kInvalid; e = entries[e].next) {
        const Entry& entry = entries[e];
        if (entry.hash != hash) continue;
        const DataBlock& ex = value_data[entry.block_index];
        if (ex.size() == n && std::memcmp(ex.bytes(), bytes, n) == 0) {
          return entry.block_index;
        }
      }
      return kInvalid;
    }

    void Insert(uint64_t hash, uint64_t block_index) {
      if (entries.size() >= static_cast<size_t>(kInvalid)) return;
      if (buckets.empty() || (entries.size() + 1) * 2 >= buckets.size()) {
        Rehash(std::max<size_t>(16, buckets.empty() ? 16 : buckets.size() * 2));
      }
      const size_t mask = buckets.size() - 1;
      const uint32_t slot = static_cast<uint32_t>(hash & mask);
      const uint32_t next = buckets[slot];
      const uint32_t idx = static_cast<uint32_t>(entries.size());
      entries.push_back({hash, block_index, next});
      buckets[slot] = idx;
    }

    void Rehash(size_t new_cap) {
      if (new_cap < 16) new_cap = 16;
      std::vector<Entry> old_entries;
      old_entries.swap(entries);
      buckets.assign(new_cap, kInvalid);
      entries.reserve(old_entries.size());
      for (const Entry& e : old_entries) {
        const size_t mask = buckets.size() - 1;
        const uint32_t slot = static_cast<uint32_t>(e.hash & mask);
        const uint32_t idx = static_cast<uint32_t>(entries.size());
        entries.push_back(e);
        entries[idx].next = buckets[slot];
        buckets[slot] = idx;
      }
    }
  };

  std::vector<DataBlock> value_data_;
  uint64_t value_start_offset_ = 0; // absolute file offset of VALUE section start
  std::vector<uint64_t> value_offsets_; // offset in VALUE section for each data block

  // Streaming-value mode: when a seekable sink is provided, each VALUE block is
  // written straight to the sink as it is built (8-byte aligned) and freed,
  // instead of being staged in value_data_. InternBlock() then returns the
  // block's ABSOLUTE file offset (not a value_data_ index), which becomes the
  // ValueRep payload directly, so ComputeValueLayoutAndPatch/StreamValueBytes
  // are skipped. Dedup is by content hash -> offset (no bytes retained). This
  // removes the multi-hundred-MB value_data_ peak on animation-dense scenes.
  bool stream_values_ = false;
  const CrateWriteSink* value_sink_ = nullptr;
  uint64_t value_stream_pos_ = 0;  // running absolute sink position of VALUE bytes
  bool value_stream_error_ = false;
  // hash -> absolute offset of the first block with that hash (content-address).
  // Keyed by (content hash, byte size) — the buffered dedup also requires
  // matching size before its memcmp, so folding size into the key rejects the
  // different-size hash collisions that hash-alone would wrongly share. (A
  // same-size, same-hash, different-bytes collision is astronomically unlikely
  // and would already perturb the buffered path's determinism.)
  struct StreamDedup {
    static constexpr uint32_t kInvalid = ~0u;
    struct Entry { uint64_t hash; uint64_t hash2; uint64_t size; uint64_t offset; uint32_t next; };
    // Entries live in fixed 64K-element segments instead of one doubling
    // vector: animation-dense scenes intern millions of tiny scalar blocks,
    // and a doubling vector re-copies the table and holds old+new buffers
    // live across each realloc (~1.5x transient on the largest write-phase
    // term). Segments keep entry indices stable, so bucket 'next' chains are
    // unaffected, and freeing is exact.
    static constexpr uint32_t kSegShift = 16;
    static constexpr uint32_t kSegSize = 1u << kSegShift;
    static constexpr uint32_t kSegMask = kSegSize - 1;
    std::vector<uint32_t> buckets;
    std::vector<std::unique_ptr<Entry[]>> segments;
    uint32_t entry_count = 0;
    Entry& entry(uint32_t i) { return segments[i >> kSegShift][i & kSegMask]; }
    uint64_t* find(uint64_t h, uint64_t h2, uint64_t sz) {
      if (buckets.empty()) return nullptr;
      const uint32_t mask = static_cast<uint32_t>(buckets.size() - 1);
      for (uint32_t e = buckets[h & mask]; e != kInvalid; e = entry(e).next)
        if (entry(e).hash == h && entry(e).hash2 == h2 && entry(e).size == sz)
          return &entry(e).offset;
      return nullptr;
    }
    void insert(uint64_t h, uint64_t h2, uint64_t sz, uint64_t off) {
      if (buckets.empty() || (entry_count + 1) * 2 >= buckets.size()) {
        const size_t nc = buckets.empty() ? 1024 : buckets.size() * 2;
        buckets.assign(nc, kInvalid);
        const uint32_t mask = static_cast<uint32_t>(nc - 1);
        for (uint32_t i = 0; i < entry_count; ++i) {
          Entry& en = entry(i);
          const uint32_t s = static_cast<uint32_t>(en.hash & mask);
          en.next = buckets[s]; buckets[s] = i;
        }
      }
      if ((entry_count & kSegMask) == 0) {
        segments.emplace_back(new Entry[kSegSize]);
      }
      const uint32_t mask = static_cast<uint32_t>(buckets.size() - 1);
      const uint32_t s = static_cast<uint32_t>(h & mask);
      const uint32_t i = entry_count++;
      entry(i) = Entry{h, h2, sz, off, buckets[s]};
      buckets[s] = i;
    }
  } stream_dedup_;

  using Clock = std::chrono::steady_clock;
  struct TimingState {
    bool enabled = false;
    double build_tables_ms = 0.0;
    double build_fields_ms = 0.0;
    double build_fields_layer_ms = 0.0;
    double build_fields_prims_ms = 0.0;
    double build_fields_root_children_ms = 0.0;
    double build_fields_child_scan_ms = 0.0;
    double build_fields_child_fill_ms = 0.0;
    double build_fields_child_emit_ms = 0.0;
    double paths_synth_ms = 0.0;
    double paths_sort_ms = 0.0;
    double paths_parent_ms = 0.0;
    double paths_sibling_ms = 0.0;
    double paths_emit_ms = 0.0;
    double paths_compress_ms = 0.0;
    double prereg_path_tokens_ms = 0.0;
    double value_layout_ms = 0.0;
    double value_emit_ms = 0.0;
    double tokens_ms = 0.0;
    double strings_ms = 0.0;
    double fields_ms = 0.0;
    double fieldsets_ms = 0.0;
    double paths_ms = 0.0;
    double specs_ms = 0.0;
    double toc_ms = 0.0;
    double stream_ms = 0.0;
  } timing_;

  static double MsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  }

  void PrintTiming() const {
    if (!timing_.enabled) return;
    std::fprintf(stderr,
        "[next_crate_write] build_tables=%.1fms build_fields=%.1fms "
        "path_tokens=%.1fms value_layout=%.1fms value_emit=%.1fms\n",
        timing_.build_tables_ms, timing_.build_fields_ms,
        timing_.prereg_path_tokens_ms, timing_.value_layout_ms,
        timing_.value_emit_ms);
    std::fprintf(stderr,
        "[next_crate_write] build_fields_sub layer=%.1fms prims=%.1fms "
        "root_children=%.1fms child_scan=%.1fms child_fill=%.1fms "
        "child_emit=%.1fms\n",
        timing_.build_fields_layer_ms, timing_.build_fields_prims_ms,
        timing_.build_fields_root_children_ms, timing_.build_fields_child_scan_ms,
        timing_.build_fields_child_fill_ms, timing_.build_fields_child_emit_ms);
    std::fprintf(stderr,
        "[next_crate_write] paths_sub synth=%.1fms sort=%.1fms parent=%.1fms "
        "sibling=%.1fms emit=%.1fms compress=%.1fms\n",
        timing_.paths_synth_ms, timing_.paths_sort_ms, timing_.paths_parent_ms,
        timing_.paths_sibling_ms, timing_.paths_emit_ms,
        timing_.paths_compress_ms);
    std::fprintf(stderr,
        "[next_crate_write] sections tokens=%.1fms strings=%.1fms fields=%.1fms "
        "fieldsets=%.1fms paths=%.1fms specs=%.1fms toc=%.1fms stream=%.1fms\n",
        timing_.tokens_ms, timing_.strings_ms, timing_.fields_ms,
        timing_.fieldsets_ms, timing_.paths_ms, timing_.specs_ms,
        timing_.toc_ms, timing_.stream_ms);
    std::fprintf(stderr,
        "[next_crate_write] counts tokens=%zu strings=%zu paths=%zu specs=%zu "
        "fields=%zu fieldsets=%zu value_blocks=%zu "
        "deduped=%zu passthrough=%zu reencoded=%zu\n",
        tokens_.size(), string_indices_.size(), paths_.size(), specs_.size(),
        fields_.size(), FieldsetCount(), value_data_.size(),
        blocks_deduped_, arrays_passed_through_,
        arrays_reencoded_);
  }

  size_t FieldsetCount() const { return fieldset_offsets_.size(); }

  uint32_t CommonToken(uint32_t& cached, const char* token) {
    if (cached != 0) return cached;
    cached = InternToken(token);
    return cached;
  }

  bool HasFieldsets() const { return !fieldset_offsets_.empty(); }

  uint32_t AddFieldset(std::vector<uint32_t>&& items) {
    uint32_t idx = static_cast<uint32_t>(fieldset_offsets_.size());
    fieldset_offsets_.push_back(static_cast<uint32_t>(fieldset_items_.size()));
    fieldset_sizes_.push_back(static_cast<uint32_t>(items.size()));
    fieldset_extra_head_.push_back(0xFFFFFFFFu);
    fieldset_extra_tail_.push_back(0xFFFFFFFFu);
    fieldset_extra_count_.push_back(0);
    fieldset_items_.insert(fieldset_items_.end(), items.begin(), items.end());
    return idx;
  }

  uint32_t AddFieldset(const SmallU32List& items) {
    uint32_t idx = static_cast<uint32_t>(fieldset_offsets_.size());
    fieldset_offsets_.push_back(static_cast<uint32_t>(fieldset_items_.size()));
    fieldset_sizes_.push_back(static_cast<uint32_t>(items.size()));
    fieldset_extra_head_.push_back(0xFFFFFFFFu);
    fieldset_extra_tail_.push_back(0xFFFFFFFFu);
    fieldset_extra_count_.push_back(0);
    items.ForEach([&](uint32_t field_idx) {
      fieldset_items_.push_back(field_idx);
    });
    return idx;
  }

  void AppendFieldsetItem(uint32_t fieldset_idx, uint32_t field_idx) {
    if (fieldset_idx >= FieldsetCount()) return;
    uint32_t node = static_cast<uint32_t>(fieldset_extra_values_.size());
    fieldset_extra_values_.push_back(field_idx);
    fieldset_extra_next_.push_back(0xFFFFFFFFu);
    if (fieldset_extra_tail_[fieldset_idx] != 0xFFFFFFFFu) {
      fieldset_extra_next_[fieldset_extra_tail_[fieldset_idx]] = node;
    } else {
      fieldset_extra_head_[fieldset_idx] = node;
    }
    fieldset_extra_tail_[fieldset_idx] = node;
    fieldset_extra_count_[fieldset_idx]++;
  }

  uint32_t FieldsetSize(uint32_t fieldset_idx) const {
    if (fieldset_idx >= FieldsetCount()) return 0;
    return fieldset_sizes_[fieldset_idx] + fieldset_extra_count_[fieldset_idx];
  }

  template <typename Fn>
  void ForEachFieldsetItem(uint32_t fieldset_idx, Fn&& fn) const {
    if (fieldset_idx >= FieldsetCount()) return;
    uint32_t offset = fieldset_offsets_[fieldset_idx];
    uint32_t count = fieldset_sizes_[fieldset_idx];
    for (uint32_t i = 0; i < count; ++i) {
      fn(fieldset_items_[offset + i]);
    }
    for (uint32_t node = fieldset_extra_head_[fieldset_idx];
         node != 0xFFFFFFFFu; node = fieldset_extra_next_[node]) {
      fn(fieldset_extra_values_[node]);
    }
  }

  void CacheBlockHash(DataBlock* block) {
    if (!block || block->has_hash) return;
    block->hash = HashBlockBytes(block->bytes(), block->size());
    block->has_hash = true;
  }

  static bool ViewEqual(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
  }

  static uint64_t HashView(std::string_view v) {
    return XXH3_64bits(v.data(), v.size());
  }

  void ResetWorkerState() {
    result_ = CrateWriteResult();
    buffer_.clear();
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
    paths_.clear();
    path_owned_.clear();
    ancestor_scan_path_indices_.clear();
    path_to_index_.clear();
    value_data_.clear();
    value_offsets_.clear();
    block_dedup_.clear();
    blocks_deduped_ = 0;
    arrays_passed_through_ = 0;
    arrays_reencoded_ = 0;
    scalar_type_token_cache_.fill(0);
    array_type_token_cache_.fill(0);
    value_data_.push_back({TypeId::Invalid, std::vector<uint8_t>()});
  }

  // Recursively rewrite a local ValueRep's block indices/paths into this (the
  // merge target) writer's global tables. C-style: three plain block helpers +
  // this dispatcher, mutually recursive by ordinary member calls (was a nest of
  // Y-combinator `auto&&`-recursive generic lambdas -> template instantiation).
  ValueRep RemapLocalValueRep(Impl& local, ValueRep v,
                              std::vector<uint64_t>& local_to_global) {
    if (v.is_inlined()) return v;
    const uint64_t payload = v.payload();
    if (payload == 0) return v;

    uint64_t global_idx = 0;
    if (v.type_id() == CrateTypeId::TimeSamples) {
      global_idx = RemapTimeSamplesBlock(local, payload, local_to_global);
    } else if (v.type_id() == CrateTypeId::PathListOp) {
      global_idx = RemapPathListBlock(local, payload, local_to_global);
    } else {
      global_idx = RemapValueBlock(local, payload, local_to_global);
    }
    return ValueRep::Make(v.type_id(), global_idx, v.is_array(), false,
                          v.is_compressed());
  }

  // TimeSamples indirection block: remap the header value-rep + each sample rep.
  uint64_t RemapTimeSamplesBlock(Impl& local, uint64_t local_idx,
                                 std::vector<uint64_t>& local_to_global) {
    if (local_idx == 0) return uint64_t(0);
    if (local_idx < local_to_global.size() &&
        local_to_global[local_idx] != UINT64_MAX) {
      return local_to_global[local_idx];
    }
    if (local_idx >= local.value_data_.size()) return uint64_t(0);
    DataBlock block = std::move(local.value_data_[local_idx]);
    block.has_hash = false;
    if (block.data.size() >= 32) {
      uint64_t raw = 0;
      std::memcpy(&raw, block.data.data() + 8, 8);
      raw = RemapLocalValueRep(local, ValueRep(raw), local_to_global).raw();
      std::memcpy(block.data.data() + 8, &raw, 8);

      uint64_t count = 0;
      std::memcpy(&count, block.data.data() + 24, 8);
      const size_t max_count = (block.data.size() - 32) / 8;
      if (count > max_count) count = max_count;
      for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
        const size_t off = 32 + i * 8;
        std::memcpy(&raw, block.data.data() + off, 8);
        raw = RemapLocalValueRep(local, ValueRep(raw), local_to_global).raw();
        std::memcpy(block.data.data() + off, &raw, 8);
      }
    }
    CacheBlockHash(&block);
    uint64_t global_idx = InternBlock(std::move(block));
    if (local_idx < local_to_global.size()) local_to_global[local_idx] = global_idx;
    return global_idx;
  }

  // PathListOp block: remap each embedded local path index into a global one.
  uint64_t RemapPathListBlock(Impl& local, uint64_t local_idx,
                              std::vector<uint64_t>& local_to_global) {
    if (local_idx == 0) return uint64_t(0);
    if (local_idx < local_to_global.size() &&
        local_to_global[local_idx] != UINT64_MAX) {
      return local_to_global[local_idx];
    }
    if (local_idx >= local.value_data_.size()) return uint64_t(0);
    DataBlock block = std::move(local.value_data_[local_idx]);
    block.has_hash = false;
    if (block.data.size() >= 9) {
      uint64_t count = 0;
      std::memcpy(&count, block.data.data() + 1, 8);
      const size_t max_count = (block.data.size() - 9) / 4;
      if (count > max_count) count = max_count;
      for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
        uint32_t local_path_idx = 0;
        std::memcpy(&local_path_idx, block.data.data() + 9 + i * 4, 4);
        uint32_t global_path_idx = local_path_idx;
        if (!local.frozen_path_ && local_path_idx < local.paths_.size()) {
          global_path_idx =
              InternPathCopy(local.PathFullString(local_path_idx));
        }
        std::memcpy(block.data.data() + 9 + i * 4, &global_path_idx, 4);
      }
    }
    CacheBlockHash(&block);
    uint64_t global_idx = InternBlock(std::move(block));
    if (local_idx < local_to_global.size()) local_to_global[local_idx] = global_idx;
    return global_idx;
  }

  // Plain value block: only Dictionary blocks embed nested value-reps to remap.
  uint64_t RemapValueBlock(Impl& local, uint64_t local_idx,
                           std::vector<uint64_t>& local_to_global) {
    if (local_idx == 0) return uint64_t(0);
    if (local_idx < local_to_global.size() &&
        local_to_global[local_idx] != UINT64_MAX) {
      return local_to_global[local_idx];
    }
    if (local_idx >= local.value_data_.size()) return uint64_t(0);

    DataBlock block = std::move(local.value_data_[local_idx]);
    if (block.type == TypeId::Dictionary && block.data.size() >= 8) {
      block.has_hash = false;
      uint64_t count = 0;
      std::memcpy(&count, block.data.data(), 8);
      const size_t max_count = (block.data.size() - 8) / 20;
      if (count > max_count) count = max_count;
      for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
        const size_t off = 8 + i * 20 + 12;
        uint64_t raw = 0;
        std::memcpy(&raw, block.data.data() + off, 8);
        raw = RemapLocalValueRep(local, ValueRep(raw), local_to_global).raw();
        std::memcpy(block.data.data() + off, &raw, 8);
      }
      CacheBlockHash(&block);
    }

    uint64_t global_idx = InternBlock(std::move(block));
    if (local_idx < local_to_global.size()) local_to_global[local_idx] = global_idx;
    return global_idx;
  }

  // Append `block` to value_data_ unless an existing block has identical bytes;
  // returns the (shared or new) block index. Must NOT be used for blocks that
  // are patched in place after creation (the TimeSamples indirection header):
  // sharing one would apply the index->offset patch twice.
  // Stream a value block's bytes to the seekable sink at the next 8-byte-aligned
  // position and return its ABSOLUTE file offset. Padding + per-block alignment
  // exactly mirror the buffered layout (ComputeValueLayoutAndPatch + Stream
  // ValueBytes) so the on-disk value section is byte-identical.
  uint64_t StreamRawBlock(const uint8_t* bytes, size_t n) {
    static const uint8_t kZeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const uint64_t pad = (8 - (value_stream_pos_ % 8)) % 8;
    if (pad) {
      if (!(*value_sink_)(kZeros, static_cast<size_t>(pad))) value_stream_error_ = true;
      value_stream_pos_ += pad;
    }
    const uint64_t off = value_stream_pos_;
    if (n) {
      if (!(*value_sink_)(bytes, n)) value_stream_error_ = true;
      value_stream_pos_ += n;
    }
    return off;
  }

  // Append a value block WITHOUT content dedup (matches the direct
  // value_data_.push_back sites: the TimeSamples indirection header and
  // Dictionary blocks, which are patched in place and never shared). Returns the
  // block's value_data_ index (buffered) or absolute file offset (streaming).
  uint64_t PushValueBlock(DataBlock&& block) {
    if (stream_values_) {
      if (block.size() == 0) return 0;
      return StreamRawBlock(block.bytes(), block.size());
    }
    uint64_t idx = value_data_.size();
    value_data_.push_back(std::move(block));
    return idx;
  }

  uint64_t InternBlock(DataBlock&& block) {
    const size_t n = block.size();
    if (stream_values_) {
      // Empty block => payload 0 (the reserved empty-array marker).
      if (n == 0) return 0;
      const uint8_t* p = block.bytes();
      // Content-address by a FULL-content 128-bit hash (two independent 64-bit
      // digests) + size. block.hash / HashBlockBytes only SAMPLES large blocks
      // (first/middle/last 256 B) — safe for the buffered path because it
      // memcmp-verifies a hash hit, but here there are no retained bytes to
      // compare, so a sampled hash would falsely share two large blocks that
      // share their sampled regions. A full 128-bit digest makes a false share
      // astronomically impossible.
      const uint64_t h = XXH3_64bits(p, n);
      const uint64_t h2 = XXH3_64bits_withSeed(p, n, 0x9E3779B97F4A7C15ull);
      if (uint64_t* off = stream_dedup_.find(h, h2, n)) {
        ++blocks_deduped_;
        return *off;
      }
      const uint64_t off = StreamRawBlock(p, n);
      stream_dedup_.insert(h, h2, n, off);
      return off;
    }
    if (n == 0) {
      // Never share the empty block: payload 0 is the reserved empty-array
      // marker and a zero-length block at a nonzero offset has no meaning.
      uint64_t idx = value_data_.size();
      value_data_.push_back(std::move(block));
      return idx;
    }
    const uint8_t* p = block.bytes();
    const uint64_t h = block.has_hash ? block.hash : HashBlockBytes(p, n);
    const uint32_t duplicate = block_dedup_.Find(h, n, p, value_data_);
    if (duplicate != BlockDedupMap::kInvalid) {
      ++blocks_deduped_;
      return duplicate;
    }
    uint64_t idx = value_data_.size();
    value_data_.push_back(std::move(block));
    block_dedup_.Insert(h, idx);
    return idx;
  }

  // Lazy-array write accounting.
  size_t arrays_passed_through_ = 0;
  size_t arrays_reencoded_ = 0;

  // Parallel build: when these point at the main builder's COMPLETED token/string
  // tables, InternToken/InternString do a read-only lookup (workers never mutate
  // the shared tables, so blocks/fields they emit already carry GLOBAL indices).
  // A miss means the serial pre-pass was incomplete; the worker sets `frozen_miss_`
  // and the driver discards the parallel result and writes serially (safe).
  const FlatStringViewU32Map*
      frozen_tok_ = nullptr;
  const FlatStringViewU32Map*
      frozen_str_ = nullptr;
  const FlatPathKeyMap* frozen_path_ = nullptr;
  bool frozen_miss_ = false;

  // Cross-spec block dedup: content hash -> block indices with that hash.
  // Flattened composition duplicates referenced array payloads per instance;
  // content-addressing the VALUE blocks writes each distinct payload once.
  BlockDedupMap block_dedup_;
  size_t blocks_deduped_ = 0;

  static uint64_t HashBlockBytes(const uint8_t* p, size_t n) {
    uint64_t h = XXH3_64bits(&n, sizeof(n));
    if (n == 0) return h;
    if (n <= 512) {
      return XXH3_64bits_withSeed(p, n, h);
    }
    h = XXH3_64bits_withSeed(p, 256, h);
    h = XXH3_64bits_withSeed(p + n / 2 - 128, 256, h);
    return XXH3_64bits_withSeed(p + n - 256, 256, h);
  }


  // Effective worker count: 1 = serial; <=0 = auto (hardware_concurrency capped
  // at 32); >1 = as requested. Always 1 in a non-threaded build.
  int ResolveThreads() const {
#if defined(TINYUSDZ_ENABLE_THREAD)
    const int req = options_.num_threads;
    if (req == 1) return 1;
    if (req > 1) return req;
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    if (hw < 1) hw = 1;
    return std::min(hw, 32);
#else
    return 1;
#endif
  }

  // Sort `v` with `cmp` across `nthreads` workers: sort P contiguous chunks in
  // parallel, then pairwise-merge the sorted runs (log P rounds, parallel within
  // a round). Falls back to std::sort when serial or small. Result is identical
  // to std::sort(v, cmp) for a strict-weak-ordering `cmp` with unique elements.
  template <class Cmp>
  static void ParallelSortIndices(std::vector<uint32_t>& v, Cmp cmp,
                                  int nthreads) {
#if defined(TINYUSDZ_ENABLE_THREAD)
    const size_t N = v.size();
    if (nthreads <= 1 || N < 200000) {
      std::sort(v.begin(), v.end(), cmp);
      return;
    }
    int P = 1;
    while (P * 2 <= nthreads && P * 2 <= 32 && (N / (P * 2)) >= 50000) P *= 2;
    if (P <= 1) { std::sort(v.begin(), v.end(), cmp); return; }
    std::vector<size_t> bnd(P + 1);
    for (int i = 0; i <= P; ++i) bnd[i] = N * static_cast<size_t>(i) / P;
    {
      std::vector<std::thread> pool;
      pool.reserve(P);
      for (int t = 0; t < P; ++t) {
        pool.emplace_back([&, t] {
          std::sort(v.begin() + bnd[t], v.begin() + bnd[t + 1], cmp);
        });
      }
      for (auto& th : pool) th.join();
    }
    for (int step = 1; step < P; step *= 2) {
      std::vector<std::thread> pool;
      for (int t = 0; t + step < P; t += 2 * step) {
        const size_t lo = bnd[t];
        const size_t mid = bnd[t + step];
        const size_t hi = bnd[std::min(t + 2 * step, P)];
        pool.emplace_back([&v, &cmp, lo, mid, hi] {
          std::inplace_merge(v.begin() + lo, v.begin() + mid, v.begin() + hi, cmp);
        });
      }
      for (auto& th : pool) th.join();
    }
#else
    (void)nthreads;
    std::sort(v.begin(), v.end(), cmp);
#endif
  }

  #include "crate-writer-tables.inc"

  #include "crate-writer-passthrough.inc"

  #include "crate-writer-values.inc"

  #include "crate-writer-properties.inc"

  #include "crate-writer-fields.inc"

  #include "crate-writer-sections.inc"

};

// ============================================================
// CrateWriter public methods
// ============================================================
