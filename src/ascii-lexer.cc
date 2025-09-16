// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Lexical analysis implementation for USD ASCII parser

#include "ascii-lexer.hh"
#include <cctype>
#include <cstring>
#include <sstream>
#include "tiny-format.hh"
#include "str-util.hh"

namespace tinyusdz {
namespace ascii {

// USD ASCII keywords
static const std::unordered_set<std::string> kKeywords = {
    "def", "over", "class", "abstract",
    "uniform", "custom", "varying", "config",
    "add", "append", "prepend", "delete", "reorder",
    "references", "inherits", "specializes", "payload", "payloads",
    "variantSet", "variantSets", "variants",
    "timeSamples", "connect", "connections",
    "rel", "attribute", "attributes",
    "kind", "active", "hidden", "instanceable",
    "customData", "customLayerData",
    "subLayers", "defaultPrim", "upAxis", "metersPerUnit",
    "kilogramsPerUnit", "timeCodesPerSecond", "startTimeCode", "endTimeCode",
    "framesPerSecond", "autoPlay", "playbackMode",
    "doc", "documentation", "comment",
    "None", "true", "false"
};

bool AsciiLexer::Char1(char* c) {
  if (!c) {
    PushError("nullptr passed to Char1");
    return false;
  }

  if (!_sr->read1(reinterpret_cast<unsigned char*>(c))) {
    return false;
  }

  UpdateCursor(*c);
  return true;
}

bool AsciiLexer::Char(char expect_c) {
  char c;
  if (!Char1(&c)) {
    return false;
  }

  if (c != expect_c) {
    Rewind(1);
    return false;
  }

  return true;
}

bool AsciiLexer::Expect(char expect_c) {
  char c;
  if (!Char1(&c)) {
    PushError(fmt::format("Expected '{}' but got EOF", expect_c));
    return false;
  }

  if (c != expect_c) {
    PushError(fmt::format("Expected '{}' but got '{}'", expect_c, c));
    return false;
  }

  return true;
}

bool AsciiLexer::LookChar1(char* c) {
  if (!c) {
    PushError("nullptr passed to LookChar1");
    return false;
  }

  size_t pos;
  if (!Tell(&pos)) {
    return false;
  }

  if (!_sr->read1(reinterpret_cast<unsigned char*>(c))) {
    return false;
  }

  // Rewind to original position
  SeekTo(pos);
  return true;
}

bool AsciiLexer::Rewind(size_t offset) {
  int64_t pos = _sr->tell();
  if (pos < 0) {
    return false;
  }

  if (static_cast<size_t>(pos) < offset) {
    PushError("Cannot rewind past beginning of stream");
    return false;
  }

  return _sr->seek_set(static_cast<uint64_t>(pos - offset));
}

bool AsciiLexer::SeekTo(size_t pos) {
  return _sr->seek_set(static_cast<uint64_t>(pos));
}

bool AsciiLexer::Tell(size_t* pos) {
  if (!pos) {
    return false;
  }

  int64_t p = _sr->tell();
  if (p < 0) {
    return false;
  }

  *pos = static_cast<size_t>(p);
  return true;
}

bool AsciiLexer::SkipWhitespace() {
  char c;
  while (Char1(&c)) {
    if (IsWhitespace(c) && !IsNewline(c)) {
      continue;
    } else {
      Rewind(1);
      break;
    }
  }
  return true;
}

bool AsciiLexer::SkipWhitespaceAndNewline(bool allow_semicolon) {
  char c;
  while (Char1(&c)) {
    if (IsWhitespace(c) || IsNewline(c)) {
      continue;
    } else if (allow_semicolon && c == ';') {
      continue;
    } else {
      Rewind(1);
      break;
    }
  }
  return true;
}

bool AsciiLexer::SkipCommentAndWhitespaceAndNewline(bool allow_semicolon) {
  while (true) {
    SkipWhitespaceAndNewline(allow_semicolon);
    
    char c;
    if (!LookChar1(&c)) {
      break;
    }
    
    if (c == '#') {
      if (!ParseSharpComment()) {
        return false;
      }
    } else {
      break;
    }
  }
  return true;
}

bool AsciiLexer::SkipUntilNewline() {
  char c;
  while (Char1(&c)) {
    if (IsNewline(c)) {
      return true;
    }
  }
  return true;  // EOF is ok
}

bool AsciiLexer::ParseSharpComment() {
  if (!Char('#')) {
    return false;
  }
  
  return SkipUntilNewline();
}

bool AsciiLexer::ReadStringLiteral(std::string* literal) {
  if (!literal) {
    PushError("nullptr passed to ReadStringLiteral");
    return false;
  }

  literal->clear();
  
  // Check for triple-quoted string first
  size_t pos;
  if (!Tell(&pos)) {
    return false;
  }
  
  char c1, c2, c3;
  if (Char1(&c1) && Char1(&c2) && Char1(&c3)) {
    if (c1 == '"' && c2 == '"' && c3 == '"') {
      // Triple-quoted string
      return ReadTripleQuotedString(literal);
    }
  }
  
  // Not triple-quoted, rewind
  SeekTo(pos);
  
  // Regular string
  if (!Char('"')) {
    return false;
  }
  
  std::stringstream ss;
  char c;
  bool escaped = false;
  
  while (Char1(&c)) {
    if (escaped) {
      switch (c) {
        case 'n': ss << '\n'; break;
        case 't': ss << '\t'; break;
        case 'r': ss << '\r'; break;
        case '\\': ss << '\\'; break;
        case '"': ss << '"'; break;
        default: ss << c; break;
      }
      escaped = false;
    } else {
      if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        *literal = ss.str();
        
        // Check memory usage
        if (!CheckMemoryUsage(literal->size())) {
          return false;
        }
        
        return true;
      } else {
        ss << c;
      }
    }
  }
  
