// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD C API implementation (core: stage/prim/attr/authoring).

#include "c-internal.hh"

#include <algorithm>
#include <cstdio>

#include "next/prim/identifier.hh"
#include "next/lightusd-next.hh"
#include "next/pipeline/flatten.hh"
#include "next/writer/usda-writer.hh"
#include "next/writer/usdc-writer.hh"
#include "next/writer/usdz-writer.hh"
#include "tydra/next/scene-access.hh"

namespace n = lightusd::next;
using tusd_internal::Fail;
using tusd_internal::FromC;
using tusd_internal::MakeView;
using tusd_internal::SetError;
using tusd_internal::SV;
using tusd_internal::EmptySV;
using tusd_internal::ToC;
using tusd_internal::ValueFromRaw;

// ============================================================
// Error handling / library info
// ============================================================

namespace tusd_internal {

namespace {
thread_local std::string t_last_error;
// Serializes first-touch materialization of lazy crate arrays (logical-const
// mutation shared across threads reading one stage).
std::mutex g_materialize_mu;
}  // namespace

void SetError(const std::string& msg) { t_last_error = msg; }
void SetError(const char* msg) { t_last_error = msg ? msg : ""; }

}  // namespace tusd_internal

extern "C" {

uint32_t tusd_api_version(void) {
  return (uint32_t(TUSD_API_VERSION_MAJOR) << 16) |
         (uint32_t(TUSD_API_VERSION_MINOR) << 8) |
         uint32_t(TUSD_API_VERSION_PATCH);
}

const char* tusd_version_string(void) { return "1.0.0"; }

const char* tusd_last_error(void) {
  return tusd_internal::t_last_error.c_str();
}

// ============================================================
// Strings
// ============================================================

tusd_sv tusd_string_view(const tusd_string* s) {
  if (!s) return EmptySV();
  return SV(s->s);
}

void tusd_string_destroy(tusd_string* s) { delete s; }

size_t tusd_strlist_size(const tusd_strlist* l) {
  return l ? l->items.size() : 0;
}

tusd_sv tusd_strlist_get(const tusd_strlist* l, size_t index) {
  if (!l || index >= l->items.size()) return EmptySV();
  return SV(l->items[index]);
}

void tusd_strlist_destroy(tusd_strlist* l) { delete l; }

// ============================================================
// Types
// ============================================================

const char* tusd_type_name(tusd_type t) {
  const char* name = n::GetTypeName(static_cast<n::TypeId>(t));
  return name ? name : "";
}

tusd_type tusd_type_from_name(const char* name) {
  if (!name) return TUSD_TYPE_INVALID;
  return static_cast<tusd_type>(n::GetTypeIdFromName(name));
}

size_t tusd_type_size(tusd_type t) {
  return n::GetTypeSize(static_cast<n::TypeId>(t));
}

size_t tusd_type_component_count(tusd_type t) {
  return n::GetComponentCount(static_cast<n::TypeId>(t));
}

}  // extern "C"

// ============================================================
// Value <-> view conversion helpers
// ============================================================

namespace tusd_internal {

namespace {

// Storage component type of the (materialized) POD buffer behind a Value.
tusd_component_type StorageFor(n::TypeId t, bool is_array) {
  switch (t) {
    case n::TypeId::Bool:
      return TUSD_COMP_UINT8;
    case n::TypeId::Int:
    case n::TypeId::Int2:
    case n::TypeId::Int3:
    case n::TypeId::Int4:
      return TUSD_COMP_INT32;
    case n::TypeId::UInt:
    case n::TypeId::UInt2:
    case n::TypeId::UInt3:
    case n::TypeId::UInt4:
      return TUSD_COMP_UINT32;
    case n::TypeId::Int64:
      return TUSD_COMP_INT64;
    case n::TypeId::UInt64:
      return TUSD_COMP_UINT64;
    // Half element types: arrays materialize into float32 buffers; scalar
    // halves stay 16-bit in the SBO.
    case n::TypeId::Half:
    case n::TypeId::Half2:
    case n::TypeId::Half3:
    case n::TypeId::Half4:
    case n::TypeId::Quath:
    case n::TypeId::Point3h:
    case n::TypeId::Vector3h:
    case n::TypeId::Normal3h:
    case n::TypeId::Color3h:
    case n::TypeId::Color4h:
    case n::TypeId::Texcoord2h:
    case n::TypeId::Texcoord3h:
      return is_array ? TUSD_COMP_FLOAT32 : TUSD_COMP_FLOAT16;
    case n::TypeId::Float:
    case n::TypeId::Float2:
    case n::TypeId::Float3:
    case n::TypeId::Float4:
    case n::TypeId::Quatf:
    case n::TypeId::Point3f:
    case n::TypeId::Vector3f:
    case n::TypeId::Normal3f:
    case n::TypeId::Color3f:
    case n::TypeId::Color4f:
    case n::TypeId::Texcoord2f:
    case n::TypeId::Texcoord3f:
    case n::TypeId::Matrix2f:
    case n::TypeId::Matrix3f:
    case n::TypeId::Matrix4f:
    case n::TypeId::Extent:
      return TUSD_COMP_FLOAT32;
    case n::TypeId::Double:
    case n::TypeId::Double2:
    case n::TypeId::Double3:
    case n::TypeId::Double4:
    case n::TypeId::Quatd:
    case n::TypeId::Point3d:
    case n::TypeId::Vector3d:
    case n::TypeId::Normal3d:
    case n::TypeId::Color3d:
    case n::TypeId::Color4d:
    case n::TypeId::Texcoord2d:
    case n::TypeId::Texcoord3d:
    case n::TypeId::Matrix2d:
    case n::TypeId::Matrix3d:
    case n::TypeId::Matrix4d:
    case n::TypeId::TimeCode:
      return TUSD_COMP_FLOAT64;
    default:
      return TUSD_COMP_NONE;
  }
}

size_t CompSize(tusd_component_type c) {
  switch (c) {
    case TUSD_COMP_UINT8:
      return 1;
    case TUSD_COMP_FLOAT16:
      return 2;
    case TUSD_COMP_INT32:
    case TUSD_COMP_UINT32:
    case TUSD_COMP_FLOAT32:
      return 4;
    case TUSD_COMP_INT64:
    case TUSD_COMP_UINT64:
    case TUSD_COMP_FLOAT64:
      return 8;
    default:
      return 0;
  }
}

bool IsStringFamily(n::TypeId t) {
  return t == n::TypeId::String || t == n::TypeId::Token ||
         t == n::TypeId::AssetPath;
}

}  // namespace

tusd_status MakeView(const n::Value& v, tusd_value_view* out) {
  if (!out) return Fail(TUSD_ERR_INVALID_ARG, "out view is null");
  std::memset(out, 0, sizeof(*out));

  const n::TypeId t = v.type_id();
  out->type = static_cast<tusd_type>(t);
  out->is_array = v.is_array() ? 1 : 0;
  out->is_block = v.is_block() ? 1 : 0;
  out->count = v.is_array() ? v.array_size() : 1;

  if (v.is_block() || v.is_empty()) {
    out->count = 0;
    return TUSD_OK;
  }

  // String-family / dictionary values carry no POD buffer; fetch them through
  // the string / token-array / dict accessors.
  if (IsStringFamily(t) || t == n::TypeId::Dictionary) {
    return TUSD_OK;
  }

  size_t nbytes = 0;
  const uint8_t* bytes = nullptr;
  if (v.is_lazy()) {
    std::lock_guard<std::mutex> lk(g_materialize_mu);
    bytes = v.raw_bytes(&nbytes);  // materializes under the lock
  } else {
    bytes = v.raw_bytes(&nbytes);
  }
  tusd_component_type storage = StorageFor(t, v.is_array());
  if (!bytes || nbytes == 0) {
    // No POD buffer (empty array, or unsupported storage); the view still
    // reports type/count/storage so callers can build an empty view.
    out->storage = static_cast<uint8_t>(storage);
    size_t comps0 = n::GetComponentCount(t);
    out->components = static_cast<uint8_t>(comps0 > 255 ? 0 : comps0);
    return TUSD_OK;
  }

  const size_t csize = CompSize(storage);
  size_t components = n::GetComponentCount(t);
  // Derive components from the actual byte size when the type table disagrees
  // (e.g. Extent = float3[2] = 6 floats).
  if (out->count > 0 && csize > 0) {
    const size_t derived = nbytes / (out->count * csize);
    if (derived > 0 && derived * out->count * csize == nbytes) {
      components = derived;
    }
  }

  out->storage = static_cast<uint8_t>(storage);
  out->components = static_cast<uint8_t>(components > 255 ? 0 : components);
  out->data = bytes;
  out->nbytes = nbytes;
  return TUSD_OK;
}

bool ValueFromRaw(tusd_type type, uint8_t is_array, const void* data,
                  size_t count, n::Value* out) {
  const n::TypeId t = static_cast<n::TypeId>(type);
  if (t == n::TypeId::Invalid || !out) {
    SetError("invalid type");
    return false;
  }

  if (IsStringFamily(t)) {
    if (is_array) {
      SetError("string-family arrays must use tusd_attr_set_token_array");
      return false;
    }
    const char* s = static_cast<const char*>(data);
    if (!s) {
      SetError("string data is null");
      return false;
    }
    if (t == n::TypeId::Token) {
      *out = n::Value::MakeToken(std::string(s));
    } else if (t == n::TypeId::AssetPath) {
      *out = n::Value::MakeAssetPath(std::string(s));
    } else {
      *out = n::Value(std::string(s));
    }
    return true;
  }

  if (!data || count == 0) {
    SetError("value data is null/empty");
    return false;
  }

  const bool array = is_array || count > 1;

  if (!array) {
    // Scalar: MakeFromRaw copies GetTypeSize(t) bytes.
    if (n::GetTypeSize(t) == 0) {
      SetError("type has no fixed size; cannot author as scalar");
      return false;
    }
    // Bool needs a real C++ bool in storage.
    if (t == n::TypeId::Bool) {
      bool b = *static_cast<const uint8_t*>(data) != 0;
      *out = n::Value(b);
      return true;
    }
    *out = n::Value::MakeFromRaw(t, data);
    return true;
  }

  // Arrays. Component layout is tightly packed elements of the declared type.
  const size_t comps = n::GetComponentCount(t);
  switch (t) {
    case n::TypeId::Bool: {
      const uint8_t* p = static_cast<const uint8_t*>(data);
      std::vector<bool> tmp(count);
      for (size_t i = 0; i < count; ++i) tmp[i] = p[i] != 0;
      *out = n::Value::MakeBoolArray(tmp);
      return true;
    }
    case n::TypeId::Int: {
      const int32_t* p = static_cast<const int32_t*>(data);
      *out = n::Value::MakeIntArray(std::vector<int32_t>(p, p + count));
      return true;
    }
    case n::TypeId::UInt: {
      const uint32_t* p = static_cast<const uint32_t*>(data);
      *out = n::Value::MakeUIntArray(std::vector<uint32_t>(p, p + count));
      return true;
    }
    case n::TypeId::Int64: {
      const int64_t* p = static_cast<const int64_t*>(data);
      *out = n::Value::MakeInt64Array(std::vector<int64_t>(p, p + count));
      return true;
    }
    case n::TypeId::UInt64: {
      const uint64_t* p = static_cast<const uint64_t*>(data);
      *out = n::Value::MakeUInt64Array(std::vector<uint64_t>(p, p + count));
      return true;
    }
    default:
      break;
  }

  const tusd_component_type storage = StorageFor(t, true);
  if (storage == TUSD_COMP_FLOAT32 && comps > 0) {
    const float* p = static_cast<const float*>(data);
    std::vector<float> flat(p, p + count * comps);
    if (t == n::TypeId::Float) {
      *out = n::Value::MakeFloatArray(std::move(flat));
    } else {
      *out = n::Value::MakeFloatCompArray(std::move(flat), t,
                                          static_cast<uint32_t>(comps));
    }
    return true;
  }
  if (storage == TUSD_COMP_FLOAT64 && comps > 0) {
    const double* p = static_cast<const double*>(data);
    std::vector<double> flat(p, p + count * comps);
    if (t == n::TypeId::Double) {
      *out = n::Value::MakeDoubleArray(std::move(flat));
    } else {
      *out = n::Value::MakeDoubleCompArray(std::move(flat), t,
                                           static_cast<uint32_t>(comps));
    }
    return true;
  }

  SetError(std::string("unsupported array element type: ") +
           tusd_type_name(type));
  return false;
}

}  // namespace tusd_internal

