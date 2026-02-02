// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Test for Layer/PrimSpec functionality

#include <iostream>
#include <cassert>
#include <cmath>

#include "next/layer/layer.hh"
#include "next/layer/prim-spec.hh"
#include "next/layer/property-index.hh"

using namespace tinyusdz::next;

void test_prop_name_table() {
  std::cout << "Testing PropNameTable..." << std::endl;

  PropNameTable& table = GetPropNameTable();

  // Common names should be pre-registered
  PropNameId points_id = table.find("points");
  assert(points_id.is_valid() && "points should be pre-registered");
  assert(table.id_points == points_id && "id_points should match find result");

  // Intern new name
  PropNameId custom_id = table.intern("myCustomProp");
  assert(custom_id.is_valid() && "custom prop should be interned");
  assert(table.get(custom_id) == "myCustomProp" && "should get back same name");

  // Re-interning returns same ID
  PropNameId custom_id2 = table.intern("myCustomProp");
  assert(custom_id == custom_id2 && "re-intern should return same ID");

  std::cout << "  PropNameTable: PASSED" << std::endl;
}

void test_prop_index() {
  std::cout << "Testing PropIndex..." << std::endl;

  PropIndex index;
  PropNameTable& table = GetPropNameTable();

  // Add some slots
  PropSlot slot1;
  slot1.name_id = table.intern("prop1");
  slot1.value_offset = 0;
  slot1.value_type = 0;
  slot1.flags = 0;
  index.add(slot1);

  PropSlot slot2;
  slot2.name_id = table.intern("prop2");
  slot2.value_offset = 1;
  slot2.value_type = 0;
  slot2.flags = PropSlot::kFlagCustom;
  index.add(slot2);

  assert(index.size() == 2 && "should have 2 slots");

  // Find by name
  const PropSlot* found = index.find("prop1");
  assert(found != nullptr && "should find prop1");
  assert(found->value_offset == 0 && "should have correct offset");

  found = index.find("prop2");
  assert(found != nullptr && "should find prop2");
  assert(found->is_custom() && "prop2 should be custom");

  // Sort for binary search
  index.sort();
  assert(index.is_sorted() && "should be sorted");

  // Find after sort
  found = index.find("prop1");
  assert(found != nullptr && "should still find prop1 after sort");

  std::cout << "  PropIndex: PASSED" << std::endl;
}

void test_prim_spec() {
  std::cout << "Testing PrimSpec..." << std::endl;

  PrimSpec prim("myPrim", "Mesh");

  assert(prim.name() == "myPrim" && "name should match");
  assert(prim.type_name() == "Mesh" && "type_name should match");
  assert(prim.specifier() == PrimSpecifier::Def && "default specifier should be Def");

  // Add properties (flat float array for float3[])
  prim.add_property("points", Value::MakeFloat3Array(std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0}));
  prim.add_property("faceVertexCounts", Value::MakeIntArray(std::vector<int32_t>{3}));
  prim.add_property("faceVertexIndices", Value::MakeIntArray(std::vector<int32_t>{0, 1, 2}));

  // Finalize for binary search
  prim.finalize_properties();

  // Lookup properties
  const PropSlot* slot = prim.property("points");
  assert(slot != nullptr && "should find points property");

  const Value* val = prim.property_value("points");
  assert(val != nullptr && "should get points value");
  assert(val->is_array() && "points should be array");

  slot = prim.property("faceVertexCounts");
  assert(slot != nullptr && "should find faceVertexCounts");

  // Non-existent property
  slot = prim.property("nonExistent");
  assert(slot == nullptr && "should not find non-existent property");

  std::cout << "  PrimSpec: PASSED" << std::endl;
}

