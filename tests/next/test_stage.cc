// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Test for Stage functionality

#include <iostream>
#include <cassert>

#include "next/stage/stage.hh"

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
  assert(stage.GetMeta().defaultPrim == "World" && "defaultPrim should be World");
  assert(stage.GetUpAxis() == "Z" && "upAxis should be Z");
  assert(stage.GetMetersPerUnit() == 1.0 && "metersPerUnit should be 1.0");
  assert(stage.GetTimeCodesPerSecond() == 30.0 && "fps should be 30");
  assert(stage.GetStartTimeCode() == 1.0 && "startTimeCode should be 1");
  assert(stage.GetEndTimeCode() == 100.0 && "endTimeCode should be 100");

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
  assert(stage.GetPrimCount() == 3 && "should have 3 prims");

  // Get prim at path
  UsdPrim root = stage.GetPrimAtPath("/Root");
  assert(root.IsValid() && "should find /Root");
  assert(root.GetName() == "Root" && "name should be Root");
  assert(root.GetTypeName() == "Xform" && "type should be Xform");

  UsdPrim child1 = stage.GetPrimAtPath("/Root/Child1");
  assert(child1.IsValid() && "should find /Root/Child1");
  assert(child1.GetTypeName() == "Mesh" && "Child1 should be Mesh");

  // Check if path exists
  assert(stage.HasPrimAtPath("/Root") && "should have /Root");
  assert(stage.HasPrimAtPath("/Root/Child2") && "should have /Root/Child2");
  assert(!stage.HasPrimAtPath("/NonExistent") && "should not have /NonExistent");

  // Get root prims
  auto roots = stage.GetRootPrims();
  assert(roots.size() == 1 && "should have 1 root prim");
  assert(roots[0].GetName() == "Root" && "root should be Root");

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
  assert(defaultPrim.IsValid() && "should have default prim");
  assert(defaultPrim.GetName() == "World" && "default prim should be World");

  // Test prim properties
  UsdPrim mesh = stage.GetPrimAtPath("/World/Mesh");
  assert(mesh.IsValid() && "should find Mesh");
  assert(mesh.IsDefined() && "Mesh should be defined");
  assert(mesh.IsActive() && "Mesh should be active");

  // Test HasProperty
  assert(mesh.HasProperty("points") && "should have points");
  assert(mesh.HasProperty("doubleSided") && "should have doubleSided");
  assert(!mesh.HasProperty("nonExistent") && "should not have nonExistent");

  // Test GetPropertyValue
  const Value* points = mesh.GetPropertyValue("points");
  assert(points != nullptr && "should get points value");
  assert(points->is_array() && "points should be array");

  const Value* ds = mesh.GetPropertyValue("doubleSided");
  assert(ds != nullptr && "should get doubleSided value");
  assert(ds->as_bool() != nullptr && "doubleSided should be bool");
  assert(*ds->as_bool() == true && "doubleSided should be true");

  // Test GetPropertyNames
  auto names = mesh.GetPropertyNames();
  assert(names.size() == 2 && "should have 2 properties");

  // Test hierarchy
  UsdPrim world = mesh.GetParent();
  assert(world.IsValid() && "mesh should have parent");
  assert(world.GetName() == "World" && "parent should be World");

  auto children = world.GetChildren();
  assert(children.size() == 1 && "World should have 1 child");
  assert(children[0].GetName() == "Mesh" && "child should be Mesh");

  UsdPrim child = world.GetChild("Mesh");
  assert(child.IsValid() && "should find Mesh child");

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
  assert(count == 6 && "should traverse 6 prims");

  // Collect prim names in order
  std::vector<std::string> names;
  stage.Traverse([&](const UsdPrim& prim) {
    names.push_back(prim.GetName());
    return true;
  });
  assert(names.size() == 6 && "should have 6 names");
  assert(names[0] == "A" && "first should be A");
  assert(names[1] == "B" && "second should be B");
  assert(names[2] == "C" && "third should be C");

  // Test early termination
  int visited = 0;
  stage.Traverse([&](const UsdPrim& prim) {
    visited++;
    return visited < 3;  // Stop after 3
  });
  assert(visited == 3 && "should stop after 3");

  // Test GetPrimsOfType
  auto meshes = stage.GetPrimsOfType("Mesh");
  assert(meshes.size() == 3 && "should have 3 meshes");

  auto xforms = stage.GetPrimsOfType("Xform");
  assert(xforms.size() == 3 && "should have 3 xforms");

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
  assert(mesh.IsValid() && "should find mesh");

  const std::vector<Path>* targets = mesh.GetRelationship("material:binding");
  assert(targets != nullptr && "should have material:binding");
  assert(targets->size() == 1 && "should have 1 target");
  assert((*targets)[0].str() == "/Materials/MyMat" && "target should match");

  // Resolve relationship target
  UsdPrim material = stage.GetPrimAtPath((*targets)[0]);
  assert(material.IsValid() && "should resolve material");
  assert(material.GetTypeName() == "Material" && "should be Material type");

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
  assert(stats.prim_count == 100 && "should have 100 prims");
  assert(stats.layer_count == 1 && "should have 1 layer");
  assert(stats.total_properties == 100 && "should have 100 properties");
  assert(stats.memory_bytes > 0 && "should use memory");

  std::cout << "  Stats: prim_count=" << stats.prim_count
            << ", properties=" << stats.total_properties
            << ", memory=" << stats.memory_bytes << " bytes" << std::endl;
  std::cout << "  Stage stats: PASSED" << std::endl;
}

void test_stage_move() {
  std::cout << "Testing Stage move semantics..." << std::endl;

  StageBuilder builder;
  LayerBuilder& lb = builder.GetLayerBuilder();
  lb.begin_prim("Test", "Mesh");
  lb.end_prim();

  Stage stage1 = builder.Build();
  assert(stage1.GetPrimCount() == 1 && "stage1 should have 1 prim");

  // Move construct
  Stage stage2 = std::move(stage1);
  assert(stage2.GetPrimCount() == 1 && "stage2 should have 1 prim after move");

  // Move assign
  Stage stage3;
  stage3 = std::move(stage2);
  assert(stage3.GetPrimCount() == 1 && "stage3 should have 1 prim after move");

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
  test_stage_move();

  std::cout << "\n=== All Stage tests PASSED ===" << std::endl;
  return 0;
}
