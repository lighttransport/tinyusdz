// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Reconstruct concrete Prim from PropertyMap or PrimSpec.
//
// TODO:
//   - [ ] Refactor code
//
#include "prim-reconstruct.hh"

#include "prim-types.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "tiny-format.hh"
#include "enum-handlers.hh"
#include "prim-property-tables.hh"

#include "usdGeom.hh"
#include "usdSkel.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdMtlx.hh"

#include "common-macros.inc"
#include "value-types.hh"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) if (err) { (*err) = s + (*err); }
#define PushWarn(s) if (warn) { (*warn) = s + (*err); }

// __VA_ARGS__ does not allow empty, thus # of args must be 2+
#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))
#define PUSH_ERROR_AND_RETURN_F(s, ...) PUSH_ERROR_AND_RETURN(fmt::format(s, __VA_ARGS__))

//
// NOTE:
//
// There are mainly 5 variant of Primtive property(relationship/attribute)
//
// - TypedAttribute<T> : Uniform only. `uniform T` or `uniform T var.connect`
// - TypedAttribute<Animatable<T>> : Varying. `T var`, `T var = val`, `T var.connect` or `T value.timeSamples`
// - optional<T> : For output attribute(Just author it. e.g. `float outputs:rgb`)
// - Relationship : Typeless relation(e.g. `rel material:binding`)
// - TypedConnection : Typed relation(e.g. `token outputs:result = </material/diffuse.rgb>`)

