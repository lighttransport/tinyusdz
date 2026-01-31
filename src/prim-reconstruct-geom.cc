// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Reconstruct Geometry Prims - split from prim-reconstruct.cc for parallel compilation
//
#include "prim-reconstruct-internal.hh"
#include "usdGeom.hh"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) if (err) { (*err) = s + (*err); }
#define PushWarn(s) if (warn) { (*warn) = s + (*err); }

#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))
#define PUSH_ERROR_AND_RETURN_F(s, ...) PUSH_ERROR_AND_RETURN(fmt::format(s, __VA_ARGS__))

namespace tinyusdz {
namespace prim {

// Include implementation helpers (anonymous namespace with ParseTypedAttribute, etc.)
#include "prim-reconstruct-impl.inc"

// GeomCapsule, GeomCube: ReconstructGPrimProperties + property loop
//
// IMPORTANT: Caller must define PRIM_CLASS_ and PRIM_PTR_ macros before calling
//            this macro, and undef them afterward. These are required by
//            EXPAND_TYPED_ATTR and EXPAND_UNIFORM_ENUM macros.
//
// Parameters:
//   PrimClass: The geometry class (e.g., GeomSphere, GeomCone)
//   prim_ptr: Pointer to the prim instance
//   TYPED_ATTRS: Property table macro (e.g., GEOM_SPHERE_TYPED_ATTRS)
//   ENUM_EXPANSION: Enum handling macro call or empty
//                   - For shapes without enums: /* empty */
//                   - For shapes with enums: GEOM_XXX_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
#define RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(PrimClass, prim_ptr, TYPED_ATTRS, ENUM_EXPANSION) \
  (void)references; \
  \
  std::set<std::string> table; \
  if (!ReconstructGPrimProperties(spec, table, properties, prim_ptr, warn, err, \
                                   options.strict_allowedToken_check)) { \
    return false; \
  } \
  \
  for (auto &prop : properties) { \
    TYPED_ATTRS(EXPAND_TYPED_ATTR) \
    ENUM_EXPANSION \
    ADD_PROPERTY(table, prop, PrimClass, prim_ptr->props) \
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop) \
  } \
  \
  return true;

template <>
bool ReconstructPrim<GeomSphere>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomSphere *sphere,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  DCOUT("Reconstruct Sphere.");
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomSphere
#define PRIM_PTR_ sphere
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomSphere, sphere, GEOM_SPHERE_TYPED_ATTRS, /* no enums */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomPoints>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomPoints *points,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;

