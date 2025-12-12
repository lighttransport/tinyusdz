// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDA lexer (tokenizer)

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "lightusd/cursor.hh"
#include "lightusd/diagnostic.hh"

namespace lightusd {
namespace v1 {

/// Token types for USDA lexer
enum class TokenType : uint8_t {
    // Special
    Eof = 0,        // End of input
    Error,          // Lexer error

    // Literals
    Integer,        // 123, -456
    Float,          // 1.5, -3.14, 1e-5
    String,         // "hello" or 'hello'
    Identifier,     // someName, _name123

    // Punctuation
    LParen,         // (
    RParen,         // )
    LBracket,       // [
    RBracket,       // ]
    LBrace,         // {
    RBrace,         // }
    Equals,         // =
    Colon,          // :
    Comma,          // ,
    Dot,            // .
    Semicolon,      // ;
    At,             // @ (for asset paths)
    LAngle,         // <
    RAngle,         // >

    // Complex literals
    AssetPath,      // @path/to/asset.usd@
    PathRef,        // </Prim/Path> or <../RelPath>

    // Keywords
    Kw_def,
    Kw_over,
    Kw_class,
    Kw_rel,
    Kw_uniform,
    Kw_custom,
    Kw_add,
    Kw_delete,
    Kw_append,
    Kw_prepend,
    Kw_reorder,
    Kw_variantSet,
    Kw_None,
    Kw_true,
    Kw_false,
    Kw_timeSamples,
    Kw_connect,
    Kw_dictionary,
};

/// Get string name for token type
const char* token_type_name(TokenType type);

/// LexToken - a single lexical token with value and location
/// (Named LexToken to avoid conflict with USD Token class)
struct LexToken {
    TokenType type = TokenType::Eof;
    Cursor start;           // Start position
    Cursor end;             // End position (exclusive)

    // Value storage (union-like, determined by type)
    std::string str_value;  // For String, Identifier
    int64_t int_value = 0;  // For Integer
    double float_value = 0; // For Float

    LexToken() = default;
    LexToken(TokenType t, const Cursor& s, const Cursor& e)
        : type(t), start(s), end(e) {}

    bool is_eof() const { return type == TokenType::Eof; }
    bool is_error() const { return type == TokenType::Error; }

    /// Check if token is a keyword
    bool is_keyword() const {
        return type >= TokenType::Kw_def && type <= TokenType::Kw_dictionary;
    }

    /// Check if token is a specifier (def/over/class)
    bool is_specifier() const {
        return type == TokenType::Kw_def ||
               type == TokenType::Kw_over ||
               type == TokenType::Kw_class;
    }

    /// Check if token is a list edit qualifier (add/delete/append/prepend/reorder)
    bool is_list_edit_qual() const {
        return type == TokenType::Kw_add ||
               type == TokenType::Kw_delete ||
               type == TokenType::Kw_append ||
               type == TokenType::Kw_prepend ||
               type == TokenType::Kw_reorder;
    }

    /// Get string representation
    std::string to_string() const;
};

/// Lexer - tokenizes USDA source text
class Lexer {
public:
    /// Construct lexer with source text (must remain valid)
    Lexer(const char* source, size_t length);
    Lexer(const std::string& source);

    /// Set filename for error reporting
    void set_filename(const std::string& filename) { filename_ = filename; }

    /// Get next token
    LexToken next();

    /// Peek at next token without consuming
    const LexToken& peek();

    /// Check if at end of input
    bool at_end() const { return pos_ >= length_; }

    /// Get current cursor position
    Cursor cursor() const { return cursor_; }

    /// Get diagnostics
    const DiagnosticList& diagnostics() const { return diagnostics_; }

    /// Check if has errors
    bool has_errors() const { return diagnostics_.has_errors(); }

private:
    // Source text
    const char* source_;
    size_t length_;
    size_t pos_ = 0;
    Cursor cursor_;
    std::string filename_;

    // Lookahead
    LexToken peeked_;
    bool has_peeked_ = false;

    // Diagnostics
    DiagnosticList diagnostics_;

    // Character access
    char current() const;
    char peek_char(size_t offset = 1) const;
    void advance();
    void advance_n(size_t n);
    void skip_whitespace_and_comments();

    // Token scanning
    LexToken scan_token();
    LexToken scan_number();
    LexToken scan_string(char quote);
    LexToken scan_triple_string(char quote);
    LexToken scan_identifier_or_keyword();
    LexToken scan_asset_path();
    LexToken scan_path();

    // Helpers
    bool match(char c);
    bool is_digit(char c) const { return c >= '0' && c <= '9'; }
    bool is_alpha(char c) const {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    bool is_alnum(char c) const { return is_alpha(c) || is_digit(c); }

    LexToken make_token(TokenType type);
    LexToken make_error(const std::string& message);

    // Keyword lookup
    TokenType lookup_keyword(const std::string& name);
};

} // namespace v1
} // namespace lightusd
