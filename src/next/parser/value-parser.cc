// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value Parser implementation

#include "value-parser.hh"
#include "lexer.hh"
#include "../crate/crate-format.hh"
#include "../types/type-info.hh"
#include "../../external/fast_float/include/fast_float/fast_float.h"

#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <limits>
#include <system_error>
#include <unordered_map>
#include <vector>

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

const std::unordered_map<std::string, TypeId>& GetTypeNameMap() {
  static std::unordered_map<std::string, TypeId> map;
  static bool initialized = false;
  if (!initialized) {
    for (const auto& entry : kTypeNames) {
      map[entry.name] = entry.id;
    }
    initialized = true;
  }
  return map;
}

// ============================================================
// Value parsing functions
// ============================================================

ParseResult ParseBool(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type == TokenType::True) {
    lexer.next();
    return ParseResult::Ok(Value(true));
  } else if (tok.type == TokenType::False) {
    lexer.next();
    return ParseResult::Ok(Value(false));
  } else if (tok.type == TokenType::Number) {
    lexer.next();
    return ParseResult::Ok(Value(std::strtod(tok.value.c_str(), nullptr) != 0.0));
  }
  return ParseResult::Error("Expected boolean value");
}

ParseResult ParseInt(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number) {
    return ParseResult::Error("Expected integer value");
  }
  lexer.next();
  int32_t value = static_cast<int32_t>(std::strtol(tok.value.c_str(), nullptr, 0));
  return ParseResult::Ok(Value(value));
}

ParseResult ParseUInt(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number) {
    return ParseResult::Error("Expected unsigned integer value");
  }
  lexer.next();
  uint32_t value = static_cast<uint32_t>(std::strtoul(tok.value.c_str(), nullptr, 0));
  return ParseResult::Ok(Value(value));
}

ParseResult ParseInt64(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number) {
    return ParseResult::Error("Expected 64-bit integer value");
  }
  lexer.next();
  int64_t value = std::strtoll(tok.value.c_str(), nullptr, 0);
  return ParseResult::Ok(Value(value));
}

ParseResult ParseUInt64(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number) {
    return ParseResult::Error("Expected 64-bit unsigned integer value");
  }
  lexer.next();
  uint64_t value = std::strtoull(tok.value.c_str(), nullptr, 0);
  return ParseResult::Ok(Value(value));
}

// Accept the USD special float literals inf / -inf / +inf / infinity / nan.
// The lexer emits the signed forms as a Number token (via scan_number) and the
// bare forms (inf / infinity / nan) as an Identifier token, so a numeric-value
// context accepts both while an attribute literally named `inf` (always read in
// identifier context) is never misclassified. strtof/strtod parse all of these.
bool IsFloatSpecialWord(const std::string& v) {
  std::string s = v;
  if (!s.empty() && (s[0] == '+' || s[0] == '-')) s.erase(s.begin());
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s == "inf" || s == "infinity" || s == "nan";
}

bool IsNumberToken(const Token& tok) {
  return tok.type == TokenType::Number ||
         (tok.type == TokenType::Identifier && IsFloatSpecialWord(tok.value));
}

ParseResult ParseFloat(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (!IsNumberToken(tok)) {
    return ParseResult::Error("Expected float value");
  }
  lexer.next();
  float value = std::strtof(tok.value.c_str(), nullptr);
  return ParseResult::Ok(Value(value));
}

ParseResult ParseDouble(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (!IsNumberToken(tok)) {
    return ParseResult::Error("Expected double value");
  }
  lexer.next();
  double value = std::strtod(tok.value.c_str(), nullptr);
  return ParseResult::Ok(Value(value));
}

ParseResult ParseString(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::String) {
    return ParseResult::Error("Expected string value");
  }
  lexer.next();
  return ParseResult::Ok(Value(tok.value));
}

ParseResult ParseToken(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::String && tok.type != TokenType::Identifier) {
    return ParseResult::Error("Expected token value");
  }
  lexer.next();
  return ParseResult::Ok(Value::MakeToken(tok.value));
}

ParseResult ParseAssetPath(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::String) {
    return ParseResult::Error("Expected asset path value");
  }
  lexer.next();
  return ParseResult::Ok(Value::MakeAssetPath(tok.value));
}

// Parse a tuple of N float values: (x, y, z, ...)
bool ParseFloatTuple(Lexer& lexer, float* out, size_t count) {
  if (!lexer.expect(TokenType::OpenParen)) return false;

  for (size_t i = 0; i < count; i++) {
    const Token& tok = lexer.peek();
    if (!IsNumberToken(tok)) {
      lexer.set_error("Expected number in tuple");
      return false;
    }
    out[i] = std::strtof(tok.value.c_str(), nullptr);
    lexer.next();

    if (i < count - 1) {
      if (!lexer.expect(TokenType::Comma)) return false;
    }
  }

  if (!lexer.expect(TokenType::CloseParen)) return false;
  return true;
}

// Parse a tuple of N double values
bool ParseDoubleTuple(Lexer& lexer, double* out, size_t count) {
  if (!lexer.expect(TokenType::OpenParen)) return false;

  for (size_t i = 0; i < count; i++) {
    const Token& tok = lexer.peek();
    if (!IsNumberToken(tok)) {
      lexer.set_error("Expected number in tuple");
      return false;
    }
    out[i] = std::strtod(tok.value.c_str(), nullptr);
    lexer.next();

    if (i < count - 1) {
      if (!lexer.expect(TokenType::Comma)) return false;
    }
  }

  if (!lexer.expect(TokenType::CloseParen)) return false;
  return true;
}

