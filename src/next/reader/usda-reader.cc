// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA Reader implementation

#include "usda-reader.hh"

#include <fstream>
#include <cstring>

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

}  // anonymous namespace

LoadResult LoadUSDAFromFile(const char* filename, const LoadOptions& options) {
  LoadResult result;

  if (!filename) {
    result.error_summary = "Null filename";
    return result;
  }

  // Set base directory if not specified
  LoadOptions opts = options;
  if (opts.base_dir.empty()) {
    opts.base_dir = GetBaseDir(filename);
  }

  AsciiParser parser(opts.parse_options);

  if (parser.ParseFile(filename)) {
    result.success = true;
    result.stage = parser.TakeStage();
  } else {
    result.errors = parser.GetErrors();
    if (!result.errors.empty()) {
      const auto& first_err = result.errors[0];
      result.error_summary = "Line " + std::to_string(first_err.line) +
                             ", column " + std::to_string(first_err.column) +
                             ": " + first_err.message;
    }
  }

  result.warnings = parser.GetWarnings();
  return result;
}

LoadResult LoadUSDAFromFile(const std::string& filename, const LoadOptions& options) {
  return LoadUSDAFromFile(filename.c_str(), options);
}

LoadResult LoadUSDAFromString(const char* data, size_t length, const LoadOptions& options) {
  LoadResult result;

  if (!data) {
    result.error_summary = "Null data";
    return result;
  }

  AsciiParser parser(options.parse_options);

  if (parser.Parse(data, length)) {
    result.success = true;
    result.stage = parser.TakeStage();
  } else {
    result.errors = parser.GetErrors();
    if (!result.errors.empty()) {
      const auto& first_err = result.errors[0];
      result.error_summary = "Line " + std::to_string(first_err.line) +
                             ", column " + std::to_string(first_err.column) +
                             ": " + first_err.message;
    }
  }

  result.warnings = parser.GetWarnings();
  return result;
}

LoadResult LoadUSDAFromString(const std::string& data, const LoadOptions& options) {
  return LoadUSDAFromString(data.c_str(), data.size(), options);
}

bool IsUSDAFile(const char* filename) {
  if (!filename) {
    return false;
  }

  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  // Read first 16 bytes and look for #usda signature
  char buffer[16] = {0};
  file.read(buffer, sizeof(buffer) - 1);

  // Check for #usda header
  if (std::strncmp(buffer, "#usda", 5) == 0) {
    return true;
  }

  // Could also start with whitespace or comments
  // For now, just check for the #usda signature
  return false;
}

}  // namespace next
}  // namespace tinyusdz
