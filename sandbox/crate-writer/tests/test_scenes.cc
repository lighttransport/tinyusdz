// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Additional USD Test Scenes for Crate Writer
//
// This test creates diverse USD scenes to stress-test the crate-writer
// and verify deduplication, compression, and format correctness.
//

#include "crate-writer.hh"
#include "tinyusdz.hh"
#include <iostream>
#include <vector>
#include <cmath>

using namespace tinyusdz;
using namespace tinyusdz::experimental;
namespace tcrate = tinyusdz::crate;

// Test 1: String Deduplication Test
// Multiple prims with repeated string values
bool CreateStringDedupTest(const std::string& filename) {
  std::cout << "\n[Test 1] String Deduplication Test: " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  // Root
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err);

  // Create 10 prims with same customData strings
  // Should result in high deduplication
  const std::vector<std::string> common_strings = {
    "material_type", "PBR", "metallic", "roughness", "version"
  };

  for (int i = 0; i < 10; i++) {
    Path prim_path("/Prim" + std::to_string(i), "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    // Add customData with repeated strings
    tcrate::CrateValue dict_value;
    value::dict d;
    d[common_strings[0]] = std::string(common_strings[1]); // "material_type" = "PBR"
    d[common_strings[2]] = 0.0f;  // "metallic" = 0.0
    d[common_strings[3]] = 0.5f;  // "roughness" = 0.5
    d[common_strings[4]] = i;     // "version" = i
    dict_value.Set(d);
    fields.push_back({"customData", dict_value});

    writer.AddSpec(prim_path, SpecType::Prim, fields, &err);
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Created: " << filename << " (10 prims, high string deduplication expected)" << std::endl;
  return true;
}

// Test 2: Large Array Compression Test
// Arrays with >100 elements to trigger LZ4 compression
bool CreateLargeArrayTest(const std::string& filename) {
  std::cout << "\n[Test 2] Large Array Compression Test: " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err);

  Path mesh_path("/BigMesh", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Generate 1000 points on a sphere
  std::vector<value::float3> points;
  const int num_points = 1000;
  for (int i = 0; i < num_points; i++) {
    float theta = (i * 2.0f * M_PI) / num_points;
    float phi = acos(1.0f - 2.0f * (i / (float)num_points));
    points.push_back({
      sin(phi) * cos(theta),
      sin(phi) * sin(theta),
      cos(phi)
    });
  }

  tcrate::CrateValue points_value;
  points_value.Set(points);
  fields.push_back({"points", points_value});

  // Generate 1000 indices
  std::vector<int32_t> indices;
  for (int i = 0; i < num_points; i++) {
    indices.push_back(i);
  }

  tcrate::CrateValue indices_value;
  indices_value.Set(indices);
  fields.push_back({"faceVertexIndices", indices_value});

  writer.AddSpec(mesh_path, SpecType::Prim, fields, &err);

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Created: " << filename << " (1000 points, LZ4 compression expected)" << std::endl;
  return true;
}

// Test 3: Deep Hierarchy Test
// Nested prims to test path tree encoding
bool CreateDeepHierarchyTest(const std::string& filename) {
  std::cout << "\n[Test 3] Deep Hierarchy Test: " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err);

  // Create deep nested hierarchy: /World/Region/City/Block/Building/Floor/Room
  std::vector<std::string> hierarchy = {
    "/World",
    "/World/Region",
    "/World/Region/City",
    "/World/Region/City/Block",
    "/World/Region/City/Block/Building",
    "/World/Region/City/Block/Building/Floor",
    "/World/Region/City/Block/Building/Floor/Room"
  };

  for (const auto& path_str : hierarchy) {
    Path prim_path(path_str, "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    writer.AddSpec(prim_path, SpecType::Prim, fields, &err);
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Created: " << filename << " (7-level hierarchy, path tree encoding test)" << std::endl;
  return true;
}

// Test 4: Wide Hierarchy Test
// Many siblings at same level
bool CreateWideHierarchyTest(const std::string& filename) {
  std::cout << "\n[Test 4] Wide Hierarchy Test: " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err);

  // Parent
  Path parent_path("/Collection", "");
  tcrate::FieldValuePairVector parent_fields;
  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  parent_fields.push_back({"specifier", spec_value});
  writer.AddSpec(parent_path, SpecType::Prim, parent_fields, &err);

  // Create 100 child prims
  for (int i = 0; i < 100; i++) {
    Path child_path("/Collection/Item" + std::to_string(i), "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue child_spec;
    child_spec.Set(Specifier::Def);
    fields.push_back({"specifier", child_spec});

    writer.AddSpec(child_path, SpecType::Prim, fields, &err);
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Created: " << filename << " (100 siblings, wide hierarchy test)" << std::endl;
  return true;
}

// Test 5: Mixed Data Types Test
// All supported data types in one scene
bool CreateMixedDataTypesTest(const std::string& filename) {
  std::cout << "\n[Test 5] Mixed Data Types Test: " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err);

  Path prim_path("/DataTypes", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  fields.push_back({"specifier", spec_value});

  // Dictionary with mixed types
  tcrate::CrateValue dict_value;
  value::dict d;

  // Primitive types
  d["bool_val"] = true;
  d["int_val"] = int32_t(42);
  d["float_val"] = 3.14f;
  d["double_val"] = 2.71828;
  d["string_val"] = std::string("Hello USD");

  // Vector types
  d["float2_val"] = value::float2{1.0f, 2.0f};
  d["float3_val"] = value::float3{1.0f, 2.0f, 3.0f};
  d["float4_val"] = value::float4{1.0f, 2.0f, 3.0f, 4.0f};

  dict_value.Set(d);
  fields.push_back({"customData", dict_value});

  // Matrix
  tcrate::CrateValue matrix_value;
  value::matrix4d m;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      m.m[i][j] = (i == j) ? 1.0 : 0.0;  // Identity matrix
    }
  }
  matrix_value.Set(m);
  fields.push_back({"xformOp:transform", matrix_value});

  // Int array
  std::vector<int32_t> int_array = {1, 2, 3, 4, 5};
  tcrate::CrateValue int_array_value;
  int_array_value.Set(int_array);
  fields.push_back({"intArray", int_array_value});

  // Float3 array
  std::vector<value::float3> float3_array = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f}
  };
  tcrate::CrateValue float3_array_value;
  float3_array_value.Set(float3_array);
  fields.push_back({"vectorArray", float3_array_value});

  writer.AddSpec(prim_path, SpecType::Prim, fields, &err);

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Created: " << filename << " (all data types in one prim)" << std::endl;
  return true;
}

