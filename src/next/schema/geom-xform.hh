// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Xformable prim detection
//
// The former UsdGeomXform wrapper class (and its XformOp/XformOpType helpers)
// was unused convenience API and has been removed; only the detection free
// functions remain (the rest of the next schema layer is free-function style).

#pragma once

#include "../stage/stage.hh"

namespace tinyusdz {
namespace next {

/// Check if a prim is xformable (Xform, Mesh, etc.)
bool IsXformable(const UsdPrim& prim);

/// Check if a prim is specifically an Xform
bool IsXform(const UsdPrim& prim);

}  // namespace next
}  // namespace tinyusdz
