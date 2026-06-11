// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdPhysics Applied API Schema Implementation

#include "physics-api.hh"
#include <cstring>

namespace tinyusdz {
namespace next {

// ============================================================
// Helper: check if prim has applied API schema
// ============================================================

static bool HasAPISchema(const UsdPrim& prim, const std::string& name) {
  for (const auto& s : prim.GetMeta().apiSchemas()) {
    if (s == name) return true;
  }
  return false;
}

// ============================================================
// PhysicsRigidBodyAPI
// ============================================================

bool HasPhysicsRigidBodyAPI(const UsdPrim& prim) {
  return HasAPISchema(prim, "PhysicsRigidBodyAPI");
}

bool GetPhysicsRigidBodyData(const Stage& stage, const UsdPrim& prim,
                              PhysicsRigidBodyData* out, double time) {
  if (!HasPhysicsRigidBodyAPI(prim) || !out) return false;

  AttributeEval eval(&stage);
  eval.SetTime(time);

  out->rigidBodyEnabled =
      eval.EvalOr(prim, "physics:rigidBodyEnabled", true);
  // UsdPhysics authors velocity/angularVelocity as single vector3f attributes
  // (not per-component); the previous physics:velocityX/Y/Z names do not exist in
  // the schema, so authored values were silently dropped. EvalFloat3 leaves the
  // {0,0,0} default in place when the attribute is absent.
  eval.EvalFloat3(prim, "physics:velocity", out->velocity);
  eval.EvalFloat3(prim, "physics:angularVelocity", out->angularVelocity);
  out->startsAsleep = eval.EvalOr(prim, "physics:startsAsleep", false);

  return true;
}

// ============================================================
// PhysicsCollisionAPI
// ============================================================

bool HasPhysicsCollisionAPI(const UsdPrim& prim) {
  return HasAPISchema(prim, "PhysicsCollisionAPI");
}

bool GetPhysicsCollisionData(const Stage& stage, const UsdPrim& prim,
                              PhysicsCollisionData* out) {
  if (!HasPhysicsCollisionAPI(prim) || !out) return false;

  (void)stage;

  {
    const Value* val = prim.GetPropertyValue("physics:collisionEnabled");
    if (val) {
      const bool* b = val->as_bool();
      if (b) out->collisionEnabled = *b;
    }
  }

  {
    const std::vector<Path>* targets =
        prim.GetRelationship("physics:simulationOwner");
    if (targets && !targets->empty()) {
      out->simulationOwner = (*targets)[0].str();
    }
  }

  return true;
}

// ============================================================
// PhysicsMaterialAPI
// ============================================================

bool HasPhysicsMaterialAPI(const UsdPrim& prim) {
  return HasAPISchema(prim, "PhysicsMaterialAPI");
}

bool GetPhysicsMaterialData(const Stage& stage, const UsdPrim& prim,
                             PhysicsMaterialData* out) {
  if (!HasPhysicsMaterialAPI(prim) || !out) return false;

  (void)stage;

  {
    const Value* val = prim.GetPropertyValue("physics:staticFriction");
    if (val) {
      const float* f = val->as_float();
      if (f) out->staticFriction = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue("physics:dynamicFriction");
    if (val) {
      const float* f = val->as_float();
      if (f) out->dynamicFriction = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue("physics:restitution");
    if (val) {
      const float* f = val->as_float();
      if (f) out->restitution = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue("physics:density");
    if (val) {
      const float* f = val->as_float();
      if (f) out->density = *f;
    }
  }

  return true;
}

// ============================================================
// PhysicsMeshCollisionAPI
// ============================================================

bool HasPhysicsMeshCollisionAPI(const UsdPrim& prim) {
  return HasAPISchema(prim, "PhysicsMeshCollisionAPI");
}

bool GetPhysicsMeshCollisionData(const UsdPrim& prim,
                                  PhysicsMeshCollisionData* out) {
  if (!HasPhysicsMeshCollisionAPI(prim) || !out) return false;

  {
    const Value* val = prim.GetPropertyValue("physics:approximation");
    if (val) {
      const std::string* s = val->as_string();
      if (s) out->approximation = *s;
      const std::string* tok = val->as_token();
      if (tok) out->approximation = *tok;
    }
  }

  return true;
}

// ============================================================
// PhysicsMassAPI
// ============================================================

bool HasPhysicsMassAPI(const UsdPrim& prim) {
  return HasAPISchema(prim, "PhysicsMassAPI");
}

bool GetPhysicsMassData(const Stage& stage, const UsdPrim& prim,
                         PhysicsMassData* out) {
  if (!HasPhysicsMassAPI(prim) || !out) return false;

  {
    const Value* val = prim.GetPropertyValue("physics:mass");
    if (val) {
      const float* f = val->as_float();
      if (f) out->mass = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue("physics:density");
    if (val) {
      const float* f = val->as_float();
      if (f) out->density = *f;
    }
  }

  // centerOfMass / diagonalInertia (vector3f) and principalAxes (quatf) — these
  // were previously never read, leaving the inertia tensor at its default.
  // EvalFloat3/EvalFloat4 leave the struct defaults in place when absent.
  AttributeEval eval(&stage);
  eval.EvalFloat3(prim, "physics:centerOfMass", out->centerOfMass);
  eval.EvalFloat3(prim, "physics:diagonalInertia", out->diagonalInertia);
  // principalAxes is a quatf; EvalFloat4 fills 4 components in the attribute's
  // stored order.
  eval.EvalFloat4(prim, "physics:principalAxes", out->principalAxes);

  return true;
}

// ============================================================
// PhysicsFilteredPairsAPI
// ============================================================

bool HasPhysicsFilteredPairsAPI(const UsdPrim& prim) {
  return HasAPISchema(prim, "PhysicsFilteredPairsAPI");
}

bool GetPhysicsFilteredPairsData(const UsdPrim& prim,
                                  PhysicsFilteredPairsData* out) {
  if (!HasPhysicsFilteredPairsAPI(prim) || !out) return false;

  {
    const std::vector<Path>* targets =
        prim.GetRelationship("physics:filteredPairs");
    if (targets) {
      out->filteredPairPaths.reserve(targets->size());
      for (const auto& t : *targets) {
        out->filteredPairPaths.push_back(t.str());
      }
    }
  }

  return true;
}

// ============================================================
// PhysicsArticulationRootAPI
// ============================================================

bool HasPhysicsArticulationRootAPI(const UsdPrim& prim) {
  return HasAPISchema(prim, "PhysicsArticulationRootAPI");
}

// ============================================================
// PhysicsDriveAPI (multi-apply)
// ============================================================

bool HasPhysicsDriveAPI(const UsdPrim& prim, const std::string& dof) {
  return HasAPISchema(prim, "PhysicsDriveAPI:" + dof);
}

bool GetPhysicsDriveData(const UsdPrim& prim, const std::string& dof,
                          PhysicsDriveData* out) {
  if (!HasPhysicsDriveAPI(prim, dof) || !out) return false;

  out->dof = dof;

  const std::string prefix = "physics:drive:" + dof + ":";

  {
    const Value* val = prim.GetPropertyValue(prefix + "type");
    if (val) {
      const std::string* s = val->as_string();
      if (s) out->type = *s;
    }
  }

  {
    const Value* val = prim.GetPropertyValue(prefix + "maxForce");
    if (val) {
      const float* f = val->as_float();
      if (f) out->maxForce = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue(prefix + "targetPosition");
    if (val) {
      const float* f = val->as_float();
      if (f) out->targetPosition = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue(prefix + "targetVelocity");
    if (val) {
      const float* f = val->as_float();
      if (f) out->targetVelocity = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue(prefix + "damping");
    if (val) {
      const float* f = val->as_float();
      if (f) out->damping = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue(prefix + "stiffness");
    if (val) {
      const float* f = val->as_float();
      if (f) out->stiffness = *f;
    }
  }

  return true;
}

// ============================================================
// PhysicsLimitAPI (multi-apply)
// ============================================================

bool HasPhysicsLimitAPI(const UsdPrim& prim, const std::string& dof) {
  return HasAPISchema(prim, "PhysicsLimitAPI:" + dof);
}

bool GetPhysicsLimitData(const UsdPrim& prim, const std::string& dof,
                          PhysicsLimitData* out) {
  if (!HasPhysicsLimitAPI(prim, dof) || !out) return false;

  out->dof = dof;

  const std::string prefix = "physics:limit:" + dof + ":";

  {
    const Value* val = prim.GetPropertyValue(prefix + "low");
    if (val) {
      const float* f = val->as_float();
      if (f) out->low = *f;
    }
  }

  {
    const Value* val = prim.GetPropertyValue(prefix + "high");
    if (val) {
      const float* f = val->as_float();
      if (f) out->high = *f;
    }
  }

  return true;
}

} // namespace next
} // namespace tinyusdz