namespace tinyusdz {
namespace prim {

//constexpr auto kTag = "[PrimReconstruct]";

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

///
/// TinyUSDZ reconstruct some frequently used shaders(e.g. UsdPreviewSurface)
/// here, not in Tydra
///
template <typename T>
bool ReconstructShader(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    T *out,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options);

namespace {


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
  std::string warn;
};

#if 0
inline std::string to_string(ParseResult::ResultCode rescode) {
  switch (rescode) {
    case ParseResult::ResultCode::Success: return "success";
    case ParseResult::ResultCode::Unmatched: return "unmatched";
    case ParseResult::ResultCode::AlreadyProcessed: return "alreadyProcessed";
    case ParseResult::ResultCode::TypeMismatch: return "typeMismatch";
    case ParseResult::ResultCode::PropertyTypeMismatch: return "propertyTypeMismatch";
    case ParseResult::ResultCode::VariabilityMismatch: return "variabilityMismatch";
    case ParseResult::ResultCode::ConnectionNotAllowed: return "connectionNotAllowed";
    case ParseResult::ResultCode::InvalidConnection: return "invalidConnection";
    case ParseResult::ResultCode::InternalError: return "internalError";
  } 
  return "[[???ResultCode]]";
}
#endif

template<typename T>
static nonstd::optional<Animatable<T>> ConvertToAnimatable(const primvar::PrimVar &var)
{
  Animatable<T> dst;

  if (!var.is_valid()) {
    DCOUT("is_valid failed");
    DCOUT("has_value " << var.has_value());
    DCOUT("has_timesamples " << var.has_timesamples());
    return nonstd::nullopt;
  }

  bool ok = false;

  if (var.has_value()) {

    if (auto pv = var.get_value<T>()) {
      dst.set_default(pv.value());

      ok = true;
    }
  }

  if (var.has_timesamples()) {
    const auto &samples = var.ts_raw().get_samples();

    for (size_t i = 0; i < samples.size(); i++) {
      const value::TimeSamples::Sample &s = samples[i];

      // Attribute Block?
      if (s.blocked || s.value.is_none()) {
        dst.add_blocked_sample(s.t);
      } else if (const T *pv = s.value.as<T>()) {
        // Use as<T>() to get pointer directly, avoiding the extra copy
        // that get_value<T>() would make into nonstd::optional<T>
        dst.add_sample(s.t, *pv);
      } else {
        // Type mismatch
        DCOUT(i << "/" << var.ts_raw().size() << " type mismatch. expected " << value::TypeTraits<T>::type_name() << ", but got " << s.value.type_name());
        return nonstd::nullopt;
      }
    }

    ok = true;
  }

  if (ok) {
    return std::move(dst);
  }

  DCOUT("???");
  return nonstd::nullopt;
}

// Mutable overload: moves values out of PrimVar's TimeSamples to avoid deep copies
template<typename T>
static nonstd::optional<Animatable<T>> ConvertToAnimatable(primvar::PrimVar &var)
{
  // If PrimVar has shared (immutable) TimeSamples, use the const path
  // to avoid triggering COW deep copy. This is critical for USDC dedup
  // performance where hundreds of specs share the same TimeSamples.
  if (var.has_shared_timesamples()) {
    const primvar::PrimVar &cvar = var;
    return ConvertToAnimatable<T>(cvar);
  }

  Animatable<T> dst;

  if (!var.is_valid()) {
    return nonstd::nullopt;
  }

  bool ok = false;

  if (var.has_value()) {
    if (auto pv = var.get_value<T>()) {
      dst.set_default(pv.value());
      ok = true;
    }
  }

  if (var.has_timesamples()) {
    auto &samples = var.ts_raw().samples();

    for (size_t i = 0; i < samples.size(); i++) {
      value::TimeSamples::Sample &s = samples[i];

      if (s.blocked || s.value.is_none()) {
        dst.add_blocked_sample(s.t);
      } else if (T *pv = s.value.as<T>()) {
        // Move the value out — avoids deep-copying vector<quatf> etc.
        dst.add_sample(s.t, std::move(*pv));
      } else {
        DCOUT(i << "/" << var.ts_raw().size() << " type mismatch. expected " << value::TypeTraits<T>::type_name() << ", but got " << s.value.type_name());
        return nonstd::nullopt;
      }
    }

    ok = true;
  }

  if (ok) {
    return std::move(dst);
  }

  return nonstd::nullopt;
}

// Require special treatment for Extent(float3[2])
template<>
nonstd::optional<Animatable<Extent>> ConvertToAnimatable(const primvar::PrimVar &var)
{
  Animatable<Extent> dst;

  if (!var.is_valid()) {
    DCOUT("is_valid failed");
    return nonstd::nullopt;
  }

  bool value_ok = false;

  if (var.has_default()) {

    if (auto pv = var.get_value<std::vector<value::float3>>()) {
      if (pv.value().size() == 2) {
        Extent ext;
        ext.lower = pv.value()[0];
        ext.upper = pv.value()[1];

        dst.set_default(ext);

      } else {
        return nonstd::nullopt;
      }

      //return std::move(dst);
    }
    value_ok = true;
  }

  if (var.has_timesamples()) {
    for (size_t i = 0; i < var.ts_raw().size(); i++) {
      const value::TimeSamples::Sample &s = var.ts_raw().get_samples()[i];

      // Attribute Block?
      if (s.blocked || s.value.is_none()) {
        dst.add_blocked_sample(s.t);
      } else if (auto pv = s.value.get_value<std::vector<value::float3>>()) {
        if (pv.value().size() == 2) {
          Extent ext;
          ext.lower = pv.value()[0];
          ext.upper = pv.value()[1];
          dst.add_sample(s.t, ext);
        } else {
          DCOUT(i << "/" << var.ts_raw().size() << " array size mismatch.");
          return nonstd::nullopt;
        }
      } else {
        // Type mismatch
        DCOUT(i << "/" << var.ts_raw().size() << " type mismatch.");
        return nonstd::nullopt;
      }
    }

    value_ok = true;
    //return std::move(dst);
  }

  if (value_ok) {
    return std::move(dst);
  }

  DCOUT("???");
  return nonstd::nullopt;
}

// Mutable Extent specialization: Extent is small (24 bytes), delegate to const version
template<>
nonstd::optional<Animatable<Extent>> ConvertToAnimatable(primvar::PrimVar &var)
{
  const primvar::PrimVar &cvar = var;
  return ConvertToAnimatable<Extent>(cvar);
}

#if 0 // TODO: remove. moved to prim-types.cc
static bool ConvertTokenAttributeToStringAttribute(
  const TypedAttribute<Animatable<value::token>> &inp,
  TypedAttribute<Animatable<std::string>> &out) {

  out.metas() = inp.metas();

  if (inp.is_blocked()) {
    out.set_blocked(true);
  } else if (inp.is_value_empty()) {
    out.set_value_empty();
  }

  if (inp.has_connections()) {
    out.set_connections(inp.get_connections());
  }

  if (inp.has_value()) {
    Animatable<value::token> toks;
    Animatable<std::string> strs;
    if (inp.get_value(&toks)) {
      if (toks.is_blocked()) {
        // TODO
      }

      if (toks.has_default()) {
        value::token tok;
        toks.get_scalar(&tok);
        strs.set(tok.str());
      }

      
      if (toks.has_timesamples()) {
        auto tok_ts = toks.get_timesamples();

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
        for (auto &item : tok_ts.get_samples()) {
          strs.add_sample(item.t, item.value.str());
        }
#else
        const auto &times = tok_ts.get_times();
        const auto &values = tok_ts.get_values();
        for (size_t i = 0; i < times.size(); i++) {
          strs.add_sample(times[i], values[i].str());
        }
#endif
      }
    }
    out.set_value(strs);
  }

  return true;
}
#endif

#if 0 // not used anymore. TODO: remove
static bool ConvertStringDataAttributeToStringAttribute(
  const TypedAttribute<Animatable<value::StringData>> &inp,
  TypedAttribute<Animatable<std::string>> &out) {

  out.metas() = inp.metas();

  if (inp.is_blocked()) {
    out.set_blocked(true);
  } else if (inp.is_value_empty()) {
    out.set_value_empty();
  }


  if (inp.has_connections()) {
    out.set_connections(inp.get_connections());
  }
  
  if (inp.has_value()) {
    Animatable<value::StringData> toks;
    Animatable<std::string> strs;
    if (inp.get_value(&toks)) {
      if (toks.is_blocked()) {
        // TODO
      }

      if (toks.has_default()) {
        value::StringData tok;
        toks.get_scalar(&tok);
        strs.set(tok.value);
      }

      if (toks.has_timesamples()) {
        auto tok_ts = toks.get_timesamples();

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
        for (auto &item : tok_ts.get_samples()) {
          strs.add_sample(item.t, item.value.value);
        }
#else
        const auto &times = tok_ts.get_times();
        const auto &values = tok_ts.get_values();
        for (size_t i = 0; i < times.size(); i++) {
          strs.add_sample(times[i], values[i].value);
        }
#endif
      }
    }
    out.set_value(strs);
  }

  return true;
}
#endif

// ============================================================================
// Traits for ParseTypedAttribute unification
// ============================================================================

// Detect if type is Animatable<T>
template<typename T>
struct is_animatable : std::false_type {
  using value_type = T;
};

template<typename T>
struct is_animatable<Animatable<T>> : std::true_type {
  using value_type = T;
};

// Detect if type is TypedAttributeWithFallback<T>
template<typename T>
struct is_with_fallback : std::false_type {};

template<typename T>
struct is_with_fallback<TypedAttributeWithFallback<T>> : std::true_type {};

// Extract value type from target
template<typename Target>
struct target_traits; // Forward declaration

// Specialization for TypedAttribute<T>
template<typename T>
struct target_traits<TypedAttribute<T>> {
  using value_type = T;
  static constexpr bool has_fallback = false;
  static constexpr bool is_varying = is_animatable<T>::value;
  using underlying_type = typename is_animatable<T>::value_type;
};

// Specialization for TypedAttributeWithFallback<T>
template<typename T>
struct target_traits<TypedAttributeWithFallback<T>> {
  using value_type = T;
  static constexpr bool has_fallback = true;
  static constexpr bool is_varying = is_animatable<T>::value;
  using underlying_type = typename is_animatable<T>::value_type;
};

// ============================================================================
// Unified ParseTypedAttribute implementation
// ============================================================================

template<typename Target>
static ParseResult ParseTypedAttributeUnified(
    std::set<std::string> &table,
    const std::string prop_name,
    Property &prop,
    const std::string &name,
    Target &target)
{
  using Traits = target_traits<Target>;
  using T = typename Traits::underlying_type;
  constexpr bool is_varying = Traits::is_varying;
  constexpr bool has_fallback = Traits::has_fallback;

  ParseResult ret;

  if (prop_name.compare(name) != 0) {
    ret.code = ParseResult::ResultCode::Unmatched;
    return ret;
  }

  // Check if property is relationship (should be attribute)
  if (prop.is_relationship()) {
    ret.code = ParseResult::ResultCode::PropertyTypeMismatch;
    ret.err = fmt::format("Property `{}` must be Attribute, but declared as Relationship.", name);
    return ret;
  }

  // Use non-const to allow mutable ConvertToAnimatable (moves values instead of copying)
  Attribute &attr = prop.attribute();
  std::string attr_type_name = attr.type_name();

  // Check type match
  if ((value::TypeTraits<T>::type_name() != attr_type_name) &&
      (value::TypeTraits<T>::underlying_type_name() != attr_type_name)) {
    DCOUT("tyname = " << value::TypeTraits<T>::type_name() << ", attr.type = " << attr_type_name);
    ret.code = ParseResult::ResultCode::TypeMismatch;
    ret.err = fmt::format("Property type mismatch. {} expects type `{}` but defined as type `{}`",
                          name, value::TypeTraits<T>::type_name(), attr_type_name);
    return ret;
  }

  bool has_connections = false;
  bool has_default = false;
  bool has_timesamples = false;

  // Handle connections
  if (attr.has_connections()) {
    target.set_connections(attr.connections());
    has_connections = true;
  }

  // Handle empty attribute
  if (prop.get_property_type() == Property::Type::EmptyAttrib) {
    DCOUT("Added prop with empty value: " << name);
    target.set_value_empty();
    target.metas() = std::move(prop.attribute().metas());
    table.insert(name);
    ret.code = ParseResult::ResultCode::Success;
    return ret;
  }

  // Handle non-empty attribute
  // Note: Type::Connection means the attribute has connections, but it may also have values.
  // Connections are already extracted above, so we need to also parse any values.
  if (prop.get_property_type() == Property::Type::Attrib ||
      prop.get_property_type() == Property::Type::Connection) {
    DCOUT("Adding typed prop: " << name);

    // Check blocked attribute
    if (attr.is_blocked()) {
      target.set_blocked(true);
      has_default = true;
    } else {
      // Variability checks
      bool is_config_attr = (name.find("config:") == 0);

      if constexpr (!is_varying) {
        // Uniform attribute - no timeSamples allowed
        if (!is_config_attr && attr.variability() != Variability::Uniform) {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = fmt::format("Attribute `{}` must be `uniform` variability.", name);
          return ret;
        }

        if (is_config_attr && attr.variability() != Variability::Uniform) {
          ret.warn = fmt::format("Config attribute `{}` should have explicit `uniform` variability.", name);
        }

        if (attr.get_var().has_timesamples()) {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = "TimeSample assigned to a property where `uniform` variability is set.";
          return ret;
        }
      }

      // Extract value based on varying vs uniform
      if constexpr (is_varying) {
        // Varying: can have both timeSamples and default
        typename Traits::value_type animatable_value;

        // Check uniform variability for attributes with fallback
        if constexpr (has_fallback) {
          if (attr.variability() == Variability::Uniform) {
            if (attr.get_var().has_timesamples()) {
              ret.code = ParseResult::ResultCode::VariabilityMismatch;
              ret.err = fmt::format("TimeSample value is assigned to `uniform` property `{}`", name);
              return ret;
            }

            if (auto pv = attr.get_value<T>()) {
              target.set_value(std::move(pv.value()));
              target.metas() = std::move(prop.attribute().metas());
              table.insert(name);
              ret.code = ParseResult::ResultCode::Success;
              return ret;
            } else {
              ret.code = ParseResult::ResultCode::TypeMismatch;
              ret.err = fmt::format("Failed to retrieve value with requested type `{}`.", value::TypeTraits<T>::type_name());
              return ret;
            }
          }
        }

        // Parse timeSamples
        if (attr.get_var().has_timesamples()) {
          if (auto av = ConvertToAnimatable<T>(attr.get_var())) {
            animatable_value = std::move(av.value());
            has_timesamples = true;
          } else {
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = fmt::format("Converting timeSamples failed for `{}`. TimeSamples may have values with different type (expected `{}`).",
                                  prop_name, value::TypeTraits<T>::type_name());
            return ret;
          }
        }

        // Parse default value
        if (attr.get_var().has_default()) {
          if (auto pv = attr.get_var().get_value<T>()) {
            animatable_value.set(std::move(pv.value()));
            has_default = true;
          } else {
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = fmt::format("get_value<{}> failed. Attribute has type {}",
                                  value::TypeTraits<T>::type_name(), attr.get_var().type_name());
            return ret;
          }
        }

        if (has_timesamples || has_default) {
          target.set_value(std::move(animatable_value));
        } else if (!has_connections) {
          // No value, default, timeSamples, or connections - use default-constructed
          target.set_value(typename Traits::value_type());
        }

      } else {
        // Uniform: only default value
        if (attr.get_var().has_default()) {
          if (auto pv = attr.get_value<T>()) {
            target.set_value(std::move(pv.value()));
            has_default = true;
          } else {
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = "Internal data corrupted.";
            return ret;
          }
        } else if (!has_connections) {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = "TimeSample or corrupted value assigned to a property where `uniform` variability is set.";
          return ret;
        }
      }
    }
  } else {
    ret.code = ParseResult::ResultCode::InternalError;
    ret.err = "ParseTypedAttribute: Invalid Property type(internal error)";
    return ret;
  }

  // Connections only case
  if (has_connections && !has_timesamples && !has_default) {
    target.set_value_empty();
  }

  // Success cases
  if (has_connections || has_timesamples || has_default) {
    target.metas() = std::move(prop.attribute().metas());
    table.insert(name);
    ret.code = ParseResult::ResultCode::Success;
    return ret;
  }

  ret.code = ParseResult::ResultCode::InternalError;
  ret.err = "ParseTypedAttribute: No valid data found(internal error)";
  return ret;
}

// ============================================================================
// Overload wrappers (delegate to unified implementation)
// ============================================================================

// For animatable attribute(`varying`) with fallback
template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttributeWithFallback<Animatable<T>> &target)
{
  return ParseTypedAttributeUnified(table, prop_name, prop, name, target);
}

#if 0 // deprecated. TODO: Remove
// Old implementation kept for reference during transition
template<typename T>
static ParseResult ParseTypedAttribute_OLD1(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttributeWithFallback<Animatable<T>> &target)
{
  ParseResult ret;

#if 0 // deprecated. TODO: Remove
  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      DCOUT("Already processed: " << prop_name);
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (prop.is_connection()) {
      if (auto pv = prop.get_relationTarget()) {
        target.set_connection(pv.value());
        //target.variability = prop.attrib.variability;
        target.metas() = prop.get_attribute().metas();
        table.insert(propname);
        ret.code = ParseResult::ResultCode::Success;
        DCOUT("Added as property with connection: " << propname);
        return ret;
      } else {
        ret.code = ParseResult::ResultCode::InvalidConnection;
        ret.err = "Connection target not found.";
        return ret;
      }
    } else {
      ret.code = ParseResult::ResultCode::InternalError;
      ret.err = "Internal error. Unsupported/Unimplemented property type.";
      return ret;
    }
#endif
  if (prop_name.compare(name) == 0) {
    //if (table.count(name)) {
    //  ret.code = ParseResult::ResultCode::AlreadyProcessed;
    //  return ret;
    //}

    if (prop.is_relationship()) {
      ret.code = ParseResult::ResultCode::PropertyTypeMismatch;
      ret.err = fmt::format("Property {} must be Attribute, but declared as Relationhip.", name);
      return ret;
    }

    Attribute &attr = prop.attribute();


    std::string attr_type_name = attr.type_name();
    if ((value::TypeTraits<T>::type_name() == attr_type_name) || (value::TypeTraits<T>::underlying_type_name() == attr_type_name)) {

      bool has_connections{false};
      bool has_default{false};
      bool has_timesamples{false};

      if (attr.has_connections()) {
        target.set_connections(attr.connections());
        //target.metas() = attr.metas();
        //table.insert(prop_name);
        //ret.code = ParseResult::ResultCode::Success;
        has_connections = true;
      }

      if (prop.get_property_type() == Property::Type::EmptyAttrib) {
        DCOUT("Added prop with empty value: " << name);
        target.set_value_empty();
        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else if (prop.get_property_type() == Property::Type::Attrib ||
                 prop.get_property_type() == Property::Type::Connection) {

        DCOUT("Adding typed prop: " << name);

        if (attr.is_blocked()) {
          // e.g. "float radius = None"
          target.set_blocked(true);
        } else if (attr.variability() == Variability::Uniform) {
          DCOUT("Property is uniform: " << name);
          // e.g. "float radius = 1.2"
          if (attr.get_var().is_timesamples()) {
            ret.code = ParseResult::ResultCode::VariabilityMismatch;
            ret.err = fmt::format("TimeSample value is assigned to `uniform` property `{}", name);
            return ret;
          }

          if (auto pv = attr.get_value<T>()) {
            target.set_value(std::move(pv.value()));  // Use move to avoid copy
          } else {
            ret.code = ParseResult::ResultCode::TypeMismatch;
            ret.err = fmt::format("Fallback. Failed to retrieve value with requested type `{}`.", value::TypeTraits<T>::type_name());
            return ret;
          }

        }

        Animatable<T> animatable_value;

        if (attr.get_var().has_timesamples()) {
          // e.g. "float radius.timeSamples = {0: 1.2, 1: 2.3}"

          if (auto av = ConvertToAnimatable<T>(attr.get_var())) {
            animatable_value = std::move(av.value());  // Use move to avoid copy
            //target.set_value(anim);
          } else {
            // Conversion failed.
            DCOUT("ConvertToAnimatable failed.");
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = fmt::format("Converting timeSamples Attribute data failed for `{}`. Guess TimeSamples have values with different type(expected is `{}`)?", prop_name, value::TypeTraits<T>::type_name());
            return ret;
          }

          has_timesamples = true;
        }

        if (attr.get_var().has_value()) {
          if (auto pv = attr.get_var().get_value<T>()) {
            //target.set_value(pv.value());
            animatable_value.set(std::move(pv.value()));  // Use move to avoid copy
          } else {
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = fmt::format("Internal error. Invalid attribute value? get_value<{}> failed. Attribute has type {}", value::TypeTraits<T>::type_name(), attr.get_var().type_name());
            return ret;
          }

          has_default = true;
        }

        if (has_timesamples || has_default) {
          target.set_value(std::move(animatable_value));  // Use move to avoid copy
        }
      }

      // connections only?
      if (has_connections && (!has_timesamples && !has_default)) {
        target.set_value_empty();
      }

      if (has_connections || has_timesamples || has_default) {

        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;

      } else {
        DCOUT("Invalid Property.type");
        ret.err = "ParseTypedAttribute: Invalid Property type(internal error)";
        ret.code = ParseResult::ResultCode::InternalError;
        return ret;
      }
    } else {
      DCOUT("tyname = " << value::TypeTraits<T>::type_name() << ", attr.type = " << attr_type_name);
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << value::TypeTraits<T>::type_name()
              << "` but defined as type `" << attr_type_name << "`";
      ret.err = ss.str();
      return ret;
    }

  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}
#endif // Old implementation 1

// For 'uniform' attribute with fallback
template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttributeWithFallback<T> &target) /* out */
{
  return ParseTypedAttributeUnified(table, prop_name, prop, name, target);
}

#if 0 // Old implementation 2
template<typename T>
static ParseResult ParseTypedAttribute_OLD2(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttributeWithFallback<T> &target) /* out */
{
  ParseResult ret;

#if 0
  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      DCOUT("Already processed: " << prop_name);
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (prop.is_connection()) {
      const Attribute &attr = prop.get_attribute();
      if (attr.is_connection()) {
        target.set_connections(attr.connections());
        //target.variability = prop.attrib.variability;
        target.metas() = prop.get_attribute().metas();
        table.insert(propname);
        ret.code = ParseResult::ResultCode::Success;
        DCOUT("Added as property with connection: " << propname);
        return ret;
      } else {
        ret.code = ParseResult::ResultCode::InvalidConnection;
        ret.err = "Connection target not found.";
        return ret;
      }
    } else {
      ret.code = ParseResult::ResultCode::InternalError;
      ret.err = "Internal error. Unsupported/Unimplemented property type.";
      return ret;
    }
#endif
  if (prop_name.compare(name) == 0) {
    //if (table.count(name)) {
    //  ret.code = ParseResult::ResultCode::AlreadyProcessed;
    //  return ret;
    //}

    if (prop.is_relationship()) {
      ret.code = ParseResult::ResultCode::PropertyTypeMismatch;
      ret.err = fmt::format("Property `{}` must be Attribute, but declared as Relationship.", name);
      
    }
    
    const Attribute &attr = prop.get_attribute();

    std::string attr_type_name = attr.type_name();
    if ((value::TypeTraits<T>::type_name() == attr_type_name) || (value::TypeTraits<T>::underlying_type_name() == attr_type_name)) {

      if (attr.has_connections()) {
        target.set_connections(attr.connections());
        //target.variability = prop.attrib.variability;
        target.metas() = prop.get_attribute().metas();
        table.insert(prop_name);
        ret.code = ParseResult::ResultCode::Success;
      }

      if (prop.get_property_type() == Property::Type::EmptyAttrib) {
        DCOUT("Added prop with empty value: " << name);
        target.set_value_empty();
        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else if (prop.get_property_type() == Property::Type::Attrib ||
                 prop.get_property_type() == Property::Type::Connection) {
        DCOUT("Adding prop: " << name);

        // Config attributes (config:*) are implicitly uniform even if not explicitly marked
        bool is_config_attr = (name.find("config:") == 0);

        if (!is_config_attr && prop.get_attribute().variability() != Variability::Uniform) {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = fmt::format("Attribute `{}` must be `uniform` variability.", name);
          return ret;
        }

        // Warn if config attribute is missing explicit uniform variability
        if (is_config_attr && prop.get_attribute().variability() != Variability::Uniform) {
          ret.warn = fmt::format("Config attribute `{}` should have explicit `uniform` variability.", name);
        }

        if (attr.is_blocked()) {
          target.set_blocked(true);
        } else if (attr.get_var().has_default()) {
          if (auto pv = attr.get_value<T>()) {
            target.set_value(std::move(pv.value()));  // Use move to avoid copy
          } else {
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = "Internal data corrupsed.";
            return ret;
          }
        } else {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = "TimeSample or corrupted value assigned to a property where `uniform` variability is set.";
          return ret;
        }

        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        DCOUT("Invalid Property.type");
        ret.err = "ParseTypedAttribute(Uniform): Invalid Property type(internal error)";
        ret.code = ParseResult::ResultCode::InternalError;
        return ret;
      }
    } else {
      DCOUT("tyname = " << value::TypeTraits<T>::type_name() << ", attr.type = " << attr_type_name);
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << value::TypeTraits<T>::type_name()
              << "` but defined as type `" << attr_type_name << "`";
      ret.err = ss.str();
      return ret;
    }

  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}
#endif // Old implementation 2

// For animatable attribute(`varying`) without fallback
template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttribute<Animatable<T>> &target) /* out */
{
  return ParseTypedAttributeUnified(table, prop_name, prop, name, target);
}

#if 0 // Old implementation 3
template<typename T>
static ParseResult ParseTypedAttribute_OLD3(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttribute<Animatable<T>> &target) /* out */
{
  ParseResult ret;

#if 0
  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      DCOUT("Already processed: " << prop_name);
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (prop.is_connection()) {
      const Attribute &attr = prop.get_attribute();
      if (attr.is_connection()) {
        target.set_connections(attr.connections());
        //target.variability = prop.attrib.variability;
        target.metas() = prop.get_attribute().metas();
        table.insert(propname);
        ret.code = ParseResult::ResultCode::Success;
        DCOUT("Added as property with connection: " << propname);
        return ret;
      } else {
        ret.code = ParseResult::ResultCode::InvalidConnection;
        ret.err = "Connection target not found.";
        return ret;
      }
    } else {
      ret.code = ParseResult::ResultCode::InternalError;
      ret.err = "Internal error. Unsupported/Unimplemented property type.";
      return ret;
    }
#endif
  if (prop_name.compare(name) == 0) {
    //if (table.count(name)) {
    //  ret.code = ParseResult::ResultCode::AlreadyProcessed;
    //  return ret;
    //}
    
    if (prop.is_relationship()) {
      ret.code = ParseResult::ResultCode::PropertyTypeMismatch;
      ret.err = fmt::format("Property `{}` must be Attribute, but declared as Relationship.", name);
      
    }

    Attribute &attr = prop.attribute();

    if (attr.has_connections()) {
      target.set_connections(attr.connections());
      //target.variability = prop.attrib.variability;
      //target.metas() = prop.get_attribute().metas();
      //table.insert(prop_name);
      ret.code = ParseResult::ResultCode::Success;
    }

    std::string attr_type_name = attr.type_name();
    if ((value::TypeTraits<T>::type_name() == attr_type_name) || (value::TypeTraits<T>::underlying_type_name() == attr_type_name)) {
      if (prop.get_property_type() == Property::Type::EmptyAttrib) {
        DCOUT("Added prop with empty value: " << name);
        target.set_value_empty();
        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else if (prop.get_property_type() == Property::Type::Attrib ||
                 prop.get_property_type() == Property::Type::Connection) {

        DCOUT("Adding typed attribute: " << name);
        DCOUT("T.tyid = " << value::TypeTraits<T>::type_id() << ", var.tyid = " << attr.get_var().type_id());

        if (attr.is_blocked()) {
          DCOUT("Attribute is blocked: " << name);
          // e.g. "uniform float radius = None"
          target.set_blocked(true);
        }

        auto &var = attr.get_var();
        DCOUT("has_value = " << var.has_value());

        if (var.has_default() || var.has_timesamples()) {
          if (auto av = ConvertToAnimatable<T>(var)) {
            target.set_value(std::move(av.value()));  // Use move to avoid copy
          } else {
            DCOUT("ConvertToAnimatable failed.");
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = "Converting Attribute data failed. Maybe TimeSamples have values with different types?";
            return ret;
          }

          DCOUT("Added typed attribute: " << name);

          target.metas() = attr.metas();
          table.insert(name);
          ret.code = ParseResult::ResultCode::Success;
          return ret;
        }
      } else {
        DCOUT("Invalid Property.type");
        ret.err = "ParseTypedAttribute(Animatable) Invalid Property type(internal error)";
        ret.code = ParseResult::ResultCode::InternalError;
        return ret;
      }
    } else {
      DCOUT("tyname = " << value::TypeTraits<T>::type_name() << ", attr.type = " << attr_type_name);
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << value::TypeTraits<T>::type_name()
              << "` but defined as type `" << attr_type_name << "`";
      ret.err = ss.str();
      return ret;
    }

    if (attr.has_connections()) { // connection only
      DCOUT("Connection only attribute.");
      target.metas() = prop.get_attribute().metas();
      table.insert(prop_name);
      ret.code = ParseResult::ResultCode::Success;
      return ret;
    } else {
      // Handle attributes that have no value, default, timeSamples, or connections
      // This can happen for empty attributes or attributes that are just placeholders
      DCOUT("Attribute has no value, using default-constructed value.");

      // Set an empty/default value so the attribute is valid but empty
      target.set_value(Animatable<T>());  // Default-constructed, no need for move
      target.metas() = attr.metas();
      table.insert(prop_name);
      ret.code = ParseResult::ResultCode::Success;
      return ret;
    }
    return ret;
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}
#endif // Old implementation 3

// For uniform attribute without fallback
template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttribute<T> &target) /* out */
{
  return ParseTypedAttributeUnified(table, prop_name, prop, name, target);
}

#if 0 // Old implementation 4
template<typename T>
static ParseResult ParseTypedAttribute_OLD4(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttribute<T> &target) /* out */
{
  ParseResult ret;

  DCOUT(fmt::format("prop name {}", prop_name));

#if 0
  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      DCOUT("Already processed: " << prop_name);
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (prop.is_connection()) {
      const Attribute &attr = prop.get_attribute();
      if (attr.is_connection()) {
        target.set_connections(attr.connections());
        //target.variability = prop.attrib.variability;
        target.metas() = prop.get_attribute().metas();
        table.insert(propname);
        ret.code = ParseResult::ResultCode::Success;
        DCOUT("Added as property with connection: " << propname);
        return ret;
      } else {
        ret.code = ParseResult::ResultCode::InvalidConnection;
        ret.err = "Connection target not found.";
        return ret;
      }
    } else {
      ret.code = ParseResult::ResultCode::InternalError;
      ret.err = "Internal error. Unsupported/Unimplemented property type.";
      return ret;
    }
#endif
  if (prop_name.compare(name) == 0) {
    DCOUT(fmt::format("prop name match {}", name));
    //if (table.count(name)) {
    //  ret.code = ParseResult::ResultCode::AlreadyProcessed;
    //  return ret;
    //}

    const Attribute &attr = prop.get_attribute();
    std::string attr_type_name = attr.type_name();
    DCOUT(fmt::format("prop name {}, type = {}", prop_name, attr_type_name));
    if ((value::TypeTraits<T>::type_name() == attr_type_name) || (value::TypeTraits<T>::underlying_type_name() == attr_type_name)) {

      bool has_connections{false};
      bool has_default{false};

      if (attr.has_connections()) {
        target.set_connections(attr.connections());
        //target.variability = prop.attrib.variability;
        //target.metas() = prop.get_attribute().metas();
        //table.insert(prop_name);
        //ret.code = ParseResult::ResultCode::Success;
        has_connections = true;
      }

      if (prop.get_property_type() == Property::Type::EmptyAttrib) {
        DCOUT("Added prop with empty value: " << name);
        target.set_value_empty();
        has_default = true; // has empty 'default'
      } else if (prop.get_property_type() == Property::Type::Attrib ||
                 prop.get_property_type() == Property::Type::Connection) {

        DCOUT("Adding typed attribute: " << name);

        if (prop.get_attribute().variability() != Variability::Uniform) {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = fmt::format("Attribute `{}` must be `uniform` variability.", name);
          return ret;
        }

        if (attr.get_var().has_timesamples()) {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = "TimeSample or corrupted value assigned to a property where `uniform` variability is set.";
          return ret;
        }

        if (attr.is_blocked()) {
          target.set_blocked(true);
          has_default = true;
        } else if (attr.get_var().has_default()) {
          if (auto pv = attr.get_value<T>()) {
            target.set_value(std::move(pv.value()));  // Use move to avoid copy
            has_default = true;
          } else {
            ret.code = ParseResult::ResultCode::VariabilityMismatch;
            ret.err = "Internal data corrupsed.";
            return ret;
          }
        }

      }

      if (has_connections || has_default) {
        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        ret.code = ParseResult::ResultCode::InternalError;
        ret.err = "Internal data corrupsed.";
        return ret;
      }
      
    } else {
      DCOUT("tyname = " << value::TypeTraits<T>::type_name() << ", attr.type = " << attr_type_name);
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << value::TypeTraits<T>::type_name()
              << "` but defined as type `" << attr_type_name << "`";
      ret.err = ss.str();
      return ret;
    }

    return ret;
  }

  ret.code = ParseResult::ResultCode::Unmatched;

  return ret;
}
#endif // Old implementation 4

// Special case for Extent(float3[2]) type.
// TODO: Reuse code of ParseTypedAttribute as much as possible
static ParseResult ParseExtentAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttribute<Animatable<Extent>> &target) /* out */
{
  ParseResult ret;

#if 0
  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      DCOUT("Already processed: " << prop_name);
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (prop.is_connection()) {
      const Attribute &attr = prop.get_attribute();
      if (attr.is_connection()) {
        target.set_connections(attr.connections());
        //target.variability = prop.attrib.variability;
        target.metas() = prop.get_attribute().metas();
        table.insert(propname);
        ret.code = ParseResult::ResultCode::Success;
        DCOUT("Added as property with connection: " << propname);
        return ret;
      } else {
        ret.code = ParseResult::ResultCode::InvalidConnection;
        ret.err = "Connection target not found.";
        return ret;
      }
    } else {
      ret.code = ParseResult::ResultCode::InternalError;
      ret.err = "Internal error. Unsupported/Unimplemented property type.";
      return ret;
    }
#endif
  if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }

    Attribute &attr = prop.attribute();

    std::string attr_type_name = attr.type_name();
    if (prop.get_property_type() == Property::Type::EmptyAttrib) {
      DCOUT("Added prop with empty value: " << name);
      target.set_value_empty();
      target.metas() = attr.metas();
      table.insert(name);
      ret.code = ParseResult::ResultCode::Success;
      return ret;
    } else if (prop.get_property_type() == Property::Type::Attrib ||
               prop.get_property_type() == Property::Type::Connection) {

      //bool has_default{false};
      bool has_connections{false};

      if (attr.has_connections()) {
        target.set_connections(attr.connections());
        //target.variability = prop.attrib.variability;
        //target.metas() = prop.get_attribute().metas();
        //table.insert(prop_name);
        //ret.code = ParseResult::ResultCode::Success;
        //return ret;
        has_connections = true;
      }

      DCOUT("Adding typed extent attribute: " << name);

      if (attr.is_blocked()) {
        // e.g. "float3[] extent = None"
        target.set_blocked(true);
      }

#if 0
      } else {
        
        //
        // No variability check. allow `uniform extent`(promote to varying)
        //
        if (auto pv = attr.get_value<std::vector<value::float3>>()) {
          if (pv.value().size() != 2) {
            ret.code = ParseResult::ResultCode::TypeMismatch;
            ret.err = fmt::format("`extent` must be `float3[2]`, but got array size {}", pv.value().size());
            return ret;
          }

          Extent ext;
          ext.lower = pv.value()[0];
          ext.upper = pv.value()[1];

          //target.set_value(ext);
          animatable_value.set(ext);
        } else {
          ret.code = ParseResult::ResultCode::TypeMismatch;
          ret.err = fmt::format("`extent` must be `float3[]` type, but got `{}`", attr.type_name());
          return ret;
        }
      }

      if (attr.get_var().has_timesamples()) {
        // e.g. "float3[] extent.timeSamples = ..."

        if (auto av = ConvertToAnimatable<Extent>(attr.get_var())) {
          animatable_value.set(av.value().get_timesamples());
          //target.set_value(anim);
          
          has_timesamples = true;
        } else {
          // Conversion failed.
          DCOUT("ConvertToAnimatable failed.");
          ret.code = ParseResult::ResultCode::InternalError;
          ret.err = "Converting Attribute data failed. Maybe TimeSamples have values with different types or invalid array size?";
          return ret;
        }
      }

      if (has_default || has_timesamples) {
        DCOUT("Added Extent attribute: " << name);
        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        DCOUT("Internal error.");
        ret.code = ParseResult::ResultCode::InternalError;
        ret.err = "Internal error. Invalid Attribute data";
        return ret;
      }
#else
      
      auto &var = attr.get_var();

      if (var.has_default() || var.has_timesamples()) {
        if (auto av = ConvertToAnimatable<Extent>(var)) {
          target.set_value(std::move(av.value()));  // Use move to avoid copy
        } else {
          DCOUT("ConvertToAnimatable failed.");
          ret.code = ParseResult::ResultCode::InternalError;
          ret.err = "Converting Attribute data failed. Maybe TimeSamples have values with different types?";
          return ret;
        }

        DCOUT("Added typed extent attribute: " << name);

        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      }

      if (has_connections) {
        DCOUT("Added Extent connection attribute: " << name);
        target.metas() = std::move(prop.attribute().metas());  // Move instead of copy
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      }

#endif

    } else {
      DCOUT("Invalid Property.type");
      ret.err = "[extent] Invalid Property type(internal error)";
      ret.code = ParseResult::ResultCode::InternalError;
      return ret;
    }

  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}


// CheckAllowedTokens template removed - now in enum-handlers.cc

// Allowed syntax:
//   "T varname"
template<typename T>
static ParseResult ParseShaderOutputTerminalAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedTerminalAttribute<T> &target) /* out */
{
  ParseResult ret;

#if 0 // Old code: TODO: Remove
  if (prop_name.compare(name + ".connect") == 0) {
    ret.code = ParseResult::ResultCode::ConnectionNotAllowed;
    ret.err = "Connection is not allowed for output terminal attribute.";
    return ret;
#endif
  if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }

    if (prop.is_attribute_connection()) {
      ret.code = ParseResult::ResultCode::ConnectionNotAllowed;
      ret.err = "Connection is not allowed for output terminal attribute.";
      return ret;
    } else {

      if (prop.get_property_type() != Property::Type::EmptyAttrib) {
          DCOUT("Output Invalid shader output terminal attribute");
          ret.err = "No value should be assigned for shader output terminal attribute.";
          ret.code = ParseResult::ResultCode::InvalidConnection;
          return ret;
      }

      const Attribute &attr = prop.get_attribute();

      std::string attr_type_name = attr.type_name();

      bool attr_is_role_type = value::IsRoleType(attr_type_name);

      DCOUT("attrname = " << name);
      DCOUT("value typename = " << value::TypeTraits<T>::type_name());
      DCOUT("attr-type_name = " << attr_type_name);


      // First check if both types are same, then
      // Allow either type is role-types(e.g. allow color3f attribute for TypedTerminalAttribute<float3>)
      // TODO: Allow both role-types case?(e.g. point3f attribute for TypedTerminalAttribute<vector3f>)
      if (value::TypeTraits<T>::type_name() == attr_type_name) {
        DCOUT("Author output terminal attribute: " << name);
        target.set_authored(true);
        target.metas() = prop.get_attribute().metas();
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else if (value::TypeTraits<T>::is_role_type()) {
        if (attr_is_role_type) {
          ret.code = ParseResult::ResultCode::TypeMismatch;
          ret.err = fmt::format("Attribute type mismatch. {} expects type `{}` but defined as type `{}`.", name, value::TypeTraits<T>::type_name(), attr_type_name);
          return ret;
        } else {
          if (value::TypeTraits<T>::underlying_type_name() == attr_type_name) {
            target.set_authored(true);
            target.set_actual_type_name(attr_type_name);
            target.metas() = prop.get_attribute().metas();
            table.insert(name);
            ret.code = ParseResult::ResultCode::Success;
            return ret;
          } else {
            ret.code = ParseResult::ResultCode::TypeMismatch;
            ret.err = fmt::format("Attribute type mismatch. {} expects type `{}`(and its underlying types) but defined as type `{}`.", name, value::TypeTraits<T>::type_name(), attr_type_name);
            return ret;
          }
        }
      } else if (attr_is_role_type) {
        if (value::TypeTraits<T>::is_role_type()) {
          ret.code = ParseResult::ResultCode::TypeMismatch;
          ret.err = fmt::format("Attribute type mismatch. {} expects type `{}` but defined as type `{}`.", name, value::TypeTraits<T>::type_name(), attr_type_name);
          return ret;
        } else {
          uint32_t attr_underlying_type_id = value::GetUnderlyingTypeId(attr_type_name);
          if (value::TypeTraits<T>::type_id() == attr_underlying_type_id) {
            target.set_authored(true);
            target.set_actual_type_name(attr_type_name);
            target.metas() = prop.get_attribute().metas();
            table.insert(name);
            ret.code = ParseResult::ResultCode::Success;
            return ret;
          } else {
            ret.code = ParseResult::ResultCode::TypeMismatch;
            ret.err = fmt::format("Attribute type mismatch. {} expects type `{}` but defined as type `{}`(and its underlying types).", name, value::TypeTraits<T>::type_name(), attr_type_name);
            return ret;
          }
        }

      } else {
        DCOUT("attr.type = " << attr_type_name);
        ret.code = ParseResult::ResultCode::TypeMismatch;
        ret.err = fmt::format("Property type mismatch. {} expects type `{}` but defined as type `{}`.", name, value::TypeTraits<T>::type_name(), attr_type_name);
        return ret;
      }
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}

#if 0 // TODO: Remove since not used.
// Allowed syntax:
//   "token outputs:surface"
//   "token outputs:surface.connect = </path/to/conn/>"
static ParseResult ParseShaderOutputProperty(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  nonstd::optional<Relationship> &target) /* out */
{
  ParseResult ret;

  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (auto pv = prop.get_relationTarget()) {
      Relationship rel;
      rel.set(pv.value());
      rel.metas() = prop.get_attribute().metas();
      target = rel;
      table.insert(propname);
      ret.code = ParseResult::ResultCode::Success;
      return ret;
    }
  } else if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }

    if (prop.is_connection()) {
      const Attribute &attr = prop.get_attribute();
      if (attr.is_connection()) {
        Relationship rel;
        std::vector<Path> conns = attr.connections();

        if (conns.size() == 0) {
          ret.code = ParseResult::ResultCode::InternalError;
          ret.err = "Invalid shader output attribute with connection. connection targetPath size is zero.";
          return ret;
        }

        if (conns.size() == 1) {
          rel.set(conns[0]);
        } else if (conns.size() > 1) {
          rel.set(conns);
        }

        rel.metas() = prop.get_attribute().metas();
        target = rel;
        table.insert(prop_name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;

      } else {
        ret.code = ParseResult::ResultCode::InternalError;
        ret.err = "Invalid shader output attribute with connection.";
        return ret;
      }
    } else {

      const Attribute &attr = prop.get_attribute();

      std::string attr_type_name = attr.type_name();
      if (value::TypeTraits<value::token>::type_name() == attr_type_name) {
        if (prop.get_property_type() == Property::Type::EmptyAttrib) {
          Relationship rel;
          rel.set_novalue();
          rel.metas() = prop.get_attribute().metas();
          table.insert(name);
          target = rel;
          ret.code = ParseResult::ResultCode::Success;
          return ret;
        } else {
          DCOUT("Output Invalid Property.type");
          ret.err = "Invalid connection or value assigned for output attribute.";
          ret.code = ParseResult::ResultCode::InvalidConnection;
          return ret;
        }
      } else {
        DCOUT("attr.type = " << attr.type_name());
        ret.code = ParseResult::ResultCode::TypeMismatch;
        std::stringstream ss;
        ss  << "Property type mismatch. " << name << " expects type `token` but defined as type `" << attr.type_name() << "`";
        ret.err = ss.str();
        return ret;
      }
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}
#endif

// Allowed syntax:
//   "token outputs:surface.connect = </path/to/conn/>"
static ParseResult ParseShaderInputConnectionProperty(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedConnection<value::token> &target) /* out */
{
  ParseResult ret;
  ret.code = ParseResult::ResultCode::InternalError;

#if 0
  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (auto pv = prop.get_relationTarget()) {
      TypedConnection<value::token> conn;
      conn.set(pv.value());
      conn.metas() = prop.get_attribute().metas();
      target = conn;
      table.insert(propname);
      ret.code = ParseResult::ResultCode::Success;
      return ret;
    } else {
      ret.code = ParseResult::ResultCode::InternalError;
      ret.err = "Property does not contain connectionPath.";
      return ret;
    }
#endif
  if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }

    DCOUT("is_attribute = " << prop.is_attribute());
    DCOUT("is_attribute_connection = " << prop.is_attribute_connection());

    // allow empty value
    if (prop.is_empty()) {
      target.set_empty();
      target.metas() = prop.get_attribute().metas();
      table.insert(prop_name);
      ret.code = ParseResult::ResultCode::Success;
      return ret;
    } else if (prop.is_attribute_connection()) {
      const Attribute &attr = prop.get_attribute();
      if (attr.is_connection()) {
        target.set(attr.connections());
        target.metas() = prop.get_attribute().metas();

        table.insert(prop_name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        ret.code = ParseResult::ResultCode::InternalError;
        ret.err = "Property is invalid Attribute connection.";
        return ret;
      }
    } else {
      ret.code = ParseResult::ResultCode::InternalError;
      ret.err = fmt::format("Property `{}` must be Attribute connection.", prop_name);
      return ret;
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}

// Rel with single targetPath(or empty)
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
      /* define-only. accept  */ \
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

// Rel with targetPaths(single path or array of Paths)
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
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing shader output property `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

#if 0 // TODO: Remove since not used.
#define PARSE_SHADER_OUTPUT_PROPERTY(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseShaderOutputProperty(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    DCOUT("Added shader output property: " << __name); \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing shader output property `{}` failed. Error: {}", __name, ret.err)); \
  } \
}
#endif

#define PARSE_SHADER_INPUT_CONNECTION_PROPERTY(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseShaderInputConnectionProperty(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    DCOUT("Added shader input connection: " << __name); \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing shader property `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

// EnumHandler and CheckAllowedTokens templates removed.
// Enum handling is now done via centralized handlers in enum-handlers.cc

} // namespace

#define PARSE_TYPED_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    if (!ret.warn.empty()) { \
      PUSH_WARN(ret.warn); \
    } \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing attribute `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

#define PARSE_TYPED_ATTRIBUTE_NOCONTINUE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    if (!ret.warn.empty()) { \
      PUSH_WARN(ret.warn); \
    } \
    /* do nothing */ \
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing attribute `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

#define PARSE_EXTENT_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseExtentAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing attribute `extent` failed. Error: {}", ret.err)); \
  } \
}

