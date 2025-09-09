// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.

///
/// @file material-binding.hh
/// @brief Material binding utilities and MaterialBinding class
///
/// Contains MaterialBinding class for managing USD material binding relationships
/// including direct bindings, purpose-restricted bindings, and collection-based bindings.
///
#pragma once

#include <map>
#include <string>
#include "ordered-dict.hh"
#include "value-types.hh"
#include "enum-types.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/optional.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

// Forward declarations
struct Relationship;

///
/// @brief MaterialBinding class for managing USD material binding relationships
///
/// Handles material binding relationships in USD, including:
/// - Direct bindings (material:binding)
/// - Purpose-restricted bindings (material:binding:preview, material:binding:full)
/// - Collection-based bindings (material:binding:collection:*)
///
/// Based on USD Material Binding specification:
/// https://openusd.org/release/wp_usdshade.html
///
class MaterialBinding {
 public:

  ///
  /// Get "all purpose" token (empty string)
  ///
  static value::token kAllPurpose() {
    return value::token("");
  }

  //
  // NOTE on material binding.
  // https://openusd.org/release/wp_usdshade.html
  //
  //  - "all purpose", direct binding, material:binding. single relationship target only
  //  - a purpose-restricted, direct, fallback binding, e.g. material:binding:preview
  //  - an all-purpose, collection-based binding, e.g. material:binding:collection:metalBits
  //  - a purpose-restricted, collection-based binding, e.g. material:binding:collection:full:metalBits
  //
  // In TinyUSDZ, treat empty purpose token as "all purpose"
  //

  // Some frequently used materialBindings
  nonstd::optional<Relationship> materialBinding; // material:binding
  nonstd::optional<Relationship> materialBindingPreview; // material:binding:preview
  nonstd::optional<Relationship> materialBindingFull; // material:binding:full

  //nonstd::optional<Relationship> materialBindingCollection; // material:binding:collection  Deprecated. use materialBindingCollectionMap[""][""] instead.

  ///
  /// Get material binding strength for the specified purpose
  ///
  value::token get_materialBindingStrength(const value::token &purpose);
  
  ///
  /// Get material binding strength for collection-based binding
  ///
  value::token get_materialBindingStrengthCollection(const value::token &collection_name, const value::token &purpose);

  ///
  /// Check if material binding exists
  ///
  bool has_materialBinding() const {
    return materialBinding.has_value();
  }

  ///
  /// Check if preview material binding exists
  ///
  bool has_materialBindingPreview() const {
    return materialBindingPreview.has_value();
  }

  ///
  /// Check if full material binding exists
  ///
  bool has_materialBindingFull() const {
    return materialBindingFull.has_value();
  }

  ///
  /// Check if material binding exists for specified purpose
  ///
  bool has_materialBinding(const value::token &mat_purpose) const {
    if (mat_purpose.str() == kAllPurpose().str()) {
      return has_materialBinding();
    } else if (mat_purpose.str() == "full") {
      return has_materialBindingFull();
    } else if (mat_purpose.str() == "preview") {
      return has_materialBindingPreview();
    } else {
      return _materialBindingMap.count(mat_purpose.str()) > 0;
    }
  }

  ///
  /// Clear material binding
  ///
  void clear_materialBinding() {
    materialBinding.reset();
  }

  ///
  /// Clear preview material binding
  ///
  void clear_materialBindingPreview() {
    materialBindingPreview.reset();
  }

  ///
  /// Clear full material binding
  ///
  void clear_materialBindingFull() {
    materialBindingFull.reset();
  }

  ///
  /// Set material binding relationship
  ///
  void set_materialBinding(const Relationship &rel) {
    materialBinding = rel;
  }

  ///
  /// Set material binding relationship with strength
  ///
  void set_materialBinding(const Relationship &rel, const MaterialBindingStrength strength) {
    value::token strength_tok(to_string(strength));
    materialBinding = rel;
    materialBinding.value().metas().bindMaterialAs = strength_tok;
  }

  ///
  /// Set preview material binding relationship
  ///
  void set_materialBindingPreview(const Relationship &rel) {
    materialBindingPreview = rel;
  }

  ///
  /// Set preview material binding relationship with strength
  ///
  void set_materialBindingPreview(const Relationship &rel, const MaterialBindingStrength strength) {
    value::token strength_tok(to_string(strength));
    materialBindingPreview = rel;
    materialBindingPreview.value().metas().bindMaterialAs = strength_tok;
  }

  ///
  /// Set full material binding relationship
  ///
  void set_materialBindingFull(const Relationship &rel) {
    materialBindingFull = rel;
  }

  ///
  /// Set full material binding relationship with strength
  ///
  void set_materialBindingFull(const Relationship &rel, const MaterialBindingStrength strength) {
    value::token strength_tok(to_string(strength));
    materialBindingFull = rel;
    materialBindingFull.value().metas().bindMaterialAs = strength_tok;
  }

