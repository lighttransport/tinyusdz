// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Core pretty printer interface for TinyUSDZ
// Part of the pprinter.cc modularization effort

#pragma once

#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <cstdint>
#include <iomanip>

namespace tinyusdz {

// Forward declarations
class Layer;
class Stage;
class Prim;
class PrimSpec;
class Property;
class Attribute;
class Relationship;
struct PrimMeta;
struct AttrMetas;  // Use the actual type name
using AttrMeta = AttrMetas;  // Create alias for compatibility
namespace value {
  class Value;
}

namespace pprint {

// Configuration for pretty printing
struct PrintConfig {
  // Indentation
  std::string indent_string = "    ";
  uint32_t initial_indent = 0;
  uint32_t max_line_width = 120;
  
  // Formatting options
  bool use_scientific_notation = false;
  bool print_defaults = false;
  bool print_hidden = false;
  bool print_custom_data = true;
  bool print_composition_arcs = true;
  bool print_metadata = true;
  bool print_time_samples = true;
  bool sort_properties = true;  // Lexicographically sort
  
  // Float/double precision
  int float_precision = 6;
  int double_precision = 15;
  
  // Output format
  enum Format {
    USDA,     // USD ASCII format
    JSON,     // JSON format
    XML,      // XML format (future)
    COMPACT   // Compact single-line format
  };
  Format format = USDA;
  
  // Verbosity level
  enum Verbosity {
    MINIMAL,   // Only essential information
    NORMAL,    // Default level
    VERBOSE,   // Include all details
    DEBUG      // Include internal details
  };
  Verbosity verbosity = NORMAL;
};

// Base interface for formatters
class Formatter {
 public:
  virtual ~Formatter() = default;
  
  // Configuration
  virtual void SetConfig(const PrintConfig &config) { config_ = config; }
  virtual const PrintConfig& GetConfig() const { return config_; }
  
  // Main formatting methods
  virtual std::string Format(const Layer &layer) = 0;
  virtual std::string Format(const Stage &stage) = 0;
  virtual std::string Format(const Prim &prim, uint32_t indent = 0) = 0;
  virtual std::string Format(const PrimSpec &primspec, uint32_t indent = 0) = 0;
  virtual std::string Format(const Property &prop, uint32_t indent = 0) = 0;
  virtual std::string Format(const Attribute &attr, uint32_t indent = 0) = 0;
  virtual std::string Format(const Relationship &rel, uint32_t indent = 0) = 0;
  
  // Value formatting
  virtual std::string FormatValue(const value::Value &val, uint32_t indent = 0) = 0;
  
  // Metadata formatting
  virtual std::string FormatPrimMeta(const PrimMeta &meta, uint32_t indent = 0) = 0;
  virtual std::string FormatAttrMeta(const AttrMeta &meta, uint32_t indent = 0) = 0;
  
 protected:
  PrintConfig config_;
  
  // Helper methods
  std::string Indent(uint32_t level) const;
  std::string Quote(const std::string &str) const;
  std::string PathQuote(const std::string &path) const;
};

// Factory for creating formatters
class FormatterFactory {
 public:
  static std::unique_ptr<Formatter> Create(PrintConfig::Format format);
  static std::unique_ptr<Formatter> CreateUSDA();
  static std::unique_ptr<Formatter> CreateJSON();
  static std::unique_ptr<Formatter> CreateCompact();
};

// Global indentation utilities
std::string Indent(uint32_t n);
void SetIndentString(const std::string &s);
const std::string& GetIndentString();

// Quoting utilities
std::string Quote(const std::string &str);
std::string SingleQuote(const std::string &str);
std::string TripleQuote(const std::string &str);
std::string PathQuote(const std::string &path);
std::string AngleBracketQuote(const std::string &str);

// Number formatting utilities
std::string FormatFloat(float value, int precision = -1);
std::string FormatDouble(double value, int precision = -1);
std::string FormatInt(int32_t value);
std::string FormatInt64(int64_t value);
std::string FormatUInt(uint32_t value);
std::string FormatUInt64(uint64_t value);
std::string FormatBool(bool value);

// String escaping utilities
std::string EscapeString(const std::string &str);
std::string UnescapeString(const std::string &str);
std::string EscapeForUSDA(const std::string &str);
std::string EscapeForJSON(const std::string &str);

// Line wrapping utilities
std::string WrapLine(const std::string &text, size_t max_width, 
                    const std::string &indent = "");
std::vector<std::string> WrapLines(const std::string &text, size_t max_width);

// Comment formatting
std::string FormatComment(const std::string &comment, uint32_t indent = 0);
std::string FormatMultilineComment(const std::string &comment, uint32_t indent = 0);
std::string FormatDocString(const std::string &doc, uint32_t indent = 0);

// Pretty printer options (backward compatibility)
struct Options {
  uint32_t indent = 0;
  bool sorted = true;
  
  Options() = default;
  Options(uint32_t ind) : indent(ind) {}
  Options(uint32_t ind, bool srt) : indent(ind), sorted(srt) {}
  
  // Convert to PrintConfig
  PrintConfig ToConfig() const {
    PrintConfig config;
    config.initial_indent = indent;
    config.sort_properties = sorted;
    return config;
  }
};

} // namespace pprint
} // namespace tinyusdz