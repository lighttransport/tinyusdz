// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdPhysics Joint Schema

#pragma once

#include "../stage/stage.hh"
#include "../eval/attribute-eval.hh"
#include <string>

namespace tinyusdz {
namespace next {

// ============================================================
// Joint type checking
// ============================================================

bool IsPhysicsJoint(const UsdPrim& prim);
bool IsPhysicsPrismaticJoint(const UsdPrim& prim);
bool IsPhysicsRevoluteJoint(const UsdPrim& prim);
bool IsPhysicsSphericalJoint(const UsdPrim& prim);
bool IsPhysicsFixedJoint(const UsdPrim& prim);
bool IsPhysicsDistanceJoint(const UsdPrim& prim);
bool IsPhysicsSliderJoint(const UsdPrim& prim);
bool IsPhysicsBallJoint(const UsdPrim& prim);

// ============================================================
// Common joint data
// ============================================================

struct PhysicsJointData {
  std::string body0;  // rel physics:body0
  std::string body1;  // rel physics:body1

  // Local pose 0
  float localPos0[3] = {0.0f, 0.0f, 0.0f};
  float localQuat0[4] = {1.0f, 0.0f, 0.0f, 0.0f};

  // Local pose 1
  float localPos1[3] = {0.0f, 0.0f, 0.0f};
  float localQuat1[4] = {1.0f, 0.0f, 0.0f, 0.0f};

  // Joint frame
  float jointFramePosition[3] = {0.0f, 0.0f, 0.0f};
  float jointFrameQuat[4] = {1.0f, 0.0f, 0.0f, 0.0f};

  bool hasBody0 = false;
  bool hasBody1 = false;
  bool hasLocalPos0 = false;
  bool hasLocalPos1 = false;
  bool hasLocalRot0 = false;
  bool hasLocalRot1 = false;

  // Break force/torque
  float breakForce = 1e10f;
  float breakTorque = 1e10f;

  // Collision enabled
  bool collisionEnabled = true;

  bool enableMotor = false;
  float motorTargetVelocity = 0.0f;
  float motorMaxForce = 1e10f;
};

/// Get common joint data for any PhysicsJoint type
bool GetPhysicsJointData(const Stage& stage, const UsdPrim& prim,
                          PhysicsJointData* out, double time = 0.0);

// ============================================================
// PrismaticJoint (1 DOF translation)
// ============================================================

struct PhysicsPrismaticJointData : public PhysicsJointData {
  float axis[3] = {1.0f, 0.0f, 0.0f};
  float lowerLimit = -1e10f;
  float upperLimit = 1e10f;
  float maxForce = 1e10f;
};

bool GetPhysicsPrismaticJointData(const Stage& stage, const UsdPrim& prim,
                                   PhysicsPrismaticJointData* out,
                                   double time = 0.0);

// ============================================================
// RevoluteJoint (1 DOF rotation)
// ============================================================

struct PhysicsRevoluteJointData : public PhysicsJointData {
  float axis[3] = {1.0f, 0.0f, 0.0f};
  float lowerLimit = -1e10f;
  float upperLimit = 1e10f;
  float maxTorque = 1e10f;
};

bool GetPhysicsRevoluteJointData(const Stage& stage, const UsdPrim& prim,
                                  PhysicsRevoluteJointData* out,
                                  double time = 0.0);

// ============================================================
// SphericalJoint (3 DOF rotation)
// ============================================================

struct PhysicsSphericalJointData : public PhysicsJointData {
  float coneAngleLimit = 180.0f;
  float coneAngleLimitY = 180.0f;
  float coneAngleLimitZ = 180.0f;
  float maxTorque = 1e10f;
};

bool GetPhysicsSphericalJointData(const Stage& stage, const UsdPrim& prim,
                                   PhysicsSphericalJointData* out,
                                   double time = 0.0);

// ============================================================
// FixedJoint (0 DOF)
// ============================================================

struct PhysicsFixedJointData : public PhysicsJointData {
  // No joint-specific properties beyond base
};

bool GetPhysicsFixedJointData(const Stage& stage, const UsdPrim& prim,
                               PhysicsFixedJointData* out,
                               double time = 0.0);

// ============================================================
// DistanceJoint (1 DOF distance)
// ============================================================

struct PhysicsDistanceJointData : public PhysicsJointData {
  float minDistance = 0.0f;
  float maxDistance = 1e10f;
};

bool GetPhysicsDistanceJointData(const Stage& stage, const UsdPrim& prim,
                                  PhysicsDistanceJointData* out,
                                  double time = 0.0);

// ============================================================
// SliderJoint (1 DOF translation, alias for Prismatic)
// ============================================================

using PhysicsSliderJointData = PhysicsPrismaticJointData;

bool GetPhysicsSliderJointData(const Stage& stage, const UsdPrim& prim,
                                PhysicsSliderJointData* out,
                                double time = 0.0);

// ============================================================
// BallJoint (3 DOF rotation, alias for Spherical)
// ============================================================

using PhysicsBallJointData = PhysicsSphericalJointData;

bool GetPhysicsBallJointData(const Stage& stage, const UsdPrim& prim,
                              PhysicsBallJointData* out,
                              double time = 0.0);

} // namespace next
} // namespace tinyusdz