void test_layer_builder() {
  std::cout << "Testing LayerBuilder..." << std::endl;

  Layer layer;
  LayerBuilder builder(layer);

  // Build a simple hierarchy:
  // /World (Xform)
  //   /Mesh (Mesh)
  //   /Light (SphereLight)

  builder.begin_prim("World", "Xform");
  builder.add_property("xformOpOrder", Value::MakeToken("xformOp:translate"));  // Single token for simplicity

    builder.begin_prim("Mesh", "Mesh");
    builder.add_property("points", Value::MakeFloat3Array(std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0}));
    builder.add_property("faceVertexCounts", Value::MakeIntArray(std::vector<int32_t>{3}));
    builder.add_property("faceVertexIndices", Value::MakeIntArray(std::vector<int32_t>{0, 1, 2}));
    builder.end_prim();

    builder.begin_prim("Light", "SphereLight");
    builder.add_property("intensity", Value(100.0f));
    builder.end_prim();

  builder.end_prim();

  builder.finalize();

  // Verify structure
  assert(layer.prim_count() == 3 && "should have 3 prims");
  assert(layer.root_indices().size() == 1 && "should have 1 root");

  // Check paths
  const PrimSpec* world = layer.prim_at_path("/World");
  assert(world != nullptr && "should find /World");
  assert(world->type_name() == "Xform" && "World should be Xform");
  assert(world->child_count() == 2 && "World should have 2 children");

  const PrimSpec* mesh = layer.prim_at_path("/World/Mesh");
  assert(mesh != nullptr && "should find /World/Mesh");
  assert(mesh->type_name() == "Mesh" && "should be Mesh type");

  const PrimSpec* light = layer.prim_at_path("/World/Light");
  assert(light != nullptr && "should find /World/Light");
  assert(light->type_name() == "SphereLight" && "should be SphereLight type");

  // Check properties
  const Value* points = mesh->property_value("points");
  assert(points != nullptr && "mesh should have points");

  const Value* intensity = light->property_value("intensity");
  assert(intensity != nullptr && "light should have intensity");
  assert(intensity->as_float() != nullptr && "intensity should be float");
  assert(*intensity->as_float() == 100.0f && "intensity should be 100");

  std::cout << "  LayerBuilder: PASSED" << std::endl;
}

void test_layer_stats() {
  std::cout << "Testing Layer stats..." << std::endl;

  Layer layer;
  LayerBuilder builder(layer);

  builder.begin_prim("Root", "Xform");
  for (int i = 0; i < 10; ++i) {
    builder.begin_prim("Child" + std::to_string(i), "Mesh");
    builder.add_property("points", Value::MakeFloat3Array(std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0}));
    builder.end_prim();
  }
  builder.end_prim();
  builder.finalize();

  Layer::Stats stats = layer.stats();
  assert(stats.prim_count == 11 && "should have 11 prims (1 root + 10 children)");
  assert(stats.root_count == 1 && "should have 1 root");
  assert(stats.total_properties == 10 && "should have 10 properties (1 per child)");
  assert(stats.memory_bytes > 0 && "memory usage should be positive");

  std::cout << "  Layer stats: PASSED" << std::endl;
  std::cout << "  Memory usage: " << stats.memory_bytes << " bytes" << std::endl;
}

void test_metadata() {
  std::cout << "Testing metadata..." << std::endl;

  Layer layer;
  layer.meta().defaultPrim = "World";
  layer.meta().upAxis = "Z";
  layer.meta().metersPerUnit = 1.0;
  layer.meta().timeCodesPerSecond = 30.0;
  layer.meta().subLayers.push_back("./sublayer.usda");

  assert(layer.meta().defaultPrim == "World" && "defaultPrim should match");
  assert(layer.meta().upAxis == "Z" && "upAxis should match");
  assert(layer.meta().metersPerUnit == 1.0 && "metersPerUnit should match");
  assert(layer.meta().subLayers.size() == 1 && "should have 1 sublayer");

  std::cout << "  Metadata: PASSED" << std::endl;
}

