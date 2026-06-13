// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII Parser implementation

#include "ascii-parser.hh"
#include "lexer.hh"
#include "value-parser.hh"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tinyusdz {
namespace next {

// ============================================================
// Parser Implementation (PIMPL)
// ============================================================

class AsciiParser::Impl {
public:
  explicit Impl(const ParseOptions& options) : options_(options) {}

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
  bool ParseTimeSamples(const std::string& prop_name, TypeId type_id,
                        bool is_array);
  bool ParseVariantSetBody(const std::string& variant_set_name);
  bool ParseVariantSetBodyInto(const std::string& variant_set_name,
                               std::vector<VariantSetData>& target, int depth);
  bool ParseVariantOption(VariantData* out, int depth);
  bool ParseNamespacedName(std::string* out, const char* what);
  bool SkipBalancedBlock(TokenType open, TokenType close);
  bool SkipValueLike();
  void SkipPropertyMetadata();
  void ParsePropertyMetadata(const std::string& prop_name);

  void AddError(const std::string& message);
  void AddWarning(const std::string& message);

  // Token helpers
  bool Match(TokenType type);
  bool Check(TokenType type);
  bool AtEnd();
};

namespace {

bool IsNameToken(const Token& tok) {
  switch (tok.type) {
    case TokenType::Identifier:
    case TokenType::Def:
    case TokenType::Over:
    case TokenType::Class:
    case TokenType::True:
    case TokenType::False:
    case TokenType::None:
    case TokenType::TimeSamples:
    case TokenType::Custom:
    case TokenType::Uniform:
    case TokenType::Varying:
    case TokenType::Prepend:
    case TokenType::Append:
    case TokenType::Delete:
    case TokenType::Add:
    case TokenType::Reorder:
    case TokenType::Rel:
      return !tok.value.empty();
    default:
      return false;
  }
}

}  // namespace

bool AsciiParser::Impl::Parse(const char* data, size_t length) {
  errors_.clear();
  warnings_.clear();
  depth_ = 0;

  if (options_.max_file_size > 0 && length > options_.max_file_size) {
    AddError("File size exceeds maximum allowed");
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
      } else if (key == "framesPerSecond") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().framesPerSecond = *result.value.as_double();
          layer_->meta().framesPerSecond_set = true;
        }
      } else if (key == "kilogramsPerUnit") {
        ParseResult result = ParseValue(*lexer_, TypeId::Double);
        if (result.success && result.value.as_double()) {
          layer_->meta().kilogramsPerUnit = *result.value.as_double();
          layer_->meta().kilogramsPerUnit_set = true;
        }
      } else if (key == "colorConfiguration") {
        // asset (`@path@`, lexed as String) or quoted string
        std::string value;
        if (Check(TokenType::String)) {
          lexer_->expect(TokenType::String, value);
          layer_->meta().colorConfiguration = value;
        }
      } else if (key == "colorManagementSystem") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().colorManagementSystem = value;
        }
      } else if (key == "doc" || key == "documentation") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().doc = value;
        }
      } else if (key == "comment") {
        std::string value;
        if (lexer_->expect(TokenType::String, value)) {
          layer_->meta().comment = value;
        }
      } else if (key == "customLayerData") {
        ParseResult r = ParseDict(*lexer_);
        if (r.success) layer_->meta().customLayerData = std::move(r.value);
      } else if (key == "expressionVariables") {
        ParseResult r = ParseDict(*lexer_);
        if (r.success) layer_->meta().expressionVariables = std::move(r.value);
      } else if (key == "subLayers") {
        // Parse sublayer list. Each element is an asset ref (`@path@`, lexed as
        // a String) or a quoted string. Decide with a non-consuming Check first:
        // `expect()` always consumes, so `expect(A) || expect(B)` would eat two
        // tokens per element and corrupt the list.
        if (Match(TokenType::OpenBracket)) {
          while (!Check(TokenType::CloseBracket) && !AtEnd()) {
            std::string path;
            if (Check(TokenType::PathRef)) {
              lexer_->expect(TokenType::PathRef, path);
            } else if (Check(TokenType::String)) {
              lexer_->expect(TokenType::String, path);
            } else {
              break;  // unexpected token: stop rather than mis-consume
            }
            layer_->meta().subLayers.push_back(path);
            Match(TokenType::Comma);
          }
          Match(TokenType::CloseBracket);
        }
      } else {
        // Generic metadata may be a dictionary/list; skip it structurally.
        SkipValueLike();
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
  PrimSpec* prim = builder_->current();
  if (!prim) return false;

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

    // Check for variantSet body
    if (tok.type == TokenType::Identifier && tok.value == "variantSet") {
      lexer_->next();
      std::string vs_name;
      if (!lexer_->expect(TokenType::String, vs_name)) {
        AddError("Expected variantSet name");
        return false;
      }
      if (!Match(TokenType::Equals)) {
        AddError("Expected '=' in variantSet");
        return false;
      }
      if (!ParseVariantSetBody(vs_name)) {
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

  std::string attr_name;
  if (!ParseNamespacedName(&attr_name, "attribute name")) {
    return false;
  }

  // Record the declared type name (as authored) so the writer re-emits the exact
  // type — round-tripping string[] vs token[], role types (color3f/point3f/...),
  // and value-less / connection-only attributes whose stored value is empty.
  if (PrimSpec* p = builder_->current()) {
    p->set_property_type_name(attr_name,
                              is_array ? type_name + "[]" : type_name);
  }

  ParsePropertyMetadata(attr_name);

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

    if (type_id == TypeId::Invalid) {
      if (!SkipValueLike()) {
        AddError("Failed to skip value for unknown attribute type: " + type_name);
        return false;
      }
      builder_->add_property(attr_name, Value(), flags);
    } else {
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
    }
    ParsePropertyMetadata(attr_name);

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
      if (!ParseTimeSamples(attr_name, type_id, is_array)) {
        return false;
      }
      ParsePropertyMetadata(attr_name);
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
      ParsePropertyMetadata(attr_name);
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
  if (!ParseNamespacedName(&rel_name, "relationship name")) {
    return false;
  }

  ParsePropertyMetadata(rel_name);

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
  ParsePropertyMetadata(rel_name);

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

  // Read one composition-arc reference into canonical "@asset@</prim>" /
  // "</prim>" form (the lexer yields @asset@ as a String without '@' and
  // </prim> as a PathRef without '<>'). Peeks before consuming so a missing
  // optional token does not eat the next one. Returns false if no arc token.
  auto ReadArcRef = [this](std::string* out) -> bool {
    std::string ref;
    if (Check(TokenType::String)) {
      std::string asset;
      lexer_->expect(TokenType::String, asset);
      ref = "@" + asset + "@";
      if (Check(TokenType::PathRef)) {
        std::string pr;
        lexer_->expect(TokenType::PathRef, pr);
        ref += "<" + pr + ">";
      }
    } else if (Check(TokenType::PathRef)) {
      std::string pr;
      lexer_->expect(TokenType::PathRef, pr);
      ref = "<" + pr + ">";
    } else {
      return false;
    }
    // Optional per-arc layer offset: `(offset = N; scale = M)`. Encoded into the
    // canonical ref as `?layerOffset=offset:scale` (Compositor::ParseReference
    // decodes it); composition composes it through the arc chain and bakes it
    // into time-sample times.
    if (Check(TokenType::OpenParen)) {
      Match(TokenType::OpenParen);
      double off = 0.0, scl = 1.0;
      while (!Check(TokenType::CloseParen) && !AtEnd()) {
        if (Check(TokenType::Identifier)) {
          std::string k;
          lexer_->expect(TokenType::Identifier, k);
          Match(TokenType::Equals);
          if (Check(TokenType::Number)) {
            std::string num;
            lexer_->expect(TokenType::Number, num);
            double v = std::strtod(num.c_str(), nullptr);
            if (k == "offset") off = v;
            else if (k == "scale") scl = v;
          }
        } else {
          lexer_->next();  // skip unexpected token (avoid spinning)
        }
        Match(TokenType::Semicolon);
      }
      Match(TokenType::CloseParen);
      if (off != 0.0 || scl != 1.0) {
        ref += "?layerOffset=" + std::to_string(off) + ":" + std::to_string(scl);
      }
    }
    *out = ref;
    return true;
  };
  // Read an arc value that may be a bracketed list or a single value.
  // List-op qualifier applied to an arc list (prepend/append/delete/explicit).
  enum class ArcQual { Explicit, Prepend, Append, Delete, Reorder };

  enum class ArcField { References, Payloads, Inherits, Specializes };
  auto SelectArc = [](PrimSpecMeta& meta,
                      ArcField f) -> std::vector<std::string>& {
    switch (f) {
      case ArcField::References: return meta.references;
      case ArcField::Payloads: return meta.payloads;
      case ArcField::Inherits: return meta.inherits;
      default: return meta.specializes;
    }
  };
  auto SelectEdit = [](ArcListOpEdits& e, ArcField f) -> ArcEdit& {
    switch (f) {
      case ArcField::References: return e.references;
      case ArcField::Payloads: return e.payloads;
      case ArcField::Inherits: return e.inherits;
      default: return e.specializes;
    }
  };

  // Read the bracketed (or single) arc references, then merge into the inline
  // arc vector honoring the list-op qualifier (explicit/bare replaces, prepend
  // front, append/reorder back, delete removes) -- the within-spec effective
  // list. ALSO record the raw qualifier into the spec's ArcEdit (Phase 7 S5),
  // which cross-layer composition (apply_list_ops) and the writer consume. Even
  // a bare empty list (`references = []`) is authored and must replace weaker
  // opinions.
  auto ReadArcList = [this, &ReadArcRef, &SelectArc, &SelectEdit](
                         PrimSpecMeta& meta, ArcField field, ArcQual qual) {
    std::vector<std::string> items;
    if (Match(TokenType::OpenBracket)) {
      while (!Check(TokenType::CloseBracket) && !AtEnd()) {
        std::string ref;
        if (!ReadArcRef(&ref)) break;
        items.push_back(ref);
        Match(TokenType::Comma);
      }
      Match(TokenType::CloseBracket);
    } else {
      std::string ref;
      if (ReadArcRef(&ref)) items.push_back(ref);
    }

    // Record the list-op edit first (copies items for non-bare qualifiers).
    ArcEdit& e = SelectEdit(meta.ensure_arc_edits(), field);
    switch (qual) {
      case ArcQual::Explicit:
        e = ArcEdit();  // explicit replaces: is_explicit=true, lists cleared
        e.authored = true;
        break;
      case ArcQual::Prepend:
        e.authored = true;
        e.is_explicit = false;
        e.prepended.insert(e.prepended.end(), items.begin(), items.end());
        break;
      case ArcQual::Append:
        e.authored = true;
        e.is_explicit = false;
        e.appended.insert(e.appended.end(), items.begin(), items.end());
        break;
      case ArcQual::Delete:
        e.authored = true;
        e.is_explicit = false;
        e.deleted.insert(e.deleted.end(), items.begin(), items.end());
        break;
      case ArcQual::Reorder:
        e.authored = true;
        e.is_explicit = false;
        e.ordered.insert(e.ordered.end(), items.begin(), items.end());
        break;
    }

    std::vector<std::string>* target = &SelectArc(meta, field);
    switch (qual) {
      case ArcQual::Explicit:
        *target = std::move(items);
        break;
      case ArcQual::Prepend:
        target->insert(target->begin(), items.begin(), items.end());
        break;
      case ArcQual::Append:
      case ArcQual::Reorder:
        target->insert(target->end(), items.begin(), items.end());
        break;
      case ArcQual::Delete:
        for (const std::string& d : items) {
          target->erase(std::remove(target->begin(), target->end(), d),
                        target->end());
        }
        break;
    }
  };

  while (!Check(TokenType::CloseParen) && !AtEnd()) {
    // Optional list-op qualifier keyword (prepend/append/delete/reorder)
    // precedes the real key, e.g. `prepend references = [...]`.
    ArcQual arc_qual = ArcQual::Explicit;
    if (Match(TokenType::Prepend)) {
      arc_qual = ArcQual::Prepend;
    } else if (Match(TokenType::Append) || Match(TokenType::Add)) {
      // `add` is USD's legacy list-op; treat it as append.
      arc_qual = ArcQual::Append;
    } else if (Match(TokenType::Delete)) {
      arc_qual = ArcQual::Delete;
    } else if (Match(TokenType::Reorder)) {
      arc_qual = ArcQual::Reorder;
    }

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
    } else if (key == "doc" || key == "documentation") {
      std::string doc;
      if (lexer_->expect(TokenType::String, doc)) {
        prim->meta().doc() = doc;
      }
    } else if (key == "comment") {
      std::string v;
      if (lexer_->expect(TokenType::String, v)) {
        prim->meta().comment() = v;
      }
    } else if (key == "kind") {
      std::string v;
      if (lexer_->expect(TokenType::String, v)) {
        prim->meta().kind() = v;
      }
    } else if (key == "displayName") {
      std::string v;
      if (lexer_->expect(TokenType::String, v)) {
        prim->meta().displayName() = v;
      }
    } else if (key == "instanceable") {
      ParseResult result = ParseValue(*lexer_, TypeId::Bool);
      if (result.success && result.value.as_bool()) {
        prim->meta().instanceable = *result.value.as_bool();
      }
    } else if (key == "apiSchemas") {
      if (Match(TokenType::OpenBracket)) {
        while (!Check(TokenType::CloseBracket) && !AtEnd()) {
          // Schema names are authored as quoted strings (`"PhysicsRigidBodyAPI"`);
          // accept bare identifiers too. expect() consumes even on mismatch, so
          // Check the token type first.
          std::string schema;
          if (Check(TokenType::String)
                  ? lexer_->expect(TokenType::String, schema)
                  : lexer_->expect(TokenType::Identifier, schema)) {
            prim->meta().apiSchemas().push_back(schema);
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBracket);
      }
    } else if (key == "references") {
      ReadArcList(prim->meta(), ArcField::References, arc_qual);
    } else if (key == "payload") {
      ReadArcList(prim->meta(), ArcField::Payloads, arc_qual);
    } else if (key == "inherits") {
      ReadArcList(prim->meta(), ArcField::Inherits, arc_qual);
    } else if (key == "specializes") {
      ReadArcList(prim->meta(), ArcField::Specializes, arc_qual);
    } else if (key == "customData") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) prim->meta().customData() = std::move(r.value);
    } else if (key == "assetInfo") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) prim->meta().assetInfo() = std::move(r.value);
    } else if (key == "sdrMetadata") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) prim->meta().sdrMetadata() = std::move(r.value);
    } else if (key == "clips") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) prim->meta().clips() = std::move(r.value);
    } else if (key == "variantSets") {
      // variantSets = ["setName1", "setName2"]  OR a single bare string
      // (`add variantSets = "shadingVariant"`). Declarations only; no body here.
      auto add_vs = [&](const std::string& vs_name) {
        for (const auto& vs : prim->meta().variantSets()) {
          if (vs.name == vs_name) return;  // already declared
        }
        prim->meta().variantSets().emplace_back();
        prim->meta().variantSets().back().name = vs_name;
      };
      if (Match(TokenType::OpenBracket)) {
        while (!Check(TokenType::CloseBracket) && !AtEnd()) {
          // expect() consumes even on mismatch, so dispatch on the peeked type
          // (the old `expect(String) || expect(Identifier)` dropped identifier
          // names by consuming them in the failed String branch).
          std::string vs_name;
          if (Check(TokenType::String)
                  ? lexer_->expect(TokenType::String, vs_name)
                  : lexer_->expect(TokenType::Identifier, vs_name)) {
            add_vs(vs_name);
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBracket);
      } else {
        std::string vs_name;
        if (Check(TokenType::String)
                ? lexer_->expect(TokenType::String, vs_name)
                : lexer_->expect(TokenType::Identifier, vs_name)) {
          add_vs(vs_name);
        }
      }
    } else if (key == "variants" || key == "variantSelection") {
      // variants = { string set = "selection"  string set2 = "sel2" }. USD
      // supports a selection per set; record them all in variantSelections()
      // (keeping the legacy single field set to the first for back-compat).
      if (Match(TokenType::OpenBrace)) {
        while (!Check(TokenType::CloseBrace) && !AtEnd()) {
          // Optional leading type name ("string"); the key may be a quoted
          // string or a bare identifier.
          std::string set_name;
          if (Check(TokenType::String)) {
            lexer_->expect(TokenType::String, set_name);
          } else if (Check(TokenType::Identifier)) {
            std::string first;
            lexer_->expect(TokenType::Identifier, first);
            // `string set = ...` -> first is the type name, read the real key.
            if (!Check(TokenType::Equals) &&
                (Check(TokenType::Identifier) || Check(TokenType::String))) {
              if (Check(TokenType::String)) {
                lexer_->expect(TokenType::String, set_name);
              } else {
                lexer_->expect(TokenType::Identifier, set_name);
              }
            } else {
              set_name = first;
            }
          } else {
            break;
          }
          if (!Match(TokenType::Equals)) break;
          std::string sel_name;
          if (!lexer_->expect(TokenType::String, sel_name)) break;
          prim->meta().variantSelections().emplace_back(set_name, sel_name);
          if (prim->meta().variantSelection.empty()) {
            prim->meta().variantSelection = set_name + "=" + sel_name;
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBrace);
      }
    } else {
      // Generic metadata may be a dictionary/list; skip it structurally.
      SkipValueLike();
      AddWarning("Unknown prim metadata: " + key);
    }
  }

  return Match(TokenType::CloseParen);
}

