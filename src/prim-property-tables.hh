// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
/// @file prim-property-tables.hh
/// @brief Property descriptor tables for prim reconstruction
///
/// This file defines property tables using X-macros to reduce code duplication
/// in prim-reconstruct.cc. Each prim type has a property table that lists
/// all its typed attributes, relationships, and enum properties.
///
/// Usage in prim-reconstruct.cc:
///   for (auto& prop : properties) {
///     GEOM_MESH_PROPERTIES(TYPED_ATTR, RELATION, ENUM_PROP)
///     // special handling...
///     ADD_PROPERTY(...)
///   }

#pragma once

//
// X-macro format:
//   TYPED_ATTR(prop_name, member_name)
//   TYPED_ATTR_NOCONT(prop_name, member_name)  // no continue
//   UNIFORM_ENUM(prop_name, enum_type, handler, member_name)
//   TIMESAMPLED_ENUM(prop_name, enum_type, handler, member_name)
//   SINGLE_REL(prop_name, member_name)
//   MULTI_REL(prop_name, member_name)
//   EXTENT(prop_name, member_name)
//

// ============================================================================
// GeomMesh Properties
// ============================================================================
#define GEOM_MESH_TYPED_ATTRS(X) \
  X("points", points) \
  X("normals", normals) \
  X("faceVertexCounts", faceVertexCounts) \
  X("faceVertexIndices", faceVertexIndices) \
  X("cornerIndices", cornerIndices) \
  X("cornerSharpnesses", cornerSharpnesses) \
  X("creaseIndices", creaseIndices) \
  X("creaseLengths", creaseLengths) \
  X("creaseSharpnesses", creaseSharpnesses) \
  X("holeIndices", holeIndices)

#define GEOM_MESH_SKEL_ATTRS(X) \
  X(kSkelBlendShapes, blendShapes)

#define GEOM_MESH_RELATIONS(SINGLE, MULTI) \
  SINGLE(kSkelSkeleton, skeleton) \
  MULTI(kSkelBlendShapeTargets, blendShapeTargets)

#define GEOM_MESH_UNIFORM_ENUMS(X) \
  X("subdivisionScheme", GeomMesh::SubdivisionScheme, SubdivisionSchemeHandler, subdivisionScheme)

#define GEOM_MESH_TIMESAMPLED_ENUMS(X) \
  X("interpolateBoundary", GeomMesh::InterpolateBoundary, InterpolateBoundaryHandler, interpolateBoundary) \
  X("facevaryingLinearInterpolation", GeomMesh::FaceVaryingLinearInterpolation, FaceVaryingLinearInterpolationHandler, faceVaryingLinearInterpolation)

// ============================================================================
// GeomCamera Properties
// ============================================================================
#define GEOM_CAMERA_TYPED_ATTRS(X) \
  X("focalLength", focalLength) \
  X("focusDistance", focusDistance) \
  X("exposure", exposure) \
  X("fStop", fStop) \
  X("horizontalAperture", horizontalAperture) \
  X("horizontalApertureOffset", horizontalApertureOffset) \
  X("verticalAperture", verticalAperture) \
  X("verticalApertureOffset", verticalApertureOffset) \
  X("clippingRange", clippingRange) \
  X("clippingPlanes", clippingPlanes) \
  X("shutter:open", shutterOpen) \
  X("shutter:close", shutterClose)

#define GEOM_CAMERA_TIMESAMPLED_ENUMS(X) \
  X("projection", GeomCamera::Projection, ProjectionHandler, projection)

#define GEOM_CAMERA_UNIFORM_ENUMS(X) \
  X("stereoRole", GeomCamera::StereoRole, StereoRoleHandler, stereoRole)

// ============================================================================
// GeomSubset Properties
// ============================================================================
#define GEOM_SUBSET_TYPED_ATTRS(X) \
  X("familyName", familyName) \
  X("indices", indices)

#define GEOM_SUBSET_UNIFORM_ENUMS(X) \
  X("elementType", GeomSubset::ElementType, ElementTypeHandler, elementType)

// ============================================================================
// GeomPointInstancer Properties
// ============================================================================
#define GEOM_POINT_INSTANCER_TYPED_ATTRS(X) \
  X("protoIndices", protoIndices) \
  X("ids", ids) \
  X("positions", positions) \
  X("orientations", orientations) \
  X("scales", scales) \
  X("velocities", velocities) \
  X("accelerations", accelerations) \
  X("angularVelocities", angularVelocities) \
  X("invisibleIds", invisibleIds) \
  X("inactiveIds", inactiveIds)

