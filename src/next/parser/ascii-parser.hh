// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII Parser
// High-level parser for USD ASCII format files

#pragma once

#include "../stage/stage.hh"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace tinyusdz {
namespace next {

/// Optional USDA parser profiling counters. All values accumulate into the
/// provided instance when ParseOptions::profile is non-null.
struct USDAParseProfile {
  uint64_t input_bytes = 0;
  double file_open_ms = 0.0;
  double file_read_ms = 0.0;
  double parser_ms = 0.0;
  double stage_metadata_ms = 0.0;
  double prims_ms = 0.0;
  double finalize_ms = 0.0;
  uint64_t prims = 0;
  uint64_t properties = 0;
  uint64_t time_samples = 0;
  uint64_t arrays = 0;
  uint64_t array_bytes = 0;
  uint64_t simple_arrays = 0;
  uint64_t numeric_arrays = 0;
  uint64_t numeric_array_bytes = 0;
  uint64_t numeric_array_scalars = 0;
  uint64_t parallel_arrays = 0;
  uint64_t parallel_array_bytes = 0;
  uint64_t parallel_array_scalars = 0;
  uint64_t parallel_array_fallbacks = 0;
  uint64_t deferred_arrays = 0;
  uint64_t deferred_array_bytes = 0;
  bool used_mmap = false;
};

/// Options for parsing USDA files
struct ParseOptions {
  /// Allow unknown/unrecognized prim types (stored as generic prims)
  bool allow_unknown_types = true;

  /// Maximum file size to parse (0 = no limit)
  size_t max_file_size = 0;

  /// Maximum nesting depth for prims
  size_t max_depth = 256;

  /// Worker-thread hint for the parallel large-array parse path (when built with
  /// TINYUSDZ_ENABLE_THREAD). 0 = auto (min(hardware_concurrency, 8)); 1 =
  /// serial; >1 = that many workers. Replaces the former TINYUSDZ_NEXT_NUM_THREADS
  /// env read so the library takes no implicit process-environment input.
  int num_threads = 0;

  /// Parse captured simple numeric arrays (attribute defaults AND timeSample
  /// values) on the parser worker pool, batched, while the main thread keeps
  /// lexing; payloads are filled in place and joined before finalize. Output
  /// is bit-identical to the synchronous parse (verified byte-identical on
  /// 4.25GB Caldera, ~2x parse speedup). Requires TINYUSDZ_ENABLE_THREAD and
  /// num_threads != 1; ignored (synchronous) otherwise.
  bool async_arrays = true;

  /// Parse mid-size prim subtrees on the parser worker pool: the main thread
  /// captures each prim block (SIMD brace matching) and workers parse blocks
  /// into layer fragments that are stitched (authored order preserved) before
  /// finalize. Semantically identical output. Requires TINYUSDZ_ENABLE_THREAD
  /// and num_threads != 1; ignored otherwise.
  bool parallel_prims = true;

  /// Optional profiling destination. Null keeps profiling disabled.
  USDAParseProfile* profile = nullptr;
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
