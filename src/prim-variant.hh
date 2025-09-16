// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// Variant and VariantSet definitions for USD
// Variants provide a way to switch between different variations of scene content

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "prim-forward-decl.hh"
#include "value-types.hh"
#include "list-op.hh"
#include "nonstd/optional.hpp"

namespace tinyusdz {

// Forward declarations
class PrimSpec;
class Property;
struct PrimMeta;

///
/// Variant represents a single variant within a VariantSet.
/// It contains the variant name and associated prim specs.
///
struct Variant {
  ///
  /// Variant name (e.g., "high", "low", "red", "blue")
  ///
  std::string name;

  ///
  /// Metas for this variant
  ///
  std::map<std::string, MetaVariable> metas;
  
  ///
  /// PrimSpecs defined within this variant
  /// These are applied when this variant is selected
  ///
  std::vector<PrimSpec> primSpecs;

  ///
  /// Properties defined within this variant
  ///
  std::map<std::string, Property> properties;

  ///
  /// Check if variant is empty (no prim specs or properties)
  ///
  bool empty() const {
    return primSpecs.empty() && properties.empty();
  }

  ///
  /// Clear all variant data
  ///
  void clear() {
    name.clear();
    metas.clear();
    primSpecs.clear();
    properties.clear();
  }
};

///
/// VariantSet represents a named collection of variants.
/// Only one variant from a set can be active at a time.
///
struct VariantSet {
  ///
  /// Name of the variant set (e.g., "modelingVariant", "shadingVariant")
  ///
  std::string name;

  ///
  /// Metadata for this variant set
  ///
  std::map<std::string, MetaVariable> metas;

  ///
  /// Map of variant name to variant definition
  /// Key: variant name (e.g., "high", "low")
  /// Value: Variant struct containing the variant's content
  ///
  std::map<std::string, Variant> variants;

  ///
  /// Currently selected variant name
  /// If empty, no variant is selected
  ///
  std::string selection;

  ///
  /// Get the currently selected variant
  ///
  const Variant* GetSelectedVariant() const {
    if (selection.empty()) {
      return nullptr;
    }
    auto it = variants.find(selection);
    return (it != variants.end()) ? &it->second : nullptr;
  }

  ///
  /// Get a variant by name
  ///
  const Variant* GetVariant(const std::string& variantName) const {
    auto it = variants.find(variantName);
    return (it != variants.end()) ? &it->second : nullptr;
  }

  ///
  /// Set the selected variant
  /// @return true if variant exists, false otherwise
  ///
  bool SetSelection(const std::string& variantName) {
    if (variantName.empty()) {
      selection.clear();
      return true;
    }
    if (variants.find(variantName) != variants.end()) {
      selection = variantName;
      return true;
    }
    return false;
  }

  ///
  /// Add or update a variant
  ///
  void SetVariant(const std::string& variantName, const Variant& variant) {
    variants[variantName] = variant;
  }

  ///
  /// Remove a variant
  ///
  bool RemoveVariant(const std::string& variantName) {
    auto it = variants.find(variantName);
    if (it != variants.end()) {
      variants.erase(it);
      if (selection == variantName) {
        selection.clear();
      }
      return true;
    }
    return false;
  }

  ///
  /// Get list of variant names
  ///
  std::vector<std::string> GetVariantNames() const {
    std::vector<std::string> names;
    names.reserve(variants.size());
    for (const auto& kv : variants) {
      names.push_back(kv.first);
    }
    return names;
  }

  ///
  /// Check if variant set is empty
  ///
  bool empty() const {
    return variants.empty();
  }

  ///
  /// Clear all variants and selection
  ///
  void clear() {
    name.clear();
    metas.clear();
    variants.clear();
    selection.clear();
  }
};

///
/// VariantSetSpec represents a variant set specification in a PrimSpec.
/// This is used during parsing and scene construction.
///
struct VariantSetSpec {
  ///
  /// Name of the variant set
  ///
  std::string name;

  ///
  /// List of variant names in this set
  ///
  std::vector<std::string> variantNames;

  ///
  /// Map of variant name to list of PrimSpecs with their edit qualifiers
  ///
  std::map<std::string, std::vector<std::pair<ListEditQual, PrimSpec>>> variantPrimSpecs;

  ///
  /// Metadata for this variant set spec
  ///
  std::map<std::string, MetaVariable> metas;

  ///
  /// Check if empty
  ///
  bool empty() const {
    return variantNames.empty() && variantPrimSpecs.empty();
  }

  ///
  /// Clear all data
  ///
  void clear() {
    name.clear();
    variantNames.clear();
    variantPrimSpecs.clear();
    metas.clear();
  }
};

///
/// VariantSelectionMap represents variant selections for a prim.
/// Maps variant set names to selected variant names.
///
using VariantSelectionMap = std::map<std::string, std::string>;

///
/// Utility functions for working with variants
///

///
/// Check if a variant name is valid
///
bool IsValidVariantName(const std::string& name);

///
/// Check if a variant set name is valid
///
bool IsValidVariantSetName(const std::string& name);

///
/// Parse variant selection string (e.g., "{varSet=varName}")
///
bool ParseVariantSelection(const std::string& str, 
                           std::string* variantSetName,
                           std::string* variantName);

///
/// Format variant selection as string
///
std::string FormatVariantSelection(const std::string& variantSetName,
                                   const std::string& variantName);

///
/// Apply variant selection to a prim
/// This merges the selected variant's content into the prim
///
bool ApplyVariantSelection(Prim& prim,
                           const std::string& variantSetName,
                           const std::string& variantName,
                           std::string* err = nullptr);

///
/// Get all variant sets from a prim
///
std::vector<VariantSet> GetVariantSets(const Prim& prim);

///
/// Get variant set by name from a prim
///
nonstd::optional<VariantSet> GetVariantSet(const Prim& prim,
                                           const std::string& variantSetName);

} // namespace tinyusdz