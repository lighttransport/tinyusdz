// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Implementation helpers for prim-reconstruct split files.
// This file is included INSIDE namespace tinyusdz::prim in each split file.
//
// Suppress unused function warnings - not all split files use all helpers
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

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
      //return std::move(dst);
    }
  }

  if (var.has_timesamples()) {
    const auto &samples = var.ts_raw().get_samples();

    for (size_t i = 0; i < samples.size(); i++) {
      const value::TimeSamples::Sample &s = samples[i];

      // Attribute Block?
      if (s.blocked || s.value.is_none()) {
        dst.add_blocked_sample(s.t);
      } else if (auto pv = s.value.get_value<T>()) {
        dst.add_sample(s.t, pv.value());
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

// Storage type name trait - returns the USD type name for storage
// For most types this is the same as type_name, but for special types
// like Extent (stored as float3[]), this returns the actual storage type.
template<typename T>
struct storage_type_trait {
  static std::string type_name() { return value::TypeTraits<T>::type_name(); }
};

// Specialization for Extent - stored as float3[] in USD
template<>
struct storage_type_trait<Extent> {
  static std::string type_name() { return "float3[]"; }
};

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

  const Attribute &attr = prop.get_attribute();
  std::string attr_type_name = attr.type_name();

  // Check type match - also check storage type for types like Extent (stored
  // as float3[]) and role types whose underlying storage matches.
  const uint32_t target_underlying_type_id =
      value::TypeTraits<T>::underlying_type_id();
  const uint32_t attr_underlying_type_id =
      value::GetUnderlyingTypeId(attr_type_name);
  if ((value::TypeTraits<T>::type_name() != attr_type_name) &&
      (value::TypeTraits<T>::underlying_type_name() != attr_type_name) &&
      (storage_type_trait<T>::type_name() != attr_type_name) &&
      (target_underlying_type_id == value::TYPE_ID_INVALID ||
       attr_underlying_type_id == value::TYPE_ID_INVALID ||
       target_underlying_type_id != attr_underlying_type_id)) {
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
          // If `varying` wasn't explicitly authored, allow schema-default uniform.
          if (!(attr.variability() == Variability::Varying && !attr.is_varying_authored())) {
            ret.code = ParseResult::ResultCode::VariabilityMismatch;
            ret.err = fmt::format("Attribute `{}` must be `uniform` variability.", name);
            return ret;
          }
        }

        if (is_config_attr && attr.variability() != Variability::Uniform) {
          if (attr.is_varying_authored()) {
            ret.warn = fmt::format("Config attribute `{}` should have explicit `uniform` variability.", name);
          }
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

        // Parse default value and/or timeSamples using ConvertToAnimatable
        // This handles special type conversions (e.g., float3[] -> Extent)
        if (attr.get_var().has_default() || attr.get_var().has_timesamples()) {
          if (auto av = ConvertToAnimatable<T>(attr.get_var())) {
            animatable_value = std::move(av.value());
            has_default = attr.get_var().has_default();
            has_timesamples = attr.get_var().has_timesamples();
          } else {
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = fmt::format("ConvertToAnimatable<{}> failed for `{}`. Attribute has type {}",
                                  value::TypeTraits<T>::type_name(), prop_name, attr.get_var().type_name());
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

// Special case for Extent(float3[2]) type.
// Extent is stored as float3[] in USD but converted to Extent struct via
// ConvertToAnimatable<Extent> specialization. The storage_type_trait<Extent>
// handles the type matching.
static ParseResult ParseExtentAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedAttribute<Animatable<Extent>> &target) /* out */
{
  return ParseTypedAttributeUnified(table, prop_name, prop, name, target);
}


// CheckAllowedTokens template removed - now in enum-handlers.cc

// Allowed syntax:
//   "T varname"
template<typename T>
static ParseResult ParseShaderOutputTerminalAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  Property &prop,  // Non-const to allow move from metadata
  const std::string &name,
  TypedTerminalAttribute<T> &target, /* out */
  bool strict_type_check) // false: accept non-conformant authored type with a warning
{
  ParseResult ret;

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


      // Accept a non-conformant authored type for a shader output terminal:
      // keep the schema type `T`(the canonical type inferred from `info:id` /
      // the UsdPreviewSurface spec) for connection/render semantics, remember
      // the authored type, and report a warning. This matches OpenUSD, which
      // does not validate shader output types. Only used when
      // `strict_type_check` is false.
      auto accept_nonconformant = [&]() -> ParseResult {
        target.set_authored(true);
        target.set_actual_type_name(attr_type_name);
        target.metas() = prop.get_attribute().metas();
        table.insert(name);
        ret.warn = fmt::format(
            "Shader output `{}` is declared as type `{}` but `{}` is expected per "
            "the shader schema(UsdPreviewSurface / info:id). Accepting the declared "
            "type; the schema type is used for connection/render semantics(matches "
            "OpenUSD). Set strict_shader_type_check=true to make this an error.",
            name, attr_type_name, value::TypeTraits<T>::type_name());
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      };

      // First check if both types are same, then
      // Allow either type is role-types(e.g. allow color3f attribute for TypedTerminalAttribute<float3>)
      // NOTE: Both-role-types case (e.g. point3f for vector3f) is intentionally disallowed
      // to prevent semantic confusion between different role types.
      if (value::TypeTraits<T>::type_name() == attr_type_name) {
        DCOUT("Author output terminal attribute: " << name);
        target.set_authored(true);
        target.metas() = prop.get_attribute().metas();
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else if (value::TypeTraits<T>::is_role_type()) {
        if (attr_is_role_type) {
          if (!strict_type_check) { return accept_nonconformant(); }
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
            if (!strict_type_check) { return accept_nonconformant(); }
            ret.code = ParseResult::ResultCode::TypeMismatch;
            ret.err = fmt::format("Attribute type mismatch. {} expects type `{}`(and its underlying types) but defined as type `{}`.", name, value::TypeTraits<T>::type_name(), attr_type_name);
            return ret;
          }
        }
      } else if (attr_is_role_type) {
        if (value::TypeTraits<T>::is_role_type()) {
          if (!strict_type_check) { return accept_nonconformant(); }
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
            if (!strict_type_check) { return accept_nonconformant(); }
            ret.code = ParseResult::ResultCode::TypeMismatch;
            ret.err = fmt::format("Attribute type mismatch. {} expects type `{}` but defined as type `{}`(and its underlying types).", name, value::TypeTraits<T>::type_name(), attr_type_name);
            return ret;
          }
        }

      } else {
        DCOUT("attr.type = " << attr_type_name);
        if (!strict_type_check) { return accept_nonconformant(); }
        ret.code = ParseResult::ResultCode::TypeMismatch;
        ret.err = fmt::format("Property type mismatch. {} expects type `{}` but defined as type `{}`.", name, value::TypeTraits<T>::type_name(), attr_type_name);
        return ret;
      }
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}

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
  ParseResult ret = ParseShaderOutputTerminalAttribute(__table, __prop.first, __prop.second, __name, __target, options.strict_shader_type_check); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    if (!ret.warn.empty()) { PUSH_WARN(ret.warn); } \
    DCOUT("Added shader terminal attribute: " << __name); \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing shader output property `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

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

// Shader INPUT attribute parse with relaxed type-conformance.
// Same as PARSE_TYPED_ATTRIBUTE, but when the authored Sdf type does not match
// the canonical schema type, behavior depends on options.strict_shader_type_check:
//   true  -> hard error (same as PARSE_TYPED_ATTRIBUTE).
//   false -> warn and leave the property unconsumed so the trailing
//            ADD_PROPERTY captures it as a custom property (no data loss). The
//            typed field stays unauthored; the canonical schema type is kept.
// Intended for UsdPreviewSurface-family shader inputs. Requires `options`
// (PrimReconstructOptions) and ADD_PROPERTY at the end of the parse loop.
#define PARSE_SHADER_INPUT_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    if (!ret.warn.empty()) { PUSH_WARN(ret.warn); } \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else if ((ret.code == ParseResult::ResultCode::TypeMismatch) && !options.strict_shader_type_check) { \
    PUSH_WARN(fmt::format("Shader input `{}` declared with a non-conformant type: {} Accepting; preserving the authored value as a custom property. Set strict_shader_type_check=true to make this an error.", __name, ret.err)); \
    /* do not consume: fall through so the trailing ADD_PROPERTY captures it */ \
  } else { \
    PUSH_ERROR_AND_RETURN(fmt::format("Parsing shader input property `{}` failed. Error: {}", __name, ret.err)); \
  } \
}

#define PARSE_TYPED_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    if (!ret.warn.empty()) { \
      PUSH_WARN(ret.warn); \
    } \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::Unmatched) { \
    /* go next */ \
  } else if (ret.code == ParseResult::ResultCode::TypeMismatch) { \
    /* Sdf-level leniency (matches OpenUSD): preserve the authored attribute as \
       a custom property (trailing ADD_PROPERTY) instead of failing the prim. */ \
    PUSH_WARN(fmt::format("Attribute `{}` declared with a non-conformant type: {} Preserving the authored value as a custom property.", __name, ret.err)); \
    /* do NOT `continue`: fall through so the trailing ADD_PROPERTY captures it */ \
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


// Unified enum property parsing macro
// __parser_fn should be ParseUniformEnumProperty or ParseTimeSampledEnumProperty
#define PARSE_ENUM_PROPERTY_IMPL(__table, __prop, __name, __enum_ty, __enum_handler, __klass, \
                                 __target, __strict_check, __parser_fn) {        \
  if (__prop.first == __name) {                                                  \
    if (__table.count(__name)) { continue; }                                     \
    const Attribute &attr = __prop.second.get_attribute();                       \
    if ((__prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) && \
        __prop.second.is_attribute() && __prop.second.is_empty()) {              \
      DCOUT("No value assigned to `" << __name << "` token attribute. Set default token value."); \
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
        DCOUT("No value assigned to `" << __name << "` token attribute. Set default token value."); \
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

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
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
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static bool ReconstructGPrimProperties(
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

} // anonymous namespace

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
