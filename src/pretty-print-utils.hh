// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Common pretty printing utilities
// Part of the pprinter.cc modularization effort

#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdint>

namespace tinyusdz {
namespace pprint {

// String manipulation utilities
namespace string_utils {

// Trimming
std::string TrimLeft(const std::string &str);
std::string TrimRight(const std::string &str);
std::string Trim(const std::string &str);

// Padding
std::string PadLeft(const std::string &str, size_t width, char pad = ' ');
std::string PadRight(const std::string &str, size_t width, char pad = ' ');
std::string Center(const std::string &str, size_t width, char pad = ' ');

// Case conversion
std::string ToLower(const std::string &str);
std::string ToUpper(const std::string &str);
std::string Capitalize(const std::string &str);
std::string CamelCaseToSnakeCase(const std::string &str);
std::string SnakeCaseToCamelCase(const std::string &str);

// Splitting and joining
std::vector<std::string> Split(const std::string &str, char delimiter);
std::vector<std::string> Split(const std::string &str, const std::string &delimiter);
std::vector<std::string> SplitLines(const std::string &str);
std::string Join(const std::vector<std::string> &parts, const std::string &delimiter);
std::string JoinLines(const std::vector<std::string> &lines);

// Replacement
std::string Replace(const std::string &str, 
                   const std::string &from, 
                   const std::string &to);
std::string ReplaceAll(const std::string &str,
                      const std::string &from,
                      const std::string &to);

// Checking
bool StartsWith(const std::string &str, const std::string &prefix);
bool EndsWith(const std::string &str, const std::string &suffix);
bool Contains(const std::string &str, const std::string &substring);
bool IsNumeric(const std::string &str);
bool IsAlpha(const std::string &str);
bool IsAlphanumeric(const std::string &str);
bool IsIdentifier(const std::string &str);

// Escaping and quoting
std::string Escape(const std::string &str);
std::string Unescape(const std::string &str);
std::string Quote(const std::string &str, const std::string &quote_char = "\"");
std::string SingleQuote(const std::string &str);
std::string DoubleQuote(const std::string &str);
std::string TripleQuote(const std::string &str);
std::string Unquote(const std::string &str);
std::string BackslashEscape(const std::string &str);
std::string BackslashUnescape(const std::string &str);

// Special characters
std::string EscapeSpecialChars(const std::string &str);
std::string EscapeNonPrintable(const std::string &str);
std::string EscapeForRegex(const std::string &str);
std::string EscapeForURL(const std::string &str);
std::string EscapeForHTML(const std::string &str);
std::string EscapeForXML(const std::string &str);
std::string EscapeForCSV(const std::string &str);

// Unicode handling
std::string UTF8ToHex(const std::string &str);
std::string HexToUTF8(const std::string &hex);
bool IsValidUTF8(const std::string &str);
size_t UTF8Length(const std::string &str);

} // namespace string_utils

// Formatting utilities
namespace format_utils {

// Text wrapping
std::string Wrap(const std::string &text, size_t width,
                const std::string &indent = "",
                const std::string &subsequent_indent = "");
std::vector<std::string> WrapLines(const std::string &text, size_t width);
std::string WrapComment(const std::string &comment, size_t width,
                       const std::string &prefix = "# ");

// Table formatting
class TableFormatter {
 public:
  TableFormatter();
  
  void AddColumn(const std::string &header, size_t width = 0);
  void AddRow(const std::vector<std::string> &row);
  void SetAlignment(size_t column, char align); // 'l', 'r', 'c'
  void SetBorder(bool has_border);
  void SetSeparator(const std::string &sep);
  
  std::string Format() const;
  std::string FormatMarkdown() const;
  std::string FormatHTML() const;
  
 private:
  std::vector<std::string> headers_;
  std::vector<size_t> widths_;
  std::vector<char> alignments_;
  std::vector<std::vector<std::string>> rows_;
  bool has_border_{true};
  std::string separator_{"|"};
};

// List formatting
std::string FormatBulletList(const std::vector<std::string> &items,
                            const std::string &bullet = "• ");
std::string FormatNumberedList(const std::vector<std::string> &items);
std::string FormatDefinitionList(
    const std::vector<std::pair<std::string, std::string>> &items);

// Tree formatting
class TreeFormatter {
 public:
  struct Node {
    std::string label;
    std::vector<Node> children;
    
    Node(const std::string &l) : label(l) {}
  };
  
  TreeFormatter();
  
  void SetStyle(const std::string &style); // "ascii", "unicode", "simple"
  void SetIndent(size_t indent);
  
  std::string Format(const Node &root) const;
  
 private:
  std::string style_{"unicode"};
  size_t indent_{2};
  