void test_interpolation() {
  std::cout << "Testing time sample interpolation..." << std::endl;

  // Test scalar interpolation
  {
    PrimSpec prim("animFloat", "Xform");
    PropNameTable& table = GetPropNameTable();
    PropNameId opacity_id = table.intern("opacity");

    // Add time samples: 0.0 -> 0.0, 1.0 -> 1.0
    prim.add_time_sample(opacity_id, 0.0, Value(0.0f));
    prim.add_time_sample(opacity_id, 1.0, Value(1.0f));

    // Test exact match
    auto result = prim.interpolate_time_sample(opacity_id, 0.0);
    assert(result.success && "should interpolate at t=0");
    assert(!result.interpolated && "should be exact match");
    assert(*result.value.as_float() == 0.0f && "should be 0.0");

    // Test linear interpolation at t=0.5
    result = prim.interpolate_time_sample(opacity_id, 0.5);
    assert(result.success && "should interpolate at t=0.5");
    assert(result.interpolated && "should be interpolated");
    float val = *result.value.as_float();
    assert(std::abs(val - 0.5f) < 0.001f && "should be ~0.5");

    // Test held interpolation
    result = prim.interpolate_time_sample(opacity_id, 0.5, TimeInterpolation::Held);
    assert(result.success && "should work with held");
    assert(!result.interpolated && "held should not interpolate");
    assert(*result.value.as_float() == 0.0f && "held should use lower value");
  }

  // Test vector interpolation (float3)
  {
    PrimSpec prim("animVec", "Xform");
    PropNameTable& table = GetPropNameTable();
    PropNameId translate_id = table.intern("xformOp:translate");

    // Add time samples
    prim.add_time_sample(translate_id, 0.0, Value::MakeFloat3(0.0f, 0.0f, 0.0f));
    prim.add_time_sample(translate_id, 1.0, Value::MakeFloat3(10.0f, 20.0f, 30.0f));

    // Test interpolation at t=0.5
    auto result = prim.interpolate_time_sample(translate_id, 0.5);
    assert(result.success && "should interpolate float3");
    const float* v = result.value.as_float3();
    assert(v != nullptr && "should have float3");
    assert(std::abs(v[0] - 5.0f) < 0.001f && "x should be 5");
    assert(std::abs(v[1] - 10.0f) < 0.001f && "y should be 10");
    assert(std::abs(v[2] - 15.0f) < 0.001f && "z should be 15");
  }

  // Test extrapolation (before first / after last)
  {
    PrimSpec prim("animExtrap", "Xform");
    PropNameTable& table = GetPropNameTable();
    PropNameId scale_id = table.intern("scale");

    prim.add_time_sample(scale_id, 1.0, Value(100.0f));
    prim.add_time_sample(scale_id, 2.0, Value(200.0f));

    // Before first sample - should use first value
    auto result = prim.interpolate_time_sample(scale_id, 0.0);
    assert(result.success && "should work before first");
    assert(*result.value.as_float() == 100.0f && "should use first value");

    // After last sample - should use last value
    result = prim.interpolate_time_sample(scale_id, 10.0);
    assert(result.success && "should work after last");
    assert(*result.value.as_float() == 200.0f && "should use last value");
  }

  std::cout << "  Time sample interpolation: PASSED" << std::endl;
}

