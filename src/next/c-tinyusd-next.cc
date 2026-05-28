// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - C API Implementation

#include "c-tinyusd-next.h"
#include "tinyusdz-next.hh"
#include "stage/stage.hh"
#include "eval/attribute-eval.hh"
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <cstdlib>

namespace {

// ============================================================
// Thread-local error state
// ============================================================

#ifdef _MSC_VER
static __declspec(thread) char tls_error_buf[4096] = {};
static __declspec(thread) char tls_prim_path_buf[4096] = {};
#else
static thread_local char tls_error_buf[4096] = {};
#endif

void SetError(const char* msg) {
  if (msg) {
    std::strncpy(tls_error_buf, msg, sizeof(tls_error_buf) - 1);
    tls_error_buf[sizeof(tls_error_buf) - 1] = '\0';
  } else {
    tls_error_buf[0] = '\0';
  }
}

// Thread-local buffer for string returns
enum { kStringBufSlots = 4, kStringBufSize = 4096 };
static thread_local char tls_string_buf[kStringBufSlots][kStringBufSize];
static thread_local int tls_string_slot = 0;

const char* BufStr(const std::string& s) {
  int slot = tls_string_slot++ % kStringBufSlots;
  std::strncpy(tls_string_buf[slot], s.c_str(), kStringBufSize - 1);
  tls_string_buf[slot][kStringBufSize - 1] = '\0';
  return tls_string_buf[slot];
}

} // namespace

// ============================================================
// Error API
// ============================================================

const char* tinyusdz_next_error_string(void) {
  return tls_error_buf[0] ? tls_error_buf : nullptr;
}

void tinyusdz_next_set_error(const char* msg) {
  SetError(msg);
}

// ============================================================
// Opaque type wrappers
// ============================================================

struct TinyUSDZNextStage {
  tinyusdz::next::Stage stage;
};

struct TinyUSDZNextPrim {
  const tinyusdz::next::UsdPrim* prim = nullptr;
};

// Lightweight storage for prim handles returned to C callers.
// For production use, consider a handle table with reference counting.
static thread_local TinyUSDZNextPrim tls_prim;

static thread_local std::string tls_type_name;
static thread_local std::string tls_name;
static thread_local std::string tls_path_str;
static thread_local std::string tls_default_prim;

// ============================================================
// Stage API
// ============================================================

TinyUSDZNextStage* tinyusdz_next_stage_new(void) {
  auto* s = new (std::nothrow) TinyUSDZNextStage;
  if (!s) {
    SetError("Out of memory");
    return nullptr;
  }
  return s;
}

void tinyusdz_next_stage_free(TinyUSDZNextStage* stage) {
  delete stage;
}

TinyUSDZNextStage* tinyusdz_next_load_usd(const char* filename) {
  if (!filename) {
    SetError("filename is null");
    return nullptr;
  }
  auto* s = new (std::nothrow) TinyUSDZNextStage;
  if (!s) { SetError("Out of memory"); return nullptr; }
  std::string warn, err;
  if (!tinyusdz::next::LoadUSD(filename, &s->stage, &warn, &err)) {
    SetError(err.empty() ? "Failed to load USD" : err.c_str());
    delete s; return nullptr;
  }
  return s;
}

TinyUSDZNextStage* tinyusdz_next_load_usda(const char* filename) {
  if (!filename) { SetError("filename is null"); return nullptr; }
  auto* s = new (std::nothrow) TinyUSDZNextStage;
  if (!s) { SetError("Out of memory"); return nullptr; }
  std::string warn, err;
  if (!tinyusdz::next::LoadUSDA(filename, &s->stage, &warn, &err)) {
    SetError(err.empty() ? "Failed to load USDA" : err.c_str());
    delete s; return nullptr;
  }
  return s;
}

