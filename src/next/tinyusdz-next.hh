// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Unified Header
//
// This header provides the main entry point for the "next" architecture.
// Use -DTINYUSDZ_USE_NEXT=ON in CMake to enable this architecture.
//
// Key differences from the original architecture:
// - Minimal template usage for faster compilation
// - Runtime type dispatch via TypeId enum
// - Small Buffer Optimization (SBO) for Value class
// - Unified PrimSpec (no separate Prim/PrimSpec trees)
// - Time sample value deduplication
// - O(1) property lookup via interned name IDs

#pragma once

#include <cstddef>

// Core types
#include "types/type-id.hh"
#include "types/type-info.hh"
#include "types/value.hh"
#include "types/interpolation.hh"

// PCP composition options (for --variant support)
#include "pcp/prim-index.hh"

// Prim types
#include "prim/path.hh"
#include "prim/attribute.hh"
#include "prim/prim.hh"

// Layer and Stage
#include "layer/property-index.hh"
#include "layer/prim-spec.hh"
#include "layer/layer.hh"
#include "stage/stage.hh"

// Parsers
#include "parser/lexer.hh"
#include "parser/value-parser.hh"
#include "parser/ascii-parser.hh"

// Readers
#include "reader/usda-reader.hh"
#include "reader/usdc-reader.hh"
#include "reader/usdz-reader.hh"

// Writers
#include "writer/usda-writer.hh"
#include "writer/usdc-writer.hh"

// Evaluation
#include "eval/attribute-eval.hh"
#include "eval/value-clip.hh"

// Asset resolution
#include "resolver/asset-resolver.hh"

// Composition
#include "composition/composition.hh"

// Schema APIs
#include "schema/geom-mesh.hh"
#include "schema/schema-registry.hh"
#include "schema/geom-point-instancer.hh"
#include "schema/geom-xform.hh"
#include "schema/usd-lux.hh"
#include "schema/usd-geom-camera.hh"
#include "schema/usd-shade.hh"
#include "schema/usd-skel.hh"
#include "schema/usdPhysics.hh"
#include "schema/usd-ar.hh"
#include "schema/usd-media.hh"
#include "schema/usd-mtlx.hh"

namespace tinyusdz {
namespace next {

// Version info
constexpr int version_major = 0;
constexpr int version_minor = 1;
constexpr int version_micro = 0;
constexpr const char* version_string = "0.1.0-dev";

// ============================================================
// Convenience loading functions (wrap reader APIs)
// ============================================================

/// Options for high-level USD loading.
struct LoadUSDOptions {
  /// Fail closed on unsupported/invalid AOUSD-authored data across USDA and
  /// USDC. Compatibility mode (false) preserves legacy permissive ingestion.
  bool strict_aousd_conformance = false;

  /// Global per-input memory cap in bytes (0 = no limit). Applied to USDA file
  /// size, USDC crate input/allocation checks, USDZ archive/entry size, and
  /// composed external layer loads. Nested format-specific caps are combined
  /// with this cap by taking the stricter non-zero value.
  size_t max_memory = 0;

  /// Format-specific USDA options.
  LoadOptions usda_options;

  /// Format-specific USDC options.
  USDCLoadOptions usdc_options;

