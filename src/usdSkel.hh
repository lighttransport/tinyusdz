// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdSkel(includes BlendShapes)
#pragma once

#include "prim-types.hh"
#include "value-types.hh"

namespace tinyusdz {

constexpr auto kSkelRoot = "SkelRoot";
constexpr auto kSkeleton = "Skeleton";
constexpr auto kBlendShape = "BlendShape";

// BlendShapes
// TODO(syoyo): Blendshape
struct BlendShape {
  std::string name;

  std::vector<float> offsets;        // uniform vector3f[]. required property
  std::vector<float> normalOffsets;  // uniform vector3f[]. required property

  std::vector<int>
      pointIndices;  // uniform int[]. optional. vertex indices to the original mesh for each
                     // values in `offsets` and `normalOffsets`.
};

// Skeleton
struct Skeleton {
  std::string name;

  AnimatableExtent extent;

  std::vector<value::matrix4d>
      bindTransforms;  // uniform matrix4d[]. bind-pose transform of each joint in world coordinate.

  std::vector<std::string> jointNames; // uniform token[]
  std::vector<std::string> joints; // uniform token[]

  // rel proxyPrim

  std::vector<value::matrix4d> restTransforms;  // uniform matrix4d[] rest-pose transforms of each
                                                // joint in local coordinate.

  Purpose purpose{Purpose::Default};
  AnimatableVisibility visibility{Visibility::Inherited};

  std::vector<value::token> xformOpOrder;
};

struct SkelRoot {
  std::string name;
  int64_t parent_id{-1};

  AnimatableExtent extent;
  Purpose purpose{Purpose::Default};
  AnimatableVisibility visibility{Visibility::Inherited};

  // NOTE: SkelRoot itself does not have dedicated attributes in the schema.

  // ref proxyPrim
  std::vector<value::token> xformOpOrder;

  int64_t skeleton_id{-1};  // index to scene.skeletons
  // Skeleton skeleton;
};

struct SkelAnimation {
  std::string name;

  std::vector<value::token> blendShapes; // uniform token[]
  std::vector<float> blendShapeWeights; // float[]
  std::vector<value::token> joints; // uniform token[]
  std::vector<value::quatf> rotations;  // quatf[] Joint-local unit quaternion rotations
  std::vector<value::half3>
      scales;  // half3[] Joint-local scaling in 16bit half float. TODO: Use float3 for TinyUSDZ for convenience?
  std::vector<value::float3> translations;  // float3[] Joint-local translation.
};

// PackedJointAnimation is deprecated(Convert to SkelAnimation)
// struct PackedJointAnimation {
// };

// W.I.P.
struct SkelBindingAPI {
  value::matrix4d geomBindTransform;     // primvars:skel:geomBindTransform
  std::vector<int> jointIndices;         // primvars:skel:jointIndices
  std::vector<float> jointWeights;       // primvars:skel:jointWeights
  std::vector<std::string> blendShapes;  // optional?
  std::vector<std::string> joints;       // optional

  int64_t animationSource{
      -1};  // index to Scene.animations. ref skel:animationSource
  int64_t blendShapeTargets{
      -1};  // index to Scene.blendshapes. ref skel:bindShapeTargets
  int64_t skeleton{-1};  // index to Scene.skeletons. // ref skel:skeleton
};

// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

// Geom
DEFINE_TYPE_TRAIT(SkelRoot, kSkelRoot, TYPE_ID_SKEL_ROOT, 1);
DEFINE_TYPE_TRAIT(Skeleton, kSkeleton, TYPE_ID_SKELETON, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
