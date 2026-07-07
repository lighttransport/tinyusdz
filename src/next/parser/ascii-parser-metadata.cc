// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII parser stage metadata.

#include "ascii-parser-internal.hh"
#include "value-parser.hh"

namespace tinyusdz {
namespace next {

bool AsciiParser::Impl::ParseStageMetadata() {
  // Check for #usda header
  const Token& first = lexer_->peek();
  if (first.type == TokenType::Identifier && first.value == "#usda") {
    // Skip #usda line
    lexer_->skip_line();
  }

  // Check for opening paren (metadata block)
  if (Check(TokenType::OpenParen)) {
    lexer_->next();

    // Parse metadata key-value pairs
    while (!Check(TokenType::CloseParen) && !AtEnd()) {
      // A bare (often triple-quoted) string is the layer documentation --
      // USD shorthand for `doc = "..."`.
      if (Check(TokenType::String)) {
        std::string d;
        lexer_->expect(TokenType::String, d);
        layer_->meta().doc = d;
        continue;
      }
      std::string key;
      if (!lexer_->expect(TokenType::Identifier, key)) {
        AddError("Expected metadata key");
        return false;
      }

      if (!Match(TokenType::Equals)) {
        AddError("Expected '=' after metadata key");
        return false;
      }

      // Parse value based on known metadata keys
      if (key == "defaultPrim") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().defaultPrim = value;
        }
      } else if (key == "upAxis") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().upAxis = value;
        }
      } else if (key == "metersPerUnit") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().metersPerUnit = *result.value.as_double();
        }
      } else if (key == "timeCodesPerSecond") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().timeCodesPerSecond = *result.value.as_double();
        }
      } else if (key == "startTimeCode") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().startTimeCode = *result.value.as_double();
        }
      } else if (key == "endTimeCode") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().endTimeCode = *result.value.as_double();
        }
      } else if (key == "framesPerSecond") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().framesPerSecond = *result.value.as_double();
          layer_->meta().framesPerSecond_set = true;
        }
      } else if (key == "kilogramsPerUnit") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().kilogramsPerUnit = *result.value.as_double();
          layer_->meta().kilogramsPerUnit_set = true;
        }
      } else if (key == "colorConfiguration") {
        // asset (`@path@`, lexed as String) or quoted string
        std::string value;
        if (Check(TokenType::String)) {
          lexer_->expect(TokenType::String, value);
          layer_->meta().colorConfiguration = value;
        }
      } else if (key == "colorManagementSystem") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().colorManagementSystem = value;
        }
      } else if (key == "doc" || key == "documentation") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().doc = value;
        }
      } else if (key == "comment") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().comment = value;
        }
      } else if (key == "customLayerData") {
        ParseResult r = ParseDict(*lexer_);
        if (r.success) layer_->meta().customLayerData = std::move(r.value);
      } else if (key == "expressionVariables") {
        ParseResult r = ParseDict(*lexer_);
        if (r.success) layer_->meta().expressionVariables = std::move(r.value);
      } else if (key == "subLayers") {
        // Parse sublayer list. Each element is an asset ref (`@path@`, lexed as
        // a String) or a quoted string. Decide with a non-consuming Check first:
        // `expect()` always consumes, so `expect(A) || expect(B)` would eat two
        // tokens per element and corrupt the list.
        if (Match(TokenType::OpenBracket)) {
          while (!Check(TokenType::CloseBracket) && !AtEnd()) {
            std::string path;
            if (Check(TokenType::PathRef)) {
              lexer_->expect(TokenType::PathRef, path);
            } else if (Check(TokenType::String)) {
              lexer_->expect(TokenType::String, path);
            } else {
              break;  // unexpected token: stop rather than mis-consume
            }
            layer_->meta().subLayers.push_back(path);
            // Optional per-sublayer layer offset "(offset = ..; scale = ..)".
            // Sublayer offsets are not yet plumbed through composition; parse
            // and skip so the file at least LOADS (previously the unexpected
            // '(' failed the whole metadata block and thus the whole load).
            if (Check(TokenType::OpenParen)) {
              AddWarning("subLayers layer offsets are parsed but not applied");
              SkipValueLike();
            }
            Match(TokenType::Comma);
          }
          Match(TokenType::CloseBracket);
        }
      } else {
        // Generic metadata may be a dictionary/list; skip it structurally.
        SkipValueLike();
        AddWarning("Unknown stage metadata: " + key);
      }
    }

    if (!Match(TokenType::CloseParen)) {
      AddError("Expected ')' to close metadata block");
      return false;
    }
  }

  return true;
}

}  // namespace next
}  // namespace tinyusdz
