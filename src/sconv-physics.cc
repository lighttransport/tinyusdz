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

// Helper macro: extract a TypedAttribute<T> as a crate field using ConvertValue
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

// Helper macro: extract a TypedAttributeWithFallback<T> (always has a value)
#define EXTRACT_FALLBACK(attr, name) do { \
  crate::CrateValue _cv; \
  value::Value _v((attr).get_value()); \
  std::string _cerr; \
  if (ConvertValue(_v, _cv, &_cerr)) { \
    fields.push_back({(name), _cv}); \
  } \
} while(0)

// Helper: extract a RelationshipProperty
static void ExtractRelProp(const RelationshipProperty &rp, const char *name,
                           crate::FieldValuePairVector &fields) {
  if (!rp.authored()) return;
  const auto &rel = rp.relationship();
  if (rel.is_path()) {
    crate::CrateValue cv;
    std::vector<Path> targets;
    targets.push_back(rel.targetPath);
    cv.Set(targets);
    fields.push_back({name, cv});
  }
}

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
  (void)prim_path; (void)err;
  return true;
}

// ============================================================================
// Physics Joint types
// ============================================================================

#define EXTRACT_JOINT_BASE(j) do { \
  ExtractRelProp((j).body0, "physics:body0", fields); \
  ExtractRelProp((j).body1, "physics:body1", fields); \
  EXTRACT_TYPED((j).localPos0, "physics:localPos0"); \
  EXTRACT_TYPED((j).localPos1, "physics:localPos1"); \
  EXTRACT_TYPED((j).localRot0, "physics:localRot0"); \
  EXTRACT_TYPED((j).localRot1, "physics:localRot1"); \
  EXTRACT_TYPED((j).jointEnabled, "physics:jointEnabled"); \
  EXTRACT_TYPED((j).collisionEnabled, "physics:collisionEnabled"); \
  EXTRACT_TYPED((j).breakForce, "physics:breakForce"); \
  EXTRACT_TYPED((j).breakTorque, "physics:breakTorque"); \
  EXTRACT_TYPED((j).excludeFromArticulation, "physics:excludeFromArticulation"); \
} while(0)

bool CrateWriter::ExtractPhysicsRevoluteJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsRevoluteJoint *j = prim.data().as<PhysicsRevoluteJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsRevoluteJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  EXTRACT_TYPED(j->axis, "physics:axis");
  EXTRACT_TYPED(j->lowerLimit, "physics:lowerLimit");
  EXTRACT_TYPED(j->upperLimit, "physics:upperLimit");
  (void)prim_path;
  return true;
}

bool CrateWriter::ExtractPhysicsPrismaticJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsPrismaticJoint *j = prim.data().as<PhysicsPrismaticJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsPrismaticJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  EXTRACT_TYPED(j->axis, "physics:axis");
  EXTRACT_TYPED(j->lowerLimit, "physics:lowerLimit");
  EXTRACT_TYPED(j->upperLimit, "physics:upperLimit");
  (void)prim_path;
  return true;
}

bool CrateWriter::ExtractPhysicsSphericalJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsSphericalJoint *j = prim.data().as<PhysicsSphericalJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsSphericalJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  EXTRACT_TYPED(j->axis, "physics:axis");
  EXTRACT_TYPED(j->coneAngle0Limit, "physics:coneAngle0Limit");
  EXTRACT_TYPED(j->coneAngle1Limit, "physics:coneAngle1Limit");
  (void)prim_path;
  return true;
}

bool CrateWriter::ExtractPhysicsFixedJointProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const PhysicsFixedJoint *j = prim.data().as<PhysicsFixedJoint>();
  if (!j) { if (err) *err = "Failed to cast to PhysicsFixedJoint"; return false; }
  EXTRACT_JOINT_BASE(*j);
  (void)prim_path;
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
  (void)prim_path;
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
  EXTRACT_TYPED(g->mergeGroup, "physics:mergeGroup");
  EXTRACT_FALLBACK(g->invertFilteredGroups, "physics:invertFilteredGroups");
  ExtractRelProp(g->filteredGroups, "physics:filteredGroups", fields);
  (void)prim_path;
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
  ExtractRelProp(a->target, "mjc:target", fields);
  EXTRACT_FALLBACK(a->dynType, "mjc:dynType");
  EXTRACT_FALLBACK(a->gainType, "mjc:gainType");
  EXTRACT_FALLBACK(a->biasType, "mjc:biasType");
  (void)prim_path;
  return true;
}

bool CrateWriter::ExtractMjcTendonProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const MjcTendon *t = prim.data().as<MjcTendon>();
  if (!t) { if (err) *err = "Failed to cast to MjcTendon"; return false; }
  EXTRACT_FALLBACK(t->type, "mjc:type");
  EXTRACT_FALLBACK(t->group, "mjc:group");
  EXTRACT_FALLBACK(t->stiffness, "mjc:stiffness");
  EXTRACT_FALLBACK(t->damping, "mjc:damping");
  ExtractRelProp(t->path, "mjc:path", fields);
  (void)prim_path;
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
#undef EXTRACT_JOINT_BASE

}  // namespace experimental
}  // namespace tinyusdz