// Parse a tuple of N int values
bool ParseIntTuple(Lexer& lexer, int32_t* out, size_t count) {
  if (!lexer.expect(TokenType::OpenParen)) return false;

  for (size_t i = 0; i < count; i++) {
    const Token& tok = lexer.peek();
    if (tok.type != TokenType::Number) {
      lexer.set_error("Expected integer in tuple");
      return false;
    }
    out[i] = static_cast<int32_t>(std::strtol(tok.value.c_str(), nullptr, 0));
    lexer.next();

    if (i < count - 1) {
      if (!lexer.expect(TokenType::Comma)) return false;
    }
  }

  if (!lexer.expect(TokenType::CloseParen)) return false;
  return true;
}

bool ParseUIntTuple(Lexer& lexer, uint32_t* out, size_t count) {
  if (!lexer.expect(TokenType::OpenParen)) return false;

  for (size_t i = 0; i < count; i++) {
    const Token& tok = lexer.peek();
    if (tok.type != TokenType::Number) {
      lexer.set_error("Expected unsigned integer in tuple");
      return false;
    }
    if (!tok.value.empty() && tok.value[0] == '-') {
      lexer.set_error("Expected non-negative unsigned integer in tuple");
      return false;
    }
    out[i] = static_cast<uint32_t>(std::strtoul(tok.value.c_str(), nullptr, 0));
    lexer.next();

    if (i < count - 1) {
      if (!lexer.expect(TokenType::Comma)) return false;
    }
  }

  if (!lexer.expect(TokenType::CloseParen)) return false;
  return true;
}

ParseResult ParseFloat2(Lexer& lexer) {
  float data[2];
  if (!ParseFloatTuple(lexer, data, 2)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeFloat2(data[0], data[1]));
}

ParseResult ParseFloat3(Lexer& lexer) {
  float data[3];
  if (!ParseFloatTuple(lexer, data, 3)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeFloat3(data[0], data[1], data[2]));
}

ParseResult ParseFloat4(Lexer& lexer) {
  float data[4];
  if (!ParseFloatTuple(lexer, data, 4)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeFloat4(data[0], data[1], data[2], data[3]));
}

ParseResult ParseHalf(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (!IsNumberToken(tok)) {
    return ParseResult::Error("Expected half value");
  }
  lexer.next();
  uint16_t bits = FloatToHalf(std::strtof(tok.value.c_str(), nullptr));
  return ParseResult::Ok(Value::MakeFromRaw(TypeId::Half, &bits));
}

ParseResult ParseHalfTuple(Lexer& lexer, TypeId type_id, size_t count) {
  float data[4] = {};
  if (!ParseFloatTuple(lexer, data, count)) {
    return ParseResult::Error(lexer.error());
  }

  uint16_t bits[4] = {};
  for (size_t i = 0; i < count; i++) {
    bits[i] = FloatToHalf(data[i]);
  }
  return ParseResult::Ok(Value::MakeFromRaw(type_id, bits));
}

ParseResult ParseHalf2(Lexer& lexer) {
  return ParseHalfTuple(lexer, TypeId::Half2, 2);
}

ParseResult ParseHalf3(Lexer& lexer) {
  return ParseHalfTuple(lexer, TypeId::Half3, 3);
}

ParseResult ParseHalf4(Lexer& lexer) {
  return ParseHalfTuple(lexer, TypeId::Half4, 4);
}

ParseResult ParseQuath(Lexer& lexer) {
  return ParseHalfTuple(lexer, TypeId::Quath, 4);
}

ParseResult ParseDouble2(Lexer& lexer) {
  double data[2];
  if (!ParseDoubleTuple(lexer, data, 2)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeDouble2(data[0], data[1]));
}

ParseResult ParseDouble3(Lexer& lexer) {
  double data[3];
  if (!ParseDoubleTuple(lexer, data, 3)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeDouble3(data[0], data[1], data[2]));
}

ParseResult ParseDouble4(Lexer& lexer) {
  double data[4];
  if (!ParseDoubleTuple(lexer, data, 4)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeDouble4(data[0], data[1], data[2], data[3]));
}

ParseResult ParseInt2(Lexer& lexer) {
  int32_t data[2];
  if (!ParseIntTuple(lexer, data, 2)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeInt2(data[0], data[1]));
}

ParseResult ParseInt3(Lexer& lexer) {
  int32_t data[3];
  if (!ParseIntTuple(lexer, data, 3)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeInt3(data[0], data[1], data[2]));
}

ParseResult ParseInt4(Lexer& lexer) {
  int32_t data[4];
  if (!ParseIntTuple(lexer, data, 4)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeInt4(data[0], data[1], data[2], data[3]));
}

ParseResult ParseUIntN(Lexer& lexer, TypeId type_id, size_t count) {
  uint32_t data[4] = {};
  if (!ParseUIntTuple(lexer, data, count)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeFromRaw(type_id, data));
}

ParseResult ParseUInt2(Lexer& lexer) {
  return ParseUIntN(lexer, TypeId::UInt2, 2);
}

ParseResult ParseUInt3(Lexer& lexer) {
  return ParseUIntN(lexer, TypeId::UInt3, 3);
}

ParseResult ParseUInt4(Lexer& lexer) {
  return ParseUIntN(lexer, TypeId::UInt4, 4);
}

ParseResult ParseQuatf(Lexer& lexer) {
  float data[4];
  if (!ParseFloatTuple(lexer, data, 4)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeQuatf(data[0], data[1], data[2], data[3]));
}

ParseResult ParseQuatd(Lexer& lexer) {
  double data[4];
  if (!ParseDoubleTuple(lexer, data, 4)) {
    return ParseResult::Error(lexer.error());
  }
  return ParseResult::Ok(Value::MakeQuatd(data[0], data[1], data[2], data[3]));
}

