// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Reader
// High-level interface for reading USDC binary files

#pragma once

#include "../crate/crate-reader.hh"
#include "../stage/stage.hh"
#include <string>

namespace tinyusdz {
namespace next {

/// Options for loading USDC files
struct USDCLoadOptions {
  /// Crate reader options
  CrateReadOptions crate_options;

  /// Resolve asset references
  bool resolve_assets = false;

  /// Base directory for resolving relative paths
  std::string base_dir;
};

/// Load result containing the stage and any messages
struct USDCLoadResult {
  bool success = false;
  Stage stage;
  std::vector<CrateError> errors;
  std::vector<std::string> warnings;
  std::string error_summary;
  CrateVersion version;
};

/// Load a USDC file from disk
USDCLoadResult LoadUSDCFromFile(const char* filename, const USDCLoadOptions& options = {});
USDCLoadResult LoadUSDCFromFile(const std::string& filename, const USDCLoadOptions& options = {});

/// Load a USDC from memory buffer
USDCLoadResult LoadUSDCFromMemory(const uint8_t* data, size_t size, const USDCLoadOptions& options = {});

/// Quick check if a file is USDC format (checks magic bytes)
bool IsUSDCFile(const char* filename);

/// Check if data is USDC format
bool IsUSDCData(const uint8_t* data, size_t size);

}  // namespace next
}  // namespace tinyusdz
