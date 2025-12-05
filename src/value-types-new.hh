// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// New optimized Value implementation
// Enabled with TUSDZ_NEW_VALUE_TYPE preprocessor flag
//
// NOTE: This file is included from value-types.hh after all type definitions
// are complete. Do not include value-types.hh from here (circular dependency).
//
#pragma once

#ifdef TUSDZ_NEW_VALUE_TYPE

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <new>

// NOTE: Do NOT open/close namespaces here - value-types.hh already has them open
// This file is included from within namespace tinyusdz { namespace value {

//
// NewValue: Memory-optimized 32-byte Value implementation
//
// Layout (32 bytes total):
// - 24 bytes: data_ (stores pointer or inlined value if sizeof(T) <= 24)
// - 4 bytes: type_id_
// - 1 byte: flags_ (array bit + reserved bits)
// - 3 bytes: padding_ (reserved for future use)
//
// Can inline up to double3 (24 bytes), float4/matrix2f (16 bytes), etc.
// Inspired by crate::ValueRep but designed for runtime type-safe access
//
class NewValue {
 public:
  static constexpr size_t kInlineDataSize = 24;

  // Bit flags in flags_ byte
  static constexpr uint8_t kArrayBitFlag = 0x01;       // Bit 0: is array
  static constexpr uint8_t kHeapAllocatedFlag = 0x02;  // Bit 1: heap allocated

  // Array class stored in bits 2-3 (when kArrayBitFlag is set)
  static constexpr uint8_t kArrayClassMask = 0x0C;     // Bits 2-3: array class
  static constexpr uint8_t kArrayClassShift = 2;

  enum class ArrayClass : uint8_t {
    StdVector = 0,      // std::vector<T>
    TypedArray = 1,     // TypedArray<T>
    Reserved = 2,       // Reserved for future use
    Invalid = 3         // Invalid/unset
  };

  // Default constructor - creates invalid/null value
  NewValue() noexcept : type_id_(TYPE_ID_NULL), flags_(0) {
    std::memset(data_, 0, sizeof(data_));
    std::memset(padding_, 0, sizeof(padding_));
  }

  // Destructor - frees heap-allocated data
  ~NewValue() {
    destroy();
  }

  // Copy constructor
  NewValue(const NewValue& rhs) : type_id_(rhs.type_id_), flags_(rhs.flags_) {
    std::memcpy(padding_, rhs.padding_, sizeof(padding_));
    copy_data_from(rhs);
  }

  // Move constructor
  NewValue(NewValue&& rhs) noexcept : type_id_(rhs.type_id_), flags_(rhs.flags_) {
    std::memcpy(padding_, rhs.padding_, sizeof(padding_));
    std::memcpy(data_, rhs.data_, sizeof(data_));

    // Clear rhs so it doesn't free the data
    std::memset(rhs.data_, 0, sizeof(rhs.data_));
    rhs.type_id_ = TYPE_ID_NULL;
    rhs.flags_ = 0;
  }

  // Copy assignment
  NewValue& operator=(const NewValue& rhs) {
    if (this != &rhs) {
      destroy();
      type_id_ = rhs.type_id_;
      flags_ = rhs.flags_;
      std::memcpy(padding_, rhs.padding_, sizeof(padding_));
      copy_data_from(rhs);
    }
    return *this;
  }

  // Move assignment
  NewValue& operator=(NewValue&& rhs) noexcept {
    if (this != &rhs) {
      destroy();
      type_id_ = rhs.type_id_;
      flags_ = rhs.flags_;
      std::memcpy(padding_, rhs.padding_, sizeof(padding_));
      std::memcpy(data_, rhs.data_, sizeof(data_));

      std::memset(rhs.data_, 0, sizeof(rhs.data_));
      rhs.type_id_ = TYPE_ID_NULL;
      rhs.flags_ = 0;
    }
    return *this;
  }

