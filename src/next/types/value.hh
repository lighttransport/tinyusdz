// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value class with Small Buffer Optimization
// Replaces linb::any with a type-aware, USD-specific value container

#pragma once

#include "type-id.hh"
#include <cstddef>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

struct LazyArrayRef;  // crate/lazy-array.hh

/// Value class - type-erased container for USD values
/// Uses Small Buffer Optimization to avoid heap allocation for small types
/// All USD scalar and vector types fit in the inline buffer
class Value {
public:
  /// Small buffer size - large enough to hold matrix4d (128 bytes)
  /// plus potential std::string on the stack
  static constexpr size_t kSBOSize = 136;

  // ============================================================
  // Constructors and assignment
  // ============================================================

  /// Default constructor - creates empty value
  Value();

  /// Destructor
  ~Value();

  /// Copy constructor
  Value(const Value& other);

  /// Move constructor
  Value(Value&& other) noexcept;

  /// Copy assignment
  Value& operator=(const Value& other);

  /// Move assignment
  Value& operator=(Value&& other) noexcept;

  // ============================================================
  // Type-specific constructors (no templates in public API)
  // ============================================================

  explicit Value(bool v);
  explicit Value(int32_t v);
  explicit Value(uint32_t v);
  explicit Value(int64_t v);
  explicit Value(uint64_t v);
  explicit Value(float v);
  explicit Value(double v);
  explicit Value(const char* v);
  explicit Value(const std::string& v);
  explicit Value(std::string&& v);

  // ============================================================
  // Factory functions for compound types
  // ============================================================

  static Value MakeInt2(int32_t x, int32_t y);
  static Value MakeInt3(int32_t x, int32_t y, int32_t z);
  static Value MakeInt4(int32_t x, int32_t y, int32_t z, int32_t w);

  static Value MakeFloat2(float x, float y);
  static Value MakeFloat3(float x, float y, float z);
  static Value MakeFloat4(float x, float y, float z, float w);

  static Value MakeDouble2(double x, double y);
  static Value MakeDouble3(double x, double y, double z);
  static Value MakeDouble4(double x, double y, double z, double w);

  static Value MakeQuatf(float x, float y, float z, float w);
  static Value MakeQuatd(double x, double y, double z, double w);

  static Value MakeMatrix2f(const float* data);   // 4 floats
  static Value MakeMatrix3f(const float* data);   // 9 floats
  static Value MakeMatrix4f(const float* data);   // 16 floats
  static Value MakeMatrix2d(const double* data);  // 4 doubles
  static Value MakeMatrix3d(const double* data);  // 9 doubles
  static Value MakeMatrix4d(const double* data);  // 16 doubles

  static Value MakeToken(const std::string& s);
  static Value MakeToken(std::string&& s);
  static Value MakeAssetPath(const std::string& s);
  static Value MakeAssetPath(std::string&& s);

  // Semantic type factories (same storage as vectors)
  static Value MakePoint3f(float x, float y, float z);
  static Value MakePoint3d(double x, double y, double z);
  static Value MakeVector3f(float x, float y, float z);
  static Value MakeVector3d(double x, double y, double z);
  static Value MakeNormal3f(float x, float y, float z);
  static Value MakeNormal3d(double x, double y, double z);
  static Value MakeColor3f(float r, float g, float b);
  static Value MakeColor4f(float r, float g, float b, float a);
  static Value MakeTexcoord2f(float u, float v);

  /// Create from raw type ID and data pointer
  static Value MakeFromRaw(TypeId type_id, const void* data);

  // ============================================================
  // Array constructors
  // ============================================================

  /// Create array value from vector of floats
  static Value MakeFloatArray(const std::vector<float>& data);
  static Value MakeFloatArray(std::vector<float>&& data);

  /// Create array value from vector of ints
  static Value MakeIntArray(const std::vector<int32_t>& data);
  static Value MakeIntArray(std::vector<int32_t>&& data);

  /// Create array of float3 from flat data (length must be multiple of 3)
  static Value MakeFloat3Array(const std::vector<float>& data);
  static Value MakeFloat3Array(std::vector<float>&& data);

  /// New array types
  static Value MakeDoubleArray(const std::vector<double>& data);
  static Value MakeDoubleArray(std::vector<double>&& data);
  static Value MakeInt64Array(const std::vector<int64_t>& data);
  static Value MakeInt64Array(std::vector<int64_t>&& data);
  static Value MakeUIntArray(const std::vector<uint32_t>& data);
  static Value MakeUIntArray(std::vector<uint32_t>&& data);
  static Value MakeUInt64Array(const std::vector<uint64_t>& data);
  static Value MakeUInt64Array(std::vector<uint64_t>&& data);
  static Value MakeBoolArray(const std::vector<bool>& data);
  static Value MakeTokenArray(const std::vector<std::string>& data);
  static Value MakeTokenArray(std::vector<std::string>&& data);

