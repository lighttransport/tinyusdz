// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Geometry primitive reconstruction - Implementation

#include "reconstruct-geom.hh"
#include "reconstruct-common.hh"
#include "prim-reconstruct.hh"
#include "str-util.hh"
#include "common-macros.inc"
#include "usdGeom.hh"

namespace tinyusdz {
namespace prim {

// GPrim properties helper function
bool ReconstructGPrimProperties(
  const Specifier &spec,
  std::set<std::string> &table,
  const PropertyMap &properties,
  GPrim *gprim,
  std::string *warn,
  std::string *err,
  bool strict_allowedToken_check)
{
  (void)warn;
  
  // Note: xformOps will be handled by transform reconstruction
  // Material binding will be handled by shader reconstruction
  // Collection properties will be handled by common utilities
  
  for (const auto &prop : properties) {
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kProxyPrim, gprim->proxyPrim)
    PARSE_TYPED_ATTRIBUTE(table, prop, "doubleSided", GPrim, gprim->doubleSided)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, GPrim,
                   gprim->visibility, strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "purpose", Purpose, PurposeEnumHandler, GPrim,
                       gprim->purpose, strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "orientation", Orientation, OrientationEnumHandler, GPrim,
                       gprim->orientation, strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, "extent", GPrim, gprim->extent)
  }

  return true;
}

// Template specializations for geometry primitives

