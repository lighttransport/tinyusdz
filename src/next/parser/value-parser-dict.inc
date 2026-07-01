// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - dictionary value parser.
// Included inside namespace tinyusdz::next.

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