  // ============================================================
  // Type queries
  // ============================================================

  /// Get the type ID
  TypeId type_id() const { return type_id_; }

  /// Check if empty (no value stored)
  bool is_empty() const { return type_id_ == TypeId::Invalid; }

  /// Check if this is an array value
  bool is_array() const { return is_array_; }

  /// Get array size (0 if not an array)
  size_t array_size() const { return is_array_ ? array_size_ : 0; }

  /// Clear the value (becomes empty)
  void clear();

  // ============================================================
  // Lazy array references (crate-backed, undecoded)
  // ============================================================

  /// Construct a lazy array value that references an undecoded block inside a
  /// retained CrateDataSource. The payload is decoded on first access.
  static Value MakeLazyArray(const LazyArrayRef& ref);

  /// True if this is an array whose payload has not been decoded yet.
  bool is_lazy() const { return is_lazy_; }

  /// True if the value was materialized AND potentially mutated (so write-time
  /// byte pass-through is no longer safe).
  bool is_dirty() const { return dirty_; }

  /// Mark the value as edited (forces re-encode on write).
  void mark_dirty() { dirty_ = true; }

  /// Access the lazy reference (nullptr unless is_lazy()).
  const LazyArrayRef* lazy_ref() const;

  /// Decode a lazy array into concrete storage (idempotent; no-op if not lazy).
  void materialize();

  // ============================================================
  // Type-safe accessors (return nullptr if wrong type)
  // ============================================================

  const bool* as_bool() const;
  const int32_t* as_int() const;
  const uint32_t* as_uint() const;
  const int64_t* as_int64() const;
  const uint64_t* as_uint64() const;
  const float* as_float() const;
  const double* as_double() const;
  const std::string* as_string() const;

  // Mutable accessors
  bool* as_bool();
  int32_t* as_int();
  uint32_t* as_uint();
  int64_t* as_int64();
  uint64_t* as_uint64();
  float* as_float();
  double* as_double();
  std::string* as_string();

  // Vector accessors (return pointer to first element)
  const int32_t* as_int2() const;
  const int32_t* as_int3() const;
  const int32_t* as_int4() const;
  const float* as_float2() const;
  const float* as_float3() const;
  const float* as_float4() const;
  const double* as_double2() const;
  const double* as_double3() const;
  const double* as_double4() const;

  // Matrix accessors (return pointer to first element)
  const float* as_matrix2f() const;
  const float* as_matrix3f() const;
  const float* as_matrix4f() const;
  const double* as_matrix2d() const;
  const double* as_matrix3d() const;
  const double* as_matrix4d() const;

  // Token and AssetPath as string
  const std::string* as_token() const;
  const std::string* as_asset_path() const;

  // Array accessors
  const std::vector<float>* as_float_array() const;
  const std::vector<int32_t>* as_int_array() const;
  const std::vector<double>* as_double_array() const;
  const std::vector<int64_t>* as_int64_array() const;
  const std::vector<uint32_t>* as_uint_array() const;
  const std::vector<uint64_t>* as_uint64_array() const;
  const std::vector<uint8_t>* as_bool_array() const;   // 0/1 values
  const std::vector<std::string>* as_token_array() const;
  std::vector<float>* as_float_array();
  std::vector<int32_t>* as_int_array();
  std::vector<double>* as_double_array();
  std::vector<int64_t>* as_int64_array();
  std::vector<uint32_t>* as_uint_array();
  std::vector<uint64_t>* as_uint64_array();

  // ============================================================
  // Raw data access
  // ============================================================

  /// Get raw pointer to data (use with caution)
  const void* raw_data() const;
  void* raw_data();

  // ============================================================
  // Comparison
  // ============================================================

  bool operator==(const Value& other) const;
  bool operator!=(const Value& other) const { return !(*this == other); }

  // ============================================================
  // Hashing (for deduplication)
  // ============================================================

  /// Compute hash of the value (for deduplication)
  /// Returns 0 for empty values
  uint64_t hash() const;

  /// Get raw bytes for comparison (for array deduplication)
  /// Returns nullptr and size=0 if not applicable
  const uint8_t* raw_bytes(size_t* out_size) const;

private:
  TypeId type_id_ = TypeId::Invalid;
  bool is_array_ = false;
  bool is_lazy_ = false;  // array payload not decoded; storage_ holds LazyArrayRef*
  bool dirty_ = false;    // materialized and possibly mutated (no byte pass-through)
  uint32_t array_size_ = 0;

  // Storage - either inline or heap-allocated
  alignas(16) char storage_[kSBOSize];

  // Helper functions
  bool uses_heap() const;
  void copy_from(const Value& other);
  void move_from(Value&& other) noexcept;
  void destroy();

  // Decode a lazy value in place if needed (logical const: lazy load).
  void ensure_materialized() const;

  void* data_ptr();
  const void* data_ptr() const;
};

}  // namespace next
}  // namespace tinyusdz
