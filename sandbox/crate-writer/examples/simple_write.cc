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

  // Add a single attribute spec with only inline values
  // Attributes don't require specifier fields
  // NOTE: Attributes at root use prim="/" and the attribute name in prop part
  {
    Path attr_path("/", "testAttr");
    tcrate::FieldValuePairVector fields;

    // Add a simple default value (inline int32)
    tcrate::CrateValue default_value;
    default_value.Set(static_cast<int32_t>(42));
    fields.push_back({"default", default_value});

    if (!writer.AddSpec(attr_path, SpecType::Attribute, fields, &err)) {
      std::cerr << "ERROR: Failed to add attribute: " << err << std::endl;
      return 1;
    }

    std::cout << "Added attribute: /testAttr = 42" << std::endl;
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
