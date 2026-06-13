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

// Length of an `inf` / `infinity` / `nan` special-float word (case-insensitive)
// starting at p, or 0 if none. Requires a non-identifier char (or end) after the
// word so e.g. `info` or `index` are not mistaken for `inf`.
size_t MatchFloatSpecial(const char* p, const char* end) {
  auto ieq = [](const char* a, const char* lit, size_t n) {
    for (size_t i = 0; i < n; i++) {
      if (std::tolower(static_cast<unsigned char>(a[i])) != lit[i]) return false;
    }
    return true;
  };
  auto word_boundary = [&](size_t n) {
    return (p + n == end) || !IsIdentifierContinue(p[n]);
  };
  if (p + 8 <= end && ieq(p, "infinity", 8) && word_boundary(8)) return 8;
  if (p + 3 <= end && ieq(p, "inf", 3) && word_boundary(3)) return 3;
  if (p + 3 <= end && ieq(p, "nan", 3) && word_boundary(3)) return 3;
  return 0;
}

int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Append a Unicode code point to `out` as UTF-8.
void AppendUtf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7f) {
    out += static_cast<char>(cp);
  } else if (cp <= 0x7ff) {
    out += static_cast<char>(0xc0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  } else if (cp <= 0xffff) {
    out += static_cast<char>(0xe0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  } else if (cp <= 0x10ffff) {
    out += static_cast<char>(0xf0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  }
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

bool Lexer::capture_bracketed_literal(const char** out_data, size_t* out_len) {
  if (!out_data || !out_len) return false;
  const Token& tok = peek();
  if (tok.type != TokenType::OpenBracket) {
    set_error("Expected OpenBracket, got " + std::string(TokenTypeName(tok.type)));
    return false;
  }

  // `peek()` has scanned the '[' token, so pos_ is just after it.
  const size_t start = pos_ > 0 ? pos_ - 1 : 0;
  has_current_ = false;

  int depth = 1;
  while (pos_ < length_ && depth > 0) {
    const char c = current_char();

    if (c == '#') {
      skip_comment();
      continue;
    }

    if (c == '"' || c == '\'') {
      const char quote = c;
      advance();
      bool triple = false;
      if (current_char() == quote && peek_char() == quote) {
        triple = true;
        advance();
        advance();
      }
      while (pos_ < length_) {
        if (current_char() == '\\') {
          advance();
          if (pos_ < length_) advance();
          continue;
        }
        if (triple) {
          if (current_char() == quote && peek_char() == quote && peek_char(2) == quote) {
            advance();
            advance();
            advance();
            break;
          }
        } else if (current_char() == quote) {
          advance();
          break;
        }
        advance();
      }
      continue;
    }

    if (c == '@') {
      advance();
      while (pos_ < length_) {
        if (current_char() == '\\') {
          advance();
          if (pos_ < length_) advance();
          continue;
        }
        if (current_char() == '@') {
          advance();
          break;
        }
        advance();
      }
      continue;
    }

    if (c == '[') {
      depth++;
    } else if (c == ']') {
      depth--;
    }
    advance();
  }

  if (depth != 0) {
    set_error("Unexpected end of input while parsing array literal");
    return false;
  }

  *out_data = data_ + start;
  *out_len = pos_ - start;
  has_current_ = false;
  return true;
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
  if (c == '"' || c == '\'') {
    return scan_string();
  }

  // Identifier or keyword
  if (IsIdentifierStart(c)) {
    return scan_identifier();
  }

  // Number (including negative, and signed inf/nan specials such as `-inf`).
  // Bare `inf`/`nan` start with a letter and are lexed as identifiers above;
  // the value parser accepts those in numeric context (keeping an attribute
  // literally named `inf` safe).
  if (IsDigit(c) || (c == '-' && IsDigit(peek_char())) || (c == '+' && IsDigit(peek_char())) ||
      ((c == '-' || c == '+') && MatchFloatSpecial(data_ + pos_ + 1, data_ + length_) > 0)) {
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

  // Signed inf / infinity / nan special float literal (e.g. `-inf`). Emit as a
  // Number token; strtof/strtod/fast_float parse these directly.
  if (size_t special = MatchFloatSpecial(data_ + pos_, data_ + length_)) {
    for (size_t i = 0; i < special; i++) advance();
    std::string value(data_ + start, pos_ - start);
    return make_token(TokenType::Number, value, start_line, start_col);
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

  const char quote = current_char();
  advance();  // Opening quote

  std::string value;
  bool is_multiline = false;

  // Check for triple-quoted string
  if (current_char() == quote && peek_char() == quote) {
    is_multiline = true;
    advance();  // Second quote
    advance();  // Third quote
  }

  while (!at_end()) {
    char c = current_char();

    if (is_multiline) {
      // Check for closing triple quotes
      if (c == quote && peek_char() == quote && peek_char(2) == quote) {
        advance();
        advance();
        advance();
        break;
      }
    } else {
      if (c == quote) {
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
      advance();                  // consume '\'
      char escaped = current_char();
      advance();                  // consume the escape indicator char
      switch (escaped) {
        case 'n': value += '\n'; break;
        case 't': value += '\t'; break;
        case 'r': value += '\r'; break;
        case 'a': value += '\a'; break;
        case 'b': value += '\b'; break;
        case 'f': value += '\f'; break;
        case 'v': value += '\v'; break;
        case '\\': value += '\\'; break;
        case '"': value += '"'; break;
        case '\'': value += '\''; break;
        case 'x': {
          // \xNN - up to two hex digits
          int v = 0, n = 0;
          while (n < 2 && HexVal(current_char()) >= 0) {
            v = v * 16 + HexVal(current_char());
            advance();
            n++;
          }
          if (n == 0) { value += '\\'; value += 'x'; }
          else value += static_cast<char>(v);
          break;
        }
        case 'u':
        case 'U': {
          // \uXXXX (4 hex) or \UXXXXXXXX (8 hex) -> UTF-8
          int want = (escaped == 'u') ? 4 : 8;
          uint32_t cp = 0;
          int n = 0;
          while (n < want && HexVal(current_char()) >= 0) {
            cp = cp * 16 + static_cast<uint32_t>(HexVal(current_char()));
            advance();
            n++;
          }
          if (n != want) { value += '\\'; value += escaped; }
          else AppendUtf8(value, cp);
          break;
        }
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
          // Octal \NNN - first digit is `escaped`, up to two more
          int v = escaped - '0', n = 1;
          while (n < 3 && current_char() >= '0' && current_char() <= '7') {
            v = v * 8 + (current_char() - '0');
            advance();
            n++;
          }
          value += static_cast<char>(v & 0xff);
          break;
        }
        default:
          value += '\\';
          value += escaped;
          break;
      }
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
