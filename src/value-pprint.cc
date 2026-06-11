// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "value-pprint.hh"

#include <sstream>

#include "pprinter.hh"
#include "str-util.hh"
#include "value-types.hh"
// NOTE: usdGeom.hh/usdLux.hh/core/prim.hh were only needed by pprint_value's
// schema-prim arms, now in value-pprint-dispatch.cc. The operator<< and
// to_string definitions here are value-type-only.

//
#include "common-macros.inc"

// For fast int/float to ascii
// Default disabled.
//#define TINYUSDZ_LOCAL_USE_JEAIII_ITOA

#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
#include "external/jeaiii_to_text.h"
#endif

// NOTE: Using dragonbox-based dtos() from str-util.hh for all float/double
// conversions - it produces shortest representation for 100% of values.

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace {

inline void append_float_to_stream(std::ostream &os, float v) {
  char buf[tinyusdz::DTOS_MAX_CHARS_FLOAT];
  size_t len = tinyusdz::dtos(v, buf);
  os.write(buf, static_cast<std::streamsize>(len));
}

}  // namespace

namespace tinyusdz {
namespace pprint {

std::string format_wrapped_array(const std::vector<std::string> &elements,
                                 uint32_t prefix_cols, uint32_t column_limit) {
  if (elements.empty()) {
    return "[]";
  }

  // Check if single-line fits
  size_t total_len = 2;  // "[" and "]"
  for (size_t i = 0; i < elements.size(); i++) {
    total_len += elements[i].size();
    if (i > 0) total_len += 2;  // ", "
  }

  if (prefix_cols + total_len <= column_limit) {
    // Fits on one line
    std::string result = "[";
    for (size_t i = 0; i < elements.size(); i++) {
      if (i > 0) result += ", ";
      result += elements[i];
    }
    result += "]";
    return result;
  }

  // Need wrapping. Compute continuation indent (column after '[').
  uint32_t cont_indent = prefix_cols + 1;
  bool deep_indent = false;

  // If too deep (>60% of column limit), fall back to newline + single indent
  if (cont_indent > column_limit * 3 / 5) {
    deep_indent = true;
    // Single indent width (default 4 spaces)
    cont_indent = static_cast<uint32_t>(Indent(1).size());
  }

  std::string result;
  uint32_t cur_col;

  if (deep_indent) {
    // Start array on next line with single indentation
    result = "\n";
    result += Indent(1);
    result += "[";
    cur_col = cont_indent + 1;
  } else {
    result = "[";
    cur_col = prefix_cols + 1;
  }

  for (size_t i = 0; i < elements.size(); i++) {
    const auto &elem = elements[i];
    // Width needed: element itself + trailing ", " (except for last element)
    uint32_t elem_width = static_cast<uint32_t>(elem.size());

    if (i > 0) {
      // Check if this element fits on the current line
      // +2 for the ", " separator before it
      if (cur_col + 2 + elem_width > column_limit) {
        // Wrap: emit comma at end of previous line, then newline
        result += ",\n";
        result += std::string(cont_indent, ' ');
        cur_col = cont_indent;
      } else {
        result += ", ";
        cur_col += 2;
      }
    }

    result += elem;
    cur_col += elem_width;
  }

  result += "]";
  return result;
}

// Non-template 1D-array emitter: takes already-stringified elements so the
// templated array operator<< (std::vector<T>/TypedArray<T>/ChunkedTypedArray<T>)
// stay thin (just the type-dependent stringify loop) instead of each re-emitting
// the wrap-vs-inline join logic. `wrappable` selects column-wrapping (numeric/
// geometric element types) vs a plain "[a, b, c]" join.
void print_1d_array(std::ostream &os, const std::vector<std::string> &elems,
                    bool wrappable) {
  if (wrappable) {
    uint32_t col_limit = GetColumnLimit();
    if (col_limit > 0 && elems.size() > 1) {
      os << format_wrapped_array(elems, GetPrefixColumns(), col_limit);
      return;
    }
  }
  os << "[";
  for (size_t i = 0; i < elems.size(); i++) {
    if (i > 0) os << ", ";
    os << elems[i];
  }
  os << "]";
}

}  // namespace pprint
}  // namespace tinyusdz

