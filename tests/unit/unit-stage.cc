#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-stage.h"
#include "prim-types.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"

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
