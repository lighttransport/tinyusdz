// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Lexer implementation

#include "lexer.hh"
#include "../strfmt.hh"
#include "simd-scan.hh"

#include <cctype>
#include <cstring>

namespace tinyusdz {
namespace next {

namespace {

// Inline ASCII character classes. USDA identifiers/numbers are ASCII; the
// locale-aware libc is* functions are real (PLT) calls on glibc and showed up
// hot in the lexer loops.
inline bool IsIdentifierStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool IsIdentifierContinue(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

inline bool IsDigit(char c) {
  return c >= '0' && c <= '9';
}

inline bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
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

TokenType LookupKeyword(std::string_view name) {
  // First-char dispatch: most identifiers (property/prim/type names) match no
  // keyword, so reject without touching the table when the first letter can't
  // start one. Keyword first letters: a c d f N o p r t u v.
  switch (name.empty() ? '\0' : name[0]) {
    case 'a': case 'c': case 'd': case 'f': case 'N': case 'o':
    case 'p': case 'r': case 't': case 'u': case 'v':
      break;
    default:
      return TokenType::Identifier;
  }
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
  // Raw pointer loop with explicit line/column bookkeeping — this runs before
  // every token, and advance()'s per-char function call + '\n' branch was
  // measurable on multi-GB inputs.
  const char* p = data_ + pos_;
  const char* const end = data_ + length_;
  size_t line = line_;
  size_t column = column_;
  for (;;) {
    if (p >= end) break;
    const char c = *p;
    if (c == ' ' || c == '\t' || c == '\r') {
      ++p;
      ++column;
    } else if (c == '\n') {
      ++p;
      ++line;
      column = 1;
    } else if (c == '#') {
      while (p < end && *p != '\n') ++p;  // comment: skip to end of line
      // column position within the comment is irrelevant (next iteration
      // handles the newline / EOF).
    } else {
      break;
    }
  }
  pos_ = static_cast<size_t>(p - data_);
  line_ = line;
  column_ = column;
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

bool Lexer::capture_bracketed_literal(const char** out_data, size_t* out_len,
                                      bool* out_simple, size_t* out_commas) {
  if (!out_data || !out_len) return false;
  const Token& tok = peek();
  if (tok.type != TokenType::OpenBracket) {
    set_error("Expected OpenBracket, got " + std::string(TokenTypeName(tok.type)));
    return false;
  }

  // `peek()` has scanned the '[' token, so pos_ is just after it.
  const size_t start = pos_ > 0 ? pos_ - 1 : 0;
  has_current_ = false;

  // "Simple" = no comment/string/asset/nested-bracket bytes inside, so every
  // comma/paren is a pure separator (safe to split for parallel numeric parse).
  bool simple = true;
  size_t commas = 0;
  int depth = 1;
  while (pos_ < length_ && depth > 0) {
    // SIMD-skip the "boring" array bytes (digits/commas/whitespace) straight to
    // the next structural byte {[ ] " ' @ #}; only those need per-char handling.
    // Equivalent to the old byte-by-byte advance() loop (same stop positions),
    // just far faster on the big numeric arrays that dominate flattened scenes.
    {
      const char* base = data_ + pos_;
      size_t nl = 0;
      const char* hit =
          simdscan::ScanArrayStructural(base, data_ + length_, &nl, &commas);
      pos_ += static_cast<size_t>(hit - base);
      line_ += nl;  // column_ left approximate inside arrays (error cosmetics)
      if (pos_ >= length_ || depth <= 0) break;
    }
    const char c = current_char();

    if (c == '#') {
      simple = false;  // comment bytes may contain commas/parens/']'
      skip_comment();
      continue;
    }

    if (c == '"' || c == '\'') {
      simple = false;  // string bytes may contain separators
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
      simple = false;  // asset-ref bytes may contain separators
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
      simple = false;  // nested array: separators are not flat
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
  if (out_simple) *out_simple = simple;
  if (out_commas) *out_commas = commas;
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
    // MUST copy, not move: parser code holds `const Token& tok = peek()`
    // references across the consuming next() call (moving out current_.value
    // was tried and broke five test suites).
    return current_;
  }
  return scan_token();
}

void Lexer::consume() {
  if (has_current_) {
    has_current_ = false;
    return;
  }
  (void)scan_token();
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
  // Identifier/Number/PathRef tokens carry their text as a view; String
  // tokens as owned (escape-processed) value.
  out_value = tok.text.empty() ? std::move(tok.value) : std::string(tok.text);
  return true;
}

void Lexer::set_error(const std::string& msg) {
  if (error_.empty()) {
    error_ = "Line " + UIntToStr(line_) + ", column " + UIntToStr(column_) + ": " + msg;
  }
}

Token Lexer::make_token(TokenType type, size_t start_line, size_t start_col) {
  Token tok;
  tok.type = type;
  tok.line = start_line;
  tok.column = start_col;
  return tok;
}

Token Lexer::make_token(TokenType type, std::string value, size_t start_line, size_t start_col) {
  Token tok;
  tok.type = type;
  tok.value = std::move(value);
  tok.line = start_line;
  tok.column = start_col;
  return tok;
}

Token Lexer::make_token(TokenType type, std::string_view text, size_t start_line, size_t start_col) {
  Token tok;
  tok.type = type;
  tok.text = text;
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
  const size_t start = pos_;

  // Identifiers contain no newline: scan with a raw pointer loop and bump
  // pos_/column_ in bulk (advance() pays a '\n' branch per char).
  const char* p = data_ + pos_;
  const char* const end = data_ + length_;
  while (p < end && IsIdentifierContinue(*p)) ++p;
  const size_t n = static_cast<size_t>(p - (data_ + start));
  pos_ += n;
  column_ += n;

  const std::string_view text(data_ + start, n);
  TokenType type = LookupKeyword(text);

  return make_token(type, text, start_line, start_col);
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
    return make_token(TokenType::Number,
                      std::string_view(data_ + start, pos_ - start),
                      start_line, start_col);
  }

  // Number text contains no newline: scan with a raw pointer and bump
  // pos_/column_ in bulk (advance() pays a '\n' branch per char).
  const char* p = data_ + pos_;
  const char* const end = data_ + length_;

  // Check for hex
  if (p < end && *p == '0' && p + 1 < end && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    while (p < end && IsHexDigit(*p)) ++p;
  } else {
    // Integer part
    while (p < end && IsDigit(*p)) ++p;

    // Decimal part
    if (p < end && *p == '.' && p + 1 < end && IsDigit(p[1])) {
      ++p;
      while (p < end && IsDigit(*p)) ++p;
    }

    // Exponent
    if (p < end && (*p == 'e' || *p == 'E')) {
      ++p;
      if (p < end && (*p == '+' || *p == '-')) ++p;
      while (p < end && IsDigit(*p)) ++p;
    }
  }

  const size_t n = static_cast<size_t>(p - (data_ + pos_));
  pos_ += n;
  column_ += n;

  return make_token(TokenType::Number,
                    std::string_view(data_ + start, pos_ - start),
                    start_line, start_col);
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

  return make_token(TokenType::String, std::move(value), start_line, start_col);
}

Token Lexer::scan_path_ref() {
  size_t start_line = line_;
  size_t start_col = column_;

  advance();  // '<'

  const size_t start = pos_;
  while (!at_end() && current_char() != '>') {
    advance();
  }
  const std::string_view text(data_ + start, pos_ - start);

  if (current_char() == '>') {
    advance();
  } else {
    set_error("Unterminated path reference");
  }

  return make_token(TokenType::PathRef, text, start_line, start_col);
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
  return make_token(TokenType::String, std::move(value), start_line, start_col);
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
