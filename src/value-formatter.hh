// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Value type formatting utilities
// Part of the pprinter.cc modularization effort

#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <cstdint>
#include "value-types.hh"

namespace tinyusdz {
namespace pprint {

// Forward declarations
class PrintConfig;

// Value formatter - handles formatting of all value types
class ValueFormatter {
 public:
  ValueFormatter();
  explicit ValueFormatter(const PrintConfig &config);
  
  // Configuration
  void SetConfig(const PrintConfig &config) { config_ = &config; }
  void SetPrecision(int float_precision, int double_precision);
  void SetScientificNotation(bool use_scientific);
  
  // Main value formatting method
  std::string Format(const value::Value &val, uint32_t indent = 0);
  
  // Type-specific formatting
  std::string FormatBool(bool value);
  std::string FormatChar(char value);
  std::string FormatUChar(unsigned char value);
  std::string FormatShort(int16_t value);
  std::string FormatUShort(uint16_t value);
  std::string FormatInt(int32_t value);
  std::string FormatUInt(uint32_t value);
  std::string FormatInt64(int64_t value);
  std::string FormatUInt64(uint64_t value);
  std::string FormatHalf(value::half value);
  std::string FormatFloat(float value);
  std::string FormatDouble(double value);
  
  // String types
  std::string FormatString(const std::string &str);
  std::string FormatToken(const value::token &tok);
  std::string FormatStringData(const value::StringData &str);
  
  // Path types
  std::string FormatPath(const Path &path);
  std::string FormatAssetPath(const value::AssetPath &path);
  
  // Time types
  std::string FormatTimeCode(const value::TimeCode &tc);
  std::string FormatLayerOffset(const value::LayerOffset &offset);
  
  // Vector types
  std::string FormatFloat2(const value::float2 &v);
  std::string FormatFloat3(const value::float3 &v);
  std::string FormatFloat4(const value::float4 &v);
  std::string FormatDouble2(const value::double2 &v);
  std::string FormatDouble3(const value::double3 &v);
  std::string FormatDouble4(const value::double4 &v);
  std::string FormatHalf2(const value::half2 &v);
  std::string FormatHalf3(const value::half3 &v);
  std::string FormatHalf4(const value::half4 &v);
  
  // Integer vector types
  std::string FormatInt2(const value::int2 &v);
  std::string FormatInt3(const value::int3 &v);
  std::string FormatInt4(const value::int4 &v);
  std::string FormatUInt2(const value::uint2 &v);
  std::string FormatUInt3(const value::uint3 &v);
  std::string FormatUInt4(const value::uint4 &v);
  
  // Matrix types
  std::string FormatMatrix2d(const value::matrix2d &m);
  std::string FormatMatrix3d(const value::matrix3d &m);
  std::string FormatMatrix4d(const value::matrix4d &m);
  std::string FormatMatrix2f(const value::matrix2f &m);
  std::string FormatMatrix3f(const value::matrix3f &m);
  std::string FormatMatrix4f(const value::matrix4f &m);
  
  // Quaternion types
  std::string FormatQuath(const value::quath &q);
  std::string FormatQuatf(const value::quatf &q);
  std::string FormatQuatd(const value::quatd &q);
  
  // Normal/Point/Vector types
  std::string FormatNormal3h(const value::normal3h &n);
  std::string FormatNormal3f(const value::normal3f &n);
  std::string FormatNormal3d(const value::normal3d &n);
  std::string FormatPoint3h(const value::point3h &p);
  std::string FormatPoint3f(const value::point3f &p);
  std::string FormatPoint3d(const value::point3d &p);
  std::string FormatVector3h(const value::vector3h &v);
  std::string FormatVector3f(const value::vector3f &v);
  std::string FormatVector3d(const value::vector3d &v);
  
  // Color types
  std::string FormatColor3h(const value::color3h &c);
  std::string FormatColor3f(const value::color3f &c);
  std::string FormatColor3d(const value::color3d &c);
  std::string FormatColor4h(const value::color4h &c);
  std::string FormatColor4f(const value::color4f &c);
  std::string FormatColor4d(const value::color4d &c);
  
