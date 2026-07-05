// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Test for Stage functionality

#include <iostream>
#include "test-check.hh"
#include <cmath>

#include "next/stage/stage.hh"
#include "next/eval/attribute-eval.hh"

using namespace tinyusdz::next;

void test_stage_builder() {
  std::cout << "Testing StageBuilder..." << std::endl;

  StageBuilder builder;
  builder.SetDefaultPrim("World");
  builder.SetUpAxis("Z");
  builder.SetMetersPerUnit(1.0);
  builder.SetTimeCodesPerSecond(30.0);
  builder.SetStartTimeCode(1.0);
  builder.SetEndTimeCode(100.0);

  LayerBuilder& lb = builder.GetLayerBuilder();

  // Build hierarchy:
  // /World (Xform)
  //   /Geometry (Scope)
  //     /Mesh (Mesh)
  //   /Materials (Scope)
  //     /DefaultMaterial (Material)

  lb.begin_prim("World", "Xform");
    lb.begin_prim("Geometry", "Scope");
      lb.begin_prim("Mesh", "Mesh");
      lb.add_property("points", Value::MakeFloat3Array(std::vector<float>{
        0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0
      }));
      lb.add_property("faceVertexCounts", Value::MakeIntArray(std::vector<int32_t>{4}));
      lb.add_property("faceVertexIndices", Value::MakeIntArray(std::vector<int32_t>{0, 1, 2, 3}));
      lb.add_relationship("material:binding", Path("/World/Materials/DefaultMaterial"));
      lb.end_prim();
    lb.end_prim();

    lb.begin_prim("Materials", "Scope");
      lb.begin_prim("DefaultMaterial", "Material");
      lb.end_prim();
    lb.end_prim();
  lb.end_prim();

  Stage stage = builder.Build();

  // Verify metadata
  NEXT_CHECK(stage.GetMeta().defaultPrim == "World" && "defaultPrim should be World");
  NEXT_CHECK(stage.GetUpAxis() == "Z" && "upAxis should be Z");
  NEXT_CHECK(stage.GetMetersPerUnit() == 1.0 && "metersPerUnit should be 1.0");
  NEXT_CHECK(stage.GetTimeCodesPerSecond() == 30.0 && "fps should be 30");
  NEXT_CHECK(stage.GetStartTimeCode() == 1.0 && "startTimeCode should be 1");
  NEXT_CHECK(stage.GetEndTimeCode() == 100.0 && "endTimeCode should be 100");

  std::cout << "  StageBuilder: PASSED" << std::endl;
}

void test_stage_prim_access() {
  std::cout << "Testing Stage prim access..." << std::endl;

  StageBuilder builder;
  LayerBuilder& lb = builder.GetLayerBuilder();

  lb.begin_prim("Root", "Xform");
    lb.begin_prim("Child1", "Mesh");
    lb.end_prim();
    lb.begin_prim("Child2", "Mesh");
    lb.end_prim();
  lb.end_prim();

  Stage stage = builder.Build();

  // Get prim count
  NEXT_CHECK(stage.GetPrimCount() == 3 && "should have 3 prims");

  // Get prim at path
  UsdPrim root = stage.GetPrimAtPath("/Root");
  NEXT_CHECK(root.IsValid() && "should find /Root");
  NEXT_CHECK(root.GetName() == "Root" && "name should be Root");
  NEXT_CHECK(root.GetTypeName() == "Xform" && "type should be Xform");

  UsdPrim child1 = stage.GetPrimAtPath("/Root/Child1");
  NEXT_CHECK(child1.IsValid() && "should find /Root/Child1");
  NEXT_CHECK(child1.GetTypeName() == "Mesh" && "Child1 should be Mesh");

  // Check if path exists
  NEXT_CHECK(stage.HasPrimAtPath("/Root") && "should have /Root");
  NEXT_CHECK(stage.HasPrimAtPath("/Root/Child2") && "should have /Root/Child2");
  NEXT_CHECK(!stage.HasPrimAtPath("/NonExistent") && "should not have /NonExistent");

  // Get root prims
  auto roots = stage.GetRootPrims();
  NEXT_CHECK(roots.size() == 1 && "should have 1 root prim");
  NEXT_CHECK(roots[0].GetName() == "Root" && "root should be Root");

  std::cout << "  Stage prim access: PASSED" << std::endl;
}

