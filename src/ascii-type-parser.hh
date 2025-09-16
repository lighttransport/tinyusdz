// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Type-specific parsing module for USD ASCII parser
#pragma once

#include <string>
#include <vector>
#include "value-types.hh"
#include "prim-types.hh"
#include "nonstd/optional.hpp"

namespace tinyusdz {
namespace ascii {

// Forward declaration
class AsciiLexer;

///
/// Type-specific parser for USD ASCII format.
/// Handles parsing of all USD value types including scalars, vectors, matrices, etc.
///
class AsciiTypeParser {
 public:
  AsciiTypeParser(AsciiLexer* lexer) : _lexer(lexer) {}

  // Boolean types
  bool ReadBasicType(nonstd::optional<bool>* value);
  bool ReadBasicType(bool* value);
  
  // Half precision float types
  bool ReadBasicType(nonstd::optional<value::half>* value);
  bool ReadBasicType(nonstd::optional<value::half2>* value);
  bool ReadBasicType(nonstd::optional<value::half3>* value);
  bool ReadBasicType(nonstd::optional<value::half4>* value);
  bool ReadBasicType(value::half* value);
  bool ReadBasicType(value::half2* value);
  bool ReadBasicType(value::half3* value);
  bool ReadBasicType(value::half4* value);
  
  // Integer types
  bool ReadBasicType(nonstd::optional<int32_t>* value);
  bool ReadBasicType(nonstd::optional<value::int2>* value);
  bool ReadBasicType(nonstd::optional<value::int3>* value);
  bool ReadBasicType(nonstd::optional<value::int4>* value);
  bool ReadBasicType(int32_t* value);
  bool ReadBasicType(value::int2* value);
  bool ReadBasicType(value::int3* value);
  bool ReadBasicType(value::int4* value);
  
  // Unsigned integer types
  bool ReadBasicType(nonstd::optional<uint32_t>* value);
  bool ReadBasicType(nonstd::optional<value::uint2>* value);
  bool ReadBasicType(nonstd::optional<value::uint3>* value);
  bool ReadBasicType(nonstd::optional<value::uint4>* value);
  bool ReadBasicType(uint32_t* value);
  bool ReadBasicType(value::uint2* value);
  bool ReadBasicType(value::uint3* value);
  bool ReadBasicType(value::uint4* value);
  
  // 64-bit integer types
  bool ReadBasicType(nonstd::optional<int64_t>* value);
  bool ReadBasicType(nonstd::optional<uint64_t>* value);
  bool ReadBasicType(int64_t* value);
  bool ReadBasicType(uint64_t* value);
  
  // Float types
  bool ReadBasicType(nonstd::optional<float>* value);
  bool ReadBasicType(nonstd::optional<value::float2>* value);
  bool ReadBasicType(nonstd::optional<value::float3>* value);
  bool ReadBasicType(nonstd::optional<value::float4>* value);
  bool ReadBasicType(float* value);
  bool ReadBasicType(value::float2* value);
  bool ReadBasicType(value::float3* value);
  bool ReadBasicType(value::float4* value);
  
  // Double types
  bool ReadBasicType(nonstd::optional<double>* value);
  bool ReadBasicType(nonstd::optional<value::double2>* value);
  bool ReadBasicType(nonstd::optional<value::double3>* value);
  bool ReadBasicType(nonstd::optional<value::double4>* value);
  bool ReadBasicType(double* value);
  bool ReadBasicType(value::double2* value);
  bool ReadBasicType(value::double3* value);
  bool ReadBasicType(value::double4* value);
  
  // Quaternion types
  bool ReadBasicType(nonstd::optional<value::quath>* value);
  bool ReadBasicType(nonstd::optional<value::quatf>* value);
  bool ReadBasicType(nonstd::optional<value::quatd>* value);
  bool ReadBasicType(value::quath* value);
  bool ReadBasicType(value::quatf* value);
  bool ReadBasicType(value::quatd* value);
  
  // Point types
  bool ReadBasicType(nonstd::optional<value::point3h>* value);
  bool ReadBasicType(nonstd::optional<value::point3f>* value);
  bool ReadBasicType(nonstd::optional<value::point3d>* value);
  bool ReadBasicType(value::point3h* value);
  bool ReadBasicType(value::point3f* value);
  bool ReadBasicType(value::point3d* value);
  
  // Vector types
  bool ReadBasicType(nonstd::optional<value::vector3h>* value);
  bool ReadBasicType(nonstd::optional<value::vector3f>* value);
  bool ReadBasicType(nonstd::optional<value::vector3d>* value);
  bool ReadBasicType(value::vector3h* value);
  bool ReadBasicType(value::vector3f* value);
  bool ReadBasicType(value::vector3d* value);
  
