// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "value-formatter.hh"
#include "pprinter-core.hh"
#include "pretty-print-utils.hh"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace tinyusdz {
namespace pprint {

ValueFormatter::ValueFormatter() 
    : config_(nullptr), 
      float_precision_(6), 
      double_precision_(15), 
      use_scientific_(false) {
}

ValueFormatter::ValueFormatter(const PrintConfig &config)
    : config_(&config),
      float_precision_(config.float_precision),
      double_precision_(config.double_precision),
      use_scientific_(config.use_scientific_notation) {
}

void ValueFormatter::SetPrecision(int float_precision, int double_precision) {
  float_precision_ = float_precision;
  double_precision_ = double_precision;
}

void ValueFormatter::SetScientificNotation(bool use_scientific) {
  use_scientific_ = use_scientific;
}

std::string ValueFormatter::Format(const value::Value &val, uint32_t indent) {
  // Dispatch based on type
  if (val.type_id() == value::TYPE_ID_NULL) {
    return FormatNone();
  }
  
  if (val.type_id() == value::TYPE_ID_VALUEBLOCK) {
    return FormatValueBlock();
  }
  
  // Basic types
  if (auto v = val.as<bool>()) return FormatBool(*v);
  if (auto v = val.as<int32_t>()) return FormatInt(*v);
  if (auto v = val.as<uint32_t>()) return FormatUInt(*v);
  if (auto v = val.as<int64_t>()) return FormatInt64(*v);
  if (auto v = val.as<uint64_t>()) return FormatUInt64(*v);
  if (auto v = val.as<value::half>()) return FormatHalf(*v);
  if (auto v = val.as<float>()) return FormatFloat(*v);
  if (auto v = val.as<double>()) return FormatDouble(*v);
  
  // String types
  if (auto v = val.as<std::string>()) return FormatString(*v);
  if (auto v = val.as<value::token>()) return FormatToken(*v);
  if (auto v = val.as<value::StringData>()) return FormatStringData(*v);
  
  // Path types
  if (auto v = val.as<Path>()) return FormatPath(*v);
  if (auto v = val.as<value::AssetPath>()) return FormatAssetPath(*v);
  
  // Time types
  if (auto v = val.as<value::TimeCode>()) return FormatTimeCode(*v);
  
  // Vector types
  if (auto v = val.as<value::float2>()) return FormatFloat2(*v);
  if (auto v = val.as<value::float3>()) return FormatFloat3(*v);
  if (auto v = val.as<value::float4>()) return FormatFloat4(*v);
  if (auto v = val.as<value::double2>()) return FormatDouble2(*v);
  if (auto v = val.as<value::double3>()) return FormatDouble3(*v);
  if (auto v = val.as<value::double4>()) return FormatDouble4(*v);
  
  // Matrix types
  if (auto v = val.as<value::matrix2d>()) return FormatMatrix2d(*v);
  if (auto v = val.as<value::matrix3d>()) return FormatMatrix3d(*v);
  if (auto v = val.as<value::matrix4d>()) return FormatMatrix4d(*v);
  
  // Quaternion types
  if (auto v = val.as<value::quatf>()) return FormatQuatf(*v);
  if (auto v = val.as<value::quatd>()) return FormatQuatd(*v);
  
  // Container types
  if (auto v = val.as<value::dict>()) return FormatDictionary(*v, indent);
  if (auto v = val.as<value::TimeSamples>()) return FormatTimeSamples(*v, indent);
  
  // Default
  return "<unknown type>";
}

// Basic types
std::string ValueFormatter::FormatBool(bool value) {
  return value ? "true" : "false";
}

std::string ValueFormatter::FormatChar(char value) {
  return std::to_string(static_cast<int>(value));
}

std::string ValueFormatter::FormatUChar(unsigned char value) {
  return std::to_string(static_cast<unsigned>(value));
}

std::string ValueFormatter::FormatInt(int32_t value) {
  return std::to_string(value);
}

std::string ValueFormatter::FormatUInt(uint32_t value) {
  return std::to_string(value);
}

std::string ValueFormatter::FormatInt64(int64_t value) {
  return std::to_string(value);
}

std::string ValueFormatter::FormatUInt64(uint64_t value) {
  return std::to_string(value);
}

std::string ValueFormatter::FormatHalf(value::half value) {
  return FormatHalfImpl(value);
}

std::string ValueFormatter::FormatFloat(float value) {
  return FormatFloatImpl(value);
}

std::string ValueFormatter::FormatDouble(double value) {
  return FormatDoubleImpl(value);
}

// String types
std::string ValueFormatter::FormatString(const std::string &str) {
  return QuoteString(EscapeString(str));
}

std::string ValueFormatter::FormatToken(const value::token &tok) {
  return QuoteString(tok.str());
}

std::string ValueFormatter::FormatStringData(const value::StringData &str) {
  if (str.is_multiline()) {
    return "\"\"\"" + str.get() + "\"\"\"";
  }
  return QuoteString(EscapeString(str.get()));
}

// Path types
std::string ValueFormatter::FormatPath(const Path &path) {
  return "<" + path.full_path_name() + ">";
}

std::string ValueFormatter::FormatAssetPath(const value::AssetPath &path) {
  if (!path.GetResolvedPath().empty()) {
    return "@" + path.GetAssetPath() + "@";
  }
  return "@" + path.GetAssetPath() + "@";
}

// Time types
std::string ValueFormatter::FormatTimeCode(const value::TimeCode &tc) {
  if (tc.is_default()) {
    return "default";
  }
  return FormatDouble(tc.Get());
}

// Vector types
std::string ValueFormatter::FormatFloat2(const value::float2 &v) {
  std::stringstream ss;
  ss << "(" << FormatFloat(v[0]) << ", " << FormatFloat(v[1]) << ")";
  return ss.str();
}

std::string ValueFormatter::FormatFloat3(const value::float3 &v) {
  std::stringstream ss;
  ss << "(" << FormatFloat(v[0]) << ", " << FormatFloat(v[1]) << ", " << FormatFloat(v[2]) << ")";
  return ss.str();
}

std::string ValueFormatter::FormatFloat4(const value::float4 &v) {
  std::stringstream ss;
  ss << "(" << FormatFloat(v[0]) << ", " << FormatFloat(v[1]) 
     << ", " << FormatFloat(v[2]) << ", " << FormatFloat(v[3]) << ")";
  return ss.str();
}

std::string ValueFormatter::FormatDouble2(const value::double2 &v) {
  std::stringstream ss;
  ss << "(" << FormatDouble(v[0]) << ", " << FormatDouble(v[1]) << ")";
  return ss.str();
}

std::string ValueFormatter::FormatDouble3(const value::double3 &v) {
  std::stringstream ss;
  ss << "(" << FormatDouble(v[0]) << ", " << FormatDouble(v[1]) << ", " << FormatDouble(v[2]) << ")";
  return ss.str();
}

std::string ValueFormatter::FormatDouble4(const value::double4 &v) {
  std::stringstream ss;
  ss << "(" << FormatDouble(v[0]) << ", " << FormatDouble(v[1]) 
     << ", " << FormatDouble(v[2]) << ", " << FormatDouble(v[3]) << ")";
  return ss.str();
}

// Matrix types
std::string ValueFormatter::FormatMatrix2d(const value::matrix2d &m) {
  std::stringstream ss;
  ss << "( ";
  for (size_t i = 0; i < 2; ++i) {
    ss << "(";
    for (size_t j = 0; j < 2; ++j) {
      ss << FormatDouble(m.m[i][j]);
      if (j < 1) ss << ", ";
    }
    ss << ")";
    if (i < 1) ss << ", ";
  }
  ss << " )";
  return ss.str();
}

std::string ValueFormatter::FormatMatrix3d(const value::matrix3d &m) {
  std::stringstream ss;
  ss << "( ";
  for (size_t i = 0; i < 3; ++i) {
    ss << "(";
    for (size_t j = 0; j < 3; ++j) {
      ss << FormatDouble(m.m[i][j]);
      if (j < 2) ss << ", ";
    }
    ss << ")";
    if (i < 2) ss << ", ";
  }
  ss << " )";
  return ss.str();
}

std::string ValueFormatter::FormatMatrix4d(const value::matrix4d &m) {
  std::stringstream ss;
  ss << "( ";
  for (size_t i = 0; i < 4; ++i) {
    ss << "(";
    for (size_t j = 0; j < 4; ++j) {
      ss << FormatDouble(m.m[i][j]);
      if (j < 3) ss << ", ";
    }
    ss << ")";
    if (i < 3) ss << ", ";
  }
  ss << " )";
  return ss.str();
}

// Quaternion types
std::string ValueFormatter::FormatQuatf(const value::quatf &q) {
  std::stringstream ss;
  ss << "(" << FormatFloat(q.real) << ", " 
     << FormatFloat(q.imaginary[0]) << ", "
     << FormatFloat(q.imaginary[1]) << ", "
     << FormatFloat(q.imaginary[2]) << ")";
  return ss.str();
}

std::string ValueFormatter::FormatQuatd(const value::quatd &q) {
  std::stringstream ss;
  ss << "(" << FormatDouble(q.real) << ", " 
     << FormatDouble(q.imaginary[0]) << ", "
     << FormatDouble(q.imaginary[1]) << ", "
     << FormatDouble(q.imaginary[2]) << ")";
  return ss.str();
}

// Container types
std::string ValueFormatter::FormatDictionary(const value::dict &dict, uint32_t indent) {
  std::stringstream ss;
  ss << "{\n";
  
  for (const auto &kv : dict) {
    ss << pprint::Indent(indent + 1);
    ss << QuoteString(kv.first) << ": ";
    ss << Format(*kv.second, indent + 1);
    ss << ",\n";
  }
  
  ss << pprint::Indent(indent) << "}";
  return ss.str();
}

std::string ValueFormatter::FormatTimeSamples(const value::TimeSamples &samples, uint32_t indent) {
  std::stringstream ss;
  ss << "{\n";
  
  for (const auto &sample : samples.samples()) {
    ss << pprint::Indent(indent + 1);
    ss << FormatDouble(sample.first) << ": ";
    ss << Format(*sample.second, indent + 1);
    ss << ",\n";
  }
  
  ss << pprint::Indent(indent) << "}";
  return ss.str();
}

// Special values
std::string ValueFormatter::FormatNone() {
  return "None";
}

std::string ValueFormatter::FormatDefault() {
  return "default";
}

std::string ValueFormatter::FormatValueBlock() {
  return "None";
}

// Private implementation methods
std::string ValueFormatter::FormatFloatImpl(float value) {
  if (std::isnan(value)) {
    return "nan";
  }
  if (std::isinf(value)) {
    return value > 0 ? "inf" : "-inf";
  }
  
  std::stringstream ss;
  if (use_scientific_) {
    ss << std::scientific;
  } else {
    ss << std::fixed;
  }
  ss << std::setprecision(float_precision_) << value;
  
  // Remove trailing zeros for cleaner output
  std::string result = ss.str();
  if (result.find('.') != std::string::npos) {
    result.erase(result.find_last_not_of('0') + 1, std::string::npos);
    if (result.back() == '.') {
      result.pop_back();
    }
  }
  
  return result;
}

std::string ValueFormatter::FormatDoubleImpl(double value) {
  if (std::isnan(value)) {
    return "nan";
  }
  if (std::isinf(value)) {
    return value > 0 ? "inf" : "-inf";
  }
  
  std::stringstream ss;
  if (use_scientific_) {
    ss << std::scientific;
  } else {
    ss << std::fixed;
  }
  ss << std::setprecision(double_precision_) << value;
  
  // Remove trailing zeros for cleaner output
  std::string result = ss.str();
  if (result.find('.') != std::string::npos) {
    result.erase(result.find_last_not_of('0') + 1, std::string::npos);
    if (result.back() == '.') {
      result.pop_back();
    }
  }
  
  return result;
}

std::string ValueFormatter::FormatHalfImpl(value::half value) {
  // Convert half to float for formatting
  // This is a simplified conversion - real implementation would use proper half-to-float
  float f = static_cast<float>(value.value) / 65536.0f;
  return FormatFloatImpl(f);
}

std::string ValueFormatter::EscapeString(const std::string &str) {
  return pprint::EscapeString(str);
}

std::string ValueFormatter::QuoteString(const std::string &str) {
  return "\"" + str + "\"";
}

// Global functions
std::string FormatValue(const value::Value &val) {
  ValueFormatter formatter;
  return formatter.Format(val);
}

std::string FormatValueCompact(const value::Value &val) {
  CompactValueFormatter formatter;
  return formatter.Format(val);
}

std::string FormatValueDebug(const value::Value &val) {
  DebugValueFormatter formatter;
  return formatter.Format(val);
}

std::string ValueToString(const value::Value &val) {
  return FormatValue(val);
}

// CompactValueFormatter implementation
CompactValueFormatter::CompactValueFormatter() : ValueFormatter() {
  SetPrecision(4, 6);  // Less precision for compact output
}

std::string CompactValueFormatter::Format(const value::Value &val, uint32_t indent) {
  // Override to provide single-line compact output
  // Skip indentation and newlines
  return ValueFormatter::Format(val, 0);
}

// DebugValueFormatter implementation
DebugValueFormatter::DebugValueFormatter() : ValueFormatter() {
  SetPrecision(9, 17);  // Max precision for debugging
}

std::string DebugValueFormatter::Format(const value::Value &val, uint32_t indent) {
  return FormatWithTypeInfo(val, indent);
}

std::string DebugValueFormatter::FormatWithTypeInfo(const value::Value &val, uint32_t indent) {
  std::stringstream ss;
  ss << "[Type:" << GetTypeName(val) << " ID:" << val.type_id() << "] ";
  ss << ValueFormatter::Format(val, indent);
  return ss.str();
}

// Type utilities
std::string ValueFormatter::GetTypeName(const value::Value &val) {
  return value::GetTypeNameFromTypeId(val.type_id());
}

std::string ValueFormatter::GetTypeName(uint32_t typeId) {
  return value::GetTypeNameFromTypeId(typeId);
}

} // namespace pprint
} // namespace tinyusdz