// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdPhysics Scene Schema Implementation

#include "physics-scene.hh"

namespace tinyusdz {
namespace next {

bool IsPhysicsScene(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "PhysicsScene";
}

bool GetPhysicsSceneData(const Stage& stage, const UsdPrim& prim,
                         PhysicsSceneData* out, double time) {
  if (!IsPhysicsScene(prim) || !out) return false;

  AttributeEval eval(&stage);
  eval.SetTime(time);

  out->gravityMagnitude =
      eval.EvalOr(prim, "physics:gravityMagnitude", -9.81f);

  float dir[3] = {0.0f, -1.0f, 0.0f};
  if (eval.EvalFloat3(prim, "physics:gravityDirection", dir)) {
    out->gravityDirection[0] = dir[0];
    out->gravityDirection[1] = dir[1];
    out->gravityDirection[2] = dir[2];
  }

  return true;
}

} // namespace next
} // namespace tinyusdz
