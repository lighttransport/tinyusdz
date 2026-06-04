#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-stage.h"
#include "core/prim.hh"
#include "core/path.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usda-writer.hh"
#include "stage.hh"
#include "mmap-array-ref.hh"

#include <cstdint>
#include <vector>

using namespace tinyusdz;

// Helper to build a test hierarchy:
// /Root
//   /Root/Child1
//     /Root/Child1/GrandChild1
//     /Root/Child1/GrandChild2
//   /Root/Child2
//     /Root/Child2/GrandChild3
static Stage build_test_stage() {
  Stage stage;

  // Create root prim
  Xform root_xform;
  root_xform.name = "Root";
  Prim root_prim("Root", root_xform);

  // Create Child1 with grandchildren
  Xform child1_xform;
  child1_xform.name = "Child1";
  Prim child1("Child1", child1_xform);

  Xform gc1_xform;
  gc1_xform.name = "GrandChild1";
  Prim grandchild1("GrandChild1", gc1_xform);
  child1.add_child(std::move(grandchild1));

  Xform gc2_xform;
  gc2_xform.name = "GrandChild2";
  Prim grandchild2("GrandChild2", gc2_xform);
  child1.add_child(std::move(grandchild2));

  root_prim.add_child(std::move(child1));

  // Create Child2 with grandchild
  Xform child2_xform;
  child2_xform.name = "Child2";
  Prim child2("Child2", child2_xform);

  Xform gc3_xform;
  gc3_xform.name = "GrandChild3";
  Prim grandchild3("GrandChild3", gc3_xform);
  child2.add_child(std::move(grandchild3));

  root_prim.add_child(std::move(child2));

  stage.add_root_prim(std::move(root_prim));

  return stage;
}

