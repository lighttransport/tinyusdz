// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Low-memory flatten pipeline implementation

#include "flatten.hh"

#include "../layer/layer.hh"
#include "../stage/stage.hh"

#include <memory>

namespace tinyusdz {
namespace next {
namespace pipeline {

namespace {

// Shared post-read logic: (optionally) flatten and write. `rr.stage`'s lazy
// Values hold their own shared_ptr to the retained source buffer, so it stays
// alive through the write regardless of the reader's lifetime.
bool FlattenLoaded(CrateReadResult&& rr, size_t input_bytes, std::vector<uint8_t>& out,
                   const FlattenOptions& opts, FlattenStats* stats, std::string* err) {
  if (!rr.success) {
    if (err) *err = rr.errors.empty() ? "crate read failed" : rr.errors[0].message;
    return false;
  }

  const Layer* root = rr.stage.GetRootLayer();
  if (!root) {
    if (err) *err = "no root layer";
    return false;
  }

  std::unique_ptr<Layer> composed;
  const Layer* layer = root;
  if (opts.flatten) {
    Compositor comp;
    comp.SetOptions(opts.composition);
    composed = comp.Compose(*root);  // structural: moves lazy refs, no decode
    if (!composed) {
      if (err) *err = "composition failed";
      return false;
    }
    layer = composed.get();
  }

  CrateWriter writer(opts.write);
  CrateWriteResult wr = writer.WriteLayerToMemory(out, *layer);
  if (!wr.success) {
    if (err) *err = wr.error.empty() ? "crate write failed" : wr.error;
    return false;
  }

  if (stats) {
    stats->input_bytes = input_bytes;
    stats->output_bytes = out.size();
    stats->prim_count = layer->prim_count();
    stats->arrays_passed_through = wr.arrays_passed_through;
    stats->arrays_reencoded = wr.arrays_reencoded;
  }
  return true;
}

}  // namespace

bool FlattenUSDCToUSDC(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                       const FlattenOptions& opts, FlattenStats* stats,
                       std::string* err) {
  if (stats) *stats = FlattenStats{};
  if (!data || size == 0) {
    if (err) *err = "empty input";
    return false;
  }
  CrateReader reader(opts.read);
  return FlattenLoaded(reader.Read(data, size), size, out, opts, stats, err);
}

bool FlattenUSDCToUSDCOwned(std::string&& data, std::vector<uint8_t>& out,
                            const FlattenOptions& opts, FlattenStats* stats,
                            std::string* err) {
  if (stats) *stats = FlattenStats{};
  if (data.empty()) {
    if (err) *err = "empty input";
    return false;
  }
  const size_t input_bytes = data.size();
  CrateReader reader(opts.read);
  return FlattenLoaded(reader.ReadOwned(std::move(data)), input_bytes, out, opts, stats, err);
}

}  // namespace pipeline
}  // namespace next
}  // namespace tinyusdz
