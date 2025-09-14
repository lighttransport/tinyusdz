// SPDX-License-Identifier: Apache 2.0

///
/// @file relationship.hh
/// @brief USD Relationship class definition
///
/// Relationships are USD's mechanism for creating typed connections between
/// scene graph objects. They are used to express dependencies and associations
/// between prims and properties.
///
#pragma once

#include <string>
#include <vector>

#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

#include "value-types.hh"
#include "path.hh"

namespace tinyusdz {

// Forward declarations
//class Path;
struct AttrMetas;
using AttrMeta = AttrMetas;
enum class ListEditQual;

///
/// @brief USD Relationship class
///
/// Relationships are lightweight connections between prims or properties.
/// Unlike attributes, relationships do not contain data values - they only
/// reference target paths in the scene graph.
///
class Relationship {
 public:
  // NOTE: no explicit `uniform` variability for Relationship
  // Relatinship have `uniform` variability implicitly.
  // (in Crate, variability is encoded as `uniform`)

  // (varying?) rel myrel    : DefineOnly(or empty)
  // (varying?) rel myrel = </a> : Path
  // (varying?) rel myrel = [</a>, </b>, ...] : PathVector
  // (varying?) rel myrel = None : ValueBlock
  //
  enum class Type { DefineOnly, Path, PathVector, ValueBlock };

  // TODO: move to private
  Type type{Type::DefineOnly};
  Path targetPath;
  std::vector<Path> targetPathVector;
  ListEditQual listOpQual{ListEditQual::ResetToExplicit};

  void set_listedit_qual(ListEditQual q) { listOpQual = q; }
  ListEditQual get_listedit_qual() const { return listOpQual; }

  void set_novalue() { type = Type::DefineOnly; }

  void set(const Path &p) {
    targetPath = p;
    type = Type::Path;
  }

  void set(const std::vector<Path> &pv) {
    targetPathVector = pv;
    type = Type::PathVector;
  }

  void set(const value::ValueBlock &v) {
    (void)v;
    type = Type::ValueBlock;
  }

  void set_blocked() { type = Type::ValueBlock; }

  bool has_value() const { return type != Type::DefineOnly; }

  bool is_path() const { return type == Type::Path; }

  bool is_pathvector() const { return type == Type::PathVector; }

  bool is_blocked() const { return type == Type::ValueBlock; }

  void set_varying_authored() { _varying_authored = true; }

  bool is_varying_authored() const { return _varying_authored; }

  const AttrMeta &metas() const { return *_metas; }
  AttrMeta &metas();

  size_t estimate_memory_usage() const;

 private:
  AttrMeta* _metas{nullptr};

  // `varying` keyword is explicitly specified?
  bool _varying_authored{false};
};

//
// To represent Property which is explicitly Relationship(for builtin property)
//
// - When authored()
//   - !has_value() => "rel material:binding"
//   - has_value() => targetPath or array of targetPath. "rel material:binding =
//   </rel>" or "rel material:binding = [</rel1>, </rel2>]"
//   - is_blocked() => "rel material:binding = None"
//
class RelationshipProperty {
 public:
  RelationshipProperty() = default;

  RelationshipProperty(const Relationship &rel)
      : _authored(true), _relationship(rel) {}

  RelationshipProperty(const Path &p) { set(p); }

  RelationshipProperty(const std::vector<Path> &pv) { set(pv); }

  RelationshipProperty(const value::ValueBlock &v) { set(v); }

  void set_listedit_qual(ListEditQual q) { _relationship.set_listedit_qual(q); }
  ListEditQual get_listedit_qual() const {
    return _relationship.get_listedit_qual();
  }

  void set_authored() { _authored = true; }

  bool authored() const { return _authored; }

  // Declare-only: e.g. `rel myrel`
  void set_empty() {
    _relationship.set_novalue();
    _authored = true;
  }

  void set(const Path &p) {
    _relationship.set(p);
    _authored = true;
  }

  void set(const std::vector<Path> &pv) {
    _relationship.set(pv);
    _authored = true;
  }

  void set(const value::ValueBlock &v) {
    (void)v;
    _relationship.set_blocked();
    _authored = true;
  }

  void set_blocked() {
    _relationship.set_blocked();
    _authored = true;
  }

  const std::vector<Path> get_targetPaths() const {
    std::vector<Path> paths;
    if (_relationship.is_path()) {
      paths.push_back(_relationship.targetPath);
    } else if (_relationship.is_pathvector()) {
      paths = _relationship.targetPathVector;
    }
    return paths;
  }

  // TODO: Deprecate this direct access API to Relationship value?
  const Relationship &relationship() const { return _relationship; }

  Relationship &relationship() { return _relationship; }

  bool has_value() const { return _relationship.has_value(); }

  bool is_blocked() const { return _relationship.is_blocked(); }

  const AttrMeta &metas() const { return _relationship.metas(); }
  AttrMeta &metas() { return _relationship.metas(); }

 private:
  bool _authored{false};
  Relationship _relationship;
};

}  // namespace tinyusdz

