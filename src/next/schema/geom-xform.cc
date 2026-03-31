// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdGeomXform Schema Implementation

#include "geom-xform.hh"
#include <cstring>
#include <cmath>

namespace tinyusdz {
namespace next {

UsdGeomXform::UsdGeomXform(const UsdPrim& prim) : prim_(prim) {}

std::vector<std::string> UsdGeomXform::GetXformOpOrder() const {
  std::vector<std::string> result;
  if (!IsValid()) return result;

  // Note: Token arrays not yet fully supported in Value class
  // For now, return empty if xformOpOrder exists but can't be parsed
  // The transform ops can still be accessed directly by name

  // Check if property exists
  if (!prim_.HasProperty("xformOpOrder")) {
    return result;
  }

  // TODO: Add token array support to Value class
  return result;
}

XformOpType UsdGeomXform::ParseOpType(const std::string& op_name) const {
  // Parse op name like "xformOp:translate" or "xformOp:rotateX:pivot"
  if (op_name.find("translate") != std::string::npos) return XformOpType::Translate;
  if (op_name.find("rotateX") != std::string::npos) return XformOpType::RotateX;
  if (op_name.find("rotateY") != std::string::npos) return XformOpType::RotateY;
  if (op_name.find("rotateZ") != std::string::npos) return XformOpType::RotateZ;
  if (op_name.find("rotate") != std::string::npos) return XformOpType::Rotate;
  if (op_name.find("scale") != std::string::npos) return XformOpType::Scale;
  if (op_name.find("orient") != std::string::npos) return XformOpType::Orient;
  if (op_name.find("transform") != std::string::npos) return XformOpType::Transform;
  return XformOpType::Unknown;
}

std::vector<XformOp> UsdGeomXform::GetXformOps() const {
  std::vector<XformOp> ops;
  if (!IsValid()) return ops;

  auto order = GetXformOpOrder();
  for (const auto& op_name : order) {
    XformOp op;
    op.is_inverse = (op_name.find("!invert!") == 0);

    std::string actual_name = op.is_inverse ? op_name.substr(8) : op_name;
    op.type = ParseOpType(actual_name);

    // Get the value
    const Value* val = prim_.GetPropertyValue(actual_name);
    if (val) {
      op.value = *val;
    }

    ops.push_back(std::move(op));
  }
  return ops;
}

bool UsdGeomXform::HasAnimatedTransform() const {
  if (!IsValid()) return false;

  // Check common transform properties
  if (prim_.HasTimeSamples("xformOp:translate")) return true;
  if (prim_.HasTimeSamples("xformOp:scale")) return true;
  if (prim_.HasTimeSamples("xformOp:rotateX")) return true;
  if (prim_.HasTimeSamples("xformOp:rotateY")) return true;
  if (prim_.HasTimeSamples("xformOp:rotateZ")) return true;
  if (prim_.HasTimeSamples("xformOp:rotateXYZ")) return true;
  if (prim_.HasTimeSamples("xformOp:orient")) return true;
  if (prim_.HasTimeSamples("xformOp:transform")) return true;

  return false;
}

bool UsdGeomXform::GetTranslation(float* x, float* y, float* z) const {
  if (!IsValid() || !x || !y || !z) return false;

  const Value* val = prim_.GetPropertyValue("xformOp:translate");
  if (!val) return false;

  // Try float3
  const float* f3 = val->as_float3();
  if (f3) {
    *x = f3[0]; *y = f3[1]; *z = f3[2];
    return true;
  }

  // Try double3
  const double* d3 = val->as_double3();
  if (d3) {
    *x = static_cast<float>(d3[0]);
    *y = static_cast<float>(d3[1]);
    *z = static_cast<float>(d3[2]);
    return true;
  }

  return false;
}

bool UsdGeomXform::GetTranslation(double* x, double* y, double* z) const {
  if (!IsValid() || !x || !y || !z) return false;

  const Value* val = prim_.GetPropertyValue("xformOp:translate");
  if (!val) return false;

  const double* d3 = val->as_double3();
  if (d3) {
    *x = d3[0]; *y = d3[1]; *z = d3[2];
    return true;
  }

  const float* f3 = val->as_float3();
  if (f3) {
    *x = f3[0]; *y = f3[1]; *z = f3[2];
    return true;
  }

  return false;
}

bool UsdGeomXform::GetScale(float* x, float* y, float* z) const {
  if (!IsValid() || !x || !y || !z) return false;

  const Value* val = prim_.GetPropertyValue("xformOp:scale");
  if (!val) return false;

  const float* f3 = val->as_float3();
  if (f3) {
    *x = f3[0]; *y = f3[1]; *z = f3[2];
    return true;
  }

  const double* d3 = val->as_double3();
  if (d3) {
    *x = static_cast<float>(d3[0]);
    *y = static_cast<float>(d3[1]);
    *z = static_cast<float>(d3[2]);
    return true;
  }

  return false;
}

bool UsdGeomXform::GetRotation(float* x, float* y, float* z) const {
  if (!IsValid() || !x || !y || !z) return false;

  // Try rotateXYZ first
  const Value* val = prim_.GetPropertyValue("xformOp:rotateXYZ");
  if (val) {
    const float* f3 = val->as_float3();
    if (f3) {
      *x = f3[0]; *y = f3[1]; *z = f3[2];
      return true;
    }
  }

  // Try individual rotations
  *x = *y = *z = 0.0f;
  bool found = false;

  val = prim_.GetPropertyValue("xformOp:rotateX");
  if (val) {
    const float* f = val->as_float();
    if (f) { *x = *f; found = true; }
  }

  val = prim_.GetPropertyValue("xformOp:rotateY");
  if (val) {
    const float* f = val->as_float();
    if (f) { *y = *f; found = true; }
  }

  val = prim_.GetPropertyValue("xformOp:rotateZ");
  if (val) {
    const float* f = val->as_float();
    if (f) { *z = *f; found = true; }
  }

  return found;
}

bool UsdGeomXform::GetOrientation(float* w, float* x, float* y, float* z) const {
  if (!IsValid() || !w || !x || !y || !z) return false;

  const Value* val = prim_.GetPropertyValue("xformOp:orient");
  if (!val) return false;

  // Quaternion stored as (w, x, y, z) or (x, y, z, w) depending on convention
  // USD uses (real, i, j, k) = (w, x, y, z)
  const float* f4 = val->as_float4();
  if (f4) {
    *w = f4[0]; *x = f4[1]; *y = f4[2]; *z = f4[3];
    return true;
  }

  const double* d4 = val->as_double4();
  if (d4) {
    *w = static_cast<float>(d4[0]);
    *x = static_cast<float>(d4[1]);
    *y = static_cast<float>(d4[2]);
    *z = static_cast<float>(d4[3]);
    return true;
  }

  return false;
}

bool UsdGeomXform::ComputeLocalTransform(float* matrix) const {
  if (!IsValid() || !matrix) return false;

  // Initialize to identity
  std::memset(matrix, 0, 16 * sizeof(float));
  matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;

  // Check for full transform matrix first
  const Value* val = prim_.GetPropertyValue("xformOp:transform");
  if (val) {
    const float* m = val->as_matrix4f();
    if (m) {
      std::memcpy(matrix, m, 16 * sizeof(float));
      return true;
    }
    const double* md = val->as_matrix4d();
    if (md) {
      for (int i = 0; i < 16; ++i) {
        matrix[i] = static_cast<float>(md[i]);
      }
      return true;
    }
  }

  // Build transform from individual operations
  // For simplicity, just apply TRS in that order
  float tx = 0, ty = 0, tz = 0;
  float sx = 1, sy = 1, sz = 1;

  GetTranslation(&tx, &ty, &tz);
  GetScale(&sx, &sy, &sz);

  // Simple TRS matrix (no rotation for now - would need proper quaternion/euler handling)
  matrix[0] = sx;
  matrix[5] = sy;
  matrix[10] = sz;
  matrix[12] = tx;
  matrix[13] = ty;
  matrix[14] = tz;

  return true;
}

bool UsdGeomXform::ComputeLocalTransform(double* matrix) const {
  float fmatrix[16];
  if (!ComputeLocalTransform(fmatrix)) return false;

  for (int i = 0; i < 16; ++i) {
    matrix[i] = fmatrix[i];
  }
  return true;
}

bool UsdGeomXform::GetTranslationAtTime(double time, float* x, float* y, float* z) const {
  if (!IsValid() || !x || !y || !z) return false;

  const Value* val = prim_.GetValueAtTime("xformOp:translate", time);
  if (!val) return false;

  const float* f3 = val->as_float3();
  if (f3) {
    *x = f3[0]; *y = f3[1]; *z = f3[2];
    return true;
  }

  const double* d3 = val->as_double3();
  if (d3) {
    *x = static_cast<float>(d3[0]);
    *y = static_cast<float>(d3[1]);
    *z = static_cast<float>(d3[2]);
    return true;
  }

  return false;
}

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
