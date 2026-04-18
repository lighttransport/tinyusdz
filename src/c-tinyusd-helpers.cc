// SPDX-License-Identifier: Apache 2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// Small C-callable helpers that complement c-tinyusd.cc for the Python
// extension. Kept in a separate translation unit so the header surface for
// the Python extension stays tidy.

#include "c-tinyusd-helpers.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "tinyusdz.hh"
#include "tydra/scene-access.hh"
#include "usda-writer.hh"
#include "usdc-writer.hh"
#include "value-types.hh"

namespace {

using tinyusdz::Attribute;
using tinyusdz::Path;
using tinyusdz::Prim;
using tinyusdz::Property;
using tinyusdz::Relationship;
using tinyusdz::Stage;

inline const Stage *S(const CTinyUSDStage *s) {
  return reinterpret_cast<const Stage *>(s);
}
inline Stage *Sm(CTinyUSDStage *s) { return reinterpret_cast<Stage *>(s); }
inline const Prim *P(const CTinyUSDPrim *p) {
  return reinterpret_cast<const Prim *>(p);
}
inline const Attribute *A(const CTinyUSDAttribute *a) {
  return reinterpret_cast<const Attribute *>(a);
}
inline Attribute *Am(CTinyUSDAttribute *a) {
  return reinterpret_cast<Attribute *>(a);
}
inline const tinyusdz::value::Value *V(const CTinyUSDValue *v) {
  return reinterpret_cast<const tinyusdz::value::Value *>(v);
}

inline bool set_err(c_tinyusd_string_t *err, const std::string &msg) {
  if (err) c_tinyusd_string_replace(err, msg.c_str());
  return false;
}

//---------------------------------------------------------------------------
// Zero-copy array access.
//
// For each supported USD value type, we produce:
//   - a pointer into the underlying std::vector<T> storage
//   - the outer element count (N)
//   - the number of inner components per element (1 for scalar arrays,
//     2/3/4/16 for vectors, quats, matrices)
//   - the size of one inner component, in bytes
//   - a single-char Python struct format for the inner component
//
// The Python buffer-protocol path in module.c assembles shape/strides from
// those numbers. This lets us expose a `point3f[]` as a 2-D (N, 3) float32
// buffer with zero copies.
//
// Role types (point3f, color3f, normal3f, texCoord2f, etc.) are POD wrappers
// around 2/3/4 packed floats/doubles/halves; their std::vector<> data() is
// bit-compatible with a flat float[]/double[]/half[] of size N*K.
//
// std::vector<bool> is specialised and has no contiguous data(), so we skip
// it; bool[] can still be read via scalar value-conversion APIs.
//---------------------------------------------------------------------------

struct ArrayView {
  const void *ptr;
  uint64_t n_outer;
  uint32_t n_inner;
  uint32_t component_size;
  const char *format;
};

// Returns true if `v` holds a std::vector<T> and fills `out`.
template <typename T>
inline bool try_vector_raw(const tinyusdz::value::any_value *av,
                           const void **ptr, uint64_t *n) {
  auto pv = tinyusdz::value::any_value_cast<const std::vector<T>>(av);
  if (!pv) return false;
  *ptr = static_cast<const void *>(pv->data());
  *n = static_cast<uint64_t>(pv->size());
  return true;
}

template <typename T>
inline bool plain_scalar_array(const tinyusdz::value::any_value *av,
                               ArrayView *out, uint32_t comp_size,
                               const char *fmt) {
  const void *ptr = nullptr;
  uint64_t n = 0;
  if (!try_vector_raw<T>(av, &ptr, &n)) return false;
  out->ptr = ptr;
  out->n_outer = n;
  out->n_inner = 1;
  out->component_size = comp_size;
  out->format = fmt;
  return true;
}

template <typename T>
inline bool role_array(const tinyusdz::value::any_value *av, ArrayView *out,
                       uint32_t n_inner, uint32_t comp_size,
                       const char *fmt) {
  const void *ptr = nullptr;
  uint64_t n = 0;
  if (!try_vector_raw<T>(av, &ptr, &n)) return false;
  out->ptr = ptr;
  out->n_outer = n;
  out->n_inner = n_inner;
  out->component_size = comp_size;
  out->format = fmt;
  return true;
}

bool resolve_array_view(const tinyusdz::value::Value *v, ArrayView *out) {
  using namespace tinyusdz::value;
  if (!v->is_array()) return false;
  const any_value *av = &v->get_raw();

  uint32_t tyid = av->type_id() & (~TYPE_ID_1D_ARRAY_BIT);

  // Plain scalar element types (value type == element type with 1D bit on).
  switch (tyid) {
    case TYPE_ID_INT32:
      return plain_scalar_array<int32_t>(av, out, 4, "i");
    case TYPE_ID_UINT32:
      return plain_scalar_array<uint32_t>(av, out, 4, "I");
    case TYPE_ID_INT64:
      return plain_scalar_array<int64_t>(av, out, 8, "q");
    case TYPE_ID_UINT64:
      return plain_scalar_array<uint64_t>(av, out, 8, "Q");
    case TYPE_ID_FLOAT:
      return plain_scalar_array<float>(av, out, 4, "f");
    case TYPE_ID_DOUBLE:
      return plain_scalar_array<double>(av, out, 8, "d");
    case TYPE_ID_HALF:
      return plain_scalar_array<half>(av, out, 2, "e");
    default:
      break;
  }

  // Role / composite element types. We list every type the core supports so
  // the Python layer gets zero-copy NumPy access for all primvar-worthy USD
  // attributes.
  if (role_array<int2>   (av, out, 2, 4, "i")) return true;
  if (role_array<int3>   (av, out, 3, 4, "i")) return true;
  if (role_array<int4>   (av, out, 4, 4, "i")) return true;
  if (role_array<uint2>  (av, out, 2, 4, "I")) return true;
  if (role_array<uint3>  (av, out, 3, 4, "I")) return true;
  if (role_array<uint4>  (av, out, 4, 4, "I")) return true;

  if (role_array<half2>  (av, out, 2, 2, "e")) return true;
  if (role_array<half3>  (av, out, 3, 2, "e")) return true;
  if (role_array<half4>  (av, out, 4, 2, "e")) return true;

  if (role_array<float2> (av, out, 2, 4, "f")) return true;
  if (role_array<float3> (av, out, 3, 4, "f")) return true;
  if (role_array<float4> (av, out, 4, 4, "f")) return true;

  if (role_array<double2>(av, out, 2, 8, "d")) return true;
  if (role_array<double3>(av, out, 3, 8, "d")) return true;
  if (role_array<double4>(av, out, 4, 8, "d")) return true;

  // Quaternions: imag[3] + real = 4 scalars.
  if (role_array<quath>  (av, out, 4, 2, "e")) return true;
  if (role_array<quatf>  (av, out, 4, 4, "f")) return true;
  if (role_array<quatd>  (av, out, 4, 8, "d")) return true;

  // Colors.
  if (role_array<color3h>(av, out, 3, 2, "e")) return true;
  if (role_array<color3f>(av, out, 3, 4, "f")) return true;
  if (role_array<color3d>(av, out, 3, 8, "d")) return true;
  if (role_array<color4h>(av, out, 4, 2, "e")) return true;
  if (role_array<color4f>(av, out, 4, 4, "f")) return true;
  if (role_array<color4d>(av, out, 4, 8, "d")) return true;

  // Points / normals / vectors / tex-coords.
  if (role_array<point3h>(av, out, 3, 2, "e")) return true;
  if (role_array<point3f>(av, out, 3, 4, "f")) return true;
  if (role_array<point3d>(av, out, 3, 8, "d")) return true;
  if (role_array<normal3h>(av, out, 3, 2, "e")) return true;
  if (role_array<normal3f>(av, out, 3, 4, "f")) return true;
  if (role_array<normal3d>(av, out, 3, 8, "d")) return true;
  if (role_array<vector3h>(av, out, 3, 2, "e")) return true;
  if (role_array<vector3f>(av, out, 3, 4, "f")) return true;
  if (role_array<vector3d>(av, out, 3, 8, "d")) return true;
  if (role_array<texcoord2h>(av, out, 2, 2, "e")) return true;
  if (role_array<texcoord2f>(av, out, 2, 4, "f")) return true;
  if (role_array<texcoord2d>(av, out, 2, 8, "d")) return true;
  if (role_array<texcoord3h>(av, out, 3, 2, "e")) return true;
  if (role_array<texcoord3f>(av, out, 3, 4, "f")) return true;
  if (role_array<texcoord3d>(av, out, 3, 8, "d")) return true;

  // Matrices (exposed as flat component arrays; Python can reshape).
  if (role_array<matrix2d>(av, out, 4, 8, "d")) return true;
  if (role_array<matrix3d>(av, out, 9, 8, "d")) return true;
  if (role_array<matrix4d>(av, out, 16, 8, "d")) return true;

  return false;
}

}  // namespace

