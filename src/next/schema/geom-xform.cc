// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Xformable detection helpers

#include "geom-xform.hh"

namespace tinyusdz {
namespace next {

bool IsXformable(const UsdPrim& prim) {
  if (!prim.IsValid()) return false;
  const std::string& type = prim.GetTypeName();
  // Most geometry types are xformable
  return type == "Xform" || type == "Mesh" || type == "Sphere" ||
         type == "Cube" || type == "Cylinder" || type == "Cone" ||
         type == "Capsule" || type == "Camera" || type == "Points" ||
         type == "BasisCurves" || type == "NurbsCurves" ||
         type == "SkelRoot" || type == "Skeleton";
}

bool IsXform(const UsdPrim& prim) {
  if (!prim.IsValid()) return false;
  return prim.GetTypeName() == "Xform";
}

}  // namespace next
}  // namespace tinyusdz
