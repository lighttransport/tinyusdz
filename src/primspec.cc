// SPDX-License-Identifier: Apache 2.0

///
/// @file primspec.cc
/// @brief USD PrimSpec class implementation
///

#include "prim-types.hh"  // Must be included first for type definitions
#include "primspec.hh"

namespace tinyusdz {

PrimMeta &PrimSpec::metas() {
  if (!_metas) {
    _metas = new PrimMeta();
  }
  return *_metas;
}

const std::vector<Reference> &PrimSpec::get_references() {
  static std::vector<Reference> empty;
  if (_metas && _metas->references.has_value()) {
    return _metas->references.value().second;
  }
  return empty;
}

const ListEditQual &PrimSpec::get_references_listedit_qualifier() {
  static ListEditQual defaultQual = ListEditQual::ResetToExplicit;
  if (_metas && _metas->references.has_value()) {
    return _metas->references.value().first;
  }
  return defaultQual;
}

const std::vector<Payload> &PrimSpec::get_payloads() {
  static std::vector<Payload> empty;
  if (_metas && _metas->payload.has_value()) {
    return _metas->payload.value().second;
  }
  return empty;
}

const ListEditQual &PrimSpec::get_payloads_listedit_qualifier() {
  static ListEditQual defaultQual = ListEditQual::ResetToExplicit;
  if (_metas && _metas->payload.has_value()) {
    return _metas->payload.value().first;
  }
  return defaultQual;
}

void PrimSpec::CopyFrom(const PrimSpec &rhs) {
  _specifier = rhs._specifier;
  _typeName = rhs._typeName;
  _name = rhs._name;

  _children = rhs._children;

  _props = rhs._props;

  //_vsmap = rhs._vsmap;
  _current_vsmap = rhs._current_vsmap;

  _variantSets = rhs._variantSets;

  _primChildren = rhs._primChildren;
  _properties = rhs._properties;
  _variantChildren = rhs._variantChildren;

  delete _metas;
  _metas = nullptr;
  if (rhs._metas) {
    _metas = new PrimMeta(*rhs._metas);
  }

  _current_working_path = rhs._current_working_path;
  _asset_search_paths = rhs._asset_search_paths;
}

void PrimSpec::MoveFrom(PrimSpec &rhs) {
  _specifier = std::move(rhs._specifier);
  _typeName = std::move(rhs._typeName);
  _name = std::move(rhs._name);

  _children = std::move(rhs._children);

  _props = std::move(rhs._props);

  //_vsmap = std::move(rhs._vsmap);
  _current_vsmap = std::move(rhs._current_vsmap);

  _variantSets = std::move(rhs._variantSets);

  _primChildren = std::move(rhs._primChildren);
  _properties = std::move(rhs._properties);
  _variantChildren = std::move(rhs._variantChildren);

  delete _metas;
  _metas = rhs._metas;
  rhs._metas = nullptr;

  _current_working_path = rhs._current_working_path;
  _asset_search_paths = std::move(rhs._asset_search_paths);
}

}  // namespace tinyusdz