// Use centralized enum handlers from enum-handlers.hh
// These wrapper functions maintain backwards compatibility with existing macro usage
static nonstd::expected<Axis, std::string> AxisEnumHandler(const std::string &tok) {
  return enum_handler::Axis(tok);
}

static nonstd::expected<Visibility, std::string> VisibilityEnumHandler(const std::string &tok) {
  return enum_handler::Visibility(tok);
}

static nonstd::expected<Purpose, std::string> PurposeEnumHandler(const std::string &tok) {
  return enum_handler::Purpose(tok);
}

static nonstd::expected<Orientation, std::string> OrientationEnumHandler(const std::string &tok) {
  return enum_handler::Orientation(tok);
}

#if 1

template<typename T, typename EnumTy>
bool ParseUniformEnumProperty(
  const std::string &prop_name,
  bool strict_allowedToken_check,
  EnumHandlerFun<EnumTy> enum_handler,
  const Attribute &attr,
  TypedAttributeWithFallback<T> *result,
  std::string *warn = nullptr,
  std::string *err = nullptr)
{

  if (!result) {
    PUSH_ERROR_AND_RETURN("[Internal error] `result` arg is nullptr.");
  }

  if (attr.is_connection()) {
    PUSH_ERROR_AND_RETURN_F("Attribute connection is not supported in TinyUSDZ for built-in 'enum' token attribute: {}", prop_name);
  }


  if (attr.variability() == Variability::Uniform) {
    // scalar

    if (attr.is_blocked()) {
      result->set_blocked(true);
      return true;
    }

    if (attr.get_var().is_timesamples()) {
      PUSH_ERROR_AND_RETURN_F("Attribute `{}` is defined as `uniform` variability but TimeSample value is assigned.", prop_name);
    }

    if (auto tok = attr.get_value<value::token>()) {
      auto e = enum_handler(tok.value().str());
      if (e) {
        (*result) = e.value();
        return true;
      } else if (strict_allowedToken_check) {
        PUSH_ERROR_AND_RETURN_F("Attribute `{}`: `{}` is not an allowed token.", prop_name, tok.value().str());
      } else {
        PUSH_WARN_F("Attribute `{}`: `{}` is not an allowed token. Ignore it.", prop_name, tok.value().str());
        result->set_value_empty();
        return true;
      }
    } else {
      PUSH_ERROR_AND_RETURN_F("Internal error. Maybe type mismatch? Attribute `{}` must be type `token`, but got type `{}`", prop_name, attr.type_name());
    }


  } else {
    // uniform or TimeSamples
    if (attr.get_var().is_scalar()) {

      if (attr.is_blocked()) {
        result->set_blocked(true);
        return true;
      }

      if (auto tok = attr.get_value<value::token>()) {
        auto e = enum_handler(tok.value().str());
        if (e) {
          (*result) = e.value();
          return true;
        } else if (strict_allowedToken_check) {
          PUSH_ERROR_AND_RETURN_F("Attribute `{}`: `{}` is not an allowed token.", prop_name, tok.value().str());
        } else {
          PUSH_WARN_F("Attribute `{}`: `{}` is not an allowed token. Ignore it.", prop_name, tok.value().str());
          result->set_value_empty();
          return true;
        }
      } else {
        PUSH_ERROR_AND_RETURN_F("Internal error. Maybe type mismatch? Attribute `{}` must be type `token`, but got type `{}`", prop_name, attr.type_name());
      }
    } else if (attr.get_var().is_timesamples()) {
      PUSH_ERROR_AND_RETURN_F("Attribute `{}` is uniform variability, but TimeSampled value is authored.",
 prop_name);

    } else {
      PUSH_ERROR_AND_RETURN_F("Internal error. Attribute `{}` is invalid", prop_name);
    }

  }

  return false;
}

