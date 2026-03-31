// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Lexer implementation

#include "lexer.hh"

#include <cctype>
#include <cstring>

namespace tinyusdz {
namespace next {

namespace {

bool IsIdentifierStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool IsIdentifierContinue(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsDigit(char c) {
  return std::isdigit(static_cast<unsigned char>(c));
}

bool IsHexDigit(char c) {
  return std::isxdigit(static_cast<unsigned char>(c));
}

struct Keyword {
  const char* name;
  TokenType type;
};

const Keyword kKeywords[] = {
  {"def", TokenType::Def},
  {"over", TokenType::Over},
  {"class", TokenType::Class},
  {"true", TokenType::True},
  {"false", TokenType::False},
  {"None", TokenType::None},
  {"timeSamples", TokenType::TimeSamples},
  {"custom", TokenType::Custom},
  {"uniform", TokenType::Uniform},
  {"varying", TokenType::Varying},
  {"prepend", TokenType::Prepend},
  {"append", TokenType::Append},
  {"delete", TokenType::Delete},
  {"add", TokenType::Add},
  {"reorder", TokenType::Reorder},
  {"rel", TokenType::Rel},
};

TokenType LookupKeyword(const std::string& name) {
  for (const auto& kw : kKeywords) {
    if (name == kw.name) {
      return kw.type;
    }
  }
  return TokenType::Identifier;
}

}  // anonymous namespace

Lexer::Lexer(const char* data, size_t length)
    : data_(data), length_(length) {}

void Lexer::advance() {
  if (pos_ < length_) {
    if (data_[pos_] == '\n') {
      line_++;
      column_ = 1;
    } else {
      column_++;
    }
    pos_++;
  }
}

char Lexer::current_char() const {
  return (pos_ < length_) ? data_[pos_] : '\0';
}

char Lexer::peek_char(size_t offset) const {
  size_t idx = pos_ + offset;
  return (idx < length_) ? data_[idx] : '\0';
}

void Lexer::skip_whitespace() {
  while (pos_ < length_) {
    char c = current_char();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
    } else if (c == '#') {
      skip_comment();
    } else {
      break;
    }
  }
}

void Lexer::skip_comment() {
  // Skip to end of line
  while (pos_ < length_ && current_char() != '\n') {
    advance();
  }
}

void Lexer::skip_line() {
  while (pos_ < length_ && current_char() != '\n') {
    advance();
  }
  if (pos_ < length_) {
    advance();  // Skip the newline
  }
}

const Token& Lexer::peek() {
  if (!has_current_) {
    current_ = scan_token();
    has_current_ = true;
  }
  return current_;
}

Token Lexer::next() {
  if (has_current_) {
    has_current_ = false;
    return current_;
  }
  return scan_token();
}

bool Lexer::expect(TokenType type) {
  Token tok = next();
  if (tok.type != type) {
    set_error("Expected " + std::string(TokenTypeName(type)) +
              ", got " + std::string(TokenTypeName(tok.type)));
    return false;
  }
  return true;
}

bool Lexer::expect(TokenType type, std::string& out_value) {
  Token tok = next();
  if (tok.type != type) {
    set_error("Expected " + std::string(TokenTypeName(type)) +
              ", got " + std::string(TokenTypeName(tok.type)));
    return false;
  }
  out_value = std::move(tok.value);
  return true;
}

void Lexer::set_error(const std::string& msg) {
  if (error_.empty()) {
    error_ = "Line " + std::to_string(line_) + ", column " + std::to_string(column_) + ": " + msg;
  }
}

Token Lexer::make_token(TokenType type, size_t start_line, size_t start_col) {
  Token tok;
  tok.type = type;
  tok.line = start_line;
  tok.column = start_col;
  return tok;
}

Token Lexer::make_token(TokenType type, const std::string& value, size_t start_line, size_t start_col) {
  Token tok;
  tok.type = type;
  tok.value = value;
  tok.line = start_line;
  tok.column = start_col;
  return tok;
}

Token Lexer::scan_token() {
  skip_whitespace();

  if (at_end()) {
    return make_token(TokenType::Eof, line_, column_);
  }

  size_t start_line = line_;
  size_t start_col = column_;
  char c = current_char();

  // Single-character tokens
  switch (c) {
    case '(': advance(); return make_token(TokenType::OpenParen, start_line, start_col);
    case ')': advance(); return make_token(TokenType::CloseParen, start_line, start_col);
    case '[': advance(); return make_token(TokenType::OpenBracket, start_line, start_col);
    case ']': advance(); return make_token(TokenType::CloseBracket, start_line, start_col);
    case '{': advance(); return make_token(TokenType::OpenBrace, start_line, start_col);
    case '}': advance(); return make_token(TokenType::CloseBrace, start_line, start_col);
    case '=': advance(); return make_token(TokenType::Equals, start_line, start_col);
    case ':': advance(); return make_token(TokenType::Colon, start_line, start_col);
    case '.': advance(); return make_token(TokenType::Dot, start_line, start_col);
    case ',': advance(); return make_token(TokenType::Comma, start_line, start_col);
    case ';': advance(); return make_token(TokenType::Semicolon, start_line, start_col);
  }

  // Path reference: </path>
  if (c == '<') {
    return scan_path_ref();
  }

  // Asset reference: @path@
  if (c == '@') {
    return scan_asset_ref();
  }

  // String
  if (c == '"') {
    return scan_string();
  }

  // Identifier or keyword
  if (IsIdentifierStart(c)) {
    return scan_identifier();
  }

  // Number (including negative)
  if (IsDigit(c) || (c == '-' && IsDigit(peek_char())) || (c == '+' && IsDigit(peek_char()))) {
    return scan_number();
  }

  // Unrecognized character
  set_error(std::string("Unexpected character: ") + c);
  advance();
  return make_token(TokenType::Invalid, start_line, start_col);
}

Token Lexer::scan_identifier() {
  size_t start_line = line_;
  size_t start_col = column_;
  size_t start = pos_;

  while (IsIdentifierContinue(current_char())) {
    advance();
  }

  std::string value(data_ + start, pos_ - start);
  TokenType type = LookupKeyword(value);

  return make_token(type, value, start_line, start_col);
}

Token Lexer::scan_number() {
  size_t start_line = line_;
  size_t start_col = column_;
  size_t start = pos_;

  // Sign
  if (current_char() == '-' || current_char() == '+') {
    advance();
  }

  // Check for hex
  if (current_char() == '0' && (peek_char() == 'x' || peek_char() == 'X')) {
    advance();  // '0'
    advance();  // 'x'
    while (IsHexDigit(current_char())) {
      advance();
    }
  } else {
    // Integer part
    while (IsDigit(current_char())) {
      advance();
    }

    // Decimal part
    if (current_char() == '.' && IsDigit(peek_char())) {
      advance();  // '.'
      while (IsDigit(current_char())) {
        advance();
      }
    }

    // Exponent
    if (current_char() == 'e' || current_char() == 'E') {
      advance();
      if (current_char() == '+' || current_char() == '-') {
        advance();
      }
      while (IsDigit(current_char())) {
        advance();
      }
    }
  }

  std::string value(data_ + start, pos_ - start);
  return make_token(TokenType::Number, value, start_line, start_col);
}

Token Lexer::scan_string() {
  size_t start_line = line_;
  size_t start_col = column_;

  advance();  // Opening quote

  std::string value;
  bool is_multiline = false;

  // Check for triple-quoted string
  if (current_char() == '"' && peek_char() == '"') {
    is_multiline = true;
    advance();  // Second quote
    advance();  // Third quote
  }

  while (!at_end()) {
    char c = current_char();

    if (is_multiline) {
      // Check for closing triple quotes
      if (c == '"' && peek_char() == '"' && peek_char(2) == '"') {
        advance();
        advance();
        advance();
        break;
      }
    } else {
      if (c == '"') {
        advance();
        break;
      }
      if (c == '\n') {
        set_error("Unterminated string literal");
        break;
      }
    }

    // Escape sequences
    if (c == '\\' && !at_end()) {
      advance();
      char escaped = current_char();
      switch (escaped) {
        case 'n': value += '\n'; break;
        case 't': value += '\t'; break;
        case 'r': value += '\r'; break;
        case '\\': value += '\\'; break;
        case '"': value += '"'; break;
        default:
          value += '\\';
          value += escaped;
          break;
      }
      advance();
    } else {
      value += c;
      advance();
    }
  }

  return make_token(TokenType::String, value, start_line, start_col);
}

Token Lexer::scan_path_ref() {
  size_t start_line = line_;
  size_t start_col = column_;

  advance();  // '<'

  std::string value;
  while (!at_end() && current_char() != '>') {
    value += current_char();
    advance();
  }

  if (current_char() == '>') {
    advance();
  } else {
    set_error("Unterminated path reference");
  }

  return make_token(TokenType::PathRef, value, start_line, start_col);
}

Token Lexer::scan_asset_ref() {
  size_t start_line = line_;
  size_t start_col = column_;

  advance();  // '@'

  // Check for @@@ (triple at)
  bool is_triple = false;
  if (current_char() == '@' && peek_char() == '@') {
    is_triple = true;
    advance();
    advance();
  }

  std::string value;
  while (!at_end()) {
    char c = current_char();

    if (is_triple) {
      if (c == '@' && peek_char() == '@' && peek_char(2) == '@') {
        advance();
        advance();
        advance();
        break;
      }
    } else {
      if (c == '@') {
        advance();
        break;
      }
    }

    value += c;
    advance();
  }

  // Return as a string token (asset references are essentially strings)
  return make_token(TokenType::String, value, start_line, start_col);
}

const char* TokenTypeName(TokenType type) {
  switch (type) {
    case TokenType::Invalid: return "Invalid";
    case TokenType::Eof: return "EOF";
    case TokenType::Identifier: return "Identifier";
    case TokenType::String: return "String";
    case TokenType::Number: return "Number";
    case TokenType::PathRef: return "PathRef";
    case TokenType::Def: return "def";
    case TokenType::Over: return "over";
    case TokenType::Class: return "class";
    case TokenType::True: return "true";
    case TokenType::False: return "false";
    case TokenType::None: return "None";
    case TokenType::OpenParen: return "(";
    case TokenType::CloseParen: return ")";
    case TokenType::OpenBracket: return "[";
    case TokenType::CloseBracket: return "]";
    case TokenType::OpenBrace: return "{";
    case TokenType::CloseBrace: return "}";
    case TokenType::Equals: return "=";
    case TokenType::Colon: return ":";
    case TokenType::Dot: return ".";
    case TokenType::Comma: return ",";
    case TokenType::Semicolon: return ";";
    case TokenType::At: return "@";
    case TokenType::TimeSamples: return "timeSamples";
    case TokenType::Custom: return "custom";
    case TokenType::Uniform: return "uniform";
    case TokenType::Varying: return "varying";
    case TokenType::Prepend: return "prepend";
    case TokenType::Append: return "append";
    case TokenType::Delete: return "delete";
    case TokenType::Add: return "add";
    case TokenType::Reorder: return "reorder";
    case TokenType::Rel: return "rel";
  }
  return "Unknown";
}

}  // namespace next
}  // namespace tinyusdz
