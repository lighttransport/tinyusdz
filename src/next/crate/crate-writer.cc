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
#include "../types/type-id.hh"
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <fstream>
#include <algorithm>
#include <cstring>

// LZ4 for compression
#include "lz4/lz4.h"

namespace tinyusdz {
namespace next {


#include "crate-writer-impl.inc"

CrateWriter::CrateWriter(const CrateWriteOptions& options)
    : impl_(new Impl(options)) {}

CrateWriter::~CrateWriter() = default;

CrateWriteResult CrateWriter::WriteToFile(const char* filename, const Stage& stage) {
  if (!filename) { CrateWriteResult r; r.error = "Null filename"; return r; }
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) { CrateWriteResult r; r.error = "Stage has no root layer"; return r; }
  std::vector<uint8_t> buffer;
  CrateWriteResult result = WriteToMemory(buffer, stage);
  if (!result.success) return result;
  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) { result.success = false; result.error = "Failed to open file"; return result; }
  ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  if (!ofs.good()) { result.success = false; result.error = "Failed to write"; return result; }
  return result;
}

CrateWriteResult CrateWriter::WriteToFile(const std::string& filename, const Stage& stage) {
  return WriteToFile(filename.c_str(), stage);
}

CrateWriteResult CrateWriter::WriteToMemory(std::vector<uint8_t>& buffer, const Stage& stage) {
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) { CrateWriteResult r; r.error = "Stage has no root layer"; return r; }
  CrateWriteResult result = impl_->Write(*root_layer);
  if (result.success) buffer = impl_->buffer();
  return result;
}

CrateWriteResult CrateWriter::WriteLayerToFile(const char* filename, const Layer& layer) {
  if (!filename) { CrateWriteResult r; r.error = "Null filename"; return r; }
  std::vector<uint8_t> buffer;
  CrateWriteResult result = WriteLayerToMemory(buffer, layer);
  if (!result.success) return result;
  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) { result.success = false; result.error = "Failed to open file"; return result; }
  ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  if (!ofs.good()) { result.success = false; result.error = "Failed to write"; return result; }
  return result;
}

CrateWriteResult CrateWriter::WriteLayerToMemory(std::vector<uint8_t>& buffer, const Layer& layer) {
  CrateWriteResult result = impl_->Write(layer);
  if (result.success) buffer = impl_->buffer();
  return result;
}

CrateWriteResult CrateWriter::WriteLayerToSink(const CrateWriteSink& sink, const Layer& layer) {
  // Impl::Write streams bootstrap/VALUE/structural/TOC to `sink` in file order
  // when a sink is supplied; buffer_ only ever holds the small structural tail.
  return impl_->Write(layer, &sink);
}

}  // namespace next
}  // namespace tinyusdz
