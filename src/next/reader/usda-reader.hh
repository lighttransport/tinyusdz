// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA Reader
// High-level interface for reading USDA files

#pragma once

#include "../stage/stage.hh"
#include "../parser/ascii-parser.hh"
#include <string>

namespace tinyusdz {
namespace next {

/// Options for loading USDA files
struct LoadOptions {
  /// Parse options passed to the parser
  ParseOptions parse_options;

  /// Resolve asset references
  bool resolve_assets = false;

  /// Base directory for resolving relative paths
  std::string base_dir;

  // ============================================================
  // Memory budget convenience accessors
  // ============================================================

  /// Set memory limit in megabytes (convenience method)
  LoadOptions& SetMaxMemoryMB(uint32_t mb) {
    parse_options.max_memory_limit_in_mb = mb;
    return *this;
  }

  /// Set maximum prim count (convenience method)
  LoadOptions& SetMaxPrimCount(size_t count) {
    parse_options.max_prim_count = count;
    return *this;
  }

  /// Set maximum properties per prim (convenience method)
  LoadOptions& SetMaxPropertiesPerPrim(size_t count) {
    parse_options.max_properties_per_prim = count;
    return *this;
  }

  /// Set maximum array elements (convenience method)
  LoadOptions& SetMaxArrayElements(size_t count) {
    parse_options.max_array_elements = count;
    return *this;
  }
};

/// Load result containing the stage and any messages
struct LoadResult {
  bool success = false;
  Stage stage;
  std::vector<ParseError> errors;
  std::vector<std::string> warnings;
  std::string error_summary;
};

/// Load a USDA file from disk
LoadResult LoadUSDAFromFile(const char* filename, const LoadOptions& options = {});
LoadResult LoadUSDAFromFile(const std::string& filename, const LoadOptions& options = {});

/// Load a USDA from a string
LoadResult LoadUSDAFromString(const char* data, size_t length, const LoadOptions& options = {});
LoadResult LoadUSDAFromString(const std::string& data, const LoadOptions& options = {});

/// Quick check if a file appears to be USDA format (checks magic bytes)
bool IsUSDAFile(const char* filename);

}  // namespace next
}  // namespace tinyusdz
