// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdPhysics Applied API Schemas
// RigidBodyAPI, CollisionAPI, MaterialAPI, MeshCollisionAPI,
// MassAPI, FilteredPairsAPI, ArticulationRootAPI, DriveAPI, LimitAPI

#pragma once

#include "../stage/stage.hh"
#include "../eval/attribute-eval.hh"
#include <string>
#include <vector>
#include <map>

namespace tinyusdz {
namespace next {

// ============================================================
// PhysicsRigidBodyAPI
// ============================================================

struct PhysicsRigidBodyData {
  bool rigidBodyEnabled = true;
  float mass = 0.0f;
  float density = 0.0f;
  float centerOfMass[3] = {0.0f, 0.0f, 0.0f};
  float diagonalInertia[3] = {0.0f, 0.0f, 0.0f};
  // physics:principalAxes is a quatf in (w, x, y, z) order (USD text order), so
  // {1, 0, 0, 0} is the identity orientation. Keep this order if a consumer is
  // added (none reads it today).
  float principalAxes[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  float velocity[3] = {0.0f, 0.0f, 0.0f};
  float angularVelocity[3] = {0.0f, 0.0f, 0.0f};
  bool startsAsleep = false;
};

bool HasPhysicsRigidBodyAPI(const UsdPrim& prim);
bool GetPhysicsRigidBodyData(const Stage& stage, const UsdPrim& prim,
                              PhysicsRigidBodyData* out, double time = 0.0);

// ============================================================
// PhysicsCollisionAPI
// ============================================================

struct PhysicsCollisionData {
  bool collisionEnabled = true;
  std::string simulationOwner; // rel physics:simulationOwner
};

bool HasPhysicsCollisionAPI(const UsdPrim& prim);
bool GetPhysicsCollisionData(const Stage& stage, const UsdPrim& prim,
                              PhysicsCollisionData* out);

// ============================================================
// PhysicsMaterialAPI
// ============================================================

struct PhysicsMaterialData {
  float staticFriction = 0.0f;
  float dynamicFriction = 0.0f;
  float restitution = 0.0f;
  float density = 0.0f;
};

bool HasPhysicsMaterialAPI(const UsdPrim& prim);
bool GetPhysicsMaterialData(const Stage& stage, const UsdPrim& prim,
                             PhysicsMaterialData* out);

// ============================================================
// PhysicsMeshCollisionAPI
// ============================================================

struct PhysicsMeshCollisionData {
  std::string approximation = "none";
};

bool HasPhysicsMeshCollisionAPI(const UsdPrim& prim);
bool GetPhysicsMeshCollisionData(const UsdPrim& prim,
                                  PhysicsMeshCollisionData* out);

// ============================================================
// PhysicsMassAPI
// ============================================================

struct PhysicsMassData {
  float mass = 0.0f;
  float density = 0.0f;
  float centerOfMass[3] = {0.0f, 0.0f, 0.0f};
  float diagonalInertia[3] = {0.0f, 0.0f, 0.0f};
  // physics:principalAxes is a quatf in (w, x, y, z) order (USD text order), so
  // {1, 0, 0, 0} is the identity orientation. Keep this order if a consumer is
  // added (none reads it today).
  float principalAxes[4] = {1.0f, 0.0f, 0.0f, 0.0f};
};

bool HasPhysicsMassAPI(const UsdPrim& prim);
bool GetPhysicsMassData(const Stage& stage, const UsdPrim& prim,
                         PhysicsMassData* out);

// ============================================================
// PhysicsFilteredPairsAPI
// ============================================================

struct PhysicsFilteredPairsData {
  std::vector<std::string> filteredPairPaths; // resolved rel targets
};

bool HasPhysicsFilteredPairsAPI(const UsdPrim& prim);
bool GetPhysicsFilteredPairsData(const UsdPrim& prim,
                                  PhysicsFilteredPairsData* out);

// ============================================================
// PhysicsArticulationRootAPI (marker — no properties)
// ============================================================

bool HasPhysicsArticulationRootAPI(const UsdPrim& prim);

// ============================================================
// PhysicsDriveAPI (multi-apply, per-DOF)
// ============================================================

struct PhysicsDriveData {
  std::string dof; // "transX", "transY", "transZ", "rotX", "rotY", "rotZ"
  std::string type = "force"; // "force" or "acceleration"
  float maxForce = 1e10f;
  float targetPosition = 0.0f;
  float targetVelocity = 0.0f;
  float damping = 0.0f;
  float stiffness = 0.0f;
};

bool HasPhysicsDriveAPI(const UsdPrim& prim, const std::string& dof);
bool GetPhysicsDriveData(const UsdPrim& prim, const std::string& dof,
                          PhysicsDriveData* out);

// ============================================================
// PhysicsLimitAPI (multi-apply, per-DOF)
// ============================================================

struct PhysicsLimitData {
  std::string dof;
  float low = -1e10f;
  float high = 1e10f;
};

bool HasPhysicsLimitAPI(const UsdPrim& prim, const std::string& dof);
bool GetPhysicsLimitData(const UsdPrim& prim, const std::string& dof,
                          PhysicsLimitData* out);

} // namespace next
} // namespace tinyusdz