  //
  // Constructor from concrete type T (copy)
  // SFINAE: Disabled when T is NewValue to avoid shadowing copy constructor
  //
  template <typename T,
            typename std::enable_if<
              !std::is_same<typename std::decay<T>::type, NewValue>::value,
              int>::type = 0>
  NewValue(const T& value) : type_id_(TypeTraits<T>::type_id()), flags_(0) {
    std::memset(padding_, 0, sizeof(padding_));

    // Determine if this is an array type
    if (TypeTraits<T>::is_array()) {
      flags_ |= kArrayBitFlag;
    }

    construct_value(value);
  }

  // Constructor from concrete type T (move)
  // SFINAE: Disabled when T is NewValue to avoid shadowing move constructor
  //
  template <typename T,
            typename std::enable_if<
              !std::is_same<typename std::decay<T>::type, NewValue>::value,
              int>::type = 0>
  NewValue(T&& value) : type_id_(TypeTraits<typename std::remove_reference<T>::type>::type_id()), flags_(0) {
    using DecayedType = typename std::remove_reference<T>::type;
    std::memset(padding_, 0, sizeof(padding_));

    // Determine if this is an array type
    if (TypeTraits<DecayedType>::is_array()) {
      flags_ |= kArrayBitFlag;
    }

    construct_value(std::forward<T>(value));
  }

  //
  // Type queries
  //
  uint32_t type_id() const noexcept { return type_id_; }

  uint32_t underlying_type_id() const noexcept {
    if (is_array()) {
      return type_id_ & (~TYPE_ID_1D_ARRAY_BIT);
    }
    // TODO: Handle role types - map to underlying type
    return type_id_;
  }

  const std::string type_name() const {
    return GetTypeName(type_id_);
  }

  const std::string underlying_type_name() const {
    return GetUnderlyingTypeName(type_id_);
  }

  bool is_array() const noexcept {
    return (flags_ & kArrayBitFlag) != 0;
  }

  bool is_empty() const noexcept {
    return type_id_ == TYPE_ID_NULL;
  }

  bool is_none() const noexcept {
    return type_id_ == TYPE_ID_VALUEBLOCK;
  }

  // Get array class (only valid when is_array() == true)
  ArrayClass get_array_class() const noexcept {
    if (!is_array()) {
      return ArrayClass::Invalid;
    }
    uint8_t class_bits = (flags_ & kArrayClassMask) >> kArrayClassShift;
    return static_cast<ArrayClass>(class_bits);
  }

  //
  // Type-safe access with strict_cast option
  // IMPORTANT: Prevents unsafe casts between std::vector and TypedArray
  //
  template <typename T>
  const T* as(bool strict_cast = false) const {
    uint32_t target_type_id = TypeTraits<T>::type_id();

    if (strict_cast) {
      // Exact type match required
      if (type_id_ != target_type_id) {
        return nullptr;
      }
    } else {
      // Allow role type conversions
      if (!is_compatible_type<T>()) {
        return nullptr;
      }
    }

    // Check array class compatibility
    if (!check_array_class_compatible<T>()) {
      return nullptr;
    }

    // Access the value
    return get_value_ptr<T>();
  }

  template <typename T>
  T* as(bool strict_cast = false) {
    return const_cast<T*>(const_cast<const NewValue*>(this)->as<T>(strict_cast));
  }

  //
  // Get value as optional (type-safe)
  //
  template <typename T>
  nonstd::optional<T> get_value() const {
    const T* ptr = as<T>(/*strict_cast=*/false);
    if (ptr) {
      return *ptr;
    }
    return nonstd::nullopt;
  }

  //
  // Array size (for array types only)
  //
  size_t array_size() const;

  //
  // Memory usage estimation
  //
  size_t estimate_memory_usage() const;