// Animatable enum tokens
template<typename T, typename EnumTy>
bool ParseTimeSampledEnumProperty(
  const std::string &prop_name,
  bool strict_allowedToken_check,
  EnumHandlerFun<EnumTy> enum_handler,
  const Attribute &attr,
  TypedAttributeWithFallback<Animatable<T>> *result,
  std::string *warn = nullptr,
  std::string *err = nullptr)
{

  if (!result) {
    PUSH_ERROR_AND_RETURN("[Internal error] `result` arg is nullptr.");
  }

  if (attr.is_connection()) {
    PUSH_ERROR_AND_RETURN_F("Attribute connection is not supported in TinyUSDZ for built-in 'enum' token attribute: {}", prop_name);
  }


  if (attr.variability() == Variability::Uniform) {
    // scalar

    if (attr.is_blocked()) {
      result->set_blocked(true);
      return true;
    }

    if (attr.get_var().is_timesamples()) {
      PUSH_ERROR_AND_RETURN_F("Attribute `{}` is defined as `uniform` variability but TimeSample value is assigned.", prop_name);
    }

    if (auto tok = attr.get_value<value::token>()) {
      auto e = enum_handler(tok.value().str());
      if (e) {
        (*result) = e.value();
        return true;
      } else if (strict_allowedToken_check) {
        PUSH_ERROR_AND_RETURN_F("Attribute `{}`: `{}` is not an allowed token.", prop_name, tok.value().str());
      } else {
        PUSH_WARN_F("Attribute `{}`: `{}` is not an allowed token. Ignore it.", prop_name, tok.value().str());
        result->set_value_empty();
        return true;
      }
    } else {
      PUSH_ERROR_AND_RETURN_F("Internal error. Maybe type mismatch? Attribute `{}` must be type `token`, but got type `{}`", prop_name, attr.type_name());
    }


  } else {
    // default and/or TimeSamples
    bool has_default{false};
    bool has_timesamples{false};

    Animatable<T> animatable_value;

    if (attr.is_blocked()) {
      result->set_blocked(true);
      has_default = true;
      //return true;
    }

    if (attr.get_var().has_default()) {
      DCOUT("has default.");

      if (auto tok = attr.get_value<value::token>()) {
        auto e = enum_handler(tok.value().str());
        if (e) {
          animatable_value.set_default(e.value());
          has_default = true;
          //return true;

        } else if (strict_allowedToken_check) {
          PUSH_ERROR_AND_RETURN_F("Attribute `{}`: `{}` is not an allowed token.", prop_name, tok.value().str());
        } else {
          PUSH_WARN_F("Attribute `{}`: `{}` is not an allowed token. Ignore it.", prop_name, tok.value().str());
          //result->set_value_empty();
          //return true;
        }
      } else {
        PUSH_ERROR_AND_RETURN_F("Internal error. Maybe type mismatch? Attribute `{}` must be type `token`, but got type `{}`", prop_name, attr.type_name());
      }
    }

    if (attr.get_var().has_timesamples()) {
      DCOUT("has timesamples.");
      size_t n = attr.get_var().num_timesamples();

      for (size_t i = 0; i < n; i++) {

        double sample_time{value::TimeCode::Default()};

        if (auto pv = attr.get_var().get_ts_time(i)) {
          sample_time = pv.value();
        } else {
          // This should not happen.
          PUSH_ERROR_AND_RETURN_F("Internal error. Failed to get timecode for `{}`", prop_name);
        }

        if (auto pv = attr.get_var().is_ts_value_blocked(i)) {
          if (pv.value() == true) {
            animatable_value.add_blocked_sample(sample_time);
            continue;
          }
        } else {
          // This should not happen.
          PUSH_ERROR_AND_RETURN_F("Internal error. Failed to get valueblock info for `{}`", prop_name);
        }

        if (auto tok = attr.get_var().get_ts_value<value::token>(i)) {
          auto e = enum_handler(tok.value().str());
          if (e) {
            animatable_value.add_sample(sample_time, e.value());
          } else if (strict_allowedToken_check) {
            PUSH_ERROR_AND_RETURN_F("Attribute `{}`: `{}` is not an allowed token.", prop_name, tok.value().str());
          } else {
            PUSH_WARN_F("Attribute `{}`: `{}` at {}'th timesample is not an allowed token. Ignore it.", prop_name, i, tok.value().str());
            continue;
          }
        } else {
          PUSH_ERROR_AND_RETURN_F("Internal error. Maybe type mismatch? Attribute `{}`'s {}'th timesample must be type `token`, but got type `{}`", prop_name, i, attr.type_name());
        }
      }

      has_timesamples = true;
      //return true;

    }

    if (has_default || has_timesamples) {
      result->set_value(animatable_value);
    }

    return true;

  }

  return false;
}
#endif


#if 0
// TODO: TimeSamples
#define PARSE_ENUM_PROPETY(__table, __prop, __name, __enum_handler, __klass, \
                           __target, __strict_check) {                          \
  if (__prop.first == __name) {                                              \
    if (__table.count(__name)) { continue; } \
    if ((__prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) && __prop.second.is_attribute() && __prop.second.is_empty()) { \
      PUSH_WARN("No value assigned to `" << __name << "` token attribute. Set default token value."); \
      /* TODO: attr meta __target.meta = attr.meta;  */                    \
      __table.insert(__name);                                              \
    } else { \
      const Attribute &attr = __prop.second.get_attribute();                           \
      if (auto tok = attr.get_value<value::token>()) {                     \
        auto e = __enum_handler(tok.value().str());                            \
        if (e) {                                                               \
          __target = e.value();                                                \
          /* TODO: attr meta __target.meta = attr.meta;  */                    \
          __table.insert(__name);                                              \
        } else if (__strict_check) {                                            \
          PUSH_ERROR_AND_RETURN("(" << value::TypeTraits<__klass>::type_name()  \
                                    << ") " << e.error());                     \
        } else { \
          PUSH_WARN("`" << tok.value().str() << "` is not allowed token for `" << __name << "`. Set to default token value."); \
          /* TODO: attr meta __target.meta = attr.meta;  */                    \
          __table.insert(__name);                                              \
        } \
      } else {                                                                 \
        PUSH_ERROR_AND_RETURN("(" << value::TypeTraits<__klass>::type_name()    \
                                  << ") Property type mismatch. " << __name    \
                                  << " must be type `token`, but got `"        \
                                  << attr.type_name() << "`.");            \
      }                                                                        \
    } \
  } \
}
#else
// Unified enum property parsing macro
// __parser_fn should be ParseUniformEnumProperty or ParseTimeSampledEnumProperty
#define PARSE_ENUM_PROPERTY_IMPL(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                                 __target, __strict_check, __parser_fn) {        \
  if (__prop.first == __name) {                                                  \
    if (__table.count(__name)) { continue; }                                     \
    const Attribute &attr = __prop.second.get_attribute();                       \
    if ((__prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) && \
        __prop.second.is_attribute() && __prop.second.is_empty()) {              \
      PUSH_WARN("No value assigned to `" << __name << "` token attribute. Set default token value."); \
      __target.metas() = std::move(__prop.second.attribute().metas());           \
      __table.insert(__name);                                                    \
      continue;                                                                  \
    }                                                                            \
    std::function<nonstd::expected<__enum_ty, std::string>(const std::string &)> fun = __enum_handler; \
    if (!__parser_fn(__name, __strict_check, fun, attr, &__target, warn, err)) { \
      return false;                                                              \
    }                                                                            \
    __target.metas() = std::move(__prop.second.attribute().metas());             \
    __table.insert(__name);                                                      \
    continue;                                                                    \
  }                                                                              \
}

// Convenience wrappers for backward compatibility
#define PARSE_UNIFORM_ENUM_PROPERTY(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                                    __target, __strict_check) \
  PARSE_ENUM_PROPERTY_IMPL(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                           __target, __strict_check, ParseUniformEnumProperty)

#define PARSE_TIMESAMPLED_ENUM_PROPERTY(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                                        __target, __strict_check) \
  PARSE_ENUM_PROPERTY_IMPL(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                           __target, __strict_check, ParseTimeSampledEnumProperty)

// NOCONTINUE version for use within if-else branches where we need to do additional work after parsing
#define PARSE_UNIFORM_ENUM_PROPERTY_NOCONTINUE(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                                               __target, __strict_check) {                              \
  if (__prop.first == __name) {                                                                         \
    if (!__table.count(__name)) {                                                                       \
      const Attribute &attr = __prop.second.get_attribute();                                            \
      if ((__prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) &&          \
          __prop.second.is_attribute() && __prop.second.is_empty()) {                                   \
        PUSH_WARN("No value assigned to `" << __name << "` token attribute. Set default token value."); \
        __target.metas() = std::move(__prop.second.attribute().metas());                                \
        __table.insert(__name);                                                                         \
      } else {                                                                                          \
        std::function<nonstd::expected<__enum_ty, std::string>(const std::string &)> fun = __enum_handler; \
        if (!ParseUniformEnumProperty(__name, __strict_check, fun, attr, &__target, warn, err)) {       \
          return false;                                                                                 \
        }                                                                                               \
        __target.metas() = std::move(__prop.second.attribute().metas());                                \
        __table.insert(__name);                                                                         \
      }                                                                                                 \
    }                                                                                                   \
  }                                                                                                     \
}
#endif


// Add custom property(including property with "primvars" prefix)
// Please call this macro after listing up all predefined property with
// `PARSE_PROPERTY` and `PARSE_***_ENUM_PROPERTY`
#define ADD_PROPERTY(__table, __prop, __klass, __dst) {        \
  /* Check if the property name is a predefined property */  \
  if (!__table.count(__prop.first)) {                        \
    DCOUT("custom property added: name = " << __prop.first); \
    __dst[__prop.first] = std::move(__prop.second);          \
    __table.insert(__prop.first);                            \
  } \
 }

// This code path should not be reached though.
#define PARSE_PROPERTY_END_MAKE_ERROR(__table, __prop) {                     \
  if (!__table.count(__prop.first)) {                              \
    PUSH_ERROR_AND_RETURN("Unsupported/unimplemented property: " + \
                          __prop.first);                           \
  } \
 }

// This code path should not be reached though.
#define PARSE_PROPERTY_END_MAKE_WARN(__table, __prop) { \
  if (!__table.count(__prop.first)) { \
    PUSH_WARN("Unsupported/unimplemented property: " + __prop.first); \
   } \
 }

