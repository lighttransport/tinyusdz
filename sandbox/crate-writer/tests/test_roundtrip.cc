// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Round-trip Test: Write USDC → Read with TinyUSDZ → Verify
//
// This test verifies that files written by the crate-writer can be
// successfully read back by TinyUSDZ without errors.
//

#include "crate-writer.hh"
#include "tinyusdz.hh"
#include <iostream>
#include <cstdio>

using namespace tinyusdz;
using namespace tinyusdz::experimental;
namespace tcrate = tinyusdz::crate;

bool TestRoundTrip(const std::string& test_name,
                   const std::function<bool(CrateWriter&, std::string*)>& write_func) {
  std::cout << "\n=== Testing: " << test_name << " ===" << std::endl;

  std::string temp_file = "/tmp/test_" + test_name + ".usdc";
  std::string err;

  // Step 1: Write the file
  std::cout << "Step 1: Writing file..." << std::endl;
  {
    CrateWriter writer(temp_file);
    CrateWriter::Options opts;
    opts.version_major = 0;
    opts.version_minor = 8;
    opts.version_patch = 0;
    opts.enable_deduplication = true;
    writer.SetOptions(opts);

    if (!writer.Open(&err)) {
      std::cerr << "ERROR: Failed to open file: " << err << std::endl;
      return false;
    }

    if (!write_func(writer, &err)) {
      std::cerr << "ERROR: Write function failed: " << err << std::endl;
      return false;
    }

    if (!writer.Finalize(&err)) {
      std::cerr << "ERROR: Failed to finalize: " << err << std::endl;
      return false;
    }

    writer.Close();
    std::cout << "  ✓ File written successfully" << std::endl;
  }

  // Step 2: Read the file back with TinyUSDZ
  std::cout << "Step 2: Reading file with TinyUSDZ..." << std::endl;
  {
    Stage stage;
    std::string warn, read_err;

    bool ret = tinyusdz::LoadUSDCFromFile(temp_file, &stage, &warn, &read_err);

    if (!warn.empty()) {
      std::cout << "  WARNING: " << warn << std::endl;
    }

    if (!ret || !read_err.empty()) {
      std::cerr << "ERROR: Failed to read file: " << read_err << std::endl;
      return false;
    }

    std::cout << "  ✓ File read successfully" << std::endl;
    std::cout << "  - Root prims: " << stage.root_prims().size() << std::endl;
  }

  // Clean up
  std::remove(temp_file.c_str());

  std::cout << "✓ Round-trip test PASSED: " << test_name << std::endl;
  return true;
}

// Test 1: Simple prim with inline values
bool Test_SimplePrim(CrateWriter& writer, std::string* err) {
  // Add root spec
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  // Add a simple prim
  Path prim_path("/TestPrim", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  return writer.AddSpec(prim_path, SpecType::Prim, fields, err);
}

// Test 2: Prim with relationship (Path array)
bool Test_Relationship(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  Path prim_path("/PrimWithRel", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Add relationship with path array
  tcrate::CrateValue path_array_value;
  std::vector<Path> targets;
  targets.push_back(Path("/Target1", ""));
  targets.push_back(Path("/Target2", ""));
  path_array_value.Set(targets);
  fields.push_back({"testRel.targetPaths", path_array_value});

  return writer.AddSpec(prim_path, SpecType::Prim, fields, err);
}

// Test 3: Prim with arrays (int32, float3)
bool Test_Arrays(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  Path prim_path("/PrimWithArrays", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Add int array
  tcrate::CrateValue int_array_value;
  std::vector<int32_t> int_vals = {1, 2, 3, 4, 5};
  int_array_value.Set(int_vals);
  fields.push_back({"testIntArray", int_array_value});

  // Add float3 array
  tcrate::CrateValue float3_array_value;
  std::vector<value::float3> float3_vals = {
    {1.0f, 2.0f, 3.0f},
    {4.0f, 5.0f, 6.0f}
  };
  float3_array_value.Set(float3_vals);
  fields.push_back({"testVec3Array", float3_array_value});

  return writer.AddSpec(prim_path, SpecType::Prim, fields, err);
}

int main() {
  std::cout << "===== USD Crate Writer Round-Trip Tests =====" << std::endl;

  int passed = 0;
  int total = 0;

  // Run tests
  total++; if (TestRoundTrip("SimplePrim", Test_SimplePrim)) passed++;
  total++; if (TestRoundTrip("Relationship", Test_Relationship)) passed++;
  total++; if (TestRoundTrip("Arrays", Test_Arrays)) passed++;

  // Summary
  std::cout << "\n===== Test Summary =====" << std::endl;
  std::cout << "Passed: " << passed << " / " << total << std::endl;

  if (passed == total) {
    std::cout << "\n✓ All tests PASSED" << std::endl;
    return 0;
  } else {
    std::cout << "\n✗ Some tests FAILED" << std::endl;
    return 1;
  }
}
