// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII Parser implementation

#include "ascii-parser.hh"
#include "lexer.hh"
#include "value-parser.hh"
#include "../memory-budget.hh"

#include <fstream>
#include <sstream>

namespace tinyusdz {
namespace next {

// ============================================================
// Parser Implementation (PIMPL)
// ============================================================

class AsciiParser::Impl {
public:
  explicit Impl(const ParseOptions& options)
      : options_(options),
        memory_budget_(options.max_memory_limit_in_mb > 0
                           ? MemoryBudget::FromMB(options.max_memory_limit_in_mb)
                           : MemoryBudget()) {}

  bool Parse(const char* data, size_t length);
  bool ParseFile(const char* filename);

  Stage TakeStage() { return std::move(stage_); }
  const std::vector<ParseError>& GetErrors() const { return errors_; }
  bool HasErrors() const { return !errors_.empty(); }
  const std::vector<std::string>& GetWarnings() const { return warnings_; }

private:
  ParseOptions options_;
  Stage stage_;
  std::vector<ParseError> errors_;
  std::vector<std::string> warnings_;

  // Memory budget tracking
  MemoryBudget memory_budget_;
  size_t prim_count_ = 0;

  // Parsing state
  std::unique_ptr<Lexer> lexer_;
  std::unique_ptr<Layer> layer_;
  std::unique_ptr<LayerBuilder> builder_;
  size_t depth_ = 0;

  // Parsing methods
  bool ParseStageMetadata();
  bool ParsePrim();
  bool ParsePrimContents();
  bool ParseAttribute();
  bool ParseRelationship();
  bool ParseMetadataBlock();
  bool ParseTimeSamples(const std::string& prop_name, TypeId type_id);

  void AddError(const std::string& message);
  void AddWarning(const std::string& message);

  // Memory budget helpers
  bool CheckMemoryBudget(uint64_t bytes, const char* context);
  bool CheckPrimCount();
  bool CheckPropertyCount(size_t count);
  bool CheckTimeSampleCount(size_t count);
  bool CheckArraySize(size_t count, size_t element_size);

  // Token helpers
  bool Match(TokenType type);
  bool Check(TokenType type);
  bool AtEnd();
};

bool AsciiParser::Impl::Parse(const char* data, size_t length) {
  errors_.clear();
  warnings_.clear();
  depth_ = 0;
  prim_count_ = 0;
  memory_budget_.Reset();

  if (options_.max_file_size > 0 && length > options_.max_file_size) {
    AddError("File size exceeds maximum allowed");
    return false;
  }

  // Reserve memory for the input data copy (lexer may reference it)
  if (!CheckMemoryBudget(length, "input data")) {
    return false;
  }

  // Create fresh layer and builder
  layer_ = std::make_unique<Layer>();
  builder_ = std::make_unique<LayerBuilder>(*layer_);

  lexer_ = std::make_unique<Lexer>(data, length);

  // Parse stage metadata (header block)
  if (!ParseStageMetadata()) {
    return false;
  }

  // Parse root prims
  while (!AtEnd()) {
    if (!ParsePrim()) {
      return false;
    }
  }

  // Finalize the layer
  builder_->finalize();

  // Create stage from layer
  stage_ = Stage();
  stage_.SetRootLayer(std::move(*layer_));

  lexer_.reset();
  builder_.reset();
  layer_.reset();

  return errors_.empty();
}

bool AsciiParser::Impl::ParseFile(const char* filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    AddError(std::string("Failed to open file: ") + filename);
    return false;
  }

  size_t size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);

  std::string content(size, '\0');
  if (!file.read(&content[0], static_cast<std::streamsize>(size))) {
    AddError("Failed to read file contents");
    return false;
  }

  return Parse(content.data(), content.size());
}