// Parse matrix as nested tuples: ((r0c0, r0c1, ...), (r1c0, r1c1, ...), ...)
template <size_t N>
ParseResult ParseMatrixFloatN(Lexer& lexer, TypeId type_id) {
  float data[N * N];

  if (!lexer.expect(TokenType::OpenParen)) {
    return ParseResult::Error(lexer.error());
  }

  for (size_t row = 0; row < N; row++) {
    float row_data[N];
    if (!ParseFloatTuple(lexer, row_data, N)) {
      return ParseResult::Error(lexer.error());
    }
    for (size_t col = 0; col < N; col++) {
      data[row * N + col] = row_data[col];
    }
    if (row + 1 < N && !lexer.expect(TokenType::Comma)) {
      return ParseResult::Error(lexer.error());
    }
  }

  if (!lexer.expect(TokenType::CloseParen)) {
    return ParseResult::Error(lexer.error());
  }

  if (type_id == TypeId::Matrix2f) return ParseResult::Ok(Value::MakeMatrix2f(data));
  if (type_id == TypeId::Matrix3f) return ParseResult::Ok(Value::MakeMatrix3f(data));
  return ParseResult::Ok(Value::MakeMatrix4f(data));
}

template <size_t N>
ParseResult ParseMatrixDoubleN(Lexer& lexer, TypeId type_id) {
  double data[N * N];

  if (!lexer.expect(TokenType::OpenParen)) {
    return ParseResult::Error(lexer.error());
  }

  for (size_t row = 0; row < N; row++) {
    double row_data[N];
    if (!ParseDoubleTuple(lexer, row_data, N)) {
      return ParseResult::Error(lexer.error());
    }
    for (size_t col = 0; col < N; col++) {
      data[row * N + col] = row_data[col];
    }
    if (row + 1 < N && !lexer.expect(TokenType::Comma)) {
      return ParseResult::Error(lexer.error());
    }
  }

  if (!lexer.expect(TokenType::CloseParen)) {
    return ParseResult::Error(lexer.error());
  }

  if (type_id == TypeId::Matrix2d) return ParseResult::Ok(Value::MakeMatrix2d(data));
  if (type_id == TypeId::Matrix3d) return ParseResult::Ok(Value::MakeMatrix3d(data));
  return ParseResult::Ok(Value::MakeMatrix4d(data));
}

ParseResult ParseMatrix2f(Lexer& lexer) {
  return ParseMatrixFloatN<2>(lexer, TypeId::Matrix2f);
}

ParseResult ParseMatrix3f(Lexer& lexer) {
  return ParseMatrixFloatN<3>(lexer, TypeId::Matrix3f);
}

ParseResult ParseMatrix4f(Lexer& lexer) {
  return ParseMatrixFloatN<4>(lexer, TypeId::Matrix4f);
}

ParseResult ParseMatrix2d(Lexer& lexer) {
  return ParseMatrixDoubleN<2>(lexer, TypeId::Matrix2d);
}

ParseResult ParseMatrix4d(Lexer& lexer) {
  return ParseMatrixDoubleN<4>(lexer, TypeId::Matrix4d);
}

ParseResult ParseMatrix3d(Lexer& lexer) {
  return ParseMatrixDoubleN<3>(lexer, TypeId::Matrix3d);
}

// ============================================================
// Parse function table
// ============================================================

using ParseFn = ParseResult (*)(Lexer&);

ParseFn GetParseFunction(TypeId type_id) {
  switch (type_id) {
    case TypeId::Bool: return &ParseBool;
    case TypeId::Int: return &ParseInt;
    case TypeId::UInt: return &ParseUInt;
    case TypeId::Int64: return &ParseInt64;
    case TypeId::UInt64: return &ParseUInt64;
    case TypeId::Half: return &ParseHalf;
    case TypeId::Float: return &ParseFloat;
    case TypeId::Double: return &ParseDouble;
    case TypeId::String: return &ParseString;
    case TypeId::Token: return &ParseToken;
    case TypeId::AssetPath: return &ParseAssetPath;
    case TypeId::Int2: return &ParseInt2;
    case TypeId::Int3: return &ParseInt3;
    case TypeId::Int4: return &ParseInt4;
    case TypeId::UInt2: return &ParseUInt2;
    case TypeId::UInt3: return &ParseUInt3;
    case TypeId::UInt4: return &ParseUInt4;
    case TypeId::Half2: return &ParseHalf2;
    case TypeId::Half3: return &ParseHalf3;
    case TypeId::Half4: return &ParseHalf4;
    case TypeId::Float2: return &ParseFloat2;
    case TypeId::Float3: return &ParseFloat3;
    case TypeId::Float4: return &ParseFloat4;
    case TypeId::Double2: return &ParseDouble2;
    case TypeId::Double3: return &ParseDouble3;
    case TypeId::Double4: return &ParseDouble4;
    case TypeId::Quath: return &ParseQuath;
    case TypeId::Quatf: return &ParseQuatf;
    case TypeId::Quatd: return &ParseQuatd;
    case TypeId::Matrix2f: return &ParseMatrix2f;
    case TypeId::Matrix3f: return &ParseMatrix3f;
    case TypeId::Matrix4f: return &ParseMatrix4f;
    case TypeId::Matrix2d: return &ParseMatrix2d;
    case TypeId::Matrix3d: return &ParseMatrix3d;
    case TypeId::Matrix4d: return &ParseMatrix4d;

    // Semantic types that share storage with vectors
    case TypeId::Point3h: return &ParseHalf3;
    case TypeId::Point3f: return &ParseFloat3;
    case TypeId::Point3d: return &ParseDouble3;
    case TypeId::Vector3h: return &ParseHalf3;
    case TypeId::Vector3f: return &ParseFloat3;
    case TypeId::Vector3d: return &ParseDouble3;
    case TypeId::Normal3h: return &ParseHalf3;
    case TypeId::Normal3f: return &ParseFloat3;
    case TypeId::Normal3d: return &ParseDouble3;
    case TypeId::Color3h: return &ParseHalf3;
    case TypeId::Color3f: return &ParseFloat3;
    case TypeId::Color3d: return &ParseDouble3;
    case TypeId::Color4h: return &ParseHalf4;
    case TypeId::Color4f: return &ParseFloat4;
    case TypeId::Color4d: return &ParseDouble4;
    case TypeId::Texcoord2h: return &ParseHalf2;
    case TypeId::Texcoord2f: return &ParseFloat2;
    case TypeId::Texcoord2d: return &ParseDouble2;
    case TypeId::Texcoord3h: return &ParseHalf3;
    case TypeId::Texcoord3f: return &ParseFloat3;
    case TypeId::Texcoord3d: return &ParseDouble3;

    case TypeId::TimeCode: return &ParseDouble;

    default:
      return nullptr;
  }
}

}  // anonymous namespace

