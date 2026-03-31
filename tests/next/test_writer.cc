// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Writer Test

#include <iostream>
#include <sstream>
#include <cassert>

#include "next/stage/stage.hh"
#include "next/layer/layer.hh"
#include "next/types/value.hh"
#include "next/writer/value-printer.hh"
#include "next/writer/prim-printer.hh"
#include "next/writer/usda-writer.hh"

using namespace tinyusdz::next;

// Helper to check if string contains substring
bool contains(const std::string& str, const std::string& substr) {
  return str.find(substr) != std::string::npos;
}

void test_value_printer() {
  std::cout << "Testing value-printer...\n";

  // Test scalar values
  {
    Value v = Value(42);
    std::string s = PrintValue(v);
    assert(s == "42");
    std::cout << "  Int: " << s << "\n";
  }

  {
    Value v = Value(3.14159f);
    std::string s = PrintValue(v);
    assert(contains(s, "3.14159"));
    std::cout << "  Float: " << s << "\n";
  }

  {
    Value v = Value(true);
    std::string s = PrintValue(v);
    assert(s == "true");
    std::cout << "  Bool: " << s << "\n";
  }

  // Test vector values
  {
    Value v = Value::MakeFloat3(1.0f, 2.0f, 3.0f);
    std::string s = PrintValue(v);
    assert(contains(s, "("));
    assert(contains(s, "1"));
    assert(contains(s, "2"));
    assert(contains(s, "3"));
    std::cout << "  Float3: " << s << "\n";
  }

  // Test string values
  {
    Value v = Value(std::string("hello world"));
    std::string s = PrintValue(v);
    assert(contains(s, "\"hello world\""));
    std::cout << "  String: " << s << "\n";
  }

  // Test arrays
  {
    std::vector<float> arr = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    Value v = Value::MakeFloatArray(arr);
    std::string s = PrintValue(v);
    assert(contains(s, "["));
    assert(contains(s, "1"));
    assert(contains(s, "5"));
    std::cout << "  Float array: " << s << "\n";
  }

  std::cout << "  value-printer tests passed!\n\n";
}

void test_layer_printer() {
  std::cout << "Testing prim-printer with Layer...\n";

  // Create a simple layer
  Layer layer;
  LayerBuilder builder(layer);

  // Set layer metadata
  layer.meta().defaultPrim = "World";
  layer.meta().upAxis = "Y";

  // Create root prim
  builder.begin_prim("World", "Xform");
  builder.end_prim();

  // Create child mesh
  builder.begin_prim("Cube", "Mesh");
  builder.add_property("extent", Value::MakeFloat3(-1, -1, -1));

  std::vector<float> points = {
    -1, -1, -1,
     1, -1, -1,
     1,  1, -1,
    -1,  1, -1,
  };
  builder.add_property("points", Value::MakeFloat3Array(points));
  builder.end_prim();

  builder.finalize();

  // Print the layer
  std::string output = PrintLayer(layer);
  std::cout << "Layer output:\n" << output << "\n";

  assert(contains(output, "#usda 1.0"));
  assert(contains(output, "defaultPrim"));
  assert(contains(output, "World"));
  assert(contains(output, "Mesh"));
  assert(contains(output, "Cube"));
  assert(contains(output, "extent"));
  assert(contains(output, "points"));

  std::cout << "  prim-printer tests passed!\n\n";
}

void test_stage_writer() {
  std::cout << "Testing usda-writer with Stage...\n";

  // Create a Stage using StageBuilder
  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("Root");
  stage_builder.SetUpAxis("Y");
  stage_builder.SetMetersPerUnit(0.01);

  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  // Create root prim
  layer.begin_prim("Root", "Xform");
  layer.end_prim();

  // Create materials
  layer.begin_prim("Materials", "Scope");
  layer.end_prim();

  layer.begin_prim("Metal", "Material");
  layer.add_property("inputs:roughness", Value(0.2f));
  layer.end_prim();

  layer.finalize();

  Stage stage = stage_builder.Build();

  // Write to string
  USDAWriteOptions opts;
  opts.float_precision = 3;
  std::string output = WriteUSDAToString(stage, opts);

  std::cout << "Stage USDA output:\n" << output << "\n";

  assert(contains(output, "#usda 1.0"));
  assert(contains(output, "defaultPrim"));
  assert(contains(output, "Root"));
  assert(contains(output, "Materials"));
  assert(contains(output, "Metal"));
  assert(contains(output, "Material"));
  assert(contains(output, "roughness"));

  // Test write to file
  USDAWriteResult result = WriteUSDAToFile("/tmp/test_output.usda", stage, opts);
  assert(result.success);
  assert(result.bytes_written > 0);
  std::cout << "  Wrote " << result.bytes_written << " bytes to /tmp/test_output.usda\n";

  std::cout << "  usda-writer tests passed!\n\n";
}

