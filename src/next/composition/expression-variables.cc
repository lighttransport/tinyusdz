// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

#include "expression-variables.hh"

namespace tinyusdz {
namespace next {

namespace {

const std::string* StringValue(const Value& value) {
  if (const std::string* s = value.as_string()) return s;
  if (const std::string* s = value.as_token()) return s;
  return value.as_asset_path();
}

}  // namespace

Value ComposeExpressionVariables(const Value& weaker, const Value& stronger) {
  Value result = Value::MakeDictionary();
  Dict* out = result.as_dictionary();
  if (const Dict* d = weaker.as_dictionary()) {
    for (const auto& entry : d->entries) out->set(entry.first, entry.second);
  }
  if (const Dict* d = stronger.as_dictionary()) {
    for (const auto& entry : d->entries) out->set(entry.first, entry.second);
  }
  return result;
}

ExpressionEvaluation EvaluateAssetPathExpression(const std::string& path,
                                                 const Value& variables) {
  ExpressionEvaluation result;
  result.value = path;
  if (path.size() < 2 || path.front() != '`' || path.back() != '`') {
    return result;
  }
  result.is_expression = true;

  std::string body = path.substr(1, path.size() - 2);
  if (body.size() >= 2 &&
      ((body.front() == '"' && body.back() == '"') ||
       (body.front() == '\'' && body.back() == '\''))) {
    body = body.substr(1, body.size() - 2);
  } else if (!(body.size() >= 4 && body.compare(0, 2, "${") == 0 &&
               body.back() == '}')) {
    result.success = false;
    result.error = "unsupported asset-path expression (expected a variable "
                   "or quoted interpolation): " + path;
    return result;
  }

  const Dict* vars = variables.as_dictionary();
  std::string expanded;
  size_t cursor = 0;
  while (cursor < body.size()) {
    const size_t begin = body.find("${", cursor);
    if (begin == std::string::npos) {
      expanded.append(body, cursor, std::string::npos);
      break;
    }
    expanded.append(body, cursor, begin - cursor);
    const size_t end = body.find('}', begin + 2);
    if (end == std::string::npos) {
      result.success = false;
      result.error = "unterminated variable reference in expression: " + path;
      return result;
    }
    const std::string name = body.substr(begin + 2, end - begin - 2);
    const Value* variable = vars ? vars->find(name) : nullptr;
    const std::string* text = variable ? StringValue(*variable) : nullptr;
    if (!text) {
      result.success = false;
      result.error = "undefined or non-string expression variable `" + name +
                     "` in: " + path;
      return result;
    }
    expanded += *text;
    cursor = end + 1;
  }
  result.value = std::move(expanded);
  return result;
}

}  // namespace next
}  // namespace tinyusdz