  // Normal types
  bool ReadBasicType(nonstd::optional<value::normal3h>* value);
  bool ReadBasicType(nonstd::optional<value::normal3f>* value);
  bool ReadBasicType(nonstd::optional<value::normal3d>* value);
  bool ReadBasicType(value::normal3h* value);
  bool ReadBasicType(value::normal3f* value);
  bool ReadBasicType(value::normal3d* value);
  
  // Color types
  bool ReadBasicType(nonstd::optional<value::color3h>* value);
  bool ReadBasicType(nonstd::optional<value::color3f>* value);
  bool ReadBasicType(nonstd::optional<value::color3d>* value);
  bool ReadBasicType(nonstd::optional<value::color4h>* value);
  bool ReadBasicType(nonstd::optional<value::color4f>* value);
  bool ReadBasicType(nonstd::optional<value::color4d>* value);
  bool ReadBasicType(value::color3h* value);
  bool ReadBasicType(value::color3f* value);
  bool ReadBasicType(value::color3d* value);
  bool ReadBasicType(value::color4h* value);
  bool ReadBasicType(value::color4f* value);
  bool ReadBasicType(value::color4d* value);
  
  // Matrix types
  bool ReadBasicType(nonstd::optional<value::matrix2f>* value);
  bool ReadBasicType(nonstd::optional<value::matrix3f>* value);
  bool ReadBasicType(nonstd::optional<value::matrix4f>* value);
  bool ReadBasicType(nonstd::optional<value::matrix2d>* value);
  bool ReadBasicType(nonstd::optional<value::matrix3d>* value);
  bool ReadBasicType(nonstd::optional<value::matrix4d>* value);
  bool ReadBasicType(value::matrix2f* value);
  bool ReadBasicType(value::matrix3f* value);
  bool ReadBasicType(value::matrix4f* value);
  bool ReadBasicType(value::matrix2d* value);
  bool ReadBasicType(value::matrix3d* value);
  bool ReadBasicType(value::matrix4d* value);
  
  // Texture coordinate types
  bool ReadBasicType(nonstd::optional<value::texcoord2h>* value);
  bool ReadBasicType(nonstd::optional<value::texcoord2f>* value);
  bool ReadBasicType(nonstd::optional<value::texcoord2d>* value);
  bool ReadBasicType(nonstd::optional<value::texcoord3h>* value);
  bool ReadBasicType(nonstd::optional<value::texcoord3f>* value);
  bool ReadBasicType(nonstd::optional<value::texcoord3d>* value);
  bool ReadBasicType(value::texcoord2h* value);
  bool ReadBasicType(value::texcoord2f* value);
  bool ReadBasicType(value::texcoord2d* value);
  bool ReadBasicType(value::texcoord3h* value);
  bool ReadBasicType(value::texcoord3f* value);
  bool ReadBasicType(value::texcoord3d* value);
  
  // String and token types
  bool ReadBasicType(nonstd::optional<value::StringData>* value);
  bool ReadBasicType(nonstd::optional<std::string>* value);
  bool ReadBasicType(nonstd::optional<value::token>* value);
  bool ReadBasicType(value::StringData* value);
  bool ReadBasicType(std::string* value);
  bool ReadBasicType(value::token* value);
  
  // Path types
  bool ReadBasicType(nonstd::optional<Path>* value);
  bool ReadBasicType(nonstd::optional<value::AssetPath>* value);
  bool ReadBasicType(Path* value);
  bool ReadBasicType(value::AssetPath* value);
  
  // Reference and payload types
  bool ReadBasicType(nonstd::optional<Reference>* value);
  bool ReadBasicType(nonstd::optional<Payload>* value);
  bool ReadBasicType(Reference* value);
  bool ReadBasicType(Payload* value);
  
  // Special value parsing
  bool ParseNone();
  bool ParseNaN(double* value);
  bool ParseInfinity(double* value);
  
  // Type name utilities
  bool ParseTypeName(std::string* type_name);
  bool IsKnownType(const std::string& type_name) const;
  uint32_t GetTypeId(const std::string& type_name) const;
  
  // Error handling
  void PushError(const std::string& msg);
  std::string GetError() const { return _err; }

 private:
  AsciiLexer* _lexer;
  std::string _err;
  
  // Helper methods for complex types
  template <typename T>
  bool ReadTuple(T* value, size_t n);
  
  template <typename T>
  bool ReadMatrix(T* value, size_t rows, size_t cols);
  
  bool ReadQuaternion(float* quat);
  bool ReadQuaternion(double* quat);
};

}  // namespace ascii
}  // namespace tinyusdz