TinyUSDZNextStage* tinyusdz_next_load_usdc(const char* filename) {
  if (!filename) { SetError("filename is null"); return nullptr; }
  auto* s = new (std::nothrow) TinyUSDZNextStage;
  if (!s) { SetError("Out of memory"); return nullptr; }
  std::string warn, err;
  if (!tinyusdz::next::LoadUSDC(filename, &s->stage, &warn, &err)) {
    SetError(err.empty() ? "Failed to load USDC" : err.c_str());
    delete s; return nullptr;
  }
  return s;
}

const char* tinyusdz_next_stage_default_prim(const TinyUSDZNextStage* stage) {
  if (!stage) return nullptr;
  tls_default_prim = stage->stage.GetMeta().defaultPrim;
  return tls_default_prim.empty() ? nullptr : tls_default_prim.c_str();
}

// ============================================================
// Prim API
// ============================================================

const TinyUSDZNextPrim* tinyusdz_next_stage_get_prim_at_path(
    const TinyUSDZNextStage* stage, const char* path) {
  if (!stage || !path) {
    SetError("Invalid arguments");
    return nullptr;
  }

  tinyusdz::next::UsdPrim usd_prim = stage->stage.GetPrimAtPath(path);
  if (!usd_prim.IsValid()) {
    SetError("Prim not found");
    return nullptr;
  }

  tls_prim.prim = nullptr;
  static thread_local tinyusdz::next::UsdPrim s_stored_prim;
  s_stored_prim = usd_prim;
  tls_prim.prim = &s_stored_prim;
  return &tls_prim;
}

const char* tinyusdz_next_prim_get_type_name(const TinyUSDZNextPrim* prim) {
  if (!prim || !prim->prim) return nullptr;
  tls_type_name = prim->prim->GetTypeName();
  return tls_type_name.c_str();
}

const char* tinyusdz_next_prim_get_name(const TinyUSDZNextPrim* prim) {
  if (!prim || !prim->prim) return nullptr;
  tls_name = prim->prim->GetName();
  return tls_name.c_str();
}

const char* tinyusdz_next_prim_get_path(const TinyUSDZNextPrim* prim) {
  if (!prim || !prim->prim) return nullptr;
  tls_path_str = prim->prim->GetPath().str();
  return tls_path_str.c_str();
}

tinyusdz_next_result_t tinyusdz_next_prim_has_property(
    const TinyUSDZNextPrim* prim, const char* prop_name) {
  if (!prim || !prim->prim || !prop_name) return 0;
  return prim->prim->HasProperty(prop_name) ? 1 : 0;
}

tinyusdz_next_result_t tinyusdz_next_prim_has_relationship(
    const TinyUSDZNextPrim* prim, const char* rel_name) {
  if (!prim || !prim->prim || !rel_name) return 0;
  return prim->prim->GetRelationship(rel_name) != nullptr ? 1 : 0;
}

size_t tinyusdz_next_prim_get_child_count(const TinyUSDZNextPrim* prim) {
  if (!prim || !prim->prim) return 0;
  return prim->prim->GetChildCount();
}

const TinyUSDZNextPrim* tinyusdz_next_prim_get_child(
    const TinyUSDZNextPrim* prim, size_t index) {
  if (!prim || !prim->prim) return nullptr;

  // UsdPrim::GetChild() takes a string name, not index
  // Use GetChildren() for index-based access
  std::vector<tinyusdz::next::UsdPrim> children = prim->prim->GetChildren();
  if (index >= children.size()) return nullptr;

  static thread_local tinyusdz::next::UsdPrim s_child;
  static thread_local TinyUSDZNextPrim s_child_prim;
  s_child = children[index];
  s_child_prim.prim = &s_child;
  return &s_child_prim;
}

