// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - Value class implementation

#include "value.hh"
#include "value-common-inl.hh"
#include "../c/lightusd-core-c.h"
#include "array-storage.hh"
#include "type-info.hh"
#include "interpolation.hh"
#include "../crate/lazy-array.hh"
#include "../crate/crate-data-source.hh"

#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lightusd {
namespace next {

namespace {
// A value is "boxed" only when its payload is an out-of-line heap allocation
// (oversized POD that does not fit the inline SBO). Inline scalars/strings/
// handles live directly in `store.bytes`, which is NOT a pointer — so the
// boxed pointer may only be read/written when the LU_VALUE_BOX kind is set.
inline bool Boxed(const lu_value& c) { return c.kind == LU_VALUE_BOX; }
inline void*& BoxRef(lu_value& c) {
  return *reinterpret_cast<void**>((void*)c.store.bytes);
}
inline void* BoxRef(const lu_value& c) {
  return *reinterpret_cast<void* const*>((const void*)c.store.bytes);
}
// Payload bytes for scalar/POD accessors: the box data when boxed, otherwise
// the inline SBO bytes. Never use `store.bytes` directly for POD reads/writes.
inline const void* PodData(const lu_value& c) {
  return Boxed(c) ? BoxRef(c) : (const void*)c.store.bytes;
}
inline void* PodData(lu_value& c) {
  return Boxed(c) ? BoxRef(c) : (void*)c.store.bytes;
}
}  // namespace

Value Value::MakeDictionary() {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Dictionary);
  new (v.core_.store.bytes) DictHandle(std::make_shared<Dict>());
  return v;
}

Value Value::MakeDictionary(Dict&& d) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Dictionary);
  new (v.core_.store.bytes) DictHandle(std::make_shared<Dict>(std::move(d)));
  return v;
}

Value Value::MakePoint3f(float x, float y, float z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Point3f);
  float data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakePoint3d(double x, double y, double z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Point3d);
  double data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeVector3f(float x, float y, float z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Vector3f);
  float data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeVector3d(double x, double y, double z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Vector3d);
  double data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeNormal3f(float x, float y, float z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Normal3f);
  float data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeNormal3d(double x, double y, double z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Normal3d);
  double data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeColor3f(float r, float g, float b) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Color3f);
  float data[3] = {r, g, b};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeColor4f(float r, float g, float b, float a) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Color4f);
  float data[4] = {r, g, b, a};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeTexcoord2f(float u, float v_coord) {
  Value val;
  val.core_.type = static_cast<uint16_t>(TypeId::Texcoord2f);
  float data[2] = {u, v_coord};
  AdoptPod(val, data, sizeof(data));
  return val;
}

Value Value::MakeFromRaw(TypeId type_id, const void* data) {
  Value v;
  if (!data || type_id == TypeId::Invalid || type_id == TypeId::Dictionary) {
    return v;
  }

  if (UsesStringStorage(type_id)) {
    v.core_.type = static_cast<uint16_t>(type_id);
    new (v.core_.store.bytes) StringStorage{std::string(static_cast<const char*>(data))};
  } else {
    const size_t size = GetTypeSize(type_id);
    if (size == 0 || size > 128) return v;
    v.core_.type = static_cast<uint16_t>(type_id);
    AdoptPod(v, data, size);
  }
  return v;
}

// ============================================================
// Array factory functions
// ============================================================

Value Value::MakeFloatArray(const std::vector<float>& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(data);
  return v;
}

Value Value::MakeFloatArray(std::vector<float>&& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

Value Value::MakeIntArray(const std::vector<int32_t>& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Int);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(data);
  return v;
}

Value Value::MakeIntArray(std::vector<int32_t>&& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Int);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

Value Value::MakeFloat2Array(const std::vector<float>& data) {
  if (!IsValidComponentArray(data.size(), TypeId::Float2, 2, true)) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float2);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size() / 2);
  new (v.core_.store.bytes) ArrayHandle(data);
  return v;
}

Value Value::MakeFloat2Array(std::vector<float>&& data) {
  if (!IsValidComponentArray(data.size(), TypeId::Float2, 2, true)) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float2);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size() / 2);
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

Value Value::MakeFloat3Array(const std::vector<float>& data) {
  if (!IsValidComponentArray(data.size(), TypeId::Float3, 3, true)) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float3);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size() / 3);
  new (v.core_.store.bytes) ArrayHandle(data);
  return v;
}

Value Value::MakeFloat3Array(std::vector<float>&& data) {
  if (!IsValidComponentArray(data.size(), TypeId::Float3, 3, true)) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float3);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size() / 3);
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

// New array types
Value Value::MakeDoubleArray(const std::vector<double>& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::Double); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(data); return v;
}
Value Value::MakeDoubleArray(std::vector<double>&& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::Double); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data)); return v;
}
Value Value::MakeInt64Array(const std::vector<int64_t>& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::Int64); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(data); return v;
}
Value Value::MakeInt64Array(std::vector<int64_t>&& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::Int64); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data)); return v;
}
Value Value::MakeUIntArray(const std::vector<uint32_t>& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::UInt); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(data); return v;
}
Value Value::MakeUIntArray(std::vector<uint32_t>&& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::UInt); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data)); return v;
}
Value Value::MakeUInt64Array(const std::vector<uint64_t>& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::UInt64); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(data); return v;
}
Value Value::MakeUInt64Array(std::vector<uint64_t>&& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::UInt64); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data)); return v;
}
Value Value::MakeBoolArray(const std::vector<bool>& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::Bool); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  std::vector<uint8_t> tmp(data.size());
  for (size_t i = 0; i < data.size(); i++) tmp[i] = data[i] ? 1 : 0;
  new (v.core_.store.bytes) ArrayHandle(std::move(tmp)); return v;
}
Value Value::MakeBoolByteArray(std::vector<uint8_t>&& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  for (uint8_t& lane : data) lane = lane != 0 ? uint8_t(1) : uint8_t(0);
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::Bool); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data)); return v;
}
Value Value::MakeTokenArray(const std::vector<std::string>& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::Token); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(data); return v;
}
Value Value::MakeTokenArray(std::vector<std::string>&& data) {
  if (!FitsValueArraySize(data.size())) return Value();
  Value v; v.core_.type = static_cast<uint16_t>(TypeId::Token); v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data)); return v;
}

