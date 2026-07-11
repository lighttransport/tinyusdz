// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

#pragma once

#include "../layer/prim-spec.hh"

#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

struct SchemaPropertyDefinition {
  std::string schema_type;
  std::string name;
  std::string type_name;
  Value fallback;
  bool has_fallback = false;
};

/// Compact built-in prim-definition registry shared by stage population,
/// value resolution, and validation. It intentionally covers the schemas
/// implemented by next; generated expansion can add definitions without
/// changing the resolver.
class SchemaRegistry {
 public:
  const SchemaPropertyDefinition* FindProperty(
      const PrimSpec& prim, const std::string& property_name) const;
  std::vector<std::string> PropertyNames(const PrimSpec& prim) const;

 private:
  SchemaRegistry();
  friend const SchemaRegistry& GetSchemaRegistry();

  std::vector<SchemaPropertyDefinition> properties_;
  std::vector<std::pair<std::string, std::string>> parents_;
};

const SchemaRegistry& GetSchemaRegistry();

}  // namespace next
}  // namespace tinyusdz