// ============================================================
// Public API
// ============================================================

ParseResult ParseValue(Lexer& lexer, TypeId expected_type) {
  // Handle None
  if (lexer.peek().type == TokenType::None) {
    lexer.next();
    return ParseResult::Ok(Value());
  }

  // Dictionaries have a dedicated recursive parser (no flat ParseFn entry).
  if (expected_type == TypeId::Dictionary ||
      lexer.peek().type == TokenType::OpenBrace) {
    return ParseDict(lexer);
  }

  ParseFn fn = GetParseFunction(expected_type);
  if (!fn) {
    // GetTypeName returns nullptr for ids without TypeInfo (e.g. semantic ids
    // a malformed file maps onto) — std::string(nullptr) is UB/abort.
    const char* tn = GetTypeName(expected_type);
    return ParseResult::Error("No parser for type " +
                              (tn ? std::string(tn)
                                  : "#" + std::to_string(int(expected_type))));
  }

  ParseResult result = fn(lexer);

  // If parsing succeeded but type has a semantic distinction, fix the type ID
  if (result.success) {
    // For semantic types, the parse function returns the base type
    // We need to update to the actual requested type
    switch (expected_type) {
      case TypeId::Point3f:
      case TypeId::Point3h:
      case TypeId::Vector3f:
      case TypeId::Vector3h:
      case TypeId::Normal3f:
      case TypeId::Normal3h:
      case TypeId::Color3f:
      case TypeId::Color3h:
      case TypeId::Point3d:
      case TypeId::Vector3d:
      case TypeId::Normal3d:
      case TypeId::Color3d:
      case TypeId::Color4f:
      case TypeId::Color4h:
      case TypeId::Color4d:
      case TypeId::Texcoord2f:
      case TypeId::Texcoord2h:
      case TypeId::Texcoord2d:
      case TypeId::Texcoord3f:
      case TypeId::Texcoord3h:
      case TypeId::Texcoord3d:
        result.value = Value::MakeFromRaw(expected_type, result.value.raw_data());
        break;
      default:
        break;
    }
  }

  return result;
}

// Helper to check if type stores float3-like data
bool IsFloat3Like(TypeId type) {
  return type == TypeId::Float3 || type == TypeId::Half3 ||
         type == TypeId::Point3h || type == TypeId::Point3f ||
         type == TypeId::Vector3h || type == TypeId::Vector3f ||
         type == TypeId::Normal3h || type == TypeId::Normal3f ||
         type == TypeId::Color3h || type == TypeId::Color3f ||
         type == TypeId::Texcoord3h || type == TypeId::Texcoord3f;
}

// Helper to check if type stores float2-like data
bool IsFloat2Like(TypeId type) {
  return type == TypeId::Float2 || type == TypeId::Half2 ||
         type == TypeId::Texcoord2h || type == TypeId::Texcoord2f;
}

// Helper to check if type stores float4-like data
bool IsFloat4Like(TypeId type) {
  return type == TypeId::Float4 || type == TypeId::Half4 ||
         type == TypeId::Quath || type == TypeId::Quatf ||
         type == TypeId::Color4h || type == TypeId::Color4f;
}

bool IsDouble2Like(TypeId type) {
  return type == TypeId::Double2 || type == TypeId::Texcoord2d;
}

bool IsDouble3Like(TypeId type) {
  return type == TypeId::Double3 || type == TypeId::Point3d ||
         type == TypeId::Vector3d || type == TypeId::Normal3d ||
         type == TypeId::Color3d || type == TypeId::Texcoord3d;
}

bool IsDouble4Like(TypeId type) {
  return type == TypeId::Double4 || type == TypeId::Quatd ||
         type == TypeId::Color4d;
}

bool SliceIsIdentifierStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool SliceIsIdentifierContinue(char c) {
  return SliceIsIdentifierStart(c) || (c >= '0' && c <= '9');
}

struct SliceParser {
  const char* p{nullptr};
  const char* end{nullptr};

