// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// Unit tests for usdSkel core types (Skeleton, SkelAnimation, BlendShape)
// and BuildSkelTopology()

#ifdef _MSC_VER
#define NOMINMAX
#endif

#include "unit-usdskel.h"

#define TEST_NO_MAIN
#include "acutest.h"
#include "unit-common.hh"

#include "usdSkel.hh"
#include "value-types.hh"

using namespace tinyusdz;

// -----------------------------------------------------------------------
// BuildSkelTopology tests
// -----------------------------------------------------------------------

// Linear chain: root/spine/head -> parent indices [-1, 0, 1]
void skel_topology_simple_chain_test(void) {
  std::vector<value::token> joints;
  joints.push_back(value::token("root"));
  joints.push_back(value::token("root/spine"));
  joints.push_back(value::token("root/spine/head"));

  std::vector<int> dst;
  std::string err;

  bool ok = BuildSkelTopology(joints, dst, &err);

  TEST_CHECK(ok);
  TEST_CHECK(err.empty());
  TEST_CHECK(dst.size() == 3);

  if (dst.size() == 3) {
    TEST_CHECK(dst[0] == -1);  // root has no parent
    TEST_CHECK(dst[1] == 0);   // spine's parent is root
    TEST_CHECK(dst[2] == 1);   // head's parent is spine
  }
}

// Branching: root + left_arm + right_arm + spine/head
void skel_topology_branching_test(void) {
  std::vector<value::token> joints;
  joints.push_back(value::token("root"));
  joints.push_back(value::token("root/left_arm"));
  joints.push_back(value::token("root/right_arm"));
  joints.push_back(value::token("root/spine"));
  joints.push_back(value::token("root/spine/head"));

  std::vector<int> dst;
  std::string err;

  bool ok = BuildSkelTopology(joints, dst, &err);

  TEST_CHECK(ok);
  TEST_CHECK(err.empty());
  TEST_CHECK(dst.size() == 5);

  if (dst.size() == 5) {
    TEST_CHECK(dst[0] == -1);  // root
    TEST_CHECK(dst[1] == 0);   // left_arm -> root
    TEST_CHECK(dst[2] == 0);   // right_arm -> root
    TEST_CHECK(dst[3] == 0);   // spine -> root
    TEST_CHECK(dst[4] == 3);   // head -> spine
  }
}

// Single joint -> [-1]; empty input -> true with empty dst
void skel_topology_single_joint_test(void) {
  // Single joint
  {
    std::vector<value::token> joints;
    joints.push_back(value::token("root"));

    std::vector<int> dst;
    std::string err;

    bool ok = BuildSkelTopology(joints, dst, &err);
    TEST_CHECK(ok);
    TEST_CHECK(dst.size() == 1);
    if (dst.size() == 1) {
      TEST_CHECK(dst[0] == -1);
    }
  }

  // Empty input
  {
    std::vector<value::token> joints;
    std::vector<int> dst;
    std::string err;

    bool ok = BuildSkelTopology(joints, dst, &err);
    TEST_CHECK(ok);
    TEST_CHECK(dst.empty());
  }
}

// Ancestor recursion: ["a", "a/b/c"] -> [-1, 0]
// "a/b" is not in the joint list, so "a" becomes the parent of "a/b/c"
void skel_topology_skipped_hierarchy_test(void) {
  std::vector<value::token> joints;
  joints.push_back(value::token("a"));
  joints.push_back(value::token("a/b/c"));

  std::vector<int> dst;
  std::string err;

  bool ok = BuildSkelTopology(joints, dst, &err);

  TEST_CHECK(ok);
  TEST_CHECK(dst.size() == 2);

  if (dst.size() == 2) {
    TEST_CHECK(dst[0] == -1);  // "a" is root
    TEST_CHECK(dst[1] == 0);   // "a/b/c"'s closest ancestor in map is "a"
  }
}

