// SPDX-License-Identifier: Apache 2.0

///
/// @file property.hh
/// @brief USD Property class definition
///
/// Properties are the base containers for both Attributes and Relationships
/// in USD. This class provides a unified interface for working with both
/// types of properties.
///
#pragma once

#include <string>
#include <vector>

#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

#include "value-types.hh"
#include "attribute.hh"
#include "relationship.hh"

#ifndef TINYUSDZ_INSIDE_PRIM_TYPES
namespace tinyusdz {
#endif

// Forward declarations
class Path;
enum class ListEditQual;

///
/// @brief Generic container for Attribute or Relation/Connection
///
/// Properties can represent either Attributes (which hold values) or 
/// Relationships (which reference other scene graph objects). This class
/// also tracks whether the property is custom or builtin.
///
class Property {
 public:
  enum class Type {
    EmptyAttrib,        // Attrib with no data.
    Attrib,             // Attrib which contains actual data
    Relation,           // `rel` with targetPath(s).
    NoTargetsRelation,  // `rel` with no targets.
    Connection,  // Connection attribute(`.connect` suffix). TODO: Deprecate
                 // this and use Attrib.
  };

  Property() {
    TUSDZ_LOG_I("Property default constructor called");
  }

  template <typename T>
  Property(bool custom = false) : _has_custom(custom) {
    _attrib.set_type_name(value::TypeTraits<T>::type_name());
    _type = Type::EmptyAttrib;
  }

  static Property MakeEmptyAttrib(const std::string &type_name,
                                  bool custom = false) {
    Property p;
    p.set_custom(custom);
    p.set_property_type(Type::EmptyAttrib);
    p.attribute().set_type_name(type_name);
    return p;
  }

  Property(const Attribute &a, bool custom = false)
      : _attrib(a), _has_custom(custom) {
    _type = Type::Attrib;
  }

  Property(Attribute &&a, bool custom = false)
      : _attrib(std::move(a)), _has_custom(custom) {
    _type = Type::Attrib;
  }

  // Relationship(typeless)
  Property(const Relationship &r, bool custom = false)
      : _rel(r), _has_custom(custom) {
    _type = Type::Relation;
    set_listedit_qual(r.get_listedit_qual());
  }

  // Relationship(typeless)
  Property(Relationship &&r, bool custom = false)
      : _has_custom(custom) {
    _type = Type::Relation;
    set_listedit_qual(r.get_listedit_qual());
    _rel = std::move(r);
  }

  // Attribute Connection: has type
  Property(const Path &path, const std::string &prop_value_type_name,
           bool custom = false)
      : _prop_value_type_name(prop_value_type_name), _has_custom(custom) {
    _attrib.set_connection(path);
    _attrib.set_type_name(prop_value_type_name);
    _type = Type::Connection;
  }

  // Attribute Connection: has multiple targetPaths
  Property(const std::vector<Path> &paths,
           const std::string &prop_value_type_name, bool custom = false)
      : _prop_value_type_name(prop_value_type_name), _has_custom(custom) {
    _attrib.set_connections(paths);
    _attrib.set_type_name(prop_value_type_name);
    _type = Type::Connection;
  }

  // Copy constructor
  Property(const Property& rhs) 
      : _attrib(rhs._attrib),
        _listOpQual(rhs._listOpQual),
        _type(rhs._type),
        _rel(rhs._rel),
        _prop_value_type_name(rhs._prop_value_type_name),
        _has_custom(rhs._has_custom) {
    TUSDZ_LOG_I("Property copy constructor called");
  }

  // Move constructor
  Property(Property&& rhs) noexcept
      : _attrib(std::move(rhs._attrib)),
        _listOpQual(rhs._listOpQual),
        _type(rhs._type),
        _rel(std::move(rhs._rel)),
        _prop_value_type_name(std::move(rhs._prop_value_type_name)),
        _has_custom(rhs._has_custom) {
    TUSDZ_LOG_I("Property move constructor called");
  }

