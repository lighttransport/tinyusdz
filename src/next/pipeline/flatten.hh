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
  double read_ms = 0.0;
  double compose_ms = 0.0;
  double write_ms = 0.0;
  std::vector<std::string> referenced_assets;  // asset-valued fields in output layer
  // Non-fatal composition errors (unresolved/unloadable external arcs). The
  // flatten still succeeds with those arcs skipped; callers should surface
  // these so silent partial composition is visible.
  std::vector<std::string> composition_errors;
};

/// Options controlling the flatten pipeline.
struct FlattenOptions {
  CrateReadOptions read;            // lazy_arrays defaults true
  CrateWriteOptions write;          // version must stay in the supported range
  CompositionOptions composition;   // LIVRPS / payload / variant options
  bool flatten = true;              // false => just re-serialize the root layer

  // Multi-file composition: external reference/payload arcs resolve through
  // `resolver` (anchor-relative + search paths) and load through
  // `layer_loader` (see MakeFileSystemLayerLoader). When unset (default),
  // external arcs are skipped — the historical single-file behavior.
  LayerLoader layer_loader;
  AssetResolver* resolver = nullptr;
  // Anchor for arcs authored in the root layer (its resolved file path);
  // empty = resolve against the resolver search paths only.
  std::string root_anchor_path;
  // When true, unresolved composition errors abort before writing. This is useful
  // for resumable/async loaders that need to fetch a missing layer and retry
  // rather than silently emitting a partial flatten.
  bool fail_on_composition_error = false;
};

/// Filesystem-backed LayerLoader for native multi-file flattens: reads the
/// resolved path and parses it as a (lazy) USDC crate. The loaded layer keeps
/// its source bytes alive via the crate data source, so arrays still pass
/// through verbatim. USDA dependencies are not supported by this loader yet.
LayerLoader MakeFileSystemLayerLoader(const CrateReadOptions& read_opts = {});

/// Read a USDC buffer, (optionally) flatten it, and write a USDC buffer, keeping
/// numeric arrays as lazy references throughout so they are copied straight
/// through instead of decoded and re-encoded. Returns false (with `*err` set)
/// on failure. `stats` is optional.
bool FlattenUSDCToUSDC(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                       const FlattenOptions& opts = {}, FlattenStats* stats = nullptr,
                       std::string* err = nullptr);

/// As FlattenUSDCToUSDC, but streams the output crate to `sink` instead of
/// returning a buffer. The caller must keep `data` alive until the call returns.
bool FlattenUSDCToUSDCToSink(const uint8_t* data, size_t size,
                             const CrateWriteSink& sink,
                             const FlattenOptions& opts = {},
                             FlattenStats* stats = nullptr,
                             std::string* err = nullptr);

/// As above, but adopts the input bytes by move (single in-heap copy of the
/// input). Prefer on memory-constrained targets when the caller can give up the
/// input buffer (e.g. the bytes the JS side already wrote into the WASM heap).
bool FlattenUSDCToUSDCOwned(std::string&& data, std::vector<uint8_t>& out,
                            const FlattenOptions& opts = {}, FlattenStats* stats = nullptr,
                            std::string* err = nullptr);

/// As FlattenUSDCToUSDCOwned, but streams the output crate to `sink` in file
/// order instead of returning a buffer. The full output is never materialized:
/// peak working set is the (small) structural sections plus the retained source
/// buffer, with VALUE bytes streamed straight from their source. Output bytes
/// are identical to the buffer form. `sink` returns false to abort.
bool FlattenUSDCToUSDCOwnedToSink(std::string&& data, const CrateWriteSink& sink,
                                  const FlattenOptions& opts = {},
                                  FlattenStats* stats = nullptr,
                                  std::string* err = nullptr);

}  // namespace pipeline
}  // namespace next
}  // namespace tinyusdz