// ============================================================
// Local helpers
// ============================================================

namespace {

n::Layer* RootLayerOf(tusd_stage* stage) {
  return stage ? stage->stage.GetRootLayer() : nullptr;
}

// Attribute/relationship names must be valid (possibly namespaced) identifiers
// -- the parser and usdcat both reject non-identifier names, so an API-created
// one would not round-trip. Reject it up front with a clear error.
bool IsValidAttrName(const char* name) {
  return name && n::IsValidNamespacedIdentifier(name);
}

n::PrimSpec* MutablePrimAt(tusd_stage* stage, const char* prim_path,
                           tusd_status* st) {
  *st = TUSD_OK;
  if (!stage || !prim_path) {
    *st = Fail(TUSD_ERR_INVALID_ARG, "stage/path is null");
    return nullptr;
  }
  n::Layer* layer = RootLayerOf(stage);
  if (!layer) {
    *st = Fail(TUSD_ERR_INTERNAL, "stage has no root layer");
    return nullptr;
  }
  n::PrimSpec* spec = layer->prim_at_path_mutable(prim_path);
  if (!spec) {
    *st = Fail(TUSD_ERR_NOT_FOUND,
               std::string("no prim at path: ") + prim_path);
    return nullptr;
  }
  return spec;
}

// Record the declared USD type name so writers re-emit exact types.
void RecordTypeName(n::PrimSpec* spec, const char* name, tusd_type type,
                    bool is_array) {
  const char* tn = tusd_type_name(type);
  if (!tn || !tn[0]) return;
  std::string decl(tn);
  if (is_array) decl += "[]";
  spec->set_property_type_name(name, decl);
}

tusd_value* NewValue(n::Value&& v) {
  tusd_value* out = new (std::nothrow) tusd_value();
  if (out) out->v = std::move(v);
  return out;
}

const n::Value* FindDefaultValue(tusd_prim p, const char* name,
                                 tusd_status* st) {
  *st = TUSD_OK;
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) {
    *st = Fail(TUSD_ERR_INVALID_ARG, "invalid prim/name");
    return nullptr;
  }
  const n::Value* v = spec->property_value(std::string(name));
  if (!v) {
    *st = Fail(TUSD_ERR_NOT_FOUND,
               std::string("no authored value for property: ") + name);
    return nullptr;
  }
  return v;
}

std::string ArcString(const char* asset_path, const char* target_prim_path) {
  // Canonical encoding: "@asset@</prim>" / "@asset@" / "</prim>"
  std::string s;
  if (asset_path && asset_path[0]) {
    s += "@";
    s += asset_path;
    s += "@";
  }
  if (target_prim_path && target_prim_path[0]) {
    s += "<";
    s += target_prim_path;
    s += ">";
  }
  return s;
}

bool HasSuffixCI(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    char a = s[s.size() - n + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a += 32;
    if (a != b) return false;
  }
  return true;
}

void ApplyLoadOptions(const tusd_load_options* opts, n::LoadUSDOptions* lo,
                      lightusd::next::pcp::CompositionOptions* co) {
  if (!opts) return;
  lo->max_memory = static_cast<size_t>(opts->max_memory);
  lo->usda_options.parse_options.enable_usda_lazy_arrays =
      opts->enable_usda_lazy_arrays != 0;
  lo->usda_options.parse_options.max_usda_lazy_array_elements =
      static_cast<size_t>(opts->max_usda_lazy_array_elements);
  lo->usda_options.parse_options.num_threads = opts->usda_num_threads;
  if (co) {
    co->load_payloads = opts->load_payloads != 0;
    if (opts->max_depth) co->max_depth = opts->max_depth;
    for (size_t i = 0; i < opts->variant_override_count; ++i) {
      if (opts->variant_sets && opts->variant_names && opts->variant_sets[i] &&
          opts->variant_names[i]) {
        co->variant_overrides[opts->variant_sets[i]] = opts->variant_names[i];
      }
    }
  }
}

}  // namespace

// ============================================================
// Public API
// ============================================================

extern "C" {

// ------------------------------------------------------------
// Owned values
// ------------------------------------------------------------

void tusd_value_destroy(tusd_value* v) { delete v; }

tusd_status tusd_value_get_view(const tusd_value* v, tusd_value_view* out) {
  if (!v) return Fail(TUSD_ERR_INVALID_ARG, "value is null");
  return MakeView(v->v, out);
}

tusd_status tusd_value_get_string(const tusd_value* v, tusd_sv* out) {
  if (!v || !out) return Fail(TUSD_ERR_INVALID_ARG, "value/out is null");
  const std::string* s = v->v.as_string();
  if (!s) s = v->v.as_token();
  if (!s) s = v->v.as_asset_path();
  if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "value is not string-family");
  *out = SV(*s);
  return TUSD_OK;
}

tusd_status tusd_value_get_token_array(const tusd_value* v,
                                       tusd_strlist** out) {
  if (!v || !out) return Fail(TUSD_ERR_INVALID_ARG, "value/out is null");
  const std::vector<std::string>* arr = v->v.as_token_array();
  if (!arr) return Fail(TUSD_ERR_TYPE_MISMATCH, "value is not a token array");
  tusd_strlist* l = new (std::nothrow) tusd_strlist();
  if (!l) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  l->items = *arr;
  *out = l;
  return TUSD_OK;
}

// ------------------------------------------------------------
// Dictionary cursor
// ------------------------------------------------------------

int tusd_dict_is_valid(tusd_dict_ref d) { return d._dict != nullptr; }

size_t tusd_dict_size(tusd_dict_ref d) {
  const n::Dict* dict = static_cast<const n::Dict*>(d._dict);
  return dict ? dict->size() : 0;
}