namespace std {

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half &v) {
  // Use direct half-precision dtos for shortest representation
  os << tinyusdz::dtos(v);
  return os;
}

// Note: operator<< for StringData is defined in pprinter.cc

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

// treat char vector type as byte
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::char2 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::char3 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ", " << int(v[2]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::char4 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ", " << int(v[2]) << ", "
     << int(v[3]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uchar2 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uchar3 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ", " << int(v[2]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uchar4 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ", " << int(v[2]) << ", "
     << int(v[3]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::short2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::short3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::short4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::ushort2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::ushort3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::ushort4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float2 &v) {
  char buffer[tinyusdz::PRINT_FLOAT2_MAX_CHARS];
  size_t len = tinyusdz::print_float2(v, buffer);
  os.write(buffer, static_cast<std::streamsize>(len));
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float3 &v) {
  char buffer[tinyusdz::PRINT_FLOAT3_MAX_CHARS];
  size_t len = tinyusdz::print_float3(v, buffer);
  os.write(buffer, static_cast<std::streamsize>(len));
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float4 &v) {
  char buffer[tinyusdz::PRINT_FLOAT4_MAX_CHARS];
  size_t len = tinyusdz::print_float4(v, buffer);
  os.write(buffer, static_cast<std::streamsize>(len));
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double2 &v) {
  char buffer[tinyusdz::PRINT_DOUBLE2_MAX_CHARS];
  size_t len = tinyusdz::print_double2(v, buffer);
  os.write(buffer, static_cast<std::streamsize>(len));
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double3 &v) {
  char buffer[tinyusdz::PRINT_DOUBLE3_MAX_CHARS];
  size_t len = tinyusdz::print_double3(v, buffer);
  os.write(buffer, static_cast<std::streamsize>(len));
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double4 &v) {
  char buffer[tinyusdz::PRINT_DOUBLE4_MAX_CHARS];
  size_t len = tinyusdz::print_double4(v, buffer);
  os.write(buffer, static_cast<std::streamsize>(len));
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3f &v) {
  os << "(";
  append_float_to_stream(os, v.x);
  os << ", ";
  append_float_to_stream(os, v.y);
  os << ", ";
  append_float_to_stream(os, v.z);
  os << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3d &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3f &v) {
  os << "(";
  append_float_to_stream(os, v.x);
  os << ", ";
  append_float_to_stream(os, v.y);
  os << ", ";
  append_float_to_stream(os, v.z);
  os << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3d &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3f &v) {
  os << "(";
  append_float_to_stream(os, v.x);
  os << ", ";
  append_float_to_stream(os, v.y);
  os << ", ";
  append_float_to_stream(os, v.z);
  os << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3d &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3h &v) {
  os << "(" << tinyusdz::value::half_to_float(v.r) << ", "
     << tinyusdz::value::half_to_float(v.g) << ", "
     << tinyusdz::value::half_to_float(v.b) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3f &v) {
  os << "(";
  append_float_to_stream(os, v.r);
  os << ", ";
  append_float_to_stream(os, v.g);
  os << ", ";
  append_float_to_stream(os, v.b);
  os << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3d &v) {
  os << "(" << tinyusdz::dtos(v.r) << ", " << tinyusdz::dtos(v.g) << ", "
     << tinyusdz::dtos(v.b) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4h &v) {
  os << "(" << tinyusdz::value::half_to_float(v.r) << ", "
     << tinyusdz::value::half_to_float(v.g) << ", "
     << tinyusdz::value::half_to_float(v.b) << ", "
     << tinyusdz::value::half_to_float(v.a) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4f &v) {
  os << "(";
  append_float_to_stream(os, v.r);
  os << ", ";
  append_float_to_stream(os, v.g);
  os << ", ";
  append_float_to_stream(os, v.b);
  os << ", ";
  append_float_to_stream(os, v.a);
  os << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4d &v) {
  os << "(" << tinyusdz::dtos(v.r) << ", " << tinyusdz::dtos(v.g) << ", "
     << tinyusdz::dtos(v.b) << ", " << tinyusdz::dtos(v.a) << ")";
  return os;
}

// pxrUSD prints quateron in [w, x, y, z] order
// https://github.com/PixarAnimationStudios/USD/blob/3abc46452b1271df7650e9948fef9f0ce602e3b2/pxr/base/gf/quatf.h#L287
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quath &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quatf &v) {
  os << "(" << tinyusdz::dtos(v.real) << ", " << tinyusdz::dtos(v.imag[0])
     << ", " << tinyusdz::dtos(v.imag[1]) << ", " << tinyusdz::dtos(v.imag[2])
     << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quatd &v) {
  os << "(" << tinyusdz::dtos(v.real) << ", " << tinyusdz::dtos(v.imag[0])
     << ", " << tinyusdz::dtos(v.imag[1]) << ", " << tinyusdz::dtos(v.imag[2])
     << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2h &v) {
  os << "(" << v.s << ", " << v.t << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2f &v) {
  os << "(";
  append_float_to_stream(os, v.s);
  os << ", ";
  append_float_to_stream(os, v.t);
  os << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2d &v) {
  os << "(" << tinyusdz::dtos(v.s) << ", " << tinyusdz::dtos(v.t) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3h &v) {
  os << "(" << v.s << ", " << v.t << ", " << v.r << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3f &v) {
  os << "(";
  append_float_to_stream(os, v.s);
  os << ", ";
  append_float_to_stream(os, v.t);
  os << ", ";
  append_float_to_stream(os, v.r);
  os << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3d &v) {
  os << "(" << tinyusdz::dtos(v.s) << ", " << tinyusdz::dtos(v.t) << ", "
     << tinyusdz::dtos(v.r) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix2f &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix3f &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << ", " << tinyusdz::dtos(m.m[0][2]) << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ", " << tinyusdz::dtos(m.m[1][2]) << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[2][0]) << ", " << tinyusdz::dtos(m.m[2][1])
      << ", " << tinyusdz::dtos(m.m[2][2]) << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix4f &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << ", " << tinyusdz::dtos(m.m[0][2]) << ", " << tinyusdz::dtos(m.m[0][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ", " << tinyusdz::dtos(m.m[1][2]) << ", " << tinyusdz::dtos(m.m[1][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[2][0]) << ", " << tinyusdz::dtos(m.m[2][1])
      << ", " << tinyusdz::dtos(m.m[2][2]) << ", " << tinyusdz::dtos(m.m[2][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[3][0]) << ", " << tinyusdz::dtos(m.m[3][1])
      << ", " << tinyusdz::dtos(m.m[3][2]) << ", " << tinyusdz::dtos(m.m[3][3])
      << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix2d &m) {
  char buffer[tinyusdz::PRINT_MATRIX2D_MAX_CHARS];
  size_t len = tinyusdz::print_matrix2d(m, buffer);
  ofs.write(buffer, static_cast<std::streamsize>(len));
  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix3d &m) {
  char buffer[tinyusdz::PRINT_MATRIX3D_MAX_CHARS];
  size_t len = tinyusdz::print_matrix3d(m, buffer);
  ofs.write(buffer, static_cast<std::streamsize>(len));
  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix4d &m) {
  char buffer[tinyusdz::PRINT_MATRIX4D_MAX_CHARS];
  size_t len = tinyusdz::print_matrix4d(m, buffer);
  ofs.write(buffer, static_cast<std::streamsize>(len));
  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::frame4d &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << ", " << tinyusdz::dtos(m.m[0][2]) << ", " << tinyusdz::dtos(m.m[0][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ", " << tinyusdz::dtos(m.m[1][2]) << ", " << tinyusdz::dtos(m.m[1][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[2][0]) << ", " << tinyusdz::dtos(m.m[2][1])
      << ", " << tinyusdz::dtos(m.m[2][2]) << ", " << tinyusdz::dtos(m.m[2][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[3][0]) << ", " << tinyusdz::dtos(m.m[3][1])
      << ", " << tinyusdz::dtos(m.m[3][2]) << ", " << tinyusdz::dtos(m.m[3][3])
      << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::timecode &tc) {
  ofs << tinyusdz::dtos(tc.value);
  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::token &tok) {
  ofs << tinyusdz::quote(tok.str());

  return ofs;
}


std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::AssetPath &asset) {
  std::string in_s = asset.GetAssetPath();

  if (in_s.empty()) {
    ofs << "@@";
  } else {
    std::string quote_str = "@";

    std::string s;

    if (tinyusdz::contains(in_s, '@')) {
      // Escape '@@@'(to '\@@@') if the input path contains '@@@'
      for (size_t i = 0; i < in_s.length(); i++) {
        if ((i + 2) < in_s.length() &&
            in_s[i] == '@' && in_s[i + 1] == '@' && in_s[i + 2] == '@') {
          s += "\\@@@";
          i += 2;
        } else {
          s += in_s[i];
        }
      }

      quote_str = "@@@";
    } else {
      s = in_s;
    }

    // Do not escape backslash for asset path
    ofs << quote_str << s << quote_str;
  }

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<double> &v) {
  uint32_t col_limit = tinyusdz::pprint::GetColumnLimit();
  if (col_limit > 0 && v.size() > 1) {
    std::vector<std::string> elems;
    elems.reserve(v.size());
    for (const auto &e : v) {
      elems.push_back(tinyusdz::dtos(e));
    }
    ofs << tinyusdz::pprint::format_wrapped_array(
        elems, tinyusdz::pprint::GetPrefixColumns(), col_limit);
    return ofs;
  }
  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    ofs << tinyusdz::dtos(v[i]);
  }
  ofs << "]";

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<float> &v) {
  uint32_t col_limit = tinyusdz::pprint::GetColumnLimit();
  if (col_limit > 0 && v.size() > 1) {
    std::vector<std::string> elems;
    elems.reserve(v.size());
    for (const auto &e : v) {
      elems.push_back(tinyusdz::dtos(e));
    }
    ofs << tinyusdz::pprint::format_wrapped_array(
        elems, tinyusdz::pprint::GetPrefixColumns(), col_limit);
    return ofs;
  }
  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    ofs << tinyusdz::dtos(v[i]);
  }
  ofs << "]";

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<int32_t> &v) {
  uint32_t col_limit = tinyusdz::pprint::GetColumnLimit();
  if (col_limit > 0 && v.size() > 1) {
    std::vector<std::string> elems;
    elems.reserve(v.size());
    for (const auto &e : v) {
      std::ostringstream ess;
      ess << e;
      elems.push_back(ess.str());
    }
    ofs << tinyusdz::pprint::format_wrapped_array(
        elems, tinyusdz::pprint::GetPrefixColumns(), col_limit);
    return ofs;
  }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
  // numeric_limits<uint64_t>::digits10 is 19, so 32 should suffice.
  char buf[32];
#endif

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
    tinyusdz::itoa(v[i], buf);
    ofs << buf;
#else
    ofs << v[i];
#endif
  }
  ofs << "]";

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<uint32_t> &v) {
  uint32_t col_limit = tinyusdz::pprint::GetColumnLimit();
  if (col_limit > 0 && v.size() > 1) {
    std::vector<std::string> elems;
    elems.reserve(v.size());
    for (const auto &e : v) {
      std::ostringstream ess;
      ess << e;
      elems.push_back(ess.str());
    }
    ofs << tinyusdz::pprint::format_wrapped_array(
        elems, tinyusdz::pprint::GetPrefixColumns(), col_limit);
    return ofs;
  }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
  char buf[32];
#endif

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
    tinyusdz::itoa(v[i], buf);
    ofs << buf;
#else
    ofs << v[i];
#endif
  }
  ofs << "]";

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<uint8_t> &v) {
  // uchar[]: print each element as an integer (os << uint8_t would emit a char).
  uint32_t col_limit = tinyusdz::pprint::GetColumnLimit();
  if (col_limit > 0 && v.size() > 1) {
    std::vector<std::string> elems;
    elems.reserve(v.size());
    for (const auto &e : v) {
      std::ostringstream ess;
      ess << static_cast<unsigned int>(e);
      elems.push_back(ess.str());
    }
    ofs << tinyusdz::pprint::format_wrapped_array(
        elems, tinyusdz::pprint::GetPrefixColumns(), col_limit);
    return ofs;
  }
  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    ofs << static_cast<unsigned int>(v[i]);
  }
  ofs << "]";
  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<int64_t> &v) {
  uint32_t col_limit = tinyusdz::pprint::GetColumnLimit();
  if (col_limit > 0 && v.size() > 1) {
    std::vector<std::string> elems;
    elems.reserve(v.size());
    for (const auto &e : v) {
      std::ostringstream ess;
      ess << e;
      elems.push_back(ess.str());
    }
    ofs << tinyusdz::pprint::format_wrapped_array(
        elems, tinyusdz::pprint::GetPrefixColumns(), col_limit);
    return ofs;
  }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
  // numeric_limits<uint64_t>::digits10 is 19, so 32 should suffice.
  char buf[32];
#endif

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
    tinyusdz::itoa(v[i], buf);
    ofs << buf;
#else
    ofs << v[i];
#endif
  }
  ofs << "]";

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<uint64_t> &v) {
  uint32_t col_limit = tinyusdz::pprint::GetColumnLimit();
  if (col_limit > 0 && v.size() > 1) {
    std::vector<std::string> elems;
    elems.reserve(v.size());
    for (const auto &e : v) {
      std::ostringstream ess;
      ess << e;
      elems.push_back(ess.str());
    }
    ofs << tinyusdz::pprint::format_wrapped_array(
        elems, tinyusdz::pprint::GetPrefixColumns(), col_limit);
    return ofs;
  }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
  char buf[32];
#endif

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
    tinyusdz::itoa(v[i], buf);
    ofs << buf;
#else
    ofs << v[i];
#endif
  }
  ofs << "]";

  return ofs;
}

}  // namespace std

namespace tinyusdz {

std::string to_string(bool v) {
  if (v) {
    return "true";
  } else {
    return "false";
  }
}

std::string to_string(int32_t v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(uint32_t v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(int64_t v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(uint64_t v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const tinyusdz::value::half &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const tinyusdz::value::half2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const tinyusdz::value::half3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const tinyusdz::value::half4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const value::char2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::char3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::char4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::short2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::short3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::short4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::int2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::int3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::int4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::uint2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::uint3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::uint4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::float2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::float3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::float4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::double2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::double3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::double4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord2h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord2f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord2d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord3h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::StringData &v) {
  // Use buildEscapedAndQuotedStringForUSDA directly for proper quoting
  return buildEscapedAndQuotedStringForUSDA(v.value);
}
std::string to_string(const value::token &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const std::string &s) {
  // TODO: Escape `"` character.

  // Escape backslash
  return quote(escapeBackslash(s));
}
std::string to_string(const value::quath &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::quatf &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::quatd &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix2f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix4f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix2d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix4d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::frame4d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::normal3h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::normal3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::normal3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::vector3h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::vector3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::vector3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::point3h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::point3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::point3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color4h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color4f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color4d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

}  // namespace tinyusdz