// Root path "/" and empty string should be rejected
void skel_topology_error_test(void) {
  // Root path "/"
  {
    std::vector<value::token> joints;
    joints.push_back(value::token("/"));

    std::vector<int> dst;
    std::string err;

    bool ok = BuildSkelTopology(joints, dst, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(!err.empty());
  }

  // Empty string
  {
    std::vector<value::token> joints;
    joints.push_back(value::token(""));

    std::vector<int> dst;
    std::string err;

    bool ok = BuildSkelTopology(joints, dst, &err);
    TEST_CHECK(!ok);
    TEST_CHECK(!err.empty());
  }
}

// -----------------------------------------------------------------------
// Skeleton struct attribute tests
// -----------------------------------------------------------------------

void skeleton_struct_attributes_test(void) {
  Skeleton skel;
  skel.set_name("TestSkeleton");

  // Set joints
  std::vector<value::token> joint_tokens;
  joint_tokens.push_back(value::token("root"));
  joint_tokens.push_back(value::token("root/spine"));
  skel.joints.set_value(joint_tokens);

  // Verify joints
  std::vector<value::token> out_joints;
  TEST_CHECK(skel.joints.get_value(&out_joints));
  TEST_CHECK(out_joints.size() == 2);
  if (out_joints.size() == 2) {
    TEST_CHECK(out_joints[0].str() == "root");
    TEST_CHECK(out_joints[1].str() == "root/spine");
  }

  // Set bindTransforms
  std::vector<value::matrix4d> binds;
  binds.push_back(value::matrix4d::identity());
  value::matrix4d m2 = value::matrix4d::identity();
  m2.m[3][0] = 1.0;  // translate X by 1
  binds.push_back(m2);
  skel.bindTransforms.set_value(binds);

  // Verify bindTransforms
  std::vector<value::matrix4d> out_binds;
  TEST_CHECK(skel.bindTransforms.get_value(&out_binds));
  TEST_CHECK(out_binds.size() == 2);
  if (out_binds.size() == 2) {
    TEST_CHECK(tinyusdz_test::float_equals(out_binds[1].m[3][0], 1.0));
  }

  // Set restTransforms
  std::vector<value::matrix4d> rests;
  rests.push_back(value::matrix4d::identity());
  rests.push_back(value::matrix4d::identity());
  skel.restTransforms.set_value(rests);

  std::vector<value::matrix4d> out_rests;
  TEST_CHECK(skel.restTransforms.get_value(&out_rests));
  TEST_CHECK(out_rests.size() == 2);

  // Verify authored() works
  TEST_CHECK(skel.joints.authored());
  TEST_CHECK(skel.bindTransforms.authored());
  TEST_CHECK(skel.restTransforms.authored());

  // jointNames should not be authored if not set
  TEST_CHECK(!skel.jointNames.authored());
}

// -----------------------------------------------------------------------
// SkelAnimation tests
// -----------------------------------------------------------------------

// Static (default) values for rotations/translations/scales
void skelanim_static_values_test(void) {
  SkelAnimation anim;
  anim.set_name("TestAnim");

  // Set joints
  std::vector<value::token> joints;
  joints.push_back(value::token("root"));
  joints.push_back(value::token("root/spine"));
  anim.joints.set_value(joints);

  // Set static rotations
  {
    Animatable<std::vector<value::quatf>> rot_anim;
    std::vector<value::quatf> rots;
    value::quatf identity_q;
    identity_q.imag = {0.0f, 0.0f, 0.0f};
    identity_q.real = 1.0f;
    rots.push_back(identity_q);
    rots.push_back(identity_q);
    rot_anim.set(rots);
    anim.rotations.set_value(rot_anim);
  }

  // Set static translations
  {
    Animatable<std::vector<value::float3>> trans_anim;
    std::vector<value::float3> trans;
    trans.push_back(value::float3({0.0f, 0.0f, 0.0f}));
    trans.push_back(value::float3({0.0f, 1.0f, 0.0f}));
    trans_anim.set(trans);
    anim.translations.set_value(trans_anim);
  }

  // Set static scales
  {
    Animatable<std::vector<value::half3>> scale_anim;
    std::vector<value::half3> scales;
    value::half h1 = value::float_to_half_full(1.0f);
    value::half3 s = {h1, h1, h1};
    scales.push_back(s);
    scales.push_back(s);
    scale_anim.set(scales);
    anim.scales.set_value(scale_anim);
  }

  // Retrieve and verify
  std::vector<value::quatf> out_rots;
  TEST_CHECK(anim.get_rotations(&out_rots));
  TEST_CHECK(out_rots.size() == 2);

  std::vector<value::float3> out_trans;
  TEST_CHECK(anim.get_translations(&out_trans));
  TEST_CHECK(out_trans.size() == 2);
  if (out_trans.size() == 2) {
    TEST_CHECK(tinyusdz_test::float_equals(out_trans[1][1], 1.0f));
  }

  std::vector<value::half3> out_scales;
  TEST_CHECK(anim.get_scales(&out_scales));
  TEST_CHECK(out_scales.size() == 2);

  // Get joints
  std::vector<value::token> out_joints;
  TEST_CHECK(anim.get_joints(&out_joints));
  TEST_CHECK(out_joints.size() == 2);
}

// Time-sampled translations at t=0, 1, 2
void skelanim_timesampled_test(void) {
  SkelAnimation anim;
  anim.set_name("TimeSampledAnim");

  // Create time-sampled translations
  {
    Animatable<std::vector<value::float3>> trans_anim;

    std::vector<value::float3> t0;
    t0.push_back(value::float3({0.0f, 0.0f, 0.0f}));
    trans_anim.add_sample(0.0, t0);

    std::vector<value::float3> t1;
    t1.push_back(value::float3({1.0f, 0.0f, 0.0f}));
    trans_anim.add_sample(1.0, t1);

    std::vector<value::float3> t2;
    t2.push_back(value::float3({2.0f, 0.0f, 0.0f}));
    trans_anim.add_sample(2.0, t2);

    anim.translations.set_value(trans_anim);
  }

  // Get at t=0
  {
    std::vector<value::float3> out;
    bool ok = anim.get_translations(&out, 0.0, value::TimeSampleInterpolationType::Held);
    TEST_CHECK(ok);
    TEST_CHECK(out.size() == 1);
    if (out.size() == 1) {
      TEST_CHECK(tinyusdz_test::float_equals(out[0][0], 0.0f));
    }
  }

  // Get at t=1
  {
    std::vector<value::float3> out;
    bool ok = anim.get_translations(&out, 1.0, value::TimeSampleInterpolationType::Held);
    TEST_CHECK(ok);
    TEST_CHECK(out.size() == 1);
    if (out.size() == 1) {
      TEST_CHECK(tinyusdz_test::float_equals(out[0][0], 1.0f));
    }
  }

  // Get at t=2
  {
    std::vector<value::float3> out;
    bool ok = anim.get_translations(&out, 2.0, value::TimeSampleInterpolationType::Held);
    TEST_CHECK(ok);
    TEST_CHECK(out.size() == 1);
    if (out.size() == 1) {
      TEST_CHECK(tinyusdz_test::float_equals(out[0][0], 2.0f));
    }
  }
}

// -----------------------------------------------------------------------
// BlendShape tests
// -----------------------------------------------------------------------

void blendshape_struct_test(void) {
  BlendShape bs;
  bs.set_name("TestBlendShape");

  TEST_CHECK(bs.get_name() == "TestBlendShape");

  // Set offsets
  std::vector<value::vector3f> offsets;
  offsets.push_back(value::vector3f({0.1f, 0.0f, 0.0f}));
  offsets.push_back(value::vector3f({0.0f, 0.2f, 0.0f}));
  offsets.push_back(value::vector3f({0.0f, 0.0f, 0.3f}));
  bs.offsets.set_value(offsets);

  // Set normalOffsets
  std::vector<value::vector3f> normalOffsets;
  normalOffsets.push_back(value::vector3f({1.0f, 0.0f, 0.0f}));
  normalOffsets.push_back(value::vector3f({0.0f, 1.0f, 0.0f}));
  normalOffsets.push_back(value::vector3f({0.0f, 0.0f, 1.0f}));
  bs.normalOffsets.set_value(normalOffsets);

  // Set pointIndices
  std::vector<int> pointIndices;
  pointIndices.push_back(0);
  pointIndices.push_back(5);
  pointIndices.push_back(10);
  bs.pointIndices.set_value(pointIndices);

  // Verify offsets
  std::vector<value::vector3f> out_offsets;
  TEST_CHECK(bs.offsets.get_value(&out_offsets));
  TEST_CHECK(out_offsets.size() == 3);
  if (out_offsets.size() == 3) {
    TEST_CHECK(tinyusdz_test::float_equals(out_offsets[0][0], 0.1f));
    TEST_CHECK(tinyusdz_test::float_equals(out_offsets[1][1], 0.2f));
    TEST_CHECK(tinyusdz_test::float_equals(out_offsets[2][2], 0.3f));
  }

  // Verify normalOffsets
  std::vector<value::vector3f> out_normals;
  TEST_CHECK(bs.normalOffsets.get_value(&out_normals));
  TEST_CHECK(out_normals.size() == 3);

  // Verify pointIndices
  std::vector<int> out_indices;
  TEST_CHECK(bs.pointIndices.get_value(&out_indices));
  TEST_CHECK(out_indices.size() == 3);
  if (out_indices.size() == 3) {
    TEST_CHECK(out_indices[0] == 0);
    TEST_CHECK(out_indices[1] == 5);
    TEST_CHECK(out_indices[2] == 10);
  }
}