static bool ReconstructXformOpFromToken(

  const std::string &token, int i,
  std::map<std::string, Property> &properties,
  std::set<std::string> &table, /* inout */
  std::vector<XformOp> *xformOps, std::string *err) {
  if (!xformOps) {
    PUSH_ERROR_AND_RETURN("Internal error: xformOps ptr is null");
  }

  constexpr auto kTranslate = "xformOp:translate";
  constexpr auto kTransform = "xformOp:transform";
  constexpr auto kScale = "xformOp:scale";
  constexpr auto kRotateX = "xformOp:rotateX";
  constexpr auto kRotateY = "xformOp:rotateY";
  constexpr auto kRotateZ = "xformOp:rotateZ";
  constexpr auto kRotateXYZ = "xformOp:rotateXYZ";
  constexpr auto kRotateXZY = "xformOp:rotateXZY";
  constexpr auto kRotateYXZ = "xformOp:rotateYXZ";
  constexpr auto kRotateYZX = "xformOp:rotateYZX";
  constexpr auto kRotateZXY = "xformOp:rotateZXY";
  constexpr auto kRotateZYX = "xformOp:rotateZYX";
  constexpr auto kOrient = "xformOp:orient";

  // false : no prefix found.
  // true : return suffix(first namespace ':' is ommited.).
  // - "" for prefix only "xformOp:translate"
  // - "blender:pivot" for "xformOp:translate:blender:pivot"
  auto SplitXformOpToken =
      [](const std::string &s,
         const std::string &prefix) -> nonstd::optional<std::string> {
    if (startsWith(s, prefix)) {
      if (s.compare(prefix) == 0) {
        // prefix only.
        return std::string();  // empty suffix
      } else {
        std::string suffix = removePrefix(s, prefix);
        DCOUT("suffix = " << suffix);
        if (suffix.length() == 1) {  // maybe namespace only.
          return nonstd::nullopt;
        }

        // remove namespace ':'
        if (suffix[0] == ':') {
          // ok
          suffix.erase(0, 1);
        } else {
          return nonstd::nullopt;
        }

        return std::move(suffix);
      }
    }

    return nonstd::nullopt;
  };

  std::string tok = token;
        XformOp op;

        DCOUT("xformOp token = " << tok);

        if (startsWith(tok, "!resetXformStack!")) {
          if (tok.compare("!resetXformStack!") != 0) {
            PUSH_ERROR_AND_RETURN(
                "`!resetXformStack!` must be defined solely(not to be a prefix "
                "to \"xformOp:*\")");
          }

          if (i != 0) {
            PUSH_ERROR_AND_RETURN(
                "`!resetXformStack!` must appear at the first element of "
                "xformOpOrder list.");
          }

          op.op_type = XformOp::OpType::ResetXformStack;
          xformOps->emplace_back(std::move(op));

          // skip looking up property
          return true;
        }

        if (startsWith(tok, "!invert!")) {
          DCOUT("invert!");
          op.inverted = true;
          tok = removePrefix(tok, "!invert!");
          DCOUT("tok = " << tok);
        }

        auto it = properties.find(tok);
        if (it == properties.end()) {
          PUSH_ERROR_AND_RETURN("Property `" + tok + "` not found.");
        }
        if (it->second.is_attribute_connection()) {
          PUSH_ERROR_AND_RETURN(
              "Connection(.connect) for xformOp attribute is not yet supported: "
              "`" +
              tok + "`");
        }
        const Attribute &attr = it->second.get_attribute();

        // Check `xformOp` namespace
        if (auto xfm = SplitXformOpToken(tok, kTransform)) {
          op.op_type = XformOp::OpType::Transform;
          op.suffix = xfm.value();  // may contain nested namespaces

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::matrix4d>::type_id()) {
                value::matrix4d dummy{value::matrix4d::identity()};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:transform` must be type `matrix4d`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::matrix4d>()) {
              op.set_value(pvd.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:transform` must be type `matrix4d`, but got type `" +
                  attr.type_name() + "`.");
            }
          }

        } else if (auto tx = SplitXformOpToken(tok, kTranslate)) {
          op.op_type = XformOp::OpType::Translate;
          op.suffix = tx.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:translate` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:translate` must be type `double3` or `float3`, but "
                  "got type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto scale = SplitXformOpToken(tok, kScale)) {
          op.op_type = XformOp::OpType::Scale;
          op.suffix = scale.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:scale` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:scale` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotX = SplitXformOpToken(tok, kRotateX)) {
          op.op_type = XformOp::OpType::RotateX;
          op.suffix = rotX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateX` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateX` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotY = SplitXformOpToken(tok, kRotateY)) {
          op.op_type = XformOp::OpType::RotateY;
          op.suffix = rotY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateY` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateY` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotZ = SplitXformOpToken(tok, kRotateZ)) {
          op.op_type = XformOp::OpType::RotateZ;
          op.suffix = rotZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZ` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZ` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateXYZ = SplitXformOpToken(tok, kRotateXYZ)) {
          op.op_type = XformOp::OpType::RotateXYZ;
          op.suffix = rotateXYZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateXZY = SplitXformOpToken(tok, kRotateXZY)) {
          op.op_type = XformOp::OpType::RotateXZY;
          op.suffix = rotateXZY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateYXZ = SplitXformOpToken(tok, kRotateYXZ)) {
          op.op_type = XformOp::OpType::RotateYXZ;
          op.suffix = rotateYXZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateYZX = SplitXformOpToken(tok, kRotateYZX)) {
          op.op_type = XformOp::OpType::RotateYZX;
          op.suffix = rotateYZX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateZXY = SplitXformOpToken(tok, kRotateZXY)) {
          op.op_type = XformOp::OpType::RotateZXY;
          op.suffix = rotateZXY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateZYX = SplitXformOpToken(tok, kRotateZYX)) {
          op.op_type = XformOp::OpType::RotateZYX;
          op.suffix = rotateZYX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto orient = SplitXformOpToken(tok, kOrient)) {
          op.op_type = XformOp::OpType::Orient;
          op.suffix = orient.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::quatf>::type_id()) {
                value::quatf q;
                q.real = 1.0f;
                q.imag = {0.0f, 0.0f, 0.0f};
                op.set_value(q);
              } else if (attr.type_id() == value::TypeTraits<value::quatd>::type_id()) {
                value::quatd q;
                q.real = 1.0;
                q.imag = {0.0, 0.0, 0.0};
                op.set_value(q);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::quatf>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::quatd>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "token for xformOpOrder must have namespace `xformOp:***`, or .");
        }

        xformOps->emplace_back(std::move(op));
        table.insert(tok);

    return true;
  }


bool ReconstructXformOpsFromProperties(
  const Specifier &spec,
  std::set<std::string> &table, /* inout */
  std::map<std::string, Property> &properties,
  std::vector<XformOp> *xformOps,
  std::string *err)
{

  if (spec == Specifier::Class) {
    // Do not materialize xformOps here.
    return true;
  }

#if 0

  constexpr auto kTranslate = "xformOp:translate";
  constexpr auto kTransform = "xformOp:transform";
  constexpr auto kScale = "xformOp:scale";
  constexpr auto kRotateX = "xformOp:rotateX";
  constexpr auto kRotateY = "xformOp:rotateY";
  constexpr auto kRotateZ = "xformOp:rotateZ";
  constexpr auto kRotateXYZ = "xformOp:rotateXYZ";
  constexpr auto kRotateXZY = "xformOp:rotateXZY";
  constexpr auto kRotateYXZ = "xformOp:rotateYXZ";
  constexpr auto kRotateYZX = "xformOp:rotateYZX";
  constexpr auto kRotateZXY = "xformOp:rotateZXY";
  constexpr auto kRotateZYX = "xformOp:rotateZYX";
  constexpr auto kOrient = "xformOp:orient";

  // false : no prefix found.
  // true : return suffix(first namespace ':' is ommited.).
  // - "" for prefix only "xformOp:translate"
  // - "blender:pivot" for "xformOp:translate:blender:pivot"
  auto SplitXformOpToken =
      [](const std::string &s,
         const std::string &prefix) -> nonstd::optional<std::string> {
    if (startsWith(s, prefix)) {
      if (s.compare(prefix) == 0) {
        // prefix only.
        return std::string();  // empty suffix
      } else {
        std::string suffix = removePrefix(s, prefix);
        DCOUT("suffix = " << suffix);
        if (suffix.length() == 1) {  // maybe namespace only.
          return nonstd::nullopt;
        }

        // remove namespace ':'
        if (suffix[0] == ':') {
          // ok
          suffix.erase(0, 1);
        } else {
          return nonstd::nullopt;
        }

        return std::move(suffix);
      }
    }

    return nonstd::nullopt;
  };
#endif

  // Lookup xform values from `xformOpOrder`
  // TODO: TimeSamples, Connection
  if (properties.count("xformOpOrder")) {
    // array of string
    auto prop = properties.at("xformOpOrder");

    // 'uniform' check
    if (prop.get_attribute().variability() != Variability::Uniform) {
      PUSH_ERROR_AND_RETURN("`xformOpOrder` must have `uniform` variability.");
    }

    //const Attribute &attr = prop.get_attribute();
    //const auto &v = attr.get_var();
    //TUSDZ_LOG_I("attr.value.type " << v.type_name());

    if (prop.is_relationship()) {
      PUSH_ERROR_AND_RETURN("Relationship for `xformOpOrder` is not supported.");
    } else if (auto tpv =
                   prop.get_attribute().get_value<TypedArray<value::token>>()) {


      for (size_t i = 0; i < tpv.value().size(); i++) {
        const auto &item = tpv.value()[i];

        if (!ReconstructXformOpFromToken(item.str(), int(i), properties, table, xformOps, err)) {
          return false;
        }
      }
    } else if (auto pv =
                   prop.get_attribute().get_value<std::vector<value::token>>()) {

      // 'uniform' check
      if (prop.get_attribute().variability() != Variability::Uniform) {
        PUSH_ERROR_AND_RETURN("`xformOpOrder` must have `uniform` variability.");
      }

      for (size_t i = 0; i < pv.value().size(); i++) {
        const auto &item = pv.value()[i];

        if (!ReconstructXformOpFromToken(item.str(), int(i), properties, table, xformOps, err)) {
          return false;
        }
#if 0

        XformOp op;

        std::string tok = item.str();
        DCOUT("xformOp token = " << tok);

        if (startsWith(tok, "!resetXformStack!")) {
          if (tok.compare("!resetXformStack!") != 0) {
            PUSH_ERROR_AND_RETURN(
                "`!resetXformStack!` must be defined solely(not to be a prefix "
                "to \"xformOp:*\")");
          }

          if (i != 0) {
            PUSH_ERROR_AND_RETURN(
                "`!resetXformStack!` must appear at the first element of "
                "xformOpOrder list.");
          }

          op.op_type = XformOp::OpType::ResetXformStack;
          xformOps->emplace_back(op);

          // skip looking up property
          continue;
        }

        if (startsWith(tok, "!invert!")) {
          DCOUT("invert!");
          op.inverted = true;
          tok = removePrefix(tok, "!invert!");
          DCOUT("tok = " << tok);
        }

        auto it = properties.find(tok);
        if (it == properties.end()) {
          PUSH_ERROR_AND_RETURN("Property `" + tok + "` not found.");
        }
        if (it->second.is_attribute_connection()) {
          PUSH_ERROR_AND_RETURN(
              "Connection(.connect) for xformOp attribute is not yet supported: "
              "`" +
              tok + "`");
        }
        const Attribute &attr = it->second.get_attribute();

        // Check `xformOp` namespace
        if (auto xfm = SplitXformOpToken(tok, kTransform)) {
          op.op_type = XformOp::OpType::Transform;
          op.suffix = xfm.value();  // may contain nested namespaces

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::matrix4d>::type_id()) {
                value::matrix4d dummy{value::matrix4d::identity()};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:transform` must be type `matrix4d`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::matrix4d>()) {
              op.set_value(pvd.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:transform` must be type `matrix4d`, but got type `" +
                  attr.type_name() + "`.");
            }
          }

        } else if (auto tx = SplitXformOpToken(tok, kTranslate)) {
          op.op_type = XformOp::OpType::Translate;
          op.suffix = tx.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:translate` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:translate` must be type `double3` or `float3`, but "
                  "got type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto scale = SplitXformOpToken(tok, kScale)) {
          op.op_type = XformOp::OpType::Scale;
          op.suffix = scale.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:scale` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:scale` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotX = SplitXformOpToken(tok, kRotateX)) {
          op.op_type = XformOp::OpType::RotateX;
          op.suffix = rotX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateX` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateX` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotY = SplitXformOpToken(tok, kRotateY)) {
          op.op_type = XformOp::OpType::RotateY;
          op.suffix = rotY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateY` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateY` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotZ = SplitXformOpToken(tok, kRotateZ)) {
          op.op_type = XformOp::OpType::RotateZ;
          op.suffix = rotZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZ` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZ` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateXYZ = SplitXformOpToken(tok, kRotateXYZ)) {
          op.op_type = XformOp::OpType::RotateXYZ;
          op.suffix = rotateXYZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateXZY = SplitXformOpToken(tok, kRotateXZY)) {
          op.op_type = XformOp::OpType::RotateXZY;
          op.suffix = rotateXZY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateYXZ = SplitXformOpToken(tok, kRotateYXZ)) {
          op.op_type = XformOp::OpType::RotateYXZ;
          op.suffix = rotateYXZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateYZX = SplitXformOpToken(tok, kRotateYZX)) {
          op.op_type = XformOp::OpType::RotateYZX;
          op.suffix = rotateYZX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateZXY = SplitXformOpToken(tok, kRotateZXY)) {
          op.op_type = XformOp::OpType::RotateZXY;
          op.suffix = rotateZXY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateZYX = SplitXformOpToken(tok, kRotateZYX)) {
          op.op_type = XformOp::OpType::RotateZYX;
          op.suffix = rotateZYX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto orient = SplitXformOpToken(tok, kOrient)) {
          op.op_type = XformOp::OpType::Orient;
          op.suffix = orient.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::quatf>::type_id()) {
                value::quatf q;
                q.real = 1.0f;
                q.imag = {0.0f, 0.0f, 0.0f};
                op.set_value(q);
              } else if (attr.type_id() == value::TypeTraits<value::quatd>::type_id()) {
                value::quatd q;
                q.real = 1.0;
                q.imag = {0.0, 0.0, 0.0};
                op.set_value(q);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::quatf>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::quatd>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "token for xformOpOrder must have namespace `xformOp:***`, or .");
        }

        xformOps->emplace_back(op);
        table.insert(tok);
#endif
      }

    } else {
      PUSH_ERROR_AND_RETURN(
          "`xformOpOrder` must be type `token[]` but got type `"
          << prop.get_attribute().type_name() << "`.");
    }
  }

  table.insert("xformOpOrder");
  return true;
}

namespace {

bool ReconstructMaterialBindingProperties(
  std::set<std::string> &table, /* inout */
  std::map<std::string, Property> &properties,
  MaterialBinding *mb, /* inout */
  std::string *err)
{

  if (!mb) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kMaterialBinding, mb->materialBinding)
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kMaterialBindingPreview, mb->materialBindingPreview)
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kMaterialBindingPreview, mb->materialBindingFull)
    // material:binding:collection
    if (prop.first == kMaterialBindingCollection) {

      if (table.count(prop.first)) {
         continue;
      }

      if (!prop.second.is_relationship()) {
        PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
      }

      const Relationship &rel = prop.second.get_relationship();

      mb->set_materialBindingCollection(value::token(""), value::token(""), rel);

      table.insert(prop.first);
      continue;
    }
    // material:binding:collection[:PURPOSE]:NAME
    if (startsWith(prop.first, kMaterialBindingCollection + std::string(":"))) {

      if (table.count(prop.first)) {
         continue;
      }

      if (!prop.second.is_relationship()) {
        PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
      }

      std::string collection_name = removePrefix(prop.first, kMaterialBindingCollection + std::string(":"));
      if (collection_name.empty()) {
        PUSH_ERROR_AND_RETURN("empty NAME is not allowed for 'mateirial:binding:collection'");
      }
      std::vector<std::string> names = split(collection_name, ":");
      if (names.size() > 2) {
        PUSH_ERROR_AND_RETURN("3 or more namespaces is not allowed for 'mateirial:binding:collection'");
      }
      value::token mat_purpose; // empty = all-purpose
      if (names.size() == 1) {
        collection_name = names[0];
      } else {
        mat_purpose = value::token(names[0]);
        collection_name = names[1];
      }

      const Relationship &rel = prop.second.get_relationship();

      mb->set_materialBindingCollection(value::token(collection_name), mat_purpose, rel);

      table.insert(prop.first);
      continue;
    }
    // material:binding:PURPOSE
    if (startsWith(prop.first, kMaterialBinding + std::string(":"))) {

      if (table.count(prop.first)) {
         continue;
      }

      if (!prop.second.is_relationship()) {
        PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
      }

      std::string purpose_name = removePrefix(prop.first, kMaterialBinding + std::string(":"));
      if (purpose_name.empty()) {
        PUSH_ERROR_AND_RETURN("empty PURPOSE is not allowed for 'mateirial:binding:'");
      }
      std::vector<std::string> names = split(purpose_name, ":");
      if (names.size() > 1) {
        PUSH_ERROR_AND_RETURN(fmt::format("PURPOSE `{}` must not have nested namespaces for 'mateirial:binding'", purpose_name));
      }
      value::token mat_purpose = value::token(names[0]);

      const Relationship &rel = prop.second.get_relationship();

      mb->set_materialBinding(rel, mat_purpose);

      table.insert(prop.first);
      continue;
    }
  }

  return true;
}

