// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Built-in USD layouts. Metadata only: no per-type executable callbacks.
//
// The table and the USD <-> TypeId name mapping live in the C11 core
// (src/next/c/type-names.c), so C consumers (the C ABI) share one copy of the
// type system. This file is the C++ facade.

#include "type-info.hh"

#include <cstring>

#include "../c/lightusd-core-c.h"

namespace lightusd {
namespace next {
namespace {

// TypeInfo and lu_type_info must be layout-compatible: the accessors below
// reinterpret_cast a C row to a TypeInfo. Both are standard-layout with the
// same field order and representation.
static_assert(sizeof(TypeInfo) == sizeof(lu_type_info),
              "TypeInfo / lu_type_info size mismatch");
static_assert(alignof(TypeInfo) == alignof(lu_type_info),
              "TypeInfo / lu_type_info alignment mismatch");

inline constexpr uint16_t T(TypeId id) { return static_cast<uint16_t>(id); }

}  // namespace

const TypeInfo* GetTypeInfo(TypeId id) {
  return reinterpret_cast<const TypeInfo*>(lu_type_info_at(T(id)));
}

void InitTypeRegistry() {}  // Built-in metadata is constant-initialized.

const char* GetTypeName(TypeId id) { return lu_type_name(T(id)); }

TypeId GetTypeIdFromName(const char* name, size_t size) {
  return static_cast<TypeId>(lu_type_id_from_name(name, size));
}

TypeId GetTypeIdFromName(const char* name) {
  return name ? GetTypeIdFromName(name, std::strlen(name)) : TypeId::Invalid;
}

size_t GetTypeSize(TypeId id) { return lu_type_size(T(id)); }

size_t GetTypeAlignment(TypeId id) { return lu_type_alignment(T(id)); }

bool IsScalarType(TypeId id) { return lu_type_is_scalar(T(id)) != 0; }

bool IsNumericType(TypeId id) { return lu_type_is_numeric(T(id)) != 0; }

TypeId GetComponentType(TypeId id) {
  return static_cast<TypeId>(lu_type_component_type(T(id)));
}

size_t GetComponentCount(TypeId id) { return lu_type_component_count(T(id)); }

}  // namespace next
}  // namespace lightusd