bool AsciiParser::Impl::ParseStageMetadata() {
  // Check for #usda header
  const Token& first = lexer_->peek();
  if (first.type == TokenType::Identifier && first.value == "#usda") {
    // Skip #usda line
    lexer_->skip_line();
  }

  // Check for opening paren (metadata block)
  if (Check(TokenType::OpenParen)) {
    lexer_->next();

    // Parse metadata key-value pairs
    while (!Check(TokenType::CloseParen) && !AtEnd()) {
      std::string key;
      if (!lexer_->expect(TokenType::Identifier, key)) {
        AddError("Expected metadata key");
        return false;
      }

      if (!Match(TokenType::Equals)) {
        AddError("Expected '=' after metadata key");
        return false;
      }

      // Parse value based on known metadata keys
      if (key == "defaultPrim") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().defaultPrim = value;
        }
      } else if (key == "upAxis") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().upAxis = value;
        }
      } else if (key == "metersPerUnit") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().metersPerUnit = *result.value.as_double();
        }
      } else if (key == "timeCodesPerSecond") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().timeCodesPerSecond = *result.value.as_double();
        }
      } else if (key == "startTimeCode") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().startTimeCode = *result.value.as_double();
        }
      } else if (key == "endTimeCode") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().endTimeCode = *result.value.as_double();
        }
      } else if (key == "doc") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().doc = value;
        }
      } else if (key == "subLayers") {
        // Parse sublayer list
        if (Match(TokenType::OpenBracket)) {
          while (!Check(TokenType::CloseBracket) && !AtEnd()) {
            std::string path;
            if (lexer_->expect(TokenType::PathRef, path) ||
                lexer_->expect(TokenType::String, path)) {
              layer_->meta().subLayers.push_back(path);
            }
            Match(TokenType::Comma);
          }
          Match(TokenType::CloseBracket);
        }
      } else {
        // Generic metadata - skip the value for now
        TypeId type;
        ParseGenericValue(*lexer_, type);
        AddWarning("Unknown stage metadata: " + key);
      }
    }

    if (!Match(TokenType::CloseParen)) {
      AddError("Expected ')' to close metadata block");
      return false;
    }
  }

  return true;
}

bool AsciiParser::Impl::ParsePrim() {
  // Check depth limit
  if (depth_ >= options_.max_depth) {
    AddError("Maximum prim nesting depth exceeded");
    return false;
  }

  // Check prim count limit
  if (!CheckPrimCount()) {
    return false;
  }

  // Reserve memory for PrimSpec structure (approximate)
  if (!CheckMemoryBudget(sizeof(PrimSpec) + 256, "prim")) {
    return false;
  }

  // Parse specifier: def, over, class
  PrimSpecifier specifier = PrimSpecifier::Def;
  const Token& spec_tok = lexer_->peek();

  if (spec_tok.type == TokenType::Def) {
    specifier = PrimSpecifier::Def;
    lexer_->next();
  } else if (spec_tok.type == TokenType::Over) {
    specifier = PrimSpecifier::Over;
    lexer_->next();
  } else if (spec_tok.type == TokenType::Class) {
    specifier = PrimSpecifier::Class;
    lexer_->next();
  } else {
    AddError("Expected 'def', 'over', or 'class'");
    return false;
  }

  // Parse type name (optional for 'over')
  std::string type_name;
  if (Check(TokenType::Identifier)) {
    lexer_->expect(TokenType::Identifier, type_name);
  }

  // Parse prim name
  std::string prim_name;
  if (!lexer_->expect(TokenType::String, prim_name)) {
    AddError("Expected prim name");
    return false;
  }

  // Begin prim in layer
  builder_->begin_prim(prim_name, type_name, specifier);
  prim_count_++;

  // Parse optional metadata block
  if (Check(TokenType::OpenParen)) {
    if (!ParseMetadataBlock()) {
      builder_->end_prim();
      return false;
    }
  }

  // Parse prim contents
  if (!Match(TokenType::OpenBrace)) {
    AddError("Expected '{' to start prim body");
    builder_->end_prim();
    return false;
  }

  depth_++;
  if (!ParsePrimContents()) {
    depth_--;
    builder_->end_prim();
    return false;
  }
  depth_--;

  if (!Match(TokenType::CloseBrace)) {
    AddError("Expected '}' to close prim body");
    builder_->end_prim();
    return false;
  }

  builder_->end_prim();
  return true;
}