static tusd_status DictOut(const std::string* key, const n::Value* v,
                           tusd_sv* out_key, tusd_value_view* val,
                           tusd_sv* sval, tusd_dict_ref* subdict) {
  if (out_key) *out_key = key ? SV(*key) : EmptySV();
  if (subdict) subdict->_dict = nullptr;
  if (sval) *sval = EmptySV();
  if (val) std::memset(val, 0, sizeof(*val));

  if (!v) return TUSD_OK;
  if (v->is_dictionary()) {
    if (subdict) subdict->_dict = v->as_dictionary();
    if (val) val->type = TUSD_TYPE_DICTIONARY;
    return TUSD_OK;
  }
  if (sval) {
    const std::string* s = v->as_string();
    if (!s) s = v->as_token();
    if (!s) s = v->as_asset_path();
    if (s) *sval = SV(*s);
  }
  if (val) return MakeView(*v, val);
  return TUSD_OK;
}

tusd_status tusd_dict_entry(tusd_dict_ref d, size_t index, tusd_sv* key,
                            tusd_value_view* val, tusd_sv* sval,
                            tusd_dict_ref* subdict) {
  const n::Dict* dict = static_cast<const n::Dict*>(d._dict);
  if (!dict) return Fail(TUSD_ERR_INVALID_ARG, "invalid dict");
  if (index >= dict->entries.size()) {
    return Fail(TUSD_ERR_NOT_FOUND, "dict index out of range");
  }
  const auto& kv = dict->entries[index];
  return DictOut(&kv.first, &kv.second, key, val, sval, subdict);
}

tusd_status tusd_dict_find(tusd_dict_ref d, const char* key,
                           tusd_value_view* val, tusd_sv* sval,
                           tusd_dict_ref* subdict) {
  const n::Dict* dict = static_cast<const n::Dict*>(d._dict);
  if (!dict || !key) return Fail(TUSD_ERR_INVALID_ARG, "invalid dict/key");
  const n::Value* v = dict->find(key);
  if (!v) return Fail(TUSD_ERR_NOT_FOUND, std::string("no dict key: ") + key);
  return DictOut(nullptr, v, nullptr, val, sval, subdict);
}

// ------------------------------------------------------------
// Stage: load / create / save
// ------------------------------------------------------------

void tusd_load_options_init(tusd_load_options* opts) {
  if (!opts) return;
  std::memset(opts, 0, sizeof(*opts));
  opts->struct_size = sizeof(*opts);
  opts->format = TUSD_FORMAT_AUTO;
  opts->composed = 1;
  opts->load_payloads = 1;
  opts->enable_usda_lazy_arrays = 0;
  opts->max_usda_lazy_array_elements = (static_cast<size_t>(1) << 30);
  opts->usda_num_threads = 0;
}

void tusd_save_options_init(tusd_save_options* opts) {
  if (!opts) return;
  std::memset(opts, 0, sizeof(*opts));
  opts->struct_size = sizeof(*opts);
  opts->format = TUSD_FORMAT_AUTO;
}

tusd_status tusd_stage_load(const char* filename,
                            const tusd_load_options* opts, tusd_stage** out) {
  if (!filename || !out) {
    return Fail(TUSD_ERR_INVALID_ARG, "filename/out is null");
  }
  *out = nullptr;

  n::LoadUSDOptions lo;
  lightusd::next::pcp::CompositionOptions co;
  // The binding's composed stage is consumed by value readers AND by
  // export/save round-trips: Native instancing would export instance prims as
  // empty husks (no prototype linkage in usda text). Holder mode keeps the
  // flattened layer self-contained: the prototype member holds the content,
  // other members reference it internally.
  co.flatten_instances = true;
  co.instance_flatten_mode = lightusd::next::pcp::InstanceFlattenMode::Holder;
  ApplyLoadOptions(opts, &lo, &co);
  const bool composed = opts ? opts->composed != 0 : true;
  const uint32_t format =
      opts ? opts->format : uint32_t(TUSD_FORMAT_AUTO);

  auto stage = std::unique_ptr<tusd_stage>(new (std::nothrow) tusd_stage());
  if (!stage) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");

  std::string warn, err;
  bool ok = false;
  if (format == TUSD_FORMAT_USDA) {
    ok = n::LoadUSDA(filename, &stage->stage, lo.usda_options, &warn, &err);
  } else if (format == TUSD_FORMAT_USDC) {
    ok = n::LoadUSDC(filename, &stage->stage, lo.usdc_options, &warn, &err);
  } else if (composed) {
    ok = n::LoadUSDComposed(filename, &stage->stage, lo, &warn, &err, &co);
  } else {
    ok = n::LoadUSD(filename, &stage->stage, lo, &warn, &err);
  }
  if (ok) {
    const std::string fn(filename ? filename : "");
    const size_t slash = fn.find_last_of("/\\");
    stage->source_dir = (slash == std::string::npos) ? "" : fn.substr(0, slash);
  }
  if (!ok) {
    // Distinguish IO from parse errors coarsely.
    const bool io = err.find("Failed to open") != std::string::npos ||
                    err.find("not found") != std::string::npos ||
                    err.find("Failed to read") != std::string::npos;
    return Fail(io ? TUSD_ERR_IO : TUSD_ERR_PARSE, err);
  }
  stage->warnings = std::move(warn);
  *out = stage.release();
  return TUSD_OK;
}

tusd_status tusd_stage_load_from_memory(const uint8_t* data, size_t size,
                                        const tusd_load_options* opts,
                                        tusd_stage** out) {
  if (!data || !size || !out) {
    return Fail(TUSD_ERR_INVALID_ARG, "data/out is null or empty");
  }
  *out = nullptr;

  n::LoadUSDOptions lo;
  ApplyLoadOptions(opts, &lo, nullptr);

  auto stage = std::unique_ptr<tusd_stage>(new (std::nothrow) tusd_stage());
  if (!stage) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");

  std::string warn, err;
  if (!n::LoadUSDFromMemory(data, size, &stage->stage, lo, &warn, &err)) {
    return Fail(TUSD_ERR_PARSE, err);
  }
  stage->warnings = std::move(warn);
  *out = stage.release();
  return TUSD_OK;
}

tusd_status tusd_stage_create(tusd_stage** out) {
  if (!out) return Fail(TUSD_ERR_INVALID_ARG, "out is null");
  tusd_stage* stage = new (std::nothrow) tusd_stage();
  if (!stage) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  n::Layer layer;
  layer.meta().metersPerUnit = 1.0;
  stage->stage.SetRootLayer(std::move(layer));
  *out = stage;
  return TUSD_OK;
}

void tusd_stage_destroy(tusd_stage* stage) { delete stage; }

tusd_status tusd_stage_take_warnings(tusd_stage* stage, tusd_string** out) {
  if (!stage || !out) return Fail(TUSD_ERR_INVALID_ARG, "stage/out is null");
  tusd_string* s = new (std::nothrow) tusd_string();
  if (!s) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  s->s = std::move(stage->warnings);
  stage->warnings.clear();
  *out = s;
  return TUSD_OK;
}

uint64_t tusd_stage_generation(const tusd_stage* stage) {
  return stage ? stage->generation.load(std::memory_order_acquire) : 0;
}

tusd_status tusd_stage_save(const tusd_stage* stage, const char* filename,
                            const tusd_save_options* opts) {
  if (!stage || !filename) {
    return Fail(TUSD_ERR_INVALID_ARG, "stage/filename is null");
  }
  uint32_t format = opts ? opts->format : uint32_t(TUSD_FORMAT_AUTO);
  if (format == TUSD_FORMAT_AUTO) {
    const std::string fn(filename);
    if (HasSuffixCI(fn, ".usda")) {
      format = TUSD_FORMAT_USDA;
    } else if (HasSuffixCI(fn, ".usdz")) {
      format = TUSD_FORMAT_USDZ;
    } else {
      format = TUSD_FORMAT_USDC;  // .usdc / .usd default
    }
  }

  std::string err;
  bool ok = false;
  if (format == TUSD_FORMAT_USDA) {
    ok = n::WriteUSDA(stage->stage, filename, &err);
  } else if (format == TUSD_FORMAT_USDC) {
    ok = n::WriteUSDC(stage->stage, filename, &err);
  } else if (format == TUSD_FORMAT_USDZ) {
    n::USDZWriteResult r = n::WriteUSDZToFile(filename, stage->stage);
    ok = r.success;
    err = r.error;
  } else {
    return Fail(TUSD_ERR_INVALID_ARG, "unknown save format");
  }
  if (!ok) return Fail(TUSD_ERR_IO, err);
  return TUSD_OK;
}

tusd_status tusd_stage_export_usda(const tusd_stage* stage,
                                   tusd_string** out) {
  if (!stage || !out) return Fail(TUSD_ERR_INVALID_ARG, "stage/out is null");
  tusd_string* s = new (std::nothrow) tusd_string();
  if (!s) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  s->s = n::WriteUSDAToString(stage->stage);
  *out = s;
  return TUSD_OK;
}

