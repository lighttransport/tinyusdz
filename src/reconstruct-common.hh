// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Common utilities for primitive reconstruction
#pragma once

#include <string>
#include <set>
#include <functional>
#include "prim-types.hh"
#include "value-types.hh"
#include "property.hh"
#include "nonstd/expected.hpp"
#include "tiny-format.hh"

// For PUSH_ERROR_AND_RETURN
#ifndef PushError
#define PushError(s) if (err) { (*err) = s + (*err); }
#endif

#ifndef PushWarn
#define PushWarn(s) if (warn) { (*warn) = s + (*err); }
#endif

#ifndef PUSH_ERROR_AND_RETURN
#define PUSH_ERROR_AND_RETURN(msg) \
  do { \
    if (err) { \
      (*err) = msg; \
    } \
    return false; \
  } while (0)
#endif

#ifndef PUSH_WARN
#define PUSH_WARN(msg) \
  do { \
    if (warn) { \
      (*warn) = msg; \
    } \
  } while (0)
#endif

// __VA_ARGS__ does not allow empty, thus # of args must be 2+
#ifndef PUSH_WARN_F
#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))
#endif

#ifndef PUSH_ERROR_AND_RETURN_F
#define PUSH_ERROR_AND_RETURN_F(s, ...) PUSH_ERROR_AND_RETURN(fmt::format(s, __VA_ARGS__))
#endif

namespace tinyusdz {
namespace prim {

// Parse result codes for property parsing
struct ParseResult
{
  enum class ResultCode
  {
    Success,
    Unmatched,
    AlreadyProcessed,
    TypeMismatch,
    VariabilityMismatch,
    ConnectionNotAllowed,
    InvalidConnection,
    PropertyTypeMismatch,
    InternalError,
  };

