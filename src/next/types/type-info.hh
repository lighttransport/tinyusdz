// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Runtime type information registry
// Provides runtime type operations without virtual functions

#pragma once

#include "type-id.hh"

namespace tinyusdz {
namespace next {

/// Function pointer types for type operations
/// These replace virtual function calls with direct function pointer dispatch
using ConstructFn = void (*)(void* dest);
using DestructFn = void (*)(void* obj);
using CopyFn = void (*)(void* dest, const void* src);
using MoveFn = void (*)(void* dest, void* src);
using EqualsFn = bool (*)(const void* a, const void* b);

/// Runtime type information structure
/// One instance exists per TypeId in a static array
struct TypeInfo {
  TypeId id;
  const char* name;         // USD type name (e.g., "float3")
  const char* cpp_name;     // C++ type name (e.g., "GfVec3f")
  size_t size;              // sizeof(T)
  size_t alignment;         // alignof(T)

  // Operation function pointers
  ConstructFn construct;    // Default constructor
  DestructFn destruct;      // Destructor
  CopyFn copy;              // Copy assignment
  MoveFn move;              // Move assignment
  EqualsFn equals;          // Equality comparison
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