tusd_status tusd_stage_export_usdc(const tusd_stage* stage,
                                   tusd_string** out) {
  if (!stage || !out) return Fail(TUSD_ERR_INVALID_ARG, "stage/out is null");
  std::vector<uint8_t> buf;
  n::USDCWriteResult r = n::WriteUSDCToMemory(buf, stage->stage);
  if (!r.success) return Fail(TUSD_ERR_INTERNAL, r.error);
  tusd_string* s = new (std::nothrow) tusd_string();
  if (!s) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  s->s.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
  *out = s;
  return TUSD_OK;
}

tusd_status tusd_stage_flatten(const tusd_stage* stage, tusd_stage** out) {
  if (!stage || !out) return Fail(TUSD_ERR_INVALID_ARG, "stage/out is null");
  tusd_stage* flat = new (std::nothrow) tusd_stage();
  if (!flat) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  n::Layer layer = stage->stage.Flatten();
  flat->stage.SetRootLayer(std::move(layer));
  flat->stage.GetMeta() = stage->stage.GetMeta();
  *out = flat;
  return TUSD_OK;
}

tusd_status tusd_flatten_file_to_usdc(const char* in_filename,
                                      const char* out_filename,
                                      const tusd_load_options* opts) {
  if (!in_filename || !out_filename) {
    return Fail(TUSD_ERR_INVALID_ARG, "filenames are null");
  }
  n::pipeline::FlattenOptions fo;
  if (opts && opts->max_memory) {
    fo.read.max_memory = static_cast<size_t>(opts->max_memory);
  }
  if (opts) fo.composition.load_payloads = opts->load_payloads != 0;

  std::FILE* fp = std::fopen(out_filename, "wb");
  if (!fp) {
    return Fail(TUSD_ERR_IO,
                std::string("failed to open for write: ") + out_filename);
  }
  if (opts) {
    fo.composition.max_layer_memory = static_cast<size_t>(opts->max_memory);
    fo.composition.usda_parse_options.enable_usda_lazy_arrays =
        opts->enable_usda_lazy_arrays != 0;
    fo.composition.usda_parse_options.max_usda_lazy_array_elements =
        static_cast<size_t>(opts->max_usda_lazy_array_elements);
    fo.composition.usda_parse_options.num_threads = opts->usda_num_threads;
  }
  auto sink = [fp](const uint8_t* data, size_t size) -> bool {
    return std::fwrite(data, 1, size, fp) == size;
  };
  std::string err;
  const bool ok = n::pipeline::FlattenUSDFileToUSDCToSink(in_filename, sink,
                                                          fo, nullptr, &err);
  std::fclose(fp);
  if (!ok) {
    std::remove(out_filename);
    return Fail(TUSD_ERR_PARSE, err);
  }
  return TUSD_OK;
}

// ------------------------------------------------------------
// Stage metadata & stats
// ------------------------------------------------------------

tusd_status tusd_stage_get_metadata(const tusd_stage* stage, const char* key,
                                    tusd_value** out) {
  if (!stage || !key || !out) {
    return Fail(TUSD_ERR_INVALID_ARG, "stage/key/out is null");
  }
  const n::StageMeta& m = stage->stage.GetMeta();
  const std::string k(key);
  n::Value v;
  if (k == "defaultPrim") {
    v = n::Value::MakeToken(m.defaultPrim);
  } else if (k == "upAxis") {
    v = n::Value::MakeToken(m.upAxis);
  } else if (k == "metersPerUnit") {
    v = n::Value(m.metersPerUnit);
  } else if (k == "timeCodesPerSecond") {
    v = n::Value(m.timeCodesPerSecond);
  } else if (k == "startTimeCode") {
    v = n::Value(m.startTimeCode);
  } else if (k == "endTimeCode") {
    v = n::Value(m.endTimeCode);
  } else if (k == "framesPerSecond") {
    v = n::Value(m.framesPerSecond);
  } else if (k == "kilogramsPerUnit") {
    v = n::Value(m.kilogramsPerUnit);
  } else if (k == "doc") {
    v = n::Value(m.doc);
  } else if (k == "comment") {
    v = n::Value(m.comment);
  } else if (k == "colorConfiguration") {
    v = n::Value::MakeAssetPath(m.colorConfiguration);
  } else if (k == "colorManagementSystem") {
    v = n::Value::MakeToken(m.colorManagementSystem);
  } else {
    return Fail(TUSD_ERR_NOT_FOUND, "unknown stage metadata key: " + k);
  }
  tusd_value* res = NewValue(std::move(v));
  if (!res) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  *out = res;
  return TUSD_OK;
}

tusd_status tusd_stage_set_metadata(tusd_stage* stage, const char* key,
                                    tusd_type type, const void* data,
                                    size_t count) {
  if (!stage || !key) return Fail(TUSD_ERR_INVALID_ARG, "stage/key is null");
  n::Layer* layer = RootLayerOf(stage);
  if (!layer) return Fail(TUSD_ERR_INTERNAL, "stage has no root layer");

  n::Value v;
  if (!ValueFromRaw(type, count > 1 ? 1 : 0, data, count ? count : 1, &v)) {
    return TUSD_ERR_INVALID_ARG;
  }

  n::StageMeta& sm = stage->stage.GetMeta();
  n::LayerMeta& lm = layer->meta();
  const std::string k(key);

  auto as_str = [&]() -> const std::string* {
    const std::string* s = v.as_string();
    if (!s) s = v.as_token();
    if (!s) s = v.as_asset_path();
    return s;
  };
  auto as_dbl = [&]() -> bool {
    return v.as_double() != nullptr || v.as_float() != nullptr ||
           v.as_int() != nullptr;
  };
  auto dbl = [&]() -> double {
    if (const double* d = v.as_double()) return *d;
    if (const float* f = v.as_float()) return double(*f);
    if (const int32_t* i = v.as_int()) return double(*i);
    return 0.0;
  };

  if (k == "defaultPrim") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "defaultPrim expects a token");
    sm.defaultPrim = *s;
    lm.defaultPrim = *s;
  } else if (k == "upAxis") {
    const std::string* s = as_str();
    if (!s || (*s != "X" && *s != "Y" && *s != "Z")) {
      return Fail(TUSD_ERR_INVALID_ARG, "upAxis must be X, Y or Z");
    }
    sm.upAxis = *s;
    lm.upAxis = *s;
    sm.upAxis_set = lm.upAxis_set = true;
  } else if (k == "metersPerUnit") {
    if (!as_dbl()) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects double");
    sm.metersPerUnit = dbl();
    lm.metersPerUnit = dbl();
    sm.metersPerUnit_set = lm.metersPerUnit_set = true;
  } else if (k == "timeCodesPerSecond") {
    if (!as_dbl()) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects double");
    sm.timeCodesPerSecond = dbl();
    lm.timeCodesPerSecond = dbl();
    sm.timeCodesPerSecond_set = lm.timeCodesPerSecond_set = true;
  } else if (k == "startTimeCode") {
    if (!as_dbl()) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects double");
    sm.startTimeCode = dbl();
    lm.startTimeCode = dbl();
    sm.startTimeCode_set = lm.startTimeCode_set = true;
  } else if (k == "endTimeCode") {
    if (!as_dbl()) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects double");
    sm.endTimeCode = dbl();
    lm.endTimeCode = dbl();
    sm.endTimeCode_set = lm.endTimeCode_set = true;
  } else if (k == "framesPerSecond") {
    if (!as_dbl()) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects double");
    sm.framesPerSecond = dbl();
    sm.framesPerSecond_set = true;
    lm.framesPerSecond = dbl();
    lm.framesPerSecond_set = true;
  } else if (k == "kilogramsPerUnit") {
    if (!as_dbl()) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects double");
    sm.kilogramsPerUnit = dbl();
    sm.kilogramsPerUnit_set = true;
    lm.kilogramsPerUnit = dbl();
    lm.kilogramsPerUnit_set = true;
  } else if (k == "doc") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "doc expects a string");
    sm.doc = *s;
    lm.doc = *s;
  } else if (k == "comment") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "comment expects a string");
    sm.comment = *s;
    lm.comment = *s;
  } else if (k == "colorConfiguration") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects an asset path");
    sm.colorConfiguration = *s;
    lm.colorConfiguration = *s;
  } else if (k == "colorManagementSystem") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a token");
    sm.colorManagementSystem = *s;
    lm.colorManagementSystem = *s;
  } else {
    return Fail(TUSD_ERR_NOT_FOUND, "unknown stage metadata key: " + k);
  }
  return TUSD_OK;
}

tusd_sv tusd_stage_default_prim_path(const tusd_stage* stage) {
  if (!stage) return EmptySV();
  return SV(stage->stage.GetMeta().defaultPrim);
}

tusd_status tusd_stage_set_default_prim(tusd_stage* stage,
                                        const char* prim_name) {
  if (!stage || !prim_name) {
    return Fail(TUSD_ERR_INVALID_ARG, "stage/name is null");
  }
  return tusd_stage_set_metadata(stage, "defaultPrim", TUSD_TYPE_TOKEN,
                                 prim_name, 1);
}

tusd_status tusd_stage_sublayers(const tusd_stage* stage, tusd_strlist** out) {
  if (!stage || !out) return Fail(TUSD_ERR_INVALID_ARG, "stage/out is null");
  tusd_strlist* l = new (std::nothrow) tusd_strlist();
  if (!l) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  if (const n::Layer* layer = stage->stage.GetRootLayer()) {
    l->items = layer->meta().subLayers;
  }
  *out = l;
  return TUSD_OK;
}

