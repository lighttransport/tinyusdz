// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA Reader implementation

#include "usda-reader.hh"
#include "../strfmt.hh"

#include <cstring>

#if defined(TINYUSDZ_NEXT_ENABLE_FILEIO)
#include "../io/file-util.hh"  // IsUSDAFile header sniff (file-based)
#endif

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
      result.error_summary = "Line " + UIntToStr(first_err.line) +
                             ", column " + UIntToStr(first_err.column) +
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
      result.error_summary = "Line " + UIntToStr(first_err.line) +
                             ", column " + UIntToStr(first_err.column) +
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
#if defined(TINYUSDZ_NEXT_ENABLE_FILEIO)
  std::vector<uint8_t> header;
  std::string err;
  if (!io::ReadFileHeader(&header, &err, filename, 16)) return false;
  return header.size() >= 5 &&
         std::memcmp(header.data(), "#usda", 5) == 0;
#else
  (void)filename;
  return false;  // no filesystem in the freestanding/WASM build
#endif
}

}  // namespace next
}  // namespace tinyusdz
