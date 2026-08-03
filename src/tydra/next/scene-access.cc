// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tydra Next - Scene Access Implementation

#include "scene-access.hh"
#include "next/layer/prim-spec.hh"
#include "next/prim/path.hh"
#include <cstring>
#include <cmath>

namespace tinyusdz {
namespace tydra {
namespace next {

using ::tinyusdz::next::Path;
using ::tinyusdz::next::PrimSpec;
using ::tinyusdz::next::PropMeta;

namespace {

const ::tinyusdz::next::PropNameId& kXformOpOrder() {
  static const ::tinyusdz::next::PropNameId id =
      ::tinyusdz::next::GetPropNameTable().intern("xformOpOrder");
  return id;
}

}  // namespace

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
         type == "DomeLight_1" || type == "RectLight" || type == "DiskLight" ||
         type == "SphereLight" || type == "CylinderLight" ||
         type == "PointLight" || type == "GeometryLight" ||
         type == "PortalLight" || type == "PluginLight" ||
         type == "LightFilter" || type == "PluginLightFilter";
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
  if (type == "DomeLight" || type == "DomeLight_1") return LightKind::DomeLight;
  if (type == "RectLight") return LightKind::RectLight;
  if (type == "DiskLight") return LightKind::DiskLight;
  if (type == "SphereLight") return LightKind::SphereLight;
  if (type == "CylinderLight") return LightKind::CylinderLight;
  if (type == "PointLight") return LightKind::PointLight;
  if (type == "GeometryLight") return LightKind::GeometryLight;
  if (type == "PortalLight") return LightKind::PortalLight;
  if (type == "PluginLight") return LightKind::PluginLight;
  if (type == "LightFilter") return LightKind::LightFilter;
  if (type == "PluginLightFilter") return LightKind::PluginLightFilter;

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
  auto name_id = tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) return nullptr;
  return prim.GetPropertyValue(name_id);
}

const Value* GetAttributeAtTime(const UsdPrim& prim, const std::string& name, double time) {
  if (!prim.IsValid()) return nullptr;
  auto name_id = tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) return nullptr;
  return prim.GetValueAtTime(name_id, time);
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

std::vector<std::string> GetTokenArray(const UsdPrim& prim,
                                       const std::string& name) {
  std::vector<std::string> result;
  const Value* val = GetAttribute(prim, name);
  if (!val) return result;
  if (const std::vector<std::string>* arr = val->as_token_array()) {
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

  if (const std::vector<Path>* targets = prim.GetRelationship(rel_name)) {
    result.reserve(targets->size());
    for (const Path& p : *targets) {
      result.push_back(p.str());
    }
  }
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

void SetIdentity(double* m) {
  std::memset(m, 0, 16 * sizeof(double));
  m[0] = m[5] = m[10] = m[15] = 1.0;
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

// --- Bit-exact xform evaluation -------------------------------------------
//
// To produce world/local matrices that are byte-identical to the legacy
// tinyusdz evaluator (Xformable::EvaluateXformOps in src/xform.cc), the helpers
// below replicate its exact arithmetic: the `sin_pi`/`cos_pi` reduced-argument
// trig (NOT std::sin/cos(deg * pi/180)), the per-op matrix layouts, and the
// `cm = op * cm` / `world = local * parent` (row-vector) multiply order.

constexpr double kPi = 3.141592653589793238462643383279502884e+00;

inline bool IsCloseD(double a, double b, double eps) {
  double d = a - b;
  if (std::fabs(d) <= eps) return true;
  return std::fabs(d) <= (eps * std::fmax(std::fabs(a), std::fabs(b)));
}

// cos(pi*x) — verbatim port of tinyusdz::math::cos_pi_imp<double>.
inline double CosPi(double x) {
  bool invert = false;
  if (std::fabs(x) < 0.25) return std::cos(kPi * x);
  if (x < 0) x = -x;
  double rem = std::floor(x);
  {
    double r = std::trunc(rem);
    int ival = static_cast<int>(r);
    if (ival & 1) invert = !invert;
  }
  rem = x - rem;
  if (rem > 0.5) {
    rem = 1 - rem;
    invert = !invert;
  }
  if (IsCloseD(rem, 0.5, 0.0)) return 0.0;
  if (rem > 0.25) {
    rem = 0.5 - rem;
    rem = std::sin(kPi * rem);
  } else {
    rem = std::cos(kPi * rem);
  }
  return invert ? -rem : rem;
}

// sin(pi*x) — verbatim port of tinyusdz::math::sin_pi_imp<double>.
inline double SinPi(double x) {
  if (x < 0) return -SinPi(-x);
  bool invert = false;
  if (x < 0.5) {
    if (IsCloseD(x, 0.25, 0.0)) return std::cos(kPi * x);
    return std::sin(kPi * x);
  }
  if (x < 1) {
    invert = true;
    x = -x;
  } else {
    invert = false;
  }
  double rem = std::floor(x);
  {
    double r = std::trunc(rem);
    int ival = static_cast<int>(r);
    if (ival & 1) invert = !invert;
  }
  rem = x - rem;
  if (rem > 0.5) rem = 1 - rem;
  if (IsCloseD(rem, 0.5, 0.0)) return invert ? -1.0 : 1.0;
  if (IsCloseD(rem, 0.25, 0.0)) {
    rem = std::cos(kPi * rem);
  } else {
    rem = std::sin(kPi * rem);
  }
  return invert ? -rem : rem;
}

void MakeTranslationD(double* m, double x, double y, double z) {
  SetIdentity(m);
  m[12] = x; m[13] = y; m[14] = z;  // matrix4d.m[3][0..2]
}

void MakeScaleD(double* m, double x, double y, double z) {
  SetIdentity(m);
  m[0] = x; m[5] = y; m[10] = z;
}

// angle in degrees; layout matches src/xform.cc XformEvaluator::RotateX/Y/Z.
void MakeRotXD(double* m, double deg) {
  SetIdentity(m);
  double c = CosPi(deg / 180.0), s = SinPi(deg / 180.0);
  m[5] = c; m[6] = s; m[9] = -s; m[10] = c;
}
void MakeRotYD(double* m, double deg) {
  SetIdentity(m);
  double c = CosPi(deg / 180.0), s = SinPi(deg / 180.0);
  m[0] = c; m[2] = -s; m[8] = s; m[10] = c;
}
void MakeRotZD(double* m, double deg) {
  SetIdentity(m);
  double c = CosPi(deg / 180.0), s = SinPi(deg / 180.0);
  m[0] = c; m[1] = s; m[4] = -s; m[5] = c;
}

// dst = a * b (row-vector convention), via the existing bit-compatible multiply.
void MatMulD(double* dst, const double* a, const double* b) {
  MatrixMultiply(dst, a, b);
}

// Read a property either at the default value (NaN time) or held at a specific
// time sample. A NaN time keeps the exact default-value path (byte-identical to
// the previous, time-unaware behaviour).
const Value* PropAtTime(const UsdPrim& prim, const std::string& name,
                        double time) {
  auto name_id = tinyusdz::next::GetPropNameTable().find(name);
  if (!name_id.is_valid()) return nullptr;
  if (std::isnan(time)) return prim.GetPropertyValue(name_id);
  // Linear interpolation between samples (pxr semantics); held/default
  // fallback. Scratch is per-thread; callers consume the pointer before the
  // next PropAtTime call (single-live-pointer pattern in this TU).
  static thread_local Value scratch;
  Value v = prim.GetInterpolatedValue(name_id, time);
  if (!v.is_empty()) {
    scratch = std::move(v);
    return &scratch;
  }
  return prim.GetValueAtTime(name_id, time);
}

// Read a 3-component op value (translate/scale/rotate) as double, trying
// float3 then double3 (matches the legacy evaluator's exact-type promotion),
// then the converting read for authored half3 (raw half-bit lanes).
bool ReadVec3D(const UsdPrim& prim, const std::string& name, double v[3],
               double time) {
  const Value* val = PropAtTime(prim, name, time);
  if (!val) return false;
  if (const float* f = val->as_float3()) {
    v[0] = double(f[0]); v[1] = double(f[1]); v[2] = double(f[2]);
    return true;
  }
  if (const double* d = val->as_double3()) {
    v[0] = d[0]; v[1] = d[1]; v[2] = d[2];
    return true;
  }
  float h[3];
  if (val->to_float3(h)) {
    v[0] = double(h[0]); v[1] = double(h[1]); v[2] = double(h[2]);
    return true;
  }
  return false;
}

bool ReadFloat1D(const UsdPrim& prim, const std::string& name, double* out,
                 double time) {
  const Value* val = PropAtTime(prim, name, time);
  if (!val) return false;
  if (const float* f = val->as_float()) { *out = double(*f); return true; }
  if (const double* d = val->as_double()) { *out = *d; return true; }
  float h = 0.0f;
  if (val->to_float(&h)) { *out = double(h); return true; }
  return false;
}

}  // namespace

namespace {

// Build a single-axis rotation (degrees) for axis 'X'/'Y'/'Z'.
void MakeRotAxisD(double* m, char axis, double deg) {
  switch (axis) {
    case 'X': MakeRotXD(m, deg); break;
    case 'Y': MakeRotYD(m, deg); break;
    case 'Z': MakeRotZD(m, deg); break;
    default: SetIdentity(m); break;
  }
}

// Quaternion (w,x,y,z) -> row-vector rotation matrix, matching
// tinyusdz::to_matrix3x3(quatd) embedded in a 4x4.
void MakeOrientD(double* m, double w, double x, double y, double z) {
  SetIdentity(m);
  m[0] = 1.0 - 2.0 * (y * y + z * z);
  m[1] = 2.0 * (x * y + z * w);
  m[2] = 2.0 * (x * z - y * w);
  m[4] = 2.0 * (x * y - z * w);
  m[5] = 1.0 - 2.0 * (x * x + z * z);
  m[6] = 2.0 * (y * z + x * w);
  m[8] = 2.0 * (x * z + y * w);
  m[9] = 2.0 * (y * z - x * w);
  m[10] = 1.0 - 2.0 * (x * x + y * y);
}

void Invert4x4D(const double* a, double* out) {
  double inv[16];
  inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
  inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
  inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
  inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
  inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
  inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
  inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
  inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
  inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
  inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
  inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
  inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
  inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
  inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
  inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
  inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
  double det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
  if (det == 0.0) { SetIdentity(out); return; }
  double idet = 1.0 / det;
  for (int i = 0; i < 16; ++i) out[i] = inv[i] * idet;
}

// Strip "xformOp:" prefix and any ":suffix"; returns the bare op name
// (e.g. "rotateXYZ"). Returns empty for non-xformOp tokens.
std::string XformOpName(const std::string& tok) {
  const std::string kPfx = "xformOp:";
  if (tok.rfind(kPfx, 0) != 0) return std::string();
  std::string rest = tok.substr(kPfx.size());
  size_t colon = rest.find(':');
  if (colon != std::string::npos) rest = rest.substr(0, colon);
  return rest;
}

// Bit-exact local matrix from authored xformOpOrder, replicating
// tinyusdz::Xformable::EvaluateXformOps (row-vector, cm = op * cm). Sets
// *reset when the op list begins with "!resetXformStack!".
bool EvalLocalXformD(const UsdPrim& prim, double* out, bool* reset, double time) {
  if (reset) *reset = false;
  SetIdentity(out);
  if (!prim.IsValid()) return false;

  // xformOpOrder is uniform (not time-sampled) -> default value.
  const Value* orderv = prim.GetPropertyValue(kXformOpOrder());
  const std::vector<std::string>* order =
      orderv ? orderv->as_token_array() : nullptr;
  if (!order || order->empty()) return true;  // no ops -> identity

  for (size_t i = 0; i < order->size(); ++i) {
    std::string tok = (*order)[i];
    bool inverted = false;
    if (tok.rfind("!invert!", 0) == 0) {
      inverted = true;
      tok = tok.substr(8);
    }
    if (tok == "!resetXformStack!") {
      if (i == 0 && reset) *reset = true;
      continue;
    }
    const std::string op = XformOpName(tok);
    if (op.empty()) continue;

    double m[16];
    SetIdentity(m);

    if (op == "transform") {
      double mm[16];
      bool got = false;
      if (const Value* val = PropAtTime(prim, tok, time)) {
        if (const double* md = val->as_matrix4d()) {
          std::memcpy(mm, md, 16 * sizeof(double));
          got = true;
        } else if (const float* mf = val->as_matrix4f()) {
          for (int e = 0; e < 16; ++e) mm[e] = double(mf[e]);
          got = true;
        }
      }
      if (got) {
        if (inverted) Invert4x4D(mm, m);
        else std::memcpy(m, mm, 16 * sizeof(double));
      }
    } else if (op == "translate") {
      double v[3] = {0, 0, 0};
      ReadVec3D(prim, tok, v, time);
      if (inverted) { v[0] = -v[0]; v[1] = -v[1]; v[2] = -v[2]; }
      MakeTranslationD(m, v[0], v[1], v[2]);
    } else if (op == "scale") {
      double v[3] = {1, 1, 1};
      ReadVec3D(prim, tok, v, time);
      if (inverted) { v[0] = 1.0 / v[0]; v[1] = 1.0 / v[1]; v[2] = 1.0 / v[2]; }
      MakeScaleD(m, v[0], v[1], v[2]);
    } else if (op == "orient") {
      const Value* val = PropAtTime(prim, tok, time);
      double q[4] = {1, 0, 0, 0};  // w,x,y,z
      if (val) {
        float h[4];
        if (const float* f = val->as_float4()) {
          q[0] = f[0]; q[1] = f[1]; q[2] = f[2]; q[3] = f[3];
        } else if (const double* d = val->as_double4()) {
          q[0] = d[0]; q[1] = d[1]; q[2] = d[2]; q[3] = d[3];
        } else if (val->to_float4(h)) {  // authored quath (half-bit lanes)
          q[0] = h[0]; q[1] = h[1]; q[2] = h[2]; q[3] = h[3];
        }
      }
      if (inverted) { q[1] = -q[1]; q[2] = -q[2]; q[3] = -q[3]; }
      MakeOrientD(m, q[0], q[1], q[2], q[3]);
    } else if (op == "rotateX" || op == "rotateY" || op == "rotateZ") {
      double a = 0;
      ReadFloat1D(prim, tok, &a, time);
      if (inverted) a = -a;
      MakeRotAxisD(m, op[6], a);  // 'X'/'Y'/'Z'
    } else if (op.size() == 9 && op.rfind("rotate", 0) == 0) {
      // rotateXYZ / rotateZYX / ... : product of single-axis rotations in the
      // letter order (m = m * R_axis), exactly as XformEvaluator does.
      double v[3] = {0, 0, 0};
      ReadVec3D(prim, tok, v, time);
      const char ax[3] = {op[6], op[7], op[8]};
      // map axis letter -> angle component (X->v[0], Y->v[1], Z->v[2]).
      auto angleFor = [&](char c) -> double {
        return c == 'X' ? v[0] : (c == 'Y' ? v[1] : v[2]);
      };
      SetIdentity(m);
      if (!inverted) {
        for (int k = 0; k < 3; ++k) {
          double r[16], tmp[16];
          MakeRotAxisD(r, ax[k], angleFor(ax[k]));
          MatMulD(tmp, m, r);
          std::memcpy(m, tmp, 16 * sizeof(double));
        }
      } else {
        for (int k = 2; k >= 0; --k) {
          double r[16], tmp[16];
          MakeRotAxisD(r, ax[k], -angleFor(ax[k]));
          MatMulD(tmp, m, r);
          std::memcpy(m, tmp, 16 * sizeof(double));
        }
      }
    } else {
      continue;  // unknown op -> identity contribution
    }

    // cm = op * cm
    double tmp[16];
    MatMulD(tmp, m, out);
    std::memcpy(out, tmp, 16 * sizeof(double));
  }
  return true;
}

}  // namespace

bool ComputeLocalTransform(const UsdPrim& prim, double* matrix16, double time) {
  if (!prim.IsValid() || !matrix16) return false;
  bool reset = false;
  return EvalLocalXformD(prim, matrix16, &reset, time);
}

bool ComputeLocalTransform(const UsdPrim& prim, float* matrix16, double time) {
  if (!matrix16) return false;
  double dmat[16];
  if (!ComputeLocalTransform(prim, dmat, time)) return false;
  for (int i = 0; i < 16; ++i) matrix16[i] = static_cast<float>(dmat[i]);
  return true;
}

bool ComputeWorldTransform(const Stage& stage, const UsdPrim& prim, double* matrix16, double time) {
  if (!prim.IsValid() || !matrix16) return false;
  SetIdentity(matrix16);

  // Collect this prim and its ancestors (leaf -> root).
  std::vector<UsdPrim> chain;
  std::string path = prim.GetPath().str();
  while (!path.empty() && path != "/") {
    UsdPrim p = stage.GetPrimAtPath(path);
    if (p.IsValid()) chain.push_back(p);
    size_t last_slash = path.rfind('/');
    if (last_slash == 0) path = "/";
    else if (last_slash != std::string::npos) path = path.substr(0, last_slash);
    else break;
  }

  // world = L_leaf * L_parent * ... * L_root  (row-vector: world = local *
  // parent_world). resetXformStack on a prim ignores its ancestors' stack.
  for (auto it = chain.begin(); it != chain.end(); ++it) {
    double local[16];
    bool reset = false;
    EvalLocalXformD(*it, local, &reset, time);
    double tmp[16];
    MatMulD(tmp, matrix16, local);
    std::memcpy(matrix16, tmp, 16 * sizeof(double));
    if (reset) break;  // this prim resets the parent stack
  }
  return true;
}

bool ComputeWorldTransform(const Stage& stage, const UsdPrim& prim, float* matrix16, double time) {
  if (!matrix16) return false;
  double dmat[16];
  if (!ComputeWorldTransform(stage, prim, dmat, time)) return false;
  for (int i = 0; i < 16; ++i) matrix16[i] = static_cast<float>(dmat[i]);
  return true;
}

bool HasResetXformStack(const UsdPrim& prim) {
  if (!prim.IsValid()) return false;
  const Value* orderv = prim.GetPropertyValue(kXformOpOrder());
  const std::vector<std::string>* order =
      orderv ? orderv->as_token_array() : nullptr;
  if (!order || order->empty()) return false;
  return (*order)[0] == "!resetXformStack!";
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

  // Iterative pre-order DFS. Stages parsed from USDA have a depth limit, but
  // callers can build Layers programmatically; recursing here made a valid
  // deep hierarchy exhaust the native/WASM stack.
  std::vector<UsdPrim> stack;
  std::vector<UsdPrim> children = prim.GetChildren();
  stack.reserve(children.size());
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    stack.push_back(*it);
  }

  while (!stack.empty()) {
    UsdPrim current = stack.back();
    stack.pop_back();
    if (!current.IsValid()) continue;

    result.push_back(current);

    children = current.GetChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      stack.push_back(*it);
    }
  }
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

        // Get interpolation: authored property metadata first (the usda/crate
        // readers store it in PropMeta), then the legacy attribute form.
        if (const PrimSpec* spec = prim.GetPrimSpec()) {
          if (const PropMeta* pm = spec->property_meta(name)) {
            if (pm->authored & PropMeta::kInterpolation) {
              pv.interpolation = pm->interpolation;
            }
          }
        }
        if (pv.interpolation.empty()) {
          std::string interp_attr = name + ":interpolation";
          GetToken(prim, interp_attr, &pv.interpolation);
        }
        pv.interpolation_authored = !pv.interpolation.empty();
        if (pv.interpolation.empty()) {
          pv.interpolation = "constant";  // USD spec default (pxr/legacy parity)
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

    if (const PrimSpec* spec = prim.GetPrimSpec()) {
      if (const PropMeta* pm = spec->property_meta(full_name)) {
        if (pm->authored & PropMeta::kInterpolation) {
          pv.interpolation = pm->interpolation;
        }
      }
    }
    if (pv.interpolation.empty()) {
      std::string interp_attr = full_name + ":interpolation";
      GetToken(prim, interp_attr, &pv.interpolation);
    }
    pv.interpolation_authored = !pv.interpolation.empty();
    if (pv.interpolation.empty()) {
      pv.interpolation = "constant";  // USD spec default (pxr/legacy parity)
    }

    std::string indices_attr = full_name + ":indices";
    pv.indices = GetIntArray(prim, indices_attr);
  }

  return pv;
}

//
// Blend shape access
//

namespace {

std::vector<std::string> ReadTokenArray(const UsdPrim& prim,
                                        const std::string& name) {
  std::vector<std::string> out;
  if (const Value* v = GetAttribute(prim, name)) {
    if (const std::vector<std::string>* toks = v->as_token_array()) out = *toks;
  }
  return out;
}

// Read a matrix4d[] / double[] array as flat doubles (16 per matrix).
std::vector<double> ReadDoubleArray(const UsdPrim& prim,
                                    const std::string& name) {
  const Value* v = GetAttribute(prim, name);
  if (!v) return {};
  if (const std::vector<double>* a = v->as_double_array()) return *a;
  // Fall back to a float-backed array (some encoders store as float).
  if (const std::vector<float>* f = v->as_float_array()) {
    return std::vector<double>(f->begin(), f->end());
  }
  return {};
}

void FillIdentity(float* m16) {
  for (int i = 0; i < 16; ++i) m16[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

}  // namespace

std::vector<BlendShapeInfo> GetBlendShapes(const UsdPrim& mesh_prim) {
  std::vector<BlendShapeInfo> result;
  if (!mesh_prim.IsValid()) return result;

  // skel:blendShapes (token[]) names paired with skel:blendShapeTargets
  // (relationship) paths. The per-shape point/normal offsets live in the
  // referenced BlendShape prims; resolving those requires a Stage, so callers
  // fetch them via the core GetBlendShapeData(stage, prim) using `path`.
  const std::vector<std::string> names =
      ReadTokenArray(mesh_prim, "skel:blendShapes");
  const std::vector<std::string> targets =
      GetRelationshipTargets(mesh_prim, "skel:blendShapeTargets");

  const size_t n = std::max(names.size(), targets.size());
  result.resize(n);
  for (size_t i = 0; i < n; ++i) {
    if (i < names.size()) result[i].name = names[i];
    if (i < targets.size()) result[i].path = targets[i];
  }
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

  const std::vector<std::string> joints = ReadTokenArray(skel_prim, "joints");
  out->joint_order = joints;

  const std::vector<double> bind = ReadDoubleArray(skel_prim, "bindTransforms");
  const std::vector<double> rest = ReadDoubleArray(skel_prim, "restTransforms");

  out->joints.resize(joints.size());
  for (size_t i = 0; i < joints.size(); ++i) {
    JointInfo& j = out->joints[i];
    j.path = joints[i];

    // Joint paths are slash-separated (e.g. "Shoulder/Elbow/Hand"); the leaf
    // token is the name and the prefix identifies the parent joint.
    const size_t slash = joints[i].rfind('/');
    j.name = (slash == std::string::npos) ? joints[i] : joints[i].substr(slash + 1);
    j.parent_index = -1;
    if (slash != std::string::npos) {
      const std::string parent_path = joints[i].substr(0, slash);
      for (size_t k = 0; k < joints.size(); ++k) {
        if (joints[k] == parent_path) {
          j.parent_index = static_cast<int32_t>(k);
          break;
        }
      }
    }

    if (i * 16 + 16 <= bind.size()) {
      for (int e = 0; e < 16; ++e) j.bind_transform[e] = float(bind[i * 16 + e]);
    } else {
      FillIdentity(j.bind_transform);
    }
    if (i * 16 + 16 <= rest.size()) {
      for (int e = 0; e < 16; ++e) j.rest_transform[e] = float(rest[i * 16 + e]);
    } else {
      FillIdentity(j.rest_transform);
    }
  }

  return true;
}

//
// Skin binding access
//

bool GetSkinBinding(const UsdPrim& mesh_prim, SkinBindingInfo* out) {
  if (!out || !mesh_prim.IsValid()) return false;

  bool any = false;

  const std::vector<std::string> skels =
      GetRelationshipTargets(mesh_prim, "skel:skeleton");
  if (!skels.empty()) {
    out->skeleton_path = skels[0];
    any = true;
  }

  out->joint_indices = GetIntArray(mesh_prim, "primvars:skel:jointIndices");
  out->joint_weights = GetFloatArray(mesh_prim, "primvars:skel:jointWeights");

  // Indexed primvars (`primvars:skel:jointIndices:indices`): flatten to the
  // expanded form all consumers expect. Per UsdGeomPrimvar, each index
  // addresses a GROUP of elementSize consecutive values.
  {
    const PrimSpec* spec = mesh_prim.GetPrimSpec();
    auto elem_size = [&](const char* pv_name) -> size_t {
      if (spec) {
        if (const PropMeta* pm = spec->property_meta(pv_name)) {
          if (pm->elementSize > 0) return size_t(pm->elementSize);
        }
      }
      return 1;
    };
    auto expand_indexed = [](auto& vals, const std::vector<int32_t>& idx,
                             size_t esize) {
      if (idx.empty() || vals.empty() || esize == 0 ||
          (vals.size() % esize) != 0) {
        return;
      }
      // Expansion size is authored data (indices count x elementSize) — a
      // hostile file can request terabytes, and this TU builds without
      // exceptions so an oversized reserve aborts. 2^28 lanes (~1 GiB of
      // int32) is far past any real skin (10M points x 8 influences = 80M).
      const size_t kMaxExpandedLanes = size_t(1) << 28;
      if (esize > kMaxExpandedLanes / idx.size()) return;  // keep authored
      const size_t elems = vals.size() / esize;
      typename std::remove_reference<decltype(vals)>::type expanded;
      expanded.reserve(idx.size() * esize);
      for (int32_t i : idx) {
        if (i < 0 || size_t(i) >= elems) return;  // malformed: keep authored
        expanded.insert(expanded.end(), vals.begin() + size_t(i) * esize,
                        vals.begin() + (size_t(i) + 1) * esize);
      }
      vals = std::move(expanded);
    };
    const std::vector<int32_t> ji_idx =
        GetIntArray(mesh_prim, "primvars:skel:jointIndices:indices");
    const std::vector<int32_t> jw_idx =
        GetIntArray(mesh_prim, "primvars:skel:jointWeights:indices");
    expand_indexed(out->joint_indices, ji_idx,
                   elem_size("primvars:skel:jointIndices"));
    expand_indexed(out->joint_weights, jw_idx,
                   elem_size("primvars:skel:jointWeights"));
  }
  if (!out->joint_indices.empty() || !out->joint_weights.empty()) any = true;

  // Mesh-local joint order (subset/permutation of the skeleton's joints).
  out->joint_order = GetTokenArray(mesh_prim, "skel:joints");

  // Influences per vertex = the jointIndices primvar's elementSize.
  out->influences_per_vertex = 0;
  if (const PrimSpec* spec = mesh_prim.GetPrimSpec()) {
    if (const PropMeta* pm =
            spec->property_meta("primvars:skel:jointIndices")) {
      out->influences_per_vertex = pm->elementSize;
    }
  }

  double gm[16];
  if (GetMatrix4d(mesh_prim, "primvars:skel:geomBindTransform", gm)) {
    for (int i = 0; i < 16; ++i) out->geom_bind_transform[i] = float(gm[i]);
    any = true;
  } else {
    FillIdentity(out->geom_bind_transform);
  }

  return any;
}

//
// Connection following
//

const Value* ResolveConnection(const Stage& stage, const UsdPrim& prim, const std::string& attr_name) {
  if (!prim.IsValid()) return nullptr;

  // A directly-authored value wins over a connection.
  if (const Value* val = GetAttribute(prim, attr_name)) {
    return val;
  }

  // Follow the connection chain: attr.connect -> target attribute, which may
  // itself hold a value or connect onward (e.g. shader input -> shader output
  // -> ...). Bounded depth guards against cycles.
  UsdPrim cur = prim;
  std::string attr = attr_name;
  for (int depth = 0; depth < 64; ++depth) {
    const PrimSpec* spec = cur.GetPrimSpec();
    if (!spec) break;
    const std::vector<Path>* conns = spec->connection(attr);
    if (!conns || conns->empty()) break;

    const Path& target = (*conns)[0];
    const std::string prop = target.property_name();
    if (prop.empty()) break;
    UsdPrim next = stage.GetPrimAtPath(target.prim_path());
    if (!next.IsValid()) break;

    // A value on the target ends the chain.
    if (const Value* v = GetAttribute(next, prop)) {
      return v;
    }
    cur = next;
    attr = prop;
  }
  return nullptr;
}

std::string GetConnectionPath(const UsdPrim& prim, const std::string& attr_name) {
  if (!prim.IsValid()) return "";

  const PrimSpec* spec = prim.GetPrimSpec();
  if (!spec) return "";
  if (const std::vector<Path>* conns = spec->connection(attr_name)) {
    if (!conns->empty()) return (*conns)[0].str();
  }
  return "";
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
