// SPDX-License-Identifier: Apache 2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// USDC writer: Physics prim property extraction
// Split from stage-converter.cc
//
#include "sconv-detail.hh"
#include "usdPhysics.hh"
#include "mjcPhysics.hh"
#include "newtonPhysics.hh"

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

// Like EXTRACT_FALLBACK / EXTRACT_TOKEN_FALLBACK, but only emit when the
// attribute was actually authored. Used for large API structs (e.g. the ~60
// field MjcSceneAPI) where unconditionally writing every fallback would bloat
// the crate and turn unauthored attrs into authored-with-default on read-back.
#define EXTRACT_FALLBACK_IF_AUTHORED(attr, name) do { \
  if ((attr).authored()) { EXTRACT_FALLBACK(attr, name); } \
} while(0)
#define EXTRACT_TOKEN_FALLBACK_IF_AUTHORED(attr, name) do { \
  if ((attr).authored()) { EXTRACT_TOKEN_FALLBACK(attr, name); } \
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
  // MjcSceneAPI mirror: the reconstruct path consumes mjc:option/flag/compiler
  // props into the typed MjcSceneAPI struct and removes them from `props`, so
  // the generic props-map pass never re-emits them. Re-emit the authored
  // fields here so a MuJoCo PhysicsScene survives USDC round-trip (previously
  // these were silently dropped on write). Mirrors EXTRACT_JOINT_BASE's mjc
  // block for joints and pprint-physics.cc's print_mjc_scene_api for USDA.
  if (scene->mjcScene.has_value()) {
    const auto &_m = scene->mjcScene.value();
    // option:*
    EXTRACT_FALLBACK_IF_AUTHORED(_m.timestep, "mjc:option:timestep");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.impratio, "mjc:option:impratio");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.wind, "mjc:option:wind");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.magnetic, "mjc:option:magnetic");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.density, "mjc:option:density");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.viscosity, "mjc:option:viscosity");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.o_margin, "mjc:option:o_margin");
    EXTRACT_TYPED(_m.o_solref, "mjc:option:o_solref");
    EXTRACT_TYPED(_m.o_solimp, "mjc:option:o_solimp");
    EXTRACT_TYPED(_m.o_friction, "mjc:option:o_friction");
    EXTRACT_TOKEN_FALLBACK_IF_AUTHORED(_m.integrator, "mjc:option:integrator");
    EXTRACT_TOKEN_FALLBACK_IF_AUTHORED(_m.cone, "mjc:option:cone");
    EXTRACT_TOKEN_FALLBACK_IF_AUTHORED(_m.jacobian, "mjc:option:jacobian");
    EXTRACT_TOKEN_FALLBACK_IF_AUTHORED(_m.solver, "mjc:option:solver");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.iterations, "mjc:option:iterations");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.tolerance, "mjc:option:tolerance");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.ls_iterations, "mjc:option:ls_iterations");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.ls_tolerance, "mjc:option:ls_tolerance");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.noslip_iterations, "mjc:option:noslip_iterations");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.noslip_tolerance, "mjc:option:noslip_tolerance");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.ccd_iterations, "mjc:option:ccd_iterations");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.ccd_tolerance, "mjc:option:ccd_tolerance");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.sdf_iterations, "mjc:option:sdf_iterations");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.sdf_initpoints, "mjc:option:sdf_initpoints");
    EXTRACT_TYPED(_m.actuatorgroupdisable, "mjc:option:actuatorgroupdisable");
    // flag:*
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_constraint, "mjc:flag:constraint");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_equality, "mjc:flag:equality");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_frictionloss, "mjc:flag:frictionloss");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_limit, "mjc:flag:limit");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_contact, "mjc:flag:contact");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_spring, "mjc:flag:spring");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_damper, "mjc:flag:damper");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_gravity, "mjc:flag:gravity");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_clampctrl, "mjc:flag:clampctrl");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_warmstart, "mjc:flag:warmstart");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_filterparent, "mjc:flag:filterparent");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_actuation, "mjc:flag:actuation");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_refsafe, "mjc:flag:refsafe");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_sensor, "mjc:flag:sensor");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_midphase, "mjc:flag:midphase");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_nativeccd, "mjc:flag:nativeccd");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_eulerdamp, "mjc:flag:eulerdamp");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_autoreset, "mjc:flag:autoreset");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_island, "mjc:flag:island");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_override, "mjc:flag:override");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_energy, "mjc:flag:energy");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_fwdinv, "mjc:flag:fwdinv");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_invdiscrete, "mjc:flag:invdiscrete");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.flag_multiccd, "mjc:flag:multiccd");
    // compiler:*
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_autoLimits, "mjc:compiler:autoLimits");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_boundMass, "mjc:compiler:boundMass");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_boundInertia, "mjc:compiler:boundInertia");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_setTotalMass, "mjc:compiler:setTotalMass");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_useThread, "mjc:compiler:useThread");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_balanceInertia, "mjc:compiler:balanceInertia");
    EXTRACT_TOKEN_FALLBACK_IF_AUTHORED(_m.compiler_angle, "mjc:compiler:angle");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_fitAABB, "mjc:compiler:fitAABB");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_fuseStatic, "mjc:compiler:fuseStatic");
    EXTRACT_TOKEN_FALLBACK_IF_AUTHORED(_m.compiler_inertiaFromGeom, "mjc:compiler:inertiaFromGeom");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_alignFree, "mjc:compiler:alignFree");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_inertiaGroupRangeMin, "mjc:compiler:inertiaGroupRange:min");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_inertiaGroupRangeMax, "mjc:compiler:inertiaGroupRange:max");
    EXTRACT_FALLBACK_IF_AUTHORED(_m.compiler_saveInertial, "mjc:compiler:saveInertial");
  }
  if (scene->newtonScene.has_value()) {
    const auto &_n = scene->newtonScene.value();
    EXTRACT_FALLBACK(_n.maxSolverIterations, "newton:maxSolverIterations");
    EXTRACT_FALLBACK(_n.timeStepsPerSecond, "newton:timeStepsPerSecond");
    EXTRACT_FALLBACK(_n.gravityEnabled, "newton:gravityEnabled");
  }
  if (scene->newtonXpbdScene.has_value()) {
    const auto &_n = scene->newtonXpbdScene.value();
    EXTRACT_FALLBACK(_n.softBodyRelaxation, "newton:xpbd:softBodyRelaxation");
    EXTRACT_FALLBACK(_n.softContactRelaxation, "newton:xpbd:softContactRelaxation");
    EXTRACT_FALLBACK(_n.jointLinearRelaxation, "newton:xpbd:jointLinearRelaxation");
    EXTRACT_FALLBACK(_n.jointAngularRelaxation, "newton:xpbd:jointAngularRelaxation");
    EXTRACT_FALLBACK(_n.jointLinearCompliance, "newton:xpbd:jointLinearCompliance");
    EXTRACT_FALLBACK(_n.jointAngularCompliance, "newton:xpbd:jointAngularCompliance");
    EXTRACT_FALLBACK(_n.rigidContactRelaxation, "newton:xpbd:rigidContactRelaxation");
    EXTRACT_FALLBACK(_n.rigidContactConWeighting, "newton:xpbd:rigidContactConWeighting");
    EXTRACT_FALLBACK(_n.angularDamping, "newton:xpbd:angularDamping");
    EXTRACT_FALLBACK(_n.restitutionEnabled, "newton:xpbd:restitutionEnabled");
  }
  if (scene->newtonKaminoScene.has_value()) {
    const auto &_n = scene->newtonKaminoScene.value();
    EXTRACT_FALLBACK(_n.padmmPrimalTolerance, "newton:kamino:padmm:primalTolerance");
    EXTRACT_FALLBACK(_n.padmmDualTolerance, "newton:kamino:padmm:dualTolerance");
    EXTRACT_FALLBACK(_n.padmmComplementarityTolerance, "newton:kamino:padmm:complementarityTolerance");
    EXTRACT_TOKEN_FALLBACK(_n.padmmWarmstarting, "newton:kamino:padmm:warmstarting");
    EXTRACT_FALLBACK(_n.padmmUseAcceleration, "newton:kamino:padmm:useAcceleration");
    EXTRACT_FALLBACK(_n.constraintsUsePreconditioning, "newton:kamino:constraints:usePreconditioning");
    EXTRACT_FALLBACK(_n.constraintsAlpha, "newton:kamino:constraints:alpha");
    EXTRACT_FALLBACK(_n.constraintsBeta, "newton:kamino:constraints:beta");
    EXTRACT_FALLBACK(_n.constraintsGamma, "newton:kamino:constraints:gamma");
    EXTRACT_TOKEN_FALLBACK(_n.jointCorrection, "newton:kamino:jointCorrection");
  }
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
  if ((j).newtonMimic.has_value()) { \
    const auto &_n = (j).newtonMimic.value(); \
    EXTRACT_FALLBACK(_n.mimicEnabled, "newton:mimicEnabled"); \
    EXTRACT_REL(_n.mimicJoint, "newton:mimicJoint"); \
    EXTRACT_FALLBACK(_n.mimicCoef0, "newton:mimicCoef0"); \
    EXTRACT_FALLBACK(_n.mimicCoef1, "newton:mimicCoef1"); \
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

bool CrateWriter::ExtractNewtonActuatorProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const NewtonActuator *a = prim.data().as<NewtonActuator>();
  if (!a) { if (err) *err = "Failed to cast to NewtonActuator"; return false; }
  EXTRACT_REL(a->targets, "newton:targets");
  EXTRACT_FALLBACK(a->delaySteps, "newton:delaySteps");
  EXTRACT_FALLBACK(a->constEffort, "newton:constEffort");
  EXTRACT_FALLBACK(a->kp, "newton:kp");
  EXTRACT_FALLBACK(a->kd, "newton:kd");
  EXTRACT_FALLBACK(a->ki, "newton:ki");
  EXTRACT_FALLBACK(a->integralMax, "newton:integralMax");
  EXTRACT_TYPED(a->modelPath, "newton:modelPath");
  EXTRACT_FALLBACK(a->maxEffort, "newton:maxEffort");
  EXTRACT_FALLBACK(a->maxMotorEffort, "newton:maxMotorEffort");
  EXTRACT_FALLBACK(a->saturationEffort, "newton:saturationEffort");
  EXTRACT_FALLBACK(a->velocityLimit, "newton:velocityLimit");
  EXTRACT_TYPED(a->lookupPositions, "newton:lookupPositions");
  EXTRACT_TYPED(a->lookupEfforts, "newton:lookupEfforts");
  (void)prim_path;
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
#undef EXTRACT_TOKEN
#undef EXTRACT_TOKEN_FALLBACK
#undef EXTRACT_JOINT_BASE

}  // namespace experimental
}  // namespace tinyusdz
