// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// New optimized Value implementation
//
#ifdef TUSDZ_NEW_VALUE_TYPE

// Include the full value-types.hh to get all type definitions
// This will pull in value-types-new.hh as well
#include "value-types.hh"
#include <vector>
#include <string>

namespace tinyusdz {
namespace value {

//
// Helper macros for type dispatch
//
#define DISPATCH_SCALAR_TYPE(MACRO) \
  MACRO(bool) \
  MACRO(uint8_t) \
  MACRO(char) \
  MACRO(int16_t) \
  MACRO(uint16_t) \
  MACRO(int32_t) \
  MACRO(uint32_t) \
  MACRO(int64_t) \
  MACRO(uint64_t) \
  MACRO(half) \
  MACRO(float) \
  MACRO(double) \
  MACRO(half2) \
  MACRO(half3) \
  MACRO(half4) \
  MACRO(int2) \
  MACRO(int3) \
  MACRO(int4) \
  MACRO(uint2) \
  MACRO(uint3) \
  MACRO(uint4) \
  MACRO(float2) \
  MACRO(float3) \
  MACRO(float4) \
  MACRO(double2) \
  MACRO(double3) \
  MACRO(double4) \
  MACRO(quath) \
  MACRO(quatf) \
  MACRO(quatd) \
  MACRO(matrix2f) \
  MACRO(matrix3f) \
  MACRO(matrix4f) \
  MACRO(matrix2d) \
  MACRO(matrix3d) \
  MACRO(matrix4d) \
  MACRO(normal3h) \
  MACRO(normal3f) \
  MACRO(normal3d) \
  MACRO(vector3h) \
  MACRO(vector3f) \
  MACRO(vector3d) \
  MACRO(point3h) \
  MACRO(point3f) \
  MACRO(point3d) \
  MACRO(color3h) \
  MACRO(color3f) \
  MACRO(color3d) \
  MACRO(color4h) \
  MACRO(color4f) \
  MACRO(color4d) \
  MACRO(texcoord2h) \
  MACRO(texcoord2f) \
  MACRO(texcoord2d) \
  MACRO(texcoord3h) \
  MACRO(texcoord3f) \
  MACRO(texcoord3d) \
  MACRO(frame4d)

#define DISPATCH_STRING_TYPE(MACRO) \
  MACRO(std::string) \
  MACRO(token) \
  MACRO(AssetPath)

// Helper to get array class from flags
static inline NewValue::ArrayClass GetArrayClass(uint8_t flags) {
  if (!(flags & NewValue::kArrayBitFlag)) {
    return NewValue::ArrayClass::Invalid;
  }
  uint8_t class_bits = (flags & NewValue::kArrayClassMask) >> NewValue::kArrayClassShift;
  return static_cast<NewValue::ArrayClass>(class_bits);
}

//
// Copy data from another NewValue
//
void NewValue::copy_data_from(const NewValue& rhs) {
  if (rhs.is_empty()) {
    std::memset(data_, 0, sizeof(data_));
    return;
  }

  // Check if heap-allocated
  if (rhs.flags_ & kHeapAllocatedFlag) {
    // Need to deep copy heap-allocated data
    // Extract the pointer from rhs.data_
    void* src_ptr;
    std::memcpy(&src_ptr, rhs.data_, sizeof(void*));

    // Dispatch based on type_id to call appropriate copy constructor

#define COPY_SCALAR_TYPE(T) \
    if (rhs.type_id_ == TypeTraits<T>::type_id()) { \
      const T* src = reinterpret_cast<const T*>(src_ptr); \
      T* dst = new T(*src); \
      void* dst_ptr = reinterpret_cast<void*>(dst); \
      std::memcpy(data_, &dst_ptr, sizeof(void*)); \
      return; \
    }

#define COPY_ARRAY_TYPE_VECTOR(T) \
    if (rhs.type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_1D_ARRAY_BIT)) { \
      ArrayClass aclass = GetArrayClass(rhs.flags_); \
      if (aclass == ArrayClass::StdVector) { \
        const std::vector<T>* src = reinterpret_cast<const std::vector<T>*>(src_ptr); \
        std::vector<T>* dst = new std::vector<T>(*src); \
        void* dst_ptr = reinterpret_cast<void*>(dst); \
        std::memcpy(data_, &dst_ptr, sizeof(void*)); \
        return; \
      } else if (aclass == ArrayClass::TypedArray) { \
        const TypedArray<T>* src = reinterpret_cast<const TypedArray<T>*>(src_ptr); \
        TypedArray<T>* dst = new TypedArray<T>(*src); \
        void* dst_ptr = reinterpret_cast<void*>(dst); \
        std::memcpy(data_, &dst_ptr, sizeof(void*)); \
        return; \
      } \
    }

    DISPATCH_SCALAR_TYPE(COPY_SCALAR_TYPE)
    DISPATCH_SCALAR_TYPE(COPY_ARRAY_TYPE_VECTOR)
    DISPATCH_STRING_TYPE(COPY_SCALAR_TYPE)
    DISPATCH_STRING_TYPE(COPY_ARRAY_TYPE_VECTOR)

#undef COPY_SCALAR_TYPE
#undef COPY_ARRAY_TYPE_VECTOR

    // Fallback for unknown types - this shouldn't happen in correct usage
    std::memset(data_, 0, sizeof(data_));
    type_id_ = TYPE_ID_INVALID;
  } else {
    // Inline data - simple copy
    std::memcpy(data_, rhs.data_, sizeof(data_));
  }
}

//
// Destroy heap-allocated data
//
void NewValue::destroy() {
  if (is_empty() || type_id_ == TYPE_ID_INVALID) {
    return;
  }

  if (!(flags_ & kHeapAllocatedFlag)) {
    // Inlined data - nothing to free
    return;
  }

  // Heap-allocated - need to delete based on actual type
  // Extract pointer from data_
  void* ptr;
  std::memcpy(&ptr, data_, sizeof(void*));

#define DESTROY_SCALAR_TYPE(T) \
  if (type_id_ == TypeTraits<T>::type_id()) { \
    T* typed_ptr = reinterpret_cast<T*>(ptr); \
    delete typed_ptr; \
    return; \
  }

#define DESTROY_ARRAY_TYPE(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_1D_ARRAY_BIT)) { \
    ArrayClass aclass = GetArrayClass(flags_); \
    if (aclass == ArrayClass::StdVector) { \
      std::vector<T>* typed_ptr = reinterpret_cast<std::vector<T>*>(ptr); \
      delete typed_ptr; \
    } else if (aclass == ArrayClass::TypedArray) { \
      TypedArray<T>* typed_ptr = reinterpret_cast<TypedArray<T>*>(ptr); \
      delete typed_ptr; \
    } \
    return; \
  }