size_t tinyusdz_next_stage_traverse(
    const TinyUSDZNextStage* stage,
    tinyusdz_next_traverse_callback_t callback,
    void* user_data) {
  if (!stage || !callback) return 0;

  struct TraversalState {
    tinyusdz_next_traverse_callback_t cb;
    void* data;
    size_t count;
    tinyusdz::next::UsdPrim* stored_prims;
    int stored_count;
    int stored_cap;

    void Visit(const tinyusdz::next::UsdPrim& prim, int depth) {
      TinyUSDZNextPrim p;
      p.prim = &prim;
      if (!cb(&p, depth, data)) return;
      count++;
      std::vector<tinyusdz::next::UsdPrim> children = prim.GetChildren();
      for (auto& child : children) {
        Visit(child, depth + 1);
      }
    }
  };

  std::vector<tinyusdz::next::UsdPrim> roots = stage->stage.GetRootPrims();
  TraversalState state{callback, user_data, 0, nullptr, 0, 0};
  for (auto& root : roots) {
    state.Visit(root, 0);
  }

  return state.count;
}

// ============================================================
// Property value type
// ============================================================

tinyusdz_next_value_type_t tinyusdz_next_prim_get_value_type(
    const TinyUSDZNextPrim* prim, const char* prop_name) {
  if (!prim || !prim->prim || !prop_name) return TINYUSDZ_NEXT_VALUE_UNKNOWN;

  const tinyusdz::next::Value* val =
      prim->prim->GetPropertyValue(prop_name);
  if (!val) return TINYUSDZ_NEXT_VALUE_UNKNOWN;

  // Check array FIRST - array types have element type_id but is_array_=true
  if (val->is_array()) {
    switch (val->type_id()) {
      case tinyusdz::next::TypeId::Float:
      case tinyusdz::next::TypeId::Float3:
        return TINYUSDZ_NEXT_VALUE_FLOAT_ARRAY;
      case tinyusdz::next::TypeId::Int:
        return TINYUSDZ_NEXT_VALUE_INT32_ARRAY;
      case tinyusdz::next::TypeId::Double:
        return TINYUSDZ_NEXT_VALUE_DOUBLE_ARRAY;
      case tinyusdz::next::TypeId::Int64:
        return TINYUSDZ_NEXT_VALUE_INT64_ARRAY;
      case tinyusdz::next::TypeId::UInt:
        return TINYUSDZ_NEXT_VALUE_UINT_ARRAY;
      case tinyusdz::next::TypeId::UInt64:
        return TINYUSDZ_NEXT_VALUE_UINT64_ARRAY;
      default:
        return TINYUSDZ_NEXT_VALUE_FLOAT_ARRAY;
    }
  }

  switch (val->type_id()) {
    case tinyusdz::next::TypeId::Bool:
      return TINYUSDZ_NEXT_VALUE_BOOL;
    case tinyusdz::next::TypeId::Int:
      return TINYUSDZ_NEXT_VALUE_INT32;
    case tinyusdz::next::TypeId::UInt:
      return TINYUSDZ_NEXT_VALUE_UINT32;
    case tinyusdz::next::TypeId::Int64:
      return TINYUSDZ_NEXT_VALUE_INT64;
    case tinyusdz::next::TypeId::UInt64:
      return TINYUSDZ_NEXT_VALUE_UINT64;
    case tinyusdz::next::TypeId::Float:
      return TINYUSDZ_NEXT_VALUE_FLOAT;
    case tinyusdz::next::TypeId::Double:
      return TINYUSDZ_NEXT_VALUE_DOUBLE;
    case tinyusdz::next::TypeId::String:
      return TINYUSDZ_NEXT_VALUE_STRING;
    case tinyusdz::next::TypeId::Token:
      return TINYUSDZ_NEXT_VALUE_TOKEN;
    case tinyusdz::next::TypeId::AssetPath:
      return TINYUSDZ_NEXT_VALUE_ASSET_PATH;
    case tinyusdz::next::TypeId::Float2:
      return TINYUSDZ_NEXT_VALUE_FLOAT2;
    case tinyusdz::next::TypeId::Float3:
    case tinyusdz::next::TypeId::Point3f:
    case tinyusdz::next::TypeId::Vector3f:
    case tinyusdz::next::TypeId::Normal3f:
    case tinyusdz::next::TypeId::Color3f:
      return TINYUSDZ_NEXT_VALUE_FLOAT3;
    case tinyusdz::next::TypeId::Float4:
    case tinyusdz::next::TypeId::Color4f:
    case tinyusdz::next::TypeId::Quatf:
      return TINYUSDZ_NEXT_VALUE_FLOAT4;
    case tinyusdz::next::TypeId::Double2:
      return TINYUSDZ_NEXT_VALUE_DOUBLE2;
    case tinyusdz::next::TypeId::Double3:
      return TINYUSDZ_NEXT_VALUE_DOUBLE3;
    case tinyusdz::next::TypeId::Double4:
      return TINYUSDZ_NEXT_VALUE_DOUBLE4;
    case tinyusdz::next::TypeId::Matrix4d:
      return TINYUSDZ_NEXT_VALUE_MATRIX4D;
    default:
      return TINYUSDZ_NEXT_VALUE_UNKNOWN;
  }
}

