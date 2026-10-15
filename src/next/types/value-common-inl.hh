// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "value.hh"
#include "../crate/lazy-array.hh"

namespace lightusd {
namespace next {

// Type storage helpers
// ============================================================

// String-like types stored in SBO using std::string
struct StringStorage {
  std::string value;
};

using detail::ArrayHandle;
inline ArrayHandle* ArraySlot(char* s) {
  return reinterpret_cast<ArrayHandle*>(s);
}
inline const ArrayHandle* ArraySlot(const char* s) {
  return reinterpret_cast<const ArrayHandle*>(s);
}
inline void DetachArray(char* s) { ArraySlot(s)->detach(); }

// Dictionary values are held by a shared_ptr<Dict> in the SBO slot — the same
// copy-on-write model as arrays (cheap copy during composition; detach on first
// mutation). A nested dict is itself just another shared_ptr handle, so the
// recursive structure never threatens the SBO size.
using DictHandle = std::shared_ptr<Dict>;
inline DictHandle* DictSlot(char* s) {
  return reinterpret_cast<DictHandle*>(s);
}
inline const DictHandle* DictSlot(const char* s) {
  return reinterpret_cast<const DictHandle*>(s);
}
inline void DetachDict(char* s) {
  DictHandle& h = *DictSlot(s);
  if (h.use_count() > 1) h = std::make_shared<Dict>(*h);
}

// Check if type uses string storage
inline bool UsesStringStorage(TypeId id) {
  return lu_value_uses_string_storage(static_cast<uint16_t>(id)) != 0;
}

// Array element types stored as flat float components:
// scalar floats plus all float vector/quat/matrix/color types.
inline bool IsFloatBackedArray(TypeId id) {
  return lu_value_is_float_backed_array(static_cast<uint16_t>(id)) != 0;
}

// Double-backed array types.
// Int-vector element types share the flat int32 array storage with Int.
inline bool IsIntBackedArray(TypeId id) {
  return lu_value_is_int_backed_array(static_cast<uint16_t>(id)) != 0;
}

// UInt-vector element types share the flat uint32 array storage with UInt.
// uchar[] is widened into the same storage (bit-exact; the crate writer
// narrows back to tightly-packed uint8).
inline bool IsUIntBackedArray(TypeId id) {
  return lu_value_is_uint_backed_array(static_cast<uint16_t>(id)) != 0;
}

inline bool IsDoubleBackedArray(TypeId id) {
  return lu_value_is_double_backed_array(static_cast<uint16_t>(id)) != 0;
}

inline bool FitsValueArraySize(size_t count) {
  return lu_value_fits_array_size(count) != 0;
}

inline bool IsValidComponentArray(size_t scalar_count, TypeId elem_type,
                           uint32_t comps_per_elem, bool storage_matches) {
  return lu_value_is_valid_component_array(
             scalar_count, static_cast<uint16_t>(elem_type), comps_per_elem,
             storage_matches ? 1 : 0) != 0;
}

inline size_t ComponentArraySize(size_t scalar_count, size_t components) {
  return lu_value_component_array_size(scalar_count, components);
}

inline bool IsValueArrayType(TypeId id) {
  return lu_value_is_array_type(static_cast<uint16_t>(id)) != 0;
}

inline bool FinalizeLazyDecode(const LazyArrayRef& ref, Value* decoded) {
  if (!decoded || decoded->is_lazy() || !decoded->is_array()) return false;
  if (decoded->array_size() != ref.element_count) return false;
  decoded->retag_role(ref.value_type);
  return decoded->type_id() == ref.value_type;
}
}  // namespace next
}  // namespace lightusd