tusd_status tusd_stage_add_sublayer_path(tusd_stage* stage,
                                         const char* asset_path) {
  if (!stage || !asset_path) {
    return Fail(TUSD_ERR_INVALID_ARG, "stage/path is null");
  }
  n::Layer* layer = RootLayerOf(stage);
  if (!layer) return Fail(TUSD_ERR_INTERNAL, "stage has no root layer");
  layer->meta().subLayers.push_back(asset_path);
  return TUSD_OK;
}

tusd_status tusd_stage_custom_layer_data(const tusd_stage* stage,
                                         tusd_dict_ref* out) {
  if (!stage || !out) return Fail(TUSD_ERR_INVALID_ARG, "stage/out is null");
  out->_dict = nullptr;
  const n::Layer* layer = stage->stage.GetRootLayer();
  if (!layer) return TUSD_OK;
  const n::Value& cld = layer->meta().customLayerData;
  if (cld.is_dictionary()) out->_dict = cld.as_dictionary();
  return TUSD_OK;
}

tusd_status tusd_stage_get_stats(const tusd_stage* stage,
                                 tusd_stage_stats* out) {
  if (!stage || !out) return Fail(TUSD_ERR_INVALID_ARG, "stage/out is null");
  n::Stage::Stats s = stage->stage.GetStats();
  out->prim_count = s.prim_count;
  out->layer_count = s.layer_count;
  out->total_properties = s.total_properties;
  out->memory_bytes = s.memory_bytes;
  return TUSD_OK;
}

double tusd_stage_start_timecode(const tusd_stage* stage) {
  return stage ? stage->stage.GetStartTimeCode() : 0.0;
}

double tusd_stage_end_timecode(const tusd_stage* stage) {
  return stage ? stage->stage.GetEndTimeCode() : 0.0;
}

// ------------------------------------------------------------
// Prim access & traversal
// ------------------------------------------------------------

int tusd_prim_is_valid(tusd_prim p) { return p._spec != nullptr; }

tusd_prim tusd_stage_pseudo_root(const tusd_stage* stage) {
  if (!stage) {
    tusd_prim p;
    std::memset(&p, 0, sizeof(p));
    return p;
  }
  return ToC(stage->stage.GetPseudoRoot());
}

tusd_prim tusd_stage_prim_at_path(const tusd_stage* stage, const char* path) {
  tusd_prim invalid;
  std::memset(&invalid, 0, sizeof(invalid));
  if (!stage || !path) return invalid;
  return ToC(stage->stage.GetPrimAtPath(std::string(path)));
}

tusd_prim tusd_stage_default_prim(const tusd_stage* stage) {
  tusd_prim invalid;
  std::memset(&invalid, 0, sizeof(invalid));
  if (!stage) return invalid;
  return ToC(stage->stage.GetDefaultPrim());
}

size_t tusd_stage_root_prim_count(const tusd_stage* stage) {
  if (!stage) return 0;
  const n::Layer* layer = stage->stage.GetRootLayer();
  return layer ? layer->root_indices().size() : 0;
}

tusd_prim tusd_stage_root_prim(const tusd_stage* stage, size_t index) {
  tusd_prim invalid;
  std::memset(&invalid, 0, sizeof(invalid));
  if (!stage) return invalid;
  const n::Layer* layer = stage->stage.GetRootLayer();
  if (!layer || index >= layer->root_indices().size()) return invalid;
  uint32_t idx = layer->root_indices()[index];
  const n::PrimSpec* spec = layer->prim(idx);
  if (!spec) return invalid;
  return ToC(n::UsdPrim(spec, layer, idx));
}

size_t tusd_stage_prim_count(const tusd_stage* stage) {
  return stage ? stage->stage.GetPrimCount() : 0;
}

tusd_sv tusd_prim_name(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return spec ? SV(spec->name()) : EmptySV();
}

tusd_sv tusd_prim_type_name(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return spec ? SV(spec->type_name()) : EmptySV();
}

tusd_sv tusd_prim_path(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return spec ? SV(spec->path().str()) : EmptySV();
}

uint8_t tusd_prim_specifier(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return spec ? static_cast<uint8_t>(spec->specifier()) : 0;
}

int tusd_prim_is_active(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return (spec && spec->meta().active) ? 1 : 0;
}

tusd_prim tusd_prim_parent(tusd_prim p) { return ToC(FromC(p).GetParent()); }

size_t tusd_prim_child_count(tusd_prim p) {
  return FromC(p).GetChildCount();
}

tusd_prim tusd_prim_child(tusd_prim p, size_t index) {
  return ToC(FromC(p).GetChildAt(index));
}

tusd_prim tusd_prim_child_by_name(tusd_prim p, const char* name) {
  tusd_prim invalid;
  std::memset(&invalid, 0, sizeof(invalid));
  if (!name) return invalid;
  return ToC(FromC(p).GetChild(std::string(name)));
}

// ------------------------------------------------------------
// Properties / attributes (read)
// ------------------------------------------------------------

size_t tusd_prim_property_count(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return spec ? spec->properties().size() : 0;
}

tusd_sv tusd_prim_property_name(tusd_prim p, size_t index) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || index >= spec->properties().size()) return EmptySV();
  const n::PropSlot& slot = spec->properties().slots()[index];
  return SV(n::GetPropNameTable().get(slot.name_id));
}

uint16_t tusd_prim_property_flags_at(tusd_prim p, size_t index) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || index >= spec->properties().size()) return 0;
  return spec->properties().slots()[index].flags;
}

int tusd_prim_has_property(tusd_prim p, const char* name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return 0;
  return spec->property(std::string(name)) != nullptr ? 1 : 0;
}

uint16_t tusd_prim_property_flags(tusd_prim p, const char* name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return 0;
  const n::PropSlot* slot = spec->property(std::string(name));
  return slot ? slot->flags : 0;
}

tusd_sv tusd_prim_property_type_name(tusd_prim p, const char* name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return EmptySV();
  const std::string* tn = spec->property_type_name(std::string(name));
  return tn ? SV(*tn) : EmptySV();
}

tusd_status tusd_attr_get(tusd_prim p, const char* name,
                          tusd_value_view* out) {
  tusd_status st;
  const n::Value* v = FindDefaultValue(p, name, &st);
  if (!v) return st;
  return MakeView(*v, out);
}

tusd_status tusd_attr_get_string(tusd_prim p, const char* name, tusd_sv* out) {
  if (!out) return Fail(TUSD_ERR_INVALID_ARG, "out is null");
  tusd_status st;
  const n::Value* v = FindDefaultValue(p, name, &st);
  if (!v) return st;
  const std::string* s = v->as_string();
  if (!s) s = v->as_token();
  if (!s) s = v->as_asset_path();
  if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "property is not string-family");
  *out = SV(*s);
  return TUSD_OK;
}

tusd_status tusd_attr_get_token_array(tusd_prim p, const char* name,
                                      tusd_strlist** out) {
  if (!out) return Fail(TUSD_ERR_INVALID_ARG, "out is null");
  tusd_status st;
  const n::Value* v = FindDefaultValue(p, name, &st);
  if (!v) return st;
  const std::vector<std::string>* arr = v->as_token_array();
  if (!arr) {
    return Fail(TUSD_ERR_TYPE_MISMATCH, "property is not a token array");
  }
  tusd_strlist* l = new (std::nothrow) tusd_strlist();
  if (!l) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  l->items = *arr;
  *out = l;
  return TUSD_OK;
}

tusd_status tusd_attr_metadata(tusd_prim p, const char* name, const char* key,
                               tusd_value** out) {
  if (!name || !key || !out) {
    return Fail(TUSD_ERR_INVALID_ARG, "name/key/out is null");
  }
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  const n::PropMeta* meta = spec->property_meta(std::string(name));
  if (!meta) {
    return Fail(TUSD_ERR_NOT_FOUND, "no property metadata authored");
  }

  const std::string k(key);
  n::Value v;
  if (k == "interpolation" && (meta->authored & n::PropMeta::kInterpolation)) {
    v = n::Value::MakeToken(meta->interpolation);
  } else if (k == "elementSize" &&
             (meta->authored & n::PropMeta::kElementSize)) {
    v = n::Value(meta->elementSize);
  } else if (k == "colorSpace" && (meta->authored & n::PropMeta::kColorSpace)) {
    v = n::Value::MakeToken(meta->colorSpace);
  } else if (k == "displayName" &&
             (meta->authored & n::PropMeta::kDisplayName)) {
    v = n::Value(meta->displayName);
  } else if (k == "displayGroup" &&
             (meta->authored & n::PropMeta::kDisplayGroup)) {
    v = n::Value(meta->displayGroup);
  } else if (k == "doc" && (meta->authored & n::PropMeta::kDoc)) {
    v = n::Value(meta->doc);
  } else if (k == "hidden" && (meta->authored & n::PropMeta::kHidden)) {
    v = n::Value(meta->hidden);
  } else if (k == "renderType" && (meta->authored & n::PropMeta::kRenderType)) {
    v = n::Value::MakeToken(meta->renderType);
  } else if (k == "connectability" &&
             (meta->authored & n::PropMeta::kConnectability)) {
    v = n::Value::MakeToken(meta->connectability);
  } else if (k == "bindMaterialAs" &&
             (meta->authored & n::PropMeta::kBindMaterialAs)) {
    v = n::Value::MakeToken(meta->bindMaterialAs);
  } else if (k == "weight" && (meta->authored & n::PropMeta::kWeight)) {
    v = n::Value(meta->weight);
  } else {
    return Fail(TUSD_ERR_NOT_FOUND, "property metadata key not authored: " + k);
  }
  tusd_value* res = NewValue(std::move(v));
  if (!res) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  *out = res;
  return TUSD_OK;
}

