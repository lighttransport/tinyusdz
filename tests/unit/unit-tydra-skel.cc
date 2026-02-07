// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// Unit tests for Tydra skeleton hierarchy and bone utility functions

#ifdef _MSC_VER
#define NOMINMAX
#endif

#include "unit-tydra-skel.h"

#define TEST_NO_MAIN
#include "acutest.h"
#include "unit-common.hh"

#include "usdSkel.hh"
#include "value-types.hh"
#include "tydra/scene-access.hh"
#include "tydra/bone-util.hh"

using namespace tinyusdz;

// Helper: create a Skeleton with given joint paths and optional bind/rest transforms
static Skeleton make_skeleton(
    const std::vector<std::string> &joint_paths,
    const std::vector<value::matrix4d> *bindXforms = nullptr,
    const std::vector<value::matrix4d> *restXforms = nullptr) {
  Skeleton skel;
  skel.set_name("TestSkel");

  std::vector<value::token> joints;
  for (const auto &jp : joint_paths) {
    joints.push_back(value::token(jp));
  }
  skel.joints.set_value(joints);

  if (bindXforms) {
    skel.bindTransforms.set_value(*bindXforms);
  }

  if (restXforms) {
    skel.restTransforms.set_value(*restXforms);
  }

  return skel;
}

// -----------------------------------------------------------------------
// BuildSkelHierarchy tests
// -----------------------------------------------------------------------

// 3-joint chain: root -> spine -> head
void build_skel_hierarchy_chain_test(void) {
  Skeleton skel = make_skeleton({"root", "root/spine", "root/spine/head"});

  // Set identity bind/rest transforms
  std::vector<value::matrix4d> xforms(3, value::matrix4d::identity());
  skel.bindTransforms.set_value(xforms);
  skel.restTransforms.set_value(xforms);

  tydra::SkelNode dst;
  std::string err;

  bool ok = tydra::BuildSkelHierarchy(skel, dst, &err);

  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("Error: %s", err.c_str());
    return;
  }

  // Root
  TEST_CHECK(dst.joint_id == 0);
  TEST_CHECK(dst.joint_path == "root");

  // Root should have 1 child (spine)
  TEST_CHECK(dst.children.size() == 1);
  if (dst.children.size() == 1) {
    const tydra::SkelNode &spine = dst.children[0];
    TEST_CHECK(spine.joint_id == 1);
    TEST_CHECK(spine.joint_path == "root/spine");

    // Spine should have 1 child (head)
    TEST_CHECK(spine.children.size() == 1);
    if (spine.children.size() == 1) {
      const tydra::SkelNode &head = spine.children[0];
      TEST_CHECK(head.joint_id == 2);
      TEST_CHECK(head.joint_path == "root/spine/head");
      TEST_CHECK(head.children.empty());
    }
  }
}

// Branching: root with 2+ children
void build_skel_hierarchy_branching_test(void) {
  Skeleton skel = make_skeleton({
    "root", "root/left_arm", "root/right_arm", "root/spine"
  });

  std::vector<value::matrix4d> xforms(4, value::matrix4d::identity());
  skel.bindTransforms.set_value(xforms);
  skel.restTransforms.set_value(xforms);

  tydra::SkelNode dst;
  std::string err;

  bool ok = tydra::BuildSkelHierarchy(skel, dst, &err);

  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("Error: %s", err.c_str());
    return;
  }

  TEST_CHECK(dst.joint_id == 0);

  // Root should have 3 children: left_arm, right_arm, spine
  TEST_CHECK(dst.children.size() == 3);
  if (dst.children.size() == 3) {
    // Verify all children reference root as parent via their joint_id
    TEST_CHECK(dst.children[0].joint_id == 1);  // left_arm
    TEST_CHECK(dst.children[1].joint_id == 2);  // right_arm
    TEST_CHECK(dst.children[2].joint_id == 3);  // spine
  }
}