  void skip_ws() {
    while (p < end) {
      if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
      } else if (*p == '#') {
        while (p < end && *p != '\n') ++p;
      } else {
        break;
      }
    }
  }

  bool consume(char c) {
    skip_ws();
    if (p >= end || *p != c) return false;
    ++p;
    return true;
  }

  bool maybe_consume(char c) {
    skip_ws();
    if (p < end && *p == c) {
      ++p;
      return true;
    }
    return false;
  }

  bool at_array_end() {
    skip_ws();
    return p < end && *p == ']';
  }

  bool finish_array() {
    if (!consume(']')) return false;
    skip_ws();
    return p == end;
  }

  bool parse_float(float* out) {
    skip_ws();
    auto r = fast_float::from_chars(p, end, *out);
    if (r.ec != std::errc{} || r.ptr == p) return false;
    p = r.ptr;
    return true;
  }

  bool parse_double(double* out) {
    skip_ws();
    auto r = fast_float::from_chars(p, end, *out);
    if (r.ec != std::errc{} || r.ptr == p) return false;
    p = r.ptr;
    return true;
  }

  bool parse_i64(int64_t* out) {
    skip_ws();
    if (p >= end) return false;
    errno = 0;
    char* next = nullptr;
    long long v = std::strtoll(p, &next, 0);
    if (next == p || errno == ERANGE) return false;
    p = next;
    *out = static_cast<int64_t>(v);
    return true;
  }

  bool parse_u64(uint64_t* out) {
    skip_ws();
    if (p >= end || *p == '-') return false;
    errno = 0;
    char* next = nullptr;
    unsigned long long v = std::strtoull(p, &next, 0);
    if (next == p || errno == ERANGE) return false;
    p = next;
    *out = static_cast<uint64_t>(v);
    return true;
  }

  bool parse_bool(bool* out) {
    skip_ws();
    if (p + 4 <= end && std::memcmp(p, "true", 4) == 0 &&
        (p + 4 == end || !SliceIsIdentifierContinue(*(p + 4)))) {
      p += 4;
      *out = true;
      return true;
    }
    if (p + 5 <= end && std::memcmp(p, "false", 5) == 0 &&
        (p + 5 == end || !SliceIsIdentifierContinue(*(p + 5)))) {
      p += 5;
      *out = false;
      return true;
    }
    double v = 0.0;
    if (!parse_double(&v)) return false;
    *out = (v != 0.0);
    return true;
  }

  bool parse_string(std::string* out) {
    skip_ws();
    if (p >= end || (*p != '"' && *p != '\'')) return false;
    const char quote = *p;
    ++p;
    bool triple = false;
    if (p + 1 < end && p[0] == quote && p[1] == quote) {
      triple = true;
      p += 2;
    }
    out->clear();
    while (p < end) {
      char c = *p++;
      if (triple) {
        if (c == quote && p + 1 < end && p[0] == quote && p[1] == quote) {
          p += 2;
          return true;
        }
      } else if (c == quote) {
        return true;
      }
      if (c == '\\' && p < end) {
        char esc = *p++;
        switch (esc) {
          case 'n': out->push_back('\n'); break;
          case 'r': out->push_back('\r'); break;
          case 't': out->push_back('\t'); break;
          case '"': out->push_back('"'); break;
          case '\'': out->push_back('\''); break;
          case '\\': out->push_back('\\'); break;
          default: out->push_back(esc); break;
        }
      } else {
        out->push_back(c);
      }
    }
    return false;
  }

  bool parse_identifier(std::string* out) {
    skip_ws();
    if (p >= end || !SliceIsIdentifierStart(*p)) return false;
    const char* start = p;
    ++p;
    while (p < end && SliceIsIdentifierContinue(*p)) ++p;
    out->assign(start, size_t(p - start));
    return true;
  }

  bool parse_token(std::string* out) {
    return parse_string(out) || parse_identifier(out);
  }
};

template <class T, class ParseOne>
bool ParseScalarArray(SliceParser* sp, std::vector<T>* out, ParseOne parse_one) {
  if (!sp->consume('[')) return false;
  out->clear();
  if (sp->at_array_end()) return sp->finish_array();
  while (true) {
    T v{};
    if (!parse_one(sp, &v)) return false;
    out->push_back(v);
    if (sp->maybe_consume(',')) continue;
    return sp->finish_array();
  }
}

template <class ScalarT, uint32_t N, class ParseOne>
bool ParseTupleArray(SliceParser* sp, std::vector<ScalarT>* out,
                     ParseOne parse_one) {
  if (!sp->consume('[')) return false;
  out->clear();
  if (sp->at_array_end()) return sp->finish_array();
  while (true) {
    if (!sp->consume('(')) return false;
    for (uint32_t i = 0; i < N; i++) {
      ScalarT v{};
      if (!parse_one(sp, &v)) return false;
      out->push_back(v);
      if (i + 1 < N && !sp->consume(',')) return false;
    }
    if (!sp->consume(')')) return false;
    if (sp->maybe_consume(',')) continue;
    return sp->finish_array();
  }
}

template <class ScalarT, uint32_t N, class ParseOne>
bool ParseMatrixArray(SliceParser* sp, std::vector<ScalarT>* out,
                      ParseOne parse_one) {
  if (!sp->consume('[')) return false;
  out->clear();
  if (sp->at_array_end()) return sp->finish_array();
  while (true) {
    if (!sp->consume('(')) return false;
    for (uint32_t r = 0; r < N; r++) {
      if (!sp->consume('(')) return false;
      for (uint32_t c = 0; c < N; c++) {
        ScalarT v{};
        if (!parse_one(sp, &v)) return false;
        out->push_back(v);
        if (c + 1 < N && !sp->consume(',')) return false;
      }
      if (!sp->consume(')')) return false;
      if (r + 1 < N && !sp->consume(',')) return false;
    }
    if (!sp->consume(')')) return false;
    if (sp->maybe_consume(',')) continue;
    return sp->finish_array();
  }
}