bool AsciiParser::Impl::ParsePrimContents() {
  while (!Check(TokenType::CloseBrace) && !AtEnd()) {
    const Token& tok = lexer_->peek();

    // Check for nested prim
    if (tok.type == TokenType::Def || tok.type == TokenType::Over || tok.type == TokenType::Class) {
      if (!ParsePrim()) {
        return false;
      }
      continue;
    }

    // Check for relationship
    if (tok.type == TokenType::Rel) {
      if (!ParseRelationship()) {
        return false;
      }
      continue;
    }

    // Check for custom/uniform qualifiers or type name (attribute)
    if (tok.type == TokenType::Custom || tok.type == TokenType::Uniform ||
        tok.type == TokenType::Varying || tok.type == TokenType::Identifier) {
      if (!ParseAttribute()) {
        return false;
      }
      continue;
    }

    // Unknown token
    AddWarning("Skipping unknown token: " + tok.value);
    lexer_->next();
  }

  return true;
}

bool AsciiParser::Impl::ParseAttribute() {
  bool is_custom = false;
  bool is_uniform = false;

  // Parse qualifiers
  while (true) {
    const Token& tok = lexer_->peek();
    if (tok.type == TokenType::Custom) {
      is_custom = true;
      lexer_->next();
    } else if (tok.type == TokenType::Uniform) {
      is_uniform = true;
      lexer_->next();
    } else if (tok.type == TokenType::Varying) {
      lexer_->next();  // Skip but don't set flag (varying is default)
    } else {
      break;
    }
  }

  // Parse type name
  std::string type_name;
  if (!lexer_->expect(TokenType::Identifier, type_name)) {
    AddError("Expected attribute type");
    return false;
  }

  // Check for array type suffix
  bool is_array = false;
  if (Check(TokenType::OpenBracket)) {
    lexer_->next();
    if (!Match(TokenType::CloseBracket)) {
      AddError("Expected ']' for array type");
      return false;
    }
    is_array = true;
  }

  // Parse attribute name
  std::string attr_name;
  if (!lexer_->expect(TokenType::Identifier, attr_name)) {
    AddError("Expected attribute name");
    return false;
  }

  // Handle namespaced names (e.g., "xformOp:translate")
  while (Check(TokenType::Colon)) {
    lexer_->next();
    std::string suffix;
    if (lexer_->expect(TokenType::Identifier, suffix)) {
      attr_name += ":" + suffix;
    }
  }

  // Look up type
  bool dummy_array;
  TypeId type_id = ParseTypeName(type_name, dummy_array);
  if (type_id == TypeId::Invalid && !options_.allow_unknown_types) {
    AddError("Unknown attribute type: " + type_name);
    return false;
  }

  // Build property flags
  uint16_t flags = 0;
  if (is_custom) flags |= PropSlot::kFlagCustom;
  if (is_uniform) flags |= PropSlot::kFlagUniform;

  // Check for assignment or timeSamples
  if (Check(TokenType::Equals)) {
    lexer_->next();

    ParseResult result;
    if (is_array) {
      result = ParseArrayValue(*lexer_, type_id);
    } else {
      result = ParseValue(*lexer_, type_id);
    }

    if (!result.success) {
      AddError(result.error);
      return false;
    }

    builder_->add_property(attr_name, std::move(result.value), flags);

  } else if (Check(TokenType::Dot)) {
    // Check for .timeSamples or .connect
    lexer_->next();
    const Token& prop_tok = lexer_->peek();

    if (prop_tok.type == TokenType::TimeSamples) {
      lexer_->next();
      if (!Match(TokenType::Equals)) {
        AddError("Expected '=' after timeSamples");
        return false;
      }
      flags |= PropSlot::kFlagTimeSampled;
      if (!ParseTimeSamples(attr_name, type_id)) {
        return false;
      }
    } else if (prop_tok.type == TokenType::Identifier && prop_tok.value == "connect") {
      lexer_->next();
      if (!Match(TokenType::Equals)) {
        AddError("Expected '=' after connect");
        return false;
      }
      std::string path;
      if (!lexer_->expect(TokenType::PathRef, path)) {
        AddError("Expected path for connection");
        return false;
      }
      flags |= PropSlot::kFlagConnection;
      // Store connection as a string value
      builder_->add_property(attr_name, Value(path), flags);
    } else {
      AddWarning("Unknown attribute property: " + prop_tok.value);
      lexer_->next();
    }
  } else {
    // Attribute declaration without value - store empty placeholder
    builder_->add_property(attr_name, Value(), flags);
  }

  return true;
}

