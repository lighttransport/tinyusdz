// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA Lexer
// Simple tokenizer for USDA ASCII format

#pragma once

#include <string>
#include <cstdint>

namespace tinyusdz {
namespace next {

/// Token types
enum class TokenType : uint8_t {
  Invalid,
  Eof,           // End of input

  // Literals
  Identifier,    // foo, bar_baz, _underscore
  String,        // "hello world"
  Number,        // 123, 3.14, -1.5e10
  PathRef,       // </World/Cube>

  // Keywords
  Def,           // def
  Over,          // over
  Class,         // class
  True,          // true
  False,         // false
  None,          // None

  // Symbols
  OpenParen,     // (
  CloseParen,    // )
  OpenBracket,   // [
  CloseBracket,  // ]
  OpenBrace,     // {
  CloseBrace,    // }
  Equals,        // =
  Colon,         // :
  Dot,           // .
  Comma,         // ,
  Semicolon,     // ;
  At,            // @

  // Special
  TimeSamples,   // timeSamples
  Custom,        // custom
  Uniform,       // uniform
  Varying,       // varying
  Prepend,       // prepend
  Append,        // append
  Delete,        // delete
  Add,           // add
  Reorder,       // reorder
  Rel,           // rel
};

/// Token with source location
struct Token {
  TokenType type = TokenType::Invalid;
  std::string value;     // The actual text content
  size_t line = 0;       // 1-based line number
  size_t column = 0;     // 1-based column number

  bool is(TokenType t) const { return type == t; }
  bool is_literal() const {
    return type == TokenType::Identifier ||
           type == TokenType::String ||
           type == TokenType::Number ||
           type == TokenType::PathRef;
  }
};

/// Lexer - tokenizes USDA input
class Lexer {
public:
  /// Construct from input data
  Lexer(const char* data, size_t length);

  /// Get current position info
  size_t line() const { return line_; }
  size_t column() const { return column_; }
  size_t position() const { return pos_; }

  /// Peek at current token without consuming
  const Token& peek();

  /// Get current token and advance
  Token next();

  /// Check if at end of input
  bool at_end() const { return pos_ >= length_; }

  /// Expect a specific token type (returns false if not matched)
  bool expect(TokenType type);

  /// Expect and consume, storing value
  bool expect(TokenType type, std::string& out_value);

  /// Skip to end of current line
  void skip_line();

  /// Capture and consume a complete bracketed literal without tokenizing every
  /// element. The returned span points into the lexer's input and includes the
  /// outer '[' and ']'.
  bool capture_bracketed_literal(const char** out_data, size_t* out_len);

  /// Get error message if in error state
  const std::string& error() const { return error_; }

  /// Check if lexer is in error state
  bool has_error() const { return !error_.empty(); }

  /// Set error message
  void set_error(const std::string& msg);

private:
  const char* data_;
  size_t length_;
  size_t pos_ = 0;
  size_t line_ = 1;
  size_t column_ = 1;

  Token current_;
  bool has_current_ = false;
  std::string error_;

  void advance();
  char current_char() const;
  char peek_char(size_t offset = 1) const;
  void skip_whitespace();
  void skip_comment();

  Token scan_token();
  Token scan_identifier();
  Token scan_number();
  Token scan_string();
  Token scan_path_ref();
  Token scan_asset_ref();

  Token make_token(TokenType type, size_t start_line, size_t start_col);
  Token make_token(TokenType type, const std::string& value, size_t start_line, size_t start_col);
};

/// Get string name for token type (for debugging)
const char* TokenTypeName(TokenType type);

}  // namespace next
}  // namespace tinyusdz