  ///
  /// Set material binding for specified purpose
  ///
  void set_materialBinding(const Relationship &rel, const value::token &mat_purpose) {

    if (mat_purpose.str().empty()) {
      return set_materialBinding(rel);
    } else if (mat_purpose.str() == "full") {
      return set_materialBindingFull(rel);
    } else if (mat_purpose.str() == "preview") {
      return set_materialBindingFull(rel);
    } else {
      _materialBindingMap[mat_purpose.str()] = rel;
    }
  }

  ///
  /// Set material binding for specified purpose with strength
  ///
  void set_materialBinding(const Relationship &rel, const value::token &mat_purpose, const MaterialBindingStrength strength) {
    value::token strength_tok(to_string(strength));

    if (mat_purpose.str().empty()) {
      return set_materialBinding(rel, strength);
    } else if (mat_purpose.str() == "full") {
      return set_materialBindingFull(rel, strength);
    } else if (mat_purpose.str() == "preview") {
      return set_materialBindingFull(rel, strength);
    } else {
      _materialBindingMap[mat_purpose.str()] = rel;
      _materialBindingMap[mat_purpose.str()].metas().bindMaterialAs = strength_tok;
    }
  }

  ///
  /// Check if collection-based material binding exists
  ///
  bool has_materialBindingCollection(const std::string &tok) {

    if (!_materialBindingCollectionMap.count(tok)) {
      return false;
    }

    return _materialBindingCollectionMap.count(tok) > 0;
  }

  ///
  /// Set collection-based material binding
  ///
  void set_materialBindingCollection(const value::token &tok, const value::token &mat_purpose, const Relationship &rel) {

    // NOTE:
    // https://openusd.org/release/wp_usdshade.html#basic-proposal-for-collection-based-assignment
    // says: material:binding:collection defines a namespace of binding relationships to be applied in namespace order, with the earliest ordered binding relationship the strongest
    //
    // so the app is better first check if `tok` element alreasy exists(using has_materialBindingCollection)

    auto &m = _materialBindingCollectionMap[tok.str()];

    m.insert(mat_purpose.str(), rel);
  }

  ///
  /// Clear collection-based material binding
  ///
  void clear_materialBindingCollection(const value::token &tok, const value::token &mat_purpose) {
    if (_materialBindingCollectionMap.count(tok.str())) {
      _materialBindingCollectionMap[tok.str()].erase(mat_purpose.str());
    }
  }

  ///
  /// Set collection-based material binding with strength
  ///
  void set_materialBindingCollection(const value::token &tok, const value::token &mat_purpose, const Relationship &rel, MaterialBindingStrength strength) {
    value::token strength_tok(to_string(strength));

    Relationship r = rel;
    r.metas().bindMaterialAs = strength_tok;

    _materialBindingCollectionMap[tok.str()].insert(mat_purpose.str(), r);
  }

  ///
  /// Get material binding map (const)
  ///
  const std::map<std::string, Relationship> &materialBindingMap() const {
    return _materialBindingMap;
  }

  ///
  /// Get collection-based material binding map (const)
  ///
  const std::map<std::string, ordered_dict<Relationship>> &materialBindingCollectionMap() const {
    return _materialBindingCollectionMap;
  }

  ///
  /// Get material binding relationship for specified purpose
  ///
  bool get_materialBinding(const value::token &mat_purpose, Relationship *relOut) const {
    if (!relOut) {
      return false;
    }

    if (mat_purpose.str().empty()) {
      if (materialBinding.has_value()) {
        (*relOut) = materialBinding.value();
        return true;
      } else {
        return false; // not authored
      }
    } else if (mat_purpose.str() == "full") {
      if (materialBindingFull.has_value()) {
        (*relOut) = materialBindingFull.value();
        return true;
      } else {
        return false; // not authored
      }
    } else if (mat_purpose.str() == "preview") {
      if (materialBindingPreview.has_value()) {
        (*relOut) = materialBindingPreview.value();
        return true;
      } else {
        return false; // not authored
      }
    } else {
      if (_materialBindingMap.count(mat_purpose.str())) {
        (*relOut) = _materialBindingMap.at(mat_purpose.str());
        return true;
      } else {
        return false; // not authored
      }
    }
  }

 private:

  // For material:binding(excludes frequently used `material:binding`, `material:binding:full` and `material:binding:preview`)
  // key = PURPOSE, value = rel
  std::map<std::string, Relationship> _materialBindingMap;

  // For material:binding:collection
  // Use ordered dict since the requests:
  //
  // https://openusd.org/release/wp_usdshade.html#basic-proposal-for-collection-based-assignment
  //
  // `...with the earliest ordered binding relationship the strongest`
  //
  // key = PURPOSE, value = map<NAME, Rel>
  // TODO: Use multi-index map
  std::map<std::string, ordered_dict<Relationship>> _materialBindingCollectionMap;
};

}  // namespace tinyusdz