  DISPATCH_SCALAR_TYPE(DESTROY_SCALAR_TYPE)
  DISPATCH_SCALAR_TYPE(DESTROY_ARRAY_TYPE)
  DISPATCH_STRING_TYPE(DESTROY_SCALAR_TYPE)
  DISPATCH_STRING_TYPE(DESTROY_ARRAY_TYPE)

#undef DESTROY_SCALAR_TYPE
#undef DESTROY_ARRAY_TYPE
}

//
// Get array size for array types
//
size_t NewValue::array_size() const {
  if (!is_array()) {
    return 0;
  }

  // Extract pointer from data_
  void* ptr;
  std::memcpy(&ptr, data_, sizeof(void*));

#define GET_ARRAY_SIZE(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_1D_ARRAY_BIT)) { \
    ArrayClass aclass = GetArrayClass(flags_); \
    if (aclass == ArrayClass::StdVector) { \
      const std::vector<T>* vec = reinterpret_cast<const std::vector<T>*>(ptr); \
      return vec->size(); \
    } else if (aclass == ArrayClass::TypedArray) { \
      const TypedArray<T>* arr = reinterpret_cast<const TypedArray<T>*>(ptr); \
      return arr->size(); \
    } \
  }

  DISPATCH_SCALAR_TYPE(GET_ARRAY_SIZE)

#undef GET_ARRAY_SIZE

  return 0;
}

//
// Estimate memory usage
//
size_t NewValue::estimate_memory_usage() const {
  size_t total = sizeof(NewValue);  // Base object size

  if (is_empty() || is_none()) {
    return total;
  }

  if (!(flags_ & kHeapAllocatedFlag)) {
    // Inlined - no additional memory
    return total;
  }

  // Heap-allocated - estimate based on type

#define ESTIMATE_SCALAR_SIZE(T) \
  if (type_id_ == TypeTraits<T>::type_id()) { \
    return total + sizeof(T); \
  }

#define ESTIMATE_ARRAY_SIZE(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_1D_ARRAY_BIT)) { \
    void* ptr; \
    std::memcpy(&ptr, data_, sizeof(void*)); \
    ArrayClass aclass = GetArrayClass(flags_); \
    if (aclass == ArrayClass::StdVector) { \
      const std::vector<T>* vec = reinterpret_cast<const std::vector<T>*>(ptr); \
      return total + sizeof(std::vector<T>) + (vec->capacity() * sizeof(T)); \
    } else if (aclass == ArrayClass::TypedArray) { \
      const TypedArray<T>* arr = reinterpret_cast<const TypedArray<T>*>(ptr); \
      return total + sizeof(TypedArray<T>) + (arr->size() * sizeof(T)); \
    } \
  }

  DISPATCH_SCALAR_TYPE(ESTIMATE_SCALAR_SIZE)
  DISPATCH_SCALAR_TYPE(ESTIMATE_ARRAY_SIZE)

#undef ESTIMATE_SCALAR_SIZE
#undef ESTIMATE_ARRAY_SIZE

  // String types need special handling
  void* ptr;
  std::memcpy(&ptr, data_, sizeof(void*));

  if (type_id_ == TYPE_ID_STRING) {
    const std::string* str = reinterpret_cast<const std::string*>(ptr);
    return total + sizeof(std::string) + str->capacity();
  }

  if (type_id_ == (TYPE_ID_STRING | TYPE_ID_1D_ARRAY_BIT)) {
    const std::vector<std::string>* vec = reinterpret_cast<const std::vector<std::string>*>(ptr);
    size_t string_mem = 0;
    for (const auto& s : *vec) {
      string_mem += sizeof(std::string) + s.capacity();
    }
    return total + sizeof(std::vector<std::string>) + string_mem;
  }

  if (type_id_ == TYPE_ID_TOKEN) {
    const token* tok = reinterpret_cast<const token*>(ptr);
    return total + sizeof(token) + tok->str().capacity();
  }

  if (type_id_ == (TYPE_ID_TOKEN | TYPE_ID_1D_ARRAY_BIT)) {
    const std::vector<token>* vec = reinterpret_cast<const std::vector<token>*>(ptr);
    size_t token_mem = 0;
    for (const auto& t : *vec) {
      token_mem += sizeof(token) + t.str().capacity();
    }
    return total + sizeof(std::vector<token>) + token_mem;
  }

  // Default estimate for unknown types
  return total + 64;
}

#undef DISPATCH_SCALAR_TYPE
#undef DISPATCH_STRING_TYPE

} // namespace value
} // namespace tinyusdz

#endif // TUSDZ_NEW_VALUE_TYPE