bool AsciiParser::Impl::ParseTimeSamples(const std::string& prop_name,
                                         TypeId type_id, bool is_array) {
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

    if (type_id == TypeId::Invalid) {
      if (!SkipValueLike()) {
        AddError("Failed to skip timeSample value for unknown attribute type");
        return false;
      }
      Match(TokenType::Comma);
      continue;
    }

    ParseResult value_result;
    if (is_array) {
      value_result = ParseArrayValue(*lexer_, type_id);
    } else {
      value_result = ParseValue(*lexer_, type_id);
    }
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

bool AsciiParser::Impl::ParseVariantSetBody(const std::string& variant_set_name) {
  PrimSpec* prim = builder_->current();
  if (!prim) return false;
  return ParseVariantSetBodyInto(variant_set_name, prim->meta().variantSets(), 0);
}

bool AsciiParser::Impl::ParseVariantSetBodyInto(
    const std::string& variant_set_name, std::vector<VariantSetData>& target,
    int depth) {
  // Guard against pathologically deep nesting of variantSets inside variants.
  if (depth > 64) {
    AddError("variantSet nesting too deep");
    return false;
  }

  // variantSet "name" = { "optionName" { ... } "optionName2" { ... } }
  if (!Match(TokenType::OpenBrace)) {
    AddError("Expected '{' for variantSet body");
    return false;
  }

  // Find or create the VariantSetData
  VariantSetData* vs_data = nullptr;
  for (auto& vs : target) {
    if (vs.name == variant_set_name) {
      vs_data = &vs;
      break;
    }
  }
  if (!vs_data) {
    target.emplace_back();
    vs_data = &target.back();
    vs_data->name = variant_set_name;
  }

  while (!Check(TokenType::CloseBrace) && !AtEnd()) {
    VariantData variant;
    if (!ParseVariantOption(&variant, depth)) {
      return false;
    }
    vs_data->variants.push_back(std::move(variant));
  }

  return Match(TokenType::CloseBrace);
}

bool AsciiParser::Impl::ParseVariantOption(VariantData* out, int depth) {
  if (!out) return false;

  // Parse variant option name
  if (!lexer_->expect(TokenType::String, out->name)) {
    AddError("Expected variant option name");
    return false;
  }

  // Parse optional metadata block: ( [list-op] key = value ... ). Known simple
  // fields are stored on the VariantData; the rest (apiSchemas, kind, variants,
  // references, ...) are consumed robustly so the body still parses.
  if (Check(TokenType::OpenParen)) {
    lexer_->next();
    while (!Check(TokenType::CloseParen) && !AtEnd()) {
      // Optional list-op qualifier before the key.
      (void)(Match(TokenType::Prepend) || Match(TokenType::Append) ||
             Match(TokenType::Delete) || Match(TokenType::Reorder) ||
             Match(TokenType::Add));

      std::string key;
      if (!lexer_->expect(TokenType::Identifier, key)) {
        if (!AtEnd()) lexer_->next();  // make progress on an unexpected token
        continue;
      }
      if (!Match(TokenType::Equals)) break;

      if (key == "active") {
        ParseResult result = ParseValue(*lexer_, TypeId::Bool);
        if (result.success && result.value.as_bool()) {
          out->active = *result.value.as_bool();
        }
      } else if (key == "hidden") {
        ParseResult result = ParseValue(*lexer_, TypeId::Bool);
        if (result.success && result.value.as_bool()) {
          out->hidden = *result.value.as_bool();
        }
      } else if (key == "doc" || key == "documentation") {
        std::string v;
        if (lexer_->expect(TokenType::String, v)) out->doc = v;
      } else {
        // Consume the value: a bracketed list / dict / paren block or a single
        // token, plus a trailing PathRef and layer-offset paren for arcs.
        SkipValueLike();
        while (Check(TokenType::PathRef)) lexer_->next();
        if (Check(TokenType::OpenParen)) SkipValueLike();
      }
    }
    Match(TokenType::CloseParen);
  }

  // Parse variant body: { properties, relationships, nested variantSets,
  // child prims }
  if (!Match(TokenType::OpenBrace)) {
    AddError("Expected '{' for variant option body");
    return false;
  }

  // Child prims inside a variant body are parsed into a content Layer whose
  // "__self__" root maps onto the host prim during composition (the shared
  // prim parser is temporarily redirected at this layer).
  std::unique_ptr<Layer> content_layer;
  std::unique_ptr<LayerBuilder> content_builder;

  while (!Check(TokenType::CloseBrace) && !AtEnd()) {
    const Token& tok = lexer_->peek();

    if (tok.type == TokenType::Def || tok.type == TokenType::Over ||
        tok.type == TokenType::Class) {
      if (!content_layer) {
        content_layer.reset(new Layer());
        content_builder.reset(new LayerBuilder(*content_layer));
        content_builder->begin_prim("__self__", "", PrimSpecifier::Over);
      }
      // Redirect the shared prim parser into the content layer. Moving the
      // owning unique_ptrs does not move the Layer pointee, so content_builder's
      // Layer& stays valid across the swap.
      std::unique_ptr<Layer> host_layer = std::move(layer_);
      std::unique_ptr<LayerBuilder> host_builder = std::move(builder_);
      layer_ = std::move(content_layer);
      builder_ = std::move(content_builder);
      bool ok = ParsePrim();
      content_layer = std::move(layer_);
      content_builder = std::move(builder_);
      layer_ = std::move(host_layer);
      builder_ = std::move(host_builder);
      if (!ok) return false;
    } else if (tok.type == TokenType::Rel) {
      lexer_->next();
      std::string rel_name;
      if (!ParseNamespacedName(&rel_name, "relationship name")) break;
      SkipPropertyMetadata();
      if (Match(TokenType::Equals)) {
        if (Check(TokenType::OpenBracket)) {
          lexer_->next();
          while (!Check(TokenType::CloseBracket) && !AtEnd()) {
            std::string target;
            if (lexer_->expect(TokenType::PathRef, target)) {
              out->relationships[rel_name].push_back(Path(target));
            }
            Match(TokenType::Comma);
          }
          Match(TokenType::CloseBracket);
        } else if (Check(TokenType::PathRef)) {
          std::string target;
          lexer_->expect(TokenType::PathRef, target);
          out->relationships[rel_name].push_back(Path(target));
        }
      }
      SkipPropertyMetadata();
    } else if (tok.type == TokenType::Identifier && tok.value == "variantSet") {
      lexer_->next();
      std::string nested_vs_name;
      if (!lexer_->expect(TokenType::String, nested_vs_name)) break;
      if (!Match(TokenType::Equals)) break;
      // Recurse into the nested variantSet, stored on this variant option.
      if (!ParseVariantSetBodyInto(nested_vs_name, out->variantSets, depth + 1)) {
        return false;
      }
    } else if (tok.type == TokenType::Custom || tok.type == TokenType::Uniform ||
               tok.type == TokenType::Varying || tok.type == TokenType::Identifier) {
      lexer_->next();
      std::string type_name;
      if (tok.type == TokenType::Identifier) {
        type_name = tok.value;
      } else {
        if (!lexer_->expect(TokenType::Identifier, type_name)) break;
      }
      bool is_array = false;
      if (Check(TokenType::OpenBracket)) {
        lexer_->next();
        Match(TokenType::CloseBracket);
        is_array = true;
      }
      std::string prop_name;
      if (!ParseNamespacedName(&prop_name, "attribute name")) break;
      SkipPropertyMetadata();
      if (Match(TokenType::Equals)) {
        TypeId tid = ParseTypeName(type_name, is_array);
        if (tid == TypeId::Invalid) {
          if (!SkipValueLike()) break;
          out->properties.emplace_back(prop_name, Value());
        } else {
          ParseResult result;
          if (is_array) {
            result = ParseArrayValue(*lexer_, tid);
          } else {
            result = ParseValue(*lexer_, tid);
          }
          if (result.success) {
            out->properties.emplace_back(prop_name, std::move(result.value));
          }
        }
        SkipPropertyMetadata();
      }
    } else {
      lexer_->next();
    }
  }

  if (content_layer) {
    content_builder->end_prim();  // pop __self__
    content_builder->finalize();
    out->content = std::shared_ptr<Layer>(content_layer.release());
  }

  return Match(TokenType::CloseBrace);
}

bool AsciiParser::Impl::ParseNamespacedName(std::string* out, const char* what) {
  if (!out) return false;

  const Token& first = lexer_->peek();
  if (!IsNameToken(first)) {
    AddError(std::string("Expected ") + what);
    return false;
  }

  *out = first.value;
  lexer_->next();

  while (Check(TokenType::Colon)) {
    lexer_->next();
    const Token& suffix = lexer_->peek();
    if (!IsNameToken(suffix)) {
      AddError(std::string("Expected namespaced suffix for ") + what);
      return false;
    }
    *out += ":";
    *out += suffix.value;
    lexer_->next();
  }

  return true;
}

bool AsciiParser::Impl::SkipBalancedBlock(TokenType open, TokenType close) {
  if (!Check(open)) return false;

  size_t depth = 0;
  while (!AtEnd()) {
    const Token& tok = lexer_->peek();
    if (tok.type == open) {
      ++depth;
      lexer_->next();
      continue;
    }
    if (tok.type == close) {
      lexer_->next();
      if (--depth == 0) {
        return true;
      }
      continue;
    }

    if (tok.type == TokenType::OpenParen) {
      if (!SkipBalancedBlock(TokenType::OpenParen, TokenType::CloseParen)) return false;
      continue;
    }
    if (tok.type == TokenType::OpenBracket) {
      if (!SkipBalancedBlock(TokenType::OpenBracket, TokenType::CloseBracket)) return false;
      continue;
    }
    if (tok.type == TokenType::OpenBrace) {
      if (!SkipBalancedBlock(TokenType::OpenBrace, TokenType::CloseBrace)) return false;
      continue;
    }

    lexer_->next();
  }

  AddError("Unexpected end of input while skipping block");
  return false;
}

bool AsciiParser::Impl::SkipValueLike() {
  if (Check(TokenType::OpenParen)) {
    return SkipBalancedBlock(TokenType::OpenParen, TokenType::CloseParen);
  }
  if (Check(TokenType::OpenBracket)) {
    return SkipBalancedBlock(TokenType::OpenBracket, TokenType::CloseBracket);
  }
  if (Check(TokenType::OpenBrace)) {
    return SkipBalancedBlock(TokenType::OpenBrace, TokenType::CloseBrace);
  }

  if (AtEnd()) return false;
  lexer_->next();
  return true;
}

void AsciiParser::Impl::SkipPropertyMetadata() {
  while (Check(TokenType::OpenParen)) {
    if (!SkipBalancedBlock(TokenType::OpenParen, TokenType::CloseParen)) {
      return;
    }
  }
}

void AsciiParser::Impl::ParsePropertyMetadata(const std::string& prop_name) {
  if (!Check(TokenType::OpenParen)) return;
  lexer_->next();  // consume '('
  PrimSpec* prim = builder_->current();

  while (!Check(TokenType::CloseParen) && !AtEnd()) {
    std::string key;
    if (!lexer_->expect(TokenType::Identifier, key)) {
      lexer_->next();  // robustness: skip stray token
      continue;
    }
    if (!Match(TokenType::Equals)) break;
    if (!prim) {  // no current prim: consume the value and continue
      SkipValueLike();
      continue;
    }
    // ensure_property_meta is keyed by interned name, so it works even at the
    // pre-`=` site where the property slot does not exist yet.
    PropMeta& m = prim->ensure_property_meta(prop_name);

    auto read_str = [&](std::string& out, uint32_t bit) {
      std::string v;
      if (lexer_->expect(TokenType::String, v)) {
        out = v;
        m.authored |= bit;
      }
    };

    if (key == "interpolation") read_str(m.interpolation, PropMeta::kInterpolation);
    else if (key == "colorSpace") read_str(m.colorSpace, PropMeta::kColorSpace);
    else if (key == "renderType") read_str(m.renderType, PropMeta::kRenderType);
    else if (key == "connectability") read_str(m.connectability, PropMeta::kConnectability);
    else if (key == "outputName") read_str(m.outputName, PropMeta::kOutputName);
    else if (key == "bindMaterialAs") read_str(m.bindMaterialAs, PropMeta::kBindMaterialAs);
    else if (key == "kind") read_str(m.kind, PropMeta::kKind);
    else if (key == "displayName") read_str(m.displayName, PropMeta::kDisplayName);
    else if (key == "displayGroup") read_str(m.displayGroup, PropMeta::kDisplayGroup);
    else if (key == "doc" || key == "documentation") read_str(m.doc, PropMeta::kDoc);
    else if (key == "elementSize") {
      ParseResult r = ParseValue(*lexer_, TypeId::Int);
      if (r.success && r.value.as_int()) {
        m.elementSize = *r.value.as_int();
        m.authored |= PropMeta::kElementSize;
      }
    } else if (key == "unauthoredValuesIndex") {
      ParseResult r = ParseValue(*lexer_, TypeId::Int);
      if (r.success && r.value.as_int()) {
        m.unauthoredValuesIndex = *r.value.as_int();
        m.authored |= PropMeta::kUnauthoredIdx;
      }
    } else if (key == "weight") {
      ParseResult r = ParseValue(*lexer_, TypeId::Double);
      if (r.success && r.value.as_double()) {
        m.weight = *r.value.as_double();
        m.authored |= PropMeta::kWeight;
      }
    } else if (key == "hidden") {
      ParseResult r = ParseValue(*lexer_, TypeId::Bool);
      if (r.success && r.value.as_bool()) {
        m.hidden = *r.value.as_bool();
        m.authored |= PropMeta::kHidden;
      }
    } else if (key == "allowedTokens") {
      ParseResult r = ParseArrayValue(*lexer_, TypeId::Token);
      if (r.success && r.value.as_token_array()) {
        m.allowedTokens = *r.value.as_token_array();
        m.authored |= PropMeta::kAllowedTokens;
      }
    } else if (key == "customData") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) { m.customData = std::move(r.value); m.authored |= PropMeta::kCustomData; }
    } else if (key == "assetInfo") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) { m.assetInfo = std::move(r.value); m.authored |= PropMeta::kAssetInfo; }
    } else if (key == "sdrMetadata") {
      ParseResult r = ParseDict(*lexer_);
      if (r.success) { m.sdrMetadata = std::move(r.value); m.authored |= PropMeta::kSdrMetadata; }
    } else {
      AddWarning("Unknown property metadata: " + key);
      SkipValueLike();
    }
  }
  Match(TokenType::CloseParen);
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
