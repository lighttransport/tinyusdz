// SPDX-License-Identifier: Apache 2.0

///
/// @file collection.hh
/// @brief USD Collection, Model, and Scope primitive definitions
///
/// Contains collection-related primitives including Collection (grouping primitive),
/// Model (generic primspec container), and Scope (simple grouping without
/// transformability). These form organizational structures in USD scene graphs.
///
#pragma once

#include <map>
#include <string>
#include <vector>

#include "nonstd/optional.hpp"
#include "value-types.hh"
#include "enum-types.hh"
#include "metadata.hh"
#include "property.hh"
#include "relationship.hh"
#include "attribute.hh"
#include "ordered-dict.hh"
#include "material-binding.hh"

namespace tinyusdz {

// Forward declarations
struct VariantSet;

///
/// @brief Collection instance for USD Collections API
///
/// Represents a single named collection with expansion rules and inclusion/exclusion
/// relationships. Collections group prims and properties for various USD operations.
///
struct CollectionInstance {

  enum class ExpansionRule {
    ExpandPrims, // "expandPrims" (default)
    ExplicitOnly, // "explicitOnly"
    ExpandPrimsAndProperties, // "expandPrimsAndProperties"
  };

  TypedAttributeWithFallback<ExpansionRule> expansionRule{ExpansionRule::ExpandPrims}; // uniform token collection:collectionName:expansionRule
  TypedAttributeWithFallback<Animatable<bool>> includeRoot{false}; // bool collection:<collectionName>:includeRoot
  nonstd::optional<Relationship> includes; // rel collection:<collectionName>:includes
  nonstd::optional<Relationship> excludes; // rel collection:<collectionName>:excludes

};

///
/// @brief USD Collection primitive
///
/// Collections provide a way to group prims and properties in USD scenes.
/// They support multiple named instances with different expansion rules
/// and inclusion/exclusion patterns.
///
class Collection
{
 public:
  const ordered_dict<CollectionInstance> instances() const {
    return _instances;
  }

  bool add_instance(const std::string &name, CollectionInstance &instance) {
    if (_instances.count(name)) {
      return false;
    }

    _instances.insert(name, instance);

    return true;
  }

  bool get_instance(const std::string &name, const CollectionInstance **coll) const {
    if (!coll) {
      return false;
    }

    return _instances.at(name, coll);
  }

  CollectionInstance &get_or_add_instance(const std::string &name) {
    return _instances.get_or_add(name);
  }

  bool has_instance(const std::string &name) const {
    return _instances.count(name);
  }

  bool del_instance(const std::string &name) {
    return _instances.erase(name);
  }

 private:
  ordered_dict<CollectionInstance> _instances;
};



///
/// @brief USD Model primitive
///
/// Generic primspec container representing unknown or unsupported Prim types.
/// Models inherit from Collection and MaterialBinding, providing grouping
/// capabilities and material assignment.
///
struct Model : public Collection, MaterialBinding {
  std::string name;

  std::string prim_type_name;  // e.g. "" for `def "bora" {}`, "UnknownPrim" for
                               // `def UnknownPrim "bora" {}`
  Specifier spec{Specifier::Def};

  int64_t parent_id{-1};  // Index to parent node

  PrimMeta meta;

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;

  // std::map<std::string, VariantSet> variantSets;

  std::map<std::string, Property> props;

  const std::vector<value::token> &primChildrenNames() const {
    return _primChildren;
  }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

// Volume-related structures

///
/// @brief OpenVDB asset definition
///
/// Represents an OpenVDB volume asset with field type and path information.
///
struct OpenVDBAsset {
  std::string fieldDataType{"float"};
  std::string fieldName{"density"};
  std::string filePath;  // asset
};

///
/// @brief MagicaVoxel asset definition
///
/// Represents a MagicaVoxel .vox asset with field type and path information.
///
struct VoxAsset {
  std::string fieldDataType{"float"};
  std::string fieldName{"density"};
  std::string filePath;  // asset
};

///
/// @brief USD Volume primitive
///
/// Simple volume class supporting OpenVDB and MagicaVoxel assets.
/// Currently a placeholder with basic asset references.
///
struct Volume {
  OpenVDBAsset vdb;
  VoxAsset vox;
};

///
/// @brief USD Scope primitive
///
/// Scope is the simplest grouping primitive and does not carry the baggage
/// of transformability. It's similar to a Group node in graphics applications.
/// Inherits from Collection and MaterialBinding.
///
struct Scope : Collection, MaterialBinding {
  std::string name;
  Specifier spec{Specifier::Def};

  int64_t parent_id{-1};

  PrimMeta meta;

  TypedAttributeWithFallback<Animatable<Visibility>> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, VariantSet> variantSet;

  std::map<std::string, Property> props;

  const std::vector<value::token> &primChildrenNames() const {
    return _primChildren;
  }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

}  // namespace tinyusdz