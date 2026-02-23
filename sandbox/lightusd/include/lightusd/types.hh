// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Core type definitions
// No templates, no RTTI, no exceptions

#pragma once

#include <cstdint>
#include <cstddef>

namespace lightusd {
namespace v1 {

/// Type identifier for all supported USD value types.
/// Uses runtime dispatch instead of compile-time templates.
enum class TypeId : uint32_t {
    Invalid = 0,
    Null,           // No value
    ValueBlock,     // USD's "None" / blocked value

    // Scalar types
    Bool,
    Int32,
    Int64,
    UInt32,
    UInt64,
    Half,           // 16-bit float
    Float,
    Double,

    // Integer vectors
    Int2,
    Int3,
    Int4,

    // Half vectors
    Half2,
    Half3,
    Half4,

    // Float vectors
    Float2,
    Float3,
    Float4,

    // Double vectors
    Double2,
    Double3,
    Double4,

    // Matrices (row-major)
    Matrix2f,
    Matrix3f,
    Matrix4f,
    Matrix2d,
    Matrix3d,
    Matrix4d,

    // Quaternions
    Quath,
    Quatf,
    Quatd,

    // USD special types
    String,
    Token,
    AssetPath,
    Path,
    TimeCode,

    // Role types (semantic aliases for vector types)
    Color3h,
    Color3f,
    Color3d,
    Color4h,
    Color4f,
    Color4d,
    Point3h,
    Point3f,
    Point3d,
    Vector3h,
    Vector3f,
    Vector3d,
    Normal3h,
    Normal3f,
    Normal3d,
    TexCoord2h,
    TexCoord2f,
    TexCoord2d,
    TexCoord3h,
    TexCoord3f,
    TexCoord3d,

    // Dictionary (for metadata)
    Dictionary,

    // Sentinel for counting
    _TypeCount,

    // Array bit flag - OR with base type to indicate array
    ArrayBit = 0x80000000,
};

/// Combine TypeId with ArrayBit
inline constexpr TypeId make_array_type(TypeId base) {
    return static_cast<TypeId>(static_cast<uint32_t>(base) | static_cast<uint32_t>(TypeId::ArrayBit));
}

/// Check if TypeId represents an array type
inline constexpr bool is_array_type(TypeId id) {
    return (static_cast<uint32_t>(id) & static_cast<uint32_t>(TypeId::ArrayBit)) != 0;
}

/// Get base TypeId from array TypeId
inline constexpr TypeId get_base_type(TypeId id) {
    return static_cast<TypeId>(static_cast<uint32_t>(id) & ~static_cast<uint32_t>(TypeId::ArrayBit));
}

/// Type descriptor - runtime type information
struct TypeDescriptor {
    TypeId id;
    const char* name;               // e.g., "float3", "color3f"
    const char* underlying_name;    // For role types, base type name (e.g., "float3" for "color3f")
    TypeId underlying_id;           // For role types, base TypeId
    uint16_t size;                  // sizeof the type in bytes
    uint8_t num_components;         // 1 for scalars, 2-4 for vectors, 4-16 for matrices
    bool is_role_type;              // true for semantic aliases (color, point, vector, etc.)
    bool is_numeric;                // true for types that can be interpolated
};

/// Get type descriptor by TypeId
/// Returns nullptr for invalid/unknown types
const TypeDescriptor* get_type_descriptor(TypeId id);

/// Get type descriptor by type name
/// Returns nullptr for invalid/unknown types
const TypeDescriptor* get_type_descriptor(const char* name);

/// Get type name from TypeId
/// Returns "invalid" for unknown types
const char* get_type_name(TypeId id);

/// Get TypeId from type name
/// Returns TypeId::Invalid for unknown names
TypeId get_type_id(const char* name);

} // namespace v1
} // namespace lightusd