  /// Format-specific USDZ options.
  USDZReadOptions usdz_options;
};

/// Load a USD file (auto-detects format: USDA, USDC)
/// @param filename Path to the USD file
/// @param stage Output stage (populated on success)
/// @param warn Warning messages (optional)
/// @param err Error messages (optional)
/// @return true on success
bool LoadUSD(const std::string& filename, Stage* stage,
             std::string* warn = nullptr, std::string* err = nullptr);

bool LoadUSD(const std::string& filename, Stage* stage,
             const LoadUSDOptions& options, std::string* warn = nullptr,
             std::string* err = nullptr);

/// Load a USD file and resolve composition arcs (sublayers / references /
/// payloads / inherits / specializes / variants) in place, so the returned
/// stage exposes the fully composed scene. External USDC layers are loaded
/// lazily and anchored to `filename`'s directory. Self-contained / pre-flattened
/// inputs skip composition entirely (identical to LoadUSD). USDA/USDZ external
/// dependencies are supported through the next PCP layer registry, including
/// direct `.usdz` layers and explicit package paths such as
/// `asset.usdz[root.usdc]`.
/// @param comp_opts Optional composition options (populated with defaults if
///                  null). variant_overrides in comp_opts are applied after
///                  authored variant selections.
bool LoadUSDComposed(const std::string& filename, Stage* stage,
                     std::string* warn = nullptr, std::string* err = nullptr,
                     const pcp::CompositionOptions* comp_opts = nullptr);

bool LoadUSDComposed(const std::string& filename, Stage* stage,
                     const LoadUSDOptions& options,
                     std::string* warn = nullptr, std::string* err = nullptr,
                     const pcp::CompositionOptions* comp_opts = nullptr);

/// True when the stage's root layer authors composition arcs (sublayers,
/// references, payloads, inherits, specializes, or variants) that a plain
/// single-layer load leaves unresolved.
bool StageNeedsComposition(const Stage& stage);

/// Compose an already-loaded stage in place through the PCP engine (variants,
/// internal references/inherits/specializes; external arcs resolve through
/// `resolver`). No-op for self-contained stages. `anchor_label` names the root
/// layer in diagnostics and anchors arcs authored in it.
bool ComposeLoadedStage(Stage* stage, AssetResolver& resolver,
                        const std::string& anchor_label,
                        const LoadUSDOptions& load_options,
                        std::string* warn, std::string* err,
                        const pcp::CompositionOptions* comp_opts = nullptr);

/// Convenience overload for memory-rooted stages (wasm): no anchor directory;
/// external arcs resolve only through resolver custom callbacks (none by
/// default). Primary use: applying variant selections / internal arcs after
/// LoadUSDFromMemory[Owned].
bool ComposeLoadedStage(Stage* stage, std::string* warn, std::string* err,
                        const pcp::CompositionOptions* comp_opts = nullptr,
                        const std::string& anchor_label = "");

/// Load USD from an in-memory buffer (auto-detects USDA / USDC / USDZ from the
/// content). Single-layer load only: composition arcs are not resolved (there
/// is no anchor directory for external assets).
bool LoadUSDFromMemory(const uint8_t* data, size_t size, Stage* stage,
                       std::string* warn = nullptr, std::string* err = nullptr);

bool LoadUSDFromMemory(const uint8_t* data, size_t size, Stage* stage,
                       const LoadUSDOptions& options,
                       std::string* warn = nullptr, std::string* err = nullptr);

/// Load USD from an owned in-memory buffer adopted by move. USDA lazy arrays and
/// USDC lazy arrays retain this buffer directly, avoiding an extra heap copy in
/// WASM/browser bindings that already copied JS bytes into a C++ string.
bool LoadUSDFromMemoryOwned(std::string&& data, Stage* stage,
                            const LoadUSDOptions& options = {},
                            std::string* warn = nullptr,
                            std::string* err = nullptr);

/// Load USDA (ASCII) file
bool LoadUSDA(const std::string& filename, Stage* stage,
              std::string* warn = nullptr, std::string* err = nullptr);

bool LoadUSDA(const std::string& filename, Stage* stage,
              const LoadOptions& options, std::string* warn = nullptr,
              std::string* err = nullptr);

/// Load USDC (binary/Crate) file
bool LoadUSDC(const std::string& filename, Stage* stage,
              std::string* warn = nullptr, std::string* err = nullptr);

bool LoadUSDC(const std::string& filename, Stage* stage,
              const USDCLoadOptions& options, std::string* warn = nullptr,
              std::string* err = nullptr);

// ============================================================
// Convenience writing functions (wrap writer APIs)
// ============================================================

/// Write stage to USDA file
bool WriteUSDA(const Stage& stage, const std::string& filename,
               std::string* err = nullptr);

/// Write stage to USDC file
bool WriteUSDC(const Stage& stage, const std::string& filename,
               std::string* err = nullptr);

}  // namespace next
}  // namespace tinyusdz
