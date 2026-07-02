// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA Lexer
// Simple tokenizer for USDA ASCII format

#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace tinyusdz {
namespace next {

struct USDAParseProfile;

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
  /// Raw token text as a slice of the lexer's input buffer (valid for the
  /// whole parse — the input outlives the token stream). Set for Identifier,
  /// Number, PathRef and keyword tokens, which never need un-escaping; empty
  /// for String tokens (see `value`). Avoids a std::string construction per
  /// token on the multi-million-token hot path.
  std::string_view text;
  /// Owned, escape-processed content — String tokens (and asset refs, which
  /// lex as String) only.
  std::string value;
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

  /// Advance past the current token without returning it. Equivalent to
  /// calling next() and discarding the result, minus the Token (and value
  /// string) copy — use for the common peek-then-consume pattern.
  void consume();

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
  /// outer '[' and ']'. When `out_simple` is non-null it reports whether the
  /// array contains ONLY "plain" bytes (no comment `#`, string/asset quote, or
  /// nested `[` ) — i.e. all commas/parens are pure structural separators, which
  /// lets a numeric array be safely split at separator boundaries for parallel
  /// parsing. When `out_commas` is non-null it receives the number of commas
  /// seen in the SIMD-scanned (plain) bytes; trustworthy ONLY when the array is
  /// simple, where it predicts the scalar count (scalars = commas + 1) so the
  /// parser can pre-size its output.
  bool capture_bracketed_literal(const char** out_data, size_t* out_len,
                                 bool* out_simple = nullptr,
                                 size_t* out_commas = nullptr);

  /// Capture and consume one complete prim block
  ///   def|over|class [Type] "name" [(meta)] { ...body... }
  /// starting at the current specifier token, without tokenizing its contents
  /// (SIMD brace/paren matching; strings/assets/comments skipped). On success
  /// the whole block is consumed and the returned span (a slice of the input,
  /// including the specifier through the closing '}') can be re-parsed
  /// independently — the basis of the parallel prim-subtree parse. `out_line`
  /// receives the 1-based line of the block start so a sub-parser can report
  /// correct error locations.
  ///
  /// If more than `max_bytes` would be scanned, the lexer state is fully
  /// restored and false is returned with *out_too_big = true (caller parses
  /// the prim inline and its CHILDREN get their own capture attempts). Blocks
  /// smaller than `min_bytes` are also restored (false, *out_too_big = false):
  /// tiny prims are cheaper to parse inline than to dispatch. Malformed input
  /// (unterminated block) restores likewise, leaving the inline parser to
  /// produce its usual error.
  bool capture_prim_block(size_t min_bytes, size_t max_bytes,
                          const char** out_block, size_t* out_len,
                          size_t* out_line, bool* out_too_big);

  /// Override the 1-based source location the lexer reports (line/column of
  /// the FIRST input byte). Used by sub-parsers running on a slice of a larger
  /// file so their diagnostics carry file-absolute locations.
  void set_source_location(size_t line, size_t column) {
    line_ = line;
    column_ = column;
  }

  /// Get error message if in error state
  const std::string& error() const { return error_; }

  /// Check if lexer is in error state
  bool has_error() const { return !error_.empty(); }

  /// Set error message
  void set_error(const std::string& msg);

  /// Worker-thread hint for the parallel large-array parse path, forwarded from
  /// ParseOptions::num_threads (0 = auto, 1 = serial, >1 = that many). Carried on
  /// the lexer because the stateless value-parser array helpers receive only the
  /// lexer; replaces the former TINYUSDZ_NEXT_NUM_THREADS env read.
  int num_threads = 0;

  /// Optional parser profiling sink.
  USDAParseProfile* profile = nullptr;

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
  Token make_token(TokenType type, std::string value, size_t start_line, size_t start_col);
  Token make_token(TokenType type, std::string_view text, size_t start_line, size_t start_col);
};

/// Get string name for token type (for debugging)
const char* TokenTypeName(TokenType type);

}  // namespace next
}  // namespace tinyusdz