// ============================================================
// Scalar value access
// ============================================================

tinyusdz_next_result_t tinyusdz_next_prim_get_bool(
    const TinyUSDZNextPrim* prim, const char* prop_name, int* out) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const bool* b = val->as_bool();
  if (!b) return 0;
  *out = *b ? 1 : 0;
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_float(
    const TinyUSDZNextPrim* prim, const char* prop_name, float* out) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const float* f = val->as_float();
  if (!f) return 0;
  *out = *f;
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_double(
    const TinyUSDZNextPrim* prim, const char* prop_name, double* out) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const double* d = val->as_double();
  if (!d) return 0;
  *out = *d;
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_int32(
    const TinyUSDZNextPrim* prim, const char* prop_name, int32_t* out) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const int32_t* i = val->as_int();
  if (!i) return 0;
  *out = *i;
  return 1;
}

const char* tinyusdz_next_prim_get_string(
    const TinyUSDZNextPrim* prim, const char* prop_name) {
  if (!prim || !prim->prim || !prop_name) return nullptr;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return nullptr;
  const std::string* s = val->as_string();
  if (s) return BufStr(*s);
  const std::string* tok = val->as_token();
  if (tok) return BufStr(*tok);
  const std::string* asset = val->as_asset_path();
  if (asset) return BufStr(*asset);
  return nullptr;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_float3(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    float out[3]) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const float* f = val->as_float3();
  if (!f) f = val->as_float4(); // quatf/color4f fallback
  if (!f) return 0;
  out[0] = f[0]; out[1] = f[1]; out[2] = f[2];
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_float4(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    float out[4]) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const float* f = val->as_float4();
  if (!f) return 0;
  out[0] = f[0]; out[1] = f[1]; out[2] = f[2]; out[3] = f[3];
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_matrix4d(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double out[16]) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const double* d = val->as_matrix4d();
  if (!d) return 0;
  for (int i = 0; i < 16; ++i) out[i] = d[i];
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_float2(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    float out[2]) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const float* f = val->as_float2();
  if (!f) return 0;
  out[0] = f[0]; out[1] = f[1];
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_double2(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double out[2]) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const double* d = val->as_double2();
  if (!d) return 0;
  out[0] = d[0]; out[1] = d[1];
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_double3(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double out[3]) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const double* d = val->as_double3();
  if (!d) return 0;
  out[0] = d[0]; out[1] = d[1]; out[2] = d[2];
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_double4(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double out[4]) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const double* d = val->as_double4();
  if (!d) return 0;
  out[0] = d[0]; out[1] = d[1]; out[2] = d[2]; out[3] = d[3];
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_int64(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    int64_t* out) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const int64_t* i = val->as_int64();
  if (!i) return 0;
  *out = *i;
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_uint32(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    uint32_t* out) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const uint32_t* u = val->as_uint();
  if (!u) return 0;
  *out = *u;
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_get_uint64(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    uint64_t* out) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const uint64_t* u = val->as_uint64();
  if (!u) return 0;
  *out = *u;
  return 1;
}

