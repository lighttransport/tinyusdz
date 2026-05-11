// SPDX-License-Identifier: Apache 2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// USDC writer: Physics prim property extraction
// Split from stage-converter.cc
//
#include "sconv-detail.hh"
#include "usdPhysics.hh"
#include "mjcPhysics.hh"

namespace tinyusdz {
namespace experimental {

// Helper macro: extract a TypedAttribute<T> as a crate field.
// Goes through value::Value -> ConvertValue for general types.
#define EXTRACT_TYPED(attr, name) do { \
  auto _opt = (attr).get_value(); \
  if (_opt.has_value()) { \
    crate::CrateValue _cv; \
    value::Value _v(_opt.value()); \
    std::string _cerr; \
    if (ConvertValue(_v, _cv, &_cerr)) { \
      fields.push_back({(name), _cv}); \
    } \
  } \
} while(0)

// Helper macro: extract a TypedAttributeWithFallback<T>
#define EXTRACT_FALLBACK(attr, name) do { \
  crate::CrateValue _cv; \
  value::Value _v((attr).get_value()); \
  std::string _cerr; \
  if (ConvertValue(_v, _cv, &_cerr)) { \
    fields.push_back({(name), _cv}); \
  } \
} while(0)

// Helper macro: extract a token TypedAttribute directly (not as uint index)
#define EXTRACT_TOKEN(attr, name) do { \
  auto _opt = (attr).get_value(); \
  if (_opt.has_value()) { \
    crate::CrateValue _cv; \
    _cv.Set(_opt.value()); \
    fields.push_back({(name), _cv}); \
  } \
} while(0)

// Helper macro: extract a token TypedAttributeWithFallback directly
#define EXTRACT_TOKEN_FALLBACK(attr, name) do { \
  crate::CrateValue _cv; \
  _cv.Set((attr).get_value()); \
  fields.push_back({(name), _cv}); \
} while(0)

// Helper: write a RelationshipProperty as a separate relationship spec
// (relationships must be separate SpecType::Relationship specs, not prim fields)
#define EXTRACT_REL(rp, name) do { \
  if ((rp).authored()) { \
    ConvertRelationshipToFields((name), (rp).relationship(), prim_path, err); \
  } \
} while(0)

// ============================================================================
// PhysicsScene
// ============================================================================

bool CrateWriter::ExtractPhysicsSceneProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsScene *scene = prim.data().as<PhysicsScene>();
  if (!scene) {
    if (err) *err = "Failed to cast prim to PhysicsScene";
    return false;
  }
  EXTRACT_TYPED(scene->gravityDirection, "physics:gravityDirection");
  EXTRACT_TYPED(scene->gravityMagnitude, "physics:gravityMagnitude");
  (void)prim_path;
  return true;
}

// ============================================================================
// Physics Joint types
// ============================================================================

#define EXTRACT_JOINT_BASE(j) do { \
  EXTRACT_REL((j).body0, "physics:body0"); \
  EXTRACT_REL((j).body1, "physics:body1"); \
  EXTRACT_TYPED((j).localPos0, "physics:localPos0"); \
  EXTRACT_TYPED((j).localPos1, "physics:localPos1"); \
  EXTRACT_TYPED((j).localRot0, "physics:localRot0"); \
  EXTRACT_TYPED((j).localRot1, "physics:localRot1"); \
  EXTRACT_TYPED((j).jointEnabled, "physics:jointEnabled"); \
  EXTRACT_TYPED((j).collisionEnabled, "physics:collisionEnabled"); \
  EXTRACT_TYPED((j).breakForce, "physics:breakForce"); \
  EXTRACT_TYPED((j).breakTorque, "physics:breakTorque"); \
  EXTRACT_TYPED((j).excludeFromArticulation, "physics:excludeFromArticulation"); \
  /* MjcJointAPI mirror: the reconstruct path consumes mjc:* into a typed   \
   * MjcJointAPI struct and removes them from `props`, so the generic       \
   * props-map iteration in stage-converter.cc never sees them. Re-emit     \
   * here so USDC round-trip preserves the values for downstream consumers  \
   * (MuJoCo / Genesis / Newton / PhysX schema mirror). Only emit when the  \
   * API schema is actually attached.                                       \
   */ \
  if ((j).mjcJoint.has_value()) { \
    const auto &_m = (j).mjcJoint.value(); \
    EXTRACT_FALLBACK(_m.group, "mjc:group"); \
    EXTRACT_FALLBACK(_m.stiffness, "mjc:stiffness"); \
    EXTRACT_FALLBACK(_m.damping, "mjc:damping"); \
    EXTRACT_FALLBACK(_m.armature, "mjc:armature"); \
    EXTRACT_FALLBACK(_m.frictionloss, "mjc:frictionloss"); \
    EXTRACT_TYPED(_m.springdamper, "mjc:springdamper"); \
    EXTRACT_FALLBACK(_m.springref, "mjc:springref"); \
    EXTRACT_FALLBACK(_m.ref, "mjc:ref"); \
    EXTRACT_FALLBACK(_m.margin, "mjc:margin"); \
    EXTRACT_TYPED(_m.solreflimit, "mjc:solreflimit"); \
    EXTRACT_TYPED(_m.solimplimit, "mjc:solimplimit"); \
    EXTRACT_TYPED(_m.solreffriction, "mjc:solreffriction"); \
    EXTRACT_TYPED(_m.solimpfriction, "mjc:solimpfriction"); \
    EXTRACT_FALLBACK(_m.actuatorfrcrange_min, "mjc:actuatorfrcrange:min"); \
    EXTRACT_FALLBACK(_m.actuatorfrcrange_max, "mjc:actuatorfrcrange:max"); \
    EXTRACT_TOKEN_FALLBACK(_m.actuatorfrclimited, "mjc:actuatorfrclimited"); \
    EXTRACT_FALLBACK(_m.actuatorgravcomp, "mjc:actuatorgravcomp"); \
  } \
} while(0)

