// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Example: Basic USDC Crate File Writing
//
// This example demonstrates how to create a simple USD file with:
// - Root prim
// - Child geometry prim
// - Basic attributes with inlined values
//

#include "crate-writer.hh"
#include <iostream>

using namespace tinyusdz;
using namespace tinyusdz::experimental;

// Use tinyusdz::crate namespace explicitly to avoid ambiguity with ::crate
namespace tcrate = tinyusdz::crate;

int main(int argc, char** argv) {
  std::string output_file = "example_output.usdc";

  if (argc > 1) {
    output_file = argv[1];
  }

  std::cout << "Creating USDC file: " << output_file << std::endl;

  // ========================================================================
  // Step 1: Create the writer
  // ========================================================================

  CrateWriter writer(output_file);

  // Optional: Configure options
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  // ========================================================================
  // Step 2: Open the file
  // ========================================================================

  std::string err;
  if (!writer.Open(&err)) {
    std::cerr << "ERROR: Failed to open file: " << err << std::endl;
    return 1;
  }

  std::cout << "File opened successfully" << std::endl;

  // ========================================================================
  // Step 3: Add specs (prims, attributes, etc.)
  // ========================================================================

  // IMPORTANT: Always add PseudoRoot first for valid USD files
  {
    Path pseudoroot_path("/", "");
    tcrate::FieldValuePairVector pseudoroot_fields;

    // PseudoRoot needs at least specifier field
    tcrate::CrateValue specifier_value;
    specifier_value.Set(Specifier::Def);
    pseudoroot_fields.push_back({"specifier", specifier_value});

    if (!writer.AddSpec(pseudoroot_path, SpecType::PseudoRoot, pseudoroot_fields, &err)) {
      std::cerr << "ERROR: Failed to add PseudoRoot: " << err << std::endl;
      return 1;
    }

    std::cout << "Added PseudoRoot" << std::endl;
  }

  // Add root prim: /World
  {
    Path root_path("/World", "");
    tcrate::FieldValuePairVector root_fields;

    // Add specifier field
    tcrate::CrateValue specifier_value;
    specifier_value.Set(Specifier::Def);
    root_fields.push_back({"specifier", specifier_value});

    // Add type name (optional)
    // Note: Currently string/token support is limited, so we'll skip this

    if (!writer.AddSpec(root_path, SpecType::Prim, root_fields, &err)) {
      std::cerr << "ERROR: Failed to add root prim: " << err << std::endl;
      return 1;
    }

    std::cout << "Added prim: /World" << std::endl;
  }

  // Add child prim: /World/Geom
  {
    Path geom_path("/World/Geom", "");
    tcrate::FieldValuePairVector geom_fields;

    tcrate::CrateValue specifier_value;
    specifier_value.Set(Specifier::Def);
    geom_fields.push_back({"specifier", specifier_value});

    if (!writer.AddSpec(geom_path, SpecType::Prim, geom_fields, &err)) {
      std::cerr << "ERROR: Failed to add geom prim: " << err << std::endl;
      return 1;
    }

    std::cout << "Added prim: /World/Geom" << std::endl;
  }

  // Add attribute: /World/Geom.size (int32)
  {
    Path attr_path("/World/Geom", "size");
    tcrate::FieldValuePairVector attr_fields;

    // Add default value (inlined int32)
    tcrate::CrateValue default_value;
    default_value.Set(static_cast<int32_t>(100));
    attr_fields.push_back({"default", default_value});

    if (!writer.AddSpec(attr_path, SpecType::Attribute, attr_fields, &err)) {
      std::cerr << "ERROR: Failed to add attribute: " << err << std::endl;
      return 1;
    }

    std::cout << "Added attribute: /World/Geom.size = 100" << std::endl;
  }

  // Add attribute: /World/Geom.scale (float)
  {
    Path attr_path("/World/Geom", "scale");
    tcrate::FieldValuePairVector attr_fields;

    // Add default value (inlined float)
    tcrate::CrateValue default_value;
    default_value.Set(2.5f);
    attr_fields.push_back({"default", default_value});

    if (!writer.AddSpec(attr_path, SpecType::Attribute, attr_fields, &err)) {
      std::cerr << "ERROR: Failed to add attribute: " << err << std::endl;
      return 1;
    }

    std::cout << "Added attribute: /World/Geom.scale = 2.5" << std::endl;
  }

  // Add attribute: /World/Geom.visible (bool)
  {
    Path attr_path("/World/Geom", "visible");
    tcrate::FieldValuePairVector attr_fields;

    // Add default value (inlined bool)
    tcrate::CrateValue default_value;
    default_value.Set(true);
    attr_fields.push_back({"default", default_value});

    if (!writer.AddSpec(attr_path, SpecType::Attribute, attr_fields, &err)) {
      std::cerr << "ERROR: Failed to add attribute: " << err << std::endl;
      return 1;
    }

    std::cout << "Added attribute: /World/Geom.visible = true" << std::endl;
  }

  // ========================================================================
  // Step 4: Finalize and write the file
  // ========================================================================

  std::cout << "\nFinalizing file..." << std::endl;

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: Failed to finalize: " << err << std::endl;
    return 1;
  }

  std::cout << "File finalized successfully" << std::endl;

  // ========================================================================
  // Step 5: Close the file
  // ========================================================================

  writer.Close();

  std::cout << "\nSUCCESS: Created USDC file: " << output_file << std::endl;
  std::cout << "\nYou can inspect the file with:" << std::endl;
  std::cout << "  usdcat " << output_file << std::endl;
  std::cout << "  usddumpcrate " << output_file << std::endl;
  std::cout << "  usdchecker " << output_file << std::endl;

  return 0;
}
