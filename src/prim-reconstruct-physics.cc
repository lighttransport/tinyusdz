// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// Physics + MuJoCo prim reconstruction specializations.
// Split from prim-reconstruct.cc
//
#include "prim-reconstruct.hh"

#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "tiny-format.hh"
#include "enum-handlers.hh"
#include "prim-property-tables.hh"

#include "usdPhysics.hh"
#include "mjcPhysics.hh"
#include "newtonPhysics.hh"

#include "common-macros.inc"
#include "value-types.hh"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) \
  if (err) { \
    (*err) = (s) + (err->empty() ? std::string() : std::string("\n")) + (*err); \
  }
#define PushWarn(s) \
  if (warn) { \
    (*warn) = (s) + (warn->empty() ? std::string() : std::string("\n")) + (*warn); \
  }

#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))

namespace tinyusdz {
namespace prim {

// Suppress unused-function warnings from enum handlers in the .inc that
// physics reconstruction does not use (e.g. PurposeEnumHandler).
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "prim-reconstruct-common.inc"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// Helper: check if any property starts with a given prefix
static bool HasPropertyPrefix(const PropertyMap &properties, const std::string &prefix) {
  for (const auto &prop : properties) {
    if (prop.first.compare(0, prefix.size(), prefix) == 0) {
      return true;
    }
  }
  return false;
}

// Helper: reconstruct PhysicsJointBase properties (shared by all joint types)
template <typename JointT>
static bool ReconstructJointBaseProperties(
    std::set<std::string> &table,
    PropertyMap &properties,
    JointT *joint,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)err;
  (void)options;

  // Check for extension properties to auto-create typed API structs. Other
  // vendor namespaces (physxJoint:* / physxLimit:* / state:*) intentionally
  // remain as untyped properties and round-trip through the generic props map.
  if (!joint->mjcJoint.has_value() && HasPropertyPrefix(properties, "mjc:")) {
    joint->mjcJoint = MjcJointAPI();
  }
  if (!joint->newtonMimic.has_value() && HasPropertyPrefix(properties, "newton:")) {
    joint->newtonMimic = NewtonMimicAPI();
  }

  for (auto &prop : properties) {
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, "physics:body0", joint->body0)
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, "physics:body1", joint->body1)
  }

  return true;
}

// ============================================================================
// PhysicsJoint (generic D6 joint)
// ============================================================================
template <>
bool ReconstructPrim<PhysicsJoint>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    PhysicsJoint *joint, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references;
  std::set<std::string> table;
  if (!ReconstructJointBaseProperties(table, properties, joint, warn, err, options)) return false;
#define PRIM_PTR_ joint
  for (auto &prop : properties) {
    PHYSICS_JOINT_BASE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_JOINT_BASE_RELS(EXPAND_SINGLE_REL)
    if (joint->mjcJoint.has_value()) { MJC_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR) }
    if (joint->newtonMimic.has_value()) {
      NEWTON_MIMIC_TYPED_ATTRS(EXPAND_TYPED_ATTR)
      NEWTON_MIMIC_RELS(EXPAND_SINGLE_REL)
    }
    ADD_PROPERTY(table, prop, PhysicsJoint, joint->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// PhysicsScene
// ============================================================================
template <>
bool ReconstructPrim<PhysicsScene>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    PhysicsScene *scene,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)references;
  (void)options;

  std::set<std::string> table;

  if (!scene->mjcScene.has_value() && HasPropertyPrefix(properties, "mjc:")) {
    scene->mjcScene = MjcSceneAPI();
  }
  if (!scene->newtonScene.has_value() && (
      HasPropertyPrefix(properties, "newton:maxSolverIterations") ||
      HasPropertyPrefix(properties, "newton:timeStepsPerSecond") ||
      HasPropertyPrefix(properties, "newton:gravityEnabled"))) {
    scene->newtonScene = NewtonSceneAPI();
  }
  if (!scene->newtonXpbdScene.has_value() &&
      HasPropertyPrefix(properties, "newton:xpbd:")) {
    scene->newtonXpbdScene = NewtonXpbdSceneAPI();
  }
  if (!scene->newtonKaminoScene.has_value() &&
      HasPropertyPrefix(properties, "newton:kamino:")) {
    scene->newtonKaminoScene = NewtonKaminoSceneAPI();
  }

