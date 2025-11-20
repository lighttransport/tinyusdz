// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Light Transport Entertainment, Inc.
#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-listprims.h"

#include <iostream>

#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "tydra/scene-access.hh"
#include "unit-common.hh"

using namespace tinyusdz;

void listprims_test(void) {
  Stage stage;
  
  // Manually create a Stage with a mesh hierarchy
  Xform xform;
  xform.name = "root";
  xform.spec = Specifier::Def;
  
  GeomMesh mesh;
  mesh.name = "mesh0";
  mesh.spec = Specifier::Def;
  
  Prim mesh_prim("mesh0", mesh);
  Prim root_prim("root", xform);
  root_prim.children().push_back(mesh_prim);
  
  stage.metas().primChildren.push_back(value::token("root"));
  stage.root_prims().push_back(root_prim);
  
  // Test ListPrims before calling compute_absolute_prim_path_and_assign_prim_id
  // Should still work using element_name fallback
  {
    using MeshMap = std::map<std::string, const GeomMesh *>;
    MeshMap meshes;
    TEST_CHECK(tydra::ListPrims(stage, meshes) == true);
    TEST_CHECK(meshes.size() == 1);
    
    // Check that the path is constructed correctly from element_name
    bool found_mesh = false;
    for (const auto &mesh_item : meshes) {
      if (mesh_item.first == "/root/mesh0") {
        found_mesh = true;
        
        // Verify we can get the Prim from the stage
        auto prim_result = stage.GetPrimAtPath(Path(mesh_item.first, ""));
        TEST_CHECK(prim_result.has_value());
        if (prim_result) {
          const Prim *prim = prim_result.value();
          TEST_CHECK(prim->element_name() == "mesh0");
        }
      }
    }
    TEST_CHECK(found_mesh == true);
  }
  
  // Now call compute_absolute_prim_path_and_assign_prim_id
  TEST_CHECK(stage.compute_absolute_prim_path_and_assign_prim_id() == true);
  
  // Test ListPrims after setting absolute paths
  {
    using MeshMap = std::map<std::string, const GeomMesh *>;
    MeshMap meshes;
    TEST_CHECK(tydra::ListPrims(stage, meshes) == true);
    TEST_CHECK(meshes.size() == 1);
    
    // Check that the path is correct
    bool found_mesh = false;
    for (const auto &mesh_item : meshes) {
      if (mesh_item.first == "/root/mesh0") {
        found_mesh = true;
        
        // Verify we can get the Prim from the stage
        auto prim_result = stage.GetPrimAtPath(Path(mesh_item.first, ""));
        TEST_CHECK(prim_result.has_value());
        if (prim_result) {
          const Prim *prim = prim_result.value();
          TEST_CHECK(prim->element_name() == "mesh0");
          // absolute_path should now be set correctly
          TEST_CHECK(prim->absolute_path().full_path_name() == "/root/mesh0");
        }
      }
    }
    TEST_CHECK(found_mesh == true);
  }
}