  std::string FormatNode(const Node &node, const std::string &prefix,
                         bool is_last) const;
};

// Diff formatting
std::string FormatDiff(const std::string &old_text,
                      const std::string &new_text);
std::string FormatUnifiedDiff(const std::string &old_text,
                             const std::string &new_text,
                             const std::string &old_name = "old",
                             const std::string &new_name = "new");

// Syntax highlighting (basic)
std::string HighlightKeywords(const std::string &text,
                             const std::vector<std::string> &keywords);
std::string HighlightNumbers(const std::string &text);
std::string HighlightStrings(const std::string &text);
std::string HighlightComments(const std::string &text);

} // namespace format_utils

// Number formatting utilities
namespace number_utils {

// Integer formatting
std::string FormatInt(int32_t value, int width = 0, char fill = ' ');
std::string FormatInt64(int64_t value, int width = 0, char fill = ' ');
std::string FormatUInt(uint32_t value, int width = 0, char fill = ' ');
std::string FormatUInt64(uint64_t value, int width = 0, char fill = ' ');

// Float formatting
std::string FormatFloat(float value, int precision = -1,
                       bool use_scientific = false);
std::string FormatDouble(double value, int precision = -1,
                        bool use_scientific = false);

// Special formatting
std::string FormatHex(uint32_t value, bool prefix = true, bool uppercase = false);
std::string FormatHex64(uint64_t value, bool prefix = true, bool uppercase = false);
std::string FormatBinary(uint32_t value, bool prefix = true);
std::string FormatBinary64(uint64_t value, bool prefix = true);
std::string FormatOctal(uint32_t value, bool prefix = true);
std::string FormatOctal64(uint64_t value, bool prefix = true);

// Human-readable formatting
std::string FormatBytes(uint64_t bytes, int precision = 2);
std::string FormatDuration(double seconds, bool compact = false);
std::string FormatPercentage(double value, int precision = 1);
std::string FormatOrdinal(int value);
std::string FormatRoman(int value);

// Number parsing
bool ParseInt(const std::string &str, int32_t *value);
bool ParseInt64(const std::string &str, int64_t *value);
bool ParseUInt(const std::string &str, uint32_t *value);
bool ParseUInt64(const std::string &str, uint64_t *value);
bool ParseFloat(const std::string &str, float *value);
bool ParseDouble(const std::string &str, double *value);

} // namespace number_utils

// Color utilities for terminal output
namespace color_utils {

enum Color {
  BLACK = 30,
  RED = 31,
  GREEN = 32,
  YELLOW = 33,
  BLUE = 34,
  MAGENTA = 35,
  CYAN = 36,
  WHITE = 37,
  DEFAULT = 39,
  
  // Bright colors
  BRIGHT_BLACK = 90,
  BRIGHT_RED = 91,
  BRIGHT_GREEN = 92,
  BRIGHT_YELLOW = 93,
  BRIGHT_BLUE = 94,
  BRIGHT_MAGENTA = 95,
  BRIGHT_CYAN = 96,
  BRIGHT_WHITE = 97
};

enum Style {
  RESET = 0,
  BOLD = 1,
  DIM = 2,
  ITALIC = 3,
  UNDERLINE = 4,
  BLINK = 5,
  REVERSE = 7,
  HIDDEN = 8,
  STRIKETHROUGH = 9
};

std::string Colorize(const std::string &text, Color color);
std::string Stylize(const std::string &text, Style style);
std::string ColorizeAndStylize(const std::string &text, Color color, Style style);

// Convenience functions
std::string Red(const std::string &text);
std::string Green(const std::string &text);
std::string Yellow(const std::string &text);
std::string Blue(const std::string &text);
std::string Magenta(const std::string &text);
std::string Cyan(const std::string &text);

std::string Bold(const std::string &text);
std::string Italic(const std::string &text);
std::string Underline(const std::string &text);

// Strip ANSI codes
std::string StripColors(const std::string &text);
std::string StripANSI(const std::string &text);

// Check terminal capability
bool IsTerminalColorSupported();
void EnableColors(bool enable);

} // namespace color_utils

// Performance utilities for fast printing
namespace perf_utils {

// Fast string builders
class FastStringBuilder {
 public:
  FastStringBuilder(size_t initial_capacity = 1024);
  ~FastStringBuilder();
  
  FastStringBuilder& Append(char c);
  FastStringBuilder& Append(const char *str);
  FastStringBuilder& Append(const std::string &str);
  FastStringBuilder& Append(const char *str, size_t len);
  FastStringBuilder& AppendInt(int32_t value);
  FastStringBuilder& AppendInt64(int64_t value);
  FastStringBuilder& AppendFloat(float value, int precision = -1);
  FastStringBuilder& AppendDouble(double value, int precision = -1);
  
  void Clear();
  void Reserve(size_t capacity);
  size_t Size() const { return size_; }
  size_t Capacity() const { return capacity_; }
  
  std::string ToString() const;
  const char* Data() const { return buffer_; }
  
 private:
  char *buffer_;
  size_t size_;
  size_t capacity_;
  
  void EnsureCapacity(size_t needed);
};

// Memory pool for string allocations
class StringPool {
 public:
  StringPool(size_t block_size = 4096);
  ~StringPool();
  
  char* Allocate(size_t size);
  void Clear();
  
 private:
  struct Block {
    char *data;
    size_t used;
    size_t capacity;
  };
  
  std::vector<Block> blocks_;
  size_t block_size_;
};

} // namespace perf_utils

} // namespace pprint
} // namespace tinyusdz