// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value Parser implementation

#include "value-parser.hh"
#include "lexer.hh"
#include "../types/type-info.hh"

#include <cstdlib>
#include <cstring>
#include <unordered_map>

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

ParseResult ParseFloat(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number) {
    return ParseResult::Error("Expected float value");
  }
  lexer.next();
  float value = std::strtof(tok.value.c_str(), nullptr);
  return ParseResult::Ok(Value(value));
}

ParseResult ParseDouble(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number) {
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
    if (tok.type != TokenType::Number) {
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
    if (tok.type != TokenType::Number) {
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
ParseResult ParseMatrix4d(Lexer& lexer) {
  double data[16];

  if (!lexer.expect(TokenType::OpenParen)) {
    return ParseResult::Error(lexer.error());
  }

  for (int row = 0; row < 4; row++) {
    double row_data[4];
    if (!ParseDoubleTuple(lexer, row_data, 4)) {
      return ParseResult::Error(lexer.error());
    }
    for (int col = 0; col < 4; col++) {
      data[row * 4 + col] = row_data[col];
    }
    if (row < 3) {
      if (!lexer.expect(TokenType::Comma)) {
        return ParseResult::Error(lexer.error());
      }
    }
  }

  if (!lexer.expect(TokenType::CloseParen)) {
    return ParseResult::Error(lexer.error());
  }

  return ParseResult::Ok(Value::MakeMatrix4d(data));
}

ParseResult ParseMatrix3d(Lexer& lexer) {
  double data[9];

  if (!lexer.expect(TokenType::OpenParen)) {
    return ParseResult::Error(lexer.error());
  }

  for (int row = 0; row < 3; row++) {
    double row_data[3];
    if (!ParseDoubleTuple(lexer, row_data, 3)) {
      return ParseResult::Error(lexer.error());
    }
    for (int col = 0; col < 3; col++) {
      data[row * 3 + col] = row_data[col];
    }
    if (row < 2) {
      if (!lexer.expect(TokenType::Comma)) {
        return ParseResult::Error(lexer.error());
      }
    }
  }

  if (!lexer.expect(TokenType::CloseParen)) {
    return ParseResult::Error(lexer.error());
  }

  return ParseResult::Ok(Value::MakeMatrix3d(data));
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
    case TypeId::Float: return &ParseFloat;
    case TypeId::Double: return &ParseDouble;
    case TypeId::String: return &ParseString;
    case TypeId::Token: return &ParseToken;
    case TypeId::AssetPath: return &ParseAssetPath;
    case TypeId::Int2: return &ParseInt2;
    case TypeId::Int3: return &ParseInt3;
    case TypeId::Int4: return &ParseInt4;
    case TypeId::Float2: return &ParseFloat2;
    case TypeId::Float3: return &ParseFloat3;
    case TypeId::Float4: return &ParseFloat4;
    case TypeId::Double2: return &ParseDouble2;
    case TypeId::Double3: return &ParseDouble3;
    case TypeId::Double4: return &ParseDouble4;
    case TypeId::Quatf: return &ParseQuatf;
    case TypeId::Quatd: return &ParseQuatd;
    case TypeId::Matrix3d: return &ParseMatrix3d;
    case TypeId::Matrix4d: return &ParseMatrix4d;

    // Semantic types that share storage with vectors
    case TypeId::Point3f: return &ParseFloat3;
    case TypeId::Point3d: return &ParseDouble3;
    case TypeId::Vector3f: return &ParseFloat3;
    case TypeId::Vector3d: return &ParseDouble3;
    case TypeId::Normal3f: return &ParseFloat3;
    case TypeId::Normal3d: return &ParseDouble3;
    case TypeId::Color3f: return &ParseFloat3;
    case TypeId::Color3d: return &ParseDouble3;
    case TypeId::Color4f: return &ParseFloat4;
    case TypeId::Color4d: return &ParseDouble4;
    case TypeId::Texcoord2f: return &ParseFloat2;
    case TypeId::Texcoord2d: return &ParseDouble2;
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

  ParseFn fn = GetParseFunction(expected_type);
  if (!fn) {
    return ParseResult::Error("No parser for type " + std::string(GetTypeName(expected_type)));
  }

  ParseResult result = fn(lexer);

  // If parsing succeeded but type has a semantic distinction, fix the type ID
  if (result.success) {
    // For semantic types, the parse function returns the base type
    // We need to update to the actual requested type
    switch (expected_type) {
      case TypeId::Point3f:
      case TypeId::Vector3f:
      case TypeId::Normal3f:
      case TypeId::Color3f:
      case TypeId::Point3d:
      case TypeId::Vector3d:
      case TypeId::Normal3d:
      case TypeId::Color3d:
      case TypeId::Color4f:
      case TypeId::Color4d:
      case TypeId::Texcoord2f:
      case TypeId::Texcoord2d:
      case TypeId::Texcoord3f:
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
  return type == TypeId::Float3 || type == TypeId::Point3f ||
         type == TypeId::Vector3f || type == TypeId::Normal3f ||
         type == TypeId::Color3f;
}

// Helper to check if type stores float2-like data
bool IsFloat2Like(TypeId type) {
  return type == TypeId::Float2 || type == TypeId::Texcoord2f;
}

// Helper to check if type stores float4-like data
bool IsFloat4Like(TypeId type) {
  return type == TypeId::Float4 || type == TypeId::Quatf ||
         type == TypeId::Color4f;
}

ParseResult ParseArrayValue(Lexer& lexer, TypeId element_type) {
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
      // For token/string arrays, just return empty float array as placeholder
      return ParseResult::Ok(Value::MakeFloatArray(std::vector<float>{}));
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
    // For now, we store string arrays as float arrays (placeholder)
    // TODO: Add proper string array support
    return ParseResult::Ok(Value::MakeFloatArray(std::vector<float>{}));
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
