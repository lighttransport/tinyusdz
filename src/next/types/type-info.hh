// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Runtime type information registry
// Provides runtime type operations without virtual functions

#pragma once

#include "type-id.hh"

namespace tinyusdz {
namespace next {

/// Runtime type information structure. One instance exists per TypeId in a
/// static array. Value dispatches copy/move/destroy/equality by STORAGE CATEGORY
/// (inline POD / string / array / dict / boxed scalar) in value.cc, not per
/// TypeId — so this record carries only the identity + layout metadata that
/// GetTypeSize()/GetTypeName() need. (A former per-type construct/destruct/copy/
/// move/equals function-pointer table here was never called and instantiated
/// ~300 dead template functions; it was removed.)
struct TypeInfo {
  TypeId id;
  const char* name;         // USD type name (e.g., "float3")
  const char* cpp_name;     // C++ type name (e.g., "GfVec3f")
  size_t size;              // sizeof(T)
  size_t alignment;         // alignof(T)
};

/// Get type info by TypeId
/// Returns nullptr for Invalid or out-of-range TypeId
/// O(1) lookup via static array indexing
const TypeInfo* GetTypeInfo(TypeId id);

/// Register a custom type (for extension)
/// Returns false if registration fails (e.g., ID already registered)
bool RegisterTypeInfo(const TypeInfo& info);

/// Initialize the type registry
/// Called automatically on first use, but can be called explicitly
void InitTypeRegistry();

}  // namespace next
}  // namespace tinyusdz