bool ReconstructCollectionProperties(
  std::set<std::string> &table, /* inout */
  std::map<std::string, Property> &properties,
  Collection *coll, /* inout */
  std::string *warn,
  std::string *err,
  bool strict_allowedToken_check)
{
  constexpr auto kCollectionPrefix = "collection:";

  // Use centralized enum handler
  std::function<nonstd::expected<CollectionInstance::ExpansionRule, std::string>(const std::string &)> ExpansionRuleEnumHandler = enum_handler::ExpansionRule;

  if (!coll) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    if (startsWith(prop.first, kCollectionPrefix)) {
      if (table.count(prop.first)) {
         continue;
      }

      std::string suffix = removePrefix(prop.first, kCollectionPrefix);
      std::vector<std::string> names = split(suffix, ":");
      if (names.size() != 2) {
        PUSH_ERROR_AND_RETURN(fmt::format("Invalid collection property name. Must be 'collection:INSTANCE_NAME:<prop_name>' but got '{}'",  prop.first));
      }
      if (names[0].empty()) {
        PUSH_ERROR_AND_RETURN("INSTANCE_NAME is empty for collection property name");
      }
      if (names[1].empty()) {
        PUSH_ERROR_AND_RETURN("Collection property name is empty");
      }

      std::string instance_name = names[0];

      if (names[1] == "includes") {

        if (!prop.second.is_relationship()) {
          PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
        }

        CollectionInstance &coll_instance = coll->get_or_add_instance(instance_name);
        coll_instance.includes = prop.second.get_relationship();
        table.insert(prop.first);

      } else if (names[1] == "expansionRule") {

        TypedAttributeWithFallback<CollectionInstance::ExpansionRule> r{CollectionInstance::ExpansionRule::ExpandPrims};

        PARSE_UNIFORM_ENUM_PROPERTY_NOCONTINUE(table, prop, prop.first, CollectionInstance::ExpansionRule, ExpansionRuleEnumHandler, CollectionInstance,
                       r, strict_allowedToken_check)

        if (table.count(prop.first)) {
          CollectionInstance &coll_instance = coll->get_or_add_instance(instance_name);
          coll_instance.expansionRule = r;  // Assign full TypedAttributeWithFallback to preserve authored state
        }
      } else if (names[1] == "includeRoot") {

        TypedAttributeWithFallback<Animatable<bool>> includeRoot{false};
        PARSE_TYPED_ATTRIBUTE_NOCONTINUE(table, prop, prop.first, CollectionInstance, includeRoot)

        if (table.count(prop.first)) {
          CollectionInstance &coll_instance = coll->get_or_add_instance(instance_name);
          coll_instance.includeRoot = includeRoot;
        }
      } else if (names[1] == "excludes") {

        if (!prop.second.is_relationship()) {
          PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
        }

        CollectionInstance &coll_instance = coll->get_or_add_instance(instance_name);
        coll_instance.excludes = prop.second.get_relationship();
        table.insert(prop.first);

      }
    }
  }

  return true;
}
// xformOps and built-in props
bool ReconstructGPrimProperties(
  const Specifier &spec,
  std::set<std::string> &table, /* inout */
  std::map<std::string, Property> &properties,
  GPrim *gprim, /* inout */
  std::string *warn,
  std::string *err,
  bool strict_allowedToken_check)
{

  (void)warn;
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &gprim->xformOps, err)) {
    return false;
  }

  if (!prim::ReconstructMaterialBindingProperties(table, properties, gprim, err)) {
    return false;
  }

  if (!prim::ReconstructCollectionProperties(
    table, properties, gprim, warn, err, strict_allowedToken_check)) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kProxyPrim, gprim->proxyPrim)
    PARSE_TYPED_ATTRIBUTE(table, prop, "doubleSided", GPrim, gprim->doubleSided)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, GPrim,
                   gprim->visibility, strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "purpose", Purpose, PurposeEnumHandler, GPrim,
                       gprim->purpose, strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "orientation", Orientation, OrientationEnumHandler, GPrim,
                       gprim->orientation, strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, "extent", GPrim, gprim->extent)
  }

  return true;
}

} // namespace local


template <>
bool ReconstructPrim<Xform>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Xform *xform,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)options;
  (void)references;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, xform, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    ADD_PROPERTY(table, prop, Xform, xform->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Model>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Model *model,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  DCOUT("Model ");
  (void)spec;
  (void)references;
  (void)model;
  (void)err;
  (void)options;

  std::set<std::string> table;
  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    ADD_PROPERTY(table, prop, Model, model->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Scope>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Scope *scope,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  // `Scope` is just a namespace in scene graph(no node xform)

  (void)spec;
  (void)references;
  (void)scope;
  (void)err;
  (void)options;

  DCOUT("Scope");
  std::set<std::string> table;
  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, Scope,
                   scope->visibility, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, Scope, scope->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<SkelRoot>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SkelRoot *root,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &root->xformOps, err)) {
    return false;
  }

  // SkelRoot is something like a grouping node, having 1 Skeleton and possibly?
  // multiple Prim hierarchy containing GeomMesh.
  // No specific properties for SkelRoot(AFAIK)

  // custom props only
  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, SkelRoot,
                   root->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, SkelRoot,
                       root->purpose, options.strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, SkelRoot, root->extent)
    ADD_PROPERTY(table, prop, SkelRoot, root->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Skeleton>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Skeleton *skel,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &skel->xformOps, err)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ Skeleton
#define PRIM_PTR_ skel
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {

    // SkelBindingAPI: animationSource relationship
    if (prop.first == kSkelAnimationSource) {
      // Must be relation of type Path.
      if (prop.second.is_relationship() && prop.second.get_relationship().is_path()) {
        const Relationship &rel = prop.second.get_relationship();
        if (rel.is_path()) {
          skel->animationSource = rel;
          table.insert(kSkelAnimationSource);
        } else {
          PUSH_ERROR_AND_RETURN("`" << kSkelAnimationSource << "` target must be Path.");
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "`" << kSkelAnimationSource << "` must be a Relationship with Path target.");
      }
    }

    SKELETON_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, Skeleton,
                   skel->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "purpose", Purpose, PurposeEnumHandler, Skeleton,
                       skel->purpose, options.strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, "extent", Skeleton, skel->extent)
    ADD_PROPERTY(table, prop, Skeleton, skel->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

#if 0 // TODO: bindTransforms & restTransforms check somewhere.
  // usdview and Houdini USD importer expects both `bindTransforms` and `restTransforms` are authored in USD
  if (!table.count("bindTransforms")) {
    // usdview and Houdini allow `bindTransforms` is not authord in USD, but it cannot compute skinning correctly without it,
    // so report an error in TinyUSDZ for a while.
    PUSH_ERROR_AND_RETURN_TAG(kTag, "`bindTransforms` is missing in Skeleton. Currently TinyUSDZ expects `bindTransforms` must exist in Skeleton.");
  }

  if (!table.count("restTransforms")) {
    // usdview and Houdini allow `restTransforms` is not authord in USD(usdview warns it), but it cannot compute skinning correctly without it,
    // (even SkelAnimation supplies trasnforms for all joints)
    // so report an error in TinyUSDZ for a while.
    PUSH_ERROR_AND_RETURN_TAG(kTag, "`restTransforms`(local joint matrices at rest state) is missing in Skeleton. Currently TinyUSDZ expects `restTransforms` must exist in Skeleton.");
  }

  // len(bindTransforms) must be equal to len(restTransforms)
  // TODO: Support connection
  {
    bool valid = false;
    if (auto bt = skel->bindTransforms.get_value()) {
      if (auto rt = skel->restTransforms.get_value()) {
        if (bt.value().size() == rt.value().size()) {
          // ok
          valid = true;
        }
      }
    }

    if (!valid) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Array length must be same for `bindTransforms` and `restTransforms`.");
    }
  }
#endif

  return true;
}

template <>
bool ReconstructPrim<SkelAnimation>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SkelAnimation *skelanim,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)warn;
  (void)references;
  (void)options;

  std::set<std::string> table;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ SkelAnimation
#define PRIM_PTR_ skelanim
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    SKEL_ANIMATION_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, SkelAnimation, skelanim->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<BlendShape>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    BlendShape *bs,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct BlendShape");

  std::set<std::string> table;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ BlendShape
#define PRIM_PTR_ bs
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    BLEND_SHAPE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, BlendShape, bs->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

#if 0 // TODO: Check required properties exist in strict mode.
  // `offsets` and `normalOffsets` are required property
  if (!table.count("offsets")) {
    PUSH_ERROR_AND_RETURN("`offsets` property is missing. `uniform vector3f[] offsets` is a required property.");
  }
  if (!table.count("normalOffsets")) {
    PUSH_ERROR_AND_RETURN("`normalOffsets` property is missing. `uniform vector3f[] normalOffsets` is a required property.");
  }
#endif

  return true;
}

template <>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GPrim *gprim,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)gprim;
  (void)err;

  (void)references;
  (void)properties;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, gprim, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  return true;
}

template <>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomBasisCurves *curves,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;

  DCOUT("GeomBasisCurves");

  // Use centralized enum handlers
  auto BasisHandler = enum_handler::BasisCurvesBasis;
  auto TypeHandler = enum_handler::BasisCurvesType;
  auto WrapHandler = enum_handler::BasisCurvesWrap;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, curves, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomBasisCurves
#define PRIM_PTR_ curves
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_BASIS_CURVES_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_BASIS_CURVES_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomBasisCurves, curves->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomNurbsCurves *curves,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;
  (void)options;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, curves, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_TYPED_ATTRIBUTE(table, prop, "curveVertexCounts", GeomNurbsCurves,
                         curves->curveVertexCounts)
    PARSE_TYPED_ATTRIBUTE(table, prop, "points", GeomNurbsCurves, curves->points)
    PARSE_TYPED_ATTRIBUTE(table, prop, "velocities", GeomNurbsCurves,
                          curves->velocities)
    PARSE_TYPED_ATTRIBUTE(table, prop, "normals", GeomNurbsCurves,
                  curves->normals)
    PARSE_TYPED_ATTRIBUTE(table, prop, "accelerations", GeomNurbsCurves,
                 curves->accelerations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "widths", GeomNurbsCurves, curves->widths)

    //
    PARSE_TYPED_ATTRIBUTE(table, prop, "order", GeomNurbsCurves, curves->order)
    PARSE_TYPED_ATTRIBUTE(table, prop, "knots", GeomNurbsCurves, curves->knots)
    PARSE_TYPED_ATTRIBUTE(table, prop, "ranges", GeomNurbsCurves, curves->ranges)
    PARSE_TYPED_ATTRIBUTE(table, prop, "pointWeights", GeomNurbsCurves, curves->pointWeights)

    ADD_PROPERTY(table, prop, GeomBasisCurves, curves->props)

    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

// ============================================================================
// Generic macro for light prim reconstruction
// ============================================================================
// Consolidates the common pattern for all light types:
// SphereLight, RectLight, DiskLight, CylinderLight, DistantLight, GeometryLight, DomeLight
//
// IMPORTANT: Caller must define PRIM_CLASS_ and PRIM_PTR_ macros before calling
//            this macro, and undef them afterward. These are required by
//            EXPAND_TYPED_ATTR macros.
//
// Parameters:
//   LightClass: The light class (e.g., SphereLight, RectLight)
//   light_ptr: Pointer to the light instance
//   TYPED_ATTRS: Property table macro (e.g., SPHERE_LIGHT_TYPED_ATTRS)
//   COMMON_ATTRS: Light common attrs macro (LIGHT_COMMON_ATTRS_WITH_SHAPING or LIGHT_COMMON_ATTRS_NO_SHAPING)
//   EXTENT_HANDLING: Either PARSE_EXTENT_ATTRIBUTE(...) or /* no extent */
//   SPECIAL_HANDLING: Special attribute handling for exceptions like RectLight's texture:file or /* no special handling */
#define RECONSTRUCT_LIGHT_PRIM_BODY(LightClass, light_ptr, TYPED_ATTRS, COMMON_ATTRS, EXTENT_HANDLING, SPECIAL_HANDLING) \
  (void)references; \
  \
  std::set<std::string> table; \
  \
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &light_ptr->xformOps, err)) { \
    return false; \
  } \
  \
  for (auto &prop : properties) { \
    SPECIAL_HANDLING \
    TYPED_ATTRS(EXPAND_TYPED_ATTR) \
    COMMON_ATTRS(EXPAND_TYPED_ATTR) \
    EXTENT_HANDLING \
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, LightClass, \
                       light_ptr->visibility, options.strict_allowedToken_check) \
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, LightClass, \
                       light_ptr->purpose, options.strict_allowedToken_check) \
    ADD_PROPERTY(table, prop, LightClass, light_ptr->props) \
    PARSE_PROPERTY_END_MAKE_WARN(table, prop) \
  } \
  \
  return true;