Value Value::MakeFloatCompArray(std::vector<float>&& data, TypeId elem_type,
                                uint32_t comps_per_elem) {
  if (!IsValidComponentArray(data.size(), elem_type, comps_per_elem,
                             IsFloatBackedArray(elem_type))) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(elem_type);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = comps_per_elem
                      ? static_cast<uint32_t>(data.size() / comps_per_elem)
                      : 0;
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

Value Value::MakeDoubleCompArray(std::vector<double>&& data, TypeId elem_type,
                                 uint32_t comps_per_elem) {
  if (!IsValidComponentArray(data.size(), elem_type, comps_per_elem,
                             IsDoubleBackedArray(elem_type))) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(elem_type);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = comps_per_elem
                      ? static_cast<uint32_t>(data.size() / comps_per_elem)
                      : 0;
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

Value Value::MakeUIntCompArray(std::vector<uint32_t>&& data, TypeId elem_type,
                               uint32_t comps_per_elem) {
  if (!IsValidComponentArray(data.size(), elem_type, comps_per_elem,
                             IsUIntBackedArray(elem_type))) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(elem_type);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = comps_per_elem
                      ? static_cast<uint32_t>(data.size() / comps_per_elem)
                      : 0;
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

Value Value::MakeStringLikeArray(std::vector<std::string>&& data,
                                 TypeId elem_type) {
  if (!UsesStringStorage(elem_type) || !FitsValueArraySize(data.size())) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(elem_type);  // Token, String or AssetPath
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = static_cast<uint32_t>(data.size());
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

Value Value::MakeIntCompArray(std::vector<int32_t>&& data, TypeId elem_type,
                              uint32_t comps_per_elem) {
  if (!IsValidComponentArray(data.size(), elem_type, comps_per_elem,
                             IsIntBackedArray(elem_type))) {
    return Value();
  }
  Value v;
  v.core_.type = static_cast<uint16_t>(elem_type);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.size = comps_per_elem
                      ? static_cast<uint32_t>(data.size() / comps_per_elem)
                      : 0;
  new (v.core_.store.bytes) ArrayHandle(std::move(data));
  return v;
}

Value Value::MakeDeferredArray(TypeId elem_type, ArrayScalarKind kind,
                               uint32_t elem_count,
                               DeferredArrayFill* out_fill) {
  if (!out_fill) return Value();
  Value v;
  v.core_.type = static_cast<uint16_t>(elem_type);
  v.core_.kind = LU_VALUE_ARRAY;
  // Final size now; the payload arrives through *out_fill before anything may
  // read it (see the header contract).
  v.core_.size = elem_count;
  ArrayHandle* h = nullptr;
  switch (kind) {
    case ArrayScalarKind::Float:
      h = new (v.core_.store.bytes) ArrayHandle(std::vector<float>());
      break;
    case ArrayScalarKind::Double:
      h = new (v.core_.store.bytes) ArrayHandle(std::vector<double>());
      break;
    case ArrayScalarKind::Int32:
      h = new (v.core_.store.bytes) ArrayHandle(std::vector<int32_t>());
      break;
    case ArrayScalarKind::UInt32:
      h = new (v.core_.store.bytes) ArrayHandle(std::vector<uint32_t>());
      break;
    case ArrayScalarKind::Int64:
      h = new (v.core_.store.bytes) ArrayHandle(std::vector<int64_t>());
      break;
    case ArrayScalarKind::UInt64:
      h = new (v.core_.store.bytes) ArrayHandle(std::vector<uint64_t>());
      break;
    case ArrayScalarKind::Bool:
      h = new (v.core_.store.bytes) ArrayHandle(std::vector<uint8_t>());
      break;
  }
  if (!h) return Value();
  out_fill->handle_ = *h;  // second reference: the fill side
  out_fill->type_id_ = elem_type;
  out_fill->elem_count_ = elem_count;
  return v;
}

bool Value::DeferredArrayFill::Complete(Value&& parsed) {
  if (!((parsed.core_.kind == LU_VALUE_ARRAY || parsed.core_.kind == LU_VALUE_LAZY_ARRAY)) ||
      (parsed.core_.kind == LU_VALUE_LAZY_ARRAY) ||
      (parsed.core_.kind == LU_VALUE_BLOCK) ||
      ((parsed.core_.flags & 0x01u) != 0) ||
      (parsed.core_.type != static_cast<uint16_t>(type_id_)) ||
      (parsed.core_.size != elem_count_)) {
    return false;
  }
  return ArraySlot(reinterpret_cast<char*>(parsed.core_.store.bytes))->swap_payload(handle_);
}

// ============================================================
// Queries and accessors
// ============================================================

size_t Value::array_size() const {
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return 0;
  if ((core_.kind == LU_VALUE_LAZY_ARRAY)) return core_.size;
  const size_t size = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->size();
  return UsesStringStorage(static_cast<TypeId>(core_.type)) ? size
      : ComponentArraySize(size, GetComponentCount(static_cast<TypeId>(core_.type)));
}

void Value::retag_role(TypeId new_type) {
  if (new_type == static_cast<TypeId>(core_.type) || new_type == TypeId::Invalid ||
      static_cast<TypeId>(core_.type) == TypeId::Invalid || (core_.kind == LU_VALUE_BLOCK)) {
    return;
  }
  // Only role re-tags: same scalar component type and component count, so the
  // buffer/SBO layout is identical (Float3 -> point3f, Half4 -> color4h, ...).
  auto comp = [](TypeId t) {
    TypeId c = GetComponentType(t);
    return c == TypeId::Invalid ? t : c;
  };
  // Signed<->unsigned 32-bit int lanes: the crate encodes uint2/3/4 as the
  // Vec*i twin, so the reader must be able to restore uintN from the
  // declared type name. Without this, `uint3 v = (...,4294967295)` read
  // back Int3-typed and printed a negative lane that fails re-parse. Array
  // storage retains distinct signed/unsigned vector objects, so the
  // buffer is REBUILT (bit-exact int32<->uint32) rather than reinterpreted.
  const TypeId oldc = comp(static_cast<TypeId>(core_.type));
  const TypeId newc = comp(new_type);
  if (oldc != newc) {
    const bool int_uint_pair =
        (oldc == TypeId::Int && newc == TypeId::UInt) ||
        (oldc == TypeId::UInt && newc == TypeId::Int);
    if (!int_uint_pair) return;
    if (GetComponentCount(static_cast<TypeId>(core_.type)) != GetComponentCount(new_type)) return;
    if ((core_.kind == LU_VALUE_LAZY_ARRAY)) return;  // would require changing the backing storage class
    if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
      ensure_materialized();
      ArrayHandle& h = *ArraySlot(reinterpret_cast<char*>(core_.store.bytes));
      if (oldc == TypeId::Int) {
        auto* st = static_cast<std::vector<int32_t>*>(h.vector_object());
        std::vector<uint32_t> u(st->begin(), st->end());
        h = ArrayHandle(std::move(u));
      } else {
        auto* st = static_cast<std::vector<uint32_t>*>(h.vector_object());
        std::vector<int32_t> i(st->begin(), st->end());
        h = ArrayHandle(std::move(i));
      }
    } else if (GetTypeSize(static_cast<TypeId>(core_.type)) != GetTypeSize(new_type)) {
      return;  // SBO scalar/vector: same byte width required (it is, 4/lane)
    }
    core_.type = static_cast<uint16_t>(new_type);
    return;
  }
  if (GetComponentCount(static_cast<TypeId>(core_.type)) != GetComponentCount(new_type)) return;
  if ((core_.kind == LU_VALUE_LAZY_ARRAY)) {
    // Role aliases share the same scalar type, arity, and on-disk bytes. Keep
    // the payload lazy while ensuring materialization preserves the role too.
    LazyArrayRef* ref;
    std::memcpy(&ref, core_.store.bytes, sizeof(ref));
    if (!ref) return;
    core_.type = static_cast<uint16_t>(new_type);
    ref->value_type = new_type;
    return;
  }
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) && GetTypeSize(static_cast<TypeId>(core_.type)) != GetTypeSize(new_type)) return;
  core_.type = static_cast<uint16_t>(new_type);
}

void Value::clear() {
  destroy();
  core_.type = static_cast<uint16_t>(TypeId::Invalid);
  core_.kind = LU_VALUE_EMPTY;
  core_.size = 0;
}

bool Value::uses_heap() const {
  return (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || static_cast<TypeId>(core_.type) == TypeId::Dictionary ||   Boxed(core_);
}

void Value::destroy() {
  if ((core_.kind == LU_VALUE_LAZY_ARRAY)) {
    LazyArrayRef* ptr;
    std::memcpy(&ptr, core_.store.bytes, sizeof(ptr));
    delete ptr;  // drops the shared_ptr<CrateDataSource> reference
    core_.type = static_cast<uint16_t>(TypeId::Invalid);
    core_.kind = LU_VALUE_EMPTY;
    // reset via empty kind below; no separate field used.
    core_.size = 0;
    return;
  }

  if (static_cast<TypeId>(core_.type) == TypeId::Invalid) {
    // block marker reset implicitly by empty kind
    return;
  }

  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
    // Drop the shared array reference (frees the buffer iff this was the last
    // owner — the copy-on-write release).
    ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->~ArrayHandle();
  } else if (static_cast<TypeId>(core_.type) == TypeId::Dictionary) {
    // Releasing the last handle through shared_ptr's normal destructor makes
    // Dict -> Value -> Dict teardown recurse once per nesting level. Move
    // uniquely-owned child handles out before destroying each Dict so even
    // API-constructed deep dictionaries use an explicit heap work stack.
    DictHandle root = std::move(*DictSlot(reinterpret_cast<const char*>(core_.store.bytes)));
    DictSlot(reinterpret_cast<const char*>(core_.store.bytes))->~DictHandle();
    std::vector<DictHandle> pending;
    pending.push_back(std::move(root));
    while (!pending.empty()) {
      DictHandle current = std::move(pending.back());
      pending.pop_back();
      if (!current || current.use_count() != 1) {
        // Another Value still owns it. Dropping this reference cannot destroy
        // the Dict, and the eventual final owner will perform this traversal.
        continue;
      }

      for (auto &entry : current->entries_) {
        Value &child = entry.second;
        if (static_cast<TypeId>(child.core_.type) != TypeId::Dictionary || ((child).core_.kind == LU_VALUE_ARRAY || (child).core_.kind == LU_VALUE_LAZY_ARRAY) ||
            child.core_.kind == LU_VALUE_LAZY_ARRAY) {
          continue;
        }
        DictHandle nested = std::move(*DictSlot(reinterpret_cast<char*>(child.core_.store.bytes)));
        DictSlot(reinterpret_cast<char*>(child.core_.store.bytes))->~DictHandle();
        child.core_.type = static_cast<uint16_t>(TypeId::Invalid);
        child.core_.kind = LU_VALUE_EMPTY;
        child.core_.flags &= ~0x01u;
        child.core_.size = 0;
        pending.push_back(std::move(nested));
      }
      current.reset();
    }
  } else if (UsesStringStorage(static_cast<TypeId>(core_.type))) {
    reinterpret_cast<StringStorage*>(core_.store.bytes)->~StringStorage();
  }

  if (Boxed(core_)) {
    delete[] static_cast<unsigned char*>(BoxRef(core_));
    BoxRef(core_) = nullptr;
  }
  core_.type = static_cast<uint16_t>(TypeId::Invalid);
  core_.kind = LU_VALUE_EMPTY;
  core_.flags &= ~0x01u;
  core_.size = 0;
}

void Value::copy_from(const Value& other) {
  if (Boxed(core_)) { delete[] static_cast<unsigned char*>(BoxRef(core_)); BoxRef(core_) = nullptr; }
  core_.type = other.core_.type;
  core_.kind = other.core_.kind;
  core_.flags = other.core_.flags;
  core_.size = other.core_.size;

  if (other.core_.kind == LU_VALUE_LAZY_ARRAY) {
    LazyArrayRef* other_ptr;
    std::memcpy(&other_ptr, other.core_.store.bytes, sizeof(other_ptr));
    auto* new_ptr = new LazyArrayRef(*other_ptr);  // shared_ptr refcount++
    std::memcpy(core_.store.bytes, &new_ptr, sizeof(new_ptr));
    return;
  }

  if (((other).core_.kind == LU_VALUE_ARRAY || (other).core_.kind == LU_VALUE_LAZY_ARRAY)) {
    // Copy-on-write: share the same buffer, just bump the refcount. The
    // element data is NOT copied; a later mutable access detaches.
    new (core_.store.bytes) ArrayHandle(*ArraySlot(reinterpret_cast<const char*>(other.core_.store.bytes)));
  } else if (static_cast<TypeId>(other.core_.type) == TypeId::Dictionary) {
    new (core_.store.bytes) DictHandle(*DictSlot(reinterpret_cast<const char*>(other.core_.store.bytes)));  // refcount++ (CoW)
  } else if (UsesStringStorage(static_cast<TypeId>(other.core_.type))) {
    new (core_.store.bytes) StringStorage{reinterpret_cast<const StringStorage*>(other.core_.store.bytes)->value};
  } else if (Boxed(other.core_)) {
      BoxRef(core_) = new unsigned char[GetTypeSize(static_cast<TypeId>(other.core_.type))];
    std::memcpy(  BoxRef(core_), BoxRef(other.core_), GetTypeSize(static_cast<TypeId>(other.core_.type)));
  } else {
    std::memcpy(core_.store.bytes, other.core_.store.bytes, kSBOSize);
  }
}

void Value::move_from(Value&& other) noexcept {
  if (Boxed(core_)) { delete[] static_cast<unsigned char*>(BoxRef(core_)); BoxRef(core_) = nullptr; }
  core_.type = other.core_.type;
  core_.kind = other.core_.kind;
  core_.flags = other.core_.flags;
  core_.size = other.core_.size;

  if (((other).core_.kind == LU_VALUE_ARRAY || (other).core_.kind == LU_VALUE_LAZY_ARRAY) && other.core_.kind != LU_VALUE_LAZY_ARRAY) {
    // Move the shared_ptr handle, then destroy the moved-from (now-empty) slot.
    new (core_.store.bytes) ArrayHandle(std::move(*ArraySlot(reinterpret_cast<const char*>(other.core_.store.bytes))));
    ArraySlot(reinterpret_cast<const char*>(other.core_.store.bytes))->~ArrayHandle();
  } else if (other.core_.kind == LU_VALUE_LAZY_ARRAY) {
    // Steal the raw LazyArrayRef* (lazy arrays are not shared_ptr-backed).
    std::memcpy(core_.store.bytes, other.core_.store.bytes, sizeof(void*));
  } else if (static_cast<TypeId>(other.core_.type) == TypeId::Dictionary) {
    new (core_.store.bytes) DictHandle(std::move(*DictSlot(reinterpret_cast<const char*>(other.core_.store.bytes))));
    DictSlot(reinterpret_cast<const char*>(other.core_.store.bytes))->~DictHandle();
  } else if (Boxed(other.core_)) {
      BoxRef(core_) = BoxRef(other.core_);
    BoxRef(other.core_) = nullptr;
  } else if (UsesStringStorage(static_cast<TypeId>(other.core_.type))) {
    new (core_.store.bytes) StringStorage{std::move(reinterpret_cast<StringStorage*>(other.core_.store.bytes)->value)};
    reinterpret_cast<StringStorage*>(other.core_.store.bytes)->~StringStorage();
  } else {
    std::memcpy(core_.store.bytes, other.core_.store.bytes, kSBOSize);
  }

  other.core_.type = static_cast<uint16_t>(TypeId::Invalid);
  other.core_.kind = LU_VALUE_EMPTY;
  other.core_.flags = 0;
  other.core_.size = 0;
}

// ============================================================
// Lazy array references
// ============================================================

Value Value::MakeBlock() {
  Value v;
  v.core_.kind = LU_VALUE_BLOCK;  // static_cast<TypeId>(core_.type) stays Invalid, (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)/(core_.kind == LU_VALUE_LAZY_ARRAY) stay false:
  return v;            // no heap, destroy()/copy share the trivial-scalar path.
}

Value Value::MakeLazyArray(const LazyArrayRef& ref) {
  Value v;
  if (!ref.source || !IsValueArrayType(ref.value_type) ||
      ref.element_count >
      static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
    return v;
  }
  v.core_.type = static_cast<uint16_t>(ref.value_type);
  v.core_.kind = LU_VALUE_ARRAY;
  v.core_.kind = LU_VALUE_LAZY_ARRAY;
  v.core_.flags &= ~0x01u;
  v.core_.size = static_cast<uint32_t>(ref.element_count);
  auto* ptr = new LazyArrayRef(ref);
  std::memcpy(v.core_.store.bytes, &ptr, sizeof(ptr));
  return v;
}

const LazyArrayRef* Value::lazy_ref() const {
  if (!(core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  LazyArrayRef* ptr;
  std::memcpy(&ptr, core_.store.bytes, sizeof(ptr));
  return ptr;
}

void Value::materialize() {
  if (!(core_.kind == LU_VALUE_LAZY_ARRAY)) return;

  LazyArrayRef* ptr;
  std::memcpy(&ptr, core_.store.bytes, sizeof(ptr));

  Value decoded;
  bool ok = (ptr && ptr->source) ? ptr->source->MaterializeArray(*ptr, &decoded) : false;
  if (ok) ok = FinalizeLazyDecode(*ptr, &decoded);

  delete ptr;  // releases the shared_ptr<CrateDataSource>

  // Reset to empty WITHOUT calling destroy() (core_.store.bytes now dangles) then adopt
  // the decoded value. If decode failed the value becomes empty.
  // kind sufficient; no-op
  core_.type = static_cast<uint16_t>(TypeId::Invalid);
  core_.kind = LU_VALUE_EMPTY;
  core_.flags &= ~0x01u;
  core_.size = 0;
  if (ok) {
    move_from(std::move(decoded));
  }
}

void Value::ensure_materialized() const {
  if ((core_.kind == LU_VALUE_LAZY_ARRAY)) {
    const_cast<Value*>(this)->materialize();
  }
}

Value Value::materialized_copy() const {
  if ((core_.kind == LU_VALUE_LAZY_ARRAY)) {
    const LazyArrayRef* ref = lazy_ref();
    Value decoded;
    if (ref && ref->source && ref->source->MaterializeArray(*ref, &decoded) &&
        FinalizeLazyDecode(*ref, &decoded)) {
      return decoded;  // NRVO / move: steals the decoded array handle.
    }
    return Value();  // decode failed -> empty (prints "None")
  }
  // Non-lazy: ordinary CoW copy (array refcount bump; preserves ((core_.flags & 0x01u) != 0)).
  return *this;
}

void* Value::data_ptr() {
  ensure_materialized();
  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
    core_.flags |= 0x01u;
    DetachArray(reinterpret_cast<char*>(core_.store.bytes));
    return ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->data();
  }
  if (static_cast<TypeId>(core_.type) == TypeId::Dictionary) {
    DetachDict(reinterpret_cast<char*>(core_.store.bytes));
    return DictSlot(reinterpret_cast<const char*>(core_.store.bytes))->get();
  }
  if (UsesStringStorage(static_cast<TypeId>(core_.type))) {
    return reinterpret_cast<StringStorage*>(core_.store.bytes)->value.data();
  }
  if (static_cast<TypeId>(core_.type) == TypeId::Invalid) return nullptr;
  if (Boxed(core_)) return   BoxRef(core_);
  return core_.store.bytes;
}

const void* Value::data_ptr() const {
  ensure_materialized();
  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->data();
  if (static_cast<TypeId>(core_.type) == TypeId::Dictionary) {
    return DictSlot(reinterpret_cast<const char*>(core_.store.bytes))->get();
  }
  if (UsesStringStorage(static_cast<TypeId>(core_.type))) {
    return reinterpret_cast<const StringStorage*>(core_.store.bytes)->value.data();
  }
  if (static_cast<TypeId>(core_.type) == TypeId::Invalid) return nullptr;
  if (Boxed(core_)) return const_cast<void*>(  BoxRef(core_));
  return core_.store.bytes;
}

const void* Value::raw_data() const {
  return data_ptr();
}

void* Value::raw_data() {
  return data_ptr();
}

// ============================================================

const bool* Value::as_bool() const { 
  if (static_cast<TypeId>(core_.type) != TypeId::Bool || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const bool* >(core_.store.bytes);
}
bool* Value::as_bool() { 
  if (static_cast<TypeId>(core_.type) != TypeId::Bool || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<bool* >(core_.store.bytes);
}
const int32_t* Value::as_int() const { 
  if (static_cast<TypeId>(core_.type) != TypeId::Int || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const int32_t* >(core_.store.bytes);
}
int32_t* Value::as_int() { 
  if (static_cast<TypeId>(core_.type) != TypeId::Int || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<int32_t* >(core_.store.bytes);
}
const uint32_t* Value::as_uint() const { 
  if (static_cast<TypeId>(core_.type) != TypeId::UInt || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const uint32_t* >(core_.store.bytes);
}
uint32_t* Value::as_uint() { 
  if (static_cast<TypeId>(core_.type) != TypeId::UInt || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<uint32_t* >(core_.store.bytes);
}
const int64_t* Value::as_int64() const { 
  if (static_cast<TypeId>(core_.type) != TypeId::Int64 || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const int64_t* >(core_.store.bytes);
}
int64_t* Value::as_int64() { 
  if (static_cast<TypeId>(core_.type) != TypeId::Int64 || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<int64_t* >(core_.store.bytes);
}
const uint64_t* Value::as_uint64() const { 
  if (static_cast<TypeId>(core_.type) != TypeId::UInt64 || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const uint64_t* >(core_.store.bytes);
}
uint64_t* Value::as_uint64() { 
  if (static_cast<TypeId>(core_.type) != TypeId::UInt64 || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<uint64_t* >(core_.store.bytes);
}
const float* Value::as_float() const { 
  if (static_cast<TypeId>(core_.type) != TypeId::Float || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const float* >(core_.store.bytes);
}
float* Value::as_float() { 
  if (static_cast<TypeId>(core_.type) != TypeId::Float || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<float* >(core_.store.bytes);
}
const uint8_t* Value::as_uchar() const { 
  if (static_cast<TypeId>(core_.type) != TypeId::UChar || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const uint8_t* >(core_.store.bytes);
}
uint8_t* Value::as_uchar() { 
  if (static_cast<TypeId>(core_.type) != TypeId::UChar || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<uint8_t* >(core_.store.bytes);
}


#undef DEFINE_SCALAR_ACCESSOR

// double additionally accepts TimeCode: same 8-byte storage, and scalar
// timecode values now keep their type identity through the crate reader.
const double* Value::as_double() const {
  if ((static_cast<TypeId>(core_.type) != TypeId::Double && static_cast<TypeId>(core_.type) != TypeId::TimeCode) ||
      (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
    return nullptr;
  }
  return reinterpret_cast<const double*>(PodData(core_));
}
double* Value::as_double() {
  if ((static_cast<TypeId>(core_.type) != TypeId::Double && static_cast<TypeId>(core_.type) != TypeId::TimeCode) ||
      (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
    return nullptr;
  }
  return reinterpret_cast<double*>(PodData(core_));
}

const std::string* Value::as_string() const {
  // PathExpression shares the string storage and reads as a string.
  if ((static_cast<TypeId>(core_.type) != TypeId::String && static_cast<TypeId>(core_.type) != TypeId::PathExpression) ||
      (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
    return nullptr;
  }
  return &reinterpret_cast<const StringStorage*>(core_.store.bytes)->value;
}

std::string* Value::as_string() {
  if (static_cast<TypeId>(core_.type) != TypeId::String || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return &reinterpret_cast<StringStorage*>(core_.store.bytes)->value;
}

const std::string* Value::as_token() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Token || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return &reinterpret_cast<const StringStorage*>(core_.store.bytes)->value;
}

const std::string* Value::as_asset_path() const {
  if (static_cast<TypeId>(core_.type) != TypeId::AssetPath || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return &reinterpret_cast<const StringStorage*>(core_.store.bytes)->value;
}

const Dict* Value::as_dictionary() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Dictionary) return nullptr;
  return DictSlot(reinterpret_cast<const char*>(core_.store.bytes))->get();
}

size_t Value::dynamic_string_memory_usage() const {
  size_t bytes = 0;
  const auto add = [&bytes](size_t n) {
    bytes = n > (std::numeric_limits<size_t>::max)() - bytes
        ? (std::numeric_limits<size_t>::max)() : bytes + n;
  };
  const auto multiply = [](size_t a, size_t b) {
    return a && b > (std::numeric_limits<size_t>::max)() / a
        ? (std::numeric_limits<size_t>::max)() : a * b;
  };
  if (const std::string* value = as_string()) add(value->capacity());
  else if (const std::string* value = as_token()) add(value->capacity());
  else if (const std::string* value = as_asset_path()) add(value->capacity());

  // Estimating a retained lazy array must not materialize it as a side effect.
  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) && !(core_.kind == LU_VALUE_LAZY_ARRAY)) {
    if (const auto* values = as_token_array()) {
      add(multiply(values->capacity(), sizeof(std::string)));
      for (const std::string& value : *values) add(value.capacity());
    }
  }
  if (const Dict* dict = as_dictionary()) add(dict->dynamic_string_memory_usage());
  return bytes;
}

size_t Value::heap_memory_usage() const {
  size_t bytes = dynamic_string_memory_usage();
  const auto add = [&bytes](size_t n) {
    bytes = n > (std::numeric_limits<size_t>::max)() - bytes
        ? (std::numeric_limits<size_t>::max)() : bytes + n;
  };
  const auto multiply = [](size_t a, size_t b) {
    return a && b > (std::numeric_limits<size_t>::max)() / a
        ? (std::numeric_limits<size_t>::max)() : a * b;
  };
  if (is_array() && !is_lazy()) {
    add(multiply(array_size(), GetTypeSize(type_id())));
  }
  if (!as_dictionary()) return bytes;
  std::vector<const Value*> pending{this};
  while (!pending.empty()) {
    const Value* current = pending.back();
    pending.pop_back();
    if (current != this && current->is_array() && !current->is_lazy()) {
      add(multiply(current->array_size(), GetTypeSize(current->type_id())));
    }
    if (const Dict* dict = current->as_dictionary()) {
      for (const auto& entry : dict->entries()) pending.push_back(&entry.second);
    }
  }
  return bytes;
}

Dict* Value::as_dictionary() {
  if (static_cast<TypeId>(core_.type) != TypeId::Dictionary) return nullptr;
  DetachDict(reinterpret_cast<char*>(core_.store.bytes));
  return DictSlot(reinterpret_cast<const char*>(core_.store.bytes))->get();
}

// Vector accessors
const int32_t* Value::as_int2() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Int2 || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const int32_t*>(PodData(core_));
}

const int32_t* Value::as_int3() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Int3 || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const int32_t*>(PodData(core_));
}

const int32_t* Value::as_int4() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Int4 || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const int32_t*>(PodData(core_));
}

const float* Value::as_float2() const {
  if ((static_cast<TypeId>(core_.type) != TypeId::Float2 && static_cast<TypeId>(core_.type) != TypeId::Texcoord2f) ||
      (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const float*>(PodData(core_));
}

const float* Value::as_float3() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Float3 && static_cast<TypeId>(core_.type) != TypeId::Point3f &&
      static_cast<TypeId>(core_.type) != TypeId::Vector3f && static_cast<TypeId>(core_.type) != TypeId::Normal3f &&
      static_cast<TypeId>(core_.type) != TypeId::Color3f && static_cast<TypeId>(core_.type) != TypeId::Texcoord3f)
    return nullptr;
  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const float*>(PodData(core_));
}

const float* Value::as_float4() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Float4 && static_cast<TypeId>(core_.type) != TypeId::Quatf &&
      static_cast<TypeId>(core_.type) != TypeId::Color4f) return nullptr;
  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const float*>(PodData(core_));
}

const double* Value::as_double2() const {
  if ((static_cast<TypeId>(core_.type) != TypeId::Double2 && static_cast<TypeId>(core_.type) != TypeId::Texcoord2d) ||
      (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const double*>(PodData(core_));
}

const double* Value::as_double3() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Double3 && static_cast<TypeId>(core_.type) != TypeId::Point3d &&
      static_cast<TypeId>(core_.type) != TypeId::Vector3d && static_cast<TypeId>(core_.type) != TypeId::Normal3d &&
      static_cast<TypeId>(core_.type) != TypeId::Color3d && static_cast<TypeId>(core_.type) != TypeId::Texcoord3d)
    return nullptr;
  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const double*>(PodData(core_));
}

// ============================================================
// Converting scalar reads (to_float*): unlike the as_* accessors, these
// widen raw-half SBO scalars (authored half/half3/... store half-bit lanes,
// not floats) and narrow double-backed values. Lanes are copied in storage
// order — quat conventions are the caller's concern, same as as_float4().
// ============================================================

namespace {


inline int HalfLaneCount(TypeId id) {
  switch (id) {
    case TypeId::Half: return 1;
    case TypeId::Half2:
    case TypeId::Texcoord2h: return 2;
    case TypeId::Half3:
    case TypeId::Point3h:
    case TypeId::Vector3h:
    case TypeId::Normal3h:
    case TypeId::Color3h:
    case TypeId::Texcoord3h: return 3;
    case TypeId::Half4:
    case TypeId::Quath:
    case TypeId::Color4h: return 4;
    default: return 0;
  }
}

}  // namespace

bool Value::ToFloatLanes(int lanes, float* out) const {
  if (!out || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return false;
  if (HalfLaneCount(static_cast<TypeId>(core_.type)) == lanes) {
    const uint16_t* bits = reinterpret_cast<const uint16_t*>(PodData(core_));
    for (int i = 0; i < lanes; ++i) out[i] = lu_half_to_float(bits[i]);
    return true;
  }
  switch (lanes) {
    case 1:
      if (const float* f = as_float()) { out[0] = *f; return true; }
      if (const double* d = as_double()) { out[0] = static_cast<float>(*d); return true; }
      return false;
    case 2:
      if (const float* f = as_float2()) { out[0] = f[0]; out[1] = f[1]; return true; }
      if (const double* d = as_double2()) {
        for (int i = 0; i < 2; ++i) out[i] = static_cast<float>(d[i]);
        return true;
      }
      return false;
    case 3:
      if (const float* f = as_float3()) {
        for (int i = 0; i < 3; ++i) out[i] = f[i];
        return true;
      }
      if (const double* d = as_double3()) {
        for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(d[i]);
        return true;
      }
      return false;
    case 4:
      if (const float* f = as_float4()) {
        for (int i = 0; i < 4; ++i) out[i] = f[i];
        return true;
      }
      if (const double* d = as_double4()) {
        for (int i = 0; i < 4; ++i) out[i] = static_cast<float>(d[i]);
        return true;
      }
      return false;
    default:
      return false;
  }
}

bool Value::to_float(float* out) const { return ToFloatLanes(1, out); }
bool Value::to_float2(float* out) const { return ToFloatLanes(2, out); }
bool Value::to_float3(float* out) const { return ToFloatLanes(3, out); }
bool Value::to_float4(float* out) const { return ToFloatLanes(4, out); }

const double* Value::as_double4() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Double4 && static_cast<TypeId>(core_.type) != TypeId::Quatd &&
      static_cast<TypeId>(core_.type) != TypeId::Color4d) return nullptr;
  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const double*>(PodData(core_));
}

// Matrix accessors
const float* Value::as_matrix2f() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Matrix2f || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const float*>(PodData(core_));
}

const float* Value::as_matrix3f() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Matrix3f || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const float*>(PodData(core_));
}

const float* Value::as_matrix4f() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Matrix4f || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  if (Boxed(core_)) return reinterpret_cast<const float*>(  BoxRef(core_));
  return reinterpret_cast<const float*>(PodData(core_));
}

const double* Value::as_matrix2d() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Matrix2d || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const double*>(PodData(core_));
}

const double* Value::as_matrix3d() const {
  if (static_cast<TypeId>(core_.type) != TypeId::Matrix3d || (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  return reinterpret_cast<const double*>(PodData(core_));
}

const double* Value::as_matrix4d() const {
  // frame4d is a matrix4d role type (same double[16] storage).
  if ((static_cast<TypeId>(core_.type) != TypeId::Matrix4d && static_cast<TypeId>(core_.type) != TypeId::Frame4d) ||
      (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
    return nullptr;
  }
  if (Boxed(core_)) return reinterpret_cast<const double*>(  BoxRef(core_));
  return reinterpret_cast<const double*>(PodData(core_));
}

// Array accessors
const std::vector<float>* Value::as_float_array() const {
  ensure_materialized();
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !IsFloatBackedArray(static_cast<TypeId>(core_.type))) return nullptr;
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<float>*>(ptr);
}

std::vector<float>* Value::as_float_array() {
  ensure_materialized();
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !IsFloatBackedArray(static_cast<TypeId>(core_.type))) return nullptr;
  core_.flags |= 0x01u;
  DetachArray(reinterpret_cast<char*>(core_.store.bytes));
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<float>*>(ptr);
}

const std::vector<int32_t>* Value::as_int_array() const {
  ensure_materialized();
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !IsIntBackedArray(static_cast<TypeId>(core_.type))) return nullptr;
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<int32_t>*>(ptr);
}

std::vector<int32_t>* Value::as_int_array() {
  ensure_materialized();
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !IsIntBackedArray(static_cast<TypeId>(core_.type))) return nullptr;
  core_.flags |= 0x01u;
  DetachArray(reinterpret_cast<char*>(core_.store.bytes));
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<int32_t>*>(ptr);
}

const std::vector<double>* Value::as_double_array() const {
  ensure_materialized();
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !IsDoubleBackedArray(static_cast<TypeId>(core_.type))) return nullptr;
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<double>*>(ptr);
}
std::vector<double>* Value::as_double_array() {
  ensure_materialized();
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !IsDoubleBackedArray(static_cast<TypeId>(core_.type))) return nullptr;
  core_.flags |= 0x01u;
  DetachArray(reinterpret_cast<char*>(core_.store.bytes));
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<double>*>(ptr);
}
const std::vector<int64_t>* Value::as_int64_array() const {
  ensure_materialized();
  if (static_cast<TypeId>(core_.type) != TypeId::Int64 || !(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<int64_t>*>(ptr);
}
std::vector<int64_t>* Value::as_int64_array() {
  ensure_materialized();
  if (static_cast<TypeId>(core_.type) != TypeId::Int64 || !(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  core_.flags |= 0x01u;
  DetachArray(reinterpret_cast<char*>(core_.store.bytes));
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<int64_t>*>(ptr);
}
const std::vector<uint32_t>* Value::as_uint_array() const {
  ensure_materialized();
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !IsUIntBackedArray(static_cast<TypeId>(core_.type))) return nullptr;
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<uint32_t>*>(ptr);
}
std::vector<uint32_t>* Value::as_uint_array() {
  ensure_materialized();
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !IsUIntBackedArray(static_cast<TypeId>(core_.type))) return nullptr;
  core_.flags |= 0x01u;
  DetachArray(reinterpret_cast<char*>(core_.store.bytes));
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<uint32_t>*>(ptr);
}
const std::vector<uint64_t>* Value::as_uint64_array() const {
  ensure_materialized();
  if (static_cast<TypeId>(core_.type) != TypeId::UInt64 || !(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<uint64_t>*>(ptr);
}
std::vector<uint64_t>* Value::as_uint64_array() {
  ensure_materialized();
  if (static_cast<TypeId>(core_.type) != TypeId::UInt64 || !(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  core_.flags |= 0x01u;
  DetachArray(reinterpret_cast<char*>(core_.store.bytes));
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<uint64_t>*>(ptr);
}
const std::vector<uint8_t>* Value::as_bool_array() const {
  ensure_materialized();
  if (static_cast<TypeId>(core_.type) != TypeId::Bool || !(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) return nullptr;
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<uint8_t>*>(ptr);
}
const std::vector<std::string>* Value::as_token_array() const {
  ensure_materialized();
  // Token / String / AssetPath / PathExpression arrays share the
  // string-vector storage.
  if (!(core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) || !UsesStringStorage(static_cast<TypeId>(core_.type))) return nullptr;
  void* ptr = ArraySlot(reinterpret_cast<const char*>(core_.store.bytes))->vector_object();
  return static_cast<std::vector<std::string>*>(ptr);
}

// ============================================================
// Comparison
// ============================================================

bool Value::operator==(const Value& other) const {
  struct ValuePair {
    const Value* lhs{nullptr};
    const Value* rhs{nullptr};
  };
  struct DictPair {
    const Dict* lhs{nullptr};
    const Dict* rhs{nullptr};
    bool operator==(const DictPair& other_pair) const {
      return lhs == other_pair.lhs && rhs == other_pair.rhs;
    }
  };
  struct DictPairHash {
    size_t operator()(const DictPair& pair) const {
      const size_t lhs_hash = std::hash<const Dict*>()(pair.lhs);
      const size_t rhs_hash = std::hash<const Dict*>()(pair.rhs);
      return lhs_hash ^ (rhs_hash + size_t(0x9e3779b9U) +
                         (lhs_hash << 6U) + (lhs_hash >> 2U));
    }
  };

  std::vector<ValuePair> pending;
  std::unordered_set<DictPair, DictPairHash> compared_dicts;
  pending.push_back(ValuePair{this, &other});

  while (!pending.empty()) {
    const ValuePair pair = pending.back();
    pending.pop_back();
    const Value& lhs = *pair.lhs;
    const Value& rhs = *pair.rhs;
    lhs.ensure_materialized();
    rhs.ensure_materialized();
    if (static_cast<TypeId>(lhs.core_.type) != static_cast<TypeId>(rhs.core_.type)) return false;
    if (((lhs).core_.kind == LU_VALUE_ARRAY || (lhs).core_.kind == LU_VALUE_LAZY_ARRAY) != ((rhs).core_.kind == LU_VALUE_ARRAY || (rhs).core_.kind == LU_VALUE_LAZY_ARRAY)) return false;
    // A value block differs from both declared-only and empty values.
    if ((lhs.core_.kind == LU_VALUE_BLOCK) != (rhs.core_.kind == LU_VALUE_BLOCK)) return false;
    if (((lhs).core_.kind == LU_VALUE_ARRAY || (lhs).core_.kind == LU_VALUE_LAZY_ARRAY) && lhs.array_size() != rhs.array_size()) return false;
    if (static_cast<TypeId>(lhs.core_.type) == TypeId::Invalid) continue;

    if (((lhs).core_.kind == LU_VALUE_ARRAY || (lhs).core_.kind == LU_VALUE_LAZY_ARRAY)) {
      bool equal = false;
      if (IsFloatBackedArray(static_cast<TypeId>(lhs.core_.type))) {
        equal = *lhs.as_float_array() == *rhs.as_float_array();
      } else if (IsDoubleBackedArray(static_cast<TypeId>(lhs.core_.type))) {
        equal = *lhs.as_double_array() == *rhs.as_double_array();
      } else if (static_cast<TypeId>(lhs.core_.type) == TypeId::Int) {
        equal = *lhs.as_int_array() == *rhs.as_int_array();
      } else if (static_cast<TypeId>(lhs.core_.type) == TypeId::Int64) {
        equal = *lhs.as_int64_array() == *rhs.as_int64_array();
      } else if (IsUIntBackedArray(static_cast<TypeId>(lhs.core_.type))) {
        equal = *lhs.as_uint_array() == *rhs.as_uint_array();
      } else if (static_cast<TypeId>(lhs.core_.type) == TypeId::UInt64) {
        equal = *lhs.as_uint64_array() == *rhs.as_uint64_array();
      } else if (static_cast<TypeId>(lhs.core_.type) == TypeId::Bool) {
        equal = *lhs.as_bool_array() == *rhs.as_bool_array();
      } else if (UsesStringStorage(static_cast<TypeId>(lhs.core_.type))) {
        equal = *lhs.as_token_array() == *rhs.as_token_array();
      } else if (IsIntBackedArray(static_cast<TypeId>(lhs.core_.type))) {
        equal = *lhs.as_int_array() == *rhs.as_int_array();
      }
      if (!equal) return false;
      continue;
    }

    if (static_cast<TypeId>(lhs.core_.type) == TypeId::Dictionary) {
      const Dict* a = lhs.as_dictionary();
      const Dict* b = rhs.as_dictionary();
      if (!a || !b) {
        if (a != b) return false;
        continue;
      }
      if (a == b) continue;
      if (!compared_dicts.insert(DictPair{a, b}).second) continue;
      if (a->entries().size() != b->entries().size()) return false;
      for (size_t i = a->entries().size(); i > 0; --i) {
        const auto& lhs_entry = a->entries()[i - 1];
        const auto& rhs_entry = b->entries()[i - 1];
        if (lhs_entry.first != rhs_entry.first) return false;
        pending.push_back(
            ValuePair{&lhs_entry.second, &rhs_entry.second});
      }
      continue;
    }

    if (UsesStringStorage(static_cast<TypeId>(lhs.core_.type))) {
      if (reinterpret_cast<const StringStorage*>(lhs.core_.store.bytes)->value !=
          reinterpret_cast<const StringStorage*>(rhs.core_.store.bytes)->value) {
        return false;
      }
      continue;
    }

    const size_t size = GetTypeSize(static_cast<TypeId>(lhs.core_.type));
    if (size == 0) return false;
    // Compare payload bytes. Inline values store data directly in the SBO;
    // boxed (oversized POD) values store a heap pointer there, so raw_bytes()
    // must be used to reach the actual payload on both sides.
    size_t ln = 0, rn = 0;
    const uint8_t* lb = lhs.raw_bytes(&ln);
    const uint8_t* rb = rhs.raw_bytes(&rn);
    if (ln != size || rn != size || !lb || !rb) return false;
    if (std::memcmp(lb, rb, size) != 0) {
      return false;
    }
  }

  return true;
}

// ============================================================
// Hashing
// ============================================================

namespace {

// FNV-1a hash for arbitrary byte sequences
inline uint64_t fnv1a_hash(const uint8_t* data, size_t len) {
  constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
  constexpr uint64_t FNV_PRIME = 1099511628211ULL;

  uint64_t hash = FNV_OFFSET;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint64_t>(data[i]);
    hash *= FNV_PRIME;
  }
  return hash;
}

}  // namespace

uint64_t Value::hash() const {
  ensure_materialized();
  if (static_cast<TypeId>(core_.type) == TypeId::Invalid) return 0;

  // Include type in hash
  uint64_t h = static_cast<uint64_t>(static_cast<TypeId>(core_.type)) | ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY) ? 0x100 : 0);

  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
    // Hash array contents
    if (IsFloatBackedArray(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_float_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(float));
      }
    } else if (IsDoubleBackedArray(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_double_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(double));
      }
    } else if (IsIntBackedArray(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_int_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(int32_t));
      }
    } else if (static_cast<TypeId>(core_.type) == TypeId::Int64) {
      const auto* arr = as_int64_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(int64_t));
      }
    } else if (IsUIntBackedArray(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_uint_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(uint32_t));
      }
    } else if (static_cast<TypeId>(core_.type) == TypeId::UInt64) {
      const auto* arr = as_uint64_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(uint64_t));
      }
    } else if (static_cast<TypeId>(core_.type) == TypeId::Bool) {
      const auto* arr = as_bool_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(uint8_t));
      }
    } else if (UsesStringStorage(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_token_array();
      if (arr) {
        for (const std::string& s : *arr) {
          h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(s.data()),
                          s.size());
          h *= 1099511628211ULL;
        }
      }
    }
    return h;
  }

  // Hash dictionary entries in post-order (order-sensitive, matching
  // operator==) without consuming one C++ stack frame per nesting level.
  if (static_cast<TypeId>(core_.type) == TypeId::Dictionary) {
    const Dict* root = as_dictionary();
    if (!root) return h;

    struct HashFrame {
      const Dict* dict{nullptr};
      size_t next_entry{0};
      uint64_t hash{static_cast<uint64_t>(TypeId::Dictionary)};
    };
    std::vector<HashFrame> pending;
    std::unordered_set<const Dict*> active;
    std::unordered_map<const Dict*, uint64_t> completed;
    pending.push_back(HashFrame{root, 0,
                                static_cast<uint64_t>(TypeId::Dictionary)});
    active.insert(root);

    while (!pending.empty()) {
      HashFrame& frame = pending.back();
      if (frame.next_entry == frame.dict->entries().size()) {
        const uint64_t child_hash = frame.hash;
        completed[frame.dict] = child_hash;
        active.erase(frame.dict);
        pending.pop_back();
        if (pending.empty()) return child_hash;
        pending.back().hash ^= child_hash;
        pending.back().hash *= 1099511628211ULL;
        continue;
      }

      const auto& kv = frame.dict->entries()[frame.next_entry++];
      frame.hash ^=
          fnv1a_hash(reinterpret_cast<const uint8_t*>(kv.first.data()),
                     kv.first.size());
      frame.hash *= 1099511628211ULL;

      const Dict* nested =
          kv.second.is_dictionary() ? kv.second.as_dictionary() : nullptr;
      if (!nested) {
        frame.hash ^= kv.second.hash();
        frame.hash *= 1099511628211ULL;
        continue;
      }

      const auto complete_it = completed.find(nested);
      if (complete_it != completed.end()) {
        frame.hash ^= complete_it->second;
        frame.hash *= 1099511628211ULL;
      } else if (active.find(nested) != active.end()) {
        // Mutable dictionary access can construct shared_ptr cycles that
        // cannot appear in parsed USD. Fold a stable dictionary marker rather
        // than looping forever on such API-created graphs.
        frame.hash ^= static_cast<uint64_t>(TypeId::Dictionary);
        frame.hash *= 1099511628211ULL;
      } else {
        active.insert(nested);
        pending.push_back(HashFrame{
            nested, 0, static_cast<uint64_t>(TypeId::Dictionary)});
      }
    }
    return h;  // unreachable, retained for defensive compiler flow analysis
  }

  // Hash string types
  if (UsesStringStorage(static_cast<TypeId>(core_.type))) {
    const auto& s = reinterpret_cast<const StringStorage*>(core_.store.bytes)->value;
    if (!s.empty()) {
      h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    return h;
  }

  // Hash scalar/vector types from storage
  size_t size = GetTypeSize(static_cast<TypeId>(core_.type));
  if (size > 0) {
    size_t nbytes = 0;
    const uint8_t* bytes = raw_bytes(&nbytes);
    if (bytes && nbytes == size) {
      h ^= fnv1a_hash(bytes, size);
    }
  }

  return h;
}

