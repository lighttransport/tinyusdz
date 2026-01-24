// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Test: Layer/PrimSpec to Crate Conversion
//
// This test demonstrates the Layer/PrimSpec based conversion approach,
// which is more generic than the Stage-based approach.
//

#include "crate-writer.hh"
#include <iostream>
#include "layer.hh"  // For Layer, PrimSpec, etc.

using namespace tinyusdz;
using namespace tinyusdz::experimental;

// Use tinyusdz::crate namespace explicitly to avoid ambiguity
namespace tcrate = tinyusdz::crate;

int main(int argc, char** argv) {
  std::string output_file = "test_layer_output.usdc";

  if (argc > 1) {
    output_file = argv[1];
  }

  std::cout << "=== Layer/PrimSpec to Crate Conversion Test ===" << std::endl;
  std::cout << "Output file: " << output_file << std::endl;

  // ========================================================================
  // Step 1: Create a Layer with PrimSpecs
  // ========================================================================

  Layer layer;

  // Create a simple prim spec for /World
  PrimSpec world_prim(Specifier::Def, "Xform", "World");

  // Add some metadata
  world_prim.metas().displayName = "World Root";
  world_prim.metas().doc = "Root prim for the USD scene";

  // Create a child prim
  PrimSpec geom_prim(Specifier::Def, "Mesh", "Mesh");

  // Add geom to world's children
  world_prim.children().push_back(geom_prim);

  // Add world to layer
  layer.primspecs().insert({"World", world_prim});

  std::cout << "Created Layer with " << layer.primspecs().size() << " top-level prims" << std::endl;

  // ========================================================================
  // Step 2: Create the CrateWriter
  // ========================================================================

  CrateWriter writer(output_file);

  // Configure options
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;
  opts.enable_compression = false;  // Disable for easier debugging
  writer.SetOptions(opts);

  // ========================================================================
  // Step 3: Open the file
  // ========================================================================

  std::string err;
  if (!writer.Open(&err)) {
    std::cerr << "ERROR: Failed to open file: " << err << std::endl;
    return 1;
  }

  std::cout << "File opened successfully" << std::endl;

  // ========================================================================
  // Step 4: Convert Layer to Crate Specs
  // ========================================================================

  std::cout << "\nStarting Layer→Crate conversion..." << std::endl;

  if (!writer.ConvertLayerToSpecs(layer, &err)) {
    std::cerr << "ERROR: Failed to convert layer: " << err << std::endl;
    return 1;
  }

  std::cout << "Layer conversion completed successfully" << std::endl;

  // ========================================================================
  // Step 5: Finalize the file
  // ========================================================================

  std::cout << "\nFinalizing USDC file..." << std::endl;

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: Failed to finalize file: " << err << std::endl;
    return 1;
  }

  std::cout << "File finalized successfully" << std::endl;
  std::cout << "\nTest completed successfully!" << std::endl;
  std::cout << "Output written to: " << output_file << std::endl;

  return 0;
}