template <>
bool ReconstructPrim<SphereLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SphereLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ SphereLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(SphereLight, light, SPHERE_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, SphereLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<RectLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    RectLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ RectLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  // Special case: texture:file uses UsdUVTexture type
  RECONSTRUCT_LIGHT_PRIM_BODY(RectLight, light, RECT_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, RectLight, light->extent),
                              PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:texture:file", UsdUVTexture, light->file))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DiskLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DiskLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DiskLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(DiskLight, light, DISK_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, DiskLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<CylinderLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    CylinderLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ CylinderLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(CylinderLight, light, CYLINDER_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, CylinderLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DistantLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DistantLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DistantLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(DistantLight, light, DISTANT_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeometryLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeometryLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeometryLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(GeometryLight, light, GEOMETRY_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DomeLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DomeLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DomeLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  DCOUT("Implement DomeLight");
  RECONSTRUCT_LIGHT_PRIM_BODY(DomeLight, light, DOME_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

// ============================================================================
// Generic macro for simple geometry prim reconstruction
// ============================================================================
// Consolidates the common pattern for GeomSphere, GeomCone, GeomCylinder,
// GeomCapsule, GeomCube: ReconstructGPrimProperties + property loop
//
// IMPORTANT: Caller must define PRIM_CLASS_ and PRIM_PTR_ macros before calling
//            this macro, and undef them afterward. These are required by
//            EXPAND_TYPED_ATTR and EXPAND_UNIFORM_ENUM macros.
//
// Parameters:
//   PrimClass: The geometry class (e.g., GeomSphere, GeomCone)
//   prim_ptr: Pointer to the prim instance
//   TYPED_ATTRS: Property table macro (e.g., GEOM_SPHERE_TYPED_ATTRS)
//   ENUM_EXPANSION: Enum handling macro call or empty
//                   - For shapes without enums: /* empty */
//                   - For shapes with enums: GEOM_XXX_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
#define RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(PrimClass, prim_ptr, TYPED_ATTRS, ENUM_EXPANSION) \
  (void)references; \
  \
  std::set<std::string> table; \
  if (!ReconstructGPrimProperties(spec, table, properties, prim_ptr, warn, err, \
                                   options.strict_allowedToken_check)) { \
    return false; \
  } \
  \
  for (auto &prop : properties) { \
    TYPED_ATTRS(EXPAND_TYPED_ATTR) \
    ENUM_EXPANSION \
    ADD_PROPERTY(table, prop, PrimClass, prim_ptr->props) \
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop) \
  } \
  \
  return true;

template <>
bool ReconstructPrim<GeomSphere>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomSphere *sphere,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  DCOUT("Reconstruct Sphere.");
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomSphere
#define PRIM_PTR_ sphere
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomSphere, sphere, GEOM_SPHERE_TYPED_ATTRS, /* no enums */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomPoints>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomPoints *points,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;

  DCOUT("Reconstruct Points.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, points, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomPoints
#define PRIM_PTR_ points
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    DCOUT("prop: " << prop.first);
    GEOM_POINTS_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, GeomPoints, points->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomCone>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCone *cone,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCone
#define PRIM_PTR_ cone
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCone, cone, GEOM_CONE_TYPED_ATTRS,
                                     GEOM_CONE_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCylinder>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCylinder *cylinder,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCylinder
#define PRIM_PTR_ cylinder
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCylinder, cylinder, GEOM_CYLINDER_TYPED_ATTRS,
                                     GEOM_CYLINDER_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCapsule>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCapsule *capsule,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCapsule
#define PRIM_PTR_ capsule
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCapsule, capsule, GEOM_CAPSULE_TYPED_ATTRS,
                                     GEOM_CAPSULE_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCube>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCube *cube,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  // pxrUSD says... "If you author size you must also author extent."
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCube
#define PRIM_PTR_ cube
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCube, cube, GEOM_CUBE_TYPED_ATTRS, /* no enums */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomMesh>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomMesh *mesh,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;

  DCOUT("GeomMesh");

  // Use centralized enum handlers (aliased for macro expansion)
  auto SubdivisionSchemeHandler = enum_handler::SubdivisionScheme;
  auto InterpolateBoundaryHandler = enum_handler::InterpolateBoundary;
  auto FaceVaryingLinearInterpolationHandler = enum_handler::FaceVaryingLinearInterpolation;
  auto FamilyTypeHandler = enum_handler::FamilyType;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, mesh, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  // Define context for property table expansion macros
  // (suppress unused-macros warning since these are used inside X-macro expansion)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomMesh
#define PRIM_PTR_ mesh
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    DCOUT("GeomMesh prop: " << prop.first);

    // Relations (using property table)
    GEOM_MESH_RELATIONS(EXPAND_SINGLE_REL, EXPAND_MULTI_REL)

    // Typed attributes (using property table)
    GEOM_MESH_TYPED_ATTRS(EXPAND_TYPED_ATTR)

    // Skel-related typed attributes
    GEOM_MESH_SKEL_ATTRS(EXPAND_TYPED_ATTR)

    // Enum properties (using property table)
    GEOM_MESH_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    GEOM_MESH_TIMESAMPLED_ENUMS(EXPAND_TIMESAMPLED_ENUM)

    // Special handling: subsetFamily for GeomSubset (cannot be table-driven)
    if (startsWith(prop.first, "subsetFamily")) {
      // uniform subsetFamily::<FAMILYNAME>:familyType = ...
      std::vector<std::string> names = split(prop.first, ":");

      if ((names.size() == 3) &&
          (names[0] == "subsetFamily") &&
          (names[2] == "familyType")) {

        if (table.count(prop.first)) {
          // Already processed
        } else if ((prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) &&
                   prop.second.is_attribute() &&
                   !prop.second.is_empty()) {
          // Parse the token enum value
          const Attribute &attr = prop.second.get_attribute();
          TypedAttributeWithFallback<GeomSubset::FamilyType> familyType{GeomSubset::FamilyType::Unrestricted};
          std::function<nonstd::expected<GeomSubset::FamilyType, std::string>(const std::string &)> fun = FamilyTypeHandler;

          if (!ParseUniformEnumProperty(prop.first, options.strict_allowedToken_check, fun, attr, &familyType, warn, err)) {
            return false;
          }

          // NOTE: Ignore metadata of familyType.
          // TODO: Validate familyName
          mesh->subsetFamilyTypeMap[value::token(names[1])] = familyType.get_value();
          table.insert(prop.first);
        }
      }
    }

    // generic property handling
    ADD_PROPERTY(table, prop, GeomMesh, mesh->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}


template <>
bool ReconstructPrim<GeomCamera>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCamera *camera,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;
  (void)warn;

  // Use centralized enum handlers
  auto ProjectionHandler = enum_handler::CameraProjection;
  auto StereoRoleHandler = enum_handler::CameraStereoRole;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, camera, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCamera
#define PRIM_PTR_ camera
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_CAMERA_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_CAMERA_TIMESAMPLED_ENUMS(EXPAND_TIMESAMPLED_ENUM)
    GEOM_CAMERA_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomCamera, camera->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomSubset>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomSubset *subset,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)references;

  DCOUT("GeomSubset");

  // Use centralized enum handler
  auto ElementTypeHandler = enum_handler::ElementType;

  std::set<std::string> table;

  if (!prim::ReconstructMaterialBindingProperties(table, properties, subset, err)) {
    return false;
  }

  if (!prim::ReconstructCollectionProperties(
    table, properties, subset, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomSubset
#define PRIM_PTR_ subset
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_SUBSET_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_SUBSET_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomSubset, subset->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomPointInstancer>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomPointInstancer *instancer,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;

  DCOUT("Reconstruct GeomPointInstancer.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, instancer, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomPointInstancer
#define PRIM_PTR_ instancer
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_POINT_INSTANCER_RELATIONS(EXPAND_SINGLE_REL, EXPAND_MULTI_REL)
    GEOM_POINT_INSTANCER_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, GeomPointInstancer, instancer->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructShader<ShaderNode>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    ShaderNode *node,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)options;

  if (!node) {
    return false;
  }

  // TODO: references
  (void)references;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  // Add everything to props.
  for (auto &prop : properties) {
    ADD_PROPERTY(table, prop, ShaderNode, node->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  DCOUT("ShaderNode reconstructed.");
  return true;
}

template <>
bool ReconstructShader<UsdPreviewSurface>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPreviewSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;
  (void)options;

  // Use centralized enum handler
  auto OpacityModeHandler = enum_handler::OpacityMode;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>
  for (auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:diffuseColor", UsdPreviewSurface,
                         surface->diffuseColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emissiveColor", UsdPreviewSurface,
                         surface->emissiveColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:roughness", UsdPreviewSurface,
                         surface->roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specularColor", UsdPreviewSurface,
                         surface->specularColor)  // specular workflow
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:metallic", UsdPreviewSurface,
                         surface->metallic)  // non specular workflow
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:clearcoat", UsdPreviewSurface,
                         surface->clearcoat)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:clearcoatRoughness",
                         UsdPreviewSurface, surface->clearcoatRoughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacity", UsdPreviewSurface,
                         surface->opacity)
    // From 2.6
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "inputs:opacityMode",
                       UsdPreviewSurface::OpacityMode, OpacityModeHandler, UsdPreviewSurface,
                       surface->opacityMode, options.strict_allowedToken_check)

    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacityThreshold",
                         UsdPreviewSurface, surface->opacityThreshold)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:ior", UsdPreviewSurface,
                         surface->ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:normal", UsdPreviewSurface,
                         surface->normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:dispacement", UsdPreviewSurface,
                         surface->displacement)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:occlusion", UsdPreviewSurface,
                         surface->occlusion)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:useSpecularWorkflow",
                         UsdPreviewSurface, surface->useSpecularWorkflow)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:surface", UsdPreviewSurface,
                   surface->outputsSurface)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:displacement", UsdPreviewSurface,
                   surface->outputsDisplacement)
    ADD_PROPERTY(table, prop, UsdPreviewSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<UsdUVTexture>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdUVTexture *texture,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)options;

  // Use centralized enum handlers
  auto SourceColorSpaceHandler = enum_handler::SourceColorSpace;
  auto WrapHandler = enum_handler::TextureWrap;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  for (auto &prop : properties) {
    DCOUT("prop.name = " << prop.first);
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:file", UsdUVTexture, texture->file)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:st", UsdUVTexture,
                          texture->st)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "inputs:sourceColorSpace",
                       UsdUVTexture::SourceColorSpace, SourceColorSpaceHandler, UsdUVTexture,
                       texture->sourceColorSpace, options.strict_allowedToken_check)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "inputs:wrapS",
                       UsdUVTexture::Wrap, WrapHandler, UsdUVTexture,
                       texture->wrapS, options.strict_allowedToken_check)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "inputs:wrapT",
                       UsdUVTexture::Wrap, WrapHandler, UsdUVTexture,
                       texture->wrapT, options.strict_allowedToken_check)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:r", UsdUVTexture,
                                  texture->outputsR)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:g", UsdUVTexture,
                                  texture->outputsG)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:b", UsdUVTexture,
                                  texture->outputsB)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:a", UsdUVTexture,
                                  texture->outputsA)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:rgb", UsdUVTexture,
                                  texture->outputsRGB)
    ADD_PROPERTY(table, prop, UsdUVTexture, texture->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  DCOUT("UsdUVTexture reconstructed.");
  return true;
}

// Helper macro for parsing inputs:varname with backwards compatibility
// Supports both token (older spec) and string (current spec) types
#define PARSE_PRIMVAR_READER_VARNAME(__table, __prop, __varname_attr, __err_msg_prefix) \
  if ((__prop.first == kInputsVarname) && !__table.count(kInputsVarname)) {             \
    /* Support older spec: token type for varname */                                    \
    TypedAttribute<Animatable<value::token>> tok_attr;                                  \
    auto ret = ParseTypedAttribute(__table, __prop.first, __prop.second, kInputsVarname, tok_attr); \
    if (ret.code == ParseResult::ResultCode::Success) {                                 \
      if (!ConvertTokenAttributeToStringAttribute(tok_attr, __varname_attr)) {          \
        PUSH_ERROR_AND_RETURN(__err_msg_prefix "Failed to convert inputs:varname token type to string type."); \
      }                                                                                  \
      continue;                                                                          \
    } else if (ret.code == ParseResult::ResultCode::TypeMismatch) {                     \
      /* Try parsing as string type */                                                  \
      ret = ParseTypedAttribute(__table, __prop.first, __prop.second, "inputs:varname", __varname_attr); \
      if (ret.code == ParseResult::ResultCode::Success) {                               \
        continue;                                                                        \
      } else {                                                                           \
        PUSH_ERROR_AND_RETURN(fmt::format(__err_msg_prefix "Failed to parse inputs:varname: {}", ret.err)); \
      }                                                                                  \
    }                                                                                    \
  }

// ============================================================================
// Generic PrimvarReader Shader Reconstruction
// ============================================================================
// All PrimvarReader variants (int, float, float2, float3, float4, string,
// vector, normal, point, matrix) follow identical logic - only the type differs.
// This helper eliminates ~220 lines of duplication.

template<typename PrimvarReaderT>
static bool ReconstructPrimvarReaderShaderImpl(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    PrimvarReaderT *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)options;
  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>
  for (auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fallback", PrimvarReaderT,
                   preader->fallback)
    PARSE_PRIMVAR_READER_VARNAME(table, prop, preader->varname, "")
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:result",
                                  PrimvarReaderT, preader->result)
    ADD_PROPERTY(table, prop, PrimvarReaderT, preader->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
  return true;
}

