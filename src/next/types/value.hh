// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value class with Small Buffer Optimization
// Replaces linb::any with a type-aware, USD-specific value container

#pragma once

#include "type-id.hh"
#include "token.hh"
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tinyusdz {
namespace next {

struct LazyArrayRef;  // crate/lazy-array.hh
struct Dict;          // recursive USD dictionary (defined below)

/// Value class - type-erased container for USD values
/// Uses Small Buffer Optimization to avoid heap allocation for small types
/// All USD scalar and vector types fit in the inline buffer
class Value {
public:
  /// Small buffer size. Shrunk to 16 bytes: enough for a shared_ptr<Dict> (16),
  /// an 8-byte array/string/scalar-box handle, and every scalar/vector up to
  /// vec2d/vec4f/quatf/matrix2f (16). Larger scalars — String/AssetPath (32-byte
  /// std::string), vec3d/vec4d/quatd, and matrix2d/3f/4f/3d/4d — move to a COW
  /// box (see StringBox / ScalarBox in value.cc). With a 16-byte SBO,
  /// sizeof(Value) is 32 (down from 48/144). See doc/next-value-redesign.md.
  static constexpr size_t kSBOSize = 16;

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
  explicit Value(std::string_view v);
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
  static Value MakeToken(const char* s);
  static Value MakeToken(std::string_view s);
  static Value MakeToken(std::string&& s);
  static Value MakeAssetPath(const std::string& s);
  static Value MakeAssetPath(const char* s);
  static Value MakeAssetPath(std::string_view s);
  static Value MakeAssetPath(std::string&& s);

  // Dictionary (recursive key->Value map; insertion-ordered for round-trip).
  // Stored behind a shared_ptr<Dict> in the SBO with copy-on-write, so copying
  // a dict-valued Value bumps a refcount and never deep-copies until mutated.
  static Value MakeDictionary();
  static Value MakeDictionary(Dict&& d);

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

  /// Create array of float2 from flat data (length must be multiple of 2)
  static Value MakeFloat2Array(const std::vector<float>& data);
  static Value MakeFloat2Array(std::vector<float>&& data);

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
  static Value MakeBoolArrayFromBytes(std::vector<uint8_t>&& data);
  static Value MakeTokenArray(const std::vector<std::string>& data);
  static Value MakeTokenArray(std::vector<std::string>&& data);
  // Direct overload for callers that already hold interned tokens (the USDA
  // token-array parser), avoiding an intermediate std::string vector.
  static Value MakeTokenArray(std::vector<TfToken>&& data);

  /// Generic flat-buffer array factories for vector/quat/matrix element types
  /// (e.g. Vec4f, Matrix4d, Quatf). `data` is the flat scalar buffer and
  /// `comps_per_elem` the number of scalars per element (4 for Vec4f / Quatf,
  /// 16 for Matrix4d, ...). array_size == data.size() / comps_per_elem.
  static Value MakeFloatCompArray(std::vector<float>&& data, TypeId elem_type,
                                  uint32_t comps_per_elem);
  static Value MakeDoubleCompArray(std::vector<double>&& data, TypeId elem_type,
                                   uint32_t comps_per_elem);

  /// Storage scalar kind of a numeric array payload (what the flat vector's
  /// element type is — half/float vector types are all Float-backed).
  enum class ArrayScalarKind : uint8_t { Float, Double, Int32, UInt32, Int64, UInt64 };

  /// Handle to a deferred array payload created by MakeDeferredArray. `vec`
  /// points at the storage's flat scalar vector (std::vector<float>* for
  /// ArrayScalarKind::Float, etc.); `keepalive` owns the storage so the vector
  /// stays valid even if the committed Value is destroyed early (error paths).
  struct DeferredArrayFill {
    std::shared_ptr<void> keepalive;
    void* vec = nullptr;
  };

  /// Deferred-fill array factory for the async USDA array parser: returns a
  /// fully-typed array Value whose flat payload vector is still EMPTY. The
  /// caller commits the Value into the layer immediately (type_id / is_array /
  /// array_size are final at creation) and a parser worker later fills the
  /// payload in place through *out_fill. The payload vector's address is
  /// stable (heap storage behind the copy-on-write shared_ptr handle), but the
  /// value must not be hashed, compared, printed, payload-accessed, or
  /// COW-detached until the fill completes (the parse's drain barrier).
  static Value MakeDeferredArray(TypeId elem_type, ArrayScalarKind kind,
                                 uint32_t elem_count,
                                 DeferredArrayFill* out_fill);

  // ============================================================
  // Type queries
  // ============================================================

  /// Get the type ID
  TypeId type_id() const { return type_id_; }

  /// Check if empty (no value stored). A value BLOCK (`= None`) is NOT empty:
  /// it is an authored opinion that blocks weaker values and must round-trip.
  bool is_empty() const { return type_id_ == TypeId::Invalid && !is_block_; }

  /// True if this is an authored value block (`= None`) — a typed opinion that
  /// carries no data but suppresses weaker opinions and emits `= None`.
  bool is_block() const { return is_block_; }

  /// Check if this is an array value
  bool is_array() const { return is_array_; }

  /// Check if this is a dictionary value
  bool is_dictionary() const { return type_id_ == TypeId::Dictionary; }

  /// Get array size (0 if not an array)
  size_t array_size() const { return is_array_ ? array_size_ : 0; }

  /// Clear the value (becomes empty)
  void clear();

  // ============================================================
  // Lazy array references (crate-backed, undecoded)
  // ============================================================

  /// Construct an authored value block (`= None`): no data, no type, but a real
  /// opinion (is_empty()==false) that blocks weaker values and re-emits `= None`.
  /// The attribute's declared type comes from the slot / property_type_name.
  static Value MakeBlock();

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

  /// Decode a lazy array into a fresh temporary Value WITHOUT mutating *this
  /// (the source Value stays lazy). For non-lazy values, returns an ordinary
  /// copy-on-write copy (preserves dirty_). On decode failure, returns an empty
  /// Value (prints as "None", matching in-place materialize() failure). Used by
  /// the writer to format a lazy array once and free it immediately, so peak
  /// resident decoded-array memory is bounded to ~one array.
  Value materialized_copy() const;

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

  // Dictionary access (nullptr if not a dictionary). The mutable accessor
  // copy-on-write-detaches before returning a writable view.
  const Dict* as_dictionary() const;
  Dict* as_dictionary();

  // Array accessors
  const std::vector<float>* as_float_array() const;
  const std::vector<int32_t>* as_int_array() const;
  const std::vector<double>* as_double_array() const;
  const std::vector<int64_t>* as_int64_array() const;
  const std::vector<uint32_t>* as_uint_array() const;
  const std::vector<uint64_t>* as_uint64_array() const;
  const std::vector<uint8_t>* as_bool_array() const;   // 0/1 values
  // Token arrays are interned: elements are TfToken (call .str() for the text).
  // MakeTokenArray still accepts std::vector<std::string> and interns.
  const std::vector<TfToken>* as_token_array() const;
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
  // Flags packed into one byte. C++17 forbids default-member-initializers on
  // bitfields, so EVERY constructor initializes them: Value() does directly, the
  // typed ctors delegate to Value(), and copy/move set them via copy_from/
  // move_from. Access syntax (`is_array_`, `is_array_ = true`) is unchanged.
  uint8_t is_array_ : 1;
  uint8_t is_lazy_ : 1;   // array payload not decoded; storage_ holds LazyArrayRef*
  uint8_t dirty_ : 1;     // materialized and possibly mutated (no byte pass-through)
  uint8_t is_block_ : 1;  // authored `= None` block: no data, but not is_empty()
  uint32_t array_size_ = 0;

  // Inline storage. alignas(8): the largest inline type is now double-based
  // (double / vec2d, 8-byte aligned); oversized doubles/matrices are boxed and
  // 16-aligned inside their ScalarBox. Packing the header to 8 bytes + this
  // 16-byte SBO gives sizeof(Value) == 24.
  alignas(8) char storage_[kSBOSize];

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

/// Recursive USD dictionary. Entries are kept in insertion order so the writer
/// re-emits them exactly as authored. A value may itself be a Dictionary.
struct Dict {
  std::vector<std::pair<std::string, Value>> entries;

  const Value* find(const std::string& key) const {
    for (const auto& kv : entries) {
      if (kv.first == key) return &kv.second;
    }
    return nullptr;
  }
  Value* find(const std::string& key) {
    for (auto& kv : entries) {
      if (kv.first == key) return &kv.second;
    }
    return nullptr;
  }
  /// Replace the value for an existing key, or append a new entry.
  void set(std::string key, Value v) {
    if (Value* existing = find(key)) {
      *existing = std::move(v);
    } else {
      entries.emplace_back(std::move(key), std::move(v));
    }
  }
  size_t size() const { return entries.size(); }
  bool empty() const { return entries.empty(); }
};

/// Linearly interpolate between two values for time-sample evaluation.
/// `t` is the fraction in [0,1] from `a` to `b`. Interpolatable numeric scalar,
/// vector, and matrix types are component-wise lerped; non-interpolatable types
/// (int/bool/string/token/asset/quaternion) and type mismatches fall back to
/// held interpolation (returns `a`).
Value LerpValue(const Value& a, const Value& b, double t);

}  // namespace next
}  // namespace tinyusdz