ParseResult ParseArrayValueOptimized(Lexer& lexer, TypeId element_type) {
  const char* data = nullptr;
  size_t len = 0;
  if (!lexer.capture_bracketed_literal(&data, &len)) {
    return ParseResult::Error(lexer.error());
  }
  SliceParser sp{data, data + len};

  auto parse_float = [](SliceParser* s, float* v) { return s->parse_float(v); };
  auto parse_double = [](SliceParser* s, double* v) { return s->parse_double(v); };

  if (element_type == TypeId::Float) {
    std::vector<float> values;
    if (!ParseScalarArray<float>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse float array");
    }
    return ParseResult::Ok(Value::MakeFloatArray(std::move(values)));
  }
  if (element_type == TypeId::Double) {
    std::vector<double> values;
    if (!ParseScalarArray<double>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse double array");
    }
    return ParseResult::Ok(Value::MakeDoubleArray(std::move(values)));
  }
  if (element_type == TypeId::Half) {
    std::vector<float> values;
    if (!ParseScalarArray<float>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse half array");
    }
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 1));
  }
  if (element_type == TypeId::Bool) {
    std::vector<bool> values;
    auto parse_bool = [](SliceParser* s, bool* v) { return s->parse_bool(v); };
    if (!ParseScalarArray<bool>(&sp, &values, parse_bool)) {
      return ParseResult::Error("Failed to parse bool array");
    }
    return ParseResult::Ok(Value::MakeBoolArray(values));
  }
  if (element_type == TypeId::Int) {
    std::vector<int32_t> values;
    auto parse_i32 = [](SliceParser* s, int32_t* v) {
      int64_t tmp = 0;
      if (!s->parse_i64(&tmp) || tmp < std::numeric_limits<int32_t>::min() ||
          tmp > std::numeric_limits<int32_t>::max()) return false;
      *v = static_cast<int32_t>(tmp);
      return true;
    };
    if (!ParseScalarArray<int32_t>(&sp, &values, parse_i32)) {
      return ParseResult::Error("Failed to parse int array");
    }
    return ParseResult::Ok(Value::MakeIntArray(std::move(values)));
  }
  if (element_type == TypeId::UInt) {
    std::vector<uint32_t> values;
    auto parse_u32 = [](SliceParser* s, uint32_t* v) {
      uint64_t tmp = 0;
      if (!s->parse_u64(&tmp) || tmp > std::numeric_limits<uint32_t>::max()) return false;
      *v = static_cast<uint32_t>(tmp);
      return true;
    };
    if (!ParseScalarArray<uint32_t>(&sp, &values, parse_u32)) {
      return ParseResult::Error("Failed to parse uint array");
    }
    return ParseResult::Ok(Value::MakeUIntArray(std::move(values)));
  }
  if (element_type == TypeId::Int64) {
    std::vector<int64_t> values;
    auto parse_i64 = [](SliceParser* s, int64_t* v) { return s->parse_i64(v); };
    if (!ParseScalarArray<int64_t>(&sp, &values, parse_i64)) {
      return ParseResult::Error("Failed to parse int64 array");
    }
    return ParseResult::Ok(Value::MakeInt64Array(std::move(values)));
  }
  if (element_type == TypeId::UInt64) {
    std::vector<uint64_t> values;
    auto parse_u64 = [](SliceParser* s, uint64_t* v) { return s->parse_u64(v); };
    if (!ParseScalarArray<uint64_t>(&sp, &values, parse_u64)) {
      return ParseResult::Error("Failed to parse uint64 array");
    }
    return ParseResult::Ok(Value::MakeUInt64Array(std::move(values)));
  }
  if (element_type == TypeId::Token || element_type == TypeId::String) {
    std::vector<std::string> values;
    auto parse_stringish = [](SliceParser* s, std::string* v) {
      return s->parse_token(v);
    };
    auto parse_string = [](SliceParser* s, std::string* v) {
      return s->parse_string(v);
    };
    if (!ParseScalarArray<std::string>(&sp, &values,
                                       element_type == TypeId::Token
                                           ? parse_stringish
                                           : parse_string)) {
      return ParseResult::Error("Failed to parse string/token array");
    }
    return ParseResult::Ok(Value::MakeTokenArray(std::move(values)));
  }

  if (IsFloat2Like(element_type)) {
    std::vector<float> values;
    if (!ParseTupleArray<float, 2>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse float2 array");
    }
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 2));
  }
  if (IsFloat3Like(element_type)) {
    std::vector<float> values;
    if (!ParseTupleArray<float, 3>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse float3 array");
    }
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 3));
  }
  if (IsFloat4Like(element_type)) {
    std::vector<float> values;
    if (!ParseTupleArray<float, 4>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse float4 array");
    }
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 4));
  }
  if (IsDouble2Like(element_type)) {
    std::vector<double> values;
    if (!ParseTupleArray<double, 2>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse double2 array");
    }
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 2));
  }
  if (IsDouble3Like(element_type)) {
    std::vector<double> values;
    if (!ParseTupleArray<double, 3>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse double3 array");
    }
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 3));
  }
  if (IsDouble4Like(element_type)) {
    std::vector<double> values;
    if (!ParseTupleArray<double, 4>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse double4 array");
    }
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 4));
  }
  if (element_type == TypeId::Matrix2f) {
    std::vector<float> values;
    if (!ParseMatrixArray<float, 2>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse matrix2f array");
    }
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 4));
  }
  if (element_type == TypeId::Matrix3f) {
    std::vector<float> values;
    if (!ParseMatrixArray<float, 3>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse matrix3f array");
    }
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 9));
  }
  if (element_type == TypeId::Matrix4f) {
    std::vector<float> values;
    if (!ParseMatrixArray<float, 4>(&sp, &values, parse_float)) {
      return ParseResult::Error("Failed to parse matrix4f array");
    }
    return ParseResult::Ok(Value::MakeFloatCompArray(std::move(values), element_type, 16));
  }
  if (element_type == TypeId::Matrix2d) {
    std::vector<double> values;
    if (!ParseMatrixArray<double, 2>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse matrix2d array");
    }
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 4));
  }
  if (element_type == TypeId::Matrix3d) {
    std::vector<double> values;
    if (!ParseMatrixArray<double, 3>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse matrix3d array");
    }
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 9));
  }
  if (element_type == TypeId::Matrix4d) {
    std::vector<double> values;
    if (!ParseMatrixArray<double, 4>(&sp, &values, parse_double)) {
      return ParseResult::Error("Failed to parse matrix4d array");
    }
    return ParseResult::Ok(Value::MakeDoubleCompArray(std::move(values), element_type, 16));
  }

  return ParseResult::Error("No optimized array parser for type");
}