  //
  // Get TypedArrayView to the underlying array data
  // Supports both std::vector and TypedArray
  //
  // Returns a view over array data if the value contains an array type that's compatible
  // with the requested element type T. For non-array types, returns an empty view.
  //
  // The view provides zero-copy access to the underlying data with type safety validation.
  //
  template <typename T>
  TypedArrayView<const T> as_view(bool strict_cast = false) const {
    // Check if this is an array type
    if (!is_array()) {
      return TypedArrayView<const T>();
    }

    // Check type compatibility
    uint32_t underlying_type_id = this->underlying_type_id();
    uint32_t target_type_id = TypeTraits<T>::underlying_type_id();

    if (strict_cast) {
      if (underlying_type_id != target_type_id) {
        return TypedArrayView<const T>();
      }
    } else {
      // Allow compatible types (same underlying type)
      if (underlying_type_id != target_type_id) {
        return TypedArrayView<const T>();
      }
    }

    // Get array class and create appropriate view
    ArrayClass aclass = get_array_class();

    if (aclass == ArrayClass::StdVector) {
      // Try to get as std::vector
      const std::vector<T>* vec = as<std::vector<T>>(strict_cast);
      if (vec) {
        return TypedArrayView<const T>(vec->data(), vec->size());
      }
    } else if (aclass == ArrayClass::TypedArray) {
      // Try to get as TypedArray
      const TypedArray<T>* arr = as<TypedArray<T>>(strict_cast);
      if (arr) {
        return TypedArrayView<const T>(arr->data(), arr->size());
      }
    }

    return TypedArrayView<const T>();
  }

  //
  // Non-const version of as_view() for mutable access
  //
  template <typename T>
  TypedArrayView<T> as_view(bool strict_cast = false) {
    // Check if this is an array type
    if (!is_array()) {
      return TypedArrayView<T>();
    }

    // Check type compatibility
    uint32_t underlying_type_id = this->underlying_type_id();
    uint32_t target_type_id = TypeTraits<T>::underlying_type_id();

    if (strict_cast) {
      if (underlying_type_id != target_type_id) {
        return TypedArrayView<T>();
      }
    } else {
      // Allow compatible types (same underlying type)
      if (underlying_type_id != target_type_id) {
        return TypedArrayView<T>();
      }
    }

    // Get array class and create appropriate view
    ArrayClass aclass = get_array_class();

    if (aclass == ArrayClass::StdVector) {
      // Try to get as std::vector
      std::vector<T>* vec = as<std::vector<T>>(strict_cast);
      if (vec) {
        return TypedArrayView<T>(vec->data(), vec->size());
      }
    } else if (aclass == ArrayClass::TypedArray) {
      // Try to get as TypedArray
      TypedArray<T>* arr = as<TypedArray<T>>(strict_cast);
      if (arr) {
        return TypedArrayView<T>(arr->data(), arr->size());
      }
    }

    return TypedArrayView<T>();
  }

 private:
  uint8_t data_[24];    // 24 bytes: pointer or inlined data
  uint32_t type_id_;    // 4 bytes: TYPE_ID_*
  uint8_t flags_;       // 1 byte: array bit + reserved bits
  uint8_t padding_[3];  // 3 bytes: reserved for future use

  //
  // Check if type T is compatible with stored type (considering role types)
  //
  template <typename T>
  bool is_compatible_type() const {
    uint32_t target_type_id = TypeTraits<T>::type_id();

    if (type_id_ == target_type_id) {
      return true;
    }

    // Handle array types
    if (TypeTraits<T>::is_array() && is_array()) {
      uint32_t target_underlying = TypeTraits<T>::underlying_type_id() & (~TYPE_ID_1D_ARRAY_BIT);
      uint32_t stored_underlying = underlying_type_id();
      return target_underlying == stored_underlying;
    }

    // Handle scalar role types
    if (!TypeTraits<T>::is_array() && !is_array()) {
      return TypeTraits<T>::underlying_type_id() == underlying_type_id();
    }

    return false;
  }

  //
  // Check if array class is compatible with type T
  // Prevents unsafe casts between std::vector and TypedArray
  //
  template <typename T>
  bool check_array_class_compatible() const {
    // If not an array, no need to check
    if (!is_array()) {
      return true;
    }

    ArrayClass stored_class = get_array_class();

    // Determine requested array class from type T
    if (is_std_vector<T>::value) {
      return stored_class == ArrayClass::StdVector;
    } else if (is_typed_array<T>::value) {
      return stored_class == ArrayClass::TypedArray;
    }

    // Non-array type requested for array value
    return false;
  }

  //
  // Type traits to detect std::vector and TypedArray
  //
  template <typename T>
  struct is_std_vector : std::false_type {};

  template <typename T>
  struct is_std_vector<std::vector<T>> : std::true_type {};