bool AsciiParser::Impl::ParseRelationship() {
  lexer_->next();  // Skip 'rel'

  // Parse relationship name
  std::string rel_name;
  if (!lexer_->expect(TokenType::Identifier, rel_name)) {
    AddError("Expected relationship name");
    return false;
  }

  // Handle namespaced names
  while (Check(TokenType::Colon)) {
    lexer_->next();
    std::string suffix;
    if (lexer_->expect(TokenType::Identifier, suffix)) {
      rel_name += ":" + suffix;
    }
  }

  if (!Match(TokenType::Equals)) {
    // Relationship declaration without target
    return true;
  }

  // Parse target(s)
  if (Check(TokenType::OpenBracket)) {
    // Multiple targets
    lexer_->next();
    while (!Check(TokenType::CloseBracket) && !AtEnd()) {
      std::string target;
      if (lexer_->expect(TokenType::PathRef, target)) {
        builder_->add_relationship(rel_name, Path(target));
      }
      if (!Check(TokenType::CloseBracket)) {
        Match(TokenType::Comma);
      }
    }
    Match(TokenType::CloseBracket);
  } else if (Check(TokenType::PathRef)) {
    // Single target
    std::string target;
    lexer_->expect(TokenType::PathRef, target);
    builder_->add_relationship(rel_name, Path(target));
  } else if (Check(TokenType::None)) {
    // No target
    lexer_->next();
  }

  return true;
}