extern "C" {

/* ---- Stage ---- */

uint64_t c_tinyusd_stage_num_root_prims(const CTinyUSDStage *stage) {
  if (!stage) return 0;
  return static_cast<uint64_t>(S(stage)->root_prims().size());
}

int c_tinyusd_stage_get_root_prim(const CTinyUSDStage *stage, uint64_t index,
                                  const CTinyUSDPrim **out) {
  if (!stage || !out) return 0;
  const auto &roots = S(stage)->root_prims();
  if (index >= roots.size()) return 0;
  *out = reinterpret_cast<const CTinyUSDPrim *>(&roots[index]);
  return 1;
}

int c_tinyusd_stage_get_prim_at_path(const CTinyUSDStage *stage,
                                     const char *abs_path,
                                     const CTinyUSDPrim **out,
                                     c_tinyusd_string_t *err) {
  if (!stage || !abs_path || !out) return 0;
  Path p(abs_path, "");
  auto r = S(stage)->GetPrimAtPath(p);
  if (!r) {
    set_err(err, r.error());
    return 0;
  }
  *out = reinterpret_cast<const CTinyUSDPrim *>(r.value());
  return 1;
}

int c_tinyusd_stage_save_to_file(const CTinyUSDStage *stage,
                                 const char *filename, CTinyUSDFormat format,
                                 c_tinyusd_string_t *warn,
                                 c_tinyusd_string_t *err) {
  if (!stage || !filename) return 0;
  const Stage *s = S(stage);
  std::string w, e;

  if (format == C_TINYUSD_FORMAT_AUTO || format == C_TINYUSD_FORMAT_UNKNOWN) {
    std::string fn(filename);
    auto pos = fn.find_last_of('.');
    std::string ext = (pos == std::string::npos) ? "" : fn.substr(pos + 1);
    for (auto &c : ext) c = static_cast<char>(::tolower(c));
    if (ext == "usda") format = C_TINYUSD_FORMAT_USDA;
    else if (ext == "usdc") format = C_TINYUSD_FORMAT_USDC;
    else if (ext == "usdz") format = C_TINYUSD_FORMAT_USDZ;
    else format = C_TINYUSD_FORMAT_USDA;
  }

  bool ok = false;
  switch (format) {
    case C_TINYUSD_FORMAT_USDA:
      ok = tinyusdz::usda::SaveAsUSDA(filename, *s, &w, &e);
      break;
    case C_TINYUSD_FORMAT_USDC:
      ok = tinyusdz::usdc::SaveAsUSDCToFile(filename, *s, &w, &e);
      break;
    case C_TINYUSD_FORMAT_USDZ: {
      std::map<std::string, std::vector<uint8_t>> assets;
      ok = tinyusdz::SaveAsUSDZToFile(filename, *s, assets, &w, &e);
      break;
    }
    default:
      e = "Unknown/unsupported format for save.";
      break;
  }

  if (warn && !w.empty()) c_tinyusd_string_replace(warn, w.c_str());
  if (err && !e.empty()) c_tinyusd_string_replace(err, e.c_str());
  return ok ? 1 : 0;
}

namespace {

// Lightweight format sniff from the first bytes of a buffer. The C-API
// header declares c_tinyusd_is_{usda,usdc,usdz}_memory but c-tinyusd.cc
// leaves them unimplemented; we avoid them and sniff locally.
inline CTinyUSDFormat sniff_memory_format(const uint8_t *data, size_t n) {
  if (n >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 &&
      data[3] == 0x04) {
    return C_TINYUSD_FORMAT_USDZ;  // ZIP magic
  }
  if (n >= 8 && data[0] == 'P' && data[1] == 'X' && data[2] == 'R' &&
      data[3] == '-' && data[4] == 'U' && data[5] == 'S' && data[6] == 'D' &&
      data[7] == 'C') {
    return C_TINYUSD_FORMAT_USDC;  // "PXR-USDC" crate magic
  }
  if (n >= 6 && data[0] == '#' && data[1] == 'u' && data[2] == 's' &&
      data[3] == 'd' && data[4] == 'a') {
    return C_TINYUSD_FORMAT_USDA;  // "#usda" header
  }
  return C_TINYUSD_FORMAT_UNKNOWN;
}

}  // namespace

int c_tinyusd_stage_load_from_memory(CTinyUSDStage *stage,
                                     const uint8_t *data, size_t nbytes,
                                     CTinyUSDFormat format,
                                     c_tinyusd_string_t *warn,
                                     c_tinyusd_string_t *err) {
  if (!stage || !data) return 0;
  std::string w, e;
  bool ok = false;

  CTinyUSDFormat fmt = format;
  if (fmt == C_TINYUSD_FORMAT_AUTO || fmt == C_TINYUSD_FORMAT_UNKNOWN) {
    fmt = sniff_memory_format(data, nbytes);
    if (fmt == C_TINYUSD_FORMAT_UNKNOWN) {
      set_err(err, "Unable to detect USD format from memory buffer.");
      return 0;
    }
  }

  Stage *s = Sm(stage);
  switch (fmt) {
    case C_TINYUSD_FORMAT_USDA:
      ok = tinyusdz::LoadUSDAFromMemory(data, nbytes, /*filename*/"", s, &w, &e);
      break;
    case C_TINYUSD_FORMAT_USDC:
      ok = tinyusdz::LoadUSDCFromMemory(data, nbytes, /*filename*/"", s, &w, &e);
      break;
    case C_TINYUSD_FORMAT_USDZ:
      ok = tinyusdz::LoadUSDZFromMemory(data, nbytes, /*filename*/"", s, &w, &e);
      break;
    default:
      e = "Unsupported format for load-from-memory.";
      break;
  }

  if (warn && !w.empty()) c_tinyusd_string_replace(warn, w.c_str());
  if (err && !e.empty()) c_tinyusd_string_replace(err, e.c_str());
  return ok ? 1 : 0;
}

/* ---- Prim ---- */

CTinyUSDAttribute *
c_tinyusd_prim_get_attribute(const CTinyUSDPrim *prim, const char *name) {
  if (!prim || !name) return nullptr;
  Property prop;
  std::string err;
  if (!tinyusdz::tydra::GetProperty(*P(prim), std::string(name), &prop, &err)) {
    return nullptr;
  }
  if (!prop.is_attribute()) return nullptr;
  Attribute *attr = new Attribute(prop.get_attribute());
  // Property-resident Attribute may have an empty name; copy the lookup key.
  if (attr->name().empty()) attr->set_name(std::string(name));
  return reinterpret_cast<CTinyUSDAttribute *>(attr);
}

CTinyUSDRelationship *
c_tinyusd_prim_get_relationship(const CTinyUSDPrim *prim, const char *name) {
  if (!prim || !name) return nullptr;
  Property prop;
  std::string err;
  if (!tinyusdz::tydra::GetProperty(*P(prim), std::string(name), &prop, &err)) {
    return nullptr;
  }
  if (!prop.is_relationship()) return nullptr;
  Relationship *rel = new Relationship(prop.get_relationship());
  return reinterpret_cast<CTinyUSDRelationship *>(rel);
}

/* ---- Attribute ---- */

CTinyUSDAttribute *c_tinyusd_attribute_new(void) {
  return reinterpret_cast<CTinyUSDAttribute *>(new Attribute());
}

int c_tinyusd_attribute_free(CTinyUSDAttribute *attr) {
  if (!attr) return 0;
  delete Am(attr);
  return 1;
}

int c_tinyusd_attribute_get_value(const CTinyUSDAttribute *attr,
                                  const CTinyUSDValue **out) {
  if (!attr || !out) return 0;
  const Attribute *a = A(attr);
  if (!a->has_value()) return 0;
  const tinyusdz::value::Value &v = a->get_var().value_raw();
  *out = reinterpret_cast<const CTinyUSDValue *>(&v);
  return 1;
}

int c_tinyusd_attribute_get_name(const CTinyUSDAttribute *attr,
                                 c_tinyusd_string_t *out) {
  if (!attr || !out) return 0;
  return c_tinyusd_string_replace(out, A(attr)->name().c_str());
}

int c_tinyusd_attribute_get_type_name(const CTinyUSDAttribute *attr,
                                      c_tinyusd_string_t *out) {
  if (!attr || !out) return 0;
  return c_tinyusd_string_replace(out, A(attr)->type_name().c_str());
}

/* ---- Value ---- */

int c_tinyusd_value_array_data(const CTinyUSDValue *value,
                               const void **out_ptr,
                               uint64_t *out_n_outer,
                               uint32_t *out_n_inner,
                               uint32_t *out_component_size,
                               const char **out_format) {
  if (!value || !out_ptr || !out_n_outer || !out_n_inner ||
      !out_component_size || !out_format) {
    return 0;
  }
  ArrayView view;
  if (!resolve_array_view(V(value), &view)) return 0;
  *out_ptr = view.ptr;
  *out_n_outer = view.n_outer;
  *out_n_inner = view.n_inner;
  *out_component_size = view.component_size;
  *out_format = view.format;
  return 1;
}

int c_tinyusd_value_as_double(const CTinyUSDValue *value, double *out) {
  if (!value || !out) return 0;
  const tinyusdz::value::Value *v = V(value);
  if (auto r = v->get_value<double>()) { *out = *r; return 1; }
  if (auto r = v->get_value<float>()) { *out = static_cast<double>(*r); return 1; }
  if (auto r = v->get_value<int32_t>()) { *out = static_cast<double>(*r); return 1; }
  if (auto r = v->get_value<int64_t>()) { *out = static_cast<double>(*r); return 1; }
  return 0;
}

int c_tinyusd_value_as_int64(const CTinyUSDValue *value, int64_t *out) {
  if (!value || !out) return 0;
  const tinyusdz::value::Value *v = V(value);
  if (auto r = v->get_value<int64_t>()) { *out = *r; return 1; }
  if (auto r = v->get_value<int32_t>()) { *out = *r; return 1; }
  if (auto r = v->get_value<uint32_t>()) { *out = *r; return 1; }
  return 0;
}

int c_tinyusd_value_as_bool(const CTinyUSDValue *value, int *out) {
  if (!value || !out) return 0;
  const tinyusdz::value::Value *v = V(value);
  if (auto r = v->get_value<bool>()) { *out = *r ? 1 : 0; return 1; }
  return 0;
}

int c_tinyusd_value_get_string(const CTinyUSDValue *value,
                               c_tinyusd_string_t *out) {
  if (!value || !out) return 0;
  const tinyusdz::value::Value *v = V(value);
  if (auto r = v->get_value<std::string>())
    return c_tinyusd_string_replace(out, r->c_str());
  if (auto r = v->get_value<tinyusdz::value::token>())
    return c_tinyusd_string_replace(out, r->str().c_str());
  return 0;
}

int c_tinyusd_value_is_array(const CTinyUSDValue *value) {
  if (!value) return 0;
  return V(value)->is_array() ? 1 : 0;
}

/* ---- Path ---- */

int c_tinyusd_path_to_string(const CTinyUSDPath *path, c_tinyusd_string_t *out) {
  if (!path || !out) return 0;
  const Path *pp = reinterpret_cast<const Path *>(path);
  return c_tinyusd_string_replace(out, pp->full_path_name().c_str());
}

/* ---- Tydra visit ---- */

namespace {

struct VisitBridge {
  CTinyUSDVisitFunction cb;
  void *ud;
  const char *type_filter;  // nullptr == no filter
};

bool VisitBridgeFn(const Path &abs_path, const Prim &prim,
                   const int32_t depth, void *userdata, std::string *err) {
  (void)err;
  VisitBridge *b = static_cast<VisitBridge *>(userdata);
  if (b->type_filter) {
    const std::string &tn = prim.type_name();
    if (tn != b->type_filter) return true;  // continue
  }
  int ret = b->cb(reinterpret_cast<const CTinyUSDPrim *>(&prim),
                  reinterpret_cast<const CTinyUSDPath *>(&abs_path),
                  static_cast<uint32_t>(depth < 0 ? 0 : depth), b->ud);
  return ret ? true : false;
}

}  // namespace

int c_tinyusd_stage_visit_prims(const CTinyUSDStage *stage,
                                CTinyUSDVisitFunction cb, void *userdata,
                                c_tinyusd_string_t *err) {
  if (!stage || !cb) return 0;
  VisitBridge b{cb, userdata, nullptr};
  std::string e;
  bool ok = tinyusdz::tydra::VisitPrims(*S(stage), VisitBridgeFn, &b, &e);
  if (!ok && err && !e.empty()) c_tinyusd_string_replace(err, e.c_str());
  return ok ? 1 : 0;
}

int c_tinyusd_stage_list_prims_by_type(const CTinyUSDStage *stage,
                                       const char *type_name,
                                       CTinyUSDVisitFunction cb, void *userdata,
                                       c_tinyusd_string_t *err) {
  if (!stage || !cb) return 0;
  VisitBridge b{cb, userdata, type_name};
  std::string e;
  bool ok = tinyusdz::tydra::VisitPrims(*S(stage), VisitBridgeFn, &b, &e);
  if (!ok && err && !e.empty()) c_tinyusd_string_replace(err, e.c_str());
  return ok ? 1 : 0;
}

}  // extern "C"
