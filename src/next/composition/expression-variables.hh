// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

#pragma once

#include "../types/value.hh"

#include <string>

namespace tinyusdz {
namespace next {

/// Policy for backtick-delimited variable expressions in composition asset
/// paths. Disabled preserves the authored path, Evaluate preserves an
/// unresolved expression and reports a warning, and RequireResolved rejects
/// the arc when parsing/evaluation fails.
enum class ExpressionVariablePolicy {
  Disabled,
  Evaluate,
  RequireResolved,
};

struct ExpressionEvaluation {
  std::string value;
  std::string error;
  bool is_expression = false;
  bool success = true;
};

/// Overlay dictionary entries from `stronger` onto `weaker` and return a new
/// dictionary. Non-dictionary inputs are treated as empty.
Value ComposeExpressionVariables(const Value& weaker, const Value& stronger);

/// Evaluate the asset-path subset of SdfVariableExpression: a direct
/// `${NAME}` or a single/double quoted string containing `${NAME}`
/// substitutions, enclosed in backticks. Variable values must be string,
/// token, or asset-path scalars.
ExpressionEvaluation EvaluateAssetPathExpression(const std::string& path,
                                                 const Value& variables);

}  // namespace next
}  // namespace tinyusdz
