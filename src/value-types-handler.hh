// SPDX-License-Identifier: Apache 2.0
// Copyright 2024, Syoyo Fujita.
// Copyright 2024, Light Transport Entertainment Inc.
//
// New 32-byte handler-based Value implementation
// Enabled with TUSDZ_NEW_32BYTE_VALUE preprocessor flag
//
// Design principles:
// - Union storage (24 bytes): type-safe inline OR heap storage
// - Handler function pointer (8 bytes): encodes type + storage location + operations
// - No redundant type_id/flags fields
// - Based on std::any handler pattern + linb::any TypeTraits pattern
//

#pragma once

#include <type_traits>
#include <utility>
#include <cstdint>
#include <cstring>
#include <new>

namespace tinyusdz {

// Forward declaration
class Value32;

// Handler action enum
enum class ValueAction : uint8_t {
  Destroy,   // Destroy the value
  Copy,      // Copy construct value
  Move,      // Move construct value  
  Get,       // Get pointer to value
  TypeId,    // Get type_id (uint32_t)
  TypeName,  // Get type name (const char*)
  ArraySize  // Get array size if array type
};

// Handler function signature
// Returns void* which is interpreted based on action:
// - Destroy/Copy/Move: returns nullptr
// - Get: returns pointer to the value
// - TypeId: returns type_id as uintptr_t (cast to uint32_t)
// - TypeName: returns const char* pointer
// - ArraySize: returns size_t as uintptr_t
using ValueHandler = void* (*)(ValueAction action, const Value32* src, Value32* dst);

// Empty handler for null/empty values
inline void* empty_handler(ValueAction action, const Value32* src, Value32* dst) {
  (void)src;
  (void)dst;
  switch (action) {
    case ValueAction::TypeId:
      return reinterpret_cast<void*>(static_cast<uintptr_t>(0)); // TYPE_ID_NULL
    case ValueAction::TypeName:
      return const_cast<void*>(static_cast<const void*>("null"));
    default:
      return nullptr;
  }
}

//
// Type traits for compile-time type information
//
template <typename T>
struct TypeTraits {
  // Get type_id for type T
  static constexpr uint32_t type_id();
  
  // Get type name for type T  
  static constexpr const char* type_name();
  
  // Decide if T should use inline storage
  static constexpr bool use_inline() {
    return sizeof(T) <= 24 && 
           alignof(T) <= 8 &&
           std::is_nothrow_move_constructible<T>::value;
  }
};

//
// 32-byte Value class with handler pattern
//
class Value32 {
public:
  // Default constructor - empty value
  Value32() noexcept : storage_(), handler_(&empty_handler) {
  }
  
  // Destructor
  ~Value32() {
    destroy();
  }
  
  // Copy constructor
  Value32(const Value32& other) : storage_(), handler_(&empty_handler) {
    if (other.handler_ != &empty_handler) {
      other.handler_(ValueAction::Copy, &other, this);
    }
  }
  
  // Move constructor
  Value32(Value32&& other) noexcept : storage_(), handler_(&empty_handler) {
    if (other.handler_ != &empty_handler) {
      other.handler_(ValueAction::Move, &other, this);
      other.handler_ = &empty_handler;
    }
  }
  
  // Copy assignment
  Value32& operator=(const Value32& other) {
    if (this != &other) {
      destroy();
      if (other.handler_ != &empty_handler) {
        other.handler_(ValueAction::Copy, &other, this);
      }
    }
    return *this;
  }
  
  // Move assignment
  Value32& operator=(Value32&& other) noexcept {
    if (this != &other) {
      destroy();
      if (other.handler_ != &empty_handler) {
        other.handler_(ValueAction::Move, &other, this);
        other.handler_ = &empty_handler;
      }
    }
    return *this;
  }
  
  // Template constructor for any type T
  template <typename T>
  explicit Value32(const T& value);
  
  // Set value
  template <typename T>
  void set(const T& value);
  
  // Get value pointer (const)
  template <typename T>
  const T* as() const {
    if (handler_ == &empty_handler) return nullptr;
    
    // TODO: Add type checking via TypeId action
    void* ptr = handler_(ValueAction::Get, this, nullptr);
    return static_cast<const T*>(ptr);
  }
  
  // Get value pointer (non-const)
  template <typename T>
  T* as() {
    if (handler_ == &empty_handler) return nullptr;
    
    void* ptr = handler_(ValueAction::Get, this, nullptr);
    return static_cast<T*>(ptr);
  }
  
  // Query methods for compatibility with old API
  uint32_t type_id() const {
    if (handler_ == &empty_handler) return 0;
    return static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(
        handler_(ValueAction::TypeId, this, nullptr)
      )
    );
  }
  
  const char* type_name() const {
    if (handler_ == &empty_handler) return "null";
    return static_cast<const char*>(
      handler_(ValueAction::TypeName, this, nullptr)
    );
  }
  
  bool is_empty() const {
    return handler_ == &empty_handler;
  }
  
  size_t array_size() const {
    if (handler_ == &empty_handler) return 0;
    return static_cast<size_t>(
      reinterpret_cast<uintptr_t>(
        handler_(ValueAction::ArraySize, this, nullptr)
      )
    );
  }
  
  // Allow handler to access storage
  friend void* get_storage_ptr(const Value32* v) {
    return const_cast<void*>(static_cast<const void*>(&v->storage_));
  }
  
  friend void set_handler(Value32* v, ValueHandler h) {
    v->handler_ = h;
  }

private:
  void destroy() {
    if (handler_ != &empty_handler) {
      handler_(ValueAction::Destroy, this, nullptr);
      handler_ = &empty_handler;
    }
  }
  
  // Union storage: EITHER heap pointer OR inline data (24 bytes)
  union Storage {
    void* ptr;                                    // Heap pointer (8 bytes)
    alignas(8) uint8_t buf[24];                  // Inline storage (24 bytes)

    // C++14 compatible: use value initialization instead of memset
    Storage() : ptr(nullptr) {}
  };
  
  Storage storage_;         // 24 bytes
  ValueHandler handler_;    // 8 bytes
  // Total: 32 bytes
};

// Static assert to verify size
static_assert(sizeof(Value32) == 32, "Value32 must be exactly 32 bytes");

} // namespace tinyusdz
