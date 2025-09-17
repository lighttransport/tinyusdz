// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Consolidated header for refactored ASCII parser modules
// This header brings together all the modularized components of ascii-parser.cc

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

// Core modules
#include "ascii-lexer.hh"
#include "ascii-error-handler.hh"
#include "ascii-expression-parser.hh"
#include "ascii-property-parser.hh"

// Forward declarations
namespace tinyusdz {

class Stage;
class Layer;
class Prim;
class PrimSpec;
class Property;
class Attribute;
class Relationship;

namespace value {
  class Value;
}

namespace ascii {

// Main parser configuration
struct ParserConfig {
  // Memory limits
  size_t max_memory_mb = 1024;  // 1GB default
  size_t max_depth = 256;
  
  // Parsing options
  bool strict_mode = false;
  bool allow_custom_schemas = true;
  bool validate_composition = true;
  
  // Error handling
  bool stop_on_first_error = false;
  bool collect_warnings = true;
  
  // Performance
  bool parallel_parsing = false;
  size_t thread_pool_size = 4;
};

// Parser statistics
struct ParserStats {
  size_t lines_parsed = 0;
  size_t prims_parsed = 0;
  size_t properties_parsed = 0;
  size_t time_samples_parsed = 0;
  size_t errors_encountered = 0;
  size_t warnings_encountered = 0;
  double parse_time_seconds = 0.0;
};

// Main ASCII parser class - refactored version
class AsciiParserRefactored {
public:
  AsciiParserRefactored();
  explicit AsciiParserRefactored(const ParserConfig &config);
  ~AsciiParserRefactored();
  
  // Main parsing interface
  bool Parse(const std::string &filename, Layer *layer, 
             std::string *warn = nullptr, std::string *err = nullptr);
  
  bool ParseFromString(const std::string &content, const std::string &base_dir,
                      Layer *layer, std::string *warn = nullptr, 
                      std::string *err = nullptr);
  
  // Configuration
  void SetConfig(const ParserConfig &config) { config_ = config; }
  const ParserConfig& GetConfig() const { return config_; }
  
  // Statistics
  const ParserStats& GetStats() const { return stats_; }
  void ResetStats() { stats_ = ParserStats(); }
  
  // Error handling
  bool HasErrors() const;
  bool HasWarnings() const;
  std::vector<ParseError> GetErrors() const;
  std::vector<ParseError> GetWarnings() const;
  
private:
  // Private implementation
  class Impl;
  std::unique_ptr<Impl> impl_;
  
  ParserConfig config_;
  ParserStats stats_;
  
  // Component parsers
  std::unique_ptr<AsciiLexer> lexer_;
  std::unique_ptr<AsciiErrorHandler> error_handler_;
  std::unique_ptr<ExpressionParser> expr_parser_;
  std::unique_ptr<PropertyParser> prop_parser_;
  
  // Internal parsing methods
  bool ParseHeader(Layer *layer);
  bool ParseStageMetadata(Layer *layer);
  bool ParsePrimSpec(PrimSpec *primspec);
  bool ParsePrim(Prim *prim);
  bool ParseProperty(Property *prop);
  bool ParseAttribute(Attribute *attr);
  bool ParseRelationship(Relationship *rel);
  bool ParseTimeSamples(value::Value *val);
  bool ParseExpression(value::Value *val);
  
  // Helper methods
  bool SkipComments();
  bool SkipWhitespace();
  bool ExpectToken(TokenType type);
  bool ConsumeToken(TokenType type);
  Token PeekToken();
  Token NextToken();
  
  // Error recovery
  bool RecoverFromError();
  bool SynchronizeToNextStatement();
  
  // Memory tracking
  bool CheckMemoryLimit();
  void UpdateMemoryUsage(size_t bytes);
  
  // Validation
  bool ValidatePrim(const Prim &prim);
  bool ValidateProperty(const Property &prop);
  bool ValidateComposition(const Layer &layer);
};

// Factory for creating parsers
class AsciiParserFactory {
public:
  static std::unique_ptr<AsciiParserRefactored> Create();
  static std::unique_ptr<AsciiParserRefactored> Create(const ParserConfig &config);
  
  // Create with specific components (for testing)
  static std::unique_ptr<AsciiParserRefactored> CreateWithComponents(
      std::unique_ptr<AsciiLexer> lexer,
      std::unique_ptr<AsciiErrorHandler> error_handler,
      std::unique_ptr<ExpressionParser> expr_parser,
      std::unique_ptr<PropertyParser> prop_parser);
};

// Convenience functions for backward compatibility
bool ParseUSDA(const std::string &filename, Stage *stage,
               std::string *warn = nullptr, std::string *err = nullptr);

bool ParseUSDAFromString(const std::string &content, const std::string &base_dir,
                        Stage *stage, std::string *warn = nullptr,
                        std::string *err = nullptr);

// Utility functions
std::string GetUSDAVersion(const std::string &filename);
bool ValidateUSDAFile(const std::string &filename, std::string *err = nullptr);
bool IsValidUSDAHeader(const std::string &header);

} // namespace ascii
} // namespace tinyusdz