// Verify bind_transform and rest_transform are propagated to SkelNodes
void build_skel_hierarchy_transforms_test(void) {
  Skeleton skel = make_skeleton({"root", "root/child"});

  // Create distinct transforms
  value::matrix4d bind0 = value::matrix4d::identity();
  bind0.m[3][0] = 10.0;  // translate X

  value::matrix4d bind1 = value::matrix4d::identity();
  bind1.m[3][1] = 20.0;  // translate Y

  std::vector<value::matrix4d> binds = {bind0, bind1};
  skel.bindTransforms.set_value(binds);

  value::matrix4d rest0 = value::matrix4d::identity();
  rest0.m[3][2] = 5.0;  // translate Z

  value::matrix4d rest1 = value::matrix4d::identity();
  rest1.m[3][2] = 7.0;

  std::vector<value::matrix4d> rests = {rest0, rest1};
  skel.restTransforms.set_value(rests);

  tydra::SkelNode dst;
  std::string err;

  bool ok = tydra::BuildSkelHierarchy(skel, dst, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("Error: %s", err.c_str());
    return;
  }

  // Check root transforms
  TEST_CHECK(tinyusdz_test::float_equals(dst.bind_transform.m[3][0], 10.0));
  TEST_CHECK(tinyusdz_test::float_equals(dst.rest_transform.m[3][2], 5.0));

  // Check child transforms
  TEST_CHECK(dst.children.size() == 1);
  if (dst.children.size() == 1) {
    TEST_CHECK(tinyusdz_test::float_equals(dst.children[0].bind_transform.m[3][1], 20.0));
    TEST_CHECK(tinyusdz_test::float_equals(dst.children[0].rest_transform.m[3][2], 7.0));
  }
}

// restTransforms computed from bindTransforms when not authored
void build_skel_hierarchy_rest_fallback_test(void) {
  Skeleton skel = make_skeleton({"root", "root/child"});

  // Only set bindTransforms (no restTransforms)
  value::matrix4d bind0 = value::matrix4d::identity();
  bind0.m[3][0] = 2.0;  // root at X=2

  value::matrix4d bind1 = value::matrix4d::identity();
  bind1.m[3][0] = 5.0;  // child at X=5

  std::vector<value::matrix4d> binds = {bind0, bind1};
  skel.bindTransforms.set_value(binds);

  // Do NOT set restTransforms - should be computed from bindTransforms
  // restTransform[root] = bindTransform[root] = translate(2,0,0)
  // restTransform[child] = inverse(bindTransform[root]) * bindTransform[child]
  //                       = translate(-2,0,0) * translate(5,0,0) = translate(3,0,0)

  tydra::SkelNode dst;
  std::string err;

  bool ok = tydra::BuildSkelHierarchy(skel, dst, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("Error: %s", err.c_str());
    return;
  }

  // Root rest = bind (since it has no parent)
  TEST_CHECK(tinyusdz_test::float_equals(dst.rest_transform.m[3][0], 2.0));

  // Child rest = inv(parent_bind) * child_bind
  TEST_CHECK(dst.children.size() == 1);
  if (dst.children.size() == 1) {
    TEST_CHECK(tinyusdz_test::float_equals(dst.children[0].rest_transform.m[3][0], 3.0));
  }
}

// Returns false when joints not authored
void build_skel_hierarchy_no_joints_test(void) {
  Skeleton skel;
  skel.set_name("EmptySkel");
  // Do NOT set joints

  tydra::SkelNode dst;
  std::string err;

  bool ok = tydra::BuildSkelHierarchy(skel, dst, &err);
  TEST_CHECK(!ok);
  TEST_CHECK(!err.empty());
}

// Returns false for multi-rooted topology
void build_skel_hierarchy_multi_root_test(void) {
  // Two unrelated roots
  Skeleton skel = make_skeleton({"rootA", "rootB"});

  std::vector<value::matrix4d> xforms(2, value::matrix4d::identity());
  skel.bindTransforms.set_value(xforms);
  skel.restTransforms.set_value(xforms);

  tydra::SkelNode dst;
  std::string err;

  bool ok = tydra::BuildSkelHierarchy(skel, dst, &err);
  TEST_CHECK(!ok);
  TEST_CHECK(!err.empty());
}

// -----------------------------------------------------------------------
// BuildSkelNameToIndexMap test
// -----------------------------------------------------------------------

void build_skel_name_to_index_map_test(void) {
  // Build a hierarchy first
  Skeleton skel = make_skeleton({"root", "root/spine", "root/spine/head"});

  std::vector<value::matrix4d> xforms(3, value::matrix4d::identity());
  skel.bindTransforms.set_value(xforms);
  skel.restTransforms.set_value(xforms);

  tydra::SkelNode dst;
  std::string err;
  bool ok = tydra::BuildSkelHierarchy(skel, dst, &err);
  TEST_CHECK(ok);
  if (!ok) return;

  // Wrap in SkelHierarchy
  tydra::SkelHierarchy hier;
  hier.root_node = dst;

  std::map<std::string, int> nameMap = tydra::BuildSkelNameToIndexMap(hier);

  // All 3 joints should be present
  TEST_CHECK(nameMap.size() == 3);
  TEST_CHECK(nameMap.count("root") == 1);
  TEST_CHECK(nameMap.count("root/spine") == 1);
  TEST_CHECK(nameMap.count("root/spine/head") == 1);

  if (nameMap.count("root")) {
    TEST_CHECK(nameMap["root"] == 0);
  }
  if (nameMap.count("root/spine")) {
    TEST_CHECK(nameMap["root/spine"] == 1);
  }
  if (nameMap.count("root/spine/head")) {
    TEST_CHECK(nameMap["root/spine/head"] == 2);
  }
}

