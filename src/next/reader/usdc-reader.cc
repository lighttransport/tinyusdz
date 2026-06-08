// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Reader implementation

#include "usdc-reader.hh"

#include <fstream>

namespace tinyusdz {
namespace next {

namespace {

std::string GetBaseDir(const std::string& filename) {
  size_t pos = filename.find_last_of("/\\");
  if (pos != std::string::npos) {
    return filename.substr(0, pos + 1);
  }
  return "";
}

// Convert CrateReadResult to USDCLoadResult
USDCLoadResult ConvertResult(CrateReadResult&& crate_result) {
  USDCLoadResult result;
  result.success = crate_result.success;
  result.stage = std::move(crate_result.stage);
  result.errors = std::move(crate_result.errors);
  result.warnings = std::move(crate_result.warnings);
  result.version = crate_result.version;

  if (!result.errors.empty()) {
    const auto& first_err = result.errors[0];
    result.error_summary = "Offset " + std::to_string(first_err.offset) +
                           ": " + first_err.message;
  }

  return result;
}

}  // anonymous namespace

USDCLoadResult LoadUSDCFromFile(const char* filename, const USDCLoadOptions& options) {
  USDCLoadResult result;

  if (!filename) {
    result.error_summary = "Null filename";
    return result;
  }

  // Set base directory if not specified
  USDCLoadOptions opts = options;
  if (opts.base_dir.empty()) {
    opts.base_dir = GetBaseDir(filename);
  }

  CrateReader reader(opts.crate_options);
  CrateReadResult crate_result = reader.ReadFile(filename);

  return ConvertResult(std::move(crate_result));
}

USDCLoadResult LoadUSDCFromFile(const std::string& filename, const USDCLoadOptions& options) {
  return LoadUSDCFromFile(filename.c_str(), options);
}

USDCLoadResult LoadUSDCFromMemory(const uint8_t* data, size_t size, const USDCLoadOptions& options) {
  USDCLoadResult result;

  if (!data || size == 0) {
    result.error_summary = "Empty data";
    return result;
  }

  CrateReader reader(options.crate_options);
  CrateReadResult crate_result = reader.Read(data, size);

  return ConvertResult(std::move(crate_result));
}

USDCLoadResult LoadUSDCFromMemoryOwned(std::string&& data, const USDCLoadOptions& options) {
  CrateReader reader(options.crate_options);
  CrateReadResult crate_result = reader.ReadOwned(std::move(data));
  return ConvertResult(std::move(crate_result));
}

// Note: IsUSDCFile and IsUSDCData are defined in crate-reader.cc

}  // namespace next
}  // namespace tinyusdz
