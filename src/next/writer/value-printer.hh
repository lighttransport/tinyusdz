// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value Printer
// Convert Value objects to string representation

#pragma once

#include "../types/value.hh"
#include <string>

namespace tinyusdz {
namespace next {

/// Options for value printing
struct PrintOptions {
  /// Number of decimal places for floats
  int float_precision = 6;

  /// Number of decimal places for doubles
  int double_precision = 15;

  /// Maximum array elements to print (0 = no limit)
  size_t max_array_elements = 0;

  /// Indent string for nested structures
  std::string indent = "    ";

  /// Use compact format (single line arrays)
  bool compact = false;
};

/// Print a Value to string (USDA format)
std::string PrintValue(const Value& value, const PrintOptions& opts = {});

/// Print a Value type name
std::string PrintTypeName(TypeId type_id, bool is_array = false);

/// Print a Value to USDA attribute declaration format
/// e.g., "float3 points = (1, 2, 3)"
std::string PrintAttributeValue(const std::string& type_name, const std::string& attr_name,
                                 const Value& value, const PrintOptions& opts = {});

}  // namespace next
}  // namespace tinyusdz
