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

// Forward decl: recursive dictionary printer (defined after PrintValue).
std::string PrintDictionaryIndented(const Dict& d, const PrintOptions& opts,
                                    int base_depth);

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
    const size_t maxN = opts.max_array_elements;

    const bool is_matrix =
        (type_id == TypeId::Matrix2f || type_id == TypeId::Matrix2d ||
         type_id == TypeId::Matrix3f || type_id == TypeId::Matrix3d ||
         type_id == TypeId::Matrix4f || type_id == TypeId::Matrix4d);
    const size_t mat_dim =
        (type_id == TypeId::Matrix2f || type_id == TypeId::Matrix2d) ? 2 :
        (type_id == TypeId::Matrix3f || type_id == TypeId::Matrix3d) ? 3 : 4;
    size_t comp_count = GetComponentCount(type_id);
    if (comp_count < 1) comp_count = 1;

    // Emit a single element (scalar, vector tuple, or nested matrix rows).
    auto emit_elem_float = [&](const float* d) {
      if (is_matrix) {
        ss << "(";
        for (size_t r = 0; r < mat_dim; ++r) {
          if (r) ss << ", ";
          ss << "(";
          for (size_t c = 0; c < mat_dim; ++c) {
            if (c) ss << ", ";
            ss << FormatFloat(d[r * mat_dim + c], opts.float_precision);
          }
          ss << ")";
        }
        ss << ")";
      } else if (comp_count > 1) {
        ss << "(";
        for (size_t c = 0; c < comp_count; ++c) {
          if (c) ss << ", ";
          ss << FormatFloat(d[c], opts.float_precision);
        }
        ss << ")";
      } else {
        ss << FormatFloat(d[0], opts.float_precision);
      }
    };
    auto emit_elem_double = [&](const double* d) {
      if (is_matrix) {
        ss << "(";
        for (size_t r = 0; r < mat_dim; ++r) {
          if (r) ss << ", ";
          ss << "(";
          for (size_t c = 0; c < mat_dim; ++c) {
            if (c) ss << ", ";
            ss << FormatDouble(d[r * mat_dim + c], opts.double_precision);
          }
          ss << ")";
        }
        ss << ")";
      } else if (comp_count > 1) {
        ss << "(";
        for (size_t c = 0; c < comp_count; ++c) {
          if (c) ss << ", ";
          ss << FormatDouble(d[c], opts.double_precision);
        }
        ss << ")";
      } else {
        ss << FormatDouble(d[0], opts.double_precision);
      }
    };

    switch (type_id) {
      case TypeId::Int: {
        if (const auto* a = value.as_int_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          for (size_t i = 0; i < limit; ++i) { if (i) ss << ", "; ss << (*a)[i]; }
          if (limit < a->size()) ss << ", ...";
        }
        break;
      }
      case TypeId::UInt: {
        if (const auto* a = value.as_uint_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          for (size_t i = 0; i < limit; ++i) { if (i) ss << ", "; ss << (*a)[i]; }
          if (limit < a->size()) ss << ", ...";
        }
        break;
      }
      case TypeId::Int64: {
        if (const auto* a = value.as_int64_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          for (size_t i = 0; i < limit; ++i) { if (i) ss << ", "; ss << (*a)[i]; }
          if (limit < a->size()) ss << ", ...";
        }
        break;
      }
      case TypeId::UInt64: {
        if (const auto* a = value.as_uint64_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          for (size_t i = 0; i < limit; ++i) { if (i) ss << ", "; ss << (*a)[i]; }
          if (limit < a->size()) ss << ", ...";
        }
        break;
      }
      case TypeId::Bool: {
        if (const auto* a = value.as_bool_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          for (size_t i = 0; i < limit; ++i) { if (i) ss << ", "; ss << ((*a)[i] ? "true" : "false"); }
          if (limit < a->size()) ss << ", ...";
        }
        break;
      }
      case TypeId::Token:
      case TypeId::String:
      case TypeId::AssetPath: {
        if (const auto* a = value.as_token_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          for (size_t i = 0; i < limit; ++i) {
            if (i) ss << ", ";
            if (type_id == TypeId::AssetPath) ss << "@" << (*a)[i] << "@";
            else ss << EscapeString((*a)[i]);
          }
          if (limit < a->size()) ss << ", ...";
        }
        break;
      }
      default: {
        // float- or double-backed scalar / vector / matrix arrays
        const bool dbl = (GetComponentType(type_id) == TypeId::Double) ||
                         type_id == TypeId::Double;
        if (dbl) {
          if (const auto* a = value.as_double_array()) {
            size_t n = a->size() / comp_count;
            size_t limit = (maxN > 0) ? std::min(maxN, n) : n;
            for (size_t i = 0; i < limit; ++i) { if (i) ss << ", "; emit_elem_double(a->data() + i * comp_count); }
            if (limit < n) ss << ", ...";
          }
        } else {
          if (const auto* a = value.as_float_array()) {
            size_t n = a->size() / comp_count;
            size_t limit = (maxN > 0) ? std::min(maxN, n) : n;
            for (size_t i = 0; i < limit; ++i) { if (i) ss << ", "; emit_elem_float(a->data() + i * comp_count); }
            if (limit < n) ss << ", ...";
          }
        }
        break;
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

    case TypeId::Dictionary: {
      const Dict* d = value.as_dictionary();
      return d ? PrintDictionaryIndented(*d, opts, 0) : std::string("{\n}");
    }

    default:
      return "<unsupported type " + std::to_string(static_cast<int>(type_id)) + ">";
  }
}

namespace {

std::string PrintDictionaryIndented(const Dict& d, const PrintOptions& opts,
                                    int base_depth) {
  std::ostringstream ss;
  ss << "{\n";
  std::string inner;
  for (int i = 0; i <= base_depth; ++i) inner += opts.indent;
  std::string closing;
  for (int i = 0; i < base_depth; ++i) closing += opts.indent;

  for (const auto& kv : d.entries) {
    const std::string& key = kv.first;
    const Value& val = kv.second;
    ss << inner;
    if (val.is_dictionary()) {
      ss << "dictionary " << key << " = "
         << PrintDictionaryIndented(*val.as_dictionary(), opts, base_depth + 1)
         << "\n";
    } else {
      ss << PrintTypeName(val.type_id(), val.is_array()) << " " << key << " = "
         << PrintValue(val, opts) << "\n";
    }
  }
  ss << closing << "}";
  return ss.str();
}

}  // anonymous namespace

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
