// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USD type-name parser table.

#include "value-parser.hh"

#include <unordered_map>
#include <string_view>

namespace tinyusdz {
namespace next {

namespace {

// ============================================================
// Type name to TypeId mapping
// ============================================================

struct TypeNameEntry {
  const char* name;
  TypeId id;
};

const TypeNameEntry kTypeNames[] = {
  // Scalars
  {"bool", TypeId::Bool},
  {"int", TypeId::Int},
  {"uint", TypeId::UInt},
  {"int64", TypeId::Int64},
  {"uint64", TypeId::UInt64},
  {"half", TypeId::Half},
  {"float", TypeId::Float},
  {"double", TypeId::Double},

  // Strings
  {"string", TypeId::String},
  {"token", TypeId::Token},
  {"asset", TypeId::AssetPath},

  // Integer vectors
  {"int2", TypeId::Int2},
  {"int3", TypeId::Int3},
  {"int4", TypeId::Int4},
  {"uint2", TypeId::UInt2},
  {"uint3", TypeId::UInt3},
  {"uint4", TypeId::UInt4},

  // Half vectors
  {"half2", TypeId::Half2},
  {"half3", TypeId::Half3},
  {"half4", TypeId::Half4},

  // Float vectors
  {"float2", TypeId::Float2},
  {"float3", TypeId::Float3},
  {"float4", TypeId::Float4},

  // Double vectors
  {"double2", TypeId::Double2},
  {"double3", TypeId::Double3},
  {"double4", TypeId::Double4},

  // Quaternions
  {"quath", TypeId::Quath},
  {"quatf", TypeId::Quatf},
  {"quatd", TypeId::Quatd},

  // Geometric types
  {"point3h", TypeId::Point3h},
  {"point3f", TypeId::Point3f},
  {"point3d", TypeId::Point3d},
  {"vector3h", TypeId::Vector3h},
  {"vector3f", TypeId::Vector3f},
  {"vector3d", TypeId::Vector3d},
  {"normal3h", TypeId::Normal3h},
  {"normal3f", TypeId::Normal3f},
  {"normal3d", TypeId::Normal3d},

  // Colors
  {"color3h", TypeId::Color3h},
  {"color3f", TypeId::Color3f},
  {"color3d", TypeId::Color3d},
  {"color4h", TypeId::Color4h},
  {"color4f", TypeId::Color4f},
  {"color4d", TypeId::Color4d},

  // Matrices
  {"matrix2d", TypeId::Matrix2d},
  {"matrix3d", TypeId::Matrix3d},
  {"matrix4d", TypeId::Matrix4d},

  // Texture coordinates
  {"texCoord2h", TypeId::Texcoord2h},
  {"texCoord2f", TypeId::Texcoord2f},
  {"texCoord2d", TypeId::Texcoord2d},
  {"texCoord3h", TypeId::Texcoord3h},
  {"texCoord3f", TypeId::Texcoord3f},
  {"texCoord3d", TypeId::Texcoord3d},

  // Special
  {"timecode", TypeId::TimeCode},
  {"dictionary", TypeId::Dictionary},
  {"rel", TypeId::Relationship},
};

struct TypeNameViewHash {
  using is_transparent = void;
  size_t operator()(std::string_view s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
};

struct TypeNameViewEq {
  using is_transparent = void;
  bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    return lhs == rhs;
  }
};

const std::unordered_map<std::string_view, TypeId, TypeNameViewHash,
                         TypeNameViewEq>& GetTypeNameMap() {
  // Populate inside the (thread-safe) function-local static initializer:
  // concurrent first callers (parallel parse/build_stage workers) must not
  // observe a half-built map (`static bool initialized` raced).
  static const std::unordered_map<std::string_view, TypeId, TypeNameViewHash,
                                  TypeNameViewEq>
      map = [] {
        std::unordered_map<std::string_view, TypeId, TypeNameViewHash,
                           TypeNameViewEq>
            m;
        m.reserve(sizeof(kTypeNames) / sizeof(kTypeNames[0]));
        for (const auto& entry : kTypeNames) {
          m[entry.name] = entry.id;
        }
        return m;
      }();
  return map;
}



}  // namespace

TypeId ParseTypeName(const std::string& type_name, bool& is_array) {
  is_array = false;
  std::string_view name_view(type_name);

  // Check for array suffix
  if (type_name.size() > 2 &&
      type_name[type_name.size() - 1] == ']' &&
      type_name[type_name.size() - 2] == '[') {
    name_view = std::string_view(type_name.data(),
                                 type_name.size() - 2);
    is_array = true;
  }

  const auto& map = GetTypeNameMap();
  auto it = map.find(name_view);
  if (it != map.end()) {
    return it->second;
  }

  return TypeId::Invalid;
}

}  // namespace next
}  // namespace tinyusdz
