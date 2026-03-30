// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Scene Access Implementation

#include "scene-access.hh"
#include <cstring>
#include <cmath>

namespace tinyusdz {
namespace tydra {
namespace next {

//
// Prim type checking
//

bool IsMesh(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Mesh";
}

bool IsXform(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Xform";
}

bool IsCamera(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Camera";
}

bool IsMaterial(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Material";
}

bool IsShader(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Shader";
}

bool IsLight(const UsdPrim& prim) {
  if (!prim.IsValid()) return false;
  const std::string& type = prim.GetTypeName();
  return type == "DistantLight" || type == "DomeLight" ||
         type == "RectLight" || type == "DiskLight" ||
         type == "SphereLight" || type == "CylinderLight" ||
         type == "PointLight";
}

bool IsSkeleton(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Skeleton";
}

bool IsSkelRoot(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "SkelRoot";
}

bool IsGeomSubset(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "GeomSubset";
}

bool IsScope(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Scope";
}

LightKind GetLightKind(const UsdPrim& prim) {
  if (!prim.IsValid()) return LightKind::Unknown;
  const std::string& type = prim.GetTypeName();

  if (type == "DistantLight") return LightKind::DistantLight;
  if (type == "DomeLight") return LightKind::DomeLight;
  if (type == "RectLight") return LightKind::RectLight;
  if (type == "DiskLight") return LightKind::DiskLight;
  if (type == "SphereLight") return LightKind::SphereLight;
  if (type == "CylinderLight") return LightKind::CylinderLight;
  if (type == "PointLight") return LightKind::PointLight;

  return LightKind::Unknown;
}

//
// Find prims by type
//

std::vector<UsdPrim> FindMeshes(const Stage& stage) {
  return FindPrimsByType(stage, "Mesh");
}

std::vector<UsdPrim> FindXforms(const Stage& stage) {
  return FindPrimsByType(stage, "Xform");
}

std::vector<UsdPrim> FindCameras(const Stage& stage) {
  return FindPrimsByType(stage, "Camera");
}

std::vector<UsdPrim> FindMaterials(const Stage& stage) {
  return FindPrimsByType(stage, "Material");
}

std::vector<UsdPrim> FindLights(const Stage& stage) {
  return FindPrims(stage, IsLight);
}

std::vector<UsdPrim> FindSkeletons(const Stage& stage) {
  return FindPrimsByType(stage, "Skeleton");
}

std::vector<UsdPrim> FindPrimsByType(const Stage& stage, const std::string& type_name) {
  std::vector<UsdPrim> result;
  stage.Traverse([&](const UsdPrim& prim) {
    if (prim.GetTypeName() == type_name) {
      result.push_back(prim);
    }
    return true;  // Continue traversal
  });
  return result;
}

std::vector<UsdPrim> FindPrims(const Stage& stage, PrimPredicate pred) {
  std::vector<UsdPrim> result;
  stage.Traverse([&](const UsdPrim& prim) {
    if (pred(prim)) {
      result.push_back(prim);
    }
    return true;
  });
  return result;
}

//
// Attribute access helpers
//

const Value* GetAttribute(const UsdPrim& prim, const std::string& name) {
  if (!prim.IsValid()) return nullptr;
  return prim.GetPropertyValue(name);
}

const Value* GetAttributeAtTime(const UsdPrim& prim, const std::string& name, double time) {
  if (!prim.IsValid()) return nullptr;
  return prim.GetValueAtTime(name, time);
}

bool GetFloat(const UsdPrim& prim, const std::string& name, float* out) {
  if (!out) return false;
  const Value* val = GetAttribute(prim, name);
  if (!val) return false;

  const float* f = val->as_float();
  if (f) {
    *out = *f;
    return true;
  }

  // Try double
  const double* d = val->as_double();
  if (d) {
    *out = static_cast<float>(*d);
    return true;
  }

  return false;
}

bool GetFloat3(const UsdPrim& prim, const std::string& name, float* x, float* y, float* z) {
  if (!x || !y || !z) return false;
  const Value* val = GetAttribute(prim, name);
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

bool GetDouble(const UsdPrim& prim, const std::string& name, double* out) {
  if (!out) return false;
  const Value* val = GetAttribute(prim, name);
  if (!val) return false;

  const double* d = val->as_double();
  if (d) {
    *out = *d;
    return true;
  }

  const float* f = val->as_float();
  if (f) {
    *out = *f;
    return true;
  }

  return false;
}

bool GetDouble3(const UsdPrim& prim, const std::string& name, double* x, double* y, double* z) {
  if (!x || !y || !z) return false;
  const Value* val = GetAttribute(prim, name);
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

bool GetInt(const UsdPrim& prim, const std::string& name, int* out) {
  if (!out) return false;
  const Value* val = GetAttribute(prim, name);
  if (!val) return false;

  const int* i = val->as_int();
  if (i) {
    *out = *i;
    return true;
  }

  return false;
}

bool GetBool(const UsdPrim& prim, const std::string& name, bool* out) {
  if (!out) return false;
  const Value* val = GetAttribute(prim, name);
  if (!val) return false;

  const bool* b = val->as_bool();
  if (b) {
    *out = *b;
    return true;
  }

  return false;
}

bool GetString(const UsdPrim& prim, const std::string& name, std::string* out) {
  if (!out) return false;
  const Value* val = GetAttribute(prim, name);
  if (!val) return false;

  const std::string* s = val->as_string();
  if (s) {
    *out = *s;
    return true;
  }

  return false;
}

bool GetToken(const UsdPrim& prim, const std::string& name, std::string* out) {
  if (!out) return false;
  const Value* val = GetAttribute(prim, name);
  if (!val) return false;

  const std::string* t = val->as_token();
  if (t) {
    *out = *t;
    return true;
  }

  return false;
}

bool GetMatrix4(const UsdPrim& prim, const std::string& name, float* matrix16) {
  if (!matrix16) return false;
  const Value* val = GetAttribute(prim, name);
  if (!val) return false;

  const float* m = val->as_matrix4f();
  if (m) {
    std::memcpy(matrix16, m, 16 * sizeof(float));
    return true;
  }

  const double* md = val->as_matrix4d();
  if (md) {
    for (int i = 0; i < 16; ++i) {
      matrix16[i] = static_cast<float>(md[i]);
    }
    return true;
  }

  return false;
}

bool GetMatrix4d(const UsdPrim& prim, const std::string& name, double* matrix16) {
  if (!matrix16) return false;
  const Value* val = GetAttribute(prim, name);
  if (!val) return false;

  const double* m = val->as_matrix4d();
  if (m) {
    std::memcpy(matrix16, m, 16 * sizeof(double));
    return true;
  }

  const float* mf = val->as_matrix4f();
  if (mf) {
    for (int i = 0; i < 16; ++i) {
      matrix16[i] = mf[i];
    }
    return true;
  }

  return false;
}

std::vector<float> GetFloatArray(const UsdPrim& prim, const std::string& name) {
  std::vector<float> result;
  const Value* val = GetAttribute(prim, name);
  if (!val) return result;

  const std::vector<float>* arr = val->as_float_array();
  if (arr) {
    result = *arr;
  }
  return result;
}

std::vector<int32_t> GetIntArray(const UsdPrim& prim, const std::string& name) {
  std::vector<int32_t> result;
  const Value* val = GetAttribute(prim, name);
  if (!val) return result;

  const std::vector<int32_t>* arr = val->as_int_array();
  if (arr) {
    result = *arr;
  }
  return result;
}

//
// Relationship access
//

std::vector<std::string> GetRelationshipTargets(const UsdPrim& prim, const std::string& rel_name) {
  std::vector<std::string> result;
  if (!prim.IsValid()) return result;

  // Check for relationship in prim spec
  // Relationships are stored in the prim spec directly
  // TODO: Implement proper relationship access in next::UsdPrim

  return result;
}

std::string GetBoundMaterial(const UsdPrim& prim) {
  if (!prim.IsValid()) return "";

  // Try material:binding relationship
  auto targets = GetRelationshipTargets(prim, "material:binding");
  if (!targets.empty()) {
    return targets[0];
  }

  return "";
}

std::string GetBoundSkeleton(const UsdPrim& prim) {
  if (!prim.IsValid()) return "";

  auto targets = GetRelationshipTargets(prim, "skel:skeleton");
  if (!targets.empty()) {
    return targets[0];
  }

  return "";
}

//
// Transform operations
//

namespace {

void SetIdentity(float* m) {
  std::memset(m, 0, 16 * sizeof(float));
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void SetIdentity(double* m) {
  std::memset(m, 0, 16 * sizeof(double));
  m[0] = m[5] = m[10] = m[15] = 1.0;
}

void MatrixMultiply(float* result, const float* a, const float* b) {
  float temp[16];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      temp[i * 4 + j] =
        a[i * 4 + 0] * b[0 * 4 + j] +
        a[i * 4 + 1] * b[1 * 4 + j] +
        a[i * 4 + 2] * b[2 * 4 + j] +
        a[i * 4 + 3] * b[3 * 4 + j];
    }
  }
  std::memcpy(result, temp, 16 * sizeof(float));
}

void MatrixMultiply(double* result, const double* a, const double* b) {
  double temp[16];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      temp[i * 4 + j] =
        a[i * 4 + 0] * b[0 * 4 + j] +
        a[i * 4 + 1] * b[1 * 4 + j] +
        a[i * 4 + 2] * b[2 * 4 + j] +
        a[i * 4 + 3] * b[3 * 4 + j];
    }
  }
  std::memcpy(result, temp, 16 * sizeof(double));
}

void MakeTranslation(float* m, float x, float y, float z) {
  SetIdentity(m);
  m[12] = x; m[13] = y; m[14] = z;
}

void MakeScale(float* m, float x, float y, float z) {
  SetIdentity(m);
  m[0] = x; m[5] = y; m[10] = z;
}

void MakeRotationX(float* m, float radians) {
  SetIdentity(m);
  float c = std::cos(radians);
  float s = std::sin(radians);
  m[5] = c; m[6] = s;
  m[9] = -s; m[10] = c;
}

void MakeRotationY(float* m, float radians) {
  SetIdentity(m);
  float c = std::cos(radians);
  float s = std::sin(radians);
  m[0] = c; m[2] = -s;
  m[8] = s; m[10] = c;
}

void MakeRotationZ(float* m, float radians) {
  SetIdentity(m);
  float c = std::cos(radians);
  float s = std::sin(radians);
  m[0] = c; m[1] = s;
  m[4] = -s; m[5] = c;
}

}  // namespace

bool ComputeLocalTransform(const UsdPrim& prim, float* matrix16, double time) {
  if (!prim.IsValid() || !matrix16) return false;

  SetIdentity(matrix16);

  // Check for xformOp:transform first (full matrix)
  const Value* xform = prim.GetPropertyValue("xformOp:transform");
  if (xform) {
    const float* m = xform->as_matrix4f();
    if (m) {
      std::memcpy(matrix16, m, 16 * sizeof(float));
      return true;
    }
    const double* md = xform->as_matrix4d();
    if (md) {
      for (int i = 0; i < 16; ++i) {
        matrix16[i] = static_cast<float>(md[i]);
      }
      return true;
    }
  }

  // Build TRS matrix
  float temp[16];

  // Translation
  float tx = 0, ty = 0, tz = 0;
  const Value* translate = prim.GetPropertyValue("xformOp:translate");
  if (translate) {
    const float* t = translate->as_float3();
    if (t) { tx = t[0]; ty = t[1]; tz = t[2]; }
    else {
      const double* td = translate->as_double3();
      if (td) {
        tx = static_cast<float>(td[0]);
        ty = static_cast<float>(td[1]);
        tz = static_cast<float>(td[2]);
      }
    }
  }

  // Scale
  float sx = 1, sy = 1, sz = 1;
  const Value* scale = prim.GetPropertyValue("xformOp:scale");
  if (scale) {
    const float* s = scale->as_float3();
    if (s) { sx = s[0]; sy = s[1]; sz = s[2]; }
  }

  // Rotation (check various forms)
  float rx = 0, ry = 0, rz = 0;

  const Value* rotXYZ = prim.GetPropertyValue("xformOp:rotateXYZ");
  if (rotXYZ) {
    const float* r = rotXYZ->as_float3();
    if (r) { rx = r[0]; ry = r[1]; rz = r[2]; }
  } else {
    // Check individual rotations
    const Value* rotX = prim.GetPropertyValue("xformOp:rotateX");
    if (rotX) {
      const float* r = rotX->as_float();
      if (r) rx = *r;
    }
    const Value* rotY = prim.GetPropertyValue("xformOp:rotateY");
    if (rotY) {
      const float* r = rotY->as_float();
      if (r) ry = *r;
    }
    const Value* rotZ = prim.GetPropertyValue("xformOp:rotateZ");
    if (rotZ) {
      const float* r = rotZ->as_float();
      if (r) rz = *r;
    }
  }

  // Convert degrees to radians
  const float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
  rx *= DEG_TO_RAD;
  ry *= DEG_TO_RAD;
  rz *= DEG_TO_RAD;

  // Build matrix: T * R * S
  float matT[16], matRx[16], matRy[16], matRz[16], matS[16];

  MakeTranslation(matT, tx, ty, tz);
  MakeRotationX(matRx, rx);
  MakeRotationY(matRy, ry);
  MakeRotationZ(matRz, rz);
  MakeScale(matS, sx, sy, sz);

  // Combine: T * Rz * Ry * Rx * S (typical order)
  MatrixMultiply(temp, matRy, matRx);
  MatrixMultiply(matrix16, matRz, temp);
  MatrixMultiply(temp, matrix16, matS);
  MatrixMultiply(matrix16, matT, temp);

  return true;
}

bool ComputeLocalTransform(const UsdPrim& prim, double* matrix16, double time) {
  float fmatrix[16];
  if (!ComputeLocalTransform(prim, fmatrix, time)) return false;

  for (int i = 0; i < 16; ++i) {
    matrix16[i] = fmatrix[i];
  }
  return true;
}

bool ComputeWorldTransform(const Stage& stage, const UsdPrim& prim, float* matrix16, double time) {
  if (!prim.IsValid() || !matrix16) return false;

  SetIdentity(matrix16);

  // Build path from root to this prim
  std::vector<UsdPrim> ancestors;
  std::string path = prim.GetPath().str();

  // Walk up the hierarchy
  while (!path.empty() && path != "/") {
    UsdPrim p = stage.GetPrimAtPath(path);
    if (p.IsValid()) {
      ancestors.push_back(p);
    }

    // Get parent path
    size_t last_slash = path.rfind('/');
    if (last_slash == 0) {
      path = "/";
    } else if (last_slash != std::string::npos) {
      path = path.substr(0, last_slash);
    } else {
      break;
    }
  }

  // Apply transforms from root to leaf
  float local[16];
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
    if (HasResetXformStack(*it)) {
      SetIdentity(matrix16);
    }

    if (ComputeLocalTransform(*it, local, time)) {
      float temp[16];
      MatrixMultiply(temp, matrix16, local);
      std::memcpy(matrix16, temp, 16 * sizeof(float));
    }
  }