void test_usd_prim() {
  std::cout << "Testing UsdPrim..." << std::endl;

  StageBuilder builder;
  builder.SetDefaultPrim("World");
  LayerBuilder& lb = builder.GetLayerBuilder();

  lb.begin_prim("World", "Xform");
    lb.begin_prim("Mesh", "Mesh");
    lb.add_property("points", Value::MakeFloat3Array(std::vector<float>{0, 0, 0, 1, 0, 0, 0, 1, 0}));
    lb.add_property("doubleSided", Value(true));
    lb.set_active(true);
    lb.end_prim();
  lb.end_prim();

  Stage stage = builder.Build();

  // Test default prim
  UsdPrim defaultPrim = stage.GetDefaultPrim();
  NEXT_CHECK(defaultPrim.IsValid() && "should have default prim");
  NEXT_CHECK(defaultPrim.GetName() == "World" && "default prim should be World");

  // Test prim properties
  UsdPrim mesh = stage.GetPrimAtPath("/World/Mesh");
  NEXT_CHECK(mesh.IsValid() && "should find Mesh");
  NEXT_CHECK(mesh.IsDefined() && "Mesh should be defined");
  NEXT_CHECK(mesh.IsActive() && "Mesh should be active");

  // Test HasProperty
  NEXT_CHECK(mesh.HasProperty("points") && "should have points");
  NEXT_CHECK(mesh.HasProperty("doubleSided") && "should have doubleSided");
  NEXT_CHECK(!mesh.HasProperty("nonExistent") && "should not have nonExistent");

  // Test GetPropertyValue
  const Value* points = mesh.GetPropertyValue("points");
  NEXT_CHECK(points != nullptr && "should get points value");
  NEXT_CHECK(points->is_array() && "points should be array");

  const Value* ds = mesh.GetPropertyValue("doubleSided");
  NEXT_CHECK(ds != nullptr && "should get doubleSided value");
  NEXT_CHECK(ds->as_bool() != nullptr && "doubleSided should be bool");
  NEXT_CHECK(*ds->as_bool() == true && "doubleSided should be true");

  // Test GetPropertyNames
  auto names = mesh.GetPropertyNames();
  NEXT_CHECK(names.size() == 2 && "should have 2 properties");

  // Test hierarchy
  UsdPrim world = mesh.GetParent();
  NEXT_CHECK(world.IsValid() && "mesh should have parent");
  NEXT_CHECK(world.GetName() == "World" && "parent should be World");

  auto children = world.GetChildren();
  NEXT_CHECK(children.size() == 1 && "World should have 1 child");
  NEXT_CHECK(children[0].GetName() == "Mesh" && "child should be Mesh");

  UsdPrim child = world.GetChild("Mesh");
  NEXT_CHECK(child.IsValid() && "should find Mesh child");

  std::cout << "  UsdPrim: PASSED" << std::endl;
}

void test_stage_traversal() {
  std::cout << "Testing Stage traversal..." << std::endl;

  StageBuilder builder;
  LayerBuilder& lb = builder.GetLayerBuilder();

  // Create a deeper hierarchy
  lb.begin_prim("A", "Xform");
    lb.begin_prim("B", "Xform");
      lb.begin_prim("C", "Mesh");
      lb.end_prim();
      lb.begin_prim("D", "Mesh");
      lb.end_prim();
    lb.end_prim();
    lb.begin_prim("E", "Xform");
      lb.begin_prim("F", "Mesh");
      lb.end_prim();
    lb.end_prim();
  lb.end_prim();

  Stage stage = builder.Build();

  // Count prims via traversal
  int count = 0;
  stage.Traverse([&](const UsdPrim& prim) {
    count++;
    return true;
  });
  NEXT_CHECK(count == 6 && "should traverse 6 prims");

  // Collect prim names in order
  std::vector<std::string> names;
  stage.Traverse([&](const UsdPrim& prim) {
    names.push_back(prim.GetName());
    return true;
  });
  NEXT_CHECK(names.size() == 6 && "should have 6 names");
  NEXT_CHECK(names[0] == "A" && "first should be A");
  NEXT_CHECK(names[1] == "B" && "second should be B");
  NEXT_CHECK(names[2] == "C" && "third should be C");

  // Test early termination
  int visited = 0;
  stage.Traverse([&](const UsdPrim& prim) {
    visited++;
    return visited < 3;  // Stop after 3
  });
  NEXT_CHECK(visited == 3 && "should stop after 3");

  // Test GetPrimsOfType
  auto meshes = stage.GetPrimsOfType("Mesh");
  NEXT_CHECK(meshes.size() == 3 && "should have 3 meshes");

  auto xforms = stage.GetPrimsOfType("Xform");
  NEXT_CHECK(xforms.size() == 3 && "should have 3 xforms");

  std::cout << "  Stage traversal: PASSED" << std::endl;
}