bool AsciiParser::Impl::ParseMetadataBlock() {
  if (!Match(TokenType::OpenParen)) {
    return false;
  }

  PrimSpec* prim = builder_->current();
  if (!prim) {
    AddError("No current prim for metadata");
    return false;
  }

  while (!Check(TokenType::CloseParen) && !AtEnd()) {
    std::string key;
    if (!lexer_->expect(TokenType::Identifier, key)) {
      AddError("Expected metadata key");
      return false;
    }

    if (!Match(TokenType::Equals)) {
      AddError("Expected '=' after metadata key");
      return false;
    }

    // Handle known prim metadata
    if (key == "active") {
      ParseResult result = ParseValue(*lexer_, TypeId::Bool);
      if (result.success && result.value.as_bool()) {
        builder_->set_active(*result.value.as_bool());
      }
    } else if (key == "hidden") {
      ParseResult result = ParseValue(*lexer_, TypeId::Bool);
      if (result.success && result.value.as_bool()) {
        builder_->set_hidden(*result.value.as_bool());
      }
    } else if (key == "doc") {
      std::string doc;
      if (lexer_->expect(TokenType::String, doc)) {
        prim->meta().doc = doc;
      }
    } else if (key == "apiSchemas") {
      if (Match(TokenType::OpenBracket)) {
        while (!Check(TokenType::CloseBracket) && !AtEnd()) {
          std::string schema;
          if (lexer_->expect(TokenType::Identifier, schema)) {
            prim->meta().apiSchemas.push_back(schema);
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBracket);
      }
    } else if (key == "references") {
      if (Match(TokenType::OpenBracket)) {
        while (!Check(TokenType::CloseBracket) && !AtEnd()) {
          std::string ref;
          if (lexer_->expect(TokenType::PathRef, ref) || lexer_->expect(TokenType::String, ref)) {
            prim->meta().references.push_back(ref);
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBracket);
      }
    } else if (key == "payload") {
      std::string payload;
      if (lexer_->expect(TokenType::PathRef, payload) || lexer_->expect(TokenType::String, payload)) {
        prim->meta().payloads.push_back(payload);
      }
    } else {
      // Generic metadata - skip the value
      TypeId type;
      ParseGenericValue(*lexer_, type);
      AddWarning("Unknown prim metadata: " + key);
    }
  }

  return Match(TokenType::CloseParen);
}

bool AsciiParser::Impl::ParseTimeSamples(const std::string& prop_name, TypeId type_id) {
  if (!Match(TokenType::OpenBrace)) {
    AddError("Expected '{' for timeSamples");
    return false;
  }

  while (!Check(TokenType::CloseBrace) && !AtEnd()) {
    // Parse time value
    ParseResult time_result = ParseValue(*lexer_, TypeId::Double);
    if (!time_result.success || !time_result.value.as_double()) {
      AddError("Expected time value in timeSamples");
      return false;
    }
    double time = *time_result.value.as_double();

    if (!Match(TokenType::Colon)) {
      AddError("Expected ':' after time in timeSamples");
      return false;
    }

    // Parse value
    ParseResult value_result = ParseValue(*lexer_, type_id);
    if (!value_result.success) {
      AddError("Failed to parse timeSample value: " + value_result.error);
      return false;
    }

    builder_->add_time_sample(prop_name, time, std::move(value_result.value));

    // Optional comma
    Match(TokenType::Comma);
  }

  return Match(TokenType::CloseBrace);
}

void AsciiParser::Impl::AddError(const std::string& message) {
  ParseError err;
  err.line = lexer_ ? lexer_->line() : 0;
  err.column = lexer_ ? lexer_->column() : 0;
  err.message = message;
  errors_.push_back(err);
}

void AsciiParser::Impl::AddWarning(const std::string& message) {
  warnings_.push_back("Line " + std::to_string(lexer_ ? lexer_->line() : 0) + ": " + message);
}

bool AsciiParser::Impl::CheckMemoryBudget(uint64_t bytes, const char* context) {
  if (!memory_budget_.TryReserve(bytes)) {
    std::string msg = "Memory limit exceeded";
    if (context) {
      msg += " while allocating ";
      msg += context;
    }
    msg += ". Limit: " + std::to_string(memory_budget_.GetMaxBudget() / (1024 * 1024)) + " MB";
    msg += ", Current usage: " + std::to_string(memory_budget_.GetCurrentUsage() / (1024 * 1024)) + " MB";
    msg += ", Requested: " + std::to_string(bytes) + " bytes";
    AddError(msg);
    return false;
  }
  return true;
}

bool AsciiParser::Impl::CheckPrimCount() {
  if (options_.max_prim_count > 0 && prim_count_ >= options_.max_prim_count) {
    AddError("Maximum prim count exceeded: " + std::to_string(options_.max_prim_count));
    return false;
  }
  return true;
}

bool AsciiParser::Impl::CheckPropertyCount(size_t count) {
  if (options_.max_properties_per_prim > 0 && count >= options_.max_properties_per_prim) {
    AddError("Maximum properties per prim exceeded: " + std::to_string(options_.max_properties_per_prim));
    return false;
  }
  return true;
}

bool AsciiParser::Impl::CheckTimeSampleCount(size_t count) {
  if (options_.max_time_samples > 0 && count >= options_.max_time_samples) {
    AddError("Maximum time samples exceeded: " + std::to_string(options_.max_time_samples));
    return false;
  }
  return true;
}

bool AsciiParser::Impl::CheckArraySize(size_t count, size_t element_size) {
  if (options_.max_array_elements > 0 && count > options_.max_array_elements) {
    AddError("Maximum array elements exceeded: " + std::to_string(options_.max_array_elements) +
             ", requested: " + std::to_string(count));
    return false;
  }
  return CheckMemoryBudget(count * element_size, "array");
}

bool AsciiParser::Impl::Match(TokenType type) {
  if (Check(type)) {
    lexer_->next();
    return true;
  }
  return false;
}

bool AsciiParser::Impl::Check(TokenType type) {
  return lexer_ && lexer_->peek().type == type;
}

bool AsciiParser::Impl::AtEnd() {
  return !lexer_ || lexer_->peek().type == TokenType::Eof;
}

// ============================================================
// AsciiParser public interface
// ============================================================

AsciiParser::AsciiParser(const ParseOptions& options)
    : impl_(std::make_unique<Impl>(options)) {}

AsciiParser::~AsciiParser() = default;

AsciiParser::AsciiParser(AsciiParser&&) noexcept = default;
AsciiParser& AsciiParser::operator=(AsciiParser&&) noexcept = default;

bool AsciiParser::Parse(const char* data, size_t length) {
  return impl_->Parse(data, length);
}

bool AsciiParser::ParseFile(const char* filename) {
  return impl_->ParseFile(filename);
}

Stage AsciiParser::TakeStage() {
  return impl_->TakeStage();
}

const std::vector<ParseError>& AsciiParser::GetErrors() const {
  return impl_->GetErrors();
}

bool AsciiParser::HasErrors() const {
  return impl_->HasErrors();
}

const std::vector<std::string>& AsciiParser::GetWarnings() const {
  return impl_->GetWarnings();
}

}  // namespace next
}  // namespace tinyusdz
