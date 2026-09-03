// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - Value Parser implementation

#include "value-parser.hh"
#include "value-parser-numeric.hh"
#include "../strfmt.hh"
#include "lexer.hh"
#include "../crate/crate-format.hh"
#include "../crate/lazy-array.hh"
#include "../types/type-info.hh"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <limits>
#include <unordered_map>
#include <vector>

#if defined(LIGHTUSD_ENABLE_THREAD)
#include <thread>
#endif

namespace lightusd {
namespace next {

namespace {

// ============================================================
// Value parsing functions
// ============================================================

using value_parser_detail::DecimalToI64;
using value_parser_detail::DecimalToI32;
using value_parser_detail::IsDecimalIntToken;
using value_parser_detail::DecimalToU32;
using value_parser_detail::DecimalToU64;
using value_parser_detail::FastFloatParse;
using value_parser_detail::FastFloatParseToken;

#include "value-parser-scalars.inc"

}  // anonymous namespace

// ============================================================
// Public API
// ============================================================

ParseResult ParseValue(Lexer& lexer, TypeId expected_type) {
  // Handle None: an authored value block, not "no value". Preserve it as a block
  // so the writer re-emits `= None` (round-trips USDC ValueBlock + USDA `= None`).
  if (lexer.peek().type == TokenType::None) {
    lexer.next();
    return ParseResult::Ok(Value::MakeBlock());
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
                                  : "#" + IntToStr(int(expected_type))));
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

#include "value-parser-arrays.inc"

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
    // Count elements in the tuple to determine arity before parsing.
    // Save byte position of '(' so we can reset the lexer after the scan pass.
    size_t saved_pos = lexer.token_start();
    lexer.next();  // consume '('
    size_t count = 1;
    size_t depth = 1;
    bool all_int = true;
    while (depth > 0) {
      const Token& t = lexer.peek();
      if (t.type == TokenType::Eof) break;
      if (t.type == TokenType::OpenParen) {
        depth++;
      } else if (t.type == TokenType::CloseParen) {
        depth--;
        if (depth == 0) break;
      } else if (t.type == TokenType::Comma && depth == 1) {
        count++;
      } else if (IsNumberToken(t)) {
        if (t.value.find('.') != std::string::npos ||
            t.value.find('e') != std::string::npos ||
            t.value.find('E') != std::string::npos) {
          all_int = false;
        }
      }
      lexer.next();
    }
    if (depth != 0) {
      return ParseResult::Error("Unmatched '(' in tuple");
    }

    // Reset lexer to saved_pos and re-parse with the correct arity.
    lexer.set_position(saved_pos);

    if (!all_int) {
      switch (count) {
        case 2: {
          out_type = TypeId::Float2;
          return ParseFloat2(lexer);
        }
        case 3: {
          out_type = TypeId::Float3;
          return ParseFloat3(lexer);
        }
        case 4: {
          out_type = TypeId::Float4;
          return ParseFloat4(lexer);
        }
        default:
          break;
      }
    } else {
      switch (count) {
        case 2: {
          out_type = TypeId::Int2;
          return ParseInt2(lexer);
        }
        case 3: {
          out_type = TypeId::Int3;
          return ParseInt3(lexer);
        }
        case 4: {
          out_type = TypeId::Int4;
          return ParseInt4(lexer);
        }
        default:
          break;
      }
    }

    out_type = TypeId::Invalid;
    return ParseResult::Error("Unsupported tuple arity " + std::to_string(count));
  }

  if (tok.type == TokenType::None) {
    out_type = TypeId::Invalid;
    lexer.next();
    return ParseResult::Ok(Value());
  }

  out_type = TypeId::Invalid;
  return ParseResult::Error("Cannot infer type from token");
}

#include "value-parser-dict.inc"


}  // namespace next
}  // namespace lightusd