ParseResult ParseArrayValue(Lexer& lexer, TypeId element_type) {
  // USD allows array-valued attributes to author `None` as a blocked/default
  // value. Treat it the same way scalar parsing does: consume it and return an
  // empty Value placeholder so the parser can keep progressing.
  if (lexer.peek().type == TokenType::None) {
    lexer.next();
    return ParseResult::Ok(Value());
  }

  switch (element_type) {
    case TypeId::Float:
    case TypeId::Double:
    case TypeId::Int:
    case TypeId::UInt:
    case TypeId::Int64:
    case TypeId::UInt64:
    case TypeId::Bool:
    case TypeId::Half:
    case TypeId::Half2:
    case TypeId::Half3:
    case TypeId::Half4:
    case TypeId::Float2:
    case TypeId::Float3:
    case TypeId::Float4:
    case TypeId::Point3h:
    case TypeId::Point3f:
    case TypeId::Vector3h:
    case TypeId::Vector3f:
    case TypeId::Normal3h:
    case TypeId::Normal3f:
    case TypeId::Color3h:
    case TypeId::Color3f:
    case TypeId::Color4h:
    case TypeId::Color4f:
    case TypeId::Texcoord2h:
    case TypeId::Texcoord2f:
    case TypeId::Texcoord3h:
    case TypeId::Texcoord3f:
    case TypeId::Quath:
    case TypeId::Quatf:
    case TypeId::Double2:
    case TypeId::Double3:
    case TypeId::Double4:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d:
    case TypeId::Color3d:
    case TypeId::Color4d:
    case TypeId::Texcoord2d:
    case TypeId::Texcoord3d:
    case TypeId::Quatd:
    case TypeId::Matrix2f:
    case TypeId::Matrix3f:
    case TypeId::Matrix4f:
    case TypeId::Matrix2d:
    case TypeId::Matrix3d:
    case TypeId::Matrix4d:
    // Token/String arrays use the same bracket-literal fast path; the slice
    // parser handles quotes, triple-quotes and escapes and returns a real
    // token array (see ParseArrayValueOptimized).
    case TypeId::Token:
    case TypeId::String:
      return ParseArrayValueOptimized(lexer, element_type);
    default:
      break;
  }

  if (!lexer.expect(TokenType::OpenBracket)) {
    return ParseResult::Error(lexer.error());
  }

  // Handle empty array
  if (lexer.peek().type == TokenType::CloseBracket) {
    lexer.next();
    // Return appropriate empty array based on element type
    if (element_type == TypeId::Float || IsFloat3Like(element_type) ||
        IsFloat2Like(element_type) || IsFloat4Like(element_type)) {
      return ParseResult::Ok(Value::MakeFloatArray(std::vector<float>{}));
    } else if (element_type == TypeId::Int) {
      return ParseResult::Ok(Value::MakeIntArray(std::vector<int32_t>{}));
    } else if (element_type == TypeId::Token || element_type == TypeId::String) {
      return ParseResult::Ok(Value::MakeTokenArray(std::vector<std::string>{}));
    }
    return ParseResult::Ok(Value::MakeFloatArray(std::vector<float>{}));
  }

  // Parse elements
  std::vector<float> float_data;
  std::vector<int32_t> int_data;
  std::vector<std::string> string_data;

  while (true) {
    ParseResult elem = ParseValue(lexer, element_type);
    if (!elem.success) {
      return elem;
    }

    // Collect data based on type
    if (element_type == TypeId::Float) {
      if (const float* f = elem.value.as_float()) {
        float_data.push_back(*f);
      }
    } else if (element_type == TypeId::Int) {
      if (const int32_t* i = elem.value.as_int()) {
        int_data.push_back(*i);
      }
    } else if (IsFloat3Like(element_type)) {
      if (const float* f = elem.value.as_float3()) {
        float_data.push_back(f[0]);
        float_data.push_back(f[1]);
        float_data.push_back(f[2]);
      }
    } else if (IsFloat2Like(element_type)) {
      if (const float* f = elem.value.as_float2()) {
        float_data.push_back(f[0]);
        float_data.push_back(f[1]);
      }
    } else if (IsFloat4Like(element_type)) {
      if (const float* f = elem.value.as_float4()) {
        float_data.push_back(f[0]);
        float_data.push_back(f[1]);
        float_data.push_back(f[2]);
        float_data.push_back(f[3]);
      }
    } else if (element_type == TypeId::Token) {
      if (const std::string* s = elem.value.as_token()) {
        string_data.push_back(*s);
      }
    } else if (element_type == TypeId::String) {
      if (const std::string* s = elem.value.as_string()) {
        string_data.push_back(*s);
      }
    }

    // Check for comma or end
    if (lexer.peek().type == TokenType::Comma) {
      lexer.next();
    } else if (lexer.peek().type == TokenType::CloseBracket) {
      lexer.next();
      break;
    } else {
      return ParseResult::Error("Expected ',' or ']' in array");
    }
  }

  // Return appropriate array type
  if (element_type == TypeId::Float) {
    return ParseResult::Ok(Value::MakeFloatArray(std::move(float_data)));
  } else if (element_type == TypeId::Int) {
    return ParseResult::Ok(Value::MakeIntArray(std::move(int_data)));
  } else if (IsFloat3Like(element_type)) {
    return ParseResult::Ok(Value::MakeFloat3Array(std::move(float_data)));
  } else if (IsFloat2Like(element_type) || IsFloat4Like(element_type)) {
    return ParseResult::Ok(Value::MakeFloatArray(std::move(float_data)));
  } else if (element_type == TypeId::Token || element_type == TypeId::String) {
    return ParseResult::Ok(Value::MakeTokenArray(std::move(string_data)));
  }

  // Default: try to return float array
  return ParseResult::Ok(Value::MakeFloatArray(std::move(float_data)));
}

