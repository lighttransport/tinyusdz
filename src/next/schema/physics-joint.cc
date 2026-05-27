// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdPhysics Joint Schema Implementation

#include "physics-joint.hh"
#include <cstring>

namespace tinyusdz {
namespace next {

// ============================================================
// Joint type checking
// ============================================================

static bool IsJointType(const UsdPrim& prim, const std::string& type) {
  return prim.IsValid() && prim.GetTypeName() == type;
}

bool IsPhysicsJoint(const UsdPrim& prim) {
  return prim.IsValid() && (prim.GetTypeName().find("Physics") != std::string::npos);
}

bool IsPhysicsPrismaticJoint(const UsdPrim& prim) {
  return IsJointType(prim, "PhysicsPrismaticJoint");
}

bool IsPhysicsRevoluteJoint(const UsdPrim& prim) {
  return IsJointType(prim, "PhysicsRevoluteJoint");
}

bool IsPhysicsSphericalJoint(const UsdPrim& prim) {
  return IsJointType(prim, "PhysicsSphericalJoint");
}

bool IsPhysicsFixedJoint(const UsdPrim& prim) {
  return IsJointType(prim, "PhysicsFixedJoint");
}

bool IsPhysicsDistanceJoint(const UsdPrim& prim) {
  return IsJointType(prim, "PhysicsDistanceJoint");
}

bool IsPhysicsSliderJoint(const UsdPrim& prim) {
  return IsJointType(prim, "PhysicsSliderJoint");
}

bool IsPhysicsBallJoint(const UsdPrim& prim) {
  return IsJointType(prim, "PhysicsBallJoint");
}

// ============================================================
// Common joint data
// ============================================================

bool GetPhysicsJointData(const Stage& stage, const UsdPrim& prim,
                          PhysicsJointData* out, double time) {
  if (!prim.IsValid() || !out) return false;

  (void)stage;
  (void)time;

  // body0
  {
    const std::vector<Path>* targets =
        prim.GetRelationship("physics:body0");
    if (targets && !targets->empty()) {
      out->body0 = (*targets)[0].str();
      out->hasBody0 = true;
    }
  }

  // body1
  {
    const std::vector<Path>* targets =
        prim.GetRelationship("physics:body1");
    if (targets && !targets->empty()) {
      out->body1 = (*targets)[0].str();
      out->hasBody1 = true;
    }
  }

  // breakForce
  {
    const Value* val = prim.GetPropertyValue("physics:breakForce");
    if (val) {
      const float* f = val->as_float();
      if (f) out->breakForce = *f;
    }
  }

  // breakTorque
  {
    const Value* val = prim.GetPropertyValue("physics:breakTorque");
    if (val) {
      const float* f = val->as_float();
      if (f) out->breakTorque = *f;
    }
  }

  // collisionEnabled
  {
    const Value* val = prim.GetPropertyValue("physics:collisionEnabled");
    if (val) {
      const bool* b = val->as_bool();
      if (b) out->collisionEnabled = *b;
    }
  }

  // enableMotor
  {
    const Value* val = prim.GetPropertyValue("physics:enableMotor");
    if (val) {
      const bool* b = val->as_bool();
      if (b) out->enableMotor = *b;
    }
  }

  // motorTargetVelocity
  {
    const Value* val = prim.GetPropertyValue("physics:motorTargetVelocity");
    if (val) {
      const float* f = val->as_float();
      if (f) out->motorTargetVelocity = *f;
    }
  }

  // motorMaxForce
  {
    const Value* val = prim.GetPropertyValue("physics:motorMaxForce");
    if (val) {
      const float* f = val->as_float();
      if (f) out->motorMaxForce = *f;
    }
  }

  return true;
}

// ============================================================
// PrismaticJoint
// ============================================================

bool GetPhysicsPrismaticJointData(const Stage& stage, const UsdPrim& prim,
                                   PhysicsPrismaticJointData* out,
                                   double time) {
  if (!IsPhysicsPrismaticJoint(prim) || !out) return false;

  if (!GetPhysicsJointData(stage, prim, out, time)) return false;

  // axis
  {
    const Value* val = prim.GetPropertyValue("physics:axis");
    if (val) {
      const std::string* s = val->as_string();
      if (s) {
        // Parse axis from string
        const char* c = s->c_str();
        if (std::strcmp(c, "X") == 0) { out->axis[0] = 1.0f; out->axis[1] = 0.0f; out->axis[2] = 0.0f; }
        else if (std::strcmp(c, "Y") == 0) { out->axis[0] = 0.0f; out->axis[1] = 1.0f; out->axis[2] = 0.0f; }
        else if (std::strcmp(c, "Z") == 0) { out->axis[0] = 0.0f; out->axis[1] = 0.0f; out->axis[2] = 1.0f; }
      }
    }
  }

  // lowerLimit
  {
    const Value* val = prim.GetPropertyValue("physics:lowerLimit");
    if (val) {
      const float* f = val->as_float();
      if (f) out->lowerLimit = *f;
    }
  }

  // upperLimit
  {
    const Value* val = prim.GetPropertyValue("physics:upperLimit");
    if (val) {
      const float* f = val->as_float();
      if (f) out->upperLimit = *f;
    }
  }

  // maxForce
  {
    const Value* val = prim.GetPropertyValue("physics:maxForce");
    if (val) {
      const float* f = val->as_float();
      if (f) out->maxForce = *f;
    }
  }

  return true;
}

// ============================================================
// RevoluteJoint
// ============================================================

bool GetPhysicsRevoluteJointData(const Stage& stage, const UsdPrim& prim,
                                  PhysicsRevoluteJointData* out,
                                  double time) {
  if (!IsPhysicsRevoluteJoint(prim) || !out) return false;

  if (!GetPhysicsJointData(stage, prim, out, time)) return false;

  // axis
  {
    const Value* val = prim.GetPropertyValue("physics:axis");
    if (val) {
      const std::string* s = val->as_string();
      if (s) {
        const char* c = s->c_str();
        if (std::strcmp(c, "X") == 0) { out->axis[0] = 1.0f; out->axis[1] = 0.0f; out->axis[2] = 0.0f; }
        else if (std::strcmp(c, "Y") == 0) { out->axis[0] = 0.0f; out->axis[1] = 1.0f; out->axis[2] = 0.0f; }
        else if (std::strcmp(c, "Z") == 0) { out->axis[0] = 0.0f; out->axis[1] = 0.0f; out->axis[2] = 1.0f; }
      }
    }
  }

  // lowerLimit
  {
    const Value* val = prim.GetPropertyValue("physics:lowerLimit");
    if (val) {
      const float* f = val->as_float();
      if (f) out->lowerLimit = *f;
    }
  }

  // upperLimit
  {
    const Value* val = prim.GetPropertyValue("physics:upperLimit");
    if (val) {
      const float* f = val->as_float();
      if (f) out->upperLimit = *f;
    }
  }

  // maxTorque
  {
    const Value* val = prim.GetPropertyValue("physics:maxTorque");
    if (val) {
      const float* f = val->as_float();
      if (f) out->maxTorque = *f;
    }
  }

  return true;
}

// ============================================================
// SphericalJoint
// ============================================================

bool GetPhysicsSphericalJointData(const Stage& stage, const UsdPrim& prim,
                                   PhysicsSphericalJointData* out,
                                   double time) {
  if (!IsPhysicsSphericalJoint(prim) || !out) return false;

  if (!GetPhysicsJointData(stage, prim, out, time)) return false;

  // coneAngleLimit
  {
    const Value* val = prim.GetPropertyValue("physics:coneAngleLimit");
    if (val) {
      const float* f = val->as_float();
      if (f) out->coneAngleLimit = *f;
    }
  }

  // coneAngleLimitY
  {
    const Value* val = prim.GetPropertyValue("physics:coneAngleLimitY");
    if (val) {
      const float* f = val->as_float();
      if (f) out->coneAngleLimitY = *f;
    }
  }

  // coneAngleLimitZ
  {
    const Value* val = prim.GetPropertyValue("physics:coneAngleLimitZ");
    if (val) {
      const float* f = val->as_float();
      if (f) out->coneAngleLimitZ = *f;
    }
  }

  // maxTorque
  {
    const Value* val = prim.GetPropertyValue("physics:maxTorque");
    if (val) {
      const float* f = val->as_float();
      if (f) out->maxTorque = *f;
    }
  }

  return true;
}

// ============================================================
// FixedJoint
// ============================================================

bool GetPhysicsFixedJointData(const Stage& stage, const UsdPrim& prim,
                               PhysicsFixedJointData* out,
                               double time) {
  if (!IsPhysicsFixedJoint(prim) || !out) return false;
  return GetPhysicsJointData(stage, prim, out, time);
}

// ============================================================
// DistanceJoint
// ============================================================

bool GetPhysicsDistanceJointData(const Stage& stage, const UsdPrim& prim,
                                  PhysicsDistanceJointData* out,
                                  double time) {
  if (!IsPhysicsDistanceJoint(prim) || !out) return false;

  if (!GetPhysicsJointData(stage, prim, out, time)) return false;

  // minDistance
  {
    const Value* val = prim.GetPropertyValue("physics:minDistance");
    if (val) {
      const float* f = val->as_float();
      if (f) out->minDistance = *f;
    }
  }

  // maxDistance
  {
    const Value* val = prim.GetPropertyValue("physics:maxDistance");
    if (val) {
      const float* f = val->as_float();
      if (f) out->maxDistance = *f;
    }
  }

  return true;
}

// ============================================================
// SliderJoint (alias for PrismaticJoint)
// ============================================================

bool GetPhysicsSliderJointData(const Stage& stage, const UsdPrim& prim,
                                PhysicsSliderJointData* out,
                                double time) {
  if (!IsPhysicsSliderJoint(prim) || !out) return false;
  return GetPhysicsPrismaticJointData(stage, prim, out, time);
}

// ============================================================
// BallJoint (alias for SphericalJoint)
// ============================================================

bool GetPhysicsBallJointData(const Stage& stage, const UsdPrim& prim,
                              PhysicsBallJointData* out,
                              double time) {
  if (!IsPhysicsBallJoint(prim) || !out) return false;
  return GetPhysicsSphericalJointData(stage, prim, out, time);
}

} // namespace next
} // namespace tinyusdz
