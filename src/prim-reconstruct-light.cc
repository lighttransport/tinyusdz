// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Reconstruct Light Prims - split from prim-reconstruct.cc for parallel compilation
//
#include "prim-reconstruct-internal.hh"
#include "usdLux.hh"
#include "usdShade.hh"  // For UsdUVTexture (used by RectLight)

// For PUSH_ERROR_AND_RETURN
#define PushError(s) if (err) { (*err) = s + (*err); }
#define PushWarn(s) if (warn) { (*warn) = s + (*err); }

#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))
#define PUSH_ERROR_AND_RETURN_F(s, ...) PUSH_ERROR_AND_RETURN(fmt::format(s, __VA_ARGS__))

namespace tinyusdz {
namespace prim {

// Include implementation helpers
#include "prim-reconstruct-impl.inc"

// ============================================================================
// Generic macro for light prim reconstruction
// ============================================================================
// Consolidates the common pattern for all light types:
// SphereLight, RectLight, DiskLight, CylinderLight, DistantLight, GeometryLight, DomeLight
//
// IMPORTANT: Caller must define PRIM_CLASS_ and PRIM_PTR_ macros before calling
//            this macro, and undef them afterward. These are required by
//            EXPAND_TYPED_ATTR macros.
//
// Parameters:
//   LightClass: The light class (e.g., SphereLight, RectLight)
//   light_ptr: Pointer to the light instance
//   TYPED_ATTRS: Property table macro (e.g., SPHERE_LIGHT_TYPED_ATTRS)
//   COMMON_ATTRS: Light common attrs macro (LIGHT_COMMON_ATTRS_WITH_SHAPING or LIGHT_COMMON_ATTRS_NO_SHAPING)
//   EXTENT_HANDLING: Either PARSE_EXTENT_ATTRIBUTE(...) or /* no extent */
//   SPECIAL_HANDLING: Special attribute handling for exceptions like RectLight's texture:file or /* no special handling */
#define RECONSTRUCT_LIGHT_PRIM_BODY(LightClass, light_ptr, TYPED_ATTRS, COMMON_ATTRS, EXTENT_HANDLING, SPECIAL_HANDLING) \
  (void)references; \
  \
  std::set<std::string> table; \
  \
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &light_ptr->xformOps, err)) { \
    return false; \
  } \
  \
  for (auto &prop : properties) { \
    SPECIAL_HANDLING \
    TYPED_ATTRS(EXPAND_TYPED_ATTR) \
    COMMON_ATTRS(EXPAND_TYPED_ATTR) \
    EXTENT_HANDLING \
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, LightClass, \
                       light_ptr->visibility, options.strict_allowedToken_check) \
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, LightClass, \
                       light_ptr->purpose, options.strict_allowedToken_check) \
    ADD_PROPERTY(table, prop, LightClass, light_ptr->props) \
    PARSE_PROPERTY_END_MAKE_WARN(table, prop) \
  } \
  \
  return true;

template <>
bool ReconstructPrim<SphereLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SphereLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ SphereLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(SphereLight, light, SPHERE_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, SphereLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<RectLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    RectLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ RectLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  // Special case: texture:file uses UsdUVTexture type
  RECONSTRUCT_LIGHT_PRIM_BODY(RectLight, light, RECT_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, RectLight, light->extent),
                              PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:texture:file", UsdUVTexture, light->file))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DiskLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DiskLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DiskLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(DiskLight, light, DISK_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, DiskLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<CylinderLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    CylinderLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ CylinderLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(CylinderLight, light, CYLINDER_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, CylinderLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DistantLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DistantLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DistantLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(DistantLight, light, DISTANT_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeometryLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeometryLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeometryLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(GeometryLight, light, GEOMETRY_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DomeLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DomeLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DomeLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  DCOUT("Implement DomeLight");
  RECONSTRUCT_LIGHT_PRIM_BODY(DomeLight, light, DOME_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
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

RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SphereLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(RectLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DiskLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(CylinderLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DistantLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeometryLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DomeLight)

}  // namespace prim
}  // namespace tinyusdz
