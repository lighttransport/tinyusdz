// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate data source (retained USDC buffer)
//
// Owns the raw bytes of one loaded crate (USDC) plus the structural tables
// needed to interpret ValueReps lazily (tokens / string indices). One per
// loaded layer; held via shared_ptr so lazy array values can keep it alive for
// exactly as long as some surviving opinion references into it.

#pragma once

#include "crate-format.hh"      // CrateVersion, ValueRep, CrateTypeId
#include "../types/type-id.hh"  // next::TypeId

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

class Value;
struct LazyArrayRef;

class CrateDataSource {
 public:
  /// Adopt the crate bytes by move (no copy) together with the decoded
  /// tokens / string-index tables.
  static std::shared_ptr<CrateDataSource> Adopt(
      std::string&& bytes, CrateVersion version, std::vector<std::string>&& tokens,
      std::vector<uint32_t>&& string_indices);

  /// Adopt just the byte buffer (tables empty). Useful for callers that only
  /// need verbatim block access (e.g. write-time pass-through).
  static std::shared_ptr<CrateDataSource> Adopt(std::string&& bytes,
                                                CrateVersion version);

  const uint8_t* base() const {
    return reinterpret_cast<const uint8_t*>(bytes_.data());
  }
  size_t size() const { return bytes_.size(); }
  CrateVersion version() const { return version_; }

  /// Set the crate version once it has been parsed from the bootstrap header.
  /// (The buffer is adopted before the header is read.)
  void set_version(CrateVersion v) { version_ = v; }

  /// Install the decoded token / string-index tables (used for token-array
  /// materialization and write-time index remapping).
  void set_tables(std::vector<std::string> tokens,
                  std::vector<uint32_t> string_indices) {
    tokens_ = std::move(tokens);
    string_indices_ = std::move(string_indices);
  }
  const std::vector<std::string>& tokens() const { return tokens_; }
  const std::vector<uint32_t>& string_indices() const { return string_indices_; }

  /// Decode a lazy array reference into a concrete Value. Returns false (and
  /// leaves `*out` empty) for array types that have no concrete Value storage
  /// yet, or on a malformed block.
  bool MaterializeArray(const LazyArrayRef& ref, Value* out) const;

 private:
  CrateDataSource() = default;

  std::string bytes_;       // the retained crate (moved in; never reallocated)
  CrateVersion version_{};  // value-initialized to 0.0.0
  std::vector<std::string> tokens_;
  std::vector<uint32_t> string_indices_;
};

/// Shared array-block decoder — the single source of truth used by both the
/// crate reader (token/string arrays) and Value::materialize(). `base`/`size`
/// span a retained crate buffer; `rep` must have its array bit set.
bool DecodeCrateArray(const uint8_t* base, size_t size, ValueRep rep,
                      const std::vector<std::string>& tokens, size_t max_elements,
                      Value* out);

/// Bytes per element of an array CrateTypeId as stored on disk (0 if unknown).
uint32_t CrateArrayElemStride(CrateTypeId id);

/// next::TypeId that materialize() would surface for an array CrateTypeId.
TypeId CrateArrayValueType(CrateTypeId id);

}  // namespace next
}  // namespace tinyusdz
