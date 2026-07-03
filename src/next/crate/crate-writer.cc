// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate Writer Implementation
// Writes spec-compliant USDC with: tokens, strings, paths, fields,
// fieldsets, specs, VALUE data section, time samples, metadata.

#include "crate-writer.hh"
#include "crate-data-source.hh"
#include "crate-writer-types.hh"
#include "lazy-array.hh"
#include "safe-arithmetic.hh"
#include "stream-writer.hh"
#include "../layer/property-index.hh"
#include "../composition/composition.hh"
#include "../types/type-id.hh"
#include <deque>
#include <fstream>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <string_view>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <thread>
#endif

// XXH3 for value-block dedup hashes.
#define XXH_STATIC_LINKING_ONLY
#define XXH_INLINE_ALL
#include "external/xxhash.h"

// LZ4 for compression
#include "lz4/lz4.h"

namespace tinyusdz {
namespace next {

namespace {

class BufferedFileSink {
 public:
  explicit BufferedFileSink(std::ofstream* ofs, size_t capacity = 16u << 20)
      : ofs_(ofs), buffer_(capacity) {}

  bool Write(const uint8_t* data, size_t size) {
    if (!ofs_ || !ofs_->good()) return false;
    if (size == 0) return true;
    if (size >= buffer_.size()) {
      if (!Flush()) return false;
      ofs_->write(reinterpret_cast<const char*>(data),
                  static_cast<std::streamsize>(size));
      bytes_written_ += size;
      return ofs_->good();
    }
    if (pos_ + size > buffer_.size()) {
      if (!Flush()) return false;
    }
    std::memcpy(buffer_.data() + pos_, data, size);
    pos_ += size;
    bytes_written_ += size;
    return true;
  }

  bool Flush() {
    if (!ofs_ || !ofs_->good()) return false;
    if (pos_ == 0) return true;
    ofs_->write(reinterpret_cast<const char*>(buffer_.data()),
                static_cast<std::streamsize>(pos_));
    pos_ = 0;
    return ofs_->good();
  }

  size_t bytes_written() const { return bytes_written_; }

 private:
  std::ofstream* ofs_ = nullptr;
  std::vector<uint8_t> buffer_;
  size_t pos_ = 0;
  size_t bytes_written_ = 0;
};

}  // namespace


#include "crate-writer-impl.inc"

CrateWriter::CrateWriter(const CrateWriteOptions& options)
    : impl_(new Impl(options)) {}

CrateWriter::~CrateWriter() = default;

CrateWriteResult CrateWriter::WriteToFile(const char* filename, const Stage& stage) {
  if (!filename) { CrateWriteResult r; r.error = "Null filename"; return r; }
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) { CrateWriteResult r; r.error = "Stage has no root layer"; return r; }
  return WriteLayerToFile(filename, *root_layer);
}

CrateWriteResult CrateWriter::WriteToFile(const std::string& filename, const Stage& stage) {
  return WriteToFile(filename.c_str(), stage);
}

CrateWriteResult CrateWriter::WriteToMemory(std::vector<uint8_t>& buffer, const Stage& stage) {
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) { CrateWriteResult r; r.error = "Stage has no root layer"; return r; }
  CrateWriteResult result = impl_->Write(*root_layer);
  if (result.success) buffer = impl_->take_buffer();
  return result;
}

CrateWriteResult CrateWriter::WriteToString(std::string& buffer, const Stage& stage) {
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) { CrateWriteResult r; r.error = "Stage has no root layer"; return r; }
  return WriteLayerToString(buffer, *root_layer);
}

CrateWriteResult CrateWriter::WriteLayerToFile(const char* filename, const Layer& layer) {
  if (!filename) { CrateWriteResult r; r.error = "Null filename"; return r; }
  const bool timing = impl_->enable_timing();
  auto t0 = std::chrono::steady_clock::now();
  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) { CrateWriteResult r; r.error = "Failed to open file"; return r; }

  BufferedFileSink file_sink(&ofs);
  CrateWriteSink sink{[](const uint8_t* data, size_t size, void* user) -> bool {
                        return static_cast<BufferedFileSink*>(user)->Write(data,
                                                                           size);
                      },
                      &file_sink};
  CrateWriteResult result = WriteLayerToSink(sink, layer);
  auto t1 = std::chrono::steady_clock::now();
  if (!result.success) return result;
  if (!file_sink.Flush() || !ofs.good()) {
    result.success = false;
    result.error = "Failed to write";
    return result;
  }
  if (timing) {
    auto t2 = std::chrono::steady_clock::now();
    const double stream_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double flush_ms =
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::fprintf(stderr,
                 "[next_crate_write] wrapper stream_to_file=%.1fms "
                 "flush=%.1fms bytes=%zu\n",
                 stream_ms, flush_ms, file_sink.bytes_written());
  }
  return result;
}

CrateWriteResult CrateWriter::WriteLayerToMemory(std::vector<uint8_t>& buffer, const Layer& layer) {
  CrateWriteResult result = impl_->Write(layer);
  if (result.success) buffer = impl_->take_buffer();
  return result;
}

CrateWriteResult CrateWriter::WriteLayerToString(std::string& buffer, const Layer& layer) {
  buffer.clear();
  CrateWriteSink sink{[](const uint8_t* data, size_t size, void* user) -> bool {
                        std::string* buf = static_cast<std::string*>(user);
                        buf->append(reinterpret_cast<const char*>(data), size);
                        return true;
                      },
                      &buffer};
  return WriteLayerToSink(sink, layer);
}

CrateWriteResult CrateWriter::WriteLayerToSink(const CrateWriteSink& sink, const Layer& layer) {
  // Impl::Write streams bootstrap/VALUE/structural/TOC to `sink` in file order
  // when a sink is supplied; buffer_ only ever holds the small structural tail.
  return impl_->Write(layer, &sink);
}

}  // namespace next
}  // namespace tinyusdz