  // TexCoord types
  std::string FormatTexCoord2h(const value::texcoord2h &tc);
  std::string FormatTexCoord2f(const value::texcoord2f &tc);
  std::string FormatTexCoord2d(const value::texcoord2d &tc);
  std::string FormatTexCoord3h(const value::texcoord3h &tc);
  std::string FormatTexCoord3f(const value::texcoord3f &tc);
  std::string FormatTexCoord3d(const value::texcoord3d &tc);
  
  // Rect types
  std::string FormatRect2i(const value::rect2i &r);
  std::string FormatRect2f(const value::rect2f &r);
  std::string FormatRect2d(const value::rect2d &r);
  
  // Range types
  std::string FormatRange1i(const value::range1i &r);
  std::string FormatRange1f(const value::range1f &r);
  std::string FormatRange1d(const value::range1d &r);
  std::string FormatRange2i(const value::range2i &r);
  std::string FormatRange2f(const value::range2f &r);
  std::string FormatRange2d(const value::range2d &r);
  std::string FormatRange3i(const value::range3i &r);
  std::string FormatRange3f(const value::range3f &r);
  std::string FormatRange3d(const value::range3d &r);
  std::string FormatInterval(const value::interval &i);
  
  // Container types
  std::string FormatDictionary(const value::dict &dict, uint32_t indent = 0);
  std::string FormatTimeSamples(const value::TimeSamples &samples, uint32_t indent = 0);
  std::string FormatVariantSelectionMap(const value::VariantSelectionMap &map);
  
  // Array types
  template <typename T>
  std::string FormatArray(const std::vector<T> &array, uint32_t indent = 0);
  
  template <typename T>
  std::string FormatArray(const value::ArrayValue<T> &array, uint32_t indent = 0);
  
  // Special values
  std::string FormatNone();
  std::string FormatDefault();
  std::string FormatValueBlock();
  
  // Type name utilities
  static std::string GetTypeName(const value::Value &val);
  static std::string GetTypeName(uint32_t typeId);
  static bool IsNumericType(uint32_t typeId);
  static bool IsVectorType(uint32_t typeId);
  static bool IsMatrixType(uint32_t typeId);
  static bool IsArrayType(uint32_t typeId);
  
 private:
  const PrintConfig *config_;
  int float_precision_{6};
  int double_precision_{15};
  bool use_scientific_{false};
  
  // Numeric formatting helpers
  std::string FormatFloatImpl(float value);
  std::string FormatDoubleImpl(double value);
  std::string FormatHalfImpl(value::half value);
  
  // Vector formatting helpers
  template <typename T, size_t N>
  std::string FormatVector(const std::array<T, N> &vec, 
                           const std::string &prefix = "(",
                           const std::string &suffix = ")");
  
  // Matrix formatting helpers
  template <typename T, size_t N>
  std::string FormatMatrix(const value::matrix<T, N> &mat);
  
  // Quaternion formatting helpers
  template <typename T>
  std::string FormatQuaternion(const value::quaternion<T> &quat);
  
  // String escaping
  std::string EscapeString(const std::string &str);
  std::string QuoteString(const std::string &str);
};

// Specialized formatters for different contexts
class CompactValueFormatter : public ValueFormatter {
 public:
  CompactValueFormatter();
  
  // Override for compact single-line output
  std::string Format(const value::Value &val, uint32_t indent = 0);
};

class DebugValueFormatter : public ValueFormatter {
 public:
  DebugValueFormatter();
  
  // Include type information and internal details
  std::string Format(const value::Value &val, uint32_t indent = 0);
  std::string FormatWithTypeInfo(const value::Value &val, uint32_t indent = 0);
};

// Global value formatting functions
std::string FormatValue(const value::Value &val);
std::string FormatValueCompact(const value::Value &val);
std::string FormatValueDebug(const value::Value &val);

// Type conversion utilities
std::string ValueToString(const value::Value &val);
bool StringToValue(const std::string &str, const std::string &type_name,
                   value::Value *out_value);

// Fast numeric to string conversions
std::string FastItoa(int32_t value);
std::string FastItoa(int64_t value);
std::string FastUtoa(uint32_t value);
std::string FastUtoa(uint64_t value);
std::string FastFtoa(float value, int precision = -1);
std::string FastDtoa(double value, int precision = -1);

} // namespace pprint
} // namespace tinyusdz