void test_stage_relationships() {
  std::cout << "Testing Stage relationships..." << std::endl;

  StageBuilder builder;
  LayerBuilder& lb = builder.GetLayerBuilder();

  lb.begin_prim("Mesh", "Mesh");
  lb.add_relationship("material:binding", Path("/Materials/MyMat"));
  lb.end_prim();

  lb.begin_prim("Materials", "Scope");
    lb.begin_prim("MyMat", "Material");
    lb.end_prim();
  lb.end_prim();

  Stage stage = builder.Build();

  UsdPrim mesh = stage.GetPrimAtPath("/Mesh");
  NEXT_CHECK(mesh.IsValid() && "should find mesh");

  const std::vector<Path>* targets = mesh.GetRelationship("material:binding");
  NEXT_CHECK(targets != nullptr && "should have material:binding");
  NEXT_CHECK(targets->size() == 1 && "should have 1 target");
  NEXT_CHECK((*targets)[0].str() == "/Materials/MyMat" && "target should match");

  // Resolve relationship target
  UsdPrim material = stage.GetPrimAtPath((*targets)[0]);
  NEXT_CHECK(material.IsValid() && "should resolve material");
  NEXT_CHECK(material.GetTypeName() == "Material" && "should be Material type");

  std::cout << "  Stage relationships: PASSED" << std::endl;
}

void test_stage_stats() {
  std::cout << "Testing Stage stats..." << std::endl;

  StageBuilder builder;
  LayerBuilder& lb = builder.GetLayerBuilder();

  for (int i = 0; i < 100; ++i) {
    lb.begin_prim("Prim" + std::to_string(i), "Mesh");
    lb.add_property("points", Value::MakeFloat3Array(std::vector<float>{0, 0, 0}));
    lb.end_prim();
  }

  Stage stage = builder.Build();

  Stage::Stats stats = stage.GetStats();
  NEXT_CHECK(stats.prim_count == 100 && "should have 100 prims");
  NEXT_CHECK(stats.layer_count == 1 && "should have 1 layer");
  NEXT_CHECK(stats.total_properties == 100 && "should have 100 properties");
  NEXT_CHECK(stats.memory_bytes > 0 && "should use memory");

  std::cout << "  Stats: prim_count=" << stats.prim_count
            << ", properties=" << stats.total_properties
            << ", memory=" << stats.memory_bytes << " bytes" << std::endl;
  std::cout << "  Stage stats: PASSED" << std::endl;
}

