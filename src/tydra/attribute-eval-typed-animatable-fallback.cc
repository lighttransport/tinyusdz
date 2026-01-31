// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
#include "attribute-eval.hh"
#include "scene-access.hh"

#include "common-macros.inc"
#include "pprinter.hh"
#include "tiny-format.hh"
#include "value-pprint.hh"

namespace tinyusdz {
namespace tydra {

// For PUSH_ERROR_AND_RETURN
#define PushError(msg) \
  if (err) {           \
    (*err) +=  msg;     \
  }

namespace {

// Convert TypedAttribute Connection to Attribute Connection.
// If TypedAttribute has value, return Attribute with empty value.
// TODO: make error when Attribute is not 'connection'.
template<typename T>
Attribute ToAttributeConnection(
  const TypedAttributeWithFallback<Animatable<T>> &input)
{
  Attribute attr;
  if (input.is_blocked()) {
    attr.set_blocked(true);
    attr.variability() = Variability::Varying;
  } else if (input.is_value_empty()) {
    // empty = set type info only
    attr.set_type_name(value::TypeTraits<T>::type_name());
    attr.variability() = Variability::Varying;

  } else if (input.is_connection()) {

    attr.set_connections(input.connections());

  } else{
    attr.set_type_name(value::TypeTraits<T>::type_name());
    attr.variability() = Variability::Varying;
  }

  return attr;
}

} // namespace

// std::string specialization - kept in the main file as it has unique logic
template<>
bool EvaluateTypedAnimatableAttribute(
    const tinyusdz::Stage &stage, const TypedAttributeWithFallback<Animatable<std::string>> &tattr,
    const std::string &attr_name,
    std::string *value_out,
    std::string *err,
    const double t,
    const value::TimeSampleInterpolationType tinterp) {

  if (!value_out) {
    PUSH_ERROR_AND_RETURN("`value_out` param is nullptr.");
  }

  if (tattr.is_blocked()) {
    if (err) {
      (*err) += "Attribute is Blocked.\n";
    }
    return false;
  } else if (tattr.is_value_empty()) {
    if (err) {
      (*err) += "Attribute value is empty.\n";
    }
    return false;
  } else if (tattr.has_value()) {
    const Animatable<std::string> &value = tattr.get_value();
    std::string v;
    if (value.get(t, &v, tinterp)) {
      return true;
    } else {
      if (err) {
        (*err) += fmt::format("Failed to get TypedAnimatableAttribute value: {} \n", attr_name);
      }
      return false;
    }

  } else if (tattr.has_connections()) {

    // Follow targetPath
    Attribute attr = ToAttributeConnection(tattr);

    //std::set<std::string> visited_paths;

    TerminalAttributeValue value;
    bool ret = EvaluateAttribute(stage, attr, attr_name, &value, err,
                                 value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);

    if (!ret) {
      return false;
    }

    if (auto pv = value.as<std::string>()) {
      (*value_out) = *pv;
      return true;
    }

    // Allow `token` typed value in the attribute of targetPath.
    if (auto pv = value.as<value::token>()) {
      // TODO: report an warninig.
      (*value_out) = pv->str();
      return true;
    }

    if (err) {
      (*err) += fmt::format("Type mismatch. Value producing attribute has type {}, but requested type is {}[]. Attribute: {}", value.type_name(), value::TypeTraits<std::string>::type_name(), attr_name);
    }

  } else {
    if (err) {
      (*err) += fmt::format("Unsupported/Invalid TypedAnimatableAttribute value: {}", attr_name);
    }
  }
  return false;
}

// Template instantiations are in split files for parallel compilation:
// - attribute-eval-typed-animatable-fallback-inst-scalar.cc (scalar types)
// - attribute-eval-typed-animatable-fallback-inst-array.cc (array types)

}  // namespace tydra
}  // namespace tinyusdz