#define GEOM_POINT_INSTANCER_RELATIONS(SINGLE, MULTI) \
  MULTI("prototypes", prototypes)

// ============================================================================
// GeomBasisCurves Properties
// ============================================================================
#define GEOM_BASIS_CURVES_TYPED_ATTRS(X) \
  X("points", points) \
  X("normals", normals) \
  X("curveVertexCounts", curveVertexCounts) \
  X("widths", widths) \
  X("velocities", velocities) \
  X("accelerations", accelerations)

#define GEOM_BASIS_CURVES_UNIFORM_ENUMS(X) \
  X("type", GeomBasisCurves::Type, TypeHandler, type) \
  X("basis", GeomBasisCurves::Basis, BasisHandler, basis) \
  X("wrap", GeomBasisCurves::Wrap, WrapHandler, wrap)

// ============================================================================
// GeomPoints Properties
// ============================================================================
#define GEOM_POINTS_TYPED_ATTRS(X) \
  X("points", points) \
  X("normals", normals) \
  X("widths", widths) \
  X("ids", ids) \
  X("velocities", velocities) \
  X("accelerations", accelerations)

// ============================================================================
// GeomSphere Properties
// ============================================================================
#define GEOM_SPHERE_TYPED_ATTRS(X) \
  X("radius", radius)

// ============================================================================
// GeomCube Properties
// ============================================================================
#define GEOM_CUBE_TYPED_ATTRS(X) \
  X("size", size)

// ============================================================================
// GeomCone Properties
// ============================================================================
#define GEOM_CONE_TYPED_ATTRS(X) \
  X("radius", radius) \
  X("height", height)

#define GEOM_CONE_UNIFORM_ENUMS(X) \
  X("axis", Axis, AxisEnumHandler, axis)

// ============================================================================
// GeomCylinder Properties
// ============================================================================
#define GEOM_CYLINDER_TYPED_ATTRS(X) \
  X("radius", radius) \
  X("height", height)

#define GEOM_CYLINDER_UNIFORM_ENUMS(X) \
  X("axis", Axis, AxisEnumHandler, axis)

// ============================================================================
// GeomCapsule Properties
// ============================================================================
#define GEOM_CAPSULE_TYPED_ATTRS(X) \
  X("radius", radius) \
  X("height", height)

#define GEOM_CAPSULE_UNIFORM_ENUMS(X) \
  X("axis", Axis, AxisEnumHandler, axis)

// ============================================================================
// Skeleton Properties
// ============================================================================
#define SKELETON_TYPED_ATTRS(X) \
  X("joints", joints) \
  X("jointNames", jointNames) \
  X("bindTransforms", bindTransforms) \
  X("restTransforms", restTransforms)

// ============================================================================
// SkelAnimation Properties
// ============================================================================
#define SKEL_ANIMATION_TYPED_ATTRS(X) \
  X("joints", joints) \
  X("translations", translations) \
  X("rotations", rotations) \
  X("scales", scales) \
  X("blendShapes", blendShapes) \
  X("blendShapeWeights", blendShapeWeights)

// ============================================================================
// BlendShape Properties
// ============================================================================
#define BLEND_SHAPE_TYPED_ATTRS(X) \
  X("offsets", offsets) \
  X("normalOffsets", normalOffsets) \
  X("pointIndices", pointIndices)

// ============================================================================
// Light Common Properties
// ============================================================================

// Shadow attributes - shared by ALL light types
#define LIGHT_SHADOW_ATTRS(X) \
  X("inputs:shadow:enable", shadowEnable) \
  X("inputs:shadow:color", shadowColor) \
  X("inputs:shadow:distance", shadowDistance) \
  X("inputs:shadow:falloff", shadowFalloff) \
  X("inputs:shadow:falloffGamma", shadowFalloffGamma)

// Shaping attributes - shared by SphereLight, RectLight, DiskLight, CylinderLight
#define LIGHT_SHAPING_ATTRS(X) \
  X("inputs:shaping:focus", shapingFocus) \
  X("inputs:shaping:focusTint", shapingFocusTint) \
  X("inputs:shaping:cone:angle", shapingConeAngle) \
  X("inputs:shaping:cone:softness", shapingConeSoftness)

