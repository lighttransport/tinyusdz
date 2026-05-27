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

// Core types
#include "types/type-id.hh"
#include "types/type-info.hh"
#include "types/value.hh"
#include "types/interpolation.hh"

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

// Writers
#include "writer/usda-writer.hh"
#include "writer/usdc-writer.hh"

// Evaluation
#include "eval/attribute-eval.hh"

// Asset resolution
#include "resolver/asset-resolver.hh"

// Composition
#include "composition/composition.hh"

// Schema APIs
#include "schema/geom-mesh.hh"
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

/// Load a USD file (auto-detects format: USDA, USDC)
/// @param filename Path to the USD file
/// @param stage Output stage (populated on success)
/// @param warn Warning messages (optional)
/// @param err Error messages (optional)
/// @return true on success
bool LoadUSD(const std::string& filename, Stage* stage,
             std::string* warn = nullptr, std::string* err = nullptr);

/// Load USDA (ASCII) file
bool LoadUSDA(const std::string& filename, Stage* stage,
              std::string* warn = nullptr, std::string* err = nullptr);

/// Load USDC (binary/Crate) file
bool LoadUSDC(const std::string& filename, Stage* stage,
              std::string* warn = nullptr, std::string* err = nullptr);

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
