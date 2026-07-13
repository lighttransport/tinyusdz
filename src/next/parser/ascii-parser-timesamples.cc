// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDA ASCII parser timeSamples support.

#include "ascii-parser-internal.hh"
#include "value-parser.hh"

namespace tinyusdz {
namespace next {

bool AsciiParser::Impl::ParseTimeSamples(const std::string& prop_name,
                                         TypeId type_id, bool is_array) {
  if (!Match(TokenType::OpenBrace)) {
    AddError("Expected '{' for timeSamples");
    return false;
  }

  while (!Check(TokenType::CloseBrace) && !AtEnd()) {
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
      ParseArrayContext array_ctx;
      array_ctx.source = source_;
      array_ctx.enable_usda_lazy_arrays = options_.enable_usda_lazy_arrays;
      array_ctx.max_usda_lazy_array_elements =
          options_.max_usda_lazy_array_elements;
      array_ctx.num_threads = options_.num_threads;
      value_result = ParseArrayValue(*lexer_, type_id, array_ctx);
    } else if (Check(TokenType::Number) && !IsScalarType(type_id)) {
      // AOUSD permits format implementations to retain a default/time sample
      // whose stored value disagrees with the declared type. Parse the scalar
      // as its own VariantValue instead of rejecting the whole layer (the
      // supplemental attributes fixture deliberately exercises this).
      value_result = ParseValue(*lexer_, TypeId::Double);
    } else {
      value_result = ParseValue(*lexer_, type_id);
    }
    if (!value_result.success) {
      AddError("Failed to parse timeSample value: " + value_result.error);
      return false;
    }

    builder_->add_time_sample(prop_name, time, std::move(value_result.value));

    Match(TokenType::Comma);
  }

  return Match(TokenType::CloseBrace);
}

}  // namespace next
}  // namespace tinyusdz
