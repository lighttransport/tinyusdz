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
  return true;
}

bool AsciiParser::Impl::ParsePrimContents() {
  PrimSpec* prim = builder_->current();
  if (!prim) return false;

  while (!Check(TokenType::CloseBrace) && !AtEnd()) {
    const Token& tok = lexer_->peek();

    if (tok.type == TokenType::Def || tok.type == TokenType::Over ||
        tok.type == TokenType::Class) {
      if (!ParsePrim()) {
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
      lexer_->next();
      std::string what;
      lexer_->expect(TokenType::Identifier, what);
      if (Match(TokenType::Equals)) SkipValueLike();
      continue;
    }

    if (tok.type == TokenType::Prepend || tok.type == TokenType::Append ||
        tok.type == TokenType::Delete || tok.type == TokenType::Add) {
      TokenType op_tok = tok.type;
      lexer_->next();
      if (lexer_->peek().type == TokenType::Rel) {
        PrimSpec::RelationshipListOp op = PrimSpec::RelationshipListOp::Append;
        if (op_tok == TokenType::Prepend) {
          op = PrimSpec::RelationshipListOp::Prepend;
        } else if (op_tok == TokenType::Delete) {
          op = PrimSpec::RelationshipListOp::Delete;
        } else if (op_tok == TokenType::Add) {
          op = PrimSpec::RelationshipListOp::Add;
        }
        if (!ParseRelationship(op, /*explicit_list=*/false)) return false;
      }
      continue;
    }

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

    if (tok.type == TokenType::Custom || tok.type == TokenType::Uniform ||
        tok.type == TokenType::Varying || tok.type == TokenType::Identifier) {
      if (!ParseAttribute()) {
        return false;
      }
      continue;
    }

    AddWarning("Skipping unknown token: " + tok.value);
    lexer_->next();
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
      lexer_->next();
    } else if (tok.type == TokenType::Uniform) {
      is_uniform = true;
      lexer_->next();
    } else if (tok.type == TokenType::Varying) {
      lexer_->next();
    } else {
      break;
    }
  }

  if (lexer_->peek().type == TokenType::Rel) {
    uint16_t rflags = 0;
    if (is_custom) rflags |= PropSlot::kFlagCustom;
    if (is_uniform) rflags |= PropSlot::kFlagUniform;
    return ParseRelationship(PrimSpec::RelationshipListOp::Append,
                             /*explicit_list=*/true, rflags);
  }

  std::string type_name;
  if (!lexer_->expect(TokenType::Identifier, type_name)) {
    AddError("Expected attribute type");
    return false;
  }

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

  if (PrimSpec* p = builder_->current()) {
    p->set_property_type_name(attr_name,
                              is_array ? type_name + "[]" : type_name);
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
    lexer_->next();

    if (type_id == TypeId::Invalid) {
      // Unknown attribute type: keep the VALUE via literal inference where the
      // form is unambiguous (pxr preserves `widget w = 5`); the declared type
      // name is preserved separately, so the writer re-emits `widget w = 5`.
      Value inferred;
      const Token& vt = lexer_->peek();
      if (vt.type == TokenType::Number) {
        ParseResult r = ParseValue(*lexer_, TypeId::Double);
        if (r.success) inferred = std::move(r.value);
      } else if (vt.type == TokenType::String) {
        ParseResult r = ParseValue(*lexer_, TypeId::String);
        if (r.success) inferred = std::move(r.value);
      } else if (vt.type == TokenType::True || vt.type == TokenType::False) {
        ParseResult r = ParseValue(*lexer_, TypeId::Bool);
        if (r.success) inferred = std::move(r.value);
      }
      if (!inferred.is_empty()) {
        if (PrimSpec* cur = builder_->current()) {
          cur->upsert_property(attr_name, std::move(inferred), flags);
        }
      } else {
        if (!SkipValueLike()) {
          AddError("Failed to skip value for unknown attribute type: " +
                   type_name);
          return false;
        }
        // Declared-only slot: storing an Invalid-typed Value here would
        // surface as a `default` field with type enum 0 in the crate writer
        // (pxr hard error).
        if (PrimSpec* cur = builder_->current()) {
          const PropNameId nid = GetPropNameTable().intern(attr_name);
          if (!cur->property(nid)) cur->add_property_slot(nid, type_id, flags);
        }
      }
    } else {
      ParseResult result;
      ParseArrayContext array_ctx;
      array_ctx.source_text = source_;
      array_ctx.enable_usda_lazy_arrays = options_.enable_usda_lazy_arrays;
      array_ctx.max_usda_lazy_array_elements =
          options_.max_usda_lazy_array_elements;
      array_ctx.num_threads = options_.num_threads;
      if (is_array) {
        result = ParseArrayValue(*lexer_, type_id, array_ctx);
      } else {
        result = ParseValue(*lexer_, type_id);
      }

      if (!result.success) {
        AddError(result.error);
        return false;
      }

      // Upsert: `attr.timeSamples = {...}` authored before `attr = v` already
      // created a slot; a second slot with the same name would emit two
      // Attribute specs with the same path (pxr: "invalid specs: spec
      // repeated" — the whole layer fails to open).
      if (PrimSpec* cur = builder_->current()) {
        cur->upsert_property(attr_name, std::move(result.value), flags);
      }
    }
    ParsePropertyMetadata(attr_name);

  } else if (Check(TokenType::Dot)) {
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
    } else if (prop_tok.type == TokenType::Identifier &&
               prop_tok.value == "connect") {
      lexer_->next();
      if (!Match(TokenType::Equals)) {
        AddError("Expected '=' after connect");
        return false;
      }
      flags |= PropSlot::kFlagConnection;
      if (is_array) flags |= PropSlot::kFlagArray;
      PrimSpec* cur = builder_->current();
      const PropNameId nid = GetPropNameTable().intern(attr_name);
      if (cur && !cur->property(nid)) {
        cur->add_property_slot(nid, type_id, flags);
      }
      if (Check(TokenType::None)) {
        // `.connect = None`: an authored connection block.
        lexer_->next();
        if (cur) cur->set_connection_block(attr_name);
      } else if (Check(TokenType::OpenBracket)) {
        lexer_->next();
        while (!Check(TokenType::CloseBracket) && !AtEnd()) {
          std::string p;
          if (lexer_->expect(TokenType::PathRef, p) && cur) {
            cur->add_connection(attr_name, Path(p));
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
        if (cur) cur->add_connection(attr_name, Path(path));
      }
      ParsePropertyMetadata(attr_name);
    } else {
      AddWarning("Unknown attribute property: " + prop_tok.value);
      lexer_->next();
    }
  } else {
    // Bare declaration (`token outputs:out`): declared-only slot with no
    // value; an Invalid-typed Value would emit a type-enum-0 `default` field.
    if (PrimSpec* cur = builder_->current()) {
      const PropNameId nid = GetPropNameTable().intern(attr_name);
      if (!cur->property(nid)) cur->add_property_slot(nid, type_id, flags);
    }
  }

  return true;
}

bool AsciiParser::Impl::ParseRelationship(PrimSpec::RelationshipListOp op,
                                          bool explicit_list,
                                          uint16_t flags) {
  lexer_->next();

  std::string rel_name;
  if (!ParseNamespacedName(&rel_name, "relationship name")) {
    return false;
  }
  if (flags) {
    if (PrimSpec* prim = builder_->current()) {
      prim->set_relationship_flags(
          rel_name, static_cast<uint16_t>(
                        prim->relationship_flags(rel_name) | flags));
    }
  }

  ParsePropertyMetadata(rel_name);

  if (!Match(TokenType::Equals)) {
    // Bare declaration (`custom rel material:binding`): register an empty
    // relationship so it round-trips (writers emit `rel name`, no targets).
    if (PrimSpec* prim = builder_->current()) {
      if (!prim->relationship(rel_name)) {
        prim->set_relationship_targets(rel_name, {});
      }
    }
    return true;
  }

  std::vector<Path> targets;
  if (Check(TokenType::OpenBracket)) {
    lexer_->next();
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
    lexer_->next();
  }

  if (PrimSpec* prim = builder_->current()) {
    if (targets.empty()) {
      // `= None` / `= []`: keep the relationship declared (empty) rather than
      // dropping it.
      if (!prim->relationship(rel_name)) {
        prim->set_relationship_targets(rel_name, {});
      }
    } else {
      // A bare explicit list authored BEFORE any edits dominates: the
      // explicit base is not representable in edit sublists, so subsequent
      // edits keep the legacy flatten-into-effective behavior.
      const bool explicit_base =
          !explicit_list && prim->relationship_edits().count(rel_name) == 0 &&
          prim->relationship(rel_name) && !prim->relationship(rel_name)->empty();
      prim->apply_relationship_list_op(rel_name, targets, op);
      // Record the authored list-op edit so the qualifier round-trips
      // (`prepend rel r = <...>` used to flatten to an explicit list and
      // `delete` edits vanished entirely).
      if (!explicit_list && !explicit_base) {
        ArcEdit& e = prim->ensure_relationship_edit(rel_name);
        e.authored = true;
        e.is_explicit = false;
        std::vector<std::string>* sub = nullptr;
        switch (op) {
          case PrimSpec::RelationshipListOp::Prepend:
            sub = &e.prepended;
            break;
          case PrimSpec::RelationshipListOp::Append:
          case PrimSpec::RelationshipListOp::Add:
            sub = &e.appended;
            break;
          case PrimSpec::RelationshipListOp::Delete:
            sub = &e.deleted;
            break;
        }
        if (sub) {
          for (const Path& t : targets) sub->push_back(t.str());
        }
      } else {
        // A later bare (explicit) authoring replaces any recorded edits.
        if (prim->relationship_edits().count(rel_name)) {
          ArcEdit& e = prim->ensure_relationship_edit(rel_name);
          e = ArcEdit();
          e.authored = true;
        }
      }
    }
  }
  ParsePropertyMetadata(rel_name);

  return true;
}

}  // namespace next
}  // namespace tinyusdz
