// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value Printer Implementation

#include "value-printer.hh"
#include "../types/type-id.hh"
#include "../strfmt.hh"
#include "dtoa.hh"
#include <algorithm>
#include <cmath>

namespace tinyusdz {
namespace next {

namespace {

// Append helpers: format straight into a reused std::string with no per-element
// heap allocation. Byte-identical to the corresponding `ss << v` (classic
// locale: plain decimal, leading '-' for negatives, no '+'/padding/separators).
inline void AppendFloat(std::string& o, float v) { dtos_append(o, v); }
inline void AppendDouble(std::string& o, double v) { dtos_append(o, v); }

// Integer append helpers live in ../strfmt.hh (AppendInt/AppendUInt), shared with
// the USDA writer.

// Reserve headroom for an array, but cap the up-front growth so a single huge
// array doesn't trigger a giant mmap. Beyond the cap, std::string's geometric
// growth handles it with cheap amortized reallocations — and, crucially, avoids
// the multi-hundred-MB simultaneous allocations that thrash the allocator when
// many subtrees are serialized in parallel.
inline void ReserveArrayHeadroom(std::string& o, size_t want) {
  constexpr size_t kCap = 8u << 20;  // 8 MiB
  o.reserve(o.size() + std::min<size_t>(want, kCap) + 8);
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

void PrintValueInto(std::string& out, const Value& value,
                    const PrintOptions& opts) {
  if (value.is_empty()) {
    out += "None";
    return;
  }

  TypeId type_id = value.type_id();

  // Handle arrays
  if (value.is_array()) {
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

    // Reserve a generous estimate (over-reserve is the accepted marginal memory)
    // so large numeric arrays append without reallocation churn.
    out += "[";

    // Emit a single element (scalar, vector tuple, or nested matrix rows).
    auto emit_elem_float = [&](const float* d) {
      if (is_matrix) {
        out += "(";
        for (size_t r = 0; r < mat_dim; ++r) {
          if (r) out += ", ";
          out += "(";
          for (size_t c = 0; c < mat_dim; ++c) {
            if (c) out += ", ";
            AppendFloat(out, d[r * mat_dim + c]);
          }
          out += ")";
        }
        out += ")";
      } else if (comp_count > 1) {
        out += "(";
        for (size_t c = 0; c < comp_count; ++c) {
          if (c) out += ", ";
          AppendFloat(out, d[c]);
        }
        out += ")";
      } else {
        AppendFloat(out, d[0]);
      }
    };
    auto emit_elem_double = [&](const double* d) {
      if (is_matrix) {
        out += "(";
        for (size_t r = 0; r < mat_dim; ++r) {
          if (r) out += ", ";
          out += "(";
          for (size_t c = 0; c < mat_dim; ++c) {
            if (c) out += ", ";
            AppendDouble(out, d[r * mat_dim + c]);
          }
          out += ")";
        }
        out += ")";
      } else if (comp_count > 1) {
        out += "(";
        for (size_t c = 0; c < comp_count; ++c) {
          if (c) out += ", ";
          AppendDouble(out, d[c]);
        }
        out += ")";
      } else {
        AppendDouble(out, d[0]);
      }
    };

    switch (type_id) {
      case TypeId::Int: {
        if (const auto* a = value.as_int_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          ReserveArrayHeadroom(out, limit * 8);
          for (size_t i = 0; i < limit; ++i) { if (i) out += ", "; AppendInt(out, (*a)[i]); }
          if (limit < a->size()) out += ", ...";
        }
        break;
      }
      case TypeId::UInt: {
        if (const auto* a = value.as_uint_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          ReserveArrayHeadroom(out, limit * 8);
          for (size_t i = 0; i < limit; ++i) { if (i) out += ", "; AppendUInt(out, (*a)[i]); }
          if (limit < a->size()) out += ", ...";
        }
        break;
      }
      case TypeId::Int64: {
        if (const auto* a = value.as_int64_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          ReserveArrayHeadroom(out, limit * 10);
          for (size_t i = 0; i < limit; ++i) { if (i) out += ", "; AppendInt(out, (*a)[i]); }
          if (limit < a->size()) out += ", ...";
        }
        break;
      }
      case TypeId::UInt64: {
        if (const auto* a = value.as_uint64_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          ReserveArrayHeadroom(out, limit * 10);
          for (size_t i = 0; i < limit; ++i) { if (i) out += ", "; AppendUInt(out, (*a)[i]); }
          if (limit < a->size()) out += ", ...";
        }
        break;
      }
      case TypeId::Bool: {
        if (const auto* a = value.as_bool_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          for (size_t i = 0; i < limit; ++i) { if (i) out += ", "; out += ((*a)[i] ? "true" : "false"); }
          if (limit < a->size()) out += ", ...";
        }
        break;
      }
      case TypeId::Token:
      case TypeId::String:
      case TypeId::AssetPath: {
        if (const auto* a = value.as_token_array()) {
          size_t limit = (maxN > 0) ? std::min(maxN, a->size()) : a->size();
          for (size_t i = 0; i < limit; ++i) {
            if (i) out += ", ";
            if (type_id == TypeId::AssetPath) { out += '@'; out += (*a)[i]; out += '@'; }
            else out += EscapeString((*a)[i]);
          }
          if (limit < a->size()) out += ", ...";
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
            ReserveArrayHeadroom(out, limit * comp_count * 12);
            for (size_t i = 0; i < limit; ++i) { if (i) out += ", "; emit_elem_double(a->data() + i * comp_count); }
            if (limit < n) out += ", ...";
          }
        } else {
          if (const auto* a = value.as_float_array()) {
            size_t n = a->size() / comp_count;
            size_t limit = (maxN > 0) ? std::min(maxN, n) : n;
            ReserveArrayHeadroom(out, limit * comp_count * 12);
            for (size_t i = 0; i < limit; ++i) { if (i) out += ", "; emit_elem_float(a->data() + i * comp_count); }
            if (limit < n) out += ", ...";
          }
        }
        break;
      }
    }

    out += "]";
    return;
  }

  // Handle scalars and vectors
  switch (type_id) {
    case TypeId::Bool: {
      const bool* v = value.as_bool();
      out += v ? (*v ? "true" : "false") : "None";
      return;
    }

    case TypeId::Int: {
      const int32_t* v = value.as_int();
      if (v) AppendInt(out, *v); else out += "None";
      return;
    }

    case TypeId::UInt: {
      const uint32_t* v = value.as_uint();
      if (v) AppendUInt(out, *v); else out += "None";
      return;
    }

    case TypeId::Int64: {
      const int64_t* v = value.as_int64();
      if (v) AppendInt(out, *v); else out += "None";
      return;
    }

    case TypeId::UInt64: {
      const uint64_t* v = value.as_uint64();
      if (v) AppendUInt(out, *v); else out += "None";
      return;
    }

    case TypeId::Float: {
      const float* v = value.as_float();
      if (v) AppendFloat(out, *v); else out += "None";
      return;
    }

    case TypeId::Double: {
      const double* v = value.as_double();
      if (v) AppendDouble(out, *v); else out += "None";
      return;
    }

    case TypeId::String: {
      const std::string* v = value.as_string();
      out += v ? EscapeString(*v) : "None";
      return;
    }

    case TypeId::Token: {
      const std::string* v = value.as_token();
      out += v ? EscapeString(*v) : "None";
      return;
    }

    case TypeId::AssetPath: {
      const std::string* v = value.as_asset_path();
      if (v) { out += '@'; out += *v; out += '@'; } else out += "None";
      return;
    }

    case TypeId::Int2: {
      const int32_t* v = value.as_int2();
      if (!v) { out += "None"; return; }
      out += '('; AppendInt(out, v[0]); out += ", "; AppendInt(out, v[1]); out += ')';
      return;
    }

    case TypeId::Int3: {
      const int32_t* v = value.as_int3();
      if (!v) { out += "None"; return; }
      out += '('; AppendInt(out, v[0]); out += ", "; AppendInt(out, v[1]);
      out += ", "; AppendInt(out, v[2]); out += ')';
      return;
    }

    case TypeId::Int4: {
      const int32_t* v = value.as_int4();
      if (!v) { out += "None"; return; }
      out += '('; AppendInt(out, v[0]); out += ", "; AppendInt(out, v[1]);
      out += ", "; AppendInt(out, v[2]); out += ", "; AppendInt(out, v[3]); out += ')';
      return;
    }

    case TypeId::Float2: {
      const float* v = value.as_float2();
      if (!v) { out += "None"; return; }
      out += '('; AppendFloat(out, v[0]); out += ", "; AppendFloat(out, v[1]); out += ')';
      return;
    }

    case TypeId::Float3:
    case TypeId::Point3f:
    case TypeId::Vector3f:
    case TypeId::Normal3f:
    case TypeId::Color3f: {
      const float* v = value.as_float3();
      if (!v) { out += "None"; return; }
      out += '('; AppendFloat(out, v[0]); out += ", "; AppendFloat(out, v[1]);
      out += ", "; AppendFloat(out, v[2]); out += ')';
      return;
    }

    case TypeId::Float4:
    case TypeId::Color4f: {
      const float* v = value.as_float4();
      if (!v) { out += "None"; return; }
      out += '('; AppendFloat(out, v[0]); out += ", "; AppendFloat(out, v[1]);
      out += ", "; AppendFloat(out, v[2]); out += ", "; AppendFloat(out, v[3]); out += ')';
      return;
    }

    case TypeId::Double2: {
      const double* v = value.as_double2();
      if (!v) { out += "None"; return; }
      out += '('; AppendDouble(out, v[0]); out += ", "; AppendDouble(out, v[1]); out += ')';
      return;
    }

    case TypeId::Double3:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d: {
      const double* v = value.as_double3();
      if (!v) { out += "None"; return; }
      out += '('; AppendDouble(out, v[0]); out += ", "; AppendDouble(out, v[1]);
      out += ", "; AppendDouble(out, v[2]); out += ')';
      return;
    }

    case TypeId::Double4: {
      const double* v = value.as_double4();
      if (!v) { out += "None"; return; }
      out += '('; AppendDouble(out, v[0]); out += ", "; AppendDouble(out, v[1]);
      out += ", "; AppendDouble(out, v[2]); out += ", "; AppendDouble(out, v[3]); out += ')';
      return;
    }

    case TypeId::Quatf: {
      const float* v = value.as_float4();  // Quat stored as 4 floats
      if (!v) { out += "None"; return; }
      out += '('; AppendFloat(out, v[0]); out += ", "; AppendFloat(out, v[1]);
      out += ", "; AppendFloat(out, v[2]); out += ", "; AppendFloat(out, v[3]); out += ')';
      return;
    }

    case TypeId::Quatd: {
      const double* v = value.as_double4();
      if (!v) { out += "None"; return; }
      out += '('; AppendDouble(out, v[0]); out += ", "; AppendDouble(out, v[1]);
      out += ", "; AppendDouble(out, v[2]); out += ", "; AppendDouble(out, v[3]); out += ')';
      return;
    }

    case TypeId::Matrix2f: {
      const float* v = value.as_matrix2f();
      if (!v) { out += "None"; return; }
      out += "(("; AppendFloat(out, v[0]); out += ", "; AppendFloat(out, v[1]);
      out += "), ("; AppendFloat(out, v[2]); out += ", "; AppendFloat(out, v[3]); out += "))";
      return;
    }

    case TypeId::Matrix3f: {
      const float* v = value.as_matrix3f();
      if (!v) { out += "None"; return; }
      out += "((";
      for (int row = 0; row < 3; ++row) {
        if (row > 0) out += "), (";
        for (int col = 0; col < 3; ++col) {
          if (col > 0) out += ", ";
          AppendFloat(out, v[row * 3 + col]);
        }
      }
      out += "))";
      return;
    }

    case TypeId::Matrix4f: {
      const float* v = value.as_matrix4f();
      if (!v) { out += "None"; return; }
      out += "((";
      for (int row = 0; row < 4; ++row) {
        if (row > 0) out += "), (";
        for (int col = 0; col < 4; ++col) {
          if (col > 0) out += ", ";
          AppendFloat(out, v[row * 4 + col]);
        }
      }
      out += "))";
      return;
    }

    case TypeId::Matrix2d: {
      const double* v = value.as_matrix2d();
      if (!v) { out += "None"; return; }
      out += "(("; AppendDouble(out, v[0]); out += ", "; AppendDouble(out, v[1]);
      out += "), ("; AppendDouble(out, v[2]); out += ", "; AppendDouble(out, v[3]); out += "))";
      return;
    }

    case TypeId::Matrix3d: {
      const double* v = value.as_matrix3d();
      if (!v) { out += "None"; return; }
      out += "((";
      for (int row = 0; row < 3; ++row) {
        if (row > 0) out += "), (";
        for (int col = 0; col < 3; ++col) {
          if (col > 0) out += ", ";
          AppendDouble(out, v[row * 3 + col]);
        }
      }
      out += "))";
      return;
    }

    case TypeId::Matrix4d: {
      const double* v = value.as_matrix4d();
      if (!v) { out += "None"; return; }
      out += "((";
      for (int row = 0; row < 4; ++row) {
        if (row > 0) out += "), (";
        for (int col = 0; col < 4; ++col) {
          if (col > 0) out += ", ";
          AppendDouble(out, v[row * 4 + col]);
        }
      }
      out += "))";
      return;
    }

    case TypeId::Dictionary: {
      const Dict* d = value.as_dictionary();
      if (d) out += PrintDictionaryIndented(*d, opts, 0); else out += "{\n}";
      return;
    }

    default:
      out += "<unsupported type ";
      AppendInt(out, static_cast<int>(type_id));
      out += ">";
      return;
  }
}

std::string PrintValue(const Value& value, const PrintOptions& opts) {
  std::string out;
  PrintValueInto(out, value, opts);
  return out;
}

namespace {

std::string PrintDictionaryIndented(const Dict& d, const PrintOptions& opts,
                                    int base_depth) {
  std::string s;
  s += "{\n";
  std::string inner;
  for (int i = 0; i <= base_depth; ++i) inner += opts.indent;
  std::string closing;
  for (int i = 0; i < base_depth; ++i) closing += opts.indent;

  for (const auto& kv : d.entries) {
    const std::string& key = kv.first;
    const Value& val = kv.second;
    s += inner;
    if (val.is_dictionary()) {
      s += "dictionary ";
      s += key;
      s += " = ";
      s += PrintDictionaryIndented(*val.as_dictionary(), opts, base_depth + 1);
      s += "\n";
    } else {
      s += PrintTypeName(val.type_id(), val.is_array());
      s += " ";
      s += key;
      s += " = ";
      PrintValueInto(s, val, opts);
      s += "\n";
    }
  }
  s += closing;
  s += "}";
  return s;
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
  std::string s;
  s += type_name;
  if (value.is_array()) {
    s += "[]";
  }
  s += " ";
  s += attr_name;
  s += " = ";
  PrintValueInto(s, value, opts);
  return s;
}

}  // namespace next
}  // namespace tinyusdz