  template <typename T>
  struct is_typed_array : std::false_type {};

  template <typename T>
  struct is_typed_array<TypedArray<T>> : std::true_type {};

  template <typename T>
  struct is_chunked_typed_array : std::false_type {};

  template <typename T>
  struct is_chunked_typed_array<ChunkedTypedArray<T>> : std::true_type {};

  //
  // Get pointer to stored value
  //
  template <typename T>
  const T* get_value_ptr() const {
    // Check actual storage location using the heap-allocated flag
    // This MUST match what was done in construct_value()
    if (flags_ & kHeapAllocatedFlag) {
      // Value is heap-allocated, data_ stores pointer to heap
      void* ptr;
      std::memcpy(&ptr, data_, sizeof(void*));
      return reinterpret_cast<const T*>(ptr);
    } else {
      // Value is inlined in data_
      return reinterpret_cast<const T*>(data_);
    }
  }

  //
  // Check if type can be inlined
  //
  template <typename T>
  static constexpr bool is_trivially_copyable() {
    return std::is_trivially_copyable<T>::value;
  }

  //
  // Construct value (inline or heap-allocate)
  //
  template <typename T>
  void construct_value(const T& value) {
    // CRITICAL: Clear data_ first to prevent garbage from being interpreted as heap pointer
    std::memset(data_, 0, sizeof(data_));

    if (sizeof(T) <= kInlineDataSize && is_trivially_copyable<T>()) {
      // Inline storage - heap flag should NOT be set
      std::memcpy(data_, &value, sizeof(T));
      // Ensure heap flag is clear
      flags_ &= ~kHeapAllocatedFlag;
    } else {
      // Heap allocate
      flags_ |= kHeapAllocatedFlag;

      // Set array class if this is an array type
      set_array_class_from_type<T>();

      T* ptr = new T(value);
      void* vptr = reinterpret_cast<void*>(ptr);
      std::memcpy(data_, &vptr, sizeof(void*));
    }
  }

  template <typename T>
  void construct_value(T&& value) {
    using DecayedType = typename std::remove_reference<T>::type;

    // CRITICAL: Clear data_ first to prevent garbage from being interpreted as heap pointer
    std::memset(data_, 0, sizeof(data_));

    if (sizeof(DecayedType) <= kInlineDataSize && is_trivially_copyable<DecayedType>()) {
      // Inline storage - heap flag should NOT be set
      std::memcpy(data_, &value, sizeof(DecayedType));
      // Ensure heap flag is clear
      flags_ &= ~kHeapAllocatedFlag;
    } else {
      // Heap allocate
      flags_ |= kHeapAllocatedFlag;

      // Set array class if this is an array type
      set_array_class_from_type<DecayedType>();

      DecayedType* ptr = new DecayedType(std::forward<T>(value));
      void* vptr = reinterpret_cast<void*>(ptr);
      std::memcpy(data_, &vptr, sizeof(void*));
    }
  }

  //
  // Set array class bits based on type T
  //
  template <typename T>
  void set_array_class_from_type() {
    if (!TypeTraits<T>::is_array()) {
      return; // Not an array, nothing to set
    }

    ArrayClass aclass = ArrayClass::Invalid;

    if (is_std_vector<T>::value) {
      aclass = ArrayClass::StdVector;
    } else if (is_typed_array<T>::value) {
      aclass = ArrayClass::TypedArray;
    } else if (is_chunked_typed_array<T>::value) {
      // Treat ChunkedTypedArray same as TypedArray for now
      aclass = ArrayClass::TypedArray;
    }

    // Clear existing array class bits and set new ones
    flags_ &= ~kArrayClassMask;
    flags_ |= (static_cast<uint8_t>(aclass) << kArrayClassShift);
  }

  //
  // Copy data from another NewValue
  //
  void copy_data_from(const NewValue& rhs);

  //
  // Destroy heap-allocated data
  //
  void destroy();
};

static_assert(sizeof(NewValue) == 32, "NewValue must be exactly 32 bytes");

// DO NOT close namespaces here - the including file (value-types.hh) manages namespaces
// } // namespace value
// } // namespace tinyusdz

#endif // TUSDZ_NEW_VALUE_TYPE