// ============================================================================
// SphereLight Properties
// ============================================================================
#define SPHERE_LIGHT_TYPED_ATTRS(X) \
  X("inputs:color", color) \
  X("inputs:radius", radius) \
  X("inputs:intensity", intensity)

// ============================================================================
// RectLight Properties
// ============================================================================
#define RECT_LIGHT_TYPED_ATTRS(X) \
  X("inputs:color", color) \
  X("inputs:height", height) \
  X("inputs:width", width) \
  X("inputs:intensity", intensity)

// RectLight has a special texture:file attr that uses UsdUVTexture type
// This needs special handling, not included in typed attrs

// ============================================================================
// DiskLight Properties
// ============================================================================
#define DISK_LIGHT_TYPED_ATTRS(X) \
  X("inputs:color", color) \
  X("inputs:intensity", intensity) \
  X("inputs:exposure", exposure) \
  X("inputs:normalize", normalize) \
  X("inputs:enableColorTemperature", enableColorTemperature) \
  X("inputs:colorTemperature", colorTemperature) \
  X("inputs:radius", radius)

// ============================================================================
// CylinderLight Properties
// ============================================================================
#define CYLINDER_LIGHT_TYPED_ATTRS(X) \
  X("inputs:length", length) \
  X("inputs:radius", radius)

// ============================================================================
// DistantLight Properties
// ============================================================================
#define DISTANT_LIGHT_TYPED_ATTRS(X) \
  X("inputs:color", color) \
  X("inputs:intensity", intensity) \
  X("inputs:exposure", exposure) \
  X("inputs:normalize", normalize) \
  X("inputs:enableColorTemperature", enableColorTemperature) \
  X("inputs:colorTemperature", colorTemperature) \
  X("inputs:angle", angle)

// ============================================================================
// GeometryLight Properties
// ============================================================================
#define GEOMETRY_LIGHT_TYPED_ATTRS(X) \
  X("inputs:color", color) \
  X("inputs:intensity", intensity) \
  X("inputs:exposure", exposure) \
  X("inputs:diffuse", diffuse) \
  X("inputs:specular", specular) \
  X("inputs:normalize", normalize) \
  X("inputs:enableColorTemperature", enableColorTemperature) \
  X("inputs:colorTemperature", colorTemperature)

// ============================================================================
// DomeLight Properties
// ============================================================================
#define DOME_LIGHT_TYPED_ATTRS(X) \
  X("guideRadius", guideRadius) \
  X("inputs:diffuse", diffuse) \
  X("inputs:specular", specular) \
  X("inputs:colorTemperature", colorTemperature) \
  X("inputs:color", color) \
  X("inputs:intensity", intensity) \
  X("inputs:texture:file", file)

// ============================================================================
// Helper macros for property table expansion
// ============================================================================
// These macros require PRIM_CLASS_ and PRIM_PTR_ to be defined before use

// Expand typed attributes with PARSE_TYPED_ATTRIBUTE macro
#define EXPAND_TYPED_ATTR(name, member) \
  PARSE_TYPED_ATTRIBUTE(table, prop, name, PRIM_CLASS_, PRIM_PTR_->member)

// Expand typed attributes without continue
#define EXPAND_TYPED_ATTR_NOCONT(name, member) \
  PARSE_TYPED_ATTRIBUTE_NOCONTINUE(table, prop, name, PRIM_CLASS_, PRIM_PTR_->member)

// Expand single target relations
#define EXPAND_SINGLE_REL(name, member) \
  PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, name, PRIM_PTR_->member)

// Expand multi target relations
#define EXPAND_MULTI_REL(name, member) \
  PARSE_TARGET_PATHS_RELATION(table, prop, name, PRIM_PTR_->member)

// Expand uniform enum properties (handler must be a local variable)
#define EXPAND_UNIFORM_ENUM(name, enum_type, handler, member) \
  PARSE_UNIFORM_ENUM_PROPERTY(table, prop, name, enum_type, handler, PRIM_CLASS_, \
                              PRIM_PTR_->member, options.strict_allowedToken_check)

// Expand timesampled enum properties (handler must be a local variable)
#define EXPAND_TIMESAMPLED_ENUM(name, enum_type, handler, member) \
  PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, name, enum_type, handler, PRIM_CLASS_, \
                                  PRIM_PTR_->member, options.strict_allowedToken_check)

// Expand extent attribute
#define EXPAND_EXTENT(name, member) \
  PARSE_EXTENT_ATTRIBUTE(table, prop, name, PRIM_CLASS_, PRIM_PTR_->member)

