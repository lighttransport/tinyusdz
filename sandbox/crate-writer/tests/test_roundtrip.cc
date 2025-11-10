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

// Test 4: Xform with transform matrix
bool Test_XformMatrix(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  Path xform_path("/Xform", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Add transform matrix
  tcrate::CrateValue matrix_value;
  value::matrix4d transform;
  // Identity matrix
  transform.m[0][0] = 1.0; transform.m[0][1] = 0.0; transform.m[0][2] = 0.0; transform.m[0][3] = 0.0;
  transform.m[1][0] = 0.0; transform.m[1][1] = 1.0; transform.m[1][2] = 0.0; transform.m[1][3] = 0.0;
  transform.m[2][0] = 0.0; transform.m[2][1] = 0.0; transform.m[2][2] = 1.0; transform.m[2][3] = 0.0;
  transform.m[3][0] = 0.0; transform.m[3][1] = 5.0; transform.m[3][2] = 0.0; transform.m[3][3] = 1.0;
  matrix_value.Set(transform);
  fields.push_back({"xformOp:transform", matrix_value});

  return writer.AddSpec(xform_path, SpecType::Prim, fields, err);
}

// Test 5: Prim with double and vector types
bool Test_VectorTypes(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  Path prim_path("/VectorPrim", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Add double value
  tcrate::CrateValue double_value;
  double_value.Set(3.14159265359);
  fields.push_back({"testDouble", double_value});

  // Add float2
  tcrate::CrateValue float2_value;
  value::float2 f2 = {1.0f, 2.0f};
  float2_value.Set(f2);
  fields.push_back({"testFloat2", float2_value});

  // Add float4
  tcrate::CrateValue float4_value;
  value::float4 f4 = {1.0f, 2.0f, 3.0f, 4.0f};
  float4_value.Set(f4);
  fields.push_back({"testFloat4", float4_value});

  return writer.AddSpec(prim_path, SpecType::Prim, fields, err);
}

// Test 6: String and token types
bool Test_StringTypes(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  Path prim_path("/StringPrim", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Add string
  tcrate::CrateValue string_value;
  string_value.Set(std::string("Hello, USD!"));
  fields.push_back({"testString", string_value});

  // Add token
  tcrate::CrateValue token_value;
  token_value.Set(value::token("myToken"));
  fields.push_back({"testToken", token_value});

  // Add AssetPath
  tcrate::CrateValue asset_value;
  asset_value.Set(value::AssetPath("textures/albedo.png"));
  fields.push_back({"testAsset", asset_value});

  return writer.AddSpec(prim_path, SpecType::Prim, fields, err);
}

// Test 7: Large arrays (test compression)
bool Test_LargeArrays(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  Path prim_path("/LargeArrayPrim", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Add large int array (should trigger compression)
  tcrate::CrateValue int_array_value;
  std::vector<int32_t> int_vals;
  for (int i = 0; i < 100; i++) {
    int_vals.push_back(i * 10);  // Sequential values compress well
  }
  int_array_value.Set(int_vals);
  fields.push_back({"largeIntArray", int_array_value});

  // Add large float array
  tcrate::CrateValue float_array_value;
  std::vector<float> float_vals;
  for (int i = 0; i < 100; i++) {
    float_vals.push_back(static_cast<float>(i) * 0.1f);
  }
  float_array_value.Set(float_vals);
  fields.push_back({"largeFloatArray", float_array_value});

  return writer.AddSpec(prim_path, SpecType::Prim, fields, err);
}

// Test 8: Dictionary type
bool Test_Dictionary(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  Path prim_path("/DictPrim", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Add dictionary
  tcrate::CrateValue dict_value;
  value::dict d;
  d["name"] = std::string("TestDict");
  d["version"] = int32_t(1);
  d["enabled"] = true;
  dict_value.Set(d);
  fields.push_back({"customData", dict_value});

  return writer.AddSpec(prim_path, SpecType::Prim, fields, err);
}

// Test 9: Multiple prims (hierarchy)
bool Test_Hierarchy(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  // Add parent prim
  Path parent_path("/Parent", "");
  tcrate::FieldValuePairVector parent_fields;
  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  parent_fields.push_back({"specifier", spec_value});
  if (!writer.AddSpec(parent_path, SpecType::Prim, parent_fields, err)) {
    return false;
  }

  // Add child prim
  Path child_path("/Parent/Child", "");
  tcrate::FieldValuePairVector child_fields;
  tcrate::CrateValue child_spec_value;
  child_spec_value.Set(Specifier::Def);
  child_fields.push_back({"specifier", child_spec_value});
  if (!writer.AddSpec(child_path, SpecType::Prim, child_fields, err)) {
    return false;
  }

  // Add grandchild prim
  Path grandchild_path("/Parent/Child/Grandchild", "");
  tcrate::FieldValuePairVector grandchild_fields;
  tcrate::CrateValue grandchild_spec_value;
  grandchild_spec_value.Set(Specifier::Def);
  grandchild_fields.push_back({"specifier", grandchild_spec_value});

  return writer.AddSpec(grandchild_path, SpecType::Prim, grandchild_fields, err);
}

// Test 10: Token array
bool Test_TokenArray(CrateWriter& writer, std::string* err) {
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  if (!writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  Path prim_path("/TokenArrayPrim", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Add token array
  tcrate::CrateValue token_array_value;
  std::vector<value::token> tokens;
  tokens.push_back(value::token("token1"));
  tokens.push_back(value::token("token2"));
  tokens.push_back(value::token("token3"));
  token_array_value.Set(tokens);
  fields.push_back({"testTokenArray", token_array_value});

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
  total++; if (TestRoundTrip("XformMatrix", Test_XformMatrix)) passed++;
  total++; if (TestRoundTrip("VectorTypes", Test_VectorTypes)) passed++;
  total++; if (TestRoundTrip("StringTypes", Test_StringTypes)) passed++;
  total++; if (TestRoundTrip("LargeArrays", Test_LargeArrays)) passed++;
  total++; if (TestRoundTrip("Dictionary", Test_Dictionary)) passed++;
  total++; if (TestRoundTrip("Hierarchy", Test_Hierarchy)) passed++;
  total++; if (TestRoundTrip("TokenArray", Test_TokenArray)) passed++;

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