tusd_status tusd_attr_custom_data(tusd_prim p, const char* name,
                                  tusd_dict_ref* out) {
  if (!name || !out) return Fail(TUSD_ERR_INVALID_ARG, "name/out is null");
  out->_dict = nullptr;
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  const n::PropMeta* meta = spec->property_meta(std::string(name));
  if (meta && meta->customData.is_dictionary()) {
    out->_dict = meta->customData.as_dictionary();
  }
  return TUSD_OK;
}

size_t tusd_attr_connection_count(tusd_prim p, const char* name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return 0;
  const std::vector<n::Path>* targets = spec->connection(std::string(name));
  return targets ? targets->size() : 0;
}

tusd_sv tusd_attr_connection(tusd_prim p, const char* name, size_t index) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return EmptySV();
  const std::vector<n::Path>* targets = spec->connection(std::string(name));
  if (!targets || index >= targets->size()) return EmptySV();
  return SV((*targets)[index].str());
}

tusd_status tusd_attr_eval(const tusd_stage* stage, tusd_prim p,
                           const char* name, double time, tusd_value** out) {
  if (!stage || !name || !out) {
    return Fail(TUSD_ERR_INVALID_ARG, "stage/name/out is null");
  }
  n::AttributeEval eval(&stage->stage);
  eval.SetTime(time);
  n::EvalResult r = eval.Eval(FromC(p), std::string(name));
  if (!r.success) {
    return Fail(TUSD_ERR_NOT_FOUND,
                std::string("attribute evaluation failed for: ") + name);
  }
  tusd_value* res = NewValue(std::move(r.value));
  if (!res) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  *out = res;
  return TUSD_OK;
}

tusd_status tusd_prim_local_transform(tusd_prim p, double time,
                                      double out16[16]) {
  if (!out16) return Fail(TUSD_ERR_INVALID_ARG, "out is null");
  n::UsdPrim prim = FromC(p);
  if (!prim.IsValid()) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  if (!lightusd::tydra::next::ComputeLocalTransform(prim, out16, time)) {
    return Fail(TUSD_ERR_INTERNAL, "failed to compute local transform");
  }
  return TUSD_OK;
}

tusd_status tusd_prim_world_transform(const tusd_stage* stage, tusd_prim p,
                                      double time, double out16[16]) {
  if (!stage || !out16) {
    return Fail(TUSD_ERR_INVALID_ARG, "stage/out is null");
  }
  n::UsdPrim prim = FromC(p);
  if (!prim.IsValid()) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  if (!lightusd::tydra::next::ComputeWorldTransform(stage->stage, prim, out16,
                                                    time)) {
    return Fail(TUSD_ERR_INTERNAL, "failed to compute world transform");
  }
  return TUSD_OK;
}

// ------------------------------------------------------------
// Time samples
// ------------------------------------------------------------

static const std::vector<std::pair<double, uint32_t>>* SamplesOf(
    tusd_prim p, const char* name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return nullptr;
  n::PropNameId id = n::GetPropNameTable().find(name);
  if (!id.is_valid()) return nullptr;
  return spec->time_samples(id);
}

int tusd_attr_has_timesamples(tusd_prim p, const char* name) {
  const auto* samples = SamplesOf(p, name);
  return (samples && !samples->empty()) ? 1 : 0;
}

size_t tusd_attr_timesample_count(tusd_prim p, const char* name) {
  const auto* samples = SamplesOf(p, name);
  return samples ? samples->size() : 0;
}

size_t tusd_attr_timesample_times(tusd_prim p, const char* name, double* out,
                                  size_t cap) {
  const auto* samples = SamplesOf(p, name);
  if (!samples) return 0;
  if (out) {
    const size_t ncopy = std::min(cap, samples->size());
    for (size_t i = 0; i < ncopy; ++i) out[i] = (*samples)[i].first;
  }
  return samples->size();
}

tusd_status tusd_attr_timesample_at(tusd_prim p, const char* name,
                                    size_t index, double* time,
                                    tusd_value_view* out) {
  const auto* samples = SamplesOf(p, name);
  if (!samples) {
    return Fail(TUSD_ERR_NOT_FOUND,
                std::string("no time samples for: ") + (name ? name : ""));
  }
  if (index >= samples->size()) {
    return Fail(TUSD_ERR_NOT_FOUND, "time sample index out of range");
  }
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  const n::Value* v = spec->time_sample_value((*samples)[index].second);
  if (!v) return Fail(TUSD_ERR_INTERNAL, "sample value missing");
  if (time) *time = (*samples)[index].first;
  return MakeView(*v, out);
}

tusd_status tusd_attr_interpolate(tusd_prim p, const char* name, double time,
                                  uint8_t interp_mode, tusd_value** out) {
  if (!name || !out) return Fail(TUSD_ERR_INVALID_ARG, "name/out is null");
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  n::SampleResult r = spec->interpolate_time_sample(
      std::string(name), time,
      interp_mode == 0 ? n::TimeInterpolation::Held
                       : n::TimeInterpolation::Linear);
  if (!r.success) {
    return Fail(TUSD_ERR_NOT_FOUND,
                std::string("no time samples for: ") + name);
  }
  tusd_value* res = NewValue(std::move(r.value));
  if (!res) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  *out = res;
  return TUSD_OK;
}

// ------------------------------------------------------------
// Relationships
// ------------------------------------------------------------

size_t tusd_prim_relationship_count(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return spec ? spec->relationship_names().size() : 0;
}

tusd_status tusd_prim_relationship_names(tusd_prim p, tusd_strlist** out) {
  if (!out) return Fail(TUSD_ERR_INVALID_ARG, "out is null");
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  tusd_strlist* l = new (std::nothrow) tusd_strlist();
  if (!l) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  l->items = spec->relationship_names();
  *out = l;
  return TUSD_OK;
}

int tusd_prim_has_relationship(tusd_prim p, const char* name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return 0;
  return spec->relationship(std::string(name)) != nullptr ? 1 : 0;
}

size_t tusd_rel_target_count(tusd_prim p, const char* name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return 0;
  const std::vector<n::Path>* targets = spec->relationship(std::string(name));
  return targets ? targets->size() : 0;
}

tusd_sv tusd_rel_target(tusd_prim p, const char* name, size_t index) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !name) return EmptySV();
  const std::vector<n::Path>* targets = spec->relationship(std::string(name));
  if (!targets || index >= targets->size()) return EmptySV();
  return SV((*targets)[index].str());
}

// ------------------------------------------------------------
// Variants (read)
// ------------------------------------------------------------

static const n::VariantSetData* FindVariantSet(tusd_prim p,
                                               const char* set_name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !set_name) return nullptr;
  for (const n::VariantSetData& vs : spec->meta().variantSets()) {
    if (vs.name == set_name) return &vs;
  }
  return nullptr;
}

size_t tusd_prim_variant_set_count(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return spec ? spec->meta().variantSets().size() : 0;
}

tusd_sv tusd_prim_variant_set_name(tusd_prim p, size_t set_index) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || set_index >= spec->meta().variantSets().size()) {
    return EmptySV();
  }
  return SV(spec->meta().variantSets()[set_index].name);
}

size_t tusd_variant_count(tusd_prim p, const char* set_name) {
  const n::VariantSetData* vs = FindVariantSet(p, set_name);
  return vs ? vs->variants.size() : 0;
}

tusd_sv tusd_variant_name(tusd_prim p, const char* set_name, size_t index) {
  const n::VariantSetData* vs = FindVariantSet(p, set_name);
  if (!vs || index >= vs->variants.size()) return EmptySV();
  return SV(vs->variants[index].name);
}

tusd_sv tusd_variant_selection(tusd_prim p, const char* set_name) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec || !set_name) return EmptySV();
  // Explicit selections list wins over the set's own `selected`.
  for (const auto& sel : spec->meta().variantSelections()) {
    if (sel.first == set_name) return SV(sel.second);
  }
  const n::VariantSetData* vs = FindVariantSet(p, set_name);
  if (vs) return SV(vs->selected);
  return EmptySV();
}

// ------------------------------------------------------------
// Prim metadata
// ------------------------------------------------------------

