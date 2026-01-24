// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Test deduplication feature compatibility with OpenUSD
// This test creates a USD file with heavily deduplicated TimeSamples
// and verifies it can be read correctly by OpenUSD

#include <iostream>
#include <string>
#include <cmath>

#include "../../../src/tinyusdz.hh"
#include "../../../src/prim-types.hh"
#include "../../../src/value-types.hh"
#include "../../../src/timesamples.hh"
#include "../../../src/primvar.hh"
#include "../include/crate-writer.hh"

using namespace tinyusdz;
using namespace tinyusdz::experimental;

int main(int argc, char** argv) {
  std::string output_file = "test_dedup_openusd_verify.usdc";

  if (argc > 1) {
    output_file = argv[1];
  }

  std::cout << "=== OpenUSD Deduplication Verification Test ===" << std::endl;
  std::cout << "Creating: " << output_file << std::endl;

  // Create a Stage with multiple prims demonstrating deduplication
  Stage stage;

  // Set layer metadata
  stage.metas().timeCodesPerSecond = TypedAttributeWithFallback<double>(24.0);
  stage.metas().framesPerSecond = TypedAttributeWithFallback<double>(24.0);
  stage.metas().startTimeCode = TypedAttributeWithFallback<double>(1.0);
  stage.metas().endTimeCode = TypedAttributeWithFallback<double>(100.0);

  // ===== Test 1: Float array deduplication =====
  {
    Xform xform;
    xform.name = "FloatArrayTest";
    xform.spec = Specifier::Def;

    Attribute attr;
    attr.set_type_name("float[]");
    attr.set_var(Variability::Varying);

    value::TimeSamples ts;

    // Create repeated array pattern
    std::vector<float> array1 = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> array2 = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};

    // Frames 1-50: array1, Frames 51-100: array2
    for (int frame = 1; frame <= 100; frame++) {
      double time = static_cast<double>(frame);
      if (frame <= 50) {
        ts.add_sample(time, value::Value(array1));
      } else {
        ts.add_sample(time, value::Value(array2));
      }
    }

    primvar::PrimVar prim_var;
    prim_var._ts = ts;
    prim_var._value = value::Value(array1);
    attr.set_var(prim_var);

    // Add property using insert
    xform.props.insert(std::make_pair("floatArrayAttr", Property(attr, false)));

    // Create Prim with element name
    Prim prim("FloatArrayTest", xform);
    stage.root_prims().emplace_back(prim);

    std::cout << "  Added FloatArrayTest: 100 frames, 2 unique arrays" << std::endl;
  }

  // ===== Test 2: String array deduplication =====
  {
    Xform xform;
    xform.name = "StringArrayTest";
    xform.spec = Specifier::Def;

    Attribute attr;
    attr.set_type_name("string[]");
    attr.set_var(Variability::Varying);

    value::TimeSamples ts;

    std::vector<std::string> metadata1 = {"author", "john", "version", "1.0"};
    std::vector<std::string> metadata2 = {"author", "jane", "version", "2.0"};

    for (int frame = 1; frame <= 60; frame++) {
      double time = static_cast<double>(frame);
      if (frame <= 40) {
        ts.add_sample(time, value::Value(metadata1));
      } else {
        ts.add_sample(time, value::Value(metadata2));
      }
    }

    primvar::PrimVar prim_var;
    prim_var._ts = ts;
    prim_var._value = value::Value(metadata1);
    attr.set_var(prim_var);

    // Add property using insert
    xform.props.insert(std::make_pair("stringArrayAttr", Property(attr, false)));

    // Create Prim with element name
    Prim prim("StringArrayTest", xform);
    stage.root_prims().emplace_back(prim);

    std::cout << "  Added StringArrayTest: 60 frames, 2 unique string arrays" << std::endl;
  }

  // ===== Test 3: Matrix4d deduplication =====
  {
    Xform xform;
    xform.name = "MatrixTest";
    xform.spec = Specifier::Def;

    Attribute attr;
    attr.set_type_name("matrix4d");
    attr.set_var(Variability::Varying);

    value::TimeSamples ts;

    // Identity matrix
    value::matrix4d identity;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        identity.m[i][j] = (i == j) ? 1.0 : 0.0;
      }
    }

    // Scale matrix (2x)
    value::matrix4d scale2x;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        scale2x.m[i][j] = (i == j && i < 3) ? 2.0 : ((i == j) ? 1.0 : 0.0);
      }
    }

    // Repeat identity for 70 frames, scale2x for 30 frames
    for (int frame = 1; frame <= 100; frame++) {
      double time = static_cast<double>(frame);
      if (frame <= 70) {
        ts.add_sample(time, value::Value(identity));
      } else {
        ts.add_sample(time, value::Value(scale2x));
      }
    }

    primvar::PrimVar prim_var;
    prim_var._ts = ts;
    prim_var._value = value::Value(identity);
    attr.set_var(prim_var);

    // Add property using insert
    xform.props.insert(std::make_pair("xformMatrix", Property(attr, false)));

    // Create Prim with element name
    Prim prim("MatrixTest", xform);
    stage.root_prims().emplace_back(prim);

    std::cout << "  Added MatrixTest: 100 frames, 2 unique matrices" << std::endl;
  }

  // ===== Test 4: Int array deduplication with pattern =====
  {
    Xform xform;
    xform.name = "IntArrayPattern";
    xform.spec = Specifier::Def;

    Attribute attr;
    attr.set_type_name("int[]");
    attr.set_var(Variability::Varying);

    value::TimeSamples ts;

    std::vector<int32_t> pattern_a = {100, 200, 300};
    std::vector<int32_t> pattern_b = {400, 500, 600};
    std::vector<int32_t> pattern_c = {700, 800, 900};

    // ABCABC... pattern
    for (int frame = 1; frame <= 90; frame++) {
      double time = static_cast<double>(frame);
      int pattern_idx = (frame - 1) % 3;
      if (pattern_idx == 0) {
        ts.add_sample(time, value::Value(pattern_a));
      } else if (pattern_idx == 1) {
        ts.add_sample(time, value::Value(pattern_b));
      } else {
        ts.add_sample(time, value::Value(pattern_c));
      }
    }

    primvar::PrimVar prim_var;
    prim_var._ts = ts;
    prim_var._value = value::Value(pattern_a);
    attr.set_var(prim_var);

    // Add property using insert
    xform.props.insert(std::make_pair("intArrayAttr", Property(attr, false)));

    // Create Prim with element name
    Prim prim("IntArrayPattern", xform);
    stage.root_prims().emplace_back(prim);

    std::cout << "  Added IntArrayPattern: 90 frames, 3 unique arrays in ABC pattern" << std::endl;
  }

  // Write with deduplication ENABLED
  std::string err;
  CrateWriter writer(output_file);
  CrateWriter::Options opts;
  opts.enable_deduplication = true;  // CRITICAL: Enable dedup
  writer.SetOptions(opts);

  if (!writer.Open(&err)) {
    std::cerr << "Failed to open writer: " << err << std::endl;
    return 1;
  }

  if (!writer.ConvertStageToSpecs(stage, &err)) {
    std::cerr << "Failed to convert stage: " << err << std::endl;
    return 1;
  }

  if (!writer.Finalize(&err)) {
    std::cerr << "Failed to finalize: " << err << std::endl;
    return 1;
  }

  writer.Close();

  std::cout << "\n✓ File written successfully with deduplication enabled" << std::endl;
  std::cout << "  Output: " << output_file << std::endl;
  std::cout << "\nNext steps:" << std::endl;
  std::cout << "  1. Run: usdcat " << output_file << " (verify file can be read)" << std::endl;
  std::cout << "  2. Run: python verify_dedup_openusd.py " << output_file << std::endl;

  return 0;
}
