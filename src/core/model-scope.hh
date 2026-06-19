// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// model-scope.hh - Model, Scope structs and Preliminary_* types
//
#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "collection-api.hh"
#include "material-binding.hh"
#include "property.hh"
#include "prim-metas.hh"
#include "prim-enums.hh"
#include "extent.hh"
#include "value-types.hh"

namespace tinyusdz {

// Forward declarations
struct VariantSet;

// Generic primspec container.
// Unknown or unsupported Prim type are also reprenseted as Model for now.
struct Model : public Collection, MaterialBinding {
  Model() = default;
  Model(const Model &) = default;
  Model(Model &&) noexcept = default;
  Model &operator=(const Model &) = default;
  Model &operator=(Model &&) noexcept = default;

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

//
// Predefined node classes
//

// USDZ Schemas for AR — full definitions in usdAR.hh
// https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/schema_definitions_for_third-party_digital_content_creation_dcc

// -----------------------------------------------------------------------------
// Placeholder schema prim types for UsdVol and UsdRender.
//
// These are recognized prim *types* (so `def Volume "x" {}`,
// `def RenderSettings "y" {}`, etc. parse into distinct prims and round-trip
// through USDA/USDC), but their schema attributes are not yet modeled as typed
// fields -- all authored properties are retained generically in `props`. Full
// typed accessors live on the consume side (separate branch). Shape mirrors the
// minimal `Scope` placeholder.
// -----------------------------------------------------------------------------
#define TINYUSDZ_DEFINE_PLACEHOLDER_PRIM(__cls)                              \
  struct __cls {                                                             \
    __cls() = default;                                                       \
    std::string name;                                                        \
    Specifier spec{Specifier::Def};                                          \
    int64_t parent_id{-1};                                                   \
    PrimMeta meta;                                                           \
    std::map<std::string, Property> props;                                   \
    const std::vector<value::token> &primChildrenNames() const {             \
      return _primChildren;                                                  \
    }                                                                        \
    const std::vector<value::token> &propertyNames() const {                 \
      return _properties;                                                    \
    }                                                                        \
    std::vector<value::token> &primChildrenNames() { return _primChildren; } \
    std::vector<value::token> &propertyNames() { return _properties; }       \
                                                                             \
   private:                                                                  \
    std::vector<value::token> _primChildren;                                 \
    std::vector<value::token> _properties;                                   \
  }

// UsdVol
TINYUSDZ_DEFINE_PLACEHOLDER_PRIM(Volume);
TINYUSDZ_DEFINE_PLACEHOLDER_PRIM(OpenVDBAsset);
TINYUSDZ_DEFINE_PLACEHOLDER_PRIM(Field3DAsset);

// UsdRender
TINYUSDZ_DEFINE_PLACEHOLDER_PRIM(RenderSettings);
TINYUSDZ_DEFINE_PLACEHOLDER_PRIM(RenderProduct);
TINYUSDZ_DEFINE_PLACEHOLDER_PRIM(RenderVar);

// UsdProc
TINYUSDZ_DEFINE_PLACEHOLDER_PRIM(GenerativeProcedural);

#undef TINYUSDZ_DEFINE_PLACEHOLDER_PRIM

// `Scope` is uncommon in graphics community, its something like `Group`.
// From USD doc: Scope is the simplest grouping primitive, and does not carry
// the baggage of transformability.
struct Scope : Collection, MaterialBinding {
  Scope() = default;
  Scope(const Scope &) = default;
  Scope(Scope &&) noexcept = default;
  Scope &operator=(const Scope &) = default;
  Scope &operator=(Scope &&) noexcept = default;

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

namespace value {

#include "define-type-trait.inc"

DEFINE_TYPE_TRAIT(Model, "Model", TYPE_ID_MODEL, 1);
DEFINE_TYPE_TRAIT(Scope, "Scope", TYPE_ID_SCOPE, 1);

// UsdVol / UsdRender placeholder prim types.
DEFINE_TYPE_TRAIT(Volume, "Volume", TYPE_ID_VOLUME, 1);
DEFINE_TYPE_TRAIT(OpenVDBAsset, "OpenVDBAsset", TYPE_ID_OPENVDB_ASSET, 1);
DEFINE_TYPE_TRAIT(Field3DAsset, "Field3DAsset", TYPE_ID_FIELD3D_ASSET, 1);
DEFINE_TYPE_TRAIT(RenderSettings, "RenderSettings", TYPE_ID_RENDER_SETTINGS, 1);
DEFINE_TYPE_TRAIT(RenderProduct, "RenderProduct", TYPE_ID_RENDER_PRODUCT, 1);
DEFINE_TYPE_TRAIT(RenderVar, "RenderVar", TYPE_ID_RENDER_VAR, 1);
DEFINE_TYPE_TRAIT(GenerativeProcedural, "GenerativeProcedural", TYPE_ID_GENERATIVE_PROCEDURAL, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
