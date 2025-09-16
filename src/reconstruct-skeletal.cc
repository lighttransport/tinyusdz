// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Skeletal animation primitive reconstruction - Implementation

#include "reconstruct-skeletal.hh"
#include "reconstruct-common.hh"
#include "prim-reconstruct.hh"
#include "str-util.hh"
#include "common-macros.inc"
#include "usdSkel.hh"

namespace tinyusdz {
namespace prim {

template <>
bool ReconstructPrim<SkelRoot>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    SkelRoot *root,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;

  std::set<std::string> table;
  // Note: XformOps will be handled by transform reconstruction
  // if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &root->xformOps, err)) {
  //   return false;
  // }

  // SkelRoot is something like a grouping node, having 1 Skeleton and possibly
  // multiple Prim hierarchy containing GeomMesh.
  // No specific properties for SkelRoot(AFAIK)

  // custom props only
  for (const auto &prop : properties) {
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, SkelRoot,
                   root->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, SkelRoot,
                       root->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, SkelRoot, root->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Skeleton>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    Skeleton *skeleton,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;

  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "joints", Skeleton, skeleton->joints)
    PARSE_TYPED_ATTRIBUTE(table, prop, "jointNames", Skeleton, skeleton->jointNames)
    PARSE_TYPED_ATTRIBUTE(table, prop, "bindTransforms", Skeleton, skeleton->bindTransforms)
    PARSE_TYPED_ATTRIBUTE(table, prop, "restTransforms", Skeleton, skeleton->restTransforms)

    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, Skeleton,
                   skeleton->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, Skeleton,
                       skeleton->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, Skeleton, skeleton->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<SkelAnimation>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    SkelAnimation *anim,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;

  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "joints", SkelAnimation, anim->joints)
    PARSE_TYPED_ATTRIBUTE(table, prop, "translations", SkelAnimation, anim->translations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "rotations", SkelAnimation, anim->rotations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "scales", SkelAnimation, anim->scales)
    PARSE_TYPED_ATTRIBUTE(table, prop, "blendShapes", SkelAnimation, anim->blendShapes)
    PARSE_TYPED_ATTRIBUTE(table, prop, "blendShapeWeights", SkelAnimation, anim->blendShapeWeights)

    ADD_PROPERTY(table, prop, SkelAnimation, anim->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<BlendShape>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    BlendShape *blendShape,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;

  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "offsets", BlendShape, blendShape->offsets)
    PARSE_TYPED_ATTRIBUTE(table, prop, "normalOffsets", BlendShape, blendShape->normalOffsets)
    PARSE_TYPED_ATTRIBUTE(table, prop, "pointIndices", BlendShape, blendShape->pointIndices)

    ADD_PROPERTY(table, prop, BlendShape, blendShape->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

} // namespace prim
} // namespace tinyusdz