// ============================================================
// Array value access
// ============================================================

size_t tinyusdz_next_prim_get_float_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const float** out_ptr) {
  if (!prim || !prim->prim || !prop_name || !out_ptr) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const std::vector<float>* arr = val->as_float_array();
  if (!arr) return 0;
  *out_ptr = arr->data();
  return arr->size();
}

size_t tinyusdz_next_prim_get_int32_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const int32_t** out_ptr) {
  if (!prim || !prim->prim || !prop_name || !out_ptr) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const std::vector<int32_t>* arr = val->as_int_array();
  if (!arr) return 0;
  *out_ptr = arr->data();
  return arr->size();
}

size_t tinyusdz_next_prim_get_double_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const double** out_ptr) {
  if (!prim || !prim->prim || !prop_name || !out_ptr) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const std::vector<double>* arr = val->as_double_array();
  if (!arr) return 0;
  *out_ptr = arr->data();
  return arr->size();
}

size_t tinyusdz_next_prim_get_int64_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const int64_t** out_ptr) {
  if (!prim || !prim->prim || !prop_name || !out_ptr) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const std::vector<int64_t>* arr = val->as_int64_array();
  if (!arr) return 0;
  *out_ptr = arr->data();
  return arr->size();
}

size_t tinyusdz_next_prim_get_uint_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const uint32_t** out_ptr) {
  if (!prim || !prim->prim || !prop_name || !out_ptr) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const std::vector<uint32_t>* arr = val->as_uint_array();
  if (!arr) return 0;
  *out_ptr = arr->data();
  return arr->size();
}

size_t tinyusdz_next_prim_get_uint64_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const uint64_t** out_ptr) {
  if (!prim || !prim->prim || !prop_name || !out_ptr) return 0;
  const tinyusdz::next::Value* val = prim->prim->GetPropertyValue(prop_name);
  if (!val) return 0;
  const std::vector<uint64_t>* arr = val->as_uint64_array();
  if (!arr) return 0;
  *out_ptr = arr->data();
  return arr->size();
}

// ============================================================
// Relationship access
// ============================================================

size_t tinyusdz_next_prim_get_relationship_targets(
    const TinyUSDZNextPrim* prim, const char* rel_name,
    const char*** out_ptr) {
  if (!prim || !prim->prim || !rel_name || !out_ptr) return 0;
  const std::vector<tinyusdz::next::Path>* targets =
      prim->prim->GetRelationship(rel_name);
  if (!targets || targets->empty()) return 0;

  static thread_local const char* tls_rel_buf[256];
  static thread_local std::string tls_rel_strs[256];
  size_t count = targets->size() > 256 ? 256 : targets->size();
  for (size_t i = 0; i < count; ++i) {
    tls_rel_strs[i] = (*targets)[i].str();
    tls_rel_buf[i] = tls_rel_strs[i].c_str();
  }
  *out_ptr = tls_rel_buf;
  return count;
}

size_t tinyusdz_next_prim_get_property_names(
    const TinyUSDZNextPrim* prim, const char*** out_ptr) {
  if (!prim || !prim->prim || !out_ptr) return 0;
  std::vector<std::string> names = prim->prim->GetPropertyNames();
  if (names.empty()) return 0;
  static thread_local const char* tls_prop_buf[512];
  static thread_local std::string tls_prop_strs[512];
  size_t count = names.size() > 512 ? 512 : names.size();
  for (size_t i = 0; i < count; ++i) {
    tls_prop_strs[i] = names[i];
    tls_prop_buf[i] = tls_prop_strs[i].c_str();
  }
  *out_ptr = tls_prop_buf;
  return count;
}