void test_time_samples() {
  std::cout << "Testing time samples with deduplication..." << std::endl;

  PrimSpec prim("animatedPrim", "Mesh");

  PropNameTable& table = GetPropNameTable();
  PropNameId points_id = table.intern("points");

  // Create a float3 array value (9 floats = 3 vertices)
  std::vector<float> points_data = {0, 0, 0, 1, 0, 0, 0, 1, 0};

  // Add time samples - some frames have duplicate values
  prim.add_time_sample(points_id, 0.0, Value::MakeFloat3Array(points_data));
  prim.add_time_sample(points_id, 1.0, Value::MakeFloat3Array(points_data));  // Same as t=0
  prim.add_time_sample(points_id, 2.0, Value::MakeFloat3Array(points_data));  // Same as t=0

  // Add a different value
  std::vector<float> moved_data = {0, 1, 0, 1, 1, 0, 0, 2, 0};
  prim.add_time_sample(points_id, 3.0, Value::MakeFloat3Array(moved_data));
  prim.add_time_sample(points_id, 4.0, Value::MakeFloat3Array(moved_data));  // Same as t=3

  // Check that we have time samples
  assert(prim.has_time_samples(points_id) && "should have time samples for points");
  assert(prim.has_any_time_samples() && "should have some time samples");

  // Get time samples
  const auto* samples = prim.time_samples(points_id);
  assert(samples != nullptr && "should get time samples");
  assert(samples->size() == 5 && "should have 5 time samples");

  // Verify time values
  assert((*samples)[0].first == 0.0 && "first sample at t=0");
  assert((*samples)[4].first == 4.0 && "last sample at t=4");

  // Check deduplication stats
  auto stats = prim.time_sample_stats();
  assert(stats.total_samples == 5 && "should have 5 total samples");
  // frames 0,1,2 share one value, frames 3,4 share another = 2 unique values
  assert(stats.unique_values == 2 && "should have 2 unique values");
  // 3 duplicates: frame 1 and 2 reuse frame 0's value, frame 4 reuses frame 3's value
  assert(stats.dedup_count == 3 && "should have 3 deduplicated values");

  // Verify we can access the values
  const Value* val0 = prim.time_sample_value((*samples)[0].second);
  const Value* val1 = prim.time_sample_value((*samples)[1].second);
  const Value* val3 = prim.time_sample_value((*samples)[3].second);

  assert(val0 != nullptr && "should get value at t=0");
  assert(val1 != nullptr && "should get value at t=1");

  // Offsets should be the same for deduplicated values
  assert((*samples)[0].second == (*samples)[1].second && "t=0 and t=1 should share offset");
  assert((*samples)[0].second == (*samples)[2].second && "t=0 and t=2 should share offset");
  assert((*samples)[3].second == (*samples)[4].second && "t=3 and t=4 should share offset");
  assert((*samples)[0].second != (*samples)[3].second && "t=0 and t=3 should have different offsets");

  // Get properties with time samples
  auto props = prim.time_sampled_properties();
  assert(props.size() == 1 && "should have 1 property with time samples");
  assert(props[0] == points_id && "should be points property");

  std::cout << "  Time samples with deduplication: PASSED" << std::endl;
  std::cout << "    Total samples: " << stats.total_samples << std::endl;
  std::cout << "    Unique values: " << stats.unique_values << std::endl;
  std::cout << "    Deduplicated: " << stats.dedup_count << std::endl;
  std::cout << "    Memory: " << stats.memory_bytes << " bytes" << std::endl;
}

void test_relationships() {
  std::cout << "Testing relationships..." << std::endl;

  Layer layer;
  LayerBuilder builder(layer);

  builder.begin_prim("Mesh", "Mesh");
  builder.add_relationship("material:binding", Path("/Materials/MyMaterial"));
  builder.end_prim();

  builder.begin_prim("Materials", "Scope");
    builder.begin_prim("MyMaterial", "Material");
    builder.end_prim();
  builder.end_prim();

  builder.finalize();

  const PrimSpec* mesh = layer.prim_at_path("/Mesh");
  assert(mesh != nullptr && "should find mesh");

  const std::vector<Path>* targets = mesh->relationship("material:binding");
  assert(targets != nullptr && "should have material:binding relationship");
  assert(targets->size() == 1 && "should have 1 target");
  assert((*targets)[0].str() == "/Materials/MyMaterial" && "target should match");

  std::cout << "  Relationships: PASSED" << std::endl;
}

int main() {
  std::cout << "=== Layer/PrimSpec Tests ===" << std::endl;

  test_prop_name_table();
  test_prop_index();
  test_prim_spec();
  test_layer_builder();
  test_layer_stats();
  test_metadata();
  test_interpolation();
  test_time_samples();
  test_relationships();

  std::cout << "\n=== All Layer tests PASSED ===" << std::endl;
  return 0;
}
