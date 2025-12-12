// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Value class (type-erased value storage)
// NO template constructors - use factory methods for fast compile time

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "lightusd/types.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Token;
class Path;

/// Value - type-erased storage for USD values.
/// Fixed 32-byte size for cache efficiency.
/// Uses factory methods instead of template constructors for fast compilation.
class Value {
public:
    /// Inline storage size (for small types)
    static constexpr size_t kInlineSize = 24;

    // ========== Constructors/Destructor ==========

    /// Default constructor - creates null value
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

    // ========== Factory Methods (NO TEMPLATES) ==========

    /// Create null value
    static Value make_null();

    /// Create ValueBlock (USD's "None")
    static Value make_none();

    /// Create from bool
    static Value from_bool(bool v);

    /// Create from integers
    static Value from_int32(int32_t v);
    static Value from_int64(int64_t v);
    static Value from_uint32(uint32_t v);
    static Value from_uint64(uint64_t v);

    /// Create from floating point
    static Value from_half(uint16_t v);  // Raw half bits
    static Value from_float(float v);
    static Value from_double(double v);

    /// Create from integer vectors
    static Value from_int2(int32_t x, int32_t y);
    static Value from_int3(int32_t x, int32_t y, int32_t z);
    static Value from_int4(int32_t x, int32_t y, int32_t z, int32_t w);

    /// Create from float vectors
    static Value from_float2(float x, float y);
    static Value from_float3(float x, float y, float z);
    static Value from_float4(float x, float y, float z, float w);

    /// Create from double vectors
    static Value from_double2(double x, double y);
    static Value from_double3(double x, double y, double z);
    static Value from_double4(double x, double y, double z, double w);

    /// Create from matrices (row-major, pass pointer to elements)
    static Value from_matrix2f(const float* m);   // 4 floats
    static Value from_matrix3f(const float* m);   // 9 floats
    static Value from_matrix4f(const float* m);   // 16 floats
    static Value from_matrix2d(const double* m);  // 4 doubles
    static Value from_matrix3d(const double* m);  // 9 doubles
    static Value from_matrix4d(const double* m);  // 16 doubles

    /// Create from quaternions (x, y, z, w order)
    static Value from_quatf(float x, float y, float z, float w);
    static Value from_quatd(double x, double y, double z, double w);

    /// Create from string types
    static Value from_string(const char* s);
    static Value from_string(const std::string& s);
    static Value from_token(const Token& t);
    static Value from_asset_path(const char* s);
    static Value from_asset_path(const std::string& s);
    static Value from_path(const Path& p);

    /// Create from timecode
    static Value from_timecode(double t);

    /// Create role-typed values (same storage as underlying, different TypeId)
    static Value from_color3f(float r, float g, float b);
    static Value from_color4f(float r, float g, float b, float a);
    static Value from_point3f(float x, float y, float z);
    static Value from_vector3f(float x, float y, float z);
    static Value from_normal3f(float x, float y, float z);
    static Value from_texcoord2f(float u, float v);

    // ========== Array Factory Methods ==========

    /// Create array from raw data (copies data)
    static Value from_int32_array(const int32_t* data, size_t count);
    static Value from_float_array(const float* data, size_t count);
    static Value from_double_array(const double* data, size_t count);

    /// Create array of vectors (data contains count*N elements)
    static Value from_float2_array(const float* data, size_t count);
    static Value from_float3_array(const float* data, size_t count);
    static Value from_float4_array(const float* data, size_t count);

    /// Create array from vector (copies data)
    static Value from_int32_array(const std::vector<int32_t>& arr);
    static Value from_float_array(const std::vector<float>& arr);
    static Value from_string_array(const std::vector<std::string>& arr);

    // ========== Type Queries ==========

    /// Get type identifier
    TypeId type_id() const;

    /// Get type name
    const char* type_name() const;

    /// Check if null
    bool is_null() const;

    /// Check if ValueBlock (None)
    bool is_none() const;

    /// Check if array type
    bool is_array() const;

    /// Get array size (0 for non-arrays)
    size_t array_size() const;

    /// Check if numeric type (can be interpolated)
    bool is_numeric() const;

    // ========== Type-Specific Accessors ==========
    // Return nullptr on type mismatch

    const bool* as_bool() const;
    const int32_t* as_int32() const;
    const int64_t* as_int64() const;
    const uint32_t* as_uint32() const;
    const uint64_t* as_uint64() const;
    const uint16_t* as_half() const;
    const float* as_float() const;
    const double* as_double() const;

    // Vector accessors (return pointer to first component)
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

    // Quaternion accessors
    const float* as_quatf() const;
    const double* as_quatd() const;

    // String type accessors
    const std::string* as_string() const;
    const Token* as_token() const;
    const std::string* as_asset_path() const;
    const Path* as_path() const;

    // Timecode accessor
    const double* as_timecode() const;

    // ========== Array Accessors ==========

    /// Array view - pointer and count
    struct ArrayView {
        const void* data;
        size_t count;

        ArrayView() : data(nullptr), count(0) {}
        ArrayView(const void* d, size_t c) : data(d), count(c) {}

        bool empty() const { return count == 0; }
    };

    /// Get array view for specific types
    ArrayView as_int32_array() const;
    ArrayView as_float_array() const;
    ArrayView as_double_array() const;
    ArrayView as_float2_array() const;
    ArrayView as_float3_array() const;
    ArrayView as_float4_array() const;

    // ========== Raw Access ==========

    /// Get raw data pointer (use with type_id() check)
    const void* raw_data() const;
    void* raw_data();

    // ========== Comparison ==========

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const;

    // ========== Utility ==========

    /// Swap contents
    void swap(Value& other) noexcept;

    /// Clear to null state
    void clear();

private:
    // Internal flags
    static constexpr uint32_t kHeapFlag = 0x80000000;
    static constexpr uint32_t kArraySizeMask = 0x00FFFFFF;

    // Storage union - inline for small types, heap pointer for large
    union Storage {
        alignas(8) uint8_t inline_[kInlineSize];
        void* heap_;
    } storage_;

    TypeId type_id_;
    uint32_t flags_;  // Heap flag | array size (for inline arrays)

    // Internal helpers
    bool is_heap() const { return (flags_ & kHeapFlag) != 0; }
    void set_heap(bool heap) {
        if (heap) flags_ |= kHeapFlag;
        else flags_ &= ~kHeapFlag;
    }

    void destroy();
    void copy_from(const Value& other);
    void move_from(Value&& other) noexcept;
};

static_assert(sizeof(Value) == 32, "Value must be exactly 32 bytes");

/// Swap specialization
inline void swap(Value& a, Value& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
