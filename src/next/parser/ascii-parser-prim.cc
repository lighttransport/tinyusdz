// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII parser prim/property bodies.

#include "ascii-parser-internal.hh"
#include "value-parser.hh"

namespace tinyusdz {
namespace next {

bool AsciiParser::Impl::ParsePrim() {
  if (depth_ >= options_.max_depth) {
    AddError("Maximum prim nesting depth exceeded");
    return false;
  }

  PrimSpecifier specifier = PrimSpecifier::Def;
  const Token& spec_tok = lexer_->peek();
  if (spec_tok.type == TokenType::Def) {
    specifier = PrimSpecifier::Def;
    lexer_->consume();
  } else if (spec_tok.type == TokenType::Over) {
    specifier = PrimSpecifier::Over;
    lexer_->consume();
  } else if (spec_tok.type == TokenType::Class) {
    specifier = PrimSpecifier::Class;
    lexer_->consume();
  } else {
    AddError("Expected 'def', 'over', or 'class'");
    return false;
  }

  std::string type_name;
  if (Check(TokenType::Identifier)) {
    lexer_->expect(TokenType::Identifier, type_name);
  }

  std::string prim_name;
  if (!lexer_->expect(TokenType::String, prim_name)) {
    AddError("Expected prim name");
    return false;
  }

  builder_->begin_prim(prim_name, type_name, specifier);

  if (Check(TokenType::OpenParen)) {
    if (!ParseMetadataBlock()) {
      builder_->end_prim();
      return false;
    }
  }

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
  if (options_.profile) {
    options_.profile->prims++;
  }
  return true;
}

bool AsciiParser::Impl::ParsePrimContents() {
  PrimSpec* prim = builder_->current();
  if (!prim) return false;

  while (!Check(TokenType::CloseBrace) && !AtEnd()) {
    const Token& tok = lexer_->peek();

    if (tok.type == TokenType::Def || tok.type == TokenType::Over ||
        tok.type == TokenType::Class) {
      if (!ParsePrimMaybeParallel()) {
        return false;
      }
      continue;
    }

    if (tok.type == TokenType::Rel) {
      if (!ParseRelationship()) {
        return false;
      }
      continue;
    }

    if (tok.type == TokenType::Reorder) {
      lexer_->consume();
      std::string what;
      lexer_->expect(TokenType::Identifier, what);
      if (Match(TokenType::Equals)) SkipValueLike();
      continue;
    }

    if (tok.type == TokenType::Prepend || tok.type == TokenType::Append ||
        tok.type == TokenType::Delete || tok.type == TokenType::Add) {
      TokenType op_tok = tok.type;
      lexer_->consume();
      if (lexer_->peek().type == TokenType::Rel) {
        PrimSpec::RelationshipListOp op = PrimSpec::RelationshipListOp::Append;
        if (op_tok == TokenType::Prepend) {
          op = PrimSpec::RelationshipListOp::Prepend;
        } else if (op_tok == TokenType::Delete) {
          op = PrimSpec::RelationshipListOp::Delete;
        } else if (op_tok == TokenType::Add) {
          op = PrimSpec::RelationshipListOp::Add;
        }
        if (!ParseRelationship(op)) return false;
      }
      continue;
    }

    if (tok.type == TokenType::Identifier && tok.text == "variantSet") {
      lexer_->consume();
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

    if (tok.type == TokenType::Custom || tok.type == TokenType::Uniform ||
        tok.type == TokenType::Varying || tok.type == TokenType::Identifier) {
      if (!ParseAttribute()) {
        return false;
      }
      continue;
    }

    AddWarning("Skipping unknown token: " + std::string(tok.text));
    lexer_->consume();
  }

  return true;
}

bool AsciiParser::Impl::ParseAttribute() {
  bool is_custom = false;
  bool is_uniform = false;

  while (true) {
    const Token& tok = lexer_->peek();
    if (tok.type == TokenType::Custom) {
      is_custom = true;
      lexer_->consume();
    } else if (tok.type == TokenType::Uniform) {
      is_uniform = true;
      lexer_->consume();
    } else if (tok.type == TokenType::Varying) {
      lexer_->consume();
    } else {
      break;
    }
  }

  if (lexer_->peek().type == TokenType::Rel) {
    return ParseRelationship();
  }

  std::string type_name;
  if (!lexer_->expect(TokenType::Identifier, type_name)) {
    AddError("Expected attribute type");
    return false;
  }

  bool is_array = false;
  if (Check(TokenType::OpenBracket)) {
    lexer_->consume();
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

  PrimSpec* cur = builder_->current();
  const PropNameId attr_name_id = GetPropNameTable().intern(attr_name);
  if (cur && attr_name_id.is_valid()) {
    if (is_array) {
      TypeNameId type_id = GetTypeNameTable().intern_array(type_name);
      if (type_id.is_valid()) {
        const std::string& tn = GetTypeNameTable().get(type_id);
        cur->set_property_type_name(attr_name_id, tn);
      }
    } else {
      cur->set_property_type_name(attr_name_id, type_name);
    }
  }

  ParsePropertyMetadata(attr_name);

  bool dummy_array;
  TypeId type_id = ParseTypeName(type_name, dummy_array);
  if (type_id == TypeId::Invalid && !options_.allow_unknown_types) {
    AddError("Unknown attribute type: " + type_name);
    return false;
  }

  uint16_t flags = 0;
  if (is_custom) flags |= PropSlot::kFlagCustom;
  if (is_uniform) flags |= PropSlot::kFlagUniform;

  if (Check(TokenType::Equals)) {
    lexer_->consume();

    if (type_id == TypeId::Invalid) {
      if (!SkipValueLike()) {
        AddError("Failed to skip value for unknown attribute type: " + type_name);
        return false;
      }
      builder_->add_property(attr_name, Value(), flags);
    } else {
      ParseResult result;
      if (is_array) {
        result = ParseArrayValueMaybeDeferred(type_id, nullptr);
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
    lexer_->consume();
    const Token& prop_tok = lexer_->peek();

    if (prop_tok.type == TokenType::TimeSamples) {
      lexer_->consume();
      if (!Match(TokenType::Equals)) {
        AddError("Expected '=' after timeSamples");
        return false;
      }
      flags |= PropSlot::kFlagTimeSampled;
      if (!ParseTimeSamples(attr_name, type_id, is_array)) {
        return false;
      }
      ParsePropertyMetadata(attr_name);
    } else if (prop_tok.type == TokenType::Identifier &&
               prop_tok.text == "connect") {
      lexer_->consume();
      if (!Match(TokenType::Equals)) {
        AddError("Expected '=' after connect");
        return false;
      }
      flags |= PropSlot::kFlagConnection;
      if (is_array) flags |= PropSlot::kFlagArray;
      if (cur && attr_name_id.is_valid() && !cur->property(attr_name_id)) {
        cur->add_property_slot(attr_name_id, type_id, flags);
      }
      if (Check(TokenType::None)) {
        lexer_->consume();
      } else if (Check(TokenType::OpenBracket)) {
        lexer_->consume();
        while (!Check(TokenType::CloseBracket) && !AtEnd()) {
          std::string p;
          if (lexer_->expect(TokenType::PathRef, p) && cur) {
            if (attr_name_id.is_valid()) {
              cur->add_connection(attr_name_id, Path(p));
            } else {
              cur->add_connection(attr_name, Path(p));
            }
          }
          Match(TokenType::Comma);
        }
        Match(TokenType::CloseBracket);
      } else {
        std::string path;
        if (!lexer_->expect(TokenType::PathRef, path)) {
          AddError("Expected path for connection");
          return false;
        }
        if (cur) {
          if (attr_name_id.is_valid())
            cur->add_connection(attr_name_id, Path(path));
          else
            cur->add_connection(attr_name, Path(path));
        }
      }
      ParsePropertyMetadata(attr_name);
    } else {
      AddWarning("Unknown attribute property: " + std::string(prop_tok.text));
      lexer_->consume();
    }
  } else {
    builder_->add_property(attr_name, Value(), flags);
  }

  if (options_.profile) {
    options_.profile->properties++;
  }
  return true;
}

bool AsciiParser::Impl::ParseRelationship(PrimSpec::RelationshipListOp op) {
  lexer_->consume();

  std::string rel_name;
  if (!ParseNamespacedName(&rel_name, "relationship name")) {
    return false;
  }

  ParsePropertyMetadata(rel_name);

  if (!Match(TokenType::Equals)) {
    return true;
  }

  std::vector<Path> targets;
  if (Check(TokenType::OpenBracket)) {
    lexer_->consume();
    while (!Check(TokenType::CloseBracket) && !AtEnd()) {
      std::string target;
      if (lexer_->expect(TokenType::PathRef, target)) {
        targets.emplace_back(target);
      }
      if (!Check(TokenType::CloseBracket)) {
        Match(TokenType::Comma);
      }
    }
    Match(TokenType::CloseBracket);
  } else if (Check(TokenType::PathRef)) {
    std::string target;
    lexer_->expect(TokenType::PathRef, target);
    targets.emplace_back(target);
  } else if (Check(TokenType::None)) {
    lexer_->consume();
  }

  if (!targets.empty()) {
    if (PrimSpec* prim = builder_->current()) {
      prim->apply_relationship_list_op(rel_name, targets, op);
    }
  }
  ParsePropertyMetadata(rel_name);

  return true;
}

}  // namespace next
}  // namespace tinyusdz
