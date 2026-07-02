// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII parser variant-set support.

#include "ascii-parser-internal.hh"
#include "value-parser.hh"

namespace tinyusdz {
namespace next {

bool AsciiParser::Impl::ParseVariantSetBody(const std::string& variant_set_name) {
  PrimSpec* prim = builder_->current();
  if (!prim) return false;
  return ParseVariantSetBodyInto(variant_set_name, prim->meta().variantSets(), 0);
}

bool AsciiParser::Impl::ParseVariantSetBodyInto(
    const std::string& variant_set_name, std::vector<VariantSetData>& target,
    int depth) {
  if (depth > 64) {
    AddError("variantSet nesting too deep");
    return false;
  }

  if (!Match(TokenType::OpenBrace)) {
    AddError("Expected '{' for variantSet body");
    return false;
  }

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

  if (!lexer_->expect(TokenType::String, out->name)) {
    AddError("Expected variant option name");
    return false;
  }

  if (Check(TokenType::OpenParen)) {
    lexer_->consume();
    while (!Check(TokenType::CloseParen) && !AtEnd()) {
      (void)(Match(TokenType::Prepend) || Match(TokenType::Append) ||
             Match(TokenType::Delete) || Match(TokenType::Reorder) ||
             Match(TokenType::Add));

      std::string key;
      if (!lexer_->expect(TokenType::Identifier, key)) {
        if (!AtEnd()) lexer_->consume();
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
      } else if (key == "references" || key == "payload" ||
                 key == "inherits" || key == "specializes") {
        std::vector<std::string>* target =
            key == "references"   ? &out->references
            : key == "payload"    ? &out->payloads
            : key == "inherits"   ? &out->inherits
                                  : &out->specializes;
        if (Check(TokenType::None)) {
          lexer_->consume();
        } else if (Match(TokenType::OpenBracket)) {
          while (!Check(TokenType::CloseBracket) && !AtEnd()) {
            std::string ref;
            if (!ReadArcRef(&ref)) break;
            target->push_back(std::move(ref));
            Match(TokenType::Comma);
          }
          Match(TokenType::CloseBracket);
        } else {
          std::string ref;
          if (ReadArcRef(&ref)) target->push_back(std::move(ref));
        }
      } else {
        SkipValueLike();
        while (Check(TokenType::PathRef)) lexer_->consume();
        if (Check(TokenType::OpenParen)) SkipValueLike();
      }
    }
    Match(TokenType::CloseParen);
  }

  if (!Match(TokenType::OpenBrace)) {
    AddError("Expected '{' for variant option body");
    return false;
  }

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
      std::unique_ptr<Layer> host_layer = std::move(layer_);
      std::unique_ptr<LayerBuilder> host_builder = std::move(builder_);
      layer_ = std::move(content_layer);
      builder_ = std::move(content_builder);
      // Guard: prims inside variant content must parse inline (subtree
      // dispatch would attach fragment placeholders to the variant layer).
      variant_depth_++;
      bool ok = ParsePrim();
      variant_depth_--;
      content_layer = std::move(layer_);
      content_builder = std::move(builder_);
      layer_ = std::move(host_layer);
      builder_ = std::move(host_builder);
      if (!ok) return false;
    } else if (tok.type == TokenType::Rel) {
      lexer_->consume();
      std::string rel_name;
      if (!ParseNamespacedName(&rel_name, "relationship name")) break;
      SkipPropertyMetadata();
      if (Match(TokenType::Equals)) {
        if (Check(TokenType::OpenBracket)) {
          lexer_->consume();
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
    } else if (tok.type == TokenType::Identifier && tok.text == "variantSet") {
      lexer_->consume();
      std::string nested_vs_name;
      if (!lexer_->expect(TokenType::String, nested_vs_name)) break;
      if (!Match(TokenType::Equals)) break;
      if (!ParseVariantSetBodyInto(nested_vs_name, out->variantSets, depth + 1)) {
        return false;
      }
    } else if (tok.type == TokenType::Custom || tok.type == TokenType::Uniform ||
               tok.type == TokenType::Varying ||
               tok.type == TokenType::Identifier) {
      uint16_t vflags = 0;
      while (true) {
        TokenType tt = lexer_->peek().type;
        if (tt == TokenType::Custom) {
          vflags |= PropSlot::kFlagCustom;
          lexer_->consume();
        } else if (tt == TokenType::Uniform) {
          vflags |= PropSlot::kFlagUniform;
          lexer_->consume();
        } else if (tt == TokenType::Varying) {
          lexer_->consume();
        } else {
          break;
        }
      }
      std::string type_name;
      if (!lexer_->expect(TokenType::Identifier, type_name)) break;
      bool is_array = false;
      if (Check(TokenType::OpenBracket)) {
        lexer_->consume();
        Match(TokenType::CloseBracket);
        is_array = true;
      }
      if (is_array) vflags |= PropSlot::kFlagArray;
      std::string prop_name;
      if (!ParseNamespacedName(&prop_name, "attribute name")) break;
      SkipPropertyMetadata();
      if (Check(TokenType::Dot)) {
        lexer_->consume();
        if (!AtEnd()) lexer_->consume();
        if (Match(TokenType::Equals)) {
          SkipValueLike();
          while (Check(TokenType::PathRef)) lexer_->consume();
        }
        SkipPropertyMetadata();
      } else if (Match(TokenType::Equals)) {
        TypeId tid = ParseTypeName(type_name, is_array);
        if (tid == TypeId::Invalid) {
          if (!SkipValueLike()) break;
          out->properties.push_back({prop_name, Value(), vflags});
        } else {
          ParseResult result;
          if (is_array) {
            result = ParseArrayValue(*lexer_, tid);
          } else {
            result = ParseValue(*lexer_, tid);
          }
          if (result.success) {
            out->properties.push_back(
                {prop_name, std::move(result.value), vflags});
          }
        }
        SkipPropertyMetadata();
      }
    } else {
      lexer_->consume();
    }
  }

  if (content_layer) {
    content_builder->end_prim();
    content_builder->finalize();
    out->content = std::shared_ptr<Layer>(content_layer.release());
  }

  return Match(TokenType::CloseBrace);
}

}  // namespace next
}  // namespace tinyusdz