template <>
bool ReconstructShader<UsdPrimvarReader_int>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_int *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_float>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_float *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_float2>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_float2 *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_float3>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_float3 *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_float4>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_float4 *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_string>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_string *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_vector>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_vector *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_normal>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_normal *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_point>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_point *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_matrix>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_matrix *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<MtlxAutodeskStandardSurface>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    MtlxAutodeskStandardSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;
  (void)options;
  (void)warn;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  for (auto &prop : properties) {
    // Base properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base", MtlxAutodeskStandardSurface,
                         surface->base)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_color", MtlxAutodeskStandardSurface,
                         surface->base_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:diffuse_roughness", MtlxAutodeskStandardSurface,
                         surface->diffuse_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:metalness", MtlxAutodeskStandardSurface,
                         surface->metalness)

    // Specular properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular", MtlxAutodeskStandardSurface,
                         surface->specular)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_color", MtlxAutodeskStandardSurface,
                         surface->specular_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_roughness", MtlxAutodeskStandardSurface,
                         surface->specular_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_IOR", MtlxAutodeskStandardSurface,
                         surface->specular_IOR)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_anisotropy", MtlxAutodeskStandardSurface,
                         surface->specular_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_rotation", MtlxAutodeskStandardSurface,
                         surface->specular_rotation)

    // Transmission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission", MtlxAutodeskStandardSurface,
                         surface->transmission)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_color", MtlxAutodeskStandardSurface,
                         surface->transmission_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_depth", MtlxAutodeskStandardSurface,
                         surface->transmission_depth)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter", MtlxAutodeskStandardSurface,
                         surface->transmission_scatter)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter_anisotropy", MtlxAutodeskStandardSurface,
                         surface->transmission_scatter_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion", MtlxAutodeskStandardSurface,
                         surface->transmission_dispersion)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_extra_roughness", MtlxAutodeskStandardSurface,
                         surface->transmission_extra_roughness)

    // Subsurface properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface", MtlxAutodeskStandardSurface,
                         surface->subsurface)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_color", MtlxAutodeskStandardSurface,
                         surface->subsurface_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_radius", MtlxAutodeskStandardSurface,
                         surface->subsurface_radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_scale", MtlxAutodeskStandardSurface,
                         surface->subsurface_scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_anisotropy", MtlxAutodeskStandardSurface,
                         surface->subsurface_anisotropy)

    // Sheen properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen", MtlxAutodeskStandardSurface,
                         surface->sheen)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_color", MtlxAutodeskStandardSurface,
                         surface->sheen_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_roughness", MtlxAutodeskStandardSurface,
                         surface->sheen_roughness)

    // Coat properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat", MtlxAutodeskStandardSurface,
                         surface->coat)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_color", MtlxAutodeskStandardSurface,
                         surface->coat_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_roughness", MtlxAutodeskStandardSurface,
                         surface->coat_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_anisotropy", MtlxAutodeskStandardSurface,
                         surface->coat_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_rotation", MtlxAutodeskStandardSurface,
                         surface->coat_rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_IOR", MtlxAutodeskStandardSurface,
                         surface->coat_IOR)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_color", MtlxAutodeskStandardSurface,
                         surface->coat_affect_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_roughness", MtlxAutodeskStandardSurface,
                         surface->coat_affect_roughness)

    // Thin film properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_thickness", MtlxAutodeskStandardSurface,
                         surface->thin_film_thickness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_IOR", MtlxAutodeskStandardSurface,
                         surface->thin_film_IOR)

    // Emission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission", MtlxAutodeskStandardSurface,
                         surface->emission)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_color", MtlxAutodeskStandardSurface,
                         surface->emission_color)

    // Other properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacity", MtlxAutodeskStandardSurface,
                         surface->opacity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_walled", MtlxAutodeskStandardSurface,
                         surface->thin_walled)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:normal", MtlxAutodeskStandardSurface,
                         surface->normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:tangent", MtlxAutodeskStandardSurface,
                         surface->tangent)

    // Output
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:out", MtlxAutodeskStandardSurface,
                   surface->out)

    ADD_PROPERTY(table, prop, MtlxAutodeskStandardSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<MtlxOpenPBRSurface>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    MtlxOpenPBRSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;
  (void)options;
  (void)warn;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  for (auto &prop : properties) {
    // Base properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_weight", MtlxOpenPBRSurface,
                         surface->base_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_color", MtlxOpenPBRSurface,
                         surface->base_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_metalness", MtlxOpenPBRSurface,
                         surface->base_metalness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_diffuse_roughness", MtlxOpenPBRSurface,
                         surface->base_diffuse_roughness)

    // Specular properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_weight", MtlxOpenPBRSurface,
                         surface->specular_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_color", MtlxOpenPBRSurface,
                         surface->specular_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_roughness", MtlxOpenPBRSurface,
                         surface->specular_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_ior", MtlxOpenPBRSurface,
                         surface->specular_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_anisotropy", MtlxOpenPBRSurface,
                         surface->specular_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_rotation", MtlxOpenPBRSurface,
                         surface->specular_rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_roughness_anisotropy", MtlxOpenPBRSurface,
                         surface->specular_roughness_anisotropy)

    // Transmission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_weight", MtlxOpenPBRSurface,
                         surface->transmission_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_color", MtlxOpenPBRSurface,
                         surface->transmission_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_depth", MtlxOpenPBRSurface,
                         surface->transmission_depth)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter", MtlxOpenPBRSurface,
                         surface->transmission_scatter)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter_anisotropy", MtlxOpenPBRSurface,
                         surface->transmission_scatter_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion", MtlxOpenPBRSurface,
                         surface->transmission_dispersion)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion_abbe_number", MtlxOpenPBRSurface,
                         surface->transmission_dispersion_abbe_number)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion_scale", MtlxOpenPBRSurface,
                         surface->transmission_dispersion_scale)

    // Subsurface properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_weight", MtlxOpenPBRSurface,
                         surface->subsurface_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_color", MtlxOpenPBRSurface,
                         surface->subsurface_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_radius", MtlxOpenPBRSurface,
                         surface->subsurface_radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_radius_scale", MtlxOpenPBRSurface,
                         surface->subsurface_radius_scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_scale", MtlxOpenPBRSurface,
                         surface->subsurface_scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_anisotropy", MtlxOpenPBRSurface,
                         surface->subsurface_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_scatter_anisotropy", MtlxOpenPBRSurface,
                         surface->subsurface_scatter_anisotropy)

    // Coat properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_weight", MtlxOpenPBRSurface,
                         surface->coat_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_color", MtlxOpenPBRSurface,
                         surface->coat_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_roughness", MtlxOpenPBRSurface,
                         surface->coat_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_anisotropy", MtlxOpenPBRSurface,
                         surface->coat_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_rotation", MtlxOpenPBRSurface,
                         surface->coat_rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_roughness_anisotropy", MtlxOpenPBRSurface,
                         surface->coat_roughness_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_ior", MtlxOpenPBRSurface,
                         surface->coat_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_darkening", MtlxOpenPBRSurface,
                         surface->coat_darkening)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_color", MtlxOpenPBRSurface,
                         surface->coat_affect_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_roughness", MtlxOpenPBRSurface,
                         surface->coat_affect_roughness)

    // Fuzz properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_weight", MtlxOpenPBRSurface,
                         surface->fuzz_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_color", MtlxOpenPBRSurface,
                         surface->fuzz_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_roughness", MtlxOpenPBRSurface,
                         surface->fuzz_roughness)

    // Thin film properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_thickness", MtlxOpenPBRSurface,
                         surface->thin_film_thickness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_ior", MtlxOpenPBRSurface,
                         surface->thin_film_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_weight", MtlxOpenPBRSurface,
                         surface->thin_film_weight)

    // Emission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_luminance", MtlxOpenPBRSurface,
                         surface->emission_luminance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_color", MtlxOpenPBRSurface,
                         surface->emission_color)

    // Geometry properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_opacity", MtlxOpenPBRSurface,
                         surface->geometry_opacity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_thin_walled", MtlxOpenPBRSurface,
                         surface->geometry_thin_walled)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_normal", MtlxOpenPBRSurface,
                         surface->geometry_normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_tangent", MtlxOpenPBRSurface,
                         surface->geometry_tangent)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_coat_normal", MtlxOpenPBRSurface,
                         surface->geometry_coat_normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_coat_tangent", MtlxOpenPBRSurface,
                         surface->geometry_coat_tangent)

    // Output
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:surface", MtlxOpenPBRSurface,
                   surface->surface)

    ADD_PROPERTY(table, prop, MtlxOpenPBRSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<OpenPBRSurface>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    OpenPBRSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;
  (void)options;
  (void)warn;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  for (auto &prop : properties) {
    // Base layer properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_weight", OpenPBRSurface,
                         surface->base_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_color", OpenPBRSurface,
                         surface->base_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_roughness", OpenPBRSurface,
                         surface->base_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_metalness", OpenPBRSurface,
                         surface->base_metalness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_diffuse_roughness", OpenPBRSurface,
                         surface->base_diffuse_roughness)

    // Specular layer properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_weight", OpenPBRSurface,
                         surface->specular_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_color", OpenPBRSurface,
                         surface->specular_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_roughness", OpenPBRSurface,
                         surface->specular_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_ior", OpenPBRSurface,
                         surface->specular_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_ior_level", OpenPBRSurface,
                         surface->specular_ior_level)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_anisotropy", OpenPBRSurface,
                         surface->specular_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_rotation", OpenPBRSurface,
                         surface->specular_rotation)

    // Transmission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_weight", OpenPBRSurface,
                         surface->transmission_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_color", OpenPBRSurface,
                         surface->transmission_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_depth", OpenPBRSurface,
                         surface->transmission_depth)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter", OpenPBRSurface,
                         surface->transmission_scatter)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter_anisotropy", OpenPBRSurface,
                         surface->transmission_scatter_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion", OpenPBRSurface,
                         surface->transmission_dispersion)

    // Subsurface properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_weight", OpenPBRSurface,
                         surface->subsurface_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_color", OpenPBRSurface,
                         surface->subsurface_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_radius", OpenPBRSurface,
                         surface->subsurface_radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_scale", OpenPBRSurface,
                         surface->subsurface_scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_anisotropy", OpenPBRSurface,
                         surface->subsurface_anisotropy)

    // Sheen properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_weight", OpenPBRSurface,
                         surface->sheen_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_color", OpenPBRSurface,
                         surface->sheen_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_roughness", OpenPBRSurface,
                         surface->sheen_roughness)

    // Fuzz properties (velvet/fabric-like appearance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_weight", OpenPBRSurface,
                         surface->fuzz_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_color", OpenPBRSurface,
                         surface->fuzz_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_roughness", OpenPBRSurface,
                         surface->fuzz_roughness)

    // Thin film properties (iridescence from thin film interference)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_weight", OpenPBRSurface,
                         surface->thin_film_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_thickness", OpenPBRSurface,
                         surface->thin_film_thickness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_ior", OpenPBRSurface,
                         surface->thin_film_ior)

    // Coat properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_weight", OpenPBRSurface,
                         surface->coat_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_color", OpenPBRSurface,
                         surface->coat_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_roughness", OpenPBRSurface,
                         surface->coat_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_anisotropy", OpenPBRSurface,
                         surface->coat_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_rotation", OpenPBRSurface,
                         surface->coat_rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_ior", OpenPBRSurface,
                         surface->coat_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_color", OpenPBRSurface,
                         surface->coat_affect_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_roughness", OpenPBRSurface,
                         surface->coat_affect_roughness)

    // Emission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_luminance", OpenPBRSurface,
                         surface->emission_luminance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_color", OpenPBRSurface,
                         surface->emission_color)

    // Geometry properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacity", OpenPBRSurface,
                         surface->opacity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_opacity", OpenPBRSurface,
                         surface->opacity)  // OpenPBR standard name, maps to same opacity field
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:normal", OpenPBRSurface,
                         surface->normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:tangent", OpenPBRSurface,
                         surface->tangent)

    // Outputs
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:surface", OpenPBRSurface,
                   surface->surface)

    ADD_PROPERTY(table, prop, OpenPBRSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<UsdTransform2d>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdTransform2d *transform,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)options;
  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>
  for (auto &prop : properties) {
    DCOUT("prop = " << prop.first);
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:in", UsdTransform2d,
                   transform->in)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:rotation", UsdTransform2d,
                   transform->rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:scale", UsdTransform2d,
                   transform->scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:translation", UsdTransform2d,
                   transform->translation)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:result",
                                  UsdTransform2d, transform->result)
    ADD_PROPERTY(table, prop, UsdPrimvarReader_float2, transform->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Shader>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Shader *shader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)properties;
  (void)options;

  bool is_generic_shader{false};
  auto info_id_prop = properties.find("info:id");
  if (info_id_prop == properties.end()) {
    // Guess MatrialX shader. info:id will be resolved by importing referenced .mtlx.
    // Treat generic Shader at the moment.
    is_generic_shader = true;
    //PUSH_ERROR_AND_RETURN("`Shader` must contain `info:id` property.");
  }

  std::string shader_type;
  if (!is_generic_shader) {
    if (info_id_prop->second.is_attribute()) {
      const Attribute &attr = info_id_prop->second.get_attribute();
      if ((attr.type_name() == value::kToken)) {
        if (auto pv = attr.get_value<value::token>()) {
          shader_type = pv.value().str();
        } else {
          PUSH_ERROR_AND_RETURN("Internal errror. `info:id` has invalid type.");
        }
      } else {
        PUSH_ERROR_AND_RETURN("`info:id` attribute must be `token` type.");
      }

      // For some corrupted? USDZ file does not have `uniform` variability.
      if (attr.variability() != Variability::Uniform) {
        PUSH_WARN("`info:id` attribute must have `uniform` variability.");
      }
    } else {
      PUSH_ERROR_AND_RETURN("Invalid type or value for `info:id` property in `Shader`.");
    }

    DCOUT("info:id = " << shader_type);
  }


  if (shader_type.compare(kUsdPreviewSurface) == 0) {
    UsdPreviewSurface surface;
    if (!ReconstructShader<UsdPreviewSurface>(spec, properties, references,
                                              &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kUsdPreviewSurface);
    }
    shader->info_id = kUsdPreviewSurface;
    shader->value = surface;
    DCOUT("info_id = " << shader->info_id);
  } else if (shader_type.compare(kUsdUVTexture) == 0) {
    UsdUVTexture texture;
    if (!ReconstructShader<UsdUVTexture>(spec, properties, references,
                                         &texture, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kUsdUVTexture);
    }
    shader->info_id = kUsdUVTexture;
    shader->value = texture;
  } else if (shader_type.compare(kUsdPrimvarReader_int) == 0) {
    UsdPrimvarReader_int preader;
    if (!ReconstructShader<UsdPrimvarReader_int>(spec, properties, references,
                                                 &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_int);
    }
    shader->info_id = kUsdPrimvarReader_int;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_float) == 0) {
    UsdPrimvarReader_float preader;
    if (!ReconstructShader<UsdPrimvarReader_float>(spec, properties, references,
                                                   &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_float);
    }
    shader->info_id = kUsdPrimvarReader_float;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_float2) == 0) {
    UsdPrimvarReader_float2 preader;
    if (!ReconstructShader<UsdPrimvarReader_float2>(spec, properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_float2);
    }
    shader->info_id = kUsdPrimvarReader_float2;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_float3) == 0) {
    UsdPrimvarReader_float3 preader;
    if (!ReconstructShader<UsdPrimvarReader_float3>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_float3);
    }
    shader->info_id = kUsdPrimvarReader_float3;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_float4) == 0) {
    UsdPrimvarReader_float4 preader;
    if (!ReconstructShader<UsdPrimvarReader_float4>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_float4);
    }
    shader->info_id = kUsdPrimvarReader_float4;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_string) == 0) {
    UsdPrimvarReader_string preader;
    if (!ReconstructShader<UsdPrimvarReader_string>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_string);
    }
    shader->info_id = kUsdPrimvarReader_string;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_vector) == 0) {
    UsdPrimvarReader_vector preader;
    if (!ReconstructShader<UsdPrimvarReader_vector>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_vector);
    }
    shader->info_id = kUsdPrimvarReader_vector;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_normal) == 0) {
    UsdPrimvarReader_normal preader;
    if (!ReconstructShader<UsdPrimvarReader_normal>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_normal);
    }
    shader->info_id = kUsdPrimvarReader_normal;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_point) == 0) {
    UsdPrimvarReader_point preader;
    if (!ReconstructShader<UsdPrimvarReader_point>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_point);
    }
    shader->info_id = kUsdPrimvarReader_point;
    shader->value = preader;
  } else if (shader_type.compare(kUsdTransform2d) == 0) {
    UsdTransform2d transform;
    if (!ReconstructShader<UsdTransform2d>(spec,properties, references,
                                                    &transform, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdTransform2d);
    }
    shader->info_id = kUsdTransform2d;
    shader->value = transform;
  } else if (shader_type.compare(kOpenPBRSurface) == 0) {
    OpenPBRSurface surface;
    if (!ReconstructShader<OpenPBRSurface>(spec, properties, references,
                                           &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kOpenPBRSurface);
    }
    shader->info_id = kOpenPBRSurface;
    shader->value = surface;
  } else if (shader_type.compare(kMtlxAutodeskStandardSurface) == 0) {
    MtlxAutodeskStandardSurface surface;
    if (!ReconstructShader<MtlxAutodeskStandardSurface>(spec, properties, references,
                                                         &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kMtlxAutodeskStandardSurface);
    }
    shader->info_id = kMtlxAutodeskStandardSurface;
    shader->value = surface;
  } else if (shader_type.compare(kNdOpenPbrSurfaceSurfaceshader) == 0) {
    // Blender v4.5 MaterialX OpenPBR Surface export
    MtlxOpenPBRSurface surface;
    if (!ReconstructShader<MtlxOpenPBRSurface>(spec, properties, references,
                                                &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kNdOpenPbrSurfaceSurfaceshader);
    }
    shader->info_id = kNdOpenPbrSurfaceSurfaceshader;
    shader->value = surface;
  } else {
    // Reconstruct as generic ShaderNode
    ShaderNode surface;
    if (!ReconstructShader<ShaderNode>(spec,properties, references,
                                              &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << shader_type);
    }
    if (shader_type.size()) {
      shader->info_id = shader_type;
    }
    shader->value = surface;
  }

  DCOUT("Shader reconstructed.");

  return true;
}

template <>
bool ReconstructPrim<Material>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Material *material,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)options;
  std::set<std::string> table;

  // TODO: special treatment for properties with 'inputs' and 'outputs' namespace.

  // Check if MaterialXConfigAPI is applied
  bool hasMaterialXConfig = false;
  for (auto &prop : properties) {
    if (prop.first == "config:mtlx:version" ||
        prop.first == "config:mtlx:namespace" ||
        prop.first == "config:mtlx:colorspace" ||
        prop.first == "config:mtlx:sourceUri") {
      hasMaterialXConfig = true;
      break;
    }
  }

  // Initialize MaterialXConfigAPI if needed
  if (hasMaterialXConfig) {
    material->materialXConfig = MaterialXConfigAPI();
  }

  // For `Material`, `outputs` are terminal attribute and treated as input attribute with connection(Should be "token output:surface.connect = </path/to/shader>").
  for (auto &prop : properties) {
    // Parse MaterialXConfigAPI properties
    if (hasMaterialXConfig) {
      PARSE_TYPED_ATTRIBUTE(table, prop, "config:mtlx:version", Material,
                           material->materialXConfig->mtlx_version)
      PARSE_TYPED_ATTRIBUTE(table, prop, "config:mtlx:namespace", Material,
                           material->materialXConfig->mtlx_namespace)
      PARSE_TYPED_ATTRIBUTE(table, prop, "config:mtlx:colorspace", Material,
                           material->materialXConfig->mtlx_colorspace)
      PARSE_TYPED_ATTRIBUTE(table, prop, "config:mtlx:sourceUri", Material,
                           material->materialXConfig->mtlx_sourceUri)
    }

    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:surface",
                                  Material, material->surface)
    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:displacement",
                                  Material, material->displacement)
    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:volume",
                                  Material, material->volume)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, Material,
                       material->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, Material, material->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
  return true;
}

template <>
bool ReconstructPrim<NodeGraph>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    NodeGraph *nodegraph,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)warn;
  std::set<std::string> table;

  // NodeGraph can have arbitrary outputs (e.g., outputs:result, outputs:normal, etc.)
  // They are stored in the props map, so we just add all properties
  for (auto &prop : properties) {
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, NodeGraph,
                       nodegraph->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, NodeGraph, nodegraph->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
  return true;
}

///
/// -- PrimSpec
///

#define RECONSTRUCT_PRIM_PRIMSPEC_IMPL(__prim_ty) \
template <> \
bool ReconstructPrim<__prim_ty>( \
    PrimSpec &primspec, \
    __prim_ty *prim, \
    std::string *warn, \
    std::string *err, \
    const PrimReconstructOptions &options) { \
 \
  ReferenceList references; /* dummy */ \
 \
  return ReconstructPrim<__prim_ty>(primspec.specifier(), primspec.props(), references, prim, warn, err, options); \
}

RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Xform)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Model)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Scope)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomMesh)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomPoints)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCylinder)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCube)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCone)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomSphere)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCapsule)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomBasisCurves)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCamera)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomSubset)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SphereLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DomeLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(CylinderLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DiskLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DistantLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(RectLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeometryLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SkelRoot)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Skeleton)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SkelAnimation)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(BlendShape)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Shader)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Material)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(NodeGraph)


} // namespace prim

} // namespace tinyusdz
