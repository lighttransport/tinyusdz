// SPDX-License-Identifier: Apache 2.0
// Copyright 2024, Syoyo Fujita.
// Copyright 2024, Light Transport Entertainment Inc.
//
// Handler implementations for Value32
//

#include "value-types-handler.hh"
#include "value-types.hh" // For TYPE_ID_* constants

#include <string>
#include <vector>

namespace tinyusdz {

//
// Helper functions to access Value32 internals
//
namespace detail {

template <typename T>
inline T* get_inline_ptr(const Value32* v) {
  void* storage = get_storage_ptr(v);
  return reinterpret_cast<T*>(static_cast<uint8_t*>(storage));
}

template <typename T>
inline T* get_heap_ptr(const Value32* v) {
  void* storage = get_storage_ptr(v);
  void* ptr = *static_cast<void**>(storage);
  return static_cast<T*>(ptr);
}

} // namespace detail

//
// Generic inline handler template
//
template <typename T, uint32_t TypeId>
void* handler_inline(ValueAction action, const Value32* src, Value32* dst) {
  switch (action) {
    case ValueAction::Destroy: {
      // In-place destruction
      auto* ptr = detail::get_inline_ptr<T>(src);
      ptr->~T();
      return nullptr;
    }
    
    case ValueAction::Copy: {
      // Placement new copy construction
      const auto* src_ptr = detail::get_inline_ptr<T>(src);
      auto* dst_ptr = detail::get_inline_ptr<T>(dst);
      new (dst_ptr) T(*src_ptr);
      set_handler(dst, &handler_inline<T, TypeId>);
      return nullptr;
    }
    
    case ValueAction::Move: {
      // Move construct + destroy source
      auto* src_ptr = detail::get_inline_ptr<T>(src);
      auto* dst_ptr = detail::get_inline_ptr<T>(dst);
      new (dst_ptr) T(std::move(*src_ptr));
      src_ptr->~T();
      set_handler(dst, &handler_inline<T, TypeId>);
      return nullptr;
    }
    
    case ValueAction::Get: {
      // Return pointer to value
      return detail::get_inline_ptr<T>(src);
    }
    
    case ValueAction::TypeId: {
      return reinterpret_cast<void*>(static_cast<uintptr_t>(TypeId));
    }
    
    case ValueAction::TypeName: {
      return const_cast<void*>(
        static_cast<const void*>(TypeTraits<T>::type_name())
      );
    }
    
    case ValueAction::ArraySize: {
      return reinterpret_cast<void*>(static_cast<uintptr_t>(0));
    }
  }
  return nullptr;
}

//
// Generic heap handler template
//
template <typename T, uint32_t TypeId>
void* handler_heap(ValueAction action, const Value32* src, Value32* dst) {
  switch (action) {
    case ValueAction::Destroy: {
      // Heap deallocation
      auto* ptr = detail::get_heap_ptr<T>(src);
      delete ptr;
      return nullptr;
    }
    
    case ValueAction::Copy: {
      // Heap allocate + copy
      const auto* src_ptr = detail::get_heap_ptr<T>(src);
      auto* dst_storage = static_cast<void**>(get_storage_ptr(dst));
      *dst_storage = new T(*src_ptr);
      set_handler(dst, &handler_heap<T, TypeId>);
      return nullptr;
    }
    
    case ValueAction::Move: {
      // Just transfer pointer ownership!
      auto* src_storage = static_cast<void**>(get_storage_ptr(src));
      auto* dst_storage = static_cast<void**>(get_storage_ptr(dst));
      *dst_storage = *src_storage;
      set_handler(dst, &handler_heap<T, TypeId>);
      return nullptr;
    }
    
    case ValueAction::Get: {
      return detail::get_heap_ptr<T>(src);
    }
    
    case ValueAction::TypeId: {
      return reinterpret_cast<void*>(static_cast<uintptr_t>(TypeId));
    }
    
    case ValueAction::TypeName: {
      return const_cast<void*>(
        static_cast<const void*>(TypeTraits<T>::type_name())
      );
    }
    
    case ValueAction::ArraySize: {
      return reinterpret_cast<void*>(static_cast<uintptr_t>(0));
    }
  }
  return nullptr;
}

//
// TypeTraits specializations for primitive types
//

// bool
template <>
constexpr uint32_t TypeTraits<bool>::type_id() { return value::TYPE_ID_BOOL; }
template <>
constexpr const char* TypeTraits<bool>::type_name() { return "bool"; }

// int32_t
template <>
constexpr uint32_t TypeTraits<int32_t>::type_id() { return value::TYPE_ID_INT32; }
template <>
constexpr const char* TypeTraits<int32_t>::type_name() { return "int"; }

// uint32_t
template <>
constexpr uint32_t TypeTraits<uint32_t>::type_id() { return value::TYPE_ID_UINT32; }
template <>
constexpr const char* TypeTraits<uint32_t>::type_name() { return "uint"; }

// int64_t
template <>
constexpr uint32_t TypeTraits<int64_t>::type_id() { return value::TYPE_ID_INT64; }
template <>
constexpr const char* TypeTraits<int64_t>::type_name() { return "int64"; }

// uint64_t
template <>
constexpr uint32_t TypeTraits<uint64_t>::type_id() { return value::TYPE_ID_UINT64; }
template <>
constexpr const char* TypeTraits<uint64_t>::type_name() { return "uint64"; }

// float
template <>
constexpr uint32_t TypeTraits<float>::type_id() { return value::TYPE_ID_FLOAT; }
template <>
constexpr const char* TypeTraits<float>::type_name() { return "float"; }

// double
template <>
constexpr uint32_t TypeTraits<double>::type_id() { return value::TYPE_ID_DOUBLE; }
template <>
constexpr const char* TypeTraits<double>::type_name() { return "double"; }

// std::string
template <>
constexpr uint32_t TypeTraits<std::string>::type_id() { return value::TYPE_ID_STRING; }
template <>
constexpr const char* TypeTraits<std::string>::type_name() { return "string"; }

//
// Value32 template method implementations
//

template <typename T>
Value32::Value32(const T& value) : storage_(), handler_(&empty_handler) {
  set(value);
}

template <typename T>
void Value32::set(const T& value) {
  destroy(); // Clear old value first
  
  if (TypeTraits<T>::use_inline()) {
    // Inline storage - use placement new
    auto* ptr = reinterpret_cast<T*>(storage_.buf);
    new (ptr) T(value);
    handler_ = &handler_inline<T, TypeTraits<T>::type_id()>;
  } else {
    // Heap storage
    storage_.ptr = new T(value);
    handler_ = &handler_heap<T, TypeTraits<T>::type_id()>;
  }
}

// Explicit template instantiations for primitive types
template Value32::Value32(const bool&);
template Value32::Value32(const int32_t&);
template Value32::Value32(const uint32_t&);
template Value32::Value32(const int64_t&);
template Value32::Value32(const uint64_t&);
template Value32::Value32(const float&);
template Value32::Value32(const double&);
template Value32::Value32(const std::string&);

template void Value32::set(const bool&);
template void Value32::set(const int32_t&);
template void Value32::set(const uint32_t&);
template void Value32::set(const int64_t&);
template void Value32::set(const uint64_t&);
template void Value32::set(const float&);
template void Value32::set(const double&);
template void Value32::set(const std::string&);

} // namespace tinyusdz
