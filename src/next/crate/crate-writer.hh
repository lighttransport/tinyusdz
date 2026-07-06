// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate Writer
// Low-level binary USDC format writer

#pragma once

#include "crate-format.hh"
#include "../layer/layer.hh"
#include "../stage/stage.hh"
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

/// Output sink for streaming crate writes: receives the file bytes in order, in
/// chunks. Returns false to abort the write. See WriteLayerToSink().
/// C-style: plain function pointer + opaque user context (no std::function --
/// no heap alloc / vtable). `user` is caller-owned and must outlive the write.
struct CrateWriteSink {
  bool (*fn)(const uint8_t* data, size_t size, void* user) = nullptr;
  void* user = nullptr;
  // Optional seek-write: overwrite `size` bytes at absolute output position
  // `pos` (bytes already emitted via fn()). When present, the writer can stream
  // the VALUE section straight to the sink as blocks are built and backfill the
  // bootstrap header afterwards, so the value blocks never accumulate in memory
  // (the low-peak streaming-write path). Null => append-only sink; the writer
  // stages value blocks and emits them at the end instead. Declared after
  // `user` so existing positional `{fn, user}` initializers stay valid.
  bool (*patch_fn)(uint64_t pos, const uint8_t* data, size_t size,
                   void* user) = nullptr;
  explicit operator bool() const { return fn != nullptr; }
  bool operator()(const uint8_t* data, size_t size) const {
    return fn(data, size, user);
  }
  bool seekable() const { return patch_fn != nullptr; }
  bool patch(uint64_t pos, const uint8_t* data, size_t size) const {
    return patch_fn(pos, data, size, user);
  }
};

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

  /// Worker count for parallel build/sort paths (1 = serial; <=0 = auto, capped).
  /// Only effective in a TINYUSDZ_ENABLE_THREAD build. Output is a valid
  /// round-trippable crate at any thread count and is byte-identical across thread
  /// counts (the parallel build merges per-prim results in deterministic order).
  int num_threads = 1;

  /// Enable writer timing counters and print timing summary after write.
  bool enable_timing = false;

  /// Consume the input layer's property values while writing: after a prim's
  /// fields are built (its values encoded into the crate's value blocks), the
  /// prim's default values and time samples are released
  /// (PrimSpec::release_value_payloads), so the composed layer and the staged
  /// value blocks never coexist in full — a large peak-RSS cut on
  /// write-and-discard flows (flatten-to-file). The layer stays structurally
  /// valid but value-stripped (AssetPath defaults are kept for post-write
  /// asset collection). Requires the caller's layer to be genuinely mutable;
  /// output bytes are identical to a non-consuming write. Default off.
  bool consume_values = false;
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

  /// VALUE blocks elided because an identical-bytes block was already written
  /// (cross-spec content dedup).
  size_t blocks_deduped = 0;
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
  /// Write Stage to string buffer
  CrateWriteResult WriteToString(std::string& buffer, const Stage& stage);

  /// Write Layer to a file (for individual layer export)
  CrateWriteResult WriteLayerToFile(const char* filename, const Layer& layer);

  /// Write Layer to memory buffer
  CrateWriteResult WriteLayerToMemory(std::vector<uint8_t>& buffer, const Layer& layer);
  /// Write Layer to string buffer
  CrateWriteResult WriteLayerToString(std::string& buffer, const Layer& layer);

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
