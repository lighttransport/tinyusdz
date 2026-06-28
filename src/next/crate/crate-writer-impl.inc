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
    std::shared_ptr<const CrateDataSource> src;  // null => inline `data`
    uint64_t src_offset = 0;
    uint64_t src_len = 0;
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
  size_t arrays_reencoded_ = 0;

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
