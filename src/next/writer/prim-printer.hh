// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Prim Printer
// Pretty-print Stage and UsdPrim for debugging

#pragma once

#include "../stage/stage.hh"
#include <string>
#include <ostream>

namespace tinyusdz {
namespace next {

/// Options for prim printing
struct PrimPrintOptions {
  /// Indent string (default 4 spaces)
  std::string indent = "    ";

  /// Maximum depth to print (-1 = unlimited)
  int max_depth = -1;

  /// Print property values
  bool print_values = true;

  /// Print metadata
  bool print_metadata = true;

  /// Print relationships
  bool print_relationships = true;

  /// Maximum properties per prim (0 = unlimited)
  size_t max_properties = 0;
};

/// Print a single UsdPrim to string
std::string PrintPrim(const UsdPrim& prim, const PrimPrintOptions& opts = {});

/// Print a single UsdPrim to stream
void PrintPrim(std::ostream& os, const UsdPrim& prim, const PrimPrintOptions& opts = {});

/// Print entire Stage to string
std::string PrintStage(const Stage& stage, const PrimPrintOptions& opts = {});

/// Print entire Stage to stream
void PrintStage(std::ostream& os, const Stage& stage, const PrimPrintOptions& opts = {});

/// Print a PrimSpec directly (lower-level)
std::string PrintPrimSpec(const PrimSpec& spec, const PrimPrintOptions& opts = {});
void PrintPrimSpec(std::ostream& os, const PrimSpec& spec, int depth, const PrimPrintOptions& opts);

/// Print a Layer to string
std::string PrintLayer(const Layer& layer, const PrimPrintOptions& opts = {});
void PrintLayer(std::ostream& os, const Layer& layer, const PrimPrintOptions& opts = {});

}  // namespace next
}  // namespace tinyusdz
