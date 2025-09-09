// SPDX-License-Identifier: Apache 2.0

///
/// @file dictionary.hh
/// @brief USD Dictionary and CustomDataType definitions
///
/// Contains Dictionary and CustomDataType for handling custom metadata
/// and arbitrary key-value data structures in USD.
///
#pragma once

#include <map>
#include <string>
#include <vector>

#include "value-types.hh"
#include "nonstd/optional.hpp"

namespace tinyusdz {

// Forward declaration
class MetaVariable;

// TODO: Use `Dictionary` and deprecate CustomDataType
using CustomDataType = std::map<std::string, MetaVariable>;

using Dictionary = CustomDataType;  // alias to CustomDataType

///
/// Helper function to access CustomData(dictionary).
/// Recursively process into subdictionaries when a key contains namespaces(':')
///
bool HasCustomDataKey(const Dictionary &customData, const std::string &key);
bool GetCustomDataByKey(const Dictionary &customData, const std::string &key,
                        /* out */ MetaVariable *dst);
bool SetCustomDataByKey(const std::string &key, const MetaVariable &val,
                        /* inout */ Dictionary &customData);

void OverrideDictionary(Dictionary &customData, const Dictionary &src, const bool override_existing = true);

///
/// Variable class for Prim and Attribute Metadataum.
///
/// Stores typed metadata values for USD properties and primitives.
/// Supports various USD value types and provides type-safe access.
///
class MetaVariable {
 public:
  MetaVariable &operator=(const MetaVariable &rhs) {
    _name = rhs._name;
    _value = rhs._value;

    return *this;
  }

  template <typename T>
  MetaVariable(const T &v) {
    set_value(v);
  }

  MetaVariable(const MetaVariable &rhs) {
    _name = rhs._name;
    _value = rhs._value;
  }

  template <typename T>
  MetaVariable(const std::string &name, const T &v) {
    set_value(name, v);
  }

  // template <typename T>
  // bool is() const {
  //   return value.index() == ValueType::index_of<T>();
  // }

  bool is_valid() const {
    return _value.type_id() != value::TypeTraits<std::nullptr_t>::type_id();
  }

  //// TODO
  // bool is_timesamples() const { return false; }

  MetaVariable() = default;

  //
  // custom data must have some value, so no set_type()
  // OK "float myval = 1"
  // NG "float myval"
  //
  template <typename T>
  void set_value(const T &v) {
    // TODO: Check T is supported type for Metadatum.
    _value = v;

    _name = std::string();  // empty
  }

  template <typename T>
  void set_value(const std::string &name, const T &v) {
    // TODO: Check T is supported type for Metadatum.
    _value = v;

    _name = name;
  }

  template <typename T>
  bool get_value(T *dst) const {
    if (!dst) {
      return false;
    }

    if (const T *v = _value.as<T>()) {
      (*dst) = *v;
      return true;
    }

    return false;
  }

  template <typename T>
  nonstd::optional<T> get_value() const {
    if (const T *v = _value.as<T>()) {
      return *v;
    }

    return nonstd::nullopt;
  }

  void set_name(const std::string &name) { _name = name; }
  const std::string &get_name() const { return _name; }

  const value::Value &get_raw_value() const { return _value; }
  value::Value &get_raw_value() { return _value; }

  // No set_type_name()
  const std::string type_name() const { return TypeName(*this); }

  uint32_t type_id() const { return TypeId(*this); }

  bool is_blocked() const {
    return type_id() == value::TypeId::TYPE_ID_VALUEBLOCK;
  }

 private:
  static std::string TypeName(const MetaVariable &v) {
    return v._value.type_name();
  }

  static uint32_t TypeId(const MetaVariable &v) { return v._value.type_id(); }

 private:
  value::Value _value{nullptr};
  std::string _name;
};

//DEFINE_TYPE_TRAIT(CustomDataType, "customData", TYPE_ID_CUSTOMDATA,
//                  1);  // TODO: Unify with `dict`?

}  // namespace tinyusdz
