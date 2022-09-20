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
constexpr auto kSkelAnimation = "SkelAnimation";
constexpr auto kBlendShape = "BlendShape";

// BlendShapes
// TODO(syoyo): Blendshape
struct BlendShape {
  std::string name;
  Specifier spec;

  TypedAttribute<std::vector<value::vector3f>> offsets;        // uniform vector3f[]. required property
  TypedAttribute<std::vector<value::vector3f>> normalOffsets;  // uniform vector3f[]. required property

  TypedAttribute<std::vector<int>>
      pointIndices;  // uniform int[]. optional. vertex indices to the original mesh for each
                     // values in `offsets` and `normalOffsets`.
                    
  std::map<std::string, Property> props;
  PrimMeta meta;
};

// Skeleton
struct Skeleton {
  std::string name;
  Specifier spec;

  TypedAttribute<Animatable<Extent>> extent;

  TypedAttribute<std::vector<value::matrix4d>> bindTransforms;  // uniform matrix4d[]. bind-pose transform of each joint in world coordinate.

  TypedAttribute<std::vector<value::token>> jointNames; // uniform token[]
  TypedAttribute<std::vector<value::token>> joints; // uniform token[]

  TypedAttribute<std::vector<value::matrix4d>> restTransforms;  // uniform matrix4d[] rest-pose transforms of each
                                                // joint in local coordinate.

  // rel proxyPrim
  

  // SkelBindingAPI
  nonstd::optional<Path> animationSource; // rel skel:animationSource = </path/...>
  

  Purpose purpose{Purpose::Default};
  Animatable<Visibility> visibility{Visibility::Inherited};

  std::map<std::string, Property> props;
  std::vector<value::token> xformOpOrder;

  PrimMeta meta;
};

// NOTE: SkelRoot itself does not have dedicated attributes in the schema.
struct SkelRoot {
  std::string name;
  Specifier spec;
  int64_t parent_id{-1};

  Animatable<Extent> extent;
  Purpose purpose{Purpose::Default};
  Animatable<Visibility> visibility{Visibility::Inherited};

  // ref proxyPrim
  std::vector<XformOp> xformOps;

  std::map<std::string, Property> props;
  PrimMeta meta;

  // TODO: Add function to check if SkelRoot contains `Skeleton` and `GeomMesh` node?;

};

struct SkelAnimation {
  std::string name;
  Specifier spec;

  TypedAttribute<std::vector<value::token>> blendShapes; // uniform token[]
  TypedAttribute<Animatable<std::vector<float>>> blendShapeWeights; // float[]
  TypedAttribute<std::vector<value::token>> joints; // uniform token[]
  TypedAttribute<Animatable<std::vector<value::quatf>>> rotations;  // quatf[] Joint-local unit quaternion rotations
  TypedAttribute<Animatable<std::vector<value::half3>>>
      scales;  // half3[] Joint-local scaling in 16bit half float. TODO: Use float3 for TinyUSDZ for convenience?
  TypedAttribute<Animatable<std::vector<value::float3>>> translations;  // float3[] Joint-local translation.

  std::map<std::string, Property> props;
  PrimMeta meta;
};

// PackedJointAnimation is deprecated(Convert to SkelAnimation)
// struct PackedJointAnimation {
// };

#if 0
// W.I.P.
struct SkelBindingAPI {
  value::matrix4d geomBindTransform;     // primvars:skel:geomBindTransform
  std::vector<int> jointIndices;         // primvars:skel:jointIndices
  std::vector<float> jointWeights;       // primvars:skel:jointWeights
  std::vector<value::token> blendShapes;  // optional?
  std::vector<value::token> joints;       // optional

  int64_t animationSource{
      -1};  // index to Scene.animations. ref skel:animationSource
  int64_t blendShapeTargets{
      -1};  // index to Scene.blendshapes. ref skel:bindShapeTargets
  int64_t skeleton{-1};  // index to Scene.skeletons. // ref skel:skeleton
};
#endif

// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

// Geom
DEFINE_TYPE_TRAIT(SkelRoot, kSkelRoot, TYPE_ID_SKEL_ROOT, 1);
DEFINE_TYPE_TRAIT(Skeleton, kSkeleton, TYPE_ID_SKELETON, 1);
DEFINE_TYPE_TRAIT(SkelAnimation, kSkelAnimation, TYPE_ID_SKELANIMATION, 1);
DEFINE_TYPE_TRAIT(BlendShape, kBlendShape, TYPE_ID_BLENDSHAPE, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