void test_time_samples() {
  std::cout << "Testing time samples...\n";

  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("Animated");
  stage_builder.SetStartTimeCode(0.0);
  stage_builder.SetEndTimeCode(100.0);
  stage_builder.SetTimeCodesPerSecond(24.0);

  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  // Create animated prim
  layer.begin_prim("Animated", "Xform");

  // Add time samples for translation
  layer.add_time_sample("xformOp:translate", 0.0, Value::MakeFloat3(0, 0, 0));
  layer.add_time_sample("xformOp:translate", 50.0, Value::MakeFloat3(10, 5, 0));
  layer.add_time_sample("xformOp:translate", 100.0, Value::MakeFloat3(20, 0, 0));

  layer.end_prim();
  layer.finalize();

  Stage stage = stage_builder.Build();

  // Write to USDA and check output
  std::string output = WriteUSDAToString(stage);
  std::cout << "Time samples output:\n" << output << "\n";

  assert(contains(output, ".timeSamples"));
  assert(contains(output, "0:"));
  assert(contains(output, "50:"));
  assert(contains(output, "100:"));

  // Test GetValueAtTime via UsdPrim
  auto prims = stage.GetRootPrims();
  assert(!prims.empty());

  const UsdPrim& prim = prims[0];
  assert(prim.HasTimeSamples("xformOp:translate"));

  auto times = prim.GetTimeSampleTimes("xformOp:translate");
  assert(times.size() == 3);
  assert(times[0] == 0.0);
  assert(times[1] == 50.0);
  assert(times[2] == 100.0);

  // Test GetValueAtTime
  const Value* val_at_0 = prim.GetValueAtTime("xformOp:translate", 0.0);
  assert(val_at_0 != nullptr);
  const float* v0 = val_at_0->as_float3();
  assert(v0 != nullptr);
  assert(v0[0] == 0.0f && v0[1] == 0.0f && v0[2] == 0.0f);

  const Value* val_at_50 = prim.GetValueAtTime("xformOp:translate", 50.0);
  assert(val_at_50 != nullptr);
  const float* v50 = val_at_50->as_float3();
  assert(v50 != nullptr);
  assert(v50[0] == 10.0f && v50[1] == 5.0f && v50[2] == 0.0f);

  // Test interpolation (held - should return previous sample)
  const Value* val_at_25 = prim.GetValueAtTime("xformOp:translate", 25.0);
  assert(val_at_25 != nullptr);
  const float* v25 = val_at_25->as_float3();
  assert(v25 != nullptr);
  // Should be same as t=0 (held interpolation)
  assert(v25[0] == 0.0f);

  std::cout << "  time samples test passed!\n\n";
}

void test_roundtrip() {
  std::cout << "Testing USDA roundtrip (write -> manual inspection)...\n";

  // Create a more complex stage
  StageBuilder stage_builder;
  stage_builder.SetDefaultPrim("World");
  stage_builder.SetUpAxis("Z");
  stage_builder.SetTimeCodesPerSecond(30.0);
  stage_builder.SetStartTimeCode(0.0);
  stage_builder.SetEndTimeCode(100.0);

  LayerBuilder& layer = stage_builder.GetLayerBuilder();

  // Root
  layer.begin_prim("World", "Xform");
  layer.end_prim();

  // Character
  layer.begin_prim("Character", "Xform");
  layer.end_prim();

  // Body mesh
  layer.begin_prim("Body", "Mesh");

  // Add properties
  std::vector<int> faceVertexCounts = {4, 4, 4, 4, 4, 4};
  std::vector<int> faceVertexIndices = {
    0, 1, 2, 3,
    4, 5, 6, 7,
    0, 4, 5, 1,
    1, 5, 6, 2,
    2, 6, 7, 3,
    3, 7, 4, 0
  };
  std::vector<float> points = {
    -1, -1, -1,
     1, -1, -1,
     1,  1, -1,
    -1,  1, -1,
    -1, -1,  1,
     1, -1,  1,
     1,  1,  1,
    -1,  1,  1
  };

  layer.add_property("faceVertexCounts", Value::MakeIntArray(faceVertexCounts));
  layer.add_property("faceVertexIndices", Value::MakeIntArray(faceVertexIndices));
  layer.add_property("points", Value::MakeFloat3Array(points));

  layer.set_active(true);
  layer.end_prim();

  layer.finalize();

  Stage stage = stage_builder.Build();

  // Write with different options
  USDAWriteOptions opts;
  opts.compact = false;
  opts.sort_properties = true;
  opts.float_precision = 4;

  std::string output = WriteUSDAToString(stage, opts);
  std::cout << "Complex Stage USDA output:\n" << output << "\n";

  // Verify structure
  assert(contains(output, "World"));
  assert(contains(output, "Character"));
  assert(contains(output, "Body"));
  assert(contains(output, "Mesh"));
  assert(contains(output, "faceVertexCounts"));
  assert(contains(output, "faceVertexIndices"));
  assert(contains(output, "points"));
  assert(contains(output, "upAxis = \"Z\""));
  assert(contains(output, "timeCodesPerSecond"));

  std::cout << "  roundtrip test passed!\n\n";
}

int main() {
  std::cout << "=== TinyUSDZ Next Writer Tests ===\n\n";

  try {
    test_value_printer();
    test_layer_printer();
    test_stage_writer();
    test_time_samples();
    test_roundtrip();

    std::cout << "=== All writer tests passed! ===\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << "\n";
    return 1;
  }
}