tusd_status tusd_prim_get_metadata(tusd_prim p, const char* key,
                                   tusd_value** out) {
  if (!key || !out) return Fail(TUSD_ERR_INVALID_ARG, "key/out is null");
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  const n::PrimSpecMeta& m = spec->meta();
  const std::string k(key);
  n::Value v;
  if (k == "active") {
    v = n::Value(m.active);
  } else if (k == "hidden") {
    v = n::Value(m.hidden);
  } else if (k == "instanceable") {
    v = n::Value(m.instanceable);
  } else if (k == "kind") {
    if (m.kind().empty()) return Fail(TUSD_ERR_NOT_FOUND, "kind unauthored");
    v = n::Value::MakeToken(m.kind());
  } else if (k == "doc") {
    if (m.doc().empty()) return Fail(TUSD_ERR_NOT_FOUND, "doc unauthored");
    v = n::Value(m.doc());
  } else if (k == "comment") {
    if (m.comment().empty()) {
      return Fail(TUSD_ERR_NOT_FOUND, "comment unauthored");
    }
    v = n::Value(m.comment());
  } else if (k == "displayName") {
    if (m.displayName().empty()) {
      return Fail(TUSD_ERR_NOT_FOUND, "displayName unauthored");
    }
    v = n::Value(m.displayName());
  } else if (k == "apiSchemas") {
    if (m.apiSchemas().empty()) {
      return Fail(TUSD_ERR_NOT_FOUND, "apiSchemas unauthored");
    }
    v = n::Value::MakeTokenArray(m.apiSchemas());
  } else {
    return Fail(TUSD_ERR_NOT_FOUND, "unknown prim metadata key: " + k);
  }
  tusd_value* res = NewValue(std::move(v));
  if (!res) return Fail(TUSD_ERR_OUT_OF_MEMORY, "alloc failed");
  *out = res;
  return TUSD_OK;
}

tusd_status tusd_prim_custom_data(tusd_prim p, tusd_dict_ref* out) {
  if (!out) return Fail(TUSD_ERR_INVALID_ARG, "out is null");
  out->_dict = nullptr;
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  const n::Value& cd = spec->meta().customData();
  if (cd.is_dictionary()) out->_dict = cd.as_dictionary();
  return TUSD_OK;
}

tusd_status tusd_prim_asset_info(tusd_prim p, tusd_dict_ref* out) {
  if (!out) return Fail(TUSD_ERR_INVALID_ARG, "out is null");
  out->_dict = nullptr;
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  if (!spec) return Fail(TUSD_ERR_INVALID_ARG, "invalid prim");
  const n::Value& ai = spec->meta().assetInfo();
  if (ai.is_dictionary()) out->_dict = ai.as_dictionary();
  return TUSD_OK;
}

tusd_sv tusd_prim_kind(tusd_prim p) {
  const n::PrimSpec* spec = static_cast<const n::PrimSpec*>(p._spec);
  return spec ? SV(spec->meta().kind()) : EmptySV();
}

// ------------------------------------------------------------
// Authoring
// ------------------------------------------------------------

tusd_status tusd_stage_define_prim(tusd_stage* stage, const char* path,
                                   const char* type_name, uint8_t specifier,
                                   tusd_prim* out) {
  if (out) std::memset(out, 0, sizeof(*out));
  if (!stage || !path) return Fail(TUSD_ERR_INVALID_ARG, "stage/path is null");
  if (specifier > 2) return Fail(TUSD_ERR_INVALID_ARG, "invalid specifier");
  n::Layer* layer = RootLayerOf(stage);
  if (!layer) return Fail(TUSD_ERR_INTERNAL, "stage has no root layer");

  uint32_t idx = layer->define_prim_at_path(
      path, type_name ? type_name : "",
      static_cast<n::PrimSpecifier>(specifier));
  if (idx == UINT32_MAX) {
    return Fail(TUSD_ERR_INVALID_ARG,
                std::string("invalid prim path: ") + path);
  }
  stage->generation.fetch_add(1, std::memory_order_acq_rel);
  if (out) {
    *out = ToC(n::UsdPrim(layer->prim(idx), layer, idx));
  }
  return TUSD_OK;
}

tusd_status tusd_stage_remove_prim(tusd_stage* stage, const char* path) {
  if (!stage || !path) return Fail(TUSD_ERR_INVALID_ARG, "stage/path is null");
  n::Layer* layer = RootLayerOf(stage);
  if (!layer) return Fail(TUSD_ERR_INTERNAL, "stage has no root layer");
  if (!layer->remove_prim_at_path(path)) {
    return Fail(TUSD_ERR_NOT_FOUND, std::string("no prim at path: ") + path);
  }
  stage->generation.fetch_add(1, std::memory_order_acq_rel);
  return TUSD_OK;
}

tusd_status tusd_attr_set(tusd_stage* stage, const char* prim_path,
                          const char* name, tusd_type type, uint8_t is_array,
                          const void* data, size_t count, uint16_t flags) {
  if (!name) return Fail(TUSD_ERR_INVALID_ARG, "name is null");
  if (!IsValidAttrName(name)) {
    return Fail(TUSD_ERR_INVALID_ARG, "name is not a valid identifier");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;

  n::Value v;
  if (!ValueFromRaw(type, is_array, data, count, &v)) {
    return TUSD_ERR_INVALID_ARG;
  }
  const bool array = v.is_array();
  spec->upsert_property(std::string(name), std::move(v),
                        flags & (TUSD_PROP_CUSTOM | TUSD_PROP_UNIFORM));
  RecordTypeName(spec, name, type, array);
  return TUSD_OK;
}

tusd_status tusd_attr_set_token_array(tusd_stage* stage, const char* prim_path,
                                      const char* name, tusd_type type,
                                      const char* const* items, size_t count,
                                      uint16_t flags) {
  if (!name || (!items && count)) {
    return Fail(TUSD_ERR_INVALID_ARG, "name/items is null");
  }
  if (!IsValidAttrName(name)) {
    return Fail(TUSD_ERR_INVALID_ARG, "name is not a valid identifier");
  }
  if (type != TUSD_TYPE_TOKEN && type != TUSD_TYPE_STRING) {
    return Fail(TUSD_ERR_INVALID_ARG, "type must be token or string");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;

  std::vector<std::string> tokens;
  tokens.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    tokens.emplace_back(items[i] ? items[i] : "");
  }
  spec->upsert_property(std::string(name),
                        n::Value::MakeTokenArray(std::move(tokens)),
                        flags & (TUSD_PROP_CUSTOM | TUSD_PROP_UNIFORM));
  RecordTypeName(spec, name, type, true);
  return TUSD_OK;
}

tusd_status tusd_attr_set_timesample(tusd_stage* stage, const char* prim_path,
                                     const char* name, double time,
                                     tusd_type type, uint8_t is_array,
                                     const void* data, size_t count) {
  if (!name) return Fail(TUSD_ERR_INVALID_ARG, "name is null");
  if (!IsValidAttrName(name)) {
    return Fail(TUSD_ERR_INVALID_ARG, "name is not a valid identifier");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;

  n::Value v;
  if (!ValueFromRaw(type, is_array, data, count, &v)) {
    return TUSD_ERR_INVALID_ARG;
  }
  const bool array = v.is_array();
  const n::TypeId tid = v.type_id();

  n::PropNameId name_id = n::GetPropNameTable().intern(name);
  spec->add_time_sample(name_id, time, std::move(v));

  // Ensure the slot exists and carries the time-sampled flag (mirrors
  // LayerBuilder::add_time_sample).
  const n::PropSlot* existing = spec->property(name_id);
  if (!existing) {
    uint16_t slot_flags = n::PropSlot::kFlagTimeSampled;
    if (array) slot_flags |= n::PropSlot::kFlagArray;
    spec->add_property_slot(name_id, tid, slot_flags);
  } else {
    spec->mark_property_time_sampled(name_id);
  }
  RecordTypeName(spec, name, type, array);
  return TUSD_OK;
}

tusd_status tusd_attr_set_metadata(tusd_stage* stage, const char* prim_path,
                                   const char* name, const char* key,
                                   tusd_type type, const void* data,
                                   size_t count) {
  if (!name || !key) return Fail(TUSD_ERR_INVALID_ARG, "name/key is null");
  if (!IsValidAttrName(name)) {
    return Fail(TUSD_ERR_INVALID_ARG, "name is not a valid identifier");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;

  n::Value v;
  if (!ValueFromRaw(type, count > 1 ? 1 : 0, data, count ? count : 1, &v)) {
    return TUSD_ERR_INVALID_ARG;
  }
  auto as_str = [&]() -> const std::string* {
    const std::string* s = v.as_string();
    if (!s) s = v.as_token();
    if (!s) s = v.as_asset_path();
    return s;
  };

  n::PropMeta& meta = spec->ensure_property_meta(std::string(name));
  const std::string k(key);
  if (k == "interpolation") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a token");
    meta.interpolation = *s;
    meta.authored |= n::PropMeta::kInterpolation;
  } else if (k == "elementSize") {
    const int32_t* i = v.as_int();
    if (!i) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects an int");
    meta.elementSize = *i;
    meta.authored |= n::PropMeta::kElementSize;
  } else if (k == "colorSpace") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a token");
    meta.colorSpace = *s;
    meta.authored |= n::PropMeta::kColorSpace;
  } else if (k == "displayName") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a string");
    meta.displayName = *s;
    meta.authored |= n::PropMeta::kDisplayName;
  } else if (k == "displayGroup") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a string");
    meta.displayGroup = *s;
    meta.authored |= n::PropMeta::kDisplayGroup;
  } else if (k == "doc") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a string");
    meta.doc = *s;
    meta.authored |= n::PropMeta::kDoc;
  } else if (k == "hidden") {
    const bool* b = v.as_bool();
    if (!b) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a bool");
    meta.hidden = *b;
    meta.authored |= n::PropMeta::kHidden;
  } else if (k == "weight") {
    const double* d = v.as_double();
    if (!d) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a double");
    meta.weight = *d;
    meta.authored |= n::PropMeta::kWeight;
  } else {
    return Fail(TUSD_ERR_NOT_FOUND, "unknown property metadata key: " + k);
  }
  return TUSD_OK;
}

