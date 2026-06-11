// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader
// Binary USD file parser

#pragma once

#include "crate-format.hh"
#include "../stage/stage.hh"
#include <string>
#include <vector>
#include <memory>

namespace tinyusdz {
namespace next {

/// Options for reading crate files
struct CrateReadOptions {
  /// Maximum number of tokens allowed
  size_t max_tokens = 1024 * 1024;

  /// Maximum number of strings allowed
  size_t max_strings = 1024 * 1024;

  /// Maximum number of fields allowed
  size_t max_fields = 10 * 1024 * 1024;

  /// Maximum number of specs allowed
  size_t max_specs = 10 * 1024 * 1024;

  /// Maximum number of paths allowed
  size_t max_paths = 10 * 1024 * 1024;

  /// Maximum number of elements in a single value array
  /// (mirrors pxrUSD/legacy core's 1<<30 cap; guards against a malformed
  /// count triggering an enormous allocation).
  size_t max_array_elements = 1024 * 1024 * 1024;

  /// Maximum recursion depth for path decoding
  size_t max_path_depth = 256;

  /// Maximum memory budget (bytes, 0 = unlimited)
  size_t max_memory = 0;

  /// Keep numeric POD arrays as lazy references into the retained source buffer
  /// instead of eagerly decoding them (low-memory load/compose/write path).
  /// Set false to force eager decode (e.g. for A/B memory comparisons).
  bool lazy_arrays = true;

  /// When reading from a file path, memory-map the crate read-only instead of
  /// copying it into an owned heap buffer (Phase 8.3). Falls back to the owned
  /// path automatically when mmap is unavailable (non-posix / WASM) or the
  /// mapping fails. The mapping stays alive as long as any lazy value (or the
  /// reader) references it. Set false to force the owned-buffer path.
  bool use_mmap = true;
};

/// Error from crate reading
struct CrateError {
  size_t offset = 0;    // File offset where error occurred
  std::string message;
};

/// Result of reading a crate file
struct CrateReadResult {
  bool success = false;
  Stage stage;
  std::vector<CrateError> errors;
  std::vector<std::string> warnings;
  CrateVersion version;
};

/// USDC Crate file reader
class CrateReader {
public:
  explicit CrateReader(const CrateReadOptions& options = {});
  ~CrateReader();

  /// No copy
  CrateReader(const CrateReader&) = delete;
  CrateReader& operator=(const CrateReader&) = delete;

  /// Move allowed
  CrateReader(CrateReader&&) noexcept;
  CrateReader& operator=(CrateReader&&) noexcept;

  /// Read from memory buffer (copies the input into a retained buffer).
  CrateReadResult Read(const uint8_t* data, size_t size);

  /// Read from an owned buffer, adopted by move (single in-heap copy). Prefer
  /// this on memory-constrained targets (e.g. WASM) when the caller can give up
  /// ownership of the input bytes.
  CrateReadResult ReadOwned(std::string&& owned);

  /// Read from file
  CrateReadResult ReadFile(const char* filename);

  // ============================================================
  // Low-level access (for debugging/testing)
  // ============================================================

  /// Get tokens table
  const std::vector<std::string>& tokens() const;

  /// Get paths table
  const std::vector<std::string>& paths() const;

  /// Get fields table
  const std::vector<CrateField>& fields() const;

  /// Get specs table
  const std::vector<CrateSpec>& specs() const;

  /// Get fieldset-indices table (flattened; 0xFFFFFFFF terminates each set)
  const std::vector<uint32_t>& fieldset_indices() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/// Check if data looks like a USDC file (check magic bytes)
bool IsUSDCData(const uint8_t* data, size_t size);

/// Check if file is a USDC file
bool IsUSDCFile(const char* filename);

}  // namespace next
}  // namespace tinyusdz
