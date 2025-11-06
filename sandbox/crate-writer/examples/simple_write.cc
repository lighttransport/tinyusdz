// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Minimal USDC Crate File Writing Example
//
// This example creates the simplest possible valid USDC file
// using only inline value types (no Specifier fields).
//

#include "crate-writer.hh"
#include <iostream>

using namespace tinyusdz;
using namespace tinyusdz::experimental;

// Use tinyusdz::crate namespace explicitly
namespace tcrate = tinyusdz::crate;

int main(int argc, char** argv) {
  std::string output_file = "simple_output.usdc";

  if (argc > 1) {
    output_file = argv[1];
  }

  std::cout << "Creating minimal USDC file: " << output_file << std::endl;

  // Create writer
  CrateWriter writer(output_file);

  // Configure for USD v0.8.0
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  // Open file
  std::string err;
  if (!writer.Open(&err)) {
    std::cerr << "ERROR: Failed to open file: " << err << std::endl;
    return 1;
  }

  std::cout << "File opened successfully" << std::endl;

  // Add root spec
  {
    Path root_path("/", "");
    tcrate::FieldValuePairVector fields;
    if (!writer.AddSpec(root_path, SpecType::PseudoRoot, fields, &err)) {
      std::cerr << "ERROR: Failed to add root: " << err << std::endl;
      return 1;
    }
    std::cout << "Added root spec" << std::endl;
  }

  // Add a prim with a relationship (Path array)
  {
    Path prim_path("/TestPrim", "");
    tcrate::FieldValuePairVector fields;

    // Add specifier
    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    // Add a relationship field with path array
    tcrate::CrateValue path_array_value;
    std::vector<Path> targets;
    targets.push_back(Path("/Target1", ""));
    targets.push_back(Path("/Target2", ""));
    path_array_value.Set(targets);
    fields.push_back({"testRel.targetPaths", path_array_value});

    if (!writer.AddSpec(prim_path, SpecType::Prim, fields, &err)) {
      std::cerr << "ERROR: Failed to add prim: " << err << std::endl;
      return 1;
    }
    std::cout << "Added prim /TestPrim with relationship" << std::endl;
  }

  // Finalize and write
  std::cout << "\nFinalizing file..." << std::endl;

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: Failed to finalize: " << err << std::endl;
    return 1;
  }

  std::cout << "File finalized successfully" << std::endl;

  // Close
  writer.Close();

  std::cout << "\nSUCCESS: Created USDC file: " << output_file << std::endl;
  std::cout << "File size: " << std::ifstream(output_file, std::ios::ate | std::ios::binary).tellg() << " bytes" << std::endl;

  return 0;
}