  ResultCode code;
  std::string err;
};

// Convert PrimVar to Animatable type
template<typename T>
nonstd::optional<Animatable<T>> ConvertToAnimatable(const primvar::PrimVar &var);

// Special case for Extent type
template<>
nonstd::optional<Animatable<Extent>> ConvertToAnimatable(const primvar::PrimVar &var);

// Parse typed attributes with various configurations
template<typename T>
ParseResult ParseTypedAttribute(std::set<std::string> &table,
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttributeWithFallback<Animatable<T>> &target);

template<typename T>
ParseResult ParseTypedAttribute(std::set<std::string> &table,
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttributeWithFallback<T> &target);

template<typename T>
ParseResult ParseTypedAttribute(std::set<std::string> &table,
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttribute<Animatable<T>> &target);

template<typename T>
ParseResult ParseTypedAttribute(std::set<std::string> &table,
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttribute<T> &target);

// Parse extent attribute (special case)
ParseResult ParseExtentAttribute(std::set<std::string> &table,
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttribute<Animatable<Extent>> &target);

// Parse shader output terminal attributes
template<typename T>
ParseResult ParseShaderOutputTerminalAttribute(std::set<std::string> &table,
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedTerminalAttribute<T> &target);

// Parse shader input connection properties
ParseResult ParseShaderInputConnectionProperty(std::set<std::string> &table,
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedConnection<value::token> &target);

// Enum handlers
template <typename EnumTy>
using EnumHandlerFun = std::function<nonstd::expected<EnumTy, std::string>(
    const std::string &)>;

template <typename T>
nonstd::expected<T, std::string> EnumHandler(
    const std::string &prop_name, const std::string &tok,
    const std::vector<std::pair<T, const char *>> &enums);

template <class E, size_t N>
nonstd::expected<bool, std::string> CheckAllowedTokens(
    const std::array<std::pair<E, const char *>, N> &allowedTokens,
    const std::string &tok);

template <class E>
nonstd::expected<bool, std::string> CheckAllowedTokens(
    const std::vector<std::pair<E, const char *>> &allowedTokens,
    const std::string &tok);

// Parse enum properties
template<typename T, typename EnumTy>
bool ParseUniformEnumProperty(
  const std::string &prop_name,
  bool strict_allowedToken_check,
  EnumHandlerFun<EnumTy> enum_handler,
  const Attribute &attr,
  TypedAttributeWithFallback<T> *result,
  std::string *warn = nullptr,
  std::string *err = nullptr);

template<typename T, typename EnumTy>
bool ParseTimeSampledEnumProperty(
  const std::string &prop_name,
  bool strict_allowedToken_check,
  EnumHandlerFun<EnumTy> enum_handler,
  const Attribute &attr,
  TypedAttributeWithFallback<Animatable<T>> *result,
  std::string *warn = nullptr,
  std::string *err = nullptr);

// Common enum handlers
nonstd::expected<Axis, std::string> AxisEnumHandler(const std::string &tok);
nonstd::expected<Visibility, std::string> VisibilityEnumHandler(const std::string &tok);
nonstd::expected<Purpose, std::string> PurposeEnumHandler(const std::string &tok);
nonstd::expected<Orientation, std::string> OrientationEnumHandler(const std::string &tok);

// Macro helpers for property parsing
#define PARSE_TYPED_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    continue; \
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing attribute `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

#define PARSE_TYPED_ATTRIBUTE_NOCONTINUE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing attribute `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

#define PARSE_EXTENT_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseExtentAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    continue; \
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing attribute `extent` failed. Error: {}", ret.err)); \
  } \
}

#define PARSE_SINGLE_TARGET_PATH_RELATION(__table, __prop, __propname, __target) \
  if (prop.first == __propname) { \
    if (__table.count(__propname)) { \
       continue; \
    } \
    if (!prop.second.is_relationship()) { \
      PUSH_ERROR_AND_RETURN(fmt::format("Property `{}` must be a Relationship.", __propname)); \
    } \
    const Relationship &rel = prop.second.get_relationship(); \
    if (rel.is_path()) { \
      __target = rel; \
      table.insert(prop.first); \
      DCOUT("Added rel " << __propname); \
      continue; \
    } else if (rel.is_pathvector()) { \
      if (rel.targetPathVector.size() == 1) { \
        __target = rel; \
        table.insert(prop.first); \
        DCOUT("Added rel " << __propname); \
        continue; \
      } \
      PUSH_ERROR_AND_RETURN(fmt::format("`{}` target is empty or has mutiple Paths. Must be single Path.", __propname)); \
    } else if (!rel.has_value()) { \
      __target = rel; \
      table.insert(prop.first); \
      DCOUT("Added rel " << __propname); \
    } else if (rel.is_blocked()) { \
      __target = rel; \
      table.insert(prop.first); \
      DCOUT("Added ValueBlocked rel " << __propname); \
    } else { \
      PUSH_ERROR_AND_RETURN(fmt::format("Internal error. Property `{}` is not a valid Relationship.", __propname)); \
    } \
  }

#define PARSE_TARGET_PATHS_RELATION(__table, __prop, __propname, __target) \
  if (prop.first == __propname) { \
    if (__table.count(__propname)) { \
       continue; \
    } \
    if (!prop.second.is_relationship()) { \
      PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", __propname)); \
    } \
    const Relationship &rel = prop.second.get_relationship(); \
    __target = rel; \
    table.insert(prop.first); \
    DCOUT("Added rel " << __propname); \
    continue; \
  }

#define PARSE_SHADER_TERMINAL_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseShaderOutputTerminalAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    DCOUT("Added shader terminal attribute: " << __name); \
    continue; \
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing shader output property `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

#define PARSE_SHADER_INPUT_CONNECTION_PROPERTY(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseShaderInputConnectionProperty(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    DCOUT("Added shader input connection: " << __name); \
    continue; \
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing shader property `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

#define PARSE_UNIFORM_ENUM_PROPERTY(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                           __target, __strict_check) { \
  if (__prop.first == __name) { \
    if (__table.count(__name)) { continue; } \
    if ((__prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) && __prop.second.is_attribute() && __prop.second.is_empty()) { \
      PUSH_WARN("No value assigned to `" << __name << "` token attribute. Set default token value."); \
      __target.metas() = __prop.second.get_attribute().metas(); \
      __table.insert(__name); \
      continue; \
    } else { \
      const Attribute &attr = __prop.second.get_attribute(); \
      std::function<nonstd::expected<__enum_ty, std::string>(const std::string &)> fun = __enum_handler; \
      if (!ParseUniformEnumProperty(__name, __strict_check, fun, attr, &__target, warn, err)) { \
        return false; \
      } \
      __target.metas() = attr.metas(); \
      __table.insert(__name); \
      continue; \
    } \
  } \
}

#define PARSE_TIMESAMPLED_ENUM_PROPERTY(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                           __target, __strict_check) { \
  if (__prop.first == __name) { \
    if (__table.count(__name)) { continue; } \
    if ((__prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) && __prop.second.is_attribute() && __prop.second.is_empty()) { \
      PUSH_WARN("No value assigned to `" << __name << "` token attribute. Set default token value."); \
      const Attribute &attr = __prop.second.get_attribute(); \
      __target.metas() = attr.metas(); \
      __table.insert(__name); \
      continue; \
    } else { \
      const Attribute &attr = __prop.second.get_attribute(); \
      std::function<nonstd::expected<__enum_ty, std::string>(const std::string &)> fun = __enum_handler; \
      if (!ParseTimeSampledEnumProperty(__name, __strict_check, fun, attr, &__target, warn, err)) { \
        return false; \
      } \
      __target.metas() = attr.metas(); \
     __table.insert(__name); \
     continue; \
    } \
  } \
}

#define ADD_PROPERTY(__table, __prop, __klass, __dst) { \
  if (!__table.count(__prop.first)) { \
    DCOUT("custom property added: name = " << __prop.first); \
    __dst[__prop.first] = __prop.second; \
    __table.insert(__prop.first); \
  } \
 }

#define PARSE_PROPERTY_END_MAKE_ERROR(__table, __prop) { \
  if (!__table.count(__prop.first)) { \
    PUSH_ERROR_AND_RETURN("Unsupported/unimplemented property: " + \
                          __prop.first); \
  } \
 }

#define PARSE_PROPERTY_END_MAKE_WARN(__table, __prop) { \
  if (!__table.count(__prop.first)) { \
    PUSH_WARN("Unsupported/unimplemented property: " + \
              __prop.first); \
  } \
 }

// Common constant strings
constexpr auto kProxyPrim = "proxyPrim";
constexpr auto kVisibility = "visibility";
constexpr auto kExtent = "extent";
constexpr auto kPurpose = "purpose";
constexpr auto kMaterialBinding = "material:binding";
constexpr auto kMaterialBindingCollection = "material:binding:collection";
constexpr auto kMaterialBindingPreview = "material:binding:preview";
constexpr auto kSkelSkeleton = "skel:skeleton";
constexpr auto kSkelAnimationSource = "skel:animationSource";
constexpr auto kSkelBlendShapes = "skel:blendShapes";
constexpr auto kSkelBlendShapeTargets = "skel:blendShapeTargets";
constexpr auto kInputsVarname = "inputs:varname";

} // namespace prim
} // namespace tinyusdz