// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value Printer Implementation

#include "value-printer.hh"
#include "../types/type-id.hh"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace tinyusdz {
namespace next {

namespace {

// Format a float value
std::string FormatFloat(float v, int precision) {
  if (std::isnan(v)) return "nan";
  if (std::isinf(v)) return v > 0 ? "inf" : "-inf";

  std::ostringstream ss;
  ss << std::setprecision(precision) << v;
  std::string s = ss.str();

  // Ensure there's a decimal point for clarity
  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) {
    s += ".0";
  }
  return s;
}

// Format a double value
std::string FormatDouble(double v, int precision) {
  if (std::isnan(v)) return "nan";
  if (std::isinf(v)) return v > 0 ? "inf" : "-inf";

  std::ostringstream ss;
  ss << std::setprecision(precision) << v;
  std::string s = ss.str();

  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) {
    s += ".0";
  }
  return s;
}

// Escape a string for USDA output
std::string EscapeString(const std::string& s) {
  std::string result;
  result.reserve(s.size() + 2);
  result += '"';
  for (char c : s) {
    switch (c) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:   result += c; break;
    }
  }
  result += '"';
  return result;
}

}  // anonymous namespace

std::string PrintValue(const Value& value, const PrintOptions& opts) {
  if (value.is_empty()) {
    return "None";
  }

  TypeId type_id = value.type_id();

  // Handle arrays
  if (value.is_array()) {
    std::ostringstream ss;
    ss << "[";

    if (type_id == TypeId::Float) {
      const auto* arr = value.as_float_array();
      if (arr) {
        size_t limit = opts.max_array_elements > 0 ?
                       std::min(opts.max_array_elements, arr->size()) : arr->size();
        for (size_t i = 0; i < limit; ++i) {
          if (i > 0) ss << ", ";
          ss << FormatFloat((*arr)[i], opts.float_precision);
        }
        if (limit < arr->size()) {
          ss << ", ...";
        }
      }
    } else if (type_id == TypeId::Int) {
      const auto* arr = value.as_int_array();
      if (arr) {
        size_t limit = opts.max_array_elements > 0 ?
                       std::min(opts.max_array_elements, arr->size()) : arr->size();
        for (size_t i = 0; i < limit; ++i) {
          if (i > 0) ss << ", ";
          ss << (*arr)[i];
        }
        if (limit < arr->size()) {
          ss << ", ...";
        }
      }
    } else if (type_id == TypeId::Float3) {
      // Float3 array - stored as flat floats
      const auto* arr = value.as_float_array();
      if (arr && arr->size() >= 3) {
        size_t count = arr->size() / 3;
        size_t limit = opts.max_array_elements > 0 ?
                       std::min(opts.max_array_elements, count) : count;
        for (size_t i = 0; i < limit; ++i) {
          if (i > 0) ss << ", ";
          ss << "(" << FormatFloat((*arr)[i*3], opts.float_precision)
             << ", " << FormatFloat((*arr)[i*3+1], opts.float_precision)
             << ", " << FormatFloat((*arr)[i*3+2], opts.float_precision) << ")";
        }
        if (limit < count) {
          ss << ", ...";
        }
      }
    }

    ss << "]";
    return ss.str();
  }

  // Handle scalars and vectors
  switch (type_id) {
    case TypeId::Bool: {
      const bool* v = value.as_bool();
      return v ? (*v ? "true" : "false") : "None";
    }

    case TypeId::Int: {
      const int32_t* v = value.as_int();
      return v ? std::to_string(*v) : "None";
    }

    case TypeId::UInt: {
      const uint32_t* v = value.as_uint();
      return v ? std::to_string(*v) : "None";
    }

    case TypeId::Int64: {
      const int64_t* v = value.as_int64();
      return v ? std::to_string(*v) : "None";
    }

    case TypeId::UInt64: {
      const uint64_t* v = value.as_uint64();
      return v ? std::to_string(*v) : "None";
    }

    case TypeId::Float: {
      const float* v = value.as_float();
      return v ? FormatFloat(*v, opts.float_precision) : "None";
    }

    case TypeId::Double: {
      const double* v = value.as_double();
      return v ? FormatDouble(*v, opts.double_precision) : "None";
    }

    case TypeId::String: {
      const std::string* v = value.as_string();
      return v ? EscapeString(*v) : "None";
    }

    case TypeId::Token: {
      const std::string* v = value.as_token();
      return v ? EscapeString(*v) : "None";
    }

    case TypeId::AssetPath: {
      const std::string* v = value.as_asset_path();
      return v ? ("@" + *v + "@") : "None";
    }

    case TypeId::Int2: {
      const int32_t* v = value.as_int2();
      if (!v) return "None";
      return "(" + std::to_string(v[0]) + ", " + std::to_string(v[1]) + ")";
    }

    case TypeId::Int3: {
      const int32_t* v = value.as_int3();
      if (!v) return "None";
      return "(" + std::to_string(v[0]) + ", " + std::to_string(v[1]) +
             ", " + std::to_string(v[2]) + ")";
    }

    case TypeId::Int4: {
      const int32_t* v = value.as_int4();
      if (!v) return "None";
      return "(" + std::to_string(v[0]) + ", " + std::to_string(v[1]) +
             ", " + std::to_string(v[2]) + ", " + std::to_string(v[3]) + ")";
    }

    case TypeId::Float2: {
      const float* v = value.as_float2();
      if (!v) return "None";
      return "(" + FormatFloat(v[0], opts.float_precision) + ", " +
             FormatFloat(v[1], opts.float_precision) + ")";
    }

    case TypeId::Float3:
    case TypeId::Point3f:
    case TypeId::Vector3f:
    case TypeId::Normal3f:
    case TypeId::Color3f: {
      const float* v = value.as_float3();
      if (!v) return "None";
      return "(" + FormatFloat(v[0], opts.float_precision) + ", " +
             FormatFloat(v[1], opts.float_precision) + ", " +
             FormatFloat(v[2], opts.float_precision) + ")";
    }

    case TypeId::Float4:
    case TypeId::Color4f: {
      const float* v = value.as_float4();
      if (!v) return "None";
      return "(" + FormatFloat(v[0], opts.float_precision) + ", " +
             FormatFloat(v[1], opts.float_precision) + ", " +
             FormatFloat(v[2], opts.float_precision) + ", " +
             FormatFloat(v[3], opts.float_precision) + ")";
    }

    case TypeId::Double2: {
      const double* v = value.as_double2();
      if (!v) return "None";
      return "(" + FormatDouble(v[0], opts.double_precision) + ", " +
             FormatDouble(v[1], opts.double_precision) + ")";
    }

    case TypeId::Double3:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d: {
      const double* v = value.as_double3();
      if (!v) return "None";
      return "(" + FormatDouble(v[0], opts.double_precision) + ", " +
             FormatDouble(v[1], opts.double_precision) + ", " +
             FormatDouble(v[2], opts.double_precision) + ")";
    }

    case TypeId::Double4: {
      const double* v = value.as_double4();
      if (!v) return "None";
      return "(" + FormatDouble(v[0], opts.double_precision) + ", " +
             FormatDouble(v[1], opts.double_precision) + ", " +
             FormatDouble(v[2], opts.double_precision) + ", " +
             FormatDouble(v[3], opts.double_precision) + ")";
    }

    case TypeId::Quatf: {
      const float* v = value.as_float4();  // Quat stored as 4 floats
      if (!v) return "None";
      return "(" + FormatFloat(v[0], opts.float_precision) + ", " +
             FormatFloat(v[1], opts.float_precision) + ", " +
             FormatFloat(v[2], opts.float_precision) + ", " +
             FormatFloat(v[3], opts.float_precision) + ")";
    }

    case TypeId::Quatd: {
      const double* v = value.as_double4();
      if (!v) return "None";
      return "(" + FormatDouble(v[0], opts.double_precision) + ", " +
             FormatDouble(v[1], opts.double_precision) + ", " +
             FormatDouble(v[2], opts.double_precision) + ", " +
             FormatDouble(v[3], opts.double_precision) + ")";
    }

    case TypeId::Matrix2f: {
      const float* v = value.as_matrix2f();
      if (!v) return "None";
      std::ostringstream ss;
      ss << "((" << FormatFloat(v[0], opts.float_precision) << ", "
         << FormatFloat(v[1], opts.float_precision) << "), ("
         << FormatFloat(v[2], opts.float_precision) << ", "
         << FormatFloat(v[3], opts.float_precision) << "))";
      return ss.str();
    }

    case TypeId::Matrix3f: {
      const float* v = value.as_matrix3f();
      if (!v) return "None";
      std::ostringstream ss;
      ss << "((";
      for (int row = 0; row < 3; ++row) {
        if (row > 0) ss << "), (";
        for (int col = 0; col < 3; ++col) {
          if (col > 0) ss << ", ";
          ss << FormatFloat(v[row * 3 + col], opts.float_precision);
        }
      }
      ss << "))";
      return ss.str();
    }

    case TypeId::Matrix4f: {
      const float* v = value.as_matrix4f();
      if (!v) return "None";
      std::ostringstream ss;
      ss << "((";
      for (int row = 0; row < 4; ++row) {
        if (row > 0) ss << "), (";
        for (int col = 0; col < 4; ++col) {
          if (col > 0) ss << ", ";
          ss << FormatFloat(v[row * 4 + col], opts.float_precision);
        }
      }
      ss << "))";
      return ss.str();
    }

    case TypeId::Matrix2d: {
      const double* v = value.as_matrix2d();
      if (!v) return "None";
      std::ostringstream ss;
      ss << "((" << FormatDouble(v[0], opts.double_precision) << ", "
         << FormatDouble(v[1], opts.double_precision) << "), ("
         << FormatDouble(v[2], opts.double_precision) << ", "
         << FormatDouble(v[3], opts.double_precision) << "))";
      return ss.str();
    }

    case TypeId::Matrix3d: {
      const double* v = value.as_matrix3d();
      if (!v) return "None";
      std::ostringstream ss;
      ss << "((";
      for (int row = 0; row < 3; ++row) {
        if (row > 0) ss << "), (";
        for (int col = 0; col < 3; ++col) {
          if (col > 0) ss << ", ";
          ss << FormatDouble(v[row * 3 + col], opts.double_precision);
        }
      }
      ss << "))";
      return ss.str();
    }

    case TypeId::Matrix4d: {
      const double* v = value.as_matrix4d();
      if (!v) return "None";
      std::ostringstream ss;
      ss << "((";
      for (int row = 0; row < 4; ++row) {
        if (row > 0) ss << "), (";
        for (int col = 0; col < 4; ++col) {
          if (col > 0) ss << ", ";
          ss << FormatDouble(v[row * 4 + col], opts.double_precision);
        }
      }
      ss << "))";
      return ss.str();
    }

    default:
      return "<unsupported type " + std::to_string(static_cast<int>(type_id)) + ">";
  }
}

std::string PrintTypeName(TypeId type_id, bool is_array) {
  const char* type_name = GetTypeName(type_id);
  std::string name = (type_name && type_name[0] != '\0') ? type_name : "unknown";
  if (is_array) {
    name += "[]";
  }
  return name;
}

std::string PrintAttributeValue(const std::string& type_name, const std::string& attr_name,
                                 const Value& value, const PrintOptions& opts) {
  std::ostringstream ss;
  ss << type_name;
  if (value.is_array()) {
    ss << "[]";
  }
  ss << " " << attr_name << " = " << PrintValue(value, opts);
  return ss.str();
}

}  // namespace next
}  // namespace tinyusdz
