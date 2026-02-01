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

  /// Maximum recursion depth for path decoding
  size_t max_path_depth = 256;

  /// Maximum memory budget (bytes, 0 = unlimited)
  size_t max_memory = 0;
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

  /// Read from memory buffer
  CrateReadResult Read(const uint8_t* data, size_t size);

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
