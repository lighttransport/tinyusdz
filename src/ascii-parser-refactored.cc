// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Refactored ASCII parser implementation
// Integrates modular components from ascii-parser decomposition

#include "ascii-parser-refactored.hh"
#include "layer.hh"
#include "stage.hh"
#include "prim.hh"
#include "value-types.hh"
#include "composition.hh"
#include "io-util.hh"

#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace tinyusdz {
namespace ascii {

// Private implementation class
class AsciiParserRefactored::Impl {
public:
  Impl(AsciiParserRefactored *parent) : parent_(parent) {}
  
  // Stream management
  std::ifstream file_stream_;
  std::istringstream string_stream_;
  std::istream *current_stream_ = nullptr;
  
  // Parser state
  size_t current_line_ = 1;
  size_t current_column_ = 1;
  size_t memory_usage_ = 0;
  size_t parse_depth_ = 0;
  
  // Base directory for asset resolution
  std::string base_dir_;
  
  // Current parsing context
  std::vector<std::string> context_stack_;
  
  // Parent parser
  AsciiParserRefactored *parent_;
  
  void PushContext(const std::string &ctx) {
    context_stack_.push_back(ctx);
  }
  
  void PopContext() {
    if (!context_stack_.empty()) {
      context_stack_.pop_back();
    }
  }
  