const uint8_t* Value::raw_bytes(size_t* out_size) const {
  if (!out_size) return nullptr;
  *out_size = 0;

  ensure_materialized();
  if (static_cast<TypeId>(core_.type) == TypeId::Invalid) return nullptr;

  if ((core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY)) {
    if (IsFloatBackedArray(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_float_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(float);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (IsDoubleBackedArray(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_double_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(double);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (IsIntBackedArray(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_int_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(int32_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (static_cast<TypeId>(core_.type) == TypeId::Int64) {
      const auto* arr = as_int64_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(int64_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (IsUIntBackedArray(static_cast<TypeId>(core_.type))) {
      const auto* arr = as_uint_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(uint32_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (static_cast<TypeId>(core_.type) == TypeId::UInt64) {
      const auto* arr = as_uint64_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(uint64_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (static_cast<TypeId>(core_.type) == TypeId::Bool) {
      const auto* arr = as_bool_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(uint8_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    }
    return nullptr;
  }

  if (UsesStringStorage(static_cast<TypeId>(core_.type))) {
    const auto& s = reinterpret_cast<const StringStorage*>(core_.store.bytes)->value;
    *out_size = s.size();
    return reinterpret_cast<const uint8_t*>(s.data());
  }

  if (Boxed(core_) && GetTypeSize(static_cast<TypeId>(core_.type)) > 0) {
    *out_size = GetTypeSize(static_cast<TypeId>(core_.type));
    return static_cast<const uint8_t*>(  BoxRef(core_));
  }
  size_t size = GetTypeSize(static_cast<TypeId>(core_.type));
  if (size > 0) {
    *out_size = size;
    return reinterpret_cast<const uint8_t*>(PodData(core_));
  }

  return nullptr;
}

Value LerpValue(const Value& a, const Value& b, double t) {
  if (a.type_id() != b.type_id()) return a;  // held on type mismatch

  // Delegate every interpolatable type to the full interpolator (per-lane
  // lerp for float/double/half-backed values, slerp for quats). The previous
  // hand-rolled scalar switch here silently HELD Half/Half2/3/4, the
  // semantic half aliases, Quath, and Matrix2f/3f/2d/3d, so timeSamples of
  // those types snapped to the earlier key when queried between samples.
  if (a.is_array() || TimeInterpolator::IsLinearInterpolatable(a.type_id())) {
    Value r = TimeInterpolator::InterpolateValues(a, b, t);
    return (r.type_id() == TypeId::Invalid) ? a : r;
  }
  return a;  // non-interpolatable (int/bool/string/token/...) -> held
}

// ---------------------------------------------------------------------------
// Inline method bodies moved out of the header so units that include it do
// not re-parse them.
// ---------------------------------------------------------------------------
TypeId Value::type_id() const { return static_cast<TypeId>(core_.type); }
bool Value::is_empty() const { return static_cast<TypeId>(core_.type) == TypeId::Invalid && !(core_.kind == LU_VALUE_BLOCK); }
bool Value::is_block() const { return (core_.kind == LU_VALUE_BLOCK); }
bool Value::is_array() const { return (core_.kind == LU_VALUE_ARRAY || core_.kind == LU_VALUE_LAZY_ARRAY); }
bool Value::is_dictionary() const { return static_cast<TypeId>(core_.type) == TypeId::Dictionary; }
bool Value::is_lazy() const { return (core_.kind == LU_VALUE_LAZY_ARRAY); }
bool Value::is_dirty() const { return ((core_.flags & 0x01u) != 0); }
void Value::mark_dirty() { core_.flags |= 0x01u; }
size_t Dict::size() const { return entries_.size(); }
bool Dict::empty() const { return entries_.empty(); }
bool Dict::typed_json_wrapper() const { return typed_json_wrapper_; }
void Dict::set_typed_json_wrapper(bool value) { typed_json_wrapper_ = value; }


void Value::adopt_pod(const void* data, size_t size) {
  if (!data || size == 0) return;
  // Release any prior out-of-line box before the inline bytes are overwritten.
  if (Boxed(core_)) {
    delete[] static_cast<unsigned char*>(BoxRef(core_));
  }
  if (size <= kSBOSize) {
    std::memcpy(core_.store.bytes, data, size);
    core_.kind = LU_VALUE_POD;
  } else {
    auto* p = new unsigned char[size];
    std::memcpy(p, data, size);
    BoxRef(core_) = p;
    core_.kind = LU_VALUE_BOX;
  }
}

}  // namespace next
}  // namespace lightusd