// -----------------------------------------------------------------------
// Bone utility tests (bone-util.hh)
// -----------------------------------------------------------------------

// CalculateBoneDepths: chain [-1, 0, 1, 2] -> [0, 1, 2, 3]
void bone_depths_linear_test(void) {
  std::vector<int> parents = {-1, 0, 1, 2};
  std::vector<int> depths;

  bool ok = tydra::CalculateBoneDepths(parents, depths);

  TEST_CHECK(ok);
  TEST_CHECK(depths.size() == 4);
  if (depths.size() == 4) {
    TEST_CHECK(depths[0] == 0);
    TEST_CHECK(depths[1] == 1);
    TEST_CHECK(depths[2] == 2);
    TEST_CHECK(depths[3] == 3);
  }
}

// CalculateBoneDepths: branching tree
// Index: 0=root, 1=childA(parent=0), 2=childB(parent=0), 3=grandchild(parent=1)
void bone_depths_branching_test(void) {
  std::vector<int> parents = {-1, 0, 0, 1};
  std::vector<int> depths;

  bool ok = tydra::CalculateBoneDepths(parents, depths);

  TEST_CHECK(ok);
  TEST_CHECK(depths.size() == 4);
  if (depths.size() == 4) {
    TEST_CHECK(depths[0] == 0);  // root
    TEST_CHECK(depths[1] == 1);  // childA
    TEST_CHECK(depths[2] == 1);  // childB
    TEST_CHECK(depths[3] == 2);  // grandchild of childA
  }
}

// FindBoneChainDistance: same=0, parent-child=1, sibling=2, invalid=-1
void bone_chain_distance_test(void) {
  // Chain: 0 -> 1 -> 2 -> 3
  std::vector<int> parents = {-1, 0, 1, 2};

  // Same bone: distance = 0
  TEST_CHECK(tydra::FindBoneChainDistance(1, 1, parents) == 0);

  // Parent-child: distance = 1
  TEST_CHECK(tydra::FindBoneChainDistance(0, 1, parents) == 1);

  // Grandparent-grandchild: distance = 2
  TEST_CHECK(tydra::FindBoneChainDistance(0, 2, parents) == 2);

  // Root to leaf: distance = 3
  TEST_CHECK(tydra::FindBoneChainDistance(0, 3, parents) == 3);

  // Branching: siblings
  // 0 -> 1, 0 -> 2
  std::vector<int> branching = {-1, 0, 0};
  // Sibling distance: 1(to root) + 1(from root) = 2
  TEST_CHECK(tydra::FindBoneChainDistance(1, 2, branching) == 2);

  // Invalid indices
  TEST_CHECK(tydra::FindBoneChainDistance(-1, 0, parents) == -1);
  TEST_CHECK(tydra::FindBoneChainDistance(0, 100, parents) == -1);
}

// -----------------------------------------------------------------------
// ReduceBoneInfluencesSimple tests
// -----------------------------------------------------------------------

// No-op when target >= element_size
void reduce_bones_simple_noop_test(void) {
  // 2 vertices, 4 influences each
  std::vector<int> indices = {0, 1, 2, 3, 4, 5, 6, 7};
  std::vector<float> weights = {0.4f, 0.3f, 0.2f, 0.1f, 0.5f, 0.3f, 0.1f, 0.1f};

  // Save copies
  std::vector<int> orig_indices = indices;
  std::vector<float> orig_weights = weights;

  // target=4 with element_size=4 => no-op
  bool ok = tydra::ReduceBoneInfluencesSimple(indices, weights, 4, 4, 2);
  TEST_CHECK(ok);

  // Arrays should be unchanged
  TEST_CHECK(indices == orig_indices);
  TEST_CHECK(weights.size() == orig_weights.size());

  // target=8 (larger) with element_size=4 => no-op
  ok = tydra::ReduceBoneInfluencesSimple(indices, weights, 4, 8, 2);
  TEST_CHECK(ok);
}

