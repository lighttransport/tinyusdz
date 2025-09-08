// SPDX-License-Identifier: Apache 2.0

///
/// @file primspec.hh
/// @brief USD PrimSpec class definition
///
/// PrimSpec represents a prim specification in a USD layer. It contains
/// the prim's definition including its properties, metadata, and child prims.
///
#pragma once

#include <string>
#include <vector>
#include <map>

#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

#include "value-types.hh"
#include "property.hh"

#ifndef TINYUSDZ_INSIDE_PRIM_TYPES
namespace tinyusdz {
#endif

// Forward declarations
class Path;
class Prim;
struct PrimMetas;
using PrimMeta = PrimMetas;
struct VariantSetSpec;
struct Reference;
struct Payload;
enum class Specifier;
enum class ListEditQual;

using VariantSelectionMap = std::map<std::string, std::string>;

///
/// @brief PrimSpec class representing a prim specification in USD
///
/// PrimSpec contains the complete specification of a prim including its
/// type, properties, metadata, child prims, and variant information.
///
class PrimSpec {
 public:
  PrimSpec() {
    TUSDZ_LOG_I("PrimSpec default constructor called");
  }

  PrimSpec(const Specifier &spec, const std::string &name)
      : _specifier(spec), _name(name) {
    TUSDZ_LOG_I("PrimSpec constructor called with spec and name: " << name);
  }
  PrimSpec(const Specifier &spec, const std::string &typeName,
           const std::string &name)
      : _specifier(spec), _typeName(typeName), _name(name) {
    TUSDZ_LOG_I("PrimSpec constructor called with spec, typeName, and name: " << name);
  }

  PrimSpec(const PrimSpec &rhs) {
    TUSDZ_LOG_I("PrimSpec copy constructor called");
    if (this != &rhs) {
      CopyFrom(rhs);
    }
  }

  PrimSpec &operator=(const PrimSpec &rhs) {
    TUSDZ_LOG_I("PrimSpec copy assignment operator called");
    if (this != &rhs) {
      CopyFrom(rhs);
    }

    return *this;
  }

  PrimSpec &operator=(PrimSpec &&rhs) noexcept {
    TUSDZ_LOG_I("PrimSpec move assignment operator called");
    if (this != &rhs) {
      MoveFrom(rhs);
    }

    return *this;
  }

  const std::string &name() const { return _name; }
  std::string &name() { return _name; }

  const std::string &typeName() const { return _typeName; }
  // Can change type name
  std::string &typeName() { return _typeName; }

  const Specifier &specifier() const { return _specifier; }
  Specifier &specifier() { return _specifier; }

  const std::vector<PrimSpec> &children() const { return _children; }
  std::vector<PrimSpec> &children() { return _children; }

  ///
  /// Select variant.
  ///
  bool select_variant(const std::string &target_name,
                      const std::string &variant_name) {
    if (metas().variants.has_value()) {
      const auto m = metas().variants.value().find(target_name);
      if (m != metas().variants.value().end()) {
        _current_vsmap[target_name] = variant_name;
        return true;
      } else {
        return false;
      }
    }
    return false;
  }

  bool current_variant_selection(const std::string &target_name,
                      std::string *selected_variant_name) {

    if (!selected_variant_name) {
      return false;
    }

    if (!metas().variants.has_value()) {
      return false;
    }

    const auto &vsmap = metas().variants.value();

    const auto m = vsmap.find(target_name);
    if (m != vsmap.end()) {
      const auto sm = _current_vsmap.find(target_name);
      if (sm != _current_vsmap.end()) {
        (*selected_variant_name) = sm->second;
      } else {
        (*selected_variant_name) = m->second;
      }
      return true;
    } else {
      return false;
    }
  }

  ///
  /// List variants in this PrimSpec
  /// key = variant name
  /// value = variats
  ///
  const VariantSelectionMap get_variant_selection_map() const {
    VariantSelectionMap vsmap;
    if (metas().variants.has_value()) {
      vsmap = metas().variants.value();
    }
    return vsmap;
  }

  ///
  /// Variants
  ///
  /// VariantSet = Prim metas + Properties and/or child Prims
  ///            = repsetent as PrimNode for a while.
  ///
  ///
  /// key = variant name
  std::map<std::string, VariantSetSpec> &variantSets() { return _variantSets; }
  const std::map<std::string, VariantSetSpec> &variantSets() const { return _variantSets; }

  const PrimMeta &metas() const { return *_metas; }

  PrimMeta &metas();

  using PropertyMap = std::map<std::string, Property>;

  const PropertyMap &props() const { return _props; }
  PropertyMap &props() { return _props; }

  const std::vector<Reference> &get_references();
  const ListEditQual &get_references_listedit_qualifier();

  const std::vector<Payload> &get_payloads();
  const ListEditQual &get_payloads_listedit_qualifier();

  const std::vector<value::token> &primChildren() const {
    return _primChildren;
  }

  const std::vector<value::token> &propertyNames() const {
    return _properties;
  }

  const std::string &get_current_working_path() const {
    return _current_working_path;
  }

  const std::vector<std::string> &get_asset_search_paths() const {
    return _asset_search_paths;
  }

  void set_current_working_path(const std::string &s) {
    _current_working_path = s;
  }

  void set_asset_search_paths(const std::vector<std::string> &search_paths) {
    _asset_search_paths = search_paths;
  }

  void set_asset_resolution_state(
    const std::string &cwp, const std::vector<std::string> &search_paths) {
    _current_working_path = cwp;
    _asset_search_paths = search_paths;
  }

 private:
  void CopyFrom(const PrimSpec &rhs);
  void MoveFrom(PrimSpec &rhs);

  Specifier _specifier{Specifier::Def};
  std::string _typeName;  // prim's typeName(e.g. "Xform", "Material") This is
                          // identitical to `typeName` in Crate format)
  std::string _name;      // elementName. Should not be empty.

  std::vector<PrimSpec> _children;  // child nodes

  PropertyMap _props;

  ///
  /// Variants
  ///
  /// variant element = Property or Prim
  ///
  using PrimSpecMap = std::map<std::string, PrimSpec>;

  //VariantSelectionMap _vsmap;  // Original variant selections
  VariantSelectionMap _current_vsmap;  // Currently selected variants

  std::map<std::string, VariantSetSpec> _variantSets;

  std::vector<value::token> _primChildren;  // List of child PrimSpec nodes
  std::vector<value::token> _properties;    // List of property names
  std::vector<value::token> _variantChildren;

  PrimMeta* _metas{nullptr};

  ///
  /// For solving asset path in nested composition.
  /// Keep asset resolution state.
  /// TODO: Use struct. Store userdata pointer.
  ///
  std::string _current_working_path;
  std::vector<std::string> _asset_search_paths;
};

#ifndef TINYUSDZ_INSIDE_PRIM_TYPES
}  // namespace tinyusdz
#endif

