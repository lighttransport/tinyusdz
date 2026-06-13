// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value Parser
// Parses USD values from tokenized input

#pragma once

#include "../types/value.hh"
#include <string>

namespace tinyusdz {
namespace next {

class Lexer;

/// Result of a parse operation
struct ParseResult {
  bool success = false;
  Value value;
  std::string error;

  /// Construct success result
  static ParseResult Ok(Value v) {
    ParseResult r;
    r.success = true;
    r.value = std::move(v);
    return r;
  }

  /// Construct error result
  static ParseResult Error(const std::string& msg) {
    ParseResult r;
    r.success = false;
    r.error = msg;
    return r;
  }
};

/// Parse a value of the expected type from the lexer
/// The lexer should be positioned at the start of the value
ParseResult ParseValue(Lexer& lexer, TypeId expected_type);

/// Parse an array value of the expected element type
/// The lexer should be positioned at the opening bracket
ParseResult ParseArrayValue(Lexer& lexer, TypeId element_type);

/// Parse a generic value, inferring the type from syntax
/// Returns the value and sets type_id to the inferred type
ParseResult ParseGenericValue(Lexer& lexer, TypeId& out_type);

/// Parse a USD dictionary `{ [type] key = value ... }` into a Dictionary Value.
/// The lexer should be positioned at the opening brace. Nested dictionaries and
/// array-valued entries are supported.
ParseResult ParseDict(Lexer& lexer);

/// Get TypeId from a USD type name string
/// Handles both simple types ("float") and array types ("float[]")
TypeId ParseTypeName(const std::string& type_name, bool& is_array);

}  // namespace next
}  // namespace tinyusdz
