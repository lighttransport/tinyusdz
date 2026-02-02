// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII Parser
// High-level parser for USD ASCII format files

#pragma once

#include "../stage/stage.hh"
#include "../memory/memory-pool.hh"
#include <string>
#include <vector>
#include <memory>

namespace tinyusdz {
namespace next {

/// Options for parsing USDA files
struct ParseOptions {
  /// Allow unknown/unrecognized prim types (stored as generic prims)
  bool allow_unknown_types = true;

  /// Maximum file size to parse (0 = no limit)
  size_t max_file_size = 0;

  /// Maximum nesting depth for prims
  size_t max_depth = 256;

  /// Maximum memory budget in megabytes (0 = unlimited)
  /// This is an advisory limit - actual memory usage may slightly exceed this
  /// due to overhead and pre-allocated buffers. Default is 16GB.
  uint32_t max_memory_limit_in_mb = 16384;

  /// Maximum number of prims allowed (0 = no limit)
  /// Helps prevent denial-of-service from files with millions of prims
  size_t max_prim_count = 0;

  /// Maximum number of properties per prim (0 = no limit)
  size_t max_properties_per_prim = 0;

  /// Maximum number of time samples per property (0 = no limit)
  size_t max_time_samples = 0;

  /// Maximum array element count (0 = no limit)
  /// Prevents allocation of huge arrays from malicious files
  size_t max_array_elements = 0;

  /// Optional memory pool for allocations
  /// If not set (nullptr), uses standard heap allocation
  /// The pool must outlive all Values created by the parser
  MemoryPool* memory_pool = nullptr;
};

/// Error information from parsing
struct ParseError {
  size_t line = 0;
  size_t column = 0;
  std::string message;
};

/// USDA ASCII Parser
/// Parses USD ASCII format files into a Stage
class AsciiParser {
public:
  /// Construct with options
  explicit AsciiParser(const ParseOptions& options = {});

  /// Destructor
  ~AsciiParser();

  /// No copy
  AsciiParser(const AsciiParser&) = delete;
  AsciiParser& operator=(const AsciiParser&) = delete;

  /// Move is allowed
  AsciiParser(AsciiParser&&) noexcept;
  AsciiParser& operator=(AsciiParser&&) noexcept;

  // ============================================================
  // Parsing
  // ============================================================

  /// Parse from string data
  bool Parse(const char* data, size_t length);

  /// Parse from a file
  bool ParseFile(const char* filename);

  // ============================================================
  // Results
  // ============================================================

  /// Get the parsed stage (moves ownership to caller)
  Stage TakeStage();

  /// Get parse errors
  const std::vector<ParseError>& GetErrors() const;

  /// Check if parsing succeeded
  bool HasErrors() const;

  /// Get warning messages
  const std::vector<std::string>& GetWarnings() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace next
}  // namespace tinyusdz
