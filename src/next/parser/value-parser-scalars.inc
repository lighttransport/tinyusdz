// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - scalar, tuple, matrix value parsers and type dispatch table.
// Included inside value-parser.cc anonymous namespace.

ParseResult ParseBool(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type == TokenType::True) {
    lexer.next();
    return ParseResult::Ok(Value(true));
  } else if (tok.type == TokenType::False) {
    lexer.next();
    return ParseResult::Ok(Value(false));
  } else if (tok.type == TokenType::Number) {
    // pxr accepts ONLY the integer literals 0 and 1 for bool (not arbitrary
    // numerics: `bool b = 5` / `= 0.5` are parse errors, not truthiness).
    if (tok.value == "0" || tok.value == "1") {
      const bool b = (tok.value == "1");
      lexer.next();
      return ParseResult::Ok(Value(b));
    }
    return ParseResult::Error("Invalid boolean value (expected 0, 1, true or false)");
  }
  return ParseResult::Error("Expected boolean value");
}
// Our own decimal string->int (no libc strtol, no octal/hex; USD integers are
// decimal). Lenient like the strtol-family it replaces: optional sign + decimal
// digits, ignore trailing chars, saturate on overflow (matching strtol's
// ERANGE clamp after the int32/int64 cast). Callers pass clean lexer Number
// tokens. Keeps the parser free of libc/locale (and thus WASM/WASI-friendly).
ParseResult ParseInt(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number ||
      !IsDecimalIntToken(tok.value, /*allow_neg=*/true)) {
    return ParseResult::Error("Expected integer value");
  }
  lexer.next();
  int64_t wide = 0;
  if (!value_parser_detail::DecimalToI64Checked(tok.value.c_str(), &wide) ||
      wide > INT32_MAX || wide < INT32_MIN) {
    return ParseResult::Error("Integer value out of range");
  }
  return ParseResult::Ok(Value(static_cast<int32_t>(wide)));
}

ParseResult ParseUInt(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number ||
      !IsDecimalIntToken(tok.value, /*allow_neg=*/false)) {
    return ParseResult::Error("Expected unsigned integer value");
  }
  lexer.next();
  uint64_t wide = 0;
  if (!value_parser_detail::DecimalToU64Checked(tok.value.c_str(), &wide) || wide > UINT32_MAX) {
    return ParseResult::Error("Unsigned integer value out of range");
  }
  return ParseResult::Ok(Value(static_cast<uint32_t>(wide)));
}

ParseResult ParseInt64(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number ||
      !IsDecimalIntToken(tok.value, /*allow_neg=*/true)) {
    return ParseResult::Error("Expected 64-bit integer value");
  }
  lexer.next();
  int64_t value = 0;
  if (!value_parser_detail::DecimalToI64Checked(tok.value.c_str(), &value)) {
    return ParseResult::Error("64-bit integer value out of range");
  }
  return ParseResult::Ok(Value(value));
}

ParseResult ParseUInt64(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number ||
      !IsDecimalIntToken(tok.value, /*allow_neg=*/false)) {
    return ParseResult::Error("Expected 64-bit unsigned integer value");
  }
  lexer.next();
  uint64_t value = 0;
  if (!value_parser_detail::DecimalToU64Checked(tok.value.c_str(), &value)) {
    return ParseResult::Error("64-bit unsigned integer value out of range");
  }
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
  return ParseResult::Ok(Value(FastFloatParseToken<float>(tok.value)));
}

ParseResult ParseDouble(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (!IsNumberToken(tok)) {
    return ParseResult::Error("Expected double value");
  }
  lexer.next();
  return ParseResult::Ok(Value(FastFloatParseToken<double>(tok.value)));
}

ParseResult ParseUChar(Lexer& lexer) {
  const Token& tok = lexer.peek();
  if (tok.type != TokenType::Number ||
      !IsDecimalIntToken(tok.value, /*allow_neg=*/false)) {
    return ParseResult::Error("Expected unsigned integer value");
  }
  lexer.next();
  uint64_t wide = 0;
  if (!value_parser_detail::DecimalToU64Checked(tok.value.c_str(), &wide) ||
      wide > 255) {
    return ParseResult::Error("uchar value out of range");
  }
  const uint8_t b = static_cast<uint8_t>(wide);
  return ParseResult::Ok(Value::MakeFromRaw(TypeId::UChar, &b));
}

ParseResult ParseTimeCode(Lexer& lexer) {
  // Same wire format as double, but the value keeps its TimeCode identity
  // (matches the crate reader; a rewrite must not mutate timecode -> double).
  const Token& tok = lexer.peek();
  if (!IsNumberToken(tok)) {
    return ParseResult::Error("Expected timecode value");
  }
  lexer.next();
  const double d = FastFloatParseToken<double>(tok.value);
  return ParseResult::Ok(Value::MakeFromRaw(TypeId::TimeCode, &d));
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
    out[i] = FastFloatParseToken<float>(tok.value);
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
    out[i] = FastFloatParseToken<double>(tok.value);
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
    if (tok.type != TokenType::Number ||
        !IsDecimalIntToken(tok.value, /*allow_neg=*/true)) {
      // DecimalToI32 stops at the first non-digit, so "1e3" would silently
      // become 1 — reject non-integer tokens like the scalar path does.
      lexer.set_error("Expected integer in tuple");
      return false;
    }
    out[i] = DecimalToI32(tok.value.c_str());
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
    if (!IsDecimalIntToken(tok.value, /*allow_neg=*/false)) {
      lexer.set_error("Expected unsigned integer in tuple");
      return false;
    }
    out[i] = DecimalToU32(tok.value.c_str());
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
  uint16_t bits = FloatToHalf(FastFloatParseToken<float>(tok.value));
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
    case TypeId::UChar: return &ParseUChar;
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

    case TypeId::TimeCode: return &ParseTimeCode;

    default:
      return nullptr;
  }
}