void test_attribute_eval() {
  std::cout << "Testing AttributeEval..." << std::endl;

  StageBuilder builder;
  builder.SetTimeCodesPerSecond(24.0);
  builder.SetStartTimeCode(1.0);
  builder.SetEndTimeCode(48.0);
  LayerBuilder& lb = builder.GetLayerBuilder();

  lb.begin_prim("World", "Xform");

  // Mesh with static properties
  lb.begin_prim("StaticMesh", "Mesh");
  lb.add_property("doubleSided", Value(true));
  lb.add_property("radius", Value(1.5f));
  lb.add_property("extent", Value::MakeFloat3(-1.0f, -1.0f, -1.0f));
  lb.end_prim();

  // Mesh with animated visibility
  lb.begin_prim("AnimatedMesh", "Mesh");
  // Add time samples for visibility (simulating animation)
  // Note: This would typically be done via lower-level API
  lb.end_prim();

  lb.end_prim();

  Stage stage = builder.Build();
  AttributeEval eval(&stage);

  // Test basic scalar evaluation
  UsdPrim staticMesh = stage.GetPrimAtPath("/World/StaticMesh");
  NEXT_CHECK(staticMesh.IsValid() && "should find StaticMesh");

  // Test EvalBool
  auto ds = eval.EvalBool(staticMesh, "doubleSided");
  NEXT_CHECK(ds.has_value() && "should get doubleSided");
  NEXT_CHECK(*ds == true && "doubleSided should be true");

  // Test EvalFloat
  auto radius = eval.EvalFloat(staticMesh, "radius");
  NEXT_CHECK(radius.has_value() && "should get radius");
  NEXT_CHECK(std::abs(*radius - 1.5f) < 0.001f && "radius should be 1.5");

  // Test EvalFloat3
  float extent[3];
  bool got_extent = eval.EvalFloat3(staticMesh, "extent", extent);
  NEXT_CHECK(got_extent && "should get extent");
  NEXT_CHECK(std::abs(extent[0] - (-1.0f)) < 0.001f && "extent[0] should be -1");

  // Test EvalOr with fallback
  float missing = eval.EvalOr(staticMesh, "nonExistent", 99.0f);
  NEXT_CHECK(std::abs(missing - 99.0f) < 0.001f && "should use fallback");

  // Test Eval with result metadata
  EvalResult result = eval.Eval(staticMesh, "radius");
  NEXT_CHECK(result.success && "should succeed");
  NEXT_CHECK(result.from_default && "should be from default");
  NEXT_CHECK(!result.from_time_sample && "should not be from time sample");
  NEXT_CHECK(!result.interpolated && "should not be interpolated");

  // Test non-existent attribute
  auto nonexistent = eval.EvalFloat(staticMesh, "nonExistent");
  NEXT_CHECK(!nonexistent.has_value() && "should not find nonExistent");

  std::cout << "  AttributeEval: PASSED" << std::endl;
}

void test_attribute_eval_convenience() {
  std::cout << "Testing convenience functions..." << std::endl;

  StageBuilder builder;
  LayerBuilder& lb = builder.GetLayerBuilder();

  lb.begin_prim("Mesh", "Mesh");
  lb.add_property("focalLength", Value(50.0f));
  lb.add_property("translate", Value::MakeDouble3(1.0, 2.0, 3.0));
  lb.end_prim();

  Stage stage = builder.Build();
  UsdPrim mesh = stage.GetPrimAtPath("/Mesh");

  // Test convenience functions
  float fl;
  bool got_fl = GetFloat(stage, mesh, "focalLength", &fl);
  NEXT_CHECK(got_fl && "should get focalLength");
  NEXT_CHECK(std::abs(fl - 50.0f) < 0.001f && "focalLength should be 50");

  double translate[3];
  bool got_trans = GetDouble3(stage, mesh, "translate", translate);
  NEXT_CHECK(got_trans && "should get translate");
  NEXT_CHECK(std::abs(translate[0] - 1.0) < 0.001 && "translate[0] should be 1");
  NEXT_CHECK(std::abs(translate[1] - 2.0) < 0.001 && "translate[1] should be 2");
  NEXT_CHECK(std::abs(translate[2] - 3.0) < 0.001 && "translate[2] should be 3");

  std::cout << "  Convenience functions: PASSED" << std::endl;
}

void test_stage_move() {
  std::cout << "Testing Stage move semantics..." << std::endl;

  StageBuilder builder;
  LayerBuilder& lb = builder.GetLayerBuilder();
  lb.begin_prim("Test", "Mesh");
  lb.end_prim();

  Stage stage1 = builder.Build();
  NEXT_CHECK(stage1.GetPrimCount() == 1 && "stage1 should have 1 prim");

  // Move construct
  Stage stage2 = std::move(stage1);
  NEXT_CHECK(stage2.GetPrimCount() == 1 && "stage2 should have 1 prim after move");

  // Move assign
  Stage stage3;
  stage3 = std::move(stage2);
  NEXT_CHECK(stage3.GetPrimCount() == 1 && "stage3 should have 1 prim after move");

  std::cout << "  Stage move semantics: PASSED" << std::endl;
}

int main() {
  std::cout << "=== Stage Tests ===" << std::endl;

  test_stage_builder();
  test_stage_prim_access();
  test_usd_prim();
  test_stage_traversal();
  test_stage_relationships();
  test_stage_stats();
  test_attribute_eval();
  test_attribute_eval_convenience();
  test_stage_move();

  std::cout << "\n=== All Stage tests PASSED ===" << std::endl;
  return 0;
}