#define PRIM_PTR_ scene

  for (auto &prop : properties) {
    PHYSICS_SCENE_TYPED_ATTRS(EXPAND_TYPED_ATTR)

    if (scene->mjcScene.has_value()) {
      MJC_SCENE_OPTION_TYPED_ATTRS(EXPAND_TYPED_ATTR)
      MJC_SCENE_FLAG_TYPED_ATTRS(EXPAND_TYPED_ATTR)
      MJC_SCENE_COMPILER_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    }
    if (scene->newtonScene.has_value()) {
      NEWTON_SCENE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    }
    if (scene->newtonXpbdScene.has_value()) {
      NEWTON_XPBD_SCENE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    }
    if (scene->newtonKaminoScene.has_value()) {
      NEWTON_KAMINO_SCENE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    }

    ADD_PROPERTY(table, prop, PhysicsScene, scene->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_PTR_

  return true;
}

// ============================================================================
// PhysicsRevoluteJoint
// ============================================================================
template <>
bool ReconstructPrim<PhysicsRevoluteJoint>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    PhysicsRevoluteJoint *joint, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references;
  std::set<std::string> table;
  if (!ReconstructJointBaseProperties(table, properties, joint, warn, err, options)) return false;
#define PRIM_PTR_ joint
  for (auto &prop : properties) {
    PHYSICS_JOINT_BASE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_REVOLUTE_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_JOINT_BASE_RELS(EXPAND_SINGLE_REL)
    if (joint->mjcJoint.has_value()) { MJC_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR) }
    if (joint->newtonMimic.has_value()) {
      NEWTON_MIMIC_TYPED_ATTRS(EXPAND_TYPED_ATTR)
      NEWTON_MIMIC_RELS(EXPAND_SINGLE_REL)
    }
    ADD_PROPERTY(table, prop, PhysicsRevoluteJoint, joint->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// PhysicsPrismaticJoint
// ============================================================================
template <>
bool ReconstructPrim<PhysicsPrismaticJoint>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    PhysicsPrismaticJoint *joint, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references;
  std::set<std::string> table;
  if (!ReconstructJointBaseProperties(table, properties, joint, warn, err, options)) return false;
#define PRIM_PTR_ joint
  for (auto &prop : properties) {
    PHYSICS_JOINT_BASE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_PRISMATIC_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_JOINT_BASE_RELS(EXPAND_SINGLE_REL)
    if (joint->mjcJoint.has_value()) { MJC_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR) }
    if (joint->newtonMimic.has_value()) {
      NEWTON_MIMIC_TYPED_ATTRS(EXPAND_TYPED_ATTR)
      NEWTON_MIMIC_RELS(EXPAND_SINGLE_REL)
    }
    ADD_PROPERTY(table, prop, PhysicsPrismaticJoint, joint->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// PhysicsSphericalJoint
// ============================================================================
template <>
bool ReconstructPrim<PhysicsSphericalJoint>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    PhysicsSphericalJoint *joint, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references;
  std::set<std::string> table;
  if (!ReconstructJointBaseProperties(table, properties, joint, warn, err, options)) return false;
#define PRIM_PTR_ joint
  for (auto &prop : properties) {
    PHYSICS_JOINT_BASE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_SPHERICAL_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_JOINT_BASE_RELS(EXPAND_SINGLE_REL)
    if (joint->mjcJoint.has_value()) { MJC_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR) }
    if (joint->newtonMimic.has_value()) {
      NEWTON_MIMIC_TYPED_ATTRS(EXPAND_TYPED_ATTR)
      NEWTON_MIMIC_RELS(EXPAND_SINGLE_REL)
    }
    ADD_PROPERTY(table, prop, PhysicsSphericalJoint, joint->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// PhysicsFixedJoint
// ============================================================================
template <>
bool ReconstructPrim<PhysicsFixedJoint>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    PhysicsFixedJoint *joint, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references;
  std::set<std::string> table;
  if (!ReconstructJointBaseProperties(table, properties, joint, warn, err, options)) return false;
#define PRIM_PTR_ joint
  for (auto &prop : properties) {
    PHYSICS_JOINT_BASE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_JOINT_BASE_RELS(EXPAND_SINGLE_REL)
    if (joint->mjcJoint.has_value()) { MJC_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR) }
    if (joint->newtonMimic.has_value()) {
      NEWTON_MIMIC_TYPED_ATTRS(EXPAND_TYPED_ATTR)
      NEWTON_MIMIC_RELS(EXPAND_SINGLE_REL)
    }
    ADD_PROPERTY(table, prop, PhysicsFixedJoint, joint->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// PhysicsDistanceJoint
// ============================================================================
template <>
bool ReconstructPrim<PhysicsDistanceJoint>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    PhysicsDistanceJoint *joint, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references;
  std::set<std::string> table;
  if (!ReconstructJointBaseProperties(table, properties, joint, warn, err, options)) return false;
#define PRIM_PTR_ joint
  for (auto &prop : properties) {
    PHYSICS_JOINT_BASE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_DISTANCE_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_JOINT_BASE_RELS(EXPAND_SINGLE_REL)
    if (joint->mjcJoint.has_value()) { MJC_JOINT_TYPED_ATTRS(EXPAND_TYPED_ATTR) }
    if (joint->newtonMimic.has_value()) {
      NEWTON_MIMIC_TYPED_ATTRS(EXPAND_TYPED_ATTR)
      NEWTON_MIMIC_RELS(EXPAND_SINGLE_REL)
    }
    ADD_PROPERTY(table, prop, PhysicsDistanceJoint, joint->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// MjcActuator
// ============================================================================
template <>
bool ReconstructPrim<MjcActuator>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    MjcActuator *actuator, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references; (void)options;
  std::set<std::string> table;
#define PRIM_PTR_ actuator
  for (auto &prop : properties) {
    MJC_ACTUATOR_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    MJC_ACTUATOR_RELS(EXPAND_SINGLE_REL)
    ADD_PROPERTY(table, prop, MjcActuator, actuator->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// NewtonActuator
// ============================================================================
template <>
bool ReconstructPrim<NewtonActuator>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    NewtonActuator *actuator, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references; (void)options;
  std::set<std::string> table;
#define PRIM_PTR_ actuator
  for (auto &prop : properties) {
    NEWTON_ACTUATOR_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    NEWTON_ACTUATOR_RELS(EXPAND_MULTI_REL)
    ADD_PROPERTY(table, prop, NewtonActuator, actuator->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// MjcTendon
// ============================================================================
template <>
bool ReconstructPrim<MjcTendon>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    MjcTendon *tendon, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references; (void)options;
  std::set<std::string> table;
#define PRIM_PTR_ tendon
  for (auto &prop : properties) {
    MJC_TENDON_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    // MJC_TENDON_RELS (mjc:path / mjc:sideSites) are multi-target rels:
    // a `<fixed>` tendon (e.g. Panda's finger linkage) lists 2+ joint
    // paths, and a `<spatial>` tendon lists N path/site segments.
    // Use the path-vector parser; single targets still parse cleanly.
    MJC_TENDON_RELS(EXPAND_MULTI_REL)
    ADD_PROPERTY(table, prop, MjcTendon, tendon->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// MjcKeyframe
// ============================================================================
template <>
bool ReconstructPrim<MjcKeyframe>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    MjcKeyframe *keyframe, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references; (void)options;
  std::set<std::string> table;
#define PRIM_PTR_ keyframe
  for (auto &prop : properties) {
    MJC_KEYFRAME_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, MjcKeyframe, keyframe->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// PhysicsCollisionGroup
// ============================================================================
template <>
bool ReconstructPrim<PhysicsCollisionGroup>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    PhysicsCollisionGroup *group, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec; (void)references; (void)options;
  std::set<std::string> table;
#define PRIM_PTR_ group
  for (auto &prop : properties) {
    PHYSICS_COLLISION_GROUP_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PHYSICS_COLLISION_GROUP_RELS(EXPAND_SINGLE_REL)
    ADD_PROPERTY(table, prop, PhysicsCollisionGroup, group->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
#undef PRIM_PTR_
  return true;
}

// ============================================================================
// PrimSpec wrappers
// ============================================================================

#define RECONSTRUCT_PRIM_PRIMSPEC_IMPL(__prim_ty) \
template <> \
bool ReconstructPrim<__prim_ty>( \
    PrimSpec &primspec, \
    __prim_ty *prim, \
    std::string *warn, \
    std::string *err, \
    const PrimReconstructOptions &options) { \
 \
  ReferenceList references; /* dummy */ \
 \
  return ReconstructPrim<__prim_ty>(primspec.specifier(), primspec.props(), references, prim, warn, err, options); \
}

RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PhysicsScene)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PhysicsRevoluteJoint)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PhysicsPrismaticJoint)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PhysicsSphericalJoint)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PhysicsFixedJoint)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PhysicsDistanceJoint)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PhysicsCollisionGroup)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(MjcActuator)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(NewtonActuator)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(MjcTendon)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(MjcKeyframe)

}  // namespace prim
}  // namespace tinyusdz