bool CrateWriter::ExtractPhysicsJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsJoint *j = prim.data().as<PhysicsJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  return true;
}

bool CrateWriter::ExtractPhysicsRevoluteJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsRevoluteJoint *j = prim.data().as<PhysicsRevoluteJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsRevoluteJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  EXTRACT_TOKEN(j->axis, "physics:axis");
  EXTRACT_TYPED(j->lowerLimit, "physics:lowerLimit");
  EXTRACT_TYPED(j->upperLimit, "physics:upperLimit");
  return true;
}

bool CrateWriter::ExtractPhysicsPrismaticJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsPrismaticJoint *j = prim.data().as<PhysicsPrismaticJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsPrismaticJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  EXTRACT_TOKEN(j->axis, "physics:axis");
  EXTRACT_TYPED(j->lowerLimit, "physics:lowerLimit");
  EXTRACT_TYPED(j->upperLimit, "physics:upperLimit");
  return true;
}

bool CrateWriter::ExtractPhysicsSphericalJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsSphericalJoint *j = prim.data().as<PhysicsSphericalJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsSphericalJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  EXTRACT_TOKEN(j->axis, "physics:axis");
  EXTRACT_TYPED(j->coneAngle0Limit, "physics:coneAngle0Limit");
  EXTRACT_TYPED(j->coneAngle1Limit, "physics:coneAngle1Limit");
  return true;
}

bool CrateWriter::ExtractPhysicsFixedJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsFixedJoint *j = prim.data().as<PhysicsFixedJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsFixedJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  return true;
}

bool CrateWriter::ExtractPhysicsDistanceJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsDistanceJoint *j = prim.data().as<PhysicsDistanceJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsDistanceJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  EXTRACT_TYPED(j->minDistance, "physics:minDistance");
  EXTRACT_TYPED(j->maxDistance, "physics:maxDistance");
  return true;
}

// ============================================================================
// PhysicsCollisionGroup
// ============================================================================

bool CrateWriter::ExtractPhysicsCollisionGroupProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsCollisionGroup *g = prim.data().as<PhysicsCollisionGroup>();
  if (!g) { if (err) *err = "Failed to cast to PhysicsCollisionGroup"; return false; }
  EXTRACT_TOKEN(g->mergeGroup, "physics:mergeGroup");
  EXTRACT_FALLBACK(g->invertFilteredGroups, "physics:invertFilteredGroups");
  EXTRACT_REL(g->filteredGroups, "physics:filteredGroups");
  return true;
}

// ============================================================================
// MjcActuator, MjcTendon, MjcKeyframe
// ============================================================================

bool CrateWriter::ExtractMjcActuatorProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const MjcActuator *a = prim.data().as<MjcActuator>();
  if (!a) { if (err) *err = "Failed to cast to MjcActuator"; return false; }
  EXTRACT_FALLBACK(a->group, "mjc:group");
  EXTRACT_REL(a->target, "mjc:target");
  EXTRACT_TOKEN_FALLBACK(a->dynType, "mjc:dynType");
  EXTRACT_TOKEN_FALLBACK(a->gainType, "mjc:gainType");
  EXTRACT_TOKEN_FALLBACK(a->biasType, "mjc:biasType");
  return true;
}

bool CrateWriter::ExtractMjcTendonProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const MjcTendon *t = prim.data().as<MjcTendon>();
  if (!t) { if (err) *err = "Failed to cast to MjcTendon"; return false; }
  EXTRACT_TOKEN_FALLBACK(t->type, "mjc:type");
  EXTRACT_FALLBACK(t->group, "mjc:group");
  EXTRACT_FALLBACK(t->stiffness, "mjc:stiffness");
  EXTRACT_FALLBACK(t->damping, "mjc:damping");
  EXTRACT_REL(t->path, "mjc:path");
  return true;
}

bool CrateWriter::ExtractMjcKeyframeProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const MjcKeyframe *kf = prim.data().as<MjcKeyframe>();
  if (!kf) { if (err) *err = "Failed to cast to MjcKeyframe"; return false; }
  EXTRACT_TYPED(kf->qpos, "mjc:qpos");
  EXTRACT_TYPED(kf->qvel, "mjc:qvel");
  EXTRACT_TYPED(kf->act, "mjc:act");
  EXTRACT_TYPED(kf->ctrl, "mjc:ctrl");
  EXTRACT_TYPED(kf->mpos, "mjc:mpos");
  EXTRACT_TYPED(kf->mquat, "mjc:mquat");
  (void)prim_path;
  return true;
}

#undef EXTRACT_TYPED
#undef EXTRACT_FALLBACK
#undef EXTRACT_REL
#undef EXTRACT_JOINT_BASE

}  // namespace experimental
}  // namespace tinyusdz