  PushError("Unterminated string literal");
  return false;
}

bool AsciiLexer::ReadIdentifier(std::string* token) {
  if (!token) {
    PushError("nullptr passed to ReadIdentifier");
    return false;
  }

  token->clear();
  
  char c;
  if (!LookChar1(&c)) {
    return false;
  }
  
  // First character must be alpha or underscore
  if (!IsAlpha(c) && c != '_') {
    return false;
  }
  
  std::stringstream ss;
  
  while (Char1(&c)) {
    if (IsAlphaNumeric(c) || c == '_' || c == ':') {
      ss << c;
    } else {
      Rewind(1);
      break;
    }
  }
  
  *token = ss.str();
  
  if (token->empty()) {
    return false;
  }
  
  // Check memory usage
  if (!CheckMemoryUsage(token->size())) {
    return false;
  }
  
  return true;
}

bool AsciiLexer::ReadPathIdentifier(std::string* path_identifier) {
  if (!path_identifier) {
    PushError("nullptr passed to ReadPathIdentifier");
    return false;
  }

  path_identifier->clear();
  
  // Path can start with '/' or '<' or be a regular identifier
  char c;
  if (!LookChar1(&c)) {
    return false;
  }
  
  if (c == '/') {
    // Absolute path
    std::stringstream ss;
    
    while (Char1(&c)) {
      if (IsAlphaNumeric(c) || c == '_' || c == '/' || c == '.' || c == '-') {
        ss << c;
      } else {
        Rewind(1);
        break;
      }
    }
    
    *path_identifier = ss.str();
  } else if (c == '<') {
    // Reference path like </Model/Geom>
    Char1(&c);  // consume '<'
    std::stringstream ss;
    ss << '<';
    
    while (Char1(&c)) {
      ss << c;
      if (c == '>') {
        break;
      }
    }
    
    *path_identifier = ss.str();
  } else {
    // Regular identifier or relative path
    return ReadIdentifier(path_identifier);
  }
  
  // Check memory usage
  if (!CheckMemoryUsage(path_identifier->size())) {
    return false;
  }
  
  return !path_identifier->empty();
}

bool AsciiLexer::ReadPrimAttrIdentifier(std::string* token) {
  // For now, same as ReadIdentifier but may have different rules
  return ReadIdentifier(token);
}

bool AsciiLexer::ReadUntilNewline(std::string* str) {
  if (!str) {
    PushError("nullptr passed to ReadUntilNewline");
    return false;
  }

  str->clear();
  std::stringstream ss;
  
  char c;
  while (Char1(&c)) {
    if (IsNewline(c)) {
      break;
    }
    ss << c;
  }
  
  *str = ss.str();
  
  // Check memory usage
  if (!CheckMemoryUsage(str->size())) {
    return false;
  }
  
  return true;
}

bool AsciiLexer::ReadNumber(double* value) {
  if (!value) {
    PushError("nullptr passed to ReadNumber");
    return false;
  }

  std::stringstream ss;
  char c;
  bool has_digit = false;
  bool has_dot = false;
  bool has_exp = false;
  
  // Optional sign
  if (LookChar1(&c) && (c == '+' || c == '-')) {
    Char1(&c);
    ss << c;
  }
  
  // Read digits and decimal point
  while (LookChar1(&c)) {
    if (IsDigit(c)) {
      Char1(&c);
      ss << c;
      has_digit = true;
    } else if (c == '.' && !has_dot && !has_exp) {
      Char1(&c);
      ss << c;
      has_dot = true;
    } else if ((c == 'e' || c == 'E') && has_digit && !has_exp) {
      Char1(&c);
      ss << c;
      has_exp = true;
      
      // Optional exponent sign
      if (LookChar1(&c) && (c == '+' || c == '-')) {
        Char1(&c);
        ss << c;
      }
    } else {
      break;
    }
  }
  
  if (!has_digit) {
    return false;
  }
  
  std::string num_str = ss.str();
  char* endptr;
  *value = std::strtod(num_str.c_str(), &endptr);
  
  if (endptr != num_str.c_str() + num_str.size()) {
    return false;
  }
  
  return true;
}

bool AsciiLexer::ReadTripleQuotedString(std::string* str) {
  if (!str) {
    return false;
  }

  str->clear();
  std::stringstream ss;
  
  // Already consumed """
  char c1, c2, c3;
  while (true) {
    if (!Char1(&c1)) {
      PushError("Unterminated triple-quoted string");
      return false;
    }
    
    if (c1 == '"') {
      if (Char1(&c2) && c2 == '"') {
        if (Char1(&c3) && c3 == '"') {
          // Found closing """
          *str = ss.str();
          
          // Check memory usage
          if (!CheckMemoryUsage(str->size())) {
            return false;
          }
          
          return true;
        } else {
          ss << c1 << c2 << c3;
        }
      } else {
        ss << c1 << c2;
      }
    } else {
      ss << c1;
    }
  }
}

bool AsciiLexer::IsKeyword(const std::string& str) const {
  return kKeywords.find(str) != kKeywords.end();
}

bool AsciiLexer::IsReservedKeyword(const std::string& str) const {
  // Additional reserved keywords that shouldn't be used as identifiers
  static const std::unordered_set<std::string> kReserved = {
      "namespace", "using", "typedef", "struct", "enum", "union",
      "if", "else", "for", "while", "do", "switch", "case",
      "return", "break", "continue", "goto"
  };
  
  return kReserved.find(str) != kReserved.end();
}

bool AsciiLexer::IsValidIdentifier(const std::string& str) const {
  if (str.empty()) return false;
  if (IsKeyword(str)) return false;
  if (IsReservedKeyword(str)) return false;
  
  // Check first character
  if (!IsAlpha(str[0]) && str[0] != '_') {
    return false;
  }
  
  // Check remaining characters
  for (size_t i = 1; i < str.size(); ++i) {
    if (!IsAlphaNumeric(str[i]) && str[i] != '_' && str[i] != ':') {
      return false;
    }
  }
  
  return true;
}

size_t AsciiLexer::GetCurrentPosition() const {
  int64_t pos = _sr->tell();
  return (pos >= 0) ? static_cast<size_t>(pos) : 0;
}

bool AsciiLexer::CheckMemoryUsage(size_t nbytes) {
  _memory_usage += nbytes;
  if (_memory_usage > _max_memory_limit_bytes) {
    PushError(fmt::format("Memory limit exceeded. Limit: {} MB, Current usage: {} MB",
                         _max_memory_limit_bytes / (1024*1024),
                         _memory_usage / (1024*1024)));
    return false;
  }
  return true;
}

void AsciiLexer::UpdateCursor(char c) {
  if (c == '\n') {
    _cursor.row++;
    _cursor.col = 0;
  } else if (c == '\r') {
    // Handle \r\n as single newline
  } else {
    _cursor.col++;
  }
}

bool AsciiLexer::IsWhitespace(char c) const {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool AsciiLexer::IsNewline(char c) const {
  return c == '\n' || c == '\r';
}

bool AsciiLexer::IsDigit(char c) const {
  return c >= '0' && c <= '9';
}

bool AsciiLexer::IsAlpha(char c) const {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool AsciiLexer::IsAlphaNumeric(char c) const {
  return IsAlpha(c) || IsDigit(c);
}

}  // namespace ascii
}  // namespace tinyusdz