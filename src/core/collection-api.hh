// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// collection-api.hh - Collection, CollectionInstance, ColorSpaceAPI
//
#pragma once

#include <string>

#include "nonstd/optional.hpp"
#include "ordered-dict.hh"
#include "typed-attribute.hh"
#include "relationship.hh"
#include "animatable.hh"
#include "value-types.hh"

namespace tinyusdz {

// Collection API
///
/// ColorSpaceAPI - API schema for specifying color space of a prim
/// See: https://openusd.org/dev/user_guides/color_user_guide.html
///
/// Apply to prims: prepend apiSchemas = ["ColorSpaceAPI"]
/// Sets source color space via: uniform token colorSpace:name
///
struct ColorSpaceAPI {
  TypedAttributeWithFallback<Animatable<value::token>> colorSpace_name;  // uniform token colorSpace:name
};

// https://openusd.org/release/api/class_usd_collection_a_p_i.html

constexpr auto kExpandPrims = "expandPrims";
constexpr auto kExplicitOnly = "explicitOnly";
constexpr auto kExpandPrimsAndProperties = "expandPrimsAndProperties";

struct CollectionInstance {

  enum class ExpansionRule {
    ExpandPrims, // "expandPrims" (default)
    ExplicitOnly, // "explicitOnly"
    ExpandPrimsAndProperties, // "expandPrimsAndProperties"
  };

  TypedAttributeWithFallback<ExpansionRule> expansionRule{ExpansionRule::ExpandPrims}; // uniform token collection:collectionName:expansionRule
  TypedAttributeWithFallback<Animatable<bool>> includeRoot{false}; // bool collection:<collectionName>:includeRoot
  RelationshipProperty includes; // rel collection:<collectionName>:includes
  RelationshipProperty excludes; // rel collection:<collectionName>:excludes

};

class Collection
{
 public:
  Collection() = default;
  Collection(const Collection &) = default;
  Collection(Collection &&) noexcept = default;
  Collection &operator=(const Collection &) = default;
  Collection &operator=(Collection &&) noexcept = default;

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

}  // namespace tinyusdz