// Reduce 8->4 influences, verify weight renormalization
void reduce_bones_simple_basic_test(void) {
  // 1 vertex, 8 influences
  std::vector<int> indices =   {0,    1,    2,    3,    4,    5,    6,    7};
  std::vector<float> weights = {0.30f, 0.25f, 0.15f, 0.10f, 0.08f, 0.06f, 0.04f, 0.02f};

  bool ok = tydra::ReduceBoneInfluencesSimple(indices, weights, 8, 4, 1);
  TEST_CHECK(ok);

  // Should now be 4 influences
  TEST_CHECK(indices.size() == 4);
  TEST_CHECK(weights.size() == 4);

  if (indices.size() == 4) {
    // Top 4 by weight should be bones 0, 1, 2, 3
    TEST_CHECK(indices[0] == 0);
    TEST_CHECK(indices[1] == 1);
    TEST_CHECK(indices[2] == 2);
    TEST_CHECK(indices[3] == 3);
  }

  // Weights should sum to 1.0 (renormalized)
  if (weights.size() == 4) {
    float sum = weights[0] + weights[1] + weights[2] + weights[3];
    TEST_CHECK(tinyusdz_test::float_equals(sum, 1.0f, 1e-5f));
  }
}

// -----------------------------------------------------------------------
// ReduceBoneInfluences (advanced) tests
// -----------------------------------------------------------------------

// Greedy strategy with stats
void reduce_bones_greedy_test(void) {
  // 2 vertices, 8 influences each
  uint32_t num_verts = 2;
  uint32_t elem_size = 8;
  std::vector<int> indices(num_verts * elem_size);
  std::vector<float> weights(num_verts * elem_size);

  // Vertex 0: bones 0-7 with decreasing weights
  for (uint32_t i = 0; i < elem_size; i++) {
    indices[i] = int(i);
    weights[i] = 0.3f - float(i) * 0.03f;
  }
  // Vertex 1: bones 10-17
  for (uint32_t i = 0; i < elem_size; i++) {
    indices[elem_size + i] = int(10 + i);
    weights[elem_size + i] = 0.25f - float(i) * 0.02f;
  }

  tydra::BoneReductionConfig config;
  config.target_bone_count = 4;
  config.strategy = tydra::BoneReductionStrategy::Greedy;
  config.normalize_weights = true;

  tydra::BoneReductionStats stats;

  bool ok = tydra::ReduceBoneInfluences(indices, weights, elem_size, num_verts, config,
                                         nullptr, &stats);
  TEST_CHECK(ok);

  // Output should be 2 * 4 = 8 elements
  TEST_CHECK(indices.size() == 8);
  TEST_CHECK(weights.size() == 8);

  // Stats should be populated
  TEST_CHECK(stats.num_vertices == 2);
  TEST_CHECK(stats.original_bone_count == 8);
  TEST_CHECK(stats.target_bone_count == 4);

  // Weights for each vertex should sum to ~1.0
  if (weights.size() == 8) {
    float sum0 = weights[0] + weights[1] + weights[2] + weights[3];
    float sum1 = weights[4] + weights[5] + weights[6] + weights[7];
    TEST_CHECK(tinyusdz_test::float_equals(sum0, 1.0f, 1e-4f));
    TEST_CHECK(tinyusdz_test::float_equals(sum1, 1.0f, 1e-4f));
  }
}

// Hierarchical strategy with BoneHierarchyInfo
void reduce_bones_hierarchical_test(void) {
  // 1 vertex, 8 influences
  uint32_t num_verts = 1;
  uint32_t elem_size = 8;

  std::vector<int> indices =   {0, 1, 2, 3, 4, 5, 6, 7};
  std::vector<float> weights = {0.25f, 0.20f, 0.15f, 0.12f, 0.10f, 0.08f, 0.06f, 0.04f};

  // Bone hierarchy: 0 -> 1 -> 2 -> 3, 0 -> 4 -> 5 -> 6, 0 -> 7
  tydra::BoneHierarchyInfo hier;
  hier.parent_indices = {-1, 0, 1, 2, 0, 4, 5, 0};
  hier.bone_names = {"root", "spine", "chest", "head",
                     "left_arm", "left_forearm", "left_hand", "right_arm"};

  TEST_CHECK(hier.is_valid());

  tydra::BoneReductionConfig config;
  config.target_bone_count = 4;
  config.strategy = tydra::BoneReductionStrategy::Hierarchical;
  config.normalize_weights = true;

  tydra::BoneReductionStats stats;

  bool ok = tydra::ReduceBoneInfluences(indices, weights, elem_size, num_verts, config,
                                         &hier, &stats);
  TEST_CHECK(ok);

  // Output should be 1 * 4 = 4 elements
  TEST_CHECK(indices.size() == 4);
  TEST_CHECK(weights.size() == 4);

  // Weights should sum to 1.0
  if (weights.size() == 4) {
    float sum = weights[0] + weights[1] + weights[2] + weights[3];
    TEST_CHECK(tinyusdz_test::float_equals(sum, 1.0f, 1e-4f));
  }

  // Stats
  TEST_CHECK(stats.num_vertices == 1);
  TEST_CHECK(stats.target_bone_count == 4);
}
