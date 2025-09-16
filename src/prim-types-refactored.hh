// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// Refactored prim-types.hh with minimal includes
// This is the new main header that includes the modular components

#pragma once

// Minimal standard library includes
#include <string>
#include <vector>
#include <memory>

// Core modular components
#include "prim-forward-decl.hh"    // Forward declarations
#include "prim-core.hh"            // Prim class and basic types
#include "prim-variant.hh"         // Variant and VariantSet
#include "prim-metadata.hh"        // Metadata structures
#include "prim-container.hh"       // Utility functions

// Essential USD types that are still needed
#include "property.hh"             // Property class
#include "primspec.hh"             // PrimSpec class
#include "define-type-trait.hh"    // Type traits

// Note: The following headers have been removed from the main include:
// - Most standard library headers (only essential ones remain)
// - value-types.hh (included via prim-core.hh as needed)
// - Many utility headers that were creating tight coupling
// 
// Users should include specific headers as needed:
// - "attribute.hh" for Attribute class
// - "relationship.hh" for Relationship class
// - "xformop.hh" for XformOp operations
// - "collection.hh" for Collection support
// - "api-schemas.hh" for API schema support
// - etc.

namespace tinyusdz {

// Re-export commonly used types for backward compatibility
using namespace tinyusdz;  // All types are already in tinyusdz namespace

// Additional utility functions that don't fit in other modules

///
/// Get the concrete type name of a prim
///
inline std::string GetPrimTypeName(const Prim& prim) {
  return prim.prim_type_name();
}

///
/// Get the element name of a prim
///
inline std::string GetPrimElementName(const Prim& prim) {
  return prim.element_name();
}

///
/// Check if a prim is a model
///
inline bool IsModel(const Prim& prim) {
  return prim.is_model();
}

///
/// Get PrimMeta from a prim (if it has one)
///
inline const PrimMeta* GetPrimMeta(const Prim& prim) {
  return prim.get_prim_meta();
}

///
/// Compatibility layer for existing code
/// These are deprecated and will be removed in future versions
///
#ifdef TINYUSDZ_ENABLE_COMPATIBILITY

// Old include style - now split into modules
// Users should migrate to including specific headers
namespace compatibility {
  // Deprecated: Use prim-core.hh
  using Prim = ::tinyusdz::Prim;
  using Model = ::tinyusdz::Model;
  using Xformable = ::tinyusdz::Xformable;
  
  // Deprecated: Use prim-variant.hh
  using Variant = ::tinyusdz::Variant;
  using VariantSet = ::tinyusdz::VariantSet;
  
  // Deprecated: Use prim-metadata.hh
  using PrimMeta = ::tinyusdz::PrimMeta;
  using AttrMeta = ::tinyusdz::AttrMeta;
}

#endif // TINYUSDZ_ENABLE_COMPATIBILITY

} // namespace tinyusdz

// Migration notes:
// 
// 1. If you were including prim-types.hh for Prim class:
//    #include "prim-core.hh"
//
// 2. If you were including prim-types.hh for Variant/VariantSet:
//    #include "prim-variant.hh"
//
// 3. If you were including prim-types.hh for metadata:
//    #include "prim-metadata.hh"
//
// 4. If you were including prim-types.hh for utilities:
//    #include "prim-container.hh"
//
// 5. If you need everything (not recommended):
//    #include "prim-types-refactored.hh"