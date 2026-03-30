// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Runtime type information implementation

#include "type-info.hh"

#include <cstring>
#include <array>

namespace tinyusdz {
namespace next {

namespace {

// ============================================================
// Half-precision float storage type
// ============================================================
struct half_t {
  uint16_t bits;
};

// ============================================================
// Basic type structs for memory layout
// ============================================================

template <typename T, size_t N>
struct VecN {
  T data[N];
};

template <typename T>
struct Quat {
  T x, y, z, w;
};

template <typename T, size_t N>
struct MatrixNxN {
  T data[N * N];
};

// Type aliases for clarity
using int2 = VecN<int32_t, 2>;
using int3 = VecN<int32_t, 3>;
using int4 = VecN<int32_t, 4>;
using uint2 = VecN<uint32_t, 2>;
using uint3 = VecN<uint32_t, 3>;
using uint4 = VecN<uint32_t, 4>;
using half2 = VecN<half_t, 2>;
using half3 = VecN<half_t, 3>;
using half4 = VecN<half_t, 4>;
using float2 = VecN<float, 2>;
using float3 = VecN<float, 3>;
using float4 = VecN<float, 4>;
using double2 = VecN<double, 2>;
using double3 = VecN<double, 3>;
using double4 = VecN<double, 4>;

using quath = Quat<half_t>;
using quatf = Quat<float>;
using quatd = Quat<double>;

using matrix2f = MatrixNxN<float, 2>;
using matrix2d = MatrixNxN<double, 2>;
using matrix3f = MatrixNxN<float, 3>;
using matrix3d = MatrixNxN<double, 3>;
using matrix4f = MatrixNxN<float, 4>;
using matrix4d = MatrixNxN<double, 4>;

// Extent is float3[2]
struct extent_t {
  float3 min;
  float3 max;
};

// ============================================================
// Generic operation templates
// ============================================================

template <typename T>
void GenericConstruct(void* dest) {
  new (dest) T();
}

template <typename T>
void GenericDestruct(void* obj) {
  static_cast<T*>(obj)->~T();
}

template <typename T>
void GenericCopy(void* dest, const void* src) {
  *static_cast<T*>(dest) = *static_cast<const T*>(src);
}

template <typename T>
void GenericMove(void* dest, void* src) {
  *static_cast<T*>(dest) = static_cast<T&&>(*static_cast<T*>(src));
}

template <typename T>
bool GenericEquals(const void* a, const void* b) {
  return *static_cast<const T*>(a) == *static_cast<const T*>(b);
}

// POD-specific operations (faster, no constructor/destructor needed)
template <typename T>
void PodConstruct(void* dest) {
  std::memset(dest, 0, sizeof(T));
}

template <typename T>
void PodDestruct(void*) {
  // No-op for POD types
}

template <typename T>
void PodCopy(void* dest, const void* src) {
  std::memcpy(dest, src, sizeof(T));
}

template <typename T>
void PodMove(void* dest, void* src) {
  std::memcpy(dest, src, sizeof(T));
}

template <typename T>
bool PodEquals(const void* a, const void* b) {
  return std::memcmp(a, b, sizeof(T)) == 0;
}

// ============================================================
// Type info table
// ============================================================

constexpr size_t kTypeCount = static_cast<size_t>(TypeId::Count);

// Macro to define POD type info entry
#define POD_TYPE_INFO(id_val, usd_name, cpp_name_str, type) \
  { TypeId::id_val, usd_name, cpp_name_str, sizeof(type), alignof(type), \
    &PodConstruct<type>, &PodDestruct<type>, &PodCopy<type>, \
    &PodMove<type>, &PodEquals<type> }

// Macro to define complex type info entry (with constructor/destructor)
#define COMPLEX_TYPE_INFO(id_val, usd_name, cpp_name_str, type) \
  { TypeId::id_val, usd_name, cpp_name_str, sizeof(type), alignof(type), \
    &GenericConstruct<type>, &GenericDestruct<type>, &GenericCopy<type>, \
    &GenericMove<type>, &GenericEquals<type> }

// Static type info array - indexed directly by TypeId
std::array<TypeInfo, kTypeCount> g_type_info = {{
  // Invalid
  { TypeId::Invalid, nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr },

  // Scalars
  POD_TYPE_INFO(Bool, "bool", "bool", bool),
  POD_TYPE_INFO(Int, "int", "int32_t", int32_t),
  POD_TYPE_INFO(UInt, "uint", "uint32_t", uint32_t),
  POD_TYPE_INFO(Int64, "int64", "int64_t", int64_t),
  POD_TYPE_INFO(UInt64, "uint64", "uint64_t", uint64_t),
  POD_TYPE_INFO(Half, "half", "half", half_t),
  POD_TYPE_INFO(Float, "float", "float", float),
  POD_TYPE_INFO(Double, "double", "double", double),

  // String types (variable size - use 0)
  { TypeId::String, "string", "std::string", 0, 1, nullptr, nullptr, nullptr, nullptr, nullptr },
  { TypeId::Token, "token", "Token", 0, 1, nullptr, nullptr, nullptr, nullptr, nullptr },
  { TypeId::AssetPath, "asset", "AssetPath", 0, 1, nullptr, nullptr, nullptr, nullptr, nullptr },

  // Integer vectors
  POD_TYPE_INFO(Int2, "int2", "int2", int2),
  POD_TYPE_INFO(Int3, "int3", "int3", int3),
  POD_TYPE_INFO(Int4, "int4", "int4", int4),
  POD_TYPE_INFO(UInt2, "uint2", "uint2", uint2),
  POD_TYPE_INFO(UInt3, "uint3", "uint3", uint3),
  POD_TYPE_INFO(UInt4, "uint4", "uint4", uint4),

  // Half vectors
  POD_TYPE_INFO(Half2, "half2", "half2", half2),
  POD_TYPE_INFO(Half3, "half3", "half3", half3),
  POD_TYPE_INFO(Half4, "half4", "half4", half4),

  // Float vectors
  POD_TYPE_INFO(Float2, "float2", "float2", float2),
  POD_TYPE_INFO(Float3, "float3", "float3", float3),
  POD_TYPE_INFO(Float4, "float4", "float4", float4),

  // Double vectors
  POD_TYPE_INFO(Double2, "double2", "double2", double2),
  POD_TYPE_INFO(Double3, "double3", "double3", double3),
  POD_TYPE_INFO(Double4, "double4", "double4", double4),

  // Quaternions
  POD_TYPE_INFO(Quath, "quath", "quath", quath),
  POD_TYPE_INFO(Quatf, "quatf", "quatf", quatf),
  POD_TYPE_INFO(Quatd, "quatd", "quatd", quatd),

  // Point types (same storage as vectors)
  POD_TYPE_INFO(Point3h, "point3h", "point3h", half3),
  POD_TYPE_INFO(Point3f, "point3f", "point3f", float3),
  POD_TYPE_INFO(Point3d, "point3d", "point3d", double3),

  // Vector types
  POD_TYPE_INFO(Vector3h, "vector3h", "vector3h", half3),
  POD_TYPE_INFO(Vector3f, "vector3f", "vector3f", float3),
  POD_TYPE_INFO(Vector3d, "vector3d", "vector3d", double3),

  // Normal types
  POD_TYPE_INFO(Normal3h, "normal3h", "normal3h", half3),
  POD_TYPE_INFO(Normal3f, "normal3f", "normal3f", float3),
  POD_TYPE_INFO(Normal3d, "normal3d", "normal3d", double3),

  // Color types
  POD_TYPE_INFO(Color3h, "color3h", "color3h", half3),
  POD_TYPE_INFO(Color3f, "color3f", "color3f", float3),
  POD_TYPE_INFO(Color3d, "color3d", "color3d", double3),
  POD_TYPE_INFO(Color4h, "color4h", "color4h", half4),
  POD_TYPE_INFO(Color4f, "color4f", "color4f", float4),
  POD_TYPE_INFO(Color4d, "color4d", "color4d", double4),

  // Matrices
  POD_TYPE_INFO(Matrix2f, "matrix2d", "matrix2f", matrix2f),  // USD uses 'd' suffix
  POD_TYPE_INFO(Matrix2d, "matrix2d", "matrix2d", matrix2d),
  POD_TYPE_INFO(Matrix3f, "matrix3d", "matrix3f", matrix3f),
  POD_TYPE_INFO(Matrix3d, "matrix3d", "matrix3d", matrix3d),
  POD_TYPE_INFO(Matrix4f, "matrix4d", "matrix4f", matrix4f),
  POD_TYPE_INFO(Matrix4d, "matrix4d", "matrix4d", matrix4d),

  // Texture coordinates
  POD_TYPE_INFO(Texcoord2h, "texCoord2h", "texcoord2h", half2),
  POD_TYPE_INFO(Texcoord2f, "texCoord2f", "texcoord2f", float2),
  POD_TYPE_INFO(Texcoord2d, "texCoord2d", "texcoord2d", double2),
  POD_TYPE_INFO(Texcoord3h, "texCoord3h", "texcoord3h", half3),
  POD_TYPE_INFO(Texcoord3f, "texCoord3f", "texcoord3f", float3),
  POD_TYPE_INFO(Texcoord3d, "texCoord3d", "texcoord3d", double3),

  // Special types
  POD_TYPE_INFO(TimeCode, "timecode", "TimeCode", double),
  POD_TYPE_INFO(Extent, "float3[]", "extent", extent_t),
  { TypeId::Dictionary, "dictionary", "Dictionary", 0, 1, nullptr, nullptr, nullptr, nullptr, nullptr },

  // Relationship types
  { TypeId::Relationship, "rel", "Relationship", 0, 1, nullptr, nullptr, nullptr, nullptr, nullptr },
  { TypeId::Reference, "reference", "Reference", 0, 1, nullptr, nullptr, nullptr, nullptr, nullptr },
}};

#undef POD_TYPE_INFO
#undef COMPLEX_TYPE_INFO

bool g_registry_initialized = false;

}  // anonymous namespace

const TypeInfo* GetTypeInfo(TypeId id) {
  size_t idx = static_cast<size_t>(id);
  if (idx >= kTypeCount) {
    return nullptr;
  }
  const TypeInfo* info = &g_type_info[idx];
  if (info->id == TypeId::Invalid && id != TypeId::Invalid) {
    return nullptr;
  }
  return info;
}

bool RegisterTypeInfo(const TypeInfo& info) {
  size_t idx = static_cast<size_t>(info.id);
  if (idx >= kTypeCount) {
    return false;
  }
  if (g_type_info[idx].id != TypeId::Invalid) {
    return false;  // Already registered
  }
  g_type_info[idx] = info;
  return true;
}

void InitTypeRegistry() {
  if (g_registry_initialized) {
    return;
  }
  // Currently all types are statically initialized
  // This function exists for future dynamic registration
  g_registry_initialized = true;
}

// ============================================================
// type-id.hh function implementations
// ============================================================

const char* GetTypeName(TypeId id) {
  const TypeInfo* info = GetTypeInfo(id);
  return info ? info->name : nullptr;
}

TypeId GetTypeIdFromName(const char* name) {
  if (!name) {
    return TypeId::Invalid;
  }
  for (size_t i = 1; i < kTypeCount; ++i) {
    if (g_type_info[i].name && std::strcmp(g_type_info[i].name, name) == 0) {
      return static_cast<TypeId>(i);
    }
  }
  return TypeId::Invalid;
}

size_t GetTypeSize(TypeId id) {
  const TypeInfo* info = GetTypeInfo(id);
  return info ? info->size : 0;
}

size_t GetTypeAlignment(TypeId id) {
  const TypeInfo* info = GetTypeInfo(id);
  return info ? info->alignment : 0;
}

bool IsScalarType(TypeId id) {
  switch (id) {
    case TypeId::Bool:
    case TypeId::Int:
    case TypeId::UInt:
    case TypeId::Int64:
    case TypeId::UInt64:
    case TypeId::Half:
    case TypeId::Float:
    case TypeId::Double:
    case TypeId::TimeCode:
      return true;
    default:
      return false;
  }
}

bool IsNumericType(TypeId id) {
  switch (id) {
    case TypeId::Bool:
    case TypeId::Int:
    case TypeId::UInt:
    case TypeId::Int64:
    case TypeId::UInt64:
    case TypeId::Half:
    case TypeId::Float:
    case TypeId::Double:
    case TypeId::Int2:
    case TypeId::Int3:
    case TypeId::Int4:
    case TypeId::UInt2:
    case TypeId::UInt3:
    case TypeId::UInt4:
    case TypeId::Half2:
    case TypeId::Half3:
    case TypeId::Half4:
    case TypeId::Float2:
    case TypeId::Float3:
    case TypeId::Float4:
    case TypeId::Double2:
    case TypeId::Double3:
    case TypeId::Double4:
    case TypeId::Quath:
    case TypeId::Quatf:
    case TypeId::Quatd:
    case TypeId::Matrix2f:
    case TypeId::Matrix2d:
    case TypeId::Matrix3f:
    case TypeId::Matrix3d:
    case TypeId::Matrix4f:
    case TypeId::Matrix4d:
      return true;
    default:
      return false;
  }
}

TypeId GetComponentType(TypeId id) {
  switch (id) {
    case TypeId::Int2:
    case TypeId::Int3:
    case TypeId::Int4:
      return TypeId::Int;

    case TypeId::UInt2:
    case TypeId::UInt3:
    case TypeId::UInt4:
      return TypeId::UInt;

    case TypeId::Half2:
    case TypeId::Half3:
    case TypeId::Half4:
    case TypeId::Quath:
    case TypeId::Point3h:
    case TypeId::Vector3h:
    case TypeId::Normal3h:
    case TypeId::Color3h:
    case TypeId::Color4h:
    case TypeId::Texcoord2h:
    case TypeId::Texcoord3h:
      return TypeId::Half;

    case TypeId::Float2:
    case TypeId::Float3:
    case TypeId::Float4:
    case TypeId::Quatf:
    case TypeId::Point3f:
    case TypeId::Vector3f:
    case TypeId::Normal3f:
    case TypeId::Color3f:
    case TypeId::Color4f:
    case TypeId::Texcoord2f:
    case TypeId::Texcoord3f:
    case TypeId::Matrix2f:
    case TypeId::Matrix3f:
    case TypeId::Matrix4f:
      return TypeId::Float;

    case TypeId::Double2:
    case TypeId::Double3:
    case TypeId::Double4:
    case TypeId::Quatd:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d:
    case TypeId::Color3d:
    case TypeId::Color4d:
    case TypeId::Texcoord2d:
    case TypeId::Texcoord3d:
    case TypeId::Matrix2d:
    case TypeId::Matrix3d:
    case TypeId::Matrix4d:
      return TypeId::Double;

    default:
      return TypeId::Invalid;
  }
}

size_t GetComponentCount(TypeId id) {
  switch (id) {
    // Scalars
    case TypeId::Bool:
    case TypeId::Int:
    case TypeId::UInt:
    case TypeId::Int64:
    case TypeId::UInt64:
    case TypeId::Half:
    case TypeId::Float:
    case TypeId::Double:
    case TypeId::TimeCode:
      return 1;

    // 2-component
    case TypeId::Int2:
    case TypeId::UInt2:
    case TypeId::Half2:
    case TypeId::Float2:
    case TypeId::Double2:
    case TypeId::Texcoord2h:
    case TypeId::Texcoord2f:
    case TypeId::Texcoord2d:
      return 2;

    // 3-component
    case TypeId::Int3:
    case TypeId::UInt3:
    case TypeId::Half3:
    case TypeId::Float3:
    case TypeId::Double3:
    case TypeId::Point3h:
    case TypeId::Point3f:
    case TypeId::Point3d:
    case TypeId::Vector3h:
    case TypeId::Vector3f:
    case TypeId::Vector3d:
    case TypeId::Normal3h:
    case TypeId::Normal3f:
    case TypeId::Normal3d:
    case TypeId::Color3h:
    case TypeId::Color3f:
    case TypeId::Color3d:
    case TypeId::Texcoord3h:
    case TypeId::Texcoord3f:
    case TypeId::Texcoord3d:
      return 3;

    // 4-component
    case TypeId::Int4:
    case TypeId::UInt4:
    case TypeId::Half4:
    case TypeId::Float4:
    case TypeId::Double4:
    case TypeId::Quath:
    case TypeId::Quatf:
    case TypeId::Quatd:
    case TypeId::Color4h:
    case TypeId::Color4f:
    case TypeId::Color4d:
    case TypeId::Matrix2f:
    case TypeId::Matrix2d:
      return 4;

    // 9-component (3x3 matrix)
    case TypeId::Matrix3f:
    case TypeId::Matrix3d:
      return 9;

    // 16-component (4x4 matrix)
    case TypeId::Matrix4f:
    case TypeId::Matrix4d:
      return 16;

    default:
      return 0;
  }
}

}  // namespace next
}  // namespace tinyusdz