// Test 6: Repeated Value Test (for future TimeSamples deduplication)
// Multiple arrays with identical values to test deduplication
bool CreateRepeatedValueTest(const std::string& filename) {
  std::cout << "\n[Test 6] Repeated Value Test (TimeSamples dedup prep): " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err);

  // Create multiple prims with SAME array values
  // This simulates TimeSamples where multiple frames have identical values
  std::vector<value::float3> common_array = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f}
  };

  for (int i = 0; i < 5; i++) {
    Path prim_path("/Frame" + std::to_string(i), "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    // Same array repeated
    tcrate::CrateValue array_value;
    array_value.Set(common_array);
    fields.push_back({"points", array_value});

    writer.AddSpec(prim_path, SpecType::Prim, fields, &err);
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Created: " << filename << " (5 prims with identical arrays)" << std::endl;
  std::cout << "  NOTE: With TimeSamples deduplication, these 5 arrays should reference same data" << std::endl;
  return true;
}

// Test 7: Relationship Network Test
// Complex relationships between multiple prims
bool CreateRelationshipNetworkTest(const std::string& filename) {
  std::cout << "\n[Test 7] Relationship Network Test: " << filename << std::endl;
  std::string err;

  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.version_major = 0;
  opts.version_minor = 8;
  opts.version_patch = 0;
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;
  writer.AddSpec(root_path, SpecType::PseudoRoot, root_fields, &err);

  // Create nodes
  std::vector<std::string> nodes = {"A", "B", "C", "D", "E"};
  for (const auto& node : nodes) {
    Path node_path("/Node" + node, "");
    tcrate::FieldValuePairVector fields;

    tcrate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    fields.push_back({"specifier", spec_value});

    writer.AddSpec(node_path, SpecType::Prim, fields, &err);
  }

  // Create relationships: A→B, A→C, B→D, C→D, D→E
  std::vector<std::pair<std::string, std::vector<std::string>>> relationships = {
    {"A", {"NodeB", "NodeC"}},
    {"B", {"NodeD"}},
    {"C", {"NodeD"}},
    {"D", {"NodeE"}},
    {"E", {}}
  };

  for (const auto& rel : relationships) {
    Path rel_path("/Node" + rel.first, "");

    if (!rel.second.empty()) {
      // Read existing spec to add relationship
      tcrate::FieldValuePairVector fields;

      tcrate::CrateValue spec_value;
      spec_value.Set(Specifier::Def);
      fields.push_back({"specifier", spec_value});

      std::vector<Path> targets;
      for (const auto& target : rel.second) {
        targets.push_back(Path("/" + target, ""));
      }

      tcrate::CrateValue targets_value;
      targets_value.Set(targets);
      fields.push_back({"targets", targets_value});

      // Re-add spec with relationship
      writer.AddSpec(rel_path, SpecType::Prim, fields, &err);
    }
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "ERROR: " << err << std::endl;
    return false;
  }

  writer.Close();
  std::cout << "  ✓ Created: " << filename << " (5 nodes with interconnected relationships)" << std::endl;
  return true;
}

int main() {
  std::cout << "===== USD Crate Writer - Additional Test Scenes =====" << std::endl;
  std::cout << "\nGenerating diverse test scenes to validate crate-writer..." << std::endl;

  bool all_pass = true;

  // Test 1: String deduplication
  if (!CreateStringDedupTest("/tmp/test_string_dedup.usdc")) {
    std::cerr << "✗ String deduplication test FAILED" << std::endl;
    all_pass = false;
  }

  // Test 2: Large array compression
  if (!CreateLargeArrayTest("/tmp/test_large_array.usdc")) {
    std::cerr << "✗ Large array test FAILED" << std::endl;
    all_pass = false;
  }

  // Test 3: Deep hierarchy
  if (!CreateDeepHierarchyTest("/tmp/test_deep_hierarchy.usdc")) {
    std::cerr << "✗ Deep hierarchy test FAILED" << std::endl;
    all_pass = false;
  }

  // Test 4: Wide hierarchy
  if (!CreateWideHierarchyTest("/tmp/test_wide_hierarchy.usdc")) {
    std::cerr << "✗ Wide hierarchy test FAILED" << std::endl;
    all_pass = false;
  }

  // Test 5: Mixed data types
  if (!CreateMixedDataTypesTest("/tmp/test_mixed_types.usdc")) {
    std::cerr << "✗ Mixed data types test FAILED" << std::endl;
    all_pass = false;
  }

  // Test 6: Repeated values (TimeSamples dedup preparation)
  if (!CreateRepeatedValueTest("/tmp/test_repeated_values.usdc")) {
    std::cerr << "✗ Repeated values test FAILED" << std::endl;
    all_pass = false;
  }

  // Test 7: Relationship network
  if (!CreateRelationshipNetworkTest("/tmp/test_relationships.usdc")) {
    std::cerr << "✗ Relationship network test FAILED" << std::endl;
    all_pass = false;
  }

  std::cout << "\n===== Verification Instructions =====" << std::endl;
  std::cout << "\nTo verify generated files:\n" << std::endl;

  std::cout << "1. Binary format analysis:" << std::endl;
  std::cout << "   tusddumpcrate /tmp/test_*.usdc" << std::endl;

  std::cout << "\n2. Convert to ASCII:" << std::endl;
  std::cout << "   tusdcat /tmp/test_string_dedup.usdc" << std::endl;
  std::cout << "   tusdcat /tmp/test_large_array.usdc" << std::endl;
  std::cout << "   tusdcat /tmp/test_deep_hierarchy.usdc" << std::endl;

  std::cout << "\n3. Check file sizes (compression effectiveness):" << std::endl;
  std::cout << "   ls -lh /tmp/test_*.usdc" << std::endl;

  std::cout << "\n4. Verify deduplication:" << std::endl;
  std::cout << "   # String dedup: 10 prims with same strings should have small STRINGS section" << std::endl;
  std::cout << "   # Repeated values: 5 identical arrays should reference same data" << std::endl;

  if (all_pass) {
    std::cout << "\n✓ All test scenes created successfully" << std::endl;
    std::cout << "\nNOTE: Test 6 (Repeated Values) prepares for TimeSamples deduplication." << std::endl;
    std::cout << "      When TimeSamples is implemented, identical values across frames" << std::endl;
    std::cout << "      should reference the same data buffer." << std::endl;
    return 0;
  } else {
    std::cout << "\n✗ Some test scenes failed" << std::endl;
    return 1;
  }
}