  std::string GetContext() const {
    std::stringstream ss;
    for (size_t i = 0; i < context_stack_.size(); ++i) {
      if (i > 0) ss << " > ";
      ss << context_stack_[i];
    }
    return ss.str();
  }
};

// Constructor
AsciiParserRefactored::AsciiParserRefactored() 
    : impl_(std::make_unique<Impl>(this)) {
  // Initialize component parsers
  lexer_ = std::make_unique<AsciiLexer>();
  error_handler_ = std::make_unique<AsciiErrorHandler>();
  expr_parser_ = std::make_unique<ExpressionParser>();
  prop_parser_ = std::make_unique<PropertyParser>();
}

AsciiParserRefactored::AsciiParserRefactored(const ParserConfig &config)
    : config_(config), impl_(std::make_unique<Impl>(this)) {
  // Initialize component parsers with config
  lexer_ = std::make_unique<AsciiLexer>();
  error_handler_ = std::make_unique<AsciiErrorHandler>();
  expr_parser_ = std::make_unique<ExpressionParser>();
  prop_parser_ = std::make_unique<PropertyParser>();
}

AsciiParserRefactored::~AsciiParserRefactored() = default;

// Main parsing interface
bool AsciiParserRefactored::Parse(const std::string &filename, Layer *layer,
                                  std::string *warn, std::string *err) {
  if (!layer) {
    if (err) *err = "Null layer pointer";
    return false;
  }
  
  // Open file
  impl_->file_stream_.open(filename, std::ios::binary);
  if (!impl_->file_stream_.is_open()) {
    if (err) *err = "Failed to open file: " + filename;
    return false;
  }
  
  impl_->current_stream_ = &impl_->file_stream_;
  impl_->base_dir_ = io::GetBaseDir(filename);
  
  // Start timing
  auto start_time = std::chrono::high_resolution_clock::now();
  
  // Initialize lexer with stream
  lexer_->Initialize(impl_->current_stream_);
  
  // Parse header
  if (!ParseHeader(layer)) {
    if (err) *err = error_handler_->GetLastError().message;
    return false;
  }
  
  // Parse stage metadata
  if (!ParseStageMetadata(layer)) {
    if (err) *err = error_handler_->GetLastError().message;
    return false;
  }
  
  // Parse prim specs
  while (!lexer_->IsEOF()) {
    // Skip whitespace and comments
    SkipWhitespace();
    SkipComments();
    
    if (lexer_->IsEOF()) break;
    
    // Parse next prim spec
    PrimSpec primspec;
    if (!ParsePrimSpec(&primspec)) {
      if (config_.stop_on_first_error) {
        if (err) *err = error_handler_->GetLastError().message;
        return false;
      }
      // Try to recover
      if (!RecoverFromError()) {
        break;
      }
      continue;
    }
    
    // Add to layer
    layer->add_primspec(primspec.name(), primspec);
    stats_.prims_parsed++;
  }
  
  // Calculate parse time
  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end_time - start_time;
  stats_.parse_time_seconds = diff.count();
  
  // Validate if required
  if (config_.validate_composition) {
    if (!ValidateComposition(*layer)) {
      if (err) *err = "Composition validation failed";
      return false;
    }
  }
  
  // Collect warnings and errors
  if (warn) {
    std::stringstream ss;
    for (const auto &w : GetWarnings()) {
      ss << w.ToString() << "\n";
    }
    *warn = ss.str();
  }
  
  if (err && HasErrors()) {
    std::stringstream ss;
    for (const auto &e : GetErrors()) {
      ss << e.ToString() << "\n";
    }
    *err = ss.str();
  }
  
  return !HasErrors();
}

bool AsciiParserRefactored::ParseFromString(const std::string &content, 
                                           const std::string &base_dir,
                                           Layer *layer,
                                           std::string *warn, std::string *err) {
  if (!layer) {
    if (err) *err = "Null layer pointer";
    return false;
  }
  
  impl_->string_stream_.str(content);
  impl_->current_stream_ = &impl_->string_stream_;
  impl_->base_dir_ = base_dir;
  
  // Rest is same as Parse()
  auto start_time = std::chrono::high_resolution_clock::now();
  
  lexer_->Initialize(impl_->current_stream_);
  
  if (!ParseHeader(layer)) {
    if (err) *err = error_handler_->GetLastError().message;
    return false;
  }
  
  if (!ParseStageMetadata(layer)) {
    if (err) *err = error_handler_->GetLastError().message;
    return false;
  }
  
  while (!lexer_->IsEOF()) {
    SkipWhitespace();
    SkipComments();
    
    if (lexer_->IsEOF()) break;
    
    PrimSpec primspec;
    if (!ParsePrimSpec(&primspec)) {
      if (config_.stop_on_first_error) {
        if (err) *err = error_handler_->GetLastError().message;
        return false;
      }
      if (!RecoverFromError()) {
        break;
      }
      continue;
    }
    
    layer->add_primspec(primspec.name(), primspec);
    stats_.prims_parsed++;
  }
  
  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end_time - start_time;
  stats_.parse_time_seconds = diff.count();
  
  if (config_.validate_composition) {
    if (!ValidateComposition(*layer)) {
      if (err) *err = "Composition validation failed";
      return false;
    }
  }
  
  if (warn) {
    std::stringstream ss;
    for (const auto &w : GetWarnings()) {
      ss << w.ToString() << "\n";
    }
    *warn = ss.str();
  }
  
  if (err && HasErrors()) {
    std::stringstream ss;
    for (const auto &e : GetErrors()) {
      ss << e.ToString() << "\n";
    }
    *err = ss.str();
  }
  
  return !HasErrors();
}

// Parse header
bool AsciiParserRefactored::ParseHeader(Layer *layer) {
  impl_->PushContext("header");
  
  // Expect "#usda 1.0"
  Token tok = NextToken();
  if (tok.type != TokenType::USDA_HEADER) {
    error_handler_->AddError(ParseError(
      ParseError::INVALID_HEADER,
      "Expected '#usda 1.0' header",
      ErrorLocation(impl_->current_line_, impl_->current_column_)
    ));
    impl_->PopContext();
    return false;
  }
  
  // Extract version
  std::string version = "1.0";  // Default
  if (tok.value.find("1.0") != std::string::npos) {
    version = "1.0";
  }
  layer->set_version(version);
  
  stats_.lines_parsed++;
  impl_->PopContext();
  return true;
}

// Parse stage metadata
bool AsciiParserRefactored::ParseStageMetadata(Layer *layer) {
  impl_->PushContext("stage_metadata");
  
  // Look for opening paren
  Token tok = PeekToken();
  if (tok.type != TokenType::LPAREN) {
    // No stage metadata
    impl_->PopContext();
    return true;
  }
  
  ConsumeToken(TokenType::LPAREN);
  
  // Parse metadata entries
  while (true) {
    tok = PeekToken();
    if (tok.type == TokenType::RPAREN) {
      ConsumeToken(TokenType::RPAREN);
      break;
    }
    
    if (tok.type != TokenType::IDENTIFIER) {
      error_handler_->AddError(ParseError(
        ParseError::UNEXPECTED_TOKEN,
        "Expected metadata key",
        ErrorLocation(impl_->current_line_, impl_->current_column_)
      ));
      impl_->PopContext();
      return false;
    }
    
    std::string key = tok.value;
    ConsumeToken(TokenType::IDENTIFIER);
    
    if (!ExpectToken(TokenType::EQUALS)) {
      impl_->PopContext();
      return false;
    }
    
    // Parse metadata value
    value::Value val;
    if (!ParseExpression(&val)) {
      impl_->PopContext();
      return false;
    }
    
    // Set layer metadata
    if (key == "defaultPrim") {
      if (auto str = val.as<std::string>()) {
        layer->set_defaultPrim(*str);
      }
    } else if (key == "startTimeCode") {
      if (auto num = val.as<double>()) {
        layer->set_startTimeCode(*num);
      }
    } else if (key == "endTimeCode") {
      if (auto num = val.as<double>()) {
        layer->set_endTimeCode(*num);
      }
    } else if (key == "framesPerSecond") {
      if (auto num = val.as<double>()) {
        layer->set_framesPerSecond(*num);
      }
    } else if (key == "timeCodesPerSecond") {
      if (auto num = val.as<double>()) {
        layer->set_timeCodesPerSecond(*num);
      }
    } else {
      // Custom metadata
      layer->add_meta(key, val);
    }
  }
  
  impl_->PopContext();
  return true;
}

// Parse prim spec
bool AsciiParserRefactored::ParsePrimSpec(PrimSpec *primspec) {
  impl_->PushContext("primspec");
  
  // Parse specifier (def, over, class)
  Token tok = NextToken();
  Specifier spec = Specifier::Def;
  
  if (tok.type == TokenType::DEF) {
    spec = Specifier::Def;
  } else if (tok.type == TokenType::OVER) {
    spec = Specifier::Over;
  } else if (tok.type == TokenType::CLASS) {
    spec = Specifier::Class;
  } else {
    error_handler_->AddError(ParseError(
      ParseError::UNEXPECTED_TOKEN,
      "Expected prim specifier (def, over, class)",
      ErrorLocation(impl_->current_line_, impl_->current_column_)
    ));
    impl_->PopContext();
    return false;
  }
  
  primspec->set_specifier(spec);
  
  // Parse optional type name
  tok = PeekToken();
  if (tok.type == TokenType::IDENTIFIER) {
    primspec->set_typeName(tok.value);
    ConsumeToken(TokenType::IDENTIFIER);
  }
  
  // Parse prim name
  tok = NextToken();
  if (tok.type != TokenType::STRING) {
    error_handler_->AddError(ParseError(
      ParseError::UNEXPECTED_TOKEN,
      "Expected prim name string",
      ErrorLocation(impl_->current_line_, impl_->current_column_)
    ));
    impl_->PopContext();
    return false;
  }
  
  primspec->set_name(tok.value);
  
  // Parse optional composition metadata
  tok = PeekToken();
  if (tok.type == TokenType::LPAREN) {
    ConsumeToken(TokenType::LPAREN);
    
    // Parse composition arcs (inherits, references, etc.)
    while (true) {
      tok = PeekToken();
      if (tok.type == TokenType::RPAREN) {
        ConsumeToken(TokenType::RPAREN);
        break;
      }
      
      // Parse composition arc
      // TODO: Implement composition arc parsing
      
      // For now, skip to closing paren
      if (!SynchronizeToNextStatement()) {
        impl_->PopContext();
        return false;
      }
    }
  }
  
  // Parse prim body
  if (!ExpectToken(TokenType::LBRACE)) {
    impl_->PopContext();
    return false;
  }
  
  // Parse properties and child prims
  while (true) {
    tok = PeekToken();
    if (tok.type == TokenType::RBRACE) {
      ConsumeToken(TokenType::RBRACE);
      break;
    }
    
    // Parse property or child prim
    if (tok.type == TokenType::DEF || tok.type == TokenType::OVER || 
        tok.type == TokenType::CLASS) {
      // Child prim
      PrimSpec child;
      if (!ParsePrimSpec(&child)) {
        if (!RecoverFromError()) {
          impl_->PopContext();
          return false;
        }
        continue;
      }
      // Add child to primspec
      // Note: PrimSpec doesn't have children in the current implementation
      // This would need to be handled differently
    } else {
      // Property
      Property prop;
      if (!ParseProperty(&prop)) {
        if (!RecoverFromError()) {
          impl_->PopContext();
          return false;
        }
        continue;
      }
      primspec->add_property(prop.name(), prop);
      stats_.properties_parsed++;
    }
  }
  
  impl_->PopContext();
  return true;
}

// Parse property
bool AsciiParserRefactored::ParseProperty(Property *prop) {
  // Delegate to PropertyParser
  return prop_parser_->ParseProperty(lexer_.get(), error_handler_.get(), prop);
}

// Parse expression
bool AsciiParserRefactored::ParseExpression(value::Value *val) {
  // Delegate to ExpressionParser
  return expr_parser_->ParseExpression(lexer_.get(), error_handler_.get(), val);
}

// Helper methods
bool AsciiParserRefactored::SkipComments() {
  while (true) {
    Token tok = PeekToken();
    if (tok.type != TokenType::COMMENT) {
      break;
    }
    ConsumeToken(TokenType::COMMENT);
    stats_.lines_parsed++;
  }
  return true;
}

bool AsciiParserRefactored::SkipWhitespace() {
  // Lexer handles whitespace internally
  return true;
}

bool AsciiParserRefactored::ExpectToken(TokenType type) {
  Token tok = NextToken();
  if (tok.type != type) {
    error_handler_->AddError(ParseError(
      ParseError::UNEXPECTED_TOKEN,
      "Expected " + TokenTypeToString(type) + ", got " + TokenTypeToString(tok.type),
      ErrorLocation(impl_->current_line_, impl_->current_column_)
    ));
    return false;
  }
  return true;
}

bool AsciiParserRefactored::ConsumeToken(TokenType type) {
  Token tok = NextToken();
  return tok.type == type;
}

Token AsciiParserRefactored::PeekToken() {
  return lexer_->PeekToken();
}

Token AsciiParserRefactored::NextToken() {
  Token tok = lexer_->NextToken();
  if (tok.type == TokenType::NEWLINE) {
    impl_->current_line_++;
    impl_->current_column_ = 1;
  } else {
    impl_->current_column_ += tok.value.length();
  }
  return tok;
}

// Error recovery
bool AsciiParserRefactored::RecoverFromError() {
  error_handler_->IncrementErrorCount();
  stats_.errors_encountered++;
  
  // Try to synchronize to next statement
  return SynchronizeToNextStatement();
}

bool AsciiParserRefactored::SynchronizeToNextStatement() {
  // Skip tokens until we find a statement boundary
  while (!lexer_->IsEOF()) {
    Token tok = NextToken();
    if (tok.type == TokenType::SEMICOLON ||
        tok.type == TokenType::RBRACE ||
        tok.type == TokenType::NEWLINE) {
      return true;
    }
  }
  return false;
}

// Memory tracking
bool AsciiParserRefactored::CheckMemoryLimit() {
  if (config_.max_memory_mb > 0) {
    size_t limit_bytes = config_.max_memory_mb * 1024 * 1024;
    if (impl_->memory_usage_ > limit_bytes) {
      error_handler_->AddError(ParseError(
        ParseError::MEMORY_LIMIT_EXCEEDED,
        "Memory limit exceeded",
        ErrorLocation(impl_->current_line_, impl_->current_column_)
      ));
      return false;
    }
  }
  return true;
}

void AsciiParserRefactored::UpdateMemoryUsage(size_t bytes) {
  impl_->memory_usage_ += bytes;
}

// Validation
bool AsciiParserRefactored::ValidatePrim(const Prim &prim) {
  // Basic validation
  if (prim.name().empty()) {
    return false;
  }
  
  // Check for invalid characters in name
  const std::string &name = prim.name();
  if (name[0] >= '0' && name[0] <= '9') {
    // Names cannot start with numbers
    return false;
  }
  
  return true;
}

bool AsciiParserRefactored::ValidateProperty(const Property &prop) {
  // Basic validation
  if (prop.name().empty()) {
    return false;
  }
  
  return true;
}

bool AsciiParserRefactored::ValidateComposition(const Layer &layer) {
  // Check for circular references
  // Check for missing references
  // etc.
  return true;
}

// Error handling
bool AsciiParserRefactored::HasErrors() const {
  return error_handler_->HasErrors();
}

bool AsciiParserRefactored::HasWarnings() const {
  return error_handler_->HasWarnings();
}

std::vector<ParseError> AsciiParserRefactored::GetErrors() const {
  return error_handler_->GetErrors();
}

std::vector<ParseError> AsciiParserRefactored::GetWarnings() const {
  return error_handler_->GetWarnings();
}

// Factory implementation
std::unique_ptr<AsciiParserRefactored> AsciiParserFactory::Create() {
  return std::make_unique<AsciiParserRefactored>();
}

std::unique_ptr<AsciiParserRefactored> AsciiParserFactory::Create(const ParserConfig &config) {
  return std::make_unique<AsciiParserRefactored>(config);
}

std::unique_ptr<AsciiParserRefactored> AsciiParserFactory::CreateWithComponents(
    std::unique_ptr<AsciiLexer> lexer,
    std::unique_ptr<AsciiErrorHandler> error_handler,
    std::unique_ptr<ExpressionParser> expr_parser,
    std::unique_ptr<PropertyParser> prop_parser) {
  auto parser = std::make_unique<AsciiParserRefactored>();
  parser->lexer_ = std::move(lexer);
  parser->error_handler_ = std::move(error_handler);
  parser->expr_parser_ = std::move(expr_parser);
  parser->prop_parser_ = std::move(prop_parser);
  return parser;
}

// Convenience functions
bool ParseUSDA(const std::string &filename, Stage *stage,
               std::string *warn, std::string *err) {
  if (!stage) {
    if (err) *err = "Null stage pointer";
    return false;
  }
  
  Layer layer;
  AsciiParserRefactored parser;
  
  if (!parser.Parse(filename, &layer, warn, err)) {
    return false;
  }
  
  // Convert layer to stage
  stage->set_root_layer(&layer);
  
  return true;
}

bool ParseUSDAFromString(const std::string &content, const std::string &base_dir,
                        Stage *stage, std::string *warn, std::string *err) {
  if (!stage) {
    if (err) *err = "Null stage pointer";
    return false;
  }
  
  Layer layer;
  AsciiParserRefactored parser;
  
  if (!parser.ParseFromString(content, base_dir, &layer, warn, err)) {
    return false;
  }
  
  // Convert layer to stage
  stage->set_root_layer(&layer);
  
  return true;
}

// Utility functions
std::string GetUSDAVersion(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    return "";
  }
  
  std::string line;
  if (std::getline(file, line)) {
    if (line.find("#usda") == 0) {
      // Extract version
      size_t pos = line.find("1.0");
      if (pos != std::string::npos) {
        return "1.0";
      }
    }
  }
  
  return "";
}

bool ValidateUSDAFile(const std::string &filename, std::string *err) {
  Layer layer;
  AsciiParserRefactored parser;
  return parser.Parse(filename, &layer, nullptr, err);
}

bool IsValidUSDAHeader(const std::string &header) {
  return header.find("#usda 1.0") == 0;
}

} // namespace ascii
} // namespace tinyusdz