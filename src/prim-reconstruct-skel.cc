// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Reconstruct Skeleton Prims - split from prim-reconstruct.cc for parallel compilation
//
#include "prim-reconstruct-internal.hh"
#include "usdGeom.hh"
#include "usdSkel.hh"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) if (err) { (*err) = s + (*err); }
#define PushWarn(s) if (warn) { (*warn) = s + (*err); }

#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))
#define PUSH_ERROR_AND_RETURN_F(s, ...) PUSH_ERROR_AND_RETURN(fmt::format(s, __VA_ARGS__))

namespace tinyusdz {
namespace prim {

// Include implementation helpers
#include "prim-reconstruct-impl.inc"

template <>
bool ReconstructPrim<SkelRoot>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SkelRoot *root,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &root->xformOps, err)) {
    return false;
  }

  // SkelRoot is something like a grouping node, having 1 Skeleton and possibly?
  // multiple Prim hierarchy containing GeomMesh.
  // No specific properties for SkelRoot(AFAIK)

  // custom props only
  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, SkelRoot,
                   root->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, SkelRoot,
                       root->purpose, options.strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, SkelRoot, root->extent)
    ADD_PROPERTY(table, prop, SkelRoot, root->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Skeleton>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Skeleton *skel,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &skel->xformOps, err)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ Skeleton
#define PRIM_PTR_ skel
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {

    // SkelBindingAPI: animationSource relationship
    if (prop.first == kSkelAnimationSource) {
      // Must be relation of type Path.
      if (prop.second.is_relationship() && prop.second.get_relationship().is_path()) {
        const Relationship &rel = prop.second.get_relationship();
        if (rel.is_path()) {
          skel->animationSource = rel;
          table.insert(kSkelAnimationSource);
        } else {
          PUSH_ERROR_AND_RETURN("`" << kSkelAnimationSource << "` target must be Path.");
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "`" << kSkelAnimationSource << "` must be a Relationship with Path target.");
      }
    }

    SKELETON_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, Skeleton,
                   skel->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "purpose", Purpose, PurposeEnumHandler, Skeleton,
                       skel->purpose, options.strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, "extent", Skeleton, skel->extent)
    ADD_PROPERTY(table, prop, Skeleton, skel->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  // Validate bindTransforms & restTransforms
  // (usdview and Houdini expect both to be authored, and their lengths must match)
  {
    bool hasBindTransforms = skel->bindTransforms.has_value();
    bool hasRestTransforms = skel->restTransforms.has_value();

    if (hasBindTransforms != hasRestTransforms) {
      PUSH_WARN("Skeleton: Only one of `bindTransforms` or `restTransforms` is authored. "
                "Both should be authored for compatibility with usdview and Houdini.");
    }

    if (hasBindTransforms && hasRestTransforms) {
      auto bindVal = skel->bindTransforms.get_value();
      auto restVal = skel->restTransforms.get_value();
      if (bindVal && restVal) {
        if (bindVal->size() != restVal->size()) {
          PUSH_WARN("Skeleton: `bindTransforms` (size=" << bindVal->size()
                    << ") and `restTransforms` (size=" << restVal->size()
                    << ") have different lengths. They should match.");
        }
      }
    }
  }

  return true;
}

template <>
bool ReconstructPrim<SkelAnimation>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SkelAnimation *skelanim,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)warn;
  (void)references;
  (void)options;

  std::set<std::string> table;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ SkelAnimation
#define PRIM_PTR_ skelanim
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    SKEL_ANIMATION_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, SkelAnimation, skelanim->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<BlendShape>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    BlendShape *bs,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;

  DCOUT("Reconstruct BlendShape");

  std::set<std::string> table;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ BlendShape
#define PRIM_PTR_ bs
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    BLEND_SHAPE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, BlendShape, bs->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  // Check required properties exist in strict mode
  // (`offsets` and `normalOffsets` are required properties per USD spec)
  {
    bool hasOffsets = bs->offsets.has_value();
    bool hasNormalOffsets = bs->normalOffsets.has_value();

    if (!hasOffsets) {
      if (options.strict_allowedToken_check) {
        PUSH_ERROR_AND_RETURN("BlendShape: Required property `offsets` is not authored.");
      } else {
        PUSH_WARN("BlendShape: Required property `offsets` is not authored.");
      }
    }

    if (!hasNormalOffsets) {
      if (options.strict_allowedToken_check) {
        PUSH_ERROR_AND_RETURN("BlendShape: Required property `normalOffsets` is not authored.");
      } else {
        PUSH_WARN("BlendShape: Required property `normalOffsets` is not authored.");
      }
    }
  }

  return true;
}

template <>
bool ReconstructPrim<GPrim>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GPrim *gprim,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)gprim;
  (void)err;

  (void)references;
  (void)properties;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, gprim, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  return true;
}

template <>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomBasisCurves *curves,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;

  DCOUT("GeomBasisCurves");

  // Use centralized enum handlers
  auto BasisHandler = enum_handler::BasisCurvesBasis;
  auto TypeHandler = enum_handler::BasisCurvesType;
  auto WrapHandler = enum_handler::BasisCurvesWrap;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, curves, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomBasisCurves
#define PRIM_PTR_ curves
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_BASIS_CURVES_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_BASIS_CURVES_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomBasisCurves, curves->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomNurbsCurves *curves,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;
  (void)options;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, curves, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_TYPED_ATTRIBUTE(table, prop, "curveVertexCounts", GeomNurbsCurves,
                         curves->curveVertexCounts)
    PARSE_TYPED_ATTRIBUTE(table, prop, "points", GeomNurbsCurves, curves->points)
    PARSE_TYPED_ATTRIBUTE(table, prop, "velocities", GeomNurbsCurves,
                          curves->velocities)
    PARSE_TYPED_ATTRIBUTE(table, prop, "normals", GeomNurbsCurves,
                  curves->normals)
    PARSE_TYPED_ATTRIBUTE(table, prop, "accelerations", GeomNurbsCurves,
                 curves->accelerations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "widths", GeomNurbsCurves, curves->widths)

    //
    PARSE_TYPED_ATTRIBUTE(table, prop, "order", GeomNurbsCurves, curves->order)
    PARSE_TYPED_ATTRIBUTE(table, prop, "knots", GeomNurbsCurves, curves->knots)
    PARSE_TYPED_ATTRIBUTE(table, prop, "ranges", GeomNurbsCurves, curves->ranges)
    PARSE_TYPED_ATTRIBUTE(table, prop, "pointWeights", GeomNurbsCurves, curves->pointWeights)

    ADD_PROPERTY(table, prop, GeomBasisCurves, curves->props)

    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

// PrimSpec versions
#define RECONSTRUCT_PRIM_PRIMSPEC_IMPL(__prim_ty) \
template <> \
bool ReconstructPrim<__prim_ty>( \
    PrimSpec &primspec, \
    __prim_ty *prim, \
    std::string *warn, \
    std::string *err, \
    const PrimReconstructOptions &options) { \
  ReferenceList references; \
  return ReconstructPrim<__prim_ty>(primspec.specifier(), primspec.props(), references, prim, warn, err, options); \
}

RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SkelRoot)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Skeleton)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SkelAnimation)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(BlendShape)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomBasisCurves)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomNurbsCurves)

}  // namespace prim
}  // namespace tinyusdz