template <>
bool ReconstructPrim<GeomSphere>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomSphere *sphere,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct Sphere.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, sphere, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "radius", GeomSphere, sphere->radius)
    ADD_PROPERTY(table, prop, GeomSphere, sphere->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomPoints>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomPoints *points,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct Points.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, points, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "points", GeomPoints, points->points)
    PARSE_TYPED_ATTRIBUTE(table, prop, "normals", GeomPoints, points->normals)
    PARSE_TYPED_ATTRIBUTE(table, prop, "widths", GeomPoints, points->widths)
    PARSE_TYPED_ATTRIBUTE(table, prop, "ids", GeomPoints, points->ids)
    PARSE_TYPED_ATTRIBUTE(table, prop, "velocities", GeomPoints, points->velocities)
    PARSE_TYPED_ATTRIBUTE(table, prop, "accelerations", GeomPoints, points->accelerations)
    ADD_PROPERTY(table, prop, GeomPoints, points->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomCone>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCone *cone,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct Cone.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, cone, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "height", GeomCone, cone->height)
    PARSE_TYPED_ATTRIBUTE(table, prop, "radius", GeomCone, cone->radius)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "axis", Axis, AxisEnumHandler, GeomCone,
                       cone->axis, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, GeomCone, cone->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomCylinder>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCylinder *cylinder,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct Cylinder.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, cylinder, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "height", GeomCylinder, cylinder->height)
    PARSE_TYPED_ATTRIBUTE(table, prop, "radius", GeomCylinder, cylinder->radius)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "axis", Axis, AxisEnumHandler, GeomCylinder,
                       cylinder->axis, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, GeomCylinder, cylinder->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomCapsule>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCapsule *capsule,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct Capsule.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, capsule, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "height", GeomCapsule, capsule->height)
    PARSE_TYPED_ATTRIBUTE(table, prop, "radius", GeomCapsule, capsule->radius)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "axis", Axis, AxisEnumHandler, GeomCapsule,
                       capsule->axis, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, GeomCapsule, capsule->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomCube>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCube *cube,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct Cube.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, cube, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "size", GeomCube, cube->size)
    ADD_PROPERTY(table, prop, GeomCube, cube->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomMesh>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomMesh *mesh,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;

  DCOUT("GeomMesh");

  auto SubdivisionSchemeHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::SubdivisionScheme, std::string> {
    using EnumTy = std::pair<GeomMesh::SubdivisionScheme, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::SubdivisionScheme::SubdivisionSchemeNone, "none"),
        std::make_pair(GeomMesh::SubdivisionScheme::CatmullClark,
                       "catmullClark"),
        std::make_pair(GeomMesh::SubdivisionScheme::Loop, "loop"),
        std::make_pair(GeomMesh::SubdivisionScheme::Bilinear, "bilinear"),
    };
    return EnumHandler<GeomMesh::SubdivisionScheme>("subdivisionScheme", tok,
                                                    enums);
  };

  auto InterpolateBoundaryHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::InterpolateBoundary, std::string> {
    using EnumTy = std::pair<GeomMesh::InterpolateBoundary, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::InterpolateBoundary::InterpolateBoundaryNone, "none"),
        std::make_pair(GeomMesh::InterpolateBoundary::EdgeAndCorner,
                       "edgeAndCorner"),
        std::make_pair(GeomMesh::InterpolateBoundary::EdgeOnly, "edgeOnly"),
    };
    return EnumHandler<GeomMesh::InterpolateBoundary>("interpolateBoundary",
                                                      tok, enums);
  };

  auto FaceVaryingLinearInterpolationHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::FaceVaryingLinearInterpolation,
                          std::string> {
    using EnumTy =
        std::pair<GeomMesh::FaceVaryingLinearInterpolation, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::FaceVaryingLinearInterpolation::CornersPlus1,
                       "cornersPlus1"),
        std::make_pair(GeomMesh::FaceVaryingLinearInterpolation::CornersPlus2,
                       "cornersPlus2"),
        std::make_pair(GeomMesh::FaceVaryingLinearInterpolation::CornersOnly,
                       "cornersOnly"),
        std::make_pair(GeomMesh::FaceVaryingLinearInterpolation::Boundaries,
                       "boundaries"),
        std::make_pair(GeomMesh::FaceVaryingLinearInterpolation::FaceVaryingLinearInterpolationNone, "none"),
        std::make_pair(GeomMesh::FaceVaryingLinearInterpolation::All, "all"),
    };
    return EnumHandler<GeomMesh::FaceVaryingLinearInterpolation>(
        "facevaryingLinearInterpolation", tok, enums);
  };

  auto FamilyTypeHandler = [](const std::string &tok)
      -> nonstd::expected<GeomSubset::FamilyType, std::string> {
    using EnumTy = std::pair<GeomSubset::FamilyType, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomSubset::FamilyType::Partition, "partition"),
        std::make_pair(GeomSubset::FamilyType::NonOverlapping, "nonOverlapping"),
        std::make_pair(GeomSubset::FamilyType::Unrestricted, "unrestricted"),
    };
    return EnumHandler<GeomSubset::FamilyType>("familyType", tok,
                                                    enums);
  };

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, mesh, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    DCOUT("GeomMesh prop: " << prop.first);
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kSkelSkeleton, mesh->skeleton)
    PARSE_TARGET_PATHS_RELATION(table, prop, kSkelBlendShapeTargets, mesh->blendShapeTargets)
    PARSE_TYPED_ATTRIBUTE(table, prop, "points", GeomMesh, mesh->points)
    PARSE_TYPED_ATTRIBUTE(table, prop, "normals", GeomMesh, mesh->normals)
    PARSE_TYPED_ATTRIBUTE(table, prop, "faceVertexCounts", GeomMesh,
                         mesh->faceVertexCounts)
    PARSE_TYPED_ATTRIBUTE(table, prop, "faceVertexIndices", GeomMesh,
                         mesh->faceVertexIndices)
    // Subd
    PARSE_TYPED_ATTRIBUTE(table, prop, "cornerIndices", GeomMesh,
                         mesh->cornerIndices)
    PARSE_TYPED_ATTRIBUTE(table, prop, "cornerSharpnesses", GeomMesh,
                         mesh->cornerSharpnesses)
    PARSE_TYPED_ATTRIBUTE(table, prop, "creaseIndices", GeomMesh,
                         mesh->creaseIndices)
    PARSE_TYPED_ATTRIBUTE(table, prop, "creaseLengths", GeomMesh,
                         mesh->creaseLengths)
    PARSE_TYPED_ATTRIBUTE(table, prop, "creaseSharpnesses", GeomMesh,
                         mesh->creaseSharpnesses)
    PARSE_TYPED_ATTRIBUTE(table, prop, "holeIndices", GeomMesh,
                         mesh->holeIndices)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "subdivisionScheme", GeomMesh::SubdivisionScheme,
                       SubdivisionSchemeHandler, GeomMesh,
                       mesh->subdivisionScheme, options.strict_allowedToken_check)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "interpolateBoundary",
                       GeomMesh::InterpolateBoundary, InterpolateBoundaryHandler, GeomMesh,
                       mesh->interpolateBoundary, options.strict_allowedToken_check)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "facevaryingLinearInterpolation",
                       GeomMesh::FaceVaryingLinearInterpolation, FaceVaryingLinearInterpolationHandler, GeomMesh,
                       mesh->faceVaryingLinearInterpolation, options.strict_allowedToken_check)
    // blendShape names
    PARSE_TYPED_ATTRIBUTE(table, prop, kSkelBlendShapes, GeomMesh, mesh->blendShapes)

    // subsetFamily for GeomSubset
    if (startsWith(prop.first, "subsetFamily")) {
      // uniform subsetFamily::<FAMILYNAME>:familyType = ...
      std::vector<std::string> names = split(prop.first, ":");

      if ((names.size() == 3) &&
          (names[0] == "subsetFamily") &&
          (names[2] == "familyType")) {

        DCOUT("subsetFamily" << prop.first);
        TypedAttributeWithFallback<GeomSubset::FamilyType> familyType{GeomSubset::FamilyType::Unrestricted};

        PARSE_UNIFORM_ENUM_PROPERTY(table, prop, prop.first,
                           GeomSubset::FamilyType, FamilyTypeHandler, GeomMesh,
                           familyType, options.strict_allowedToken_check)

        // NOTE: Ignore metadataum of familyType.
        
        // TODO: Validate familyName
        mesh->subsetFamilyTypeMap[value::token(names[1])] = familyType.get_value();

      }
    }

    // generic
    ADD_PROPERTY(table, prop, GeomMesh, mesh->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomCamera>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCamera *camera,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)warn;
  (void)options;

  auto ProjectionHandler = [](const std::string &tok)
      -> nonstd::expected<GeomCamera::Projection, std::string> {
    using EnumTy = std::pair<GeomCamera::Projection, const char *>;
    constexpr std::array<EnumTy, 2> enums = {
        std::make_pair(GeomCamera::Projection::Perspective, "perspective"),
        std::make_pair(GeomCamera::Projection::Orthographic, "orthographic"),
    };

    auto ret =
        CheckAllowedTokens<GeomCamera::Projection, enums.size()>(enums, tok);
    if (!ret) {
      return nonstd::make_unexpected(ret.error());
    }

    for (auto &item : enums) {
      if (tok == item.second) {
        return item.first;
      }
    }

    // Should never reach here, though.
    return nonstd::make_unexpected(
        quote(tok) + " is invalid token for `projection` property");
  };

  auto StereoRoleHandler = [](const std::string &tok)
      -> nonstd::expected<GeomCamera::StereoRole, std::string> {
    using EnumTy = std::pair<GeomCamera::StereoRole, const char *>;
    constexpr std::array<EnumTy, 3> enums = {
        std::make_pair(GeomCamera::StereoRole::Mono, "mono"),
        std::make_pair(GeomCamera::StereoRole::Left, "left"),
        std::make_pair(GeomCamera::StereoRole::Right, "right"),
    };

    auto ret =
        CheckAllowedTokens<GeomCamera::StereoRole, enums.size()>(enums, tok);
    if (!ret) {
      return nonstd::make_unexpected(ret.error());
    }

    for (auto &item : enums) {
      if (tok == item.second) {
        return item.first;
      }
    }

    // Should never reach here, though.
    return nonstd::make_unexpected(
        quote(tok) + " is invalid token for `stereoRole` property");
  };

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, camera, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "focalLength", GeomCamera, camera->focalLength)
    PARSE_TYPED_ATTRIBUTE(table, prop, "focusDistance", GeomCamera,
                   camera->focusDistance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "exposure", GeomCamera, camera->exposure)
    PARSE_TYPED_ATTRIBUTE(table, prop, "fStop", GeomCamera, camera->fStop)
    PARSE_TYPED_ATTRIBUTE(table, prop, "horizontalAperture", GeomCamera,
                   camera->horizontalAperture)
    PARSE_TYPED_ATTRIBUTE(table, prop, "horizontalApertureOffset", GeomCamera,
                   camera->horizontalApertureOffset)
    PARSE_TYPED_ATTRIBUTE(table, prop, "verticalAperture", GeomCamera,
                   camera->verticalAperture)
    PARSE_TYPED_ATTRIBUTE(table, prop, "verticalApertureOffset", GeomCamera,
                   camera->verticalApertureOffset)
    PARSE_TYPED_ATTRIBUTE(table, prop, "clippingRange", GeomCamera,
                   camera->clippingRange)
    PARSE_TYPED_ATTRIBUTE(table, prop, "clippingPlanes", GeomCamera,
                   camera->clippingPlanes)
    PARSE_TYPED_ATTRIBUTE(table, prop, "shutter:open", GeomCamera, camera->shutterOpen)
    PARSE_TYPED_ATTRIBUTE(table, prop, "shutter:close", GeomCamera,
                   camera->shutterClose)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "projection", GeomCamera::Projection, ProjectionHandler, GeomCamera,
                       camera->projection, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "stereoRole", GeomCamera::StereoRole, StereoRoleHandler, GeomCamera,
                       camera->stereoRole, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, GeomCamera, camera->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomSubset>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomSubset *subset,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)references;

  DCOUT("GeomSubset");

  // Currently schema only allows 'face'
  auto ElementTypeHandler = [](const std::string &tok)
      -> nonstd::expected<GeomSubset::ElementType, std::string> {
    using EnumTy = std::pair<GeomSubset::ElementType, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomSubset::ElementType::Face, "face"),
        std::make_pair(GeomSubset::ElementType::Point, "point"),
    };
    return EnumHandler<GeomSubset::ElementType>("elementType", tok,
                                                    enums);
  };

  std::set<std::string> table;

  // Note: The original implementation called helper functions here
  // But for now we'll implement the simple version without material binding
  // since GeomSubset doesn't directly inherit from MaterialBinding

  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "familyName", GeomSubset, subset->familyName)
    PARSE_TYPED_ATTRIBUTE(table, prop, "indices", GeomSubset, subset->indices)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "elementType", GeomSubset::ElementType, ElementTypeHandler, GeomSubset, subset->elementType, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, GeomSubset, subset->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<GeomPointInstancer>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomPointInstancer *instancer,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct GeomPointInstancer.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, instancer, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (const auto &prop : properties) {
    PARSE_TARGET_PATHS_RELATION(table, prop, "prototypes", instancer->prototypes)
    PARSE_TYPED_ATTRIBUTE(table, prop, "protoIndices", GeomPointInstancer, instancer->protoIndices)
    PARSE_TYPED_ATTRIBUTE(table, prop, "ids", GeomPointInstancer, instancer->ids)
    PARSE_TYPED_ATTRIBUTE(table, prop, "positions", GeomPointInstancer, instancer->positions)
    PARSE_TYPED_ATTRIBUTE(table, prop, "orientations", GeomPointInstancer, instancer->orientations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "scales", GeomPointInstancer, instancer->scales)
    PARSE_TYPED_ATTRIBUTE(table, prop, "velocities", GeomPointInstancer, instancer->velocities)
    PARSE_TYPED_ATTRIBUTE(table, prop, "accelerations", GeomPointInstancer, instancer->accelerations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "angularVelocities", GeomPointInstancer, instancer->angularVelocities)
    PARSE_TYPED_ATTRIBUTE(table, prop, "invisibleIds", GeomPointInstancer, instancer->invisibleIds)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inactiveIds", GeomPointInstancer, instancer->inactiveIds)

    ADD_PROPERTY(table, prop, GeomPointInstancer, instancer->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

} // namespace prim
} // namespace tinyusdz