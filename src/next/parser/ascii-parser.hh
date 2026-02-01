// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII Parser
// High-level parser for USD ASCII format files

#pragma once

#include "../stage/stage.hh"
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