// ============================================================
// Time sample evaluation
// ============================================================

tinyusdz_next_result_t tinyusdz_next_prim_has_time_samples(
    const TinyUSDZNextPrim* prim, const char* prop_name) {
  if (!prim || !prim->prim || !prop_name) return 0;
  return prim->prim->HasTimeSamples(prop_name) ? 1 : 0;
}

tinyusdz_next_result_t tinyusdz_next_prim_eval_float(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double time, float* out) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val =
      prim->prim->GetValueAtTime(prop_name, time);
  if (!val) return 0;
  const float* f = val->as_float();
  if (!f) return 0;
  *out = *f;
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_eval_float3(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double time, float out[3]) {
  if (!prim || !prim->prim || !prop_name || !out) return 0;
  const tinyusdz::next::Value* val =
      prim->prim->GetValueAtTime(prop_name, time);
  if (!val) return 0;
  const float* f = val->as_float3();
  if (!f) return 0;
  out[0] = f[0]; out[1] = f[1]; out[2] = f[2];
  return 1;
}

size_t tinyusdz_next_prim_eval_float_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double time, const float** out_ptr) {
  if (!prim || !prim->prim || !prop_name || !out_ptr) return 0;
  const tinyusdz::next::Value* val =
      prim->prim->GetValueAtTime(prop_name, time);
  if (!val) return 0;
  const std::vector<float>* arr = val->as_float_array();
  if (!arr) return 0;
  *out_ptr = arr->data();
  return arr->size();
}

// ============================================================
// Composition arc authoring
// ============================================================

static tinyusdz::next::PrimSpec* _GetMutablePrim(
    TinyUSDZNextStage* stage, const char* path) {
  if (!stage || !path) return nullptr;
  tinyusdz::next::Layer* layer = stage->stage.GetRootLayer();
  if (!layer) return nullptr;
  return layer->prim_at_path_mutable(path);
}

static std::string _MakeArcString(const char* asset, const char* prim) {
  std::string s = "@";
  if (asset && asset[0]) s += asset;
  s += "@";
  if (prim && prim[0]) { s += "</"; s += prim; s += ">"; }
  return s;
}

tinyusdz_next_result_t tinyusdz_next_prim_add_reference(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* asset_path, const char* ref_prim_path) {
  auto* spec = _GetMutablePrim(stage, prim_path);
  if (!spec) { SetError("Prim not found"); return 0; }
  spec->meta().references.push_back(
      _MakeArcString(asset_path, ref_prim_path));
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_add_payload(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* asset_path, const char* payload_prim_path) {
  auto* spec = _GetMutablePrim(stage, prim_path);
  if (!spec) { SetError("Prim not found"); return 0; }
  spec->meta().payloads.push_back(
      _MakeArcString(asset_path, payload_prim_path));
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_add_inherit(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* inherited_prim_path) {
  auto* spec = _GetMutablePrim(stage, prim_path);
  if (!spec) { SetError("Prim not found"); return 0; }
  std::string s = "</";
  if (inherited_prim_path) s += inherited_prim_path;
  s += ">";
  spec->meta().inherits.push_back(s);
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_add_specialize(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* specialized_prim_path) {
  auto* spec = _GetMutablePrim(stage, prim_path);
  if (!spec) { SetError("Prim not found"); return 0; }
  std::string s = "</";
  if (specialized_prim_path) s += specialized_prim_path;
  s += ">";
  spec->meta().specializes.push_back(s);
  return 1;
}

tinyusdz_next_result_t tinyusdz_next_prim_set_variant_selection(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* variant_set, const char* variant_name) {
  auto* spec = _GetMutablePrim(stage, prim_path);
  if (!spec) { SetError("Prim not found"); return 0; }
  std::string s;
  if (variant_set) s += variant_set;
  s += "=";
  if (variant_name) s += variant_name;
  spec->meta().variantSelection = s;
  return 1;
}
