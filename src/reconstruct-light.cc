// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Light primitive reconstruction - Implementation

#include "reconstruct-light.hh"
#include "reconstruct-common.hh"
#include "prim-reconstruct.hh"
#include "str-util.hh"
#include "common-macros.inc"
#include "usdLux.hh"

namespace tinyusdz {
namespace prim {

template <>
bool ReconstructPrim<SphereLight>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    SphereLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;
  std::set<std::string> table;

  // Note: XformOps will be handled by transform reconstruction
  // if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &light->xformOps, err)) {
  //   return false;
  // }

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:color", SphereLight, light->color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:radius", SphereLight, light->radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:intensity", SphereLight,
                   light->intensity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:enable", SphereLight, light->shadowEnable)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:color", SphereLight, light->shadowColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:distance", SphereLight, light->shadowDistance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloff", SphereLight, light->shadowFalloff)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloffGamma", SphereLight, light->shadowFalloffGamma)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shaping:focus", SphereLight, light->shapingFocus)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shaping:focusTint", SphereLight, light->shapingFocusTint)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shaping:cone:angle", SphereLight, light->shapingConeAngle)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shaping:cone:softness", SphereLight, light->shapingConeSoftness)

    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, SphereLight,
                   light->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, SphereLight,
                       light->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, SphereLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<RectLight>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    RectLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;
  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:color", RectLight, light->color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:width", RectLight, light->width)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:height", RectLight, light->height)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:intensity", RectLight, light->intensity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:enable", RectLight, light->shadowEnable)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:color", RectLight, light->shadowColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:distance", RectLight, light->shadowDistance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloff", RectLight, light->shadowFalloff)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloffGamma", RectLight, light->shadowFalloffGamma)

    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, RectLight,
                   light->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, RectLight,
                       light->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, RectLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<DiskLight>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    DiskLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;
  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:color", DiskLight, light->color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:radius", DiskLight, light->radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:intensity", DiskLight, light->intensity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:enable", DiskLight, light->shadowEnable)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:color", DiskLight, light->shadowColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:distance", DiskLight, light->shadowDistance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloff", DiskLight, light->shadowFalloff)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloffGamma", DiskLight, light->shadowFalloffGamma)

    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, DiskLight,
                   light->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, DiskLight,
                       light->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, DiskLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<CylinderLight>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    CylinderLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;
  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:color", CylinderLight, light->color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:radius", CylinderLight, light->radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:length", CylinderLight, light->length)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:intensity", CylinderLight, light->intensity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:enable", CylinderLight, light->shadowEnable)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:color", CylinderLight, light->shadowColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:distance", CylinderLight, light->shadowDistance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloff", CylinderLight, light->shadowFalloff)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloffGamma", CylinderLight, light->shadowFalloffGamma)

    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, CylinderLight,
                   light->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, CylinderLight,
                       light->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, CylinderLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<DistantLight>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    DistantLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;
  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:color", DistantLight, light->color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:angle", DistantLight, light->angle)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:intensity", DistantLight, light->intensity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:enable", DistantLight, light->shadowEnable)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:color", DistantLight, light->shadowColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:distance", DistantLight, light->shadowDistance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloff", DistantLight, light->shadowFalloff)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloffGamma", DistantLight, light->shadowFalloffGamma)

    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, DistantLight,
                   light->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, DistantLight,
                       light->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, DistantLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<DomeLight>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    DomeLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;
  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "guideRadius", DomeLight, light->guideRadius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:diffuse", DomeLight, light->diffuse)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular", DomeLight, light->specular)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:colorTemperature", DomeLight, light->colorTemperature)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:color", DomeLight, light->color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:intensity", DomeLight, light->intensity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:texture:file", DomeLight, light->file)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:enable", DomeLight, light->shadowEnable)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:color", DomeLight, light->shadowColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:distance", DomeLight, light->shadowDistance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloff", DomeLight, light->shadowFalloff)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:shadow:falloffGamma", DomeLight, light->shadowFalloffGamma)

    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, DomeLight,
                   light->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, DomeLight,
                       light->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, DomeLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

} // namespace prim
} // namespace tinyusdz