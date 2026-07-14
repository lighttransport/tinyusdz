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
  bool strict_aousd_conformance() const {
    return options_.strict_aousd_conformance;
  }

private:
  CrateWriteOptions options_;
  CrateWriteResult result_;
  std::vector<uint8_t> buffer_;
  StreamWriter writer_;

  // Table storage
  std::vector<std::string> tokens_;
  std::unordered_map<std::string, uint32_t> token_to_index_;

  // STRINGS: each entry is a token index
  std::vector<uint32_t> string_indices_;
  std::unordered_map<std::string, uint32_t> string_to_index_;

  // Paths
  std::vector<std::string> paths_;
  std::unordered_map<std::string, uint32_t> path_to_index_;

  // Fields and fieldsets
  std::vector<CrateField> fields_;
  std::vector<std::vector<uint32_t>> fieldsets_;

  // Specs
  std::vector<CrateSpec> specs_;

  // VALUE data section payloads. A block is either inline (`data`) or a verbatim
  // pass-through reference into a retained source crate buffer (`src` pins the
  // buffer alive; bytes are copied to the output only in WriteValueSection, so
  // the large array payload is never staged in a second heap allocation).
  struct DataBlock {
    TypeId type;
    std::vector<uint8_t> data;
    std::shared_ptr<const LazyArraySource> src;  // null => inline `data`
    uint64_t src_offset = 0;
    uint64_t src_len = 0;
    // Byte offsets of recursively-written ValueReps embedded in this block.
    // These payloads start as value_data_ indices and are relocated after the
    // VALUE layout is known (used by spline per-knot customData maps).
    std::vector<size_t> embedded_value_rep_offsets;
    DataBlock() : type(TypeId::Invalid) {}
    DataBlock(TypeId t, std::vector<uint8_t>&& bytes)
        : type(t), data(std::move(bytes)) {}
    bool is_ref() const { return static_cast<bool>(src); }
    size_t size() const { return is_ref() ? static_cast<size_t>(src_len) : data.size(); }
    const uint8_t* bytes() const {
      return is_ref() ? src->base() + src_offset : data.data();
    }
  };
  std::vector<DataBlock> value_data_;
  uint64_t value_start_offset_ = 0; // absolute file offset of VALUE section start
  std::vector<uint64_t> value_offsets_; // offset in VALUE section for each data block

  // Append `block` to value_data_ unless an existing block has identical bytes;
  // returns the (shared or new) block index. Must NOT be used for blocks that
  // are patched in place after creation (the TimeSamples indirection header):
  // sharing one would apply the index->offset patch twice.
  uint64_t InternBlock(DataBlock&& block) {
    const size_t n = block.size();
    if (n == 0) {
      // Never share the empty block: payload 0 is the reserved empty-array
      // marker and a zero-length block at a nonzero offset has no meaning.
      uint64_t idx = value_data_.size();
      value_data_.push_back(std::move(block));
      return idx;
    }
    const uint8_t* p = block.bytes();
    const uint64_t h = HashBlockBytes(p, n);
    auto range = block_dedup_.equal_range(h);
    for (auto it = range.first; it != range.second; ++it) {
      const DataBlock& ex = value_data_[it->second];
      if (ex.size() == n && std::memcmp(ex.bytes(), p, n) == 0) {
        ++blocks_deduped_;
        return it->second;
      }
    }
    uint64_t idx = value_data_.size();
    value_data_.push_back(std::move(block));
    block_dedup_.emplace(h, idx);
    return idx;
  }

  // Lazy-array write accounting.
  size_t arrays_passed_through_ = 0;
  // Highest crate minor version the authored content REQUIRES (pxr-style
  // write-version upgrade): timecode values need 0.9, relocates 0.11.
  // WriteBootstrap stamps max(options version, this).
  uint8_t required_version_minor_ = 0;
  size_t arrays_reencoded_ = 0;

  // Parallel build: when these point at the main builder's COMPLETED token/string
  // tables, InternToken/InternString do a read-only lookup (workers never mutate
  // the shared tables, so blocks/fields they emit already carry GLOBAL indices).
  // A miss means the serial pre-pass was incomplete; the worker sets `frozen_miss_`
  // and the driver discards the parallel result and writes serially (safe).
  const std::unordered_map<std::string, uint32_t>* frozen_tok_ = nullptr;
  const std::unordered_map<std::string, uint32_t>* frozen_str_ = nullptr;
  bool frozen_miss_ = false;

  // Cross-spec block dedup: content hash -> block indices with that hash.
  // Flattened composition duplicates referenced array payloads per instance;
  // content-addressing the VALUE blocks writes each distinct payload once.
  std::unordered_multimap<uint64_t, uint64_t> block_dedup_;
  size_t blocks_deduped_ = 0;

  static uint64_t HashBlockBytes(const uint8_t* p, size_t n) {
    // FNV-1a over (length, head, tail) — cheap on multi-MB blocks; candidates
    // are confirmed with a full memcmp, so collisions only cost compares.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const uint8_t* q, size_t m) {
      for (size_t i = 0; i < m; ++i) { h ^= q[i]; h *= 1099511628211ull; }
    };
    mix(reinterpret_cast<const uint8_t*>(&n), sizeof(n));
    if (n <= 512) {
      mix(p, n);
    } else {
      mix(p, 256);
      mix(p + n / 2 - 128, 256);
      mix(p + n - 256, 256);
    }
    return h;
  }


  // Effective worker count: 1 = serial; <=0 = auto (hardware_concurrency capped
  // at 16); >1 = as requested. Always 1 in a non-threaded build.
  int ResolveThreads() const {
#if defined(TINYUSDZ_ENABLE_THREAD)
    const int req = options_.num_threads;
    if (req == 1) return 1;
    if (req > 1) return req;
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    if (hw < 1) hw = 1;
    return std::min(hw, 16);
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
    while (P * 2 <= nthreads && P * 2 <= 16 && (N / (P * 2)) >= 50000) P *= 2;
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