tusd_status tusd_attr_add_connection(tusd_stage* stage, const char* prim_path,
                                     const char* name, const char* target) {
  if (!name || !target) {
    return Fail(TUSD_ERR_INVALID_ARG, "name/target is null");
  }
  if (!IsValidAttrName(name)) {
    return Fail(TUSD_ERR_INVALID_ARG, "name is not a valid identifier");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;
  spec->add_connection(name, n::Path(target));
  // Ensure a slot flagged as connection exists so the writer emits it.
  n::PropNameId name_id = n::GetPropNameTable().intern(name);
  const n::PropSlot* slot = spec->property(name_id);
  if (!slot) {
    spec->add_property_slot(name_id, n::TypeId::Invalid,
                            n::PropSlot::kFlagConnection);
  }
  return TUSD_OK;
}

tusd_status tusd_attr_block(tusd_stage* stage, const char* prim_path,
                            const char* name) {
  if (!name) return Fail(TUSD_ERR_INVALID_ARG, "name is null");
  if (!IsValidAttrName(name)) {
    return Fail(TUSD_ERR_INVALID_ARG, "name is not a valid identifier");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;
  spec->upsert_property(std::string(name), n::Value::MakeBlock(), 0);
  return TUSD_OK;
}

tusd_status tusd_attr_remove(tusd_stage* stage, const char* prim_path,
                             const char* name) {
  if (!name) return Fail(TUSD_ERR_INVALID_ARG, "name is null");
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;
  if (!spec->remove_property(std::string(name))) {
    return Fail(TUSD_ERR_NOT_FOUND, std::string("no property: ") + name);
  }
  return TUSD_OK;
}

tusd_status tusd_rel_add_target(tusd_stage* stage, const char* prim_path,
                                const char* rel_name, const char* target) {
  if (!rel_name || !target) {
    return Fail(TUSD_ERR_INVALID_ARG, "rel_name/target is null");
  }
  if (!IsValidAttrName(rel_name)) {
    return Fail(TUSD_ERR_INVALID_ARG, "rel_name is not a valid identifier");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;
  spec->add_relationship(rel_name, n::Path(target));
  return TUSD_OK;
}

tusd_status tusd_rel_set_targets(tusd_stage* stage, const char* prim_path,
                                 const char* rel_name,
                                 const char* const* targets, size_t count) {
  if (!rel_name || (!targets && count)) {
    return Fail(TUSD_ERR_INVALID_ARG, "rel_name/targets is null");
  }
  if (!IsValidAttrName(rel_name)) {
    return Fail(TUSD_ERR_INVALID_ARG, "rel_name is not a valid identifier");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;
  std::vector<n::Path> paths;
  paths.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    paths.emplace_back(targets[i] ? targets[i] : "");
  }
  spec->set_relationship_targets(rel_name, std::move(paths));
  return TUSD_OK;
}

tusd_status tusd_rel_remove(tusd_stage* stage, const char* prim_path,
                            const char* rel_name) {
  if (!rel_name) return Fail(TUSD_ERR_INVALID_ARG, "rel_name is null");
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;
  if (!spec->remove_relationship(rel_name)) {
    return Fail(TUSD_ERR_NOT_FOUND,
                std::string("no relationship: ") + rel_name);
  }
  return TUSD_OK;
}

tusd_status tusd_prim_add_arc(tusd_stage* stage, const char* prim_path,
                              uint8_t arc_type, const char* asset_path,
                              const char* target_prim_path) {
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;

  const std::string arc = ArcString(asset_path, target_prim_path);
  if (arc.empty()) {
    return Fail(TUSD_ERR_INVALID_ARG,
                "arc needs an asset path and/or a target prim path");
  }
  switch (arc_type) {
    case TUSD_ARC_REFERENCE:
      spec->meta().references.push_back(arc);
      break;
    case TUSD_ARC_PAYLOAD:
      spec->meta().payloads.push_back(arc);
      break;
    case TUSD_ARC_INHERIT:
      spec->meta().inherits.push_back(arc);
      break;
    case TUSD_ARC_SPECIALIZE:
      spec->meta().specializes.push_back(arc);
      break;
    default:
      return Fail(TUSD_ERR_INVALID_ARG, "unknown arc type");
  }
  return TUSD_OK;
}

tusd_status tusd_prim_set_metadata(tusd_stage* stage, const char* prim_path,
                                   const char* key, tusd_type type,
                                   const void* data, size_t count) {
  if (!key) return Fail(TUSD_ERR_INVALID_ARG, "key is null");
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;

  n::Value v;
  if (!ValueFromRaw(type, count > 1 ? 1 : 0, data, count ? count : 1, &v)) {
    return TUSD_ERR_INVALID_ARG;
  }
  auto as_str = [&]() -> const std::string* {
    const std::string* s = v.as_string();
    if (!s) s = v.as_token();
    if (!s) s = v.as_asset_path();
    return s;
  };

  n::PrimSpecMeta& m = spec->meta();
  const std::string k(key);
  if (k == "active") {
    const bool* b = v.as_bool();
    if (!b) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a bool");
    m.active = *b;
    m.active_authored = true;
  } else if (k == "hidden") {
    const bool* b = v.as_bool();
    if (!b) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a bool");
    m.hidden = *b;
    m.hidden_authored = true;
  } else if (k == "instanceable") {
    const bool* b = v.as_bool();
    if (!b) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a bool");
    m.instanceable = *b;
  } else if (k == "kind") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a token");
    m.kind() = *s;
  } else if (k == "doc") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a string");
    m.doc() = *s;
  } else if (k == "comment") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a string");
    m.comment() = *s;
  } else if (k == "displayName") {
    const std::string* s = as_str();
    if (!s) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a string");
    m.displayName() = *s;
  } else if (k == "apiSchemas") {
    const std::vector<std::string>* arr = v.as_token_array();
    if (!arr) return Fail(TUSD_ERR_TYPE_MISMATCH, "expects a token array");
    m.apiSchemas() = *arr;
  } else {
    return Fail(TUSD_ERR_NOT_FOUND, "unknown prim metadata key: " + k);
  }
  return TUSD_OK;
}

tusd_status tusd_prim_add_variant_set(tusd_stage* stage, const char* prim_path,
                                      const char* set_name) {
  if (!set_name || !set_name[0]) {
    return Fail(TUSD_ERR_INVALID_ARG, "set_name is null/empty");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;
  for (const n::VariantSetData& vs : spec->meta().variantSets()) {
    if (vs.name == set_name) return TUSD_OK;  // idempotent
  }
  n::VariantSetData vs;
  vs.name = set_name;
  spec->meta().variantSets().push_back(std::move(vs));
  return TUSD_OK;
}

tusd_status tusd_prim_add_variant(tusd_stage* stage, const char* prim_path,
                                  const char* set_name,
                                  const char* variant_name) {
  if (!set_name || !variant_name || !variant_name[0]) {
    return Fail(TUSD_ERR_INVALID_ARG, "set/variant name is null/empty");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;
  tusd_status vst = tusd_prim_add_variant_set(stage, prim_path, set_name);
  if (vst != TUSD_OK) return vst;
  for (n::VariantSetData& vs : spec->meta().variantSets()) {
    if (vs.name != set_name) continue;
    for (const n::VariantData& v : vs.variants) {
      if (v.name == variant_name) return TUSD_OK;  // idempotent
    }
    n::VariantData v;
    v.name = variant_name;
    vs.variants.push_back(std::move(v));
    return TUSD_OK;
  }
  return Fail(TUSD_ERR_INTERNAL, "variant set vanished");
}

tusd_status tusd_prim_set_variant_selection(tusd_stage* stage,
                                            const char* prim_path,
                                            const char* set_name,
                                            const char* variant_name) {
  if (!set_name || !variant_name) {
    return Fail(TUSD_ERR_INVALID_ARG, "set/variant name is null");
  }
  tusd_status st;
  n::PrimSpec* spec = MutablePrimAt(stage, prim_path, &st);
  if (!spec) return st;

  for (n::VariantSetData& vs : spec->meta().variantSets()) {
    if (vs.name == set_name) {
      vs.selected = variant_name;
    }
  }
  auto& sels = spec->meta().variantSelections();
  for (auto& sel : sels) {
    if (sel.first == set_name) {
      sel.second = variant_name;
      return TUSD_OK;
    }
  }
  sels.emplace_back(set_name, variant_name);
  return TUSD_OK;
}

}  // extern "C"