void stage_get_prim_at_path_test(void) {
  Stage stage = build_test_stage();

  // Test finding root prim
  {
    Path path("/Root", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      TEST_CHECK(result.value()->element_name() == "Root");
    }
  }

  // Test finding first level child
  {
    Path path("/Root/Child1", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      TEST_CHECK(result.value()->element_name() == "Child1");
    }
  }

  // Test finding second level child (grandchild)
  {
    Path path("/Root/Child1/GrandChild1", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      TEST_CHECK(result.value()->element_name() == "GrandChild1");
    }
  }

  {
    Path path("/Root/Child1/GrandChild2", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      TEST_CHECK(result.value()->element_name() == "GrandChild2");
    }
  }

  {
    Path path("/Root/Child2/GrandChild3", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      TEST_CHECK(result.value()->element_name() == "GrandChild3");
    }
  }

  // Test non-existent paths
  {
    Path path("/NonExistent", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(!result.has_value());
  }

  {
    Path path("/Root/NonExistent", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(!result.has_value());
  }

  {
    Path path("/Root/Child1/NonExistent", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(!result.has_value());
  }

  // Test invalid path (root only)
  {
    Path path("/", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(!result.has_value());
  }

  // Test caching - second lookup should use cache
  {
    Path path("/Root/Child1/GrandChild1", "");
    auto result1 = stage.GetPrimAtPath(path);
    auto result2 = stage.GetPrimAtPath(path);
    TEST_CHECK(result1.has_value());
    TEST_CHECK(result2.has_value());
    if (result1 && result2) {
      TEST_CHECK(result1.value() == result2.value());  // Same pointer
    }
  }
}

void stage_find_prim_by_id_test(void) {
  Stage stage = build_test_stage();

  // Assign prim IDs
  TEST_CHECK(stage.compute_absolute_prim_path_and_assign_prim_id(true));

  // Collect prim IDs first
  std::vector<int64_t> prim_ids;
  std::vector<std::string> prim_names;

  // Get root prim ID
  {
    Path path("/Root", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      prim_ids.push_back(result.value()->prim_id());
      prim_names.push_back(result.value()->element_name());
    }
  }

  // Get child prim IDs
  {
    Path path("/Root/Child1", "");
    auto result = stage.GetPrimAtPath(path);
    if (result) {
      prim_ids.push_back(result.value()->prim_id());
      prim_names.push_back(result.value()->element_name());
    }
  }

  {
    Path path("/Root/Child1/GrandChild1", "");
    auto result = stage.GetPrimAtPath(path);
    if (result) {
      prim_ids.push_back(result.value()->prim_id());
      prim_names.push_back(result.value()->element_name());
    }
  }

  {
    Path path("/Root/Child2/GrandChild3", "");
    auto result = stage.GetPrimAtPath(path);
    if (result) {
      prim_ids.push_back(result.value()->prim_id());
      prim_names.push_back(result.value()->element_name());
    }
  }

  // Now test find_prim_by_prim_id
  for (size_t i = 0; i < prim_ids.size(); i++) {
    const Prim *found_prim = nullptr;
    std::string err;
    bool found = stage.find_prim_by_prim_id(
        static_cast<uint64_t>(prim_ids[i]), found_prim, &err);

    TEST_CHECK(found);
    if (found && found_prim) {
      TEST_CHECK(found_prim->element_name() == prim_names[i]);
      TEST_CHECK(found_prim->prim_id() == prim_ids[i]);
    }
  }

  // Test non-existent prim ID
  {
    const Prim *found_prim = nullptr;
    std::string err;
    bool found = stage.find_prim_by_prim_id(999999, found_prim, &err);
    TEST_CHECK(!found);
  }

  // Test invalid prim ID (0)
  {
    const Prim *found_prim = nullptr;
    std::string err;
    bool found = stage.find_prim_by_prim_id(0, found_prim, &err);
    TEST_CHECK(!found);
  }

  // Test caching - second lookup should use cache
  if (!prim_ids.empty()) {
    const Prim *found1 = nullptr;
    const Prim *found2 = nullptr;
    std::string err;

    bool ok1 = stage.find_prim_by_prim_id(
        static_cast<uint64_t>(prim_ids[0]), found1, &err);
    bool ok2 = stage.find_prim_by_prim_id(
        static_cast<uint64_t>(prim_ids[0]), found2, &err);

    TEST_CHECK(ok1 && ok2);
    TEST_CHECK(found1 == found2);  // Same pointer from cache
  }
}

void stage_adopt_mmap_buffer_lifetime_test(void) {
  std::vector<uint8_t> bytes = {0, 1, 2, 3, 4, 5, 6, 7};

  Stage stage;
  stage.set_mmap_source(MMapDataSource(bytes.data() + 2, 4));

  MMapArrayRef ref;
  ref.byte_offset = 1;
  ref.element_count = 2;
  ref.element_size = sizeof(uint8_t);

  MMapArrayTable table;
  table.add("/Mesh", "points", ref);
  stage.set_mmap_table(std::move(table));

  TEST_CHECK(stage.adopt_mmap_buffer(std::move(bytes)));
  TEST_CHECK(stage.has_mmap_zero_copy());

  const MMapArrayRef *stored_ref = stage.mmap_table()->find("/Mesh", "points");
  TEST_CHECK(stored_ref != nullptr);
  if (stored_ref) {
    const uint8_t *ptr = stage.mmap_source()->get_ptr<uint8_t>(*stored_ref);
    TEST_CHECK(ptr != nullptr);
    if (ptr) {
      TEST_CHECK(ptr[0] == 3);
      TEST_CHECK(ptr[1] == 4);
    }
  }

  stage.clear_mmap_data();
  TEST_CHECK(!stage.has_mmap_zero_copy());
}

void stage_add_root_prim_test(void) {
  // Start with empty stage
  Stage stage;
  TEST_CHECK(stage.root_prims().size() == 0);

  // Add first root prim
  {
    Xform xf;
    xf.name = "First";
    Prim prim("First", xf);
    bool ok = stage.add_root_prim(std::move(prim), false);
    TEST_CHECK(ok);
    TEST_CHECK(stage.root_prims().size() == 1);
    TEST_CHECK(stage.root_prims()[0].element_name() == "First");
  }

  // Add second root prim with different name
  {
    Xform xf;
    xf.name = "Second";
    Prim prim("Second", xf);
    bool ok = stage.add_root_prim(std::move(prim), false);
    TEST_CHECK(ok);
    TEST_CHECK(stage.root_prims().size() == 2);
    TEST_CHECK(stage.root_prims()[1].element_name() == "Second");
  }

  // Adding prim with same name and rename_prim_name=true should succeed
  {
    Xform xf;
    xf.name = "First";
    Prim prim("First", xf);
    bool ok = stage.add_root_prim(std::move(prim), true);
    TEST_CHECK(ok);
    TEST_CHECK(stage.root_prims().size() == 3);
    // The name should have been renamed to avoid collision
    TEST_MSG("Third prim name after rename: %s",
             stage.root_prims()[2].element_name().c_str());
  }
}

void stage_replace_root_prim_test(void) {
  Stage stage;

  // Add an Xform root prim named "A"
  {
    Xform xf;
    xf.name = "A";
    Prim prim("A", xf);
    stage.add_root_prim(std::move(prim), false);
  }
  TEST_CHECK(stage.root_prims().size() == 1);
  TEST_CHECK(stage.root_prims()[0].type_name() == "Xform");

  // Replace "A" with a GeomMesh prim
  {
    GeomMesh mesh;
    mesh.name = "A";
    Prim prim("A", mesh);
    bool ok = stage.replace_root_prim("A", std::move(prim));
    TEST_CHECK(ok);
    TEST_CHECK(stage.root_prims().size() == 1);
    TEST_CHECK(stage.root_prims()[0].type_name() == "Mesh");
    TEST_CHECK(stage.root_prims()[0].element_name() == "A");
  }

  // replace_root_prim for non-existent name should add it
  {
    Xform xf;
    xf.name = "NonExistent";
    Prim prim("NonExistent", xf);
    bool ok = stage.replace_root_prim("NonExistent", std::move(prim));
    TEST_CHECK(ok);
    TEST_CHECK(stage.root_prims().size() == 2);
  }

  // replace_root_prim with empty name should fail
  {
    Xform xf;
    Prim prim("Dummy", xf);
    bool ok = stage.replace_root_prim("", std::move(prim));
    TEST_CHECK(!ok);
  }
}

void stage_export_to_string_test(void) {
  Stage stage;

  // Add an Xform root prim
  Xform xf;
  xf.name = "MyXform";
  Prim prim("MyXform", xf);
  stage.add_root_prim(std::move(prim), false);

  // Export to string
  std::string usda = stage.ExportToString();
  TEST_CHECK(!usda.empty());
  TEST_CHECK(usda.find("#usda 1.0") != std::string::npos);
  TEST_MSG("USDA output contains header");
  TEST_CHECK(usda.find("MyXform") != std::string::npos);
  TEST_MSG("USDA output contains prim name");

  // Round-trip: re-parse the USDA string
  Stage stage2;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
      "", &stage2, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());
    return;
  }
  TEST_CHECK(stage2.root_prims().size() == 1);
  TEST_CHECK(stage2.root_prims()[0].element_name() == "MyXform");
}

void stage_commit_prim_id_test(void) {
  Stage stage;

  // Build a small hierarchy: /Root/Child
  Xform root_xf;
  root_xf.name = "Root";
  Prim root_prim("Root", root_xf);

  Xform child_xf;
  child_xf.name = "Child";
  Prim child_prim("Child", child_xf);
  root_prim.add_child(std::move(child_prim));

  stage.add_root_prim(std::move(root_prim), false);

  // commit() assigns prim IDs
  bool ok = stage.commit();
  TEST_CHECK(ok);

  // Verify Root prim has a valid prim_id
  {
    Path path("/Root", "");
    auto result = stage.GetPrimAtPath(path);
    TEST_CHECK(result.has_value());
    if (result) {
      int64_t id = result.value()->prim_id();
      TEST_CHECK(id > 0);
      TEST_MSG("Root prim_id = %lld", (long long)id);
    }
  }

  // Verify Child prim has a valid and different prim_id
  {
    Path root_path("/Root", "");
    Path child_path("/Root/Child", "");
    auto root_result = stage.GetPrimAtPath(root_path);
    auto child_result = stage.GetPrimAtPath(child_path);
    TEST_CHECK(root_result.has_value());
    TEST_CHECK(child_result.has_value());
    if (root_result && child_result) {
      int64_t root_id = root_result.value()->prim_id();
      int64_t child_id = child_result.value()->prim_id();
      TEST_CHECK(child_id > 0);
      TEST_CHECK(root_id != child_id);
      TEST_MSG("Root prim_id = %lld, Child prim_id = %lld",
               (long long)root_id, (long long)child_id);
    }
  }
}

void stage_metas_test(void) {
  Stage stage;

  // Set stage metadata
  stage.metas().defaultPrim = value::token("Root");
  stage.metas().upAxis = Axis::Y;
  stage.metas().metersPerUnit = 0.01;

  // Verify locally
  TEST_CHECK(stage.metas().defaultPrim.str() == "Root");
  TEST_CHECK(stage.metas().upAxis.get_value() == Axis::Y);
  TEST_CHECK(stage.metas().metersPerUnit.get_value() == 0.01);

  // Add a root prim so we have valid USDA
  Xform xf;
  xf.name = "Root";
  Prim prim("Root", xf);
  stage.add_root_prim(std::move(prim), false);

  // Export and round-trip
  std::string usda = stage.ExportToString();
  TEST_CHECK(!usda.empty());

  Stage stage2;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(),
      "", &stage2, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());
    return;
  }

  // Check re-parsed metas
  TEST_CHECK(stage2.metas().defaultPrim.str() == "Root");
  TEST_CHECK(stage2.metas().upAxis.get_value() == Axis::Y);

  // Check metersPerUnit with tolerance for floating point
  double mpu = stage2.metas().metersPerUnit.get_value();
  TEST_CHECK(mpu > 0.009 && mpu < 0.011);
  TEST_MSG("Re-parsed metersPerUnit = %f", mpu);
}

