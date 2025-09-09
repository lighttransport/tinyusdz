// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.

///
/// @file material-binding.cc
/// @brief Implementation file for MaterialBinding class
///
/// Contains method implementations for MaterialBinding class material strength
/// queries and collection-based binding lookups.
///

#include "material-binding.hh"
#include "prim-types.hh"  // For Relationship definition
#include "enum-types.hh"   // For kWeaderThanDescendants

namespace tinyusdz {

value::token MaterialBinding::get_materialBindingStrength(const value::token &purpose) {

  if (purpose.str() == kAllPurpose().str()) {
    if (materialBinding && materialBinding.value().metas().bindMaterialAs) {
      return materialBinding.value().metas().bindMaterialAs.value();
    }
  } else if (purpose.str() == "full") {
    if (materialBindingFull && materialBindingFull.value().metas().bindMaterialAs) {
      return materialBindingFull.value().metas().bindMaterialAs.value();
    }
  } else if (purpose.str() == "preview") {
    if (materialBindingPreview && materialBindingPreview.value().metas().bindMaterialAs) {
      return materialBindingPreview.value().metas().bindMaterialAs.value();
    }
  } else {
    if (_materialBindingMap.count(purpose.str())) {
      const auto &m = _materialBindingMap.at(purpose.str());
      if (m.metas().bindMaterialAs) {
        return m.metas().bindMaterialAs.value();
      }
    }
  }

  return value::token(kWeaderThanDescendants);
}

value::token MaterialBinding::get_materialBindingStrengthCollection(const value::token &coll_name, const value::token &purpose) {

  if (coll_name.str().empty()) {
    return get_materialBindingStrength(purpose);
  }

  if (_materialBindingCollectionMap.count(coll_name.str())) {
    const auto &coll_mb = _materialBindingCollectionMap.at(coll_name.str());

    if (coll_mb.count(purpose.str())) {
      const Relationship *prel{nullptr};
      if (coll_mb.at(purpose.str(), &prel)) {
        if (prel->metas().bindMaterialAs) {
          return prel->metas().bindMaterialAs.value();
        }
      }
    }
  }

  return value::token(kWeaderThanDescendants);
}

}  // namespace tinyusdz