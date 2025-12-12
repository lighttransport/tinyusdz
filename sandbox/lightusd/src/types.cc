// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Type registry implementation

#include "lightusd/types.hh"
#include <cstring>
#include <array>

namespace lightusd {
namespace v1 {

namespace {

// Static type descriptors - no dynamic allocation
// Order must match TypeId enum
constexpr TypeDescriptor kTypeDescriptors[] = {
    // Invalid
    {TypeId::Invalid, "invalid", nullptr, TypeId::Invalid, 0, 0, false, false},
    // Null
    {TypeId::Null, "null", nullptr, TypeId::Null, 0, 0, false, false},
    // ValueBlock
    {TypeId::ValueBlock, "ValueBlock", nullptr, TypeId::ValueBlock, 0, 0, false, false},

    // Scalars
    {TypeId::Bool, "bool", nullptr, TypeId::Bool, 1, 1, false, false},
    {TypeId::Int32, "int", nullptr, TypeId::Int32, 4, 1, false, true},
    {TypeId::Int64, "int64", nullptr, TypeId::Int64, 8, 1, false, true},
    {TypeId::UInt32, "uint", nullptr, TypeId::UInt32, 4, 1, false, true},
    {TypeId::UInt64, "uint64", nullptr, TypeId::UInt64, 8, 1, false, true},
    {TypeId::Half, "half", nullptr, TypeId::Half, 2, 1, false, true},
    {TypeId::Float, "float", nullptr, TypeId::Float, 4, 1, false, true},
    {TypeId::Double, "double", nullptr, TypeId::Double, 8, 1, false, true},

    // Integer vectors
    {TypeId::Int2, "int2", nullptr, TypeId::Int2, 8, 2, false, true},
    {TypeId::Int3, "int3", nullptr, TypeId::Int3, 12, 3, false, true},
    {TypeId::Int4, "int4", nullptr, TypeId::Int4, 16, 4, false, true},

    // Half vectors
    {TypeId::Half2, "half2", nullptr, TypeId::Half2, 4, 2, false, true},
    {TypeId::Half3, "half3", nullptr, TypeId::Half3, 6, 3, false, true},
    {TypeId::Half4, "half4", nullptr, TypeId::Half4, 8, 4, false, true},

    // Float vectors
    {TypeId::Float2, "float2", nullptr, TypeId::Float2, 8, 2, false, true},
    {TypeId::Float3, "float3", nullptr, TypeId::Float3, 12, 3, false, true},
    {TypeId::Float4, "float4", nullptr, TypeId::Float4, 16, 4, false, true},

    // Double vectors
    {TypeId::Double2, "double2", nullptr, TypeId::Double2, 16, 2, false, true},
    {TypeId::Double3, "double3", nullptr, TypeId::Double3, 24, 3, false, true},
    {TypeId::Double4, "double4", nullptr, TypeId::Double4, 32, 4, false, true},

    // Matrices
    {TypeId::Matrix2f, "matrix2f", nullptr, TypeId::Matrix2f, 16, 4, false, true},
    {TypeId::Matrix3f, "matrix3f", nullptr, TypeId::Matrix3f, 36, 9, false, true},
    {TypeId::Matrix4f, "matrix4f", nullptr, TypeId::Matrix4f, 64, 16, false, true},
    {TypeId::Matrix2d, "matrix2d", nullptr, TypeId::Matrix2d, 32, 4, false, true},
    {TypeId::Matrix3d, "matrix3d", nullptr, TypeId::Matrix3d, 72, 9, false, true},
    {TypeId::Matrix4d, "matrix4d", nullptr, TypeId::Matrix4d, 128, 16, false, true},

    // Quaternions
    {TypeId::Quath, "quath", nullptr, TypeId::Quath, 8, 4, false, true},
    {TypeId::Quatf, "quatf", nullptr, TypeId::Quatf, 16, 4, false, true},
    {TypeId::Quatd, "quatd", nullptr, TypeId::Quatd, 32, 4, false, true},

    // USD special types
    {TypeId::String, "string", nullptr, TypeId::String, sizeof(void*), 1, false, false},
    {TypeId::Token, "token", nullptr, TypeId::Token, sizeof(void*), 1, false, false},
    {TypeId::AssetPath, "asset", nullptr, TypeId::AssetPath, sizeof(void*), 1, false, false},
    {TypeId::Path, "path", nullptr, TypeId::Path, sizeof(void*), 1, false, false},
    {TypeId::TimeCode, "timecode", nullptr, TypeId::TimeCode, 8, 1, false, true},

    // Role types - ordered to match enum (Color3hfd, Color4hfd, Point3hfd, etc.)
    {TypeId::Color3h, "color3h", "half3", TypeId::Half3, 6, 3, true, true},
    {TypeId::Color3f, "color3f", "float3", TypeId::Float3, 12, 3, true, true},
    {TypeId::Color3d, "color3d", "double3", TypeId::Double3, 24, 3, true, true},
    {TypeId::Color4h, "color4h", "half4", TypeId::Half4, 8, 4, true, true},
    {TypeId::Color4f, "color4f", "float4", TypeId::Float4, 16, 4, true, true},
    {TypeId::Color4d, "color4d", "double4", TypeId::Double4, 32, 4, true, true},
    {TypeId::Point3h, "point3h", "half3", TypeId::Half3, 6, 3, true, true},
    {TypeId::Point3f, "point3f", "float3", TypeId::Float3, 12, 3, true, true},
    {TypeId::Point3d, "point3d", "double3", TypeId::Double3, 24, 3, true, true},
    {TypeId::Vector3h, "vector3h", "half3", TypeId::Half3, 6, 3, true, true},
    {TypeId::Vector3f, "vector3f", "float3", TypeId::Float3, 12, 3, true, true},
    {TypeId::Vector3d, "vector3d", "double3", TypeId::Double3, 24, 3, true, true},
    {TypeId::Normal3h, "normal3h", "half3", TypeId::Half3, 6, 3, true, true},
    {TypeId::Normal3f, "normal3f", "float3", TypeId::Float3, 12, 3, true, true},
    {TypeId::Normal3d, "normal3d", "double3", TypeId::Double3, 24, 3, true, true},
    {TypeId::TexCoord2h, "texCoord2h", "half2", TypeId::Half2, 4, 2, true, true},
    {TypeId::TexCoord2f, "texCoord2f", "float2", TypeId::Float2, 8, 2, true, true},
    {TypeId::TexCoord2d, "texCoord2d", "double2", TypeId::Double2, 16, 2, true, true},
    {TypeId::TexCoord3h, "texCoord3h", "half3", TypeId::Half3, 6, 3, true, true},
    {TypeId::TexCoord3f, "texCoord3f", "float3", TypeId::Float3, 12, 3, true, true},
    {TypeId::TexCoord3d, "texCoord3d", "double3", TypeId::Double3, 24, 3, true, true},

    // Dictionary
    {TypeId::Dictionary, "dictionary", nullptr, TypeId::Dictionary, sizeof(void*), 1, false, false},
};

constexpr size_t kNumTypes = sizeof(kTypeDescriptors) / sizeof(kTypeDescriptors[0]);

} // anonymous namespace

const TypeDescriptor* get_type_descriptor(TypeId id) {
    // Handle array types - return base type descriptor
    TypeId base_id = get_base_type(id);
    uint32_t idx = static_cast<uint32_t>(base_id);

    if (idx >= kNumTypes) {
        return nullptr;
    }

    return &kTypeDescriptors[idx];
}

const TypeDescriptor* get_type_descriptor(const char* name) {
    if (!name) {
        return nullptr;
    }

    // Linear search - could optimize with hash table if needed
    for (size_t i = 0; i < kNumTypes; ++i) {
        if (kTypeDescriptors[i].name && std::strcmp(kTypeDescriptors[i].name, name) == 0) {
            return &kTypeDescriptors[i];
        }
    }

    return nullptr;
}

const char* get_type_name(TypeId id) {
    const TypeDescriptor* desc = get_type_descriptor(id);
    if (!desc) {
        return "invalid";
    }
    return desc->name;
}

TypeId get_type_id(const char* name) {
    const TypeDescriptor* desc = get_type_descriptor(name);
    if (!desc) {
        return TypeId::Invalid;
    }
    return desc->id;
}

} // namespace v1
} // namespace lightusd
