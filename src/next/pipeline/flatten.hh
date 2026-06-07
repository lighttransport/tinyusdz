// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Low-memory flatten pipeline
//
// One-call "load USDC (lazy) -> compose/flatten (structural) -> write USDC
// (verbatim byte pass-through)" facade. Designed for both native use and the
// WASM binding: numeric array payloads are never decoded unless edited, so a
// flatten of a 200 MB scene stays close to the input size in heap instead of
// the 5-10x blow-up of the eager path.

#pragma once

#include "../composition/composition.hh"
#include "../crate/crate-reader.hh"
#include "../crate/crate-writer.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {
namespace pipeline {

/// Accounting for a flatten run.
struct FlattenStats {
  size_t input_bytes = 0;
  size_t output_bytes = 0;
  size_t prim_count = 0;
  size_t arrays_passed_through = 0;  // arrays copied verbatim from the source
  size_t arrays_reencoded = 0;       // lazy arrays that fell back to re-encode
};

/// Options controlling the flatten pipeline.
struct FlattenOptions {
  CrateReadOptions read;            // lazy_arrays defaults true
  CrateWriteOptions write;          // version must stay in the supported range
  CompositionOptions composition;   // LIVRPS / payload / variant options
  bool flatten = true;              // false => just re-serialize the root layer
};

/// Read a USDC buffer, (optionally) flatten it, and write a USDC buffer, keeping
/// numeric arrays as lazy references throughout so they are copied straight
/// through instead of decoded and re-encoded. Returns false (with `*err` set)
/// on failure. `stats` is optional.
bool FlattenUSDCToUSDC(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                       const FlattenOptions& opts = {}, FlattenStats* stats = nullptr,
                       std::string* err = nullptr);

/// As above, but adopts the input bytes by move (single in-heap copy of the
/// input). Prefer on memory-constrained targets when the caller can give up the
/// input buffer (e.g. the bytes the JS side already wrote into the WASM heap).
bool FlattenUSDCToUSDCOwned(std::string&& data, std::vector<uint8_t>& out,
                            const FlattenOptions& opts = {}, FlattenStats* stats = nullptr,
                            std::string* err = nullptr);

}  // namespace pipeline
}  // namespace next
}  // namespace tinyusdz