  return true;
}

bool ComputeWorldTransform(const Stage& stage, const UsdPrim& prim, double* matrix16, double time) {
  float fmatrix[16];
  if (!ComputeWorldTransform(stage, prim, fmatrix, time)) return false;

  for (int i = 0; i < 16; ++i) {
    matrix16[i] = fmatrix[i];
  }
  return true;
}

bool HasResetXformStack(const UsdPrim& prim) {
  // Check for xformOpOrder containing "!resetXformStack!"
  // For now, check for the property directly
  return prim.HasProperty("!resetXformStack!");
}

//
// Hierarchy access
//

UsdPrim GetParent(const Stage& stage, const UsdPrim& prim) {
  if (!prim.IsValid()) return UsdPrim();

  std::string path = prim.GetPath().str();
  std::string parent_path = GetParentPath(path);

  if (parent_path.empty() || parent_path == "/") {
    return UsdPrim();
  }

  return stage.GetPrimAtPath(parent_path);
}

std::vector<UsdPrim> GetChildren(const UsdPrim& prim) {
  if (!prim.IsValid()) return {};
  return prim.GetChildren();
}

std::vector<UsdPrim> GetDescendants(const UsdPrim& prim) {
  std::vector<UsdPrim> result;
  if (!prim.IsValid()) return result;

  // Recursive traversal
  std::function<void(const UsdPrim&)> traverse = [&](const UsdPrim& p) {
    auto children = p.GetChildren();
    for (const auto& child : children) {
      result.push_back(child);
      traverse(child);
    }
  };

  traverse(prim);
  return result;
}

std::string GetPrimName(const UsdPrim& prim) {
  if (!prim.IsValid()) return "";
  return prim.GetName();
}

std::string GetParentPath(const std::string& path) {
  if (path.empty() || path == "/") return "";

  size_t last_slash = path.rfind('/');
  if (last_slash == 0) return "/";
  if (last_slash == std::string::npos) return "";

  return path.substr(0, last_slash);
}

//
// GeomSubset access
//

std::vector<GeomSubset> GetGeomSubsets(const UsdPrim& mesh_prim) {
  std::vector<GeomSubset> result;
  if (!mesh_prim.IsValid()) return result;

  auto children = mesh_prim.GetChildren();
  for (const auto& child : children) {
    if (IsGeomSubset(child)) {
      GeomSubset gs;
      gs.name = child.GetName();
      gs.path = child.GetPath().str();

      GetToken(child, "familyName", &gs.family_name);

      // Get indices
      gs.indices = GetIntArray(child, "indices");

      // Get material binding
      gs.material_path = GetBoundMaterial(child);

      result.push_back(std::move(gs));
    }
  }

  return result;
}

//
// Primvar access
//

std::vector<Primvar> GetPrimvars(const UsdPrim& prim) {
  std::vector<Primvar> result;
  if (!prim.IsValid()) return result;

  // Get all properties that start with "primvars:"
  auto prop_names = prim.GetPropertyNames();
  for (const auto& name : prop_names) {
    if (name.find("primvars:") == 0 && name.find(":indices") == std::string::npos) {
      Primvar pv;
      pv.name = name.substr(9);  // Remove "primvars:" prefix
      pv.value = GetAttribute(prim, name);
      if (pv.value) {
        pv.type_id = pv.value->type_id();

        // Get interpolation
        std::string interp_attr = name + ":interpolation";
        GetToken(prim, interp_attr, &pv.interpolation);
        if (pv.interpolation.empty()) {
          pv.interpolation = "vertex";  // Default
        }

        // Get indices if present
        std::string indices_attr = name + ":indices";
        pv.indices = GetIntArray(prim, indices_attr);

        result.push_back(std::move(pv));
      }
    }
  }

  return result;
}

Primvar GetPrimvar(const UsdPrim& prim, const std::string& name) {
  Primvar pv;
  pv.name = name;

  std::string full_name = "primvars:" + name;
  pv.value = GetAttribute(prim, full_name);

  if (pv.value) {
    pv.type_id = pv.value->type_id();

    std::string interp_attr = full_name + ":interpolation";
    GetToken(prim, interp_attr, &pv.interpolation);
    if (pv.interpolation.empty()) {
      pv.interpolation = "vertex";
    }

    std::string indices_attr = full_name + ":indices";
    pv.indices = GetIntArray(prim, indices_attr);
  }

  return pv;
}

//
// Blend shape access
//

std::vector<BlendShapeInfo> GetBlendShapes(const UsdPrim& mesh_prim) {
  std::vector<BlendShapeInfo> result;
  // TODO: Implement blend shape extraction from SkelBindingAPI
  return result;
}

//
// Skeleton access
//

bool GetSkeletonInfo(const UsdPrim& skel_prim, SkeletonInfo* out) {
  if (!out || !skel_prim.IsValid() || !IsSkeleton(skel_prim)) {
    return false;
  }

  out->name = skel_prim.GetName();
  out->path = skel_prim.GetPath().str();

  // TODO: Implement full skeleton extraction
  // joints, jointOrder, bindTransforms, restTransforms

  return true;
}

//
// Skin binding access
//

bool GetSkinBinding(const UsdPrim& mesh_prim, SkinBindingInfo* out) {
  if (!out || !mesh_prim.IsValid()) return false;

  // TODO: Implement skin binding extraction
  // primvars:skel:jointIndices, primvars:skel:jointWeights
  // skel:skeleton relationship

  return false;
}

//
// Connection following
//

const Value* ResolveConnection(const Stage& stage, const UsdPrim& prim, const std::string& attr_name) {
  if (!prim.IsValid()) return nullptr;

  // First check if there's a direct value
  const Value* val = GetAttribute(prim, attr_name);
  if (val) {
    return val;
  }

  // TODO: Follow connection chain
  // This requires parsing the connection target path and resolving it

  (void)stage;  // Suppress unused warning
  return nullptr;
}

std::string GetConnectionPath(const UsdPrim& prim, const std::string& attr_name) {
  if (!prim.IsValid()) return "";

  // Check for connection attribute (name.connect)
  std::string connect_name = attr_name + ".connect";
  const Value* val = GetAttribute(prim, connect_name);
  if (val) {
    const std::string* path = val->as_string();
    if (path) return *path;
  }

  return "";
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