void stage_memory_estimation_test(void) {
  Stage stage = build_test_stage();
  size_t mem = stage.estimate_memory_usage();
  TEST_CHECK(mem > 0);
  TEST_MSG("Memory usage estimate: %zu bytes", mem);
}

void stage_empty_test(void) {
  Stage stage;

  // Empty stage should have no root prims
  TEST_CHECK(stage.root_prims().empty());

  // ExportToString should still produce valid USDA with header
  std::string usda = stage.ExportToString();
  TEST_CHECK(!usda.empty());
  TEST_CHECK(usda.find("#usda 1.0") != std::string::npos);
  TEST_MSG("Empty stage USDA: %.80s...", usda.c_str());
}

void stage_nested_hierarchy_test(void) {
  Stage stage;

  // Build 4-level hierarchy: /Root/A/B/C
  Xform xf_c;
  xf_c.name = "C";
  Prim prim_c("C", xf_c);

  Xform xf_b;
  xf_b.name = "B";
  Prim prim_b("B", xf_b);
  prim_b.add_child(std::move(prim_c));

  Xform xf_a;
  xf_a.name = "A";
  Prim prim_a("A", xf_a);
  prim_a.add_child(std::move(prim_b));

  Xform xf_root;
  xf_root.name = "Root";
  Prim prim_root("Root", xf_root);
  prim_root.add_child(std::move(prim_a));

  stage.add_root_prim(std::move(prim_root), false);
  bool ok = stage.commit();
  TEST_CHECK(ok);

  // Verify all 4 levels are reachable via GetPrimAtPath
  {
    Path p("/Root", "");
    auto r = stage.GetPrimAtPath(p);
    TEST_CHECK(r.has_value());
    if (r) {
      TEST_CHECK(r.value()->element_name() == "Root");
    }
  }

  {
    Path p("/Root/A", "");
    auto r = stage.GetPrimAtPath(p);
    TEST_CHECK(r.has_value());
    if (r) {
      TEST_CHECK(r.value()->element_name() == "A");
    }
  }

  {
    Path p("/Root/A/B", "");
    auto r = stage.GetPrimAtPath(p);
    TEST_CHECK(r.has_value());
    if (r) {
      TEST_CHECK(r.value()->element_name() == "B");
    }
  }

  {
    Path p("/Root/A/B/C", "");
    auto r = stage.GetPrimAtPath(p);
    TEST_CHECK(r.has_value());
    if (r) {
      TEST_CHECK(r.value()->element_name() == "C");
    }
  }

  // Non-existent deep path
  {
    Path p("/Root/A/B/C/D", "");
    auto r = stage.GetPrimAtPath(p);
    TEST_CHECK(!r.has_value());
  }
}
