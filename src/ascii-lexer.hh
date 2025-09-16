// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Lexical analysis module for USD ASCII parser
#pragma once

#include <string>
#include <functional>
#include "stream-reader.hh"
#include "nonstd/optional.hpp"

namespace tinyusdz {
namespace ascii {

///
/// Cursor position tracking for error reporting
///
struct Cursor {
  int row{0};
  int col{0};
};

///
/// Lexical analyzer for USD ASCII format.
/// Handles tokenization, whitespace, comments, and basic lexical elements.
///
class AsciiLexer {
 public:
  AsciiLexer(StreamReader* sr, size_t max_memory_limit)
      : _sr(sr), _max_memory_limit_bytes(max_memory_limit) {}

  // Basic character operations
  bool Char1(char* c);
  bool Char(char expect_c);
  bool Expect(char expect_c);
  bool LookChar1(char* c);
  bool Rewind(size_t offset);
  bool SeekTo(size_t pos);
  bool Tell(size_t* pos);
  
  // Whitespace and comment handling
  bool SkipWhitespace();
  bool SkipWhitespaceAndNewline(bool allow_semicolon = false);
  bool SkipCommentAndWhitespaceAndNewline(bool allow_semicolon = false);
  bool SkipUntilNewline();
  bool ParseSharpComment();
  
  // String and identifier reading
  bool ReadStringLiteral(std::string* literal);
  bool ReadIdentifier(std::string* token);
  bool ReadPathIdentifier(std::string* path_identifier);
  bool ReadPrimAttrIdentifier(std::string* token);
  bool ReadUntilNewline(std::string* str);
  
  // Number parsing
  bool ReadNumber(double* value);
  bool ReadInteger(int64_t* value);
  bool ReadUnsignedInteger(uint64_t* value);
  bool ReadFloat(float* value);
  bool ReadDouble(double* value);
  
  // Token classification
  bool IsKeyword(const std::string& str) const;
  bool IsReservedKeyword(const std::string& str) const;
  bool IsValidIdentifier(const std::string& str) const;
  
  // Triple-quoted string handling
  bool ReadTripleQuotedString(std::string* str);
  
  // Position and cursor tracking
  Cursor GetCursor() const { return _cursor; }
  size_t GetCurrentPosition() const;
  
  // Memory tracking
  bool CheckMemoryUsage(size_t nbytes);
  size_t GetMemoryUsage() const { return _memory_usage; }
  
  // Error handling
  void PushError(const std::string& msg) { _err += msg + "\n"; }
  void PushWarn(const std::string& msg) { _warn += msg + "\n"; }
  std::string GetError() const { return _err; }
  std::string GetWarning() const { return _warn; }
  void ClearError() { _err.clear(); }
  void ClearWarning() { _warn.clear(); }

 private:
  StreamReader* _sr;
  Cursor _cursor;
  
  // Memory management
  size_t _memory_usage{0};
  size_t _max_memory_limit_bytes;
  
  // Error messages
  std::string _err;
  std::string _warn;
  
  // Helper methods
  void UpdateCursor(char c);
  bool IsWhitespace(char c) const;
  bool IsNewline(char c) const;
  bool IsDigit(char c) const;
  bool IsAlpha(char c) const;
  bool IsAlphaNumeric(char c) const;
};

}  // namespace ascii
}  // namespace tinyusdz