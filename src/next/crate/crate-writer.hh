// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate Writer
// Low-level binary USDC format writer

#pragma once

#include "crate-format.hh"
#include "../layer/layer.hh"
#include "../stage/stage.hh"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <ostream>

namespace tinyusdz {
namespace next {

/// Output sink for streaming crate writes: receives the file bytes in order, in
/// chunks. Returns false to abort the write. See WriteLayerToSink().
using CrateWriteSink = std::function<bool(const uint8_t* data, size_t size)>;

/// Options for crate writing
struct CrateWriteOptions {
  /// Version to write (default 0.8.0 for broad compatibility)
  uint8_t version_major = 0;
  uint8_t version_minor = 8;
  uint8_t version_patch = 0;

  /// Compress large arrays with LZ4
  bool compress_arrays = true;

  /// Minimum array size to compress (in bytes)
  size_t compression_threshold = 256;

  /// Write string tokens inline if small enough
  bool inline_small_values = true;

  /// Maximum inline value size (bytes)
  size_t max_inline_size = 8;

  /// Stream the output to a sink instead of materializing the whole crate in a
  /// single buffer. Only the (small) structural sections are staged in memory;
  /// the (large) VALUE section is streamed block-by-block straight from the
  /// retained source buffer. Used by WriteLayerToSink(); see that method.
  bool streaming = false;
};

/// Result of crate write operation
struct CrateWriteResult {
  bool success = false;
  std::string error;
  size_t bytes_written = 0;

  /// Statistics
  size_t token_count = 0;
  size_t string_count = 0;
  size_t path_count = 0;
  size_t spec_count = 0;
  size_t field_count = 0;

  /// Lazy-array write accounting: arrays copied verbatim from the source crate
  /// (byte pass-through) vs. arrays decoded and re-encoded.
  size_t arrays_passed_through = 0;
  size_t arrays_reencoded = 0;
};

/// Crate file writer
/// Writes Stage/Layer to binary USDC format
class CrateWriter {
public:
  explicit CrateWriter(const CrateWriteOptions& options = {});
  ~CrateWriter();

  /// Write Stage to a file
  CrateWriteResult WriteToFile(const char* filename, const Stage& stage);
  CrateWriteResult WriteToFile(const std::string& filename, const Stage& stage);

  /// Write Stage to memory buffer
  CrateWriteResult WriteToMemory(std::vector<uint8_t>& buffer, const Stage& stage);

  /// Write Layer to a file (for individual layer export)
  CrateWriteResult WriteLayerToFile(const char* filename, const Layer& layer);

  /// Write Layer to memory buffer
  CrateWriteResult WriteLayerToMemory(std::vector<uint8_t>& buffer, const Layer& layer);

  /// Write Layer to a streaming sink. The crate is emitted in file order
  /// (bootstrap, VALUE section, structural sections, TOC) without ever holding
  /// the full output in memory: peak working set is the (small) structural
  /// sections plus the retained source buffer, with VALUE bytes streamed
  /// straight from their source. `sink` receives ordered byte chunks and
  /// returns false to abort. Output is byte-identical to WriteLayerToMemory.
  CrateWriteResult WriteLayerToSink(const CrateWriteSink& sink, const Layer& layer);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace next
}  // namespace tinyusdz