  DCOUT("Reconstruct Points.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, points, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomPoints
#define PRIM_PTR_ points
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    DCOUT("prop: " << prop.first);
    GEOM_POINTS_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, GeomPoints, points->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomCone>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCone *cone,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCone
#define PRIM_PTR_ cone
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCone, cone, GEOM_CONE_TYPED_ATTRS,
                                     GEOM_CONE_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCylinder>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCylinder *cylinder,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCylinder
#define PRIM_PTR_ cylinder
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCylinder, cylinder, GEOM_CYLINDER_TYPED_ATTRS,
                                     GEOM_CYLINDER_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCapsule>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCapsule *capsule,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCapsule
#define PRIM_PTR_ capsule
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCapsule, capsule, GEOM_CAPSULE_TYPED_ATTRS,
                                     GEOM_CAPSULE_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCube>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCube *cube,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  // pxrUSD says... "If you author size you must also author extent."
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCube
#define PRIM_PTR_ cube
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCube, cube, GEOM_CUBE_TYPED_ATTRS, /* no enums */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomMesh>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomMesh *mesh,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;

  DCOUT("GeomMesh");

  // Use centralized enum handlers (aliased for macro expansion)
  auto SubdivisionSchemeHandler = enum_handler::SubdivisionScheme;
  auto InterpolateBoundaryHandler = enum_handler::InterpolateBoundary;
  auto FaceVaryingLinearInterpolationHandler = enum_handler::FaceVaryingLinearInterpolation;
  auto FamilyTypeHandler = enum_handler::FamilyType;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, mesh, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  // Define context for property table expansion macros
  // (suppress unused-macros warning since these are used inside X-macro expansion)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomMesh
#define PRIM_PTR_ mesh
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    DCOUT("GeomMesh prop: " << prop.first);

    // Relations (using property table)
    GEOM_MESH_RELATIONS(EXPAND_SINGLE_REL, EXPAND_MULTI_REL)

    // Typed attributes (using property table)
    GEOM_MESH_TYPED_ATTRS(EXPAND_TYPED_ATTR)

    // Skel-related typed attributes
    GEOM_MESH_SKEL_ATTRS(EXPAND_TYPED_ATTR)

    // Enum properties (using property table)
    GEOM_MESH_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    GEOM_MESH_TIMESAMPLED_ENUMS(EXPAND_TIMESAMPLED_ENUM)

    // Special handling: subsetFamily for GeomSubset (cannot be table-driven)
    if (startsWith(prop.first, "subsetFamily")) {
      // uniform subsetFamily::<FAMILYNAME>:familyType = ...
      std::vector<std::string> names = split(prop.first, ":");

      if ((names.size() == 3) &&
          (names[0] == "subsetFamily") &&
          (names[2] == "familyType")) {

        if (table.count(prop.first)) {
          // Already processed
        } else if ((prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) &&
                   prop.second.is_attribute() &&
                   !prop.second.is_empty()) {
          // Parse the token enum value
          const Attribute &attr = prop.second.get_attribute();
          TypedAttributeWithFallback<GeomSubset::FamilyType> familyType{GeomSubset::FamilyType::Unrestricted};
          std::function<nonstd::expected<GeomSubset::FamilyType, std::string>(const std::string &)> fun = FamilyTypeHandler;

          if (!ParseUniformEnumProperty(prop.first, options.strict_allowedToken_check, fun, attr, &familyType, warn, err)) {
            return false;
          }

          // Validate familyName (names[1]) is a valid identifier
          const std::string &familyName = names[1];
          if (familyName.empty()) {
            PUSH_WARN("subsetFamily: familyName is empty in property `" << prop.first << "`");
          } else if (!std::isalpha(static_cast<unsigned char>(familyName[0])) && familyName[0] != '_') {
            PUSH_WARN("subsetFamily: familyName `" << familyName << "` should start with a letter or underscore.");
          }

          // NOTE: Ignore metadata of familyType.
          mesh->subsetFamilyTypeMap[value::token(familyName)] = familyType.get_value();
          table.insert(prop.first);
        }
      }
    }

    // generic property handling
    ADD_PROPERTY(table, prop, GeomMesh, mesh->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}


template <>
bool ReconstructPrim<GeomCamera>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCamera *camera,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;
  (void)warn;

  // Use centralized enum handlers
  auto ProjectionHandler = enum_handler::CameraProjection;
  auto StereoRoleHandler = enum_handler::CameraStereoRole;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, camera, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCamera
#define PRIM_PTR_ camera
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_CAMERA_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_CAMERA_TIMESAMPLED_ENUMS(EXPAND_TIMESAMPLED_ENUM)
    GEOM_CAMERA_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomCamera, camera->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomSubset>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomSubset *subset,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)references;

  DCOUT("GeomSubset");

  // Use centralized enum handler
  auto ElementTypeHandler = enum_handler::ElementType;

  std::set<std::string> table;

  if (!prim::ReconstructMaterialBindingProperties(table, properties, subset, err)) {
    return false;
  }

  if (!prim::ReconstructCollectionProperties(
    table, properties, subset, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomSubset
#define PRIM_PTR_ subset
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_SUBSET_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_SUBSET_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomSubset, subset->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomPointInstancer>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomPointInstancer *instancer,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;

  DCOUT("Reconstruct GeomPointInstancer.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, instancer, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomPointInstancer
#define PRIM_PTR_ instancer
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_POINT_INSTANCER_RELATIONS(EXPAND_SINGLE_REL, EXPAND_MULTI_REL)
    GEOM_POINT_INSTANCER_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, GeomPointInstancer, instancer->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

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

RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomMesh)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomPoints)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCylinder)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCube)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCone)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomSphere)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCapsule)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCamera)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomSubset)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomPointInstancer)

}  // namespace prim
}  // namespace tinyusdz