ParseResult ParseGenericValue(Lexer& lexer, TypeId& out_type) {
  const Token& tok = lexer.peek();

  if (tok.type == TokenType::True || tok.type == TokenType::False) {
    out_type = TypeId::Bool;
    return ParseBool(lexer);
  }

  if (tok.type == TokenType::Number) {
    // Try to determine if it's int or float
    if (tok.value.find('.') != std::string::npos ||
        tok.value.find('e') != std::string::npos ||
        tok.value.find('E') != std::string::npos) {
      out_type = TypeId::Double;
      return ParseDouble(lexer);
    } else {
      out_type = TypeId::Int;
      return ParseInt(lexer);
    }
  }

  if (tok.type == TokenType::String) {
    out_type = TypeId::String;
    return ParseString(lexer);
  }

  if (tok.type == TokenType::OpenParen) {
    // Could be a tuple - peek ahead to determine size
    // For now, assume float3 as default
    out_type = TypeId::Float3;
    return ParseFloat3(lexer);
  }

  if (tok.type == TokenType::None) {
    out_type = TypeId::Invalid;
    lexer.next();
    return ParseResult::Ok(Value());
  }

  out_type = TypeId::Invalid;
  return ParseResult::Error("Cannot infer type from token");
}

// Recursive dictionary body parser. Grammar (USD):
//   { [<typeName>] <key> = <value>   ... }
// where <key> is an identifier or quoted string, <value> is a scalar/array
// literal or a nested `{ ... }`. The type name is optional (inferred if absent).
static ParseResult ParseDictDepth(Lexer& lexer, int depth) {
  if (depth > 64) return ParseResult::Error("dictionary nesting too deep");
  if (!lexer.expect(TokenType::OpenBrace)) {
    return ParseResult::Error("Expected '{' to start dictionary");
  }

  Value v = Value::MakeDictionary();
  Dict* d = v.as_dictionary();

  while (lexer.peek().type != TokenType::CloseBrace &&
         lexer.peek().type != TokenType::Eof) {
    std::string type_name, key;
    bool have_type = false;

    const Token& t = lexer.peek();
    if (t.type == TokenType::String) {
      key = t.value;
      lexer.next();
    } else if (t.type == TokenType::Identifier) {
      std::string first = t.value;
      lexer.next();
      const Token& t2 = lexer.peek();
      if (t2.type == TokenType::Identifier || t2.type == TokenType::String ||
          t2.type == TokenType::OpenBracket) {
        // `first` was the type name; the key (after an optional []) follows.
        type_name = first;
        have_type = true;
        if (lexer.peek().type == TokenType::OpenBracket) {
          lexer.next();
          lexer.expect(TokenType::CloseBracket);
          type_name += "[]";
        }
        const Token& kt = lexer.peek();
        if (kt.type == TokenType::String || kt.type == TokenType::Identifier) {
          key = kt.value;
          lexer.next();
        } else {
          return ParseResult::Error("Expected dictionary key after type name");
        }
      } else {
        key = first;  // bare key, no type name
      }
    } else {
      lexer.next();  // unexpected token: skip to avoid an infinite loop
      continue;
    }

    if (!lexer.expect(TokenType::Equals)) {
      return ParseResult::Error("Expected '=' in dictionary entry");
    }

    ParseResult val;
    if (lexer.peek().type == TokenType::OpenBrace) {
      val = ParseDictDepth(lexer, depth + 1);
    } else if (have_type) {
      bool is_array = false;
      TypeId tid = ParseTypeName(type_name, is_array);
      if (tid == TypeId::Invalid) {
        TypeId inferred;
        val = ParseGenericValue(lexer, inferred);
      } else if (is_array) {
        val = ParseArrayValue(lexer, tid);
      } else {
        val = ParseValue(lexer, tid);
      }
    } else {
      TypeId inferred;
      val = ParseGenericValue(lexer, inferred);
    }
    if (!val.success) return val;
    d->set(std::move(key), std::move(val.value));

    if (lexer.peek().type == TokenType::Comma) lexer.next();
  }

  if (!lexer.expect(TokenType::CloseBrace)) {
    return ParseResult::Error("Expected '}' to close dictionary");
  }
  return ParseResult::Ok(std::move(v));
}

ParseResult ParseDict(Lexer& lexer) { return ParseDictDepth(lexer, 0); }

TypeId ParseTypeName(const std::string& type_name, bool& is_array) {
  std::string base_name = type_name;
  is_array = false;

  // Check for array suffix
  if (type_name.size() > 2 && type_name.substr(type_name.size() - 2) == "[]") {
    base_name = type_name.substr(0, type_name.size() - 2);
    is_array = true;
  }

  const auto& map = GetTypeNameMap();
  auto it = map.find(base_name);
  if (it != map.end()) {
    return it->second;
  }

  return TypeId::Invalid;
}

}  // namespace next
}  // namespace tinyusdz