  // Copy assignment operator
  Property& operator=(const Property& rhs) {
    TUSDZ_LOG_I("Property copy assignment operator called");
    if (this != &rhs) {
      _type = rhs._type;
      _attrib = rhs._attrib;
      _rel = rhs._rel;
      _prop_value_type_name = rhs._prop_value_type_name;
      _has_custom = rhs._has_custom;
      _listOpQual = rhs._listOpQual;
    }
    return *this;
  }

  // Move assignment operator
  Property& operator=(Property&& rhs) noexcept {
    TUSDZ_LOG_I("Property move assignment operator called");
    if (this != &rhs) {
      _type = rhs._type;
      _attrib = std::move(rhs._attrib);
      _rel = std::move(rhs._rel);
      _prop_value_type_name = std::move(rhs._prop_value_type_name);
      _has_custom = rhs._has_custom;
      _listOpQual = rhs._listOpQual;
    }
    return *this;
  }

  bool is_attribute() const {
    return (_type == Type::EmptyAttrib) || (_type == Type::Attrib);
  }
  bool is_empty() const {
    return (_type == Type::EmptyAttrib) || (_type == Type::NoTargetsRelation);
  }
  bool is_relationship() const {
    return (_type == Type::Relation) || (_type == Type::NoTargetsRelation);
  }

  // TODO: Deprecate this and use is_attribute_connection
  //bool is_connection() const { return _type == Type::Connection; }

  bool is_attribute_connection() const {
    if (is_attribute()) {
      return _attrib.is_connection();
    }

    return false;
  }

  std::string value_type_name() const {
    if (is_relationship()) {
      // relation is typeless.
      return std::string();
    } else {
      return _attrib.type_name();
    }
  }

  bool has_custom() const { return _has_custom; }
  void set_custom(const bool onoff) { _has_custom = onoff; }

  void set_property_type(Type ty) { _type = ty; }

  Type get_property_type() const { return _type; }

  void set_listedit_qual(ListEditQual qual) { _listOpQual = qual; }

  const Attribute &get_attribute() const { return _attrib; }

  Attribute &attribute() { return _attrib; }

  void set_attribute(const Attribute &attrib) {
    _attrib = attrib;
    _type = Type::Attrib;
  }

  const Relationship &get_relationship() const { return _rel; }

  Relationship &relationship() { return _rel; }

  ///
  /// Convienient methos when Property is a Relationship
  ///

  ///
  /// Return single relationTarget path when Property is a Relationship.
  /// Return the first path when Relationship is composed of PathVector(multiple
  /// paths)
  ///
  nonstd::optional<Path> get_relationTarget() const {

    if (_rel.is_path()) {
      return _rel.targetPath;
    } else if (_rel.is_pathvector()) {
      if (_rel.targetPathVector.size() > 0) {
        return _rel.targetPathVector[0];
      }
    }

    return nonstd::nullopt;
  }

  ///
  /// Return multiple relationTarget paths when Property is a Relationship.
  /// Returns empty when Property is not a Relationship or a Relationship does
  /// not contain any target paths.
  ///
  std::vector<Path> get_relationTargets() const {
    std::vector<Path> pv;

    if (_rel.is_path()) {
      pv.push_back(_rel.targetPath);
    } else if (_rel.is_pathvector()) {
      pv = _rel.targetPathVector;
    }

    return pv;
  }

  ListEditQual get_listedit_qual() const { return _listOpQual; }

  size_t estimate_memory_usage() const;

 private:
  Attribute _attrib;  // attribute(value or ".connect")

  // List Edit qualifier(Attribute can never be list editable)
  // TODO:  Store listEdit qualifier to `Relation`
  ListEditQual _listOpQual{ListEditQual::ResetToExplicit};

  Type _type{Type::EmptyAttrib};
  Relationship _rel;                  // Relation(`rel`)
  std::string _prop_value_type_name;  // for Connection.
  bool _has_custom{false};  // Qualified with 'custom' keyword? This will be
                            // deprecated though
};

#ifndef TINYUSDZ_INSIDE_PRIM_TYPES
}  // namespace tinyusdz
#endif

