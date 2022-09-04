#include "prim-reconstruct.hh"

#include "prim-types.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "tiny-format.hh"

#include "usdGeom.hh"
#include "usdSkel.hh"
#include "usdLux.hh"
#include "usdShade.hh"

#include "common-macros.inc"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) if (err) { (*err) += s; }
#define PushWarn(s) if (warn) { (*warn) += s; }


namespace tinyusdz {
namespace prim {

constexpr auto kMaterialBinding = "material:binding";

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
    InternalError,
  };

  ResultCode code;
  std::string err;
};

template<typename T>
static nonstd::optional<Animatable<T>> ConvertToAnimatable(const primvar::PrimVar &var)
{
  Animatable<T> dst;

  if (!var.is_valid()) {
    DCOUT("is_valid failed");
    return nonstd::nullopt;
  }

  if (var.is_scalar()) {

    if (auto pv = var.get_value<T>()) {
      dst.value = pv.value();
      dst.blocked = false;

      return std::move(dst);
    }
  } else if (var.is_timesample()) {
    for (size_t i = 0; i < var.var.times.size(); i++) {
      double t = var.var.times[i];

      // Attribute Block?
      if (auto pvb = var.get_ts_value<value::ValueBlock>(i)) {
        dst.ts.AddBlockedSample(t);
      } else if (auto pv = var.get_ts_value<T>(i)) {
        dst.ts.AddSample(t, pv.value());
      } else {
        // Type mismatch
        DCOUT(i << "/" << var.var.times.size() << " type mismatch.");
        return nonstd::nullopt;
      }
    }

    return std::move(dst);
  }

  DCOUT("???");
  return nonstd::nullopt;
}

// For animatable attribute(`varying`)
template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttributeWithFallback<Animatable<T>> &target) /* out */
{
  ParseResult ret;

  if (prop_name.compare(name + ".connect") == 0) {
    ret.code = ParseResult::ResultCode::ConnectionNotAllowed;
    ret.err = fmt::format("Connection is not allowed for Attribute `{}`.", name);
    return ret;
  } else if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    const PrimAttrib &attr = prop.attrib;

    DCOUT("attrib.type = " << value::TypeTrait<T>::type_name() << ", attr.var.type= " << attr.var.type_name());

    // Type info is stored in Attribute::type_name
    if (value::TypeTrait<T>::type_name() == attr.var.type_name()) {
      if (prop.type == Property::Type::EmptyAttrib) {
        target.meta = attr.meta;
        table.insert(name);
      } else if (prop.type == Property::Type::Attrib) {

        DCOUT("Adding prop: " << name);

        if (attr.blocked) {
          // e.g. "float radius = None"
          target.SetBlock(true);
        } else if (attr.variability == Variability::Uniform) {
          // e.g. "float radius = 1.2"
          if (!attr.var.is_scalar()) {
            ret.code = ParseResult::ResultCode::VariabilityMismatch;
            ret.err = fmt::format("TimeSample value is assigned to `uniform` property `{}", name);
            return ret;
          }

          if (auto pv = attr.var.get_value<T>()) {
            target.set(pv.value());
          } else {
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = fmt::format("Failed to retrieve value with requested type.");
            return ret;
          }

        } else if (attr.var.is_timesample()) {
          // e.g. "float radius.timeSamples = {0: 1.2, 1: 2.3}"

          Animatable<T> anim;
          if (auto av = ConvertToAnimatable<T>(attr.var)) {
            anim = av.value();
            target.set(anim);
          } else {
            // Conversion failed.
            DCOUT("ConvertToAnimatable failed.");
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = "Converting Attribute data failed. Maybe TimeSamples have values with different types?";
            return ret;
          }
        }

        target.meta = attr.meta;
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        DCOUT("Invalid Property.type");
        ret.err = "Invalid Property type(internal error)";
        ret.code = ParseResult::ResultCode::InternalError;
        return ret;
      }
    } else {
      DCOUT("tyname = " << value::TypeTrait<T>::type_name() << ", attr.type = " << attr.var.type_name());
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << value::TypeTrait<T>::type_name()
              << "` but defined as type `" << attr.var.type_name() << "`";
      ret.err = ss.str();
      return ret;
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}

// For 'uniform' attribute
template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttributeWithFallback<T> &target) /* out */
{
  ParseResult ret;

  if (prop_name.compare(name + ".connect") == 0) {
    ret.code = ParseResult::ResultCode::ConnectionNotAllowed;
    ret.err = fmt::format("Connection is not allowed for Attribute `{}`.", name);
    return ret;
  } else if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    const PrimAttrib &attr = prop.attrib;

    DCOUT("attrib.type = " << value::TypeTrait<T>::type_name() << ", attr.var.type= " << attr.var.type_name());

    // Type info is stored in Attribute::type_name
    if (value::TypeTrait<T>::type_name() == attr.var.type_name()) {
      if (prop.type == Property::Type::EmptyAttrib) {
        target.meta = attr.meta;
        table.insert(name);
      } else if (prop.type == Property::Type::Attrib) {
        DCOUT("Adding prop: " << name);

        if (prop.attrib.variability != Variability::Uniform) {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = fmt::format("Attribute `{}` must be `uniform` variability.", name);
          return ret;
        }

        if (attr.blocked) {
          target.SetBlock(true);
        } else if (attr.var.is_scalar()) {
          if (auto pv = attr.var.get_value<T>()) {
            target.set(pv.value());
          } else {
            ret.code = ParseResult::ResultCode::VariabilityMismatch;
            ret.err = "Internal data corrupsed.";
            return ret;
          }
        } else {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = "TimeSample or corrupted value assigned to a property where `uniform` variability is set.";
          return ret;
        }

        target.meta = attr.meta;
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        DCOUT("Invalid Property.type");
        ret.err = "Invalid Property type(internal error)";
        ret.code = ParseResult::ResultCode::InternalError;
        return ret;
      }
    } else {
      DCOUT("tyname = " << value::TypeTrait<T>::type_name() << ", attr.type = " << attr.var.type_name());
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << value::TypeTrait<T>::type_name()
              << "` but defined as type `" << attr.var.type_name() << "`";
      ret.err = ss.str();
      return ret;
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}

// TODO: Unify code with TypedAttrib<Animatable<T>> variant
template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttribute<T> &target) /* out */
{
  ParseResult ret;

  if (prop_name.compare(name + ".connect") == 0) {
    ret.code = ParseResult::ResultCode::ConnectionNotAllowed;
    ret.err = fmt::format("Connection is not allowed for Attribute `{}`.", name);
    return ret;
  } else if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    const PrimAttrib &attr = prop.attrib;

    DCOUT("attrib.type = " << value::TypeTrait<T>::type_name() << ", attr.var.type= " << attr.var.type_name());

    // Type info is stored in Attribute::type_name
    if (value::TypeTrait<T>::type_name() == attr.var.type_name()) {
      if (prop.type == Property::Type::EmptyAttrib) {
        target.meta = attr.meta;
        table.insert(name);
      } else if (prop.type == Property::Type::Attrib) {

        DCOUT("Adding prop: " << name);

        if (prop.attrib.variability != Variability::Uniform) {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = fmt::format("Attribute `{}` must be `uniform` variability.", name);
          return ret;
        }

        if (attr.blocked) {
          target.SetBlock(true);
        } else if (attr.var.is_scalar()) {
          if (auto pv = attr.var.get_value<T>()) {
            target.set(pv.value());
          } else {
            ret.code = ParseResult::ResultCode::VariabilityMismatch;
            ret.err = "Internal data corrupsed.";
            return ret;
          }
        } else {
          ret.code = ParseResult::ResultCode::VariabilityMismatch;
          ret.err = "TimeSample or corrupted value assigned to a property where `uniform` variability is set.";
          return ret;
        }

        target.meta = attr.meta;
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        DCOUT("Invalid Property.type");
        ret.err = "Invalid Property type(internal error)";
        ret.code = ParseResult::ResultCode::InternalError;
        return ret;
      }
    } else {
      DCOUT("tyname = " << value::TypeTrait<T>::type_name() << ", attr.type = " << attr.var.type_name());
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << value::TypeTrait<T>::type_name()
              << "` but defined as type `" << attr.var.type_name() << "`";
      ret.err = ss.str();
      return ret;
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}


template<typename T>
static ParseResult ParseTypedProperty(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedProperty<T> &target) /* out */
{
  ParseResult ret;

  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (auto pv = prop.GetConnectionTarget()) {
      target.target = pv.value();
      target.variability = prop.attrib.variability;
      target.meta = prop.attrib.meta;
      table.insert(propname);
      ret.code = ParseResult::ResultCode::Success;
      return ret;
    }
  } else if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    const PrimAttrib &attr = prop.attrib;

    DCOUT("attrib.type = " << value::TypeTrait<T>::type_name() << ", attr.var.type= " << attr.var.type_name());


    // Type info is stored in Attribute::type_name
    if (value::TypeTrait<T>::type_name() == attr.var.type_name()) {
      if (prop.type == Property::Type::EmptyAttrib) {
        target.define_only = true;
        target.variability = attr.variability;
        target.meta = attr.meta;
        table.insert(name);
      } else if (prop.type == Property::Type::Attrib) {
        DCOUT("Adding prop: " << name);

        Animatable<T> anim;

        if (attr.blocked) {
          anim.blocked = true;
        } else {
          if (auto av = ConvertToAnimatable<T>(attr.var)) {
            anim = av.value();
          } else {
            // Conversion failed.
            DCOUT("ConvertToAnimatable failed.");
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = "Converting Attribute data failed. Maybe TimeSamples have values with different types?";
            return ret;
          }
        }

        target.value = anim;
        target.variability = attr.variability;
        target.meta = attr.meta;
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        DCOUT("Invalid Property.type");
        ret.err = "Invalid Property type(internal error)";
        ret.code = ParseResult::ResultCode::InternalError;
        return ret;
      }
    } else {
      DCOUT("tyname = " << value::TypeTrait<T>::type_name() << ", attr.type = " << attr.var.type_name());
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << value::TypeTrait<T>::type_name()
              << "` but defined as type `" << attr.var.type_name() << "`";
      ret.err = ss.str();
      return ret;
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}


// Empty allowedTokens = allow all
template <class E, size_t N>
static nonstd::expected<bool, std::string> CheckAllowedTokens(
    const std::array<std::pair<E, const char *>, N> &allowedTokens,
    const std::string &tok) {
  if (allowedTokens.empty()) {
    return true;
  }

  for (size_t i = 0; i < N; i++) {
    if (tok.compare(std::get<1>(allowedTokens[i])) == 0) {
      return true;
    }
  }

  std::vector<std::string> toks;
  for (size_t i = 0; i < N; i++) {
    toks.push_back(std::get<1>(allowedTokens[i]));
  }

  std::string s = join(", ", tinyusdz::quote(toks));

  return nonstd::make_unexpected("Allowed tokens are [" + s + "] but got " +
                                 quote(tok) + ".");
};

template <class E>
static nonstd::expected<bool, std::string> CheckAllowedTokens(
    const std::vector<std::pair<E, const char *>> &allowedTokens,
    const std::string &tok) {
  if (allowedTokens.empty()) {
    return true;
  }

  for (size_t i = 0; i < allowedTokens.size(); i++) {
    if (tok.compare(std::get<1>(allowedTokens[i])) == 0) {
      return true;
    }
  }

  std::vector<std::string> toks;
  for (size_t i = 0; i < allowedTokens.size(); i++) {
    toks.push_back(std::get<1>(allowedTokens[i]));
  }

  std::string s = join(", ", tinyusdz::quote(toks));

  return nonstd::make_unexpected("Allowed tokens are [" + s + "] but got " +
                                 quote(tok) + ".");
};

template <typename T>
nonstd::expected<T, std::string> EnumHandler(
    const std::string &prop_name, const std::string &tok,
    const std::vector<std::pair<T, const char *>> &enums) {
  auto ret = CheckAllowedTokens<T>(enums, tok);
  if (!ret) {
    return nonstd::make_unexpected(ret.error());
  }

  for (auto &item : enums) {
    if (tok == item.second) {
      return item.first;
    }
  }
  // Should never reach here, though.
  return nonstd::make_unexpected(
      quote(tok) + " is an invalid token for attribute `" + prop_name + "`");
}


} // namespace

#define PARSE_TYPED_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::TypeMismatch) { \
    PUSH_ERROR_AND_RETURN(                                                   \
        "(" << value::TypeTrait<__klass>::type_name()                        \
            << ") " << ret.err); \
  } else if (ret.code == ParseResult::ResultCode::InternalError) { \
    PUSH_ERROR_AND_RETURN("Internal error: " + ret.err); \
  } else { \
    /* go next */ \
  } \
}

template <typename EnumTy>
using EnumHandlerFun = std::function<nonstd::expected<EnumTy, std::string>(
    const std::string &)>;

// Animatable enum
template<typename T, typename EnumTy>
nonstd::expected<bool, std::string> ParseEnumProperty(
  const std::string &prop_name,
  EnumHandlerFun<EnumTy> enum_handler,
  const PrimAttrib &attr,
  TypedAttributeWithFallback<Animatable<T>> *result)
{
  if (!result) {
    return false;
  }

  if (attr.variability == Variability::Uniform) {
    if (attr.var.is_timesample()) {
      return nonstd::make_unexpected(fmt::format("Property `{}` is defined as `uniform` variability but TimeSample value is assigned.", prop_name));
    }

    if (auto tok = attr.var.get_value<value::token>()) {
      auto e = enum_handler(tok.value().str());
      if (e) {
        (*result) = e.value();
        return true;
      } else {
        return nonstd::make_unexpected(fmt::format("({}) {}", value::TypeTrait<T>::type_name(), e.error()));
      }
    } else {
      return nonstd::make_unexpected(fmt::format("Property `{}` must be type `token`, but got type `{}`", prop_name, attr.var.type_name()));
    }


  } else {
    // uniform or TimeSamples
    if (attr.var.is_scalar()) {

      if (auto tok = attr.var.get_value<value::token>()) {
        auto e = enum_handler(tok.value().str());
        if (e) {
          (*result) = e.value();
          return true;
        } else {
          return nonstd::make_unexpected(fmt::format("({}) {}", value::TypeTrait<T>::type_name(), e.error()));
        }
      } else {
        return nonstd::make_unexpected(fmt::format("Property `{}` must be type `token`, but got type `{}`", prop_name, attr.var.type_name()));
      }
    } else if (attr.var.is_timesample()) {
      size_t n = attr.var.num_timesamples();

      TypedTimeSamples<T> samples;

      for (size_t i = 0; i < n; i++) {

        double sample_time;

        if (auto pv = attr.var.get_ts_time(i)) {
          sample_time = pv.value();
        } else {
          // This should not happen.
          return nonstd::make_unexpected("Internal error.");
        }

        if (auto pv = attr.var.is_ts_value_blocked(i)) {
          if (pv.value() == true) {
            samples.AddBlockedSample(sample_time);
            continue;
          }
        } else {
          // This should not happen.
          return nonstd::make_unexpected("Internal error.");
        }

        if (auto tok = attr.var.get_ts_value<value::token>(i)) {
          auto e = enum_handler(tok.value().str());
          if (e) {
            samples.AddSample(sample_time, e.value());
          } else {
            return nonstd::make_unexpected(fmt::format("({}) {}", value::TypeTrait<T>::type_name(), e.error()));
          }
        } else {
          return nonstd::make_unexpected(fmt::format("Property `{}`'s TimeSample value must be type `token`, but got invalid type", prop_name));
        }
      }

      result->ts = samples;
      return true;

    } else {
      return nonstd::make_unexpected(fmt::format("Property `{}` has invalid value."));
    }

  }

  return false;
}


// Uniform enum

#define PARSE_ENUM_PROPETY(__table, __prop, __name, __enum_handler, __klass, \
                           __target)                                         \
  if (__prop.first == __name) {                                              \
    if (__table.count(__name)) { continue; } \
    const PrimAttrib &attr = __prop.second.attrib;                           \
    if (auto tok = attr.var.get_value<value::token>()) {                     \
      auto e = __enum_handler(tok.value().str());                            \
      if (e) {                                                               \
        __target = e.value();                                                \
        /* TODO: attr meta __target.meta = attr.meta;  */                    \
        __table.insert(__name);                                              \
      } else {                                                               \
        PUSH_ERROR_AND_RETURN("(" << value::TypeTrait<__klass>::type_name()  \
                                  << ") " << e.error());                     \
      }                                                                      \
    } else {                                                                 \
      PUSH_ERROR_AND_RETURN("(" << value::TypeTrait<__klass>::type_name()    \
                                << ") Property type mismatch. " << __name    \
                                << " must be type `token`, but got `"        \
                                << attr.var.type_name() << "`.");            \
    }                                                                        \
  } else

#define PARSE_TYPED_PROPERTY(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedProperty(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::TypeMismatch) { \
    PUSH_ERROR_AND_RETURN(                                                   \
        "(" << value::TypeTrait<__klass>::type_name()                        \
            << ") " << ret.err); \
  } else if (ret.code == ParseResult::ResultCode::InternalError) { \
    PUSH_ERROR_AND_RETURN("Internal error: " + ret.err); \
  } else { \
    /* go next */ \
  } \
}

// Add custom property(including property with "primvars" prefix)
// Please call this macro after listing up all predefined property using
// `PARSE_PROPERTY` and `PARSE_ENUM_PROPETY`
#define ADD_PROPERY(__table, __prop, __klass, __dst)         \
  /* Check if the property name is a predefined property */  \
  if (!__table.count(__prop.first)) {                        \
    DCOUT("custom property added: name = " << __prop.first); \
    __dst[__prop.first] = __prop.second;                     \
    __table.insert(__prop.first);                            \
  } else

// This code path should not be reached though.
#define PARSE_PROPERTY_END_MAKE_ERROR(__table, __prop)                      \
  if (!__table.count(__prop.first)) {                              \
    PUSH_ERROR_AND_RETURN("Unsupported/unimplemented property: " + \
                          __prop.first);                           \
  }

// This code path should not be reached though.
#define PARSE_PROPERTY_END_MAKE_WARN(__prop) \
  { PUSH_WARN("Unsupported/unimplemented property: " + __prop.first); }

bool ReconstructXformOpsFromProperties(
  std::set<std::string> &table, /* inout */
  const std::map<std::string, Property> &properties,
  std::vector<XformOp> *xformOps,
  std::string *err)
{

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


  // Lookup xform values from `xformOpOrder`
  // TODO: TimeSamples, Connection
  if (properties.count("xformOpOrder")) {
    // array of string
    auto prop = properties.at("xformOpOrder");

    if (prop.IsRel()) {
      PUSH_ERROR_AND_RETURN("Relation for `xformOpOrder` is not supported.");
    } else if (auto pv =
                   prop.attrib.var.get_value<std::vector<value::token>>()) {

      // 'uniform' check
      if (prop.attrib.variability != Variability::Uniform) {
        PUSH_ERROR_AND_RETURN("`xformOpOrder` must have `uniform` variability.");
      }

      for (size_t i = 0; i < pv.value().size(); i++) {
        const auto &item = pv.value()[i];

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

          op.op = XformOp::OpType::ResetXformStack;
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
        if (it->second.IsConnection()) {
          PUSH_ERROR_AND_RETURN(
              "Connection(.connect) of xformOp property is not yet supported: "
              "`" +
              tok + "`");
        }
        const PrimAttrib &attr = it->second.attrib;

        // Check `xformOp` namespace
        if (auto xfm = SplitXformOpToken(tok, kTransform)) {
          op.op = XformOp::OpType::Transform;
          op.suffix = xfm.value();  // may contain nested namespaces

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::matrix4d>()) {
            op.set_scalar(pvd.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:transform` must be type `matrix4d`, but got type `" +
                attr.var.type_name() + "`.");
          }

        } else if (auto tx = SplitXformOpToken(tok, kTranslate)) {
          op.op = XformOp::OpType::Translate;
          op.suffix = tx.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:translate` must be type `double3` or `float3`, but "
                "got type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto scale = SplitXformOpToken(tok, kScale)) {
          op.op = XformOp::OpType::Scale;
          op.suffix = scale.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:scale` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotX = SplitXformOpToken(tok, kRotateX)) {
          op.op = XformOp::OpType::RotateX;
          op.suffix = rotX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateX` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotY = SplitXformOpToken(tok, kRotateY)) {
          op.op = XformOp::OpType::RotateY;
          op.suffix = rotX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateY` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotZ = SplitXformOpToken(tok, kRotateZ)) {
          op.op = XformOp::OpType::RotateY;
          op.suffix = rotZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZ` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateXYZ = SplitXformOpToken(tok, kRotateXYZ)) {
          op.op = XformOp::OpType::RotateXYZ;
          op.suffix = rotateXYZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateXZY = SplitXformOpToken(tok, kRotateXZY)) {
          op.op = XformOp::OpType::RotateXZY;
          op.suffix = rotateXZY.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateYXZ = SplitXformOpToken(tok, kRotateYXZ)) {
          op.op = XformOp::OpType::RotateYXZ;
          op.suffix = rotateYXZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateYZX = SplitXformOpToken(tok, kRotateYZX)) {
          op.op = XformOp::OpType::RotateYZX;
          op.suffix = rotateYZX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateZXY = SplitXformOpToken(tok, kRotateZXY)) {
          op.op = XformOp::OpType::RotateZXY;
          op.suffix = rotateZXY.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateZYX = SplitXformOpToken(tok, kRotateZYX)) {
          op.op = XformOp::OpType::RotateZYX;
          op.suffix = rotateZYX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto orient = SplitXformOpToken(tok, kOrient)) {
          op.op = XformOp::OpType::Orient;
          op.suffix = orient.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::quatf>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::quatd>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "token for xformOpOrder must have namespace `xformOp:***`, or .");
        }

        xformOps->emplace_back(op);
        table.insert(tok);
      }

    } else {
      PUSH_ERROR_AND_RETURN(
          "`xformOpOrder` must be type `token[]` but got type `"
          << prop.attrib.var.type_name() << "`.");
    }
  }

  return true;
}


template <>
bool ReconstructPrim(
    const PropertyMap &properties,
    const ReferenceList &references,
    Xform *xform,
    std::string *warn,
    std::string *err) {

  (void)warn;

  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(table, properties, &xform->xformOps, err)) {
    return false;
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
    }
  }

  return true;
}

template <>
bool ReconstructPrim<Model>(
    const PropertyMap &properties,
    const ReferenceList &references,
    Model *model,
    std::string *warn,
    std::string *err) {
  DCOUT("Model(`def` with no type)");
  (void)references;
  (void)model;
  (void)err;

  std::set<std::string> table;
  for (const auto &prop : properties) {
    ADD_PROPERY(table, prop, Model, model->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Scope>(
    const PropertyMap &properties,
    const ReferenceList &references,
    Scope *scope,
    std::string *warn,
    std::string *err) {
  // `Scope` is just a namespace in scene graph(no node xform)

  (void)references;
  (void)scope;
  (void)err;

  DCOUT("Scope");
  std::set<std::string> table;
  for (const auto &prop : properties) {
    ADD_PROPERY(table, prop, Scope, scope->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool ReconstructPrim<SkelRoot>(
    const PropertyMap &properties,
    const ReferenceList &references,
    SkelRoot *root,
    std::string *warn,
    std::string *err) {

  (void)references;

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(table, properties, &root->xformOps, err)) {
    return false;
  }

  // SkelRoot is something like a grouping node, having 1 Skeleton and possibly?
  // multiple Prim hierarchy containing GeomMesh.
  // No specific properties for SkelRoot(AFAIK)

  // custom props only
  for (const auto &prop : properties) {
    ADD_PROPERY(table, prop, SkelRoot, root->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Skeleton>(
    const PropertyMap &properties,
    const ReferenceList &references,
    Skeleton *skel,
    std::string *warn,
    std::string *err) {

  (void)warn;
  (void)references;

  constexpr auto kSkelAnimationSource = "skel:animationSource";

  std::set<std::string> table;
  for (auto &prop : properties) {

    // SkelBindingAPI
    if (prop.first == kSkelAnimationSource) {

      // Must be relation of type Path.
      if (prop.second.IsRel() && prop.second.rel.IsPath()) {
        {
          const Relation &rel = prop.second.rel;
          if (rel.IsPath()) {
            DCOUT(kSkelAnimationSource);
            skel->animationSource = rel.targetPath;
            table.insert(kSkelAnimationSource);
          } else {
            PUSH_ERROR_AND_RETURN("`" << kSkelAnimationSource << "` target must be Path.");
          }
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "`" << kSkelAnimationSource << "` must be a Relation with Path target.");
      }
    }

    //

    PARSE_TYPED_PROPERTY(table, prop, "bindTransforms", Skeleton, skel->bindTransforms)
    PARSE_TYPED_PROPERTY(table, prop, "joints", Skeleton, skel->joints)
    PARSE_TYPED_PROPERTY(table, prop, "jointNames", Skeleton, skel->jointNames)
    PARSE_TYPED_PROPERTY(table, prop, "restTransforms", Skeleton, skel->restTransforms)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<SkelAnimation>(
    const PropertyMap &properties,
    const ReferenceList &references,
    SkelAnimation *skelanim,
    std::string *warn,
    std::string *err) {

  (void)warn;
  (void)references;
  std::set<std::string> table;
  for (auto &prop : properties) {
    PARSE_TYPED_PROPERTY(table, prop, "joints", SkelAnimation, skelanim->joints)
    PARSE_TYPED_PROPERTY(table, prop, "translations", SkelAnimation, skelanim->translations)
    PARSE_TYPED_PROPERTY(table, prop, "rotations", SkelAnimation, skelanim->rotations)
    PARSE_TYPED_PROPERTY(table, prop, "scales", SkelAnimation, skelanim->scales)
    PARSE_TYPED_PROPERTY(table, prop, "blendShapes", SkelAnimation, skelanim->blendShapes)
    PARSE_TYPED_PROPERTY(table, prop, "blendShapeWeights", SkelAnimation, skelanim->blendShapeWeights)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<BlendShape>(
    const PropertyMap &properties,
    const ReferenceList &references,
    BlendShape *bs,
    std::string *warn,
    std::string *err) {
  (void)warn;
  (void)references;

  constexpr auto kOffsets = "offsets";
  constexpr auto kNormalOffsets = "normalOffsets";
  constexpr auto kPointIndices = "pointIndices";

  std::set<std::string> table;
  for (auto &prop : properties) {
    PARSE_TYPED_PROPERTY(table, prop, kOffsets, BlendShape, bs->offsets)
    PARSE_TYPED_PROPERTY(table, prop, kNormalOffsets, BlendShape, bs->normalOffsets)
    PARSE_TYPED_PROPERTY(table, prop, kPointIndices, BlendShape, bs->pointIndices)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  // `offsets` and `normalOffsets` are required property.
  if (!table.count(kOffsets)) {
    PUSH_ERROR_AND_RETURN("`offsets` property is missing. `uniform vector3f[] offsets` is a required property.");
  }
  if (!table.count(kNormalOffsets)) {
    PUSH_ERROR_AND_RETURN("`normalOffsets` property is missing. `uniform vector3f[] normalOffsets` is a required property.");
  }

  return true;
}

template <>
bool ReconstructPrim(
    const PropertyMap &properties,
    const ReferenceList &references,
    GPrim *gprim,
    std::string *warn,
    std::string *err) {
  (void)gprim;
  (void)err;

  (void)references;
  (void)properties;

  PUSH_WARN("TODO: GPrim");

  return true;
}

template <>
bool ReconstructPrim(
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomBasisCurves *curves,
    std::string *warn,
    std::string *err) {
  (void)references;

  DCOUT("GeomBasisCurves");

  auto BasisHandler = [](const std::string &tok)
      -> nonstd::expected<GeomBasisCurves::Basis, std::string> {
    using EnumTy = std::pair<GeomBasisCurves::Basis, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomBasisCurves::Basis::Bezier, "bezier"),
        std::make_pair(GeomBasisCurves::Basis::Bspline, "bspline"),
        std::make_pair(GeomBasisCurves::Basis::CatmullRom, "catmullRom"),
    };

    return EnumHandler<GeomBasisCurves::Basis>("basis", tok, enums);
  };

  auto TypeHandler = [](const std::string &tok)
      -> nonstd::expected<GeomBasisCurves::Type, std::string> {
    using EnumTy = std::pair<GeomBasisCurves::Type, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomBasisCurves::Type::Cubic, "cubic"),
        std::make_pair(GeomBasisCurves::Type::Linear, "linear"),
    };

    return EnumHandler<GeomBasisCurves::Type>("type", tok, enums);
  };

  auto WrapHandler = [](const std::string &tok)
      -> nonstd::expected<GeomBasisCurves::Wrap, std::string> {
    using EnumTy = std::pair<GeomBasisCurves::Wrap, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomBasisCurves::Wrap::Nonperiodic, "nonperiodic"),
        std::make_pair(GeomBasisCurves::Wrap::Periodic, "periodic"),
        std::make_pair(GeomBasisCurves::Wrap::Pinned, "periodic"),
    };

    return EnumHandler<GeomBasisCurves::Wrap>("wrap", tok, enums);
  };

  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_PROPERTY(table, prop, "curveVertexCounts", GeomBasisCurves,
                         curves->curveVertexCounts)
    PARSE_TYPED_PROPERTY(table, prop, "points", GeomBasisCurves, curves->points)
    PARSE_TYPED_PROPERTY(table, prop, "velocities", GeomBasisCurves,
                          curves->velocities)
    PARSE_TYPED_PROPERTY(table, prop, "normals", GeomBasisCurves,
                  curves->normals)
    PARSE_TYPED_PROPERTY(table, prop, "accelerations", GeomBasisCurves,
                 curves->accelerations)
    PARSE_TYPED_PROPERTY(table, prop, "widths", GeomBasisCurves, curves->widths)
    PARSE_ENUM_PROPETY(table, prop, "type", TypeHandler, GeomBasisCurves,
                       curves->type)
    PARSE_ENUM_PROPETY(table, prop, "basis", BasisHandler, GeomBasisCurves,
                       curves->basis)
    PARSE_ENUM_PROPETY(table, prop, "wrap", WrapHandler, GeomBasisCurves,
                       curves->wrap)

    ADD_PROPERY(table, prop, GeomBasisCurves, curves->props);

    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool ReconstructPrim<LuxSphereLight>(
    const PropertyMap &properties,
    const ReferenceList &references,
    LuxSphereLight *light,
    std::string *warn,
    std::string *err) {

  (void)references;

  std::set<std::string> table;
  for (const auto &prop : properties) {
    // PARSE_PROPERTY(prop, "inputs:colorTemperature", light->colorTemperature)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:color", LuxSphereLight, light->color)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:radius", LuxSphereLight, light->radius)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:intensity", LuxSphereLight,
                   light->intensity)
    ADD_PROPERY(table, prop, LuxSphereLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool ReconstructPrim<LuxDomeLight>(
    const PropertyMap &properties,
    const ReferenceList &references,
    LuxDomeLight *light,
    std::string *warn,
    std::string *err) {

  (void)references;

  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_PROPERTY(table, prop, "guideRadius", LuxDomeLight, light->guideRadius)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:diffuse", LuxDomeLight, light->diffuse)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:specular", LuxDomeLight,
                   light->specular)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:colorTemperature", LuxDomeLight,
                   light->colorTemperature)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:color", LuxDomeLight, light->color)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:intensity", LuxDomeLight,
                   light->intensity)
    ADD_PROPERY(table, prop, LuxDomeLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  DCOUT("Implement DomeLight");
  return true;
}

template <>
bool ReconstructPrim<GeomSphere>(
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomSphere *sphere,
    std::string *warn,
    std::string *err) {

  (void)references;

  DCOUT("Reconstruct Sphere.");

#if 0 //  TODO
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
#if 0
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                sphere->radius = *p;
              }
            }
          }
#endif
        }
      }
    }
  }
#endif

  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomSphere, sphere->radius)
      ADD_PROPERY(table, prop, GeomSphere, sphere->props)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

#if 0 // TODO
  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
          // if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
          //   if (prop.first == "radius") {
          //     if (auto p = value::as_basic<double>(&attr->var)) {
          //       SDCOUT << "append reference radius = " << (*p) << "\n";
          //       sphere->radius = *p;
          //     }
          //   }
          // }
        }
      }
    }
  }
#endif

  return true;
}

template <>
bool ReconstructPrim<GeomCone>(
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCone *cone,
    std::string *warn,
    std::string *err) {

  (void)references;

  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            cone->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomCone, cone->radius)
      PARSE_TYPED_PROPERTY(table, prop, "height", GeomCone, cone->height)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

  return true;
}

template <>
bool ReconstructPrim<GeomCylinder>(
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCylinder *cylinder,
    std::string *warn,
    std::string *err) {

  (void)references;

  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            cylinder->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomCylinder,
                           cylinder->radius)
      PARSE_TYPED_PROPERTY(table, prop, "height", GeomCylinder,
                           cylinder->height)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

  return true;
}

template <>
bool ReconstructPrim<GeomCapsule>(
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCapsule *capsule,
    std::string *warn,
    std::string *err) {

  (void)references;

  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            capsule->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomCapsule, capsule->radius)
      PARSE_TYPED_PROPERTY(table, prop, "height", GeomCapsule, capsule->height)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

  return true;
}

template <>
bool ReconstructPrim<GeomCube>(
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCube *cube,
    std::string *warn,
    std::string *err) {

  (void)references;

  //
  // pxrUSD says... "If you author size you must also author extent."
  //
  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            cube->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "size", GeomCube, cube->size)
      ADD_PROPERY(table, prop, GeomCube, cube->props)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

  return true;
}

template <>
bool ReconstructPrim<GeomMesh>(
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomMesh *mesh,
    std::string *warn,
    std::string *err) {

  (void)references;

#if 0 // TODO
  //
  // Resolve prepend references
  //

  for (const auto &ref : references) {
    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      if (endsWith(asset_ref.asset_path, ".obj")) {
        std::string err;
        GPrim gprim;

        // abs path.
        std::string filepath = asset_ref.asset_path;

        if (io::IsAbsPath(asset_ref.asset_path)) {
          // do nothing
        } else {
          if (!_base_dir.empty()) {
            filepath = io::JoinPath(_base_dir, filepath);
          }
        }

        DCOUT("Reading .obj file: " + filepath);

        if (!usdObj::ReadObjFromFile(filepath, &gprim, &err)) {
          PUSH_ERROR_AND_RETURN("Failed to read .obj(usdObj). err = " + err);
        }
        DCOUT("Loaded .obj file: " + filepath);

        mesh->visibility = gprim.visibility;
        mesh->doubleSided = gprim.doubleSided;
        mesh->orientation = gprim.orientation;

        if (gprim.props.count("points")) {
          DCOUT("points");
          const Property &prop = gprim.props.at("points");
          if (prop.IsRel()) {
            PUSH_WARN("TODO: points Rel\n");
          } else {
            const PrimAttrib &attr = prop.attrib;
            // PrimVar
            DCOUT("points.type:" + attr.var.type_name());
            if (attr.var.is_scalar()) {
              auto p = attr.var.get_value<std::vector<value::point3f>>();
              if (p) {
                mesh->points.value = p.value();
              } else {
                PUSH_ERROR_AND_RETURN("TODO: points.type = " +
                                      attr.var.type_name());
              }
              // if (auto p = value::as_vector<value::float3>(&pattr->var)) {
              //   DCOUT("points. sz = " + std::to_string(p->size()));
              //   mesh->points = (*p);
              // }
            } else {
              PUSH_ERROR_AND_RETURN("TODO: timesample points.");
            }
          }
        }

      } else {
        DCOUT("Not a .obj file");
      }
    }
  }
#endif

  auto SubdivisioSchemeHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::SubdivisionScheme, std::string> {
    using EnumTy = std::pair<GeomMesh::SubdivisionScheme, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::SubdivisionScheme::None, "none"),
        std::make_pair(GeomMesh::SubdivisionScheme::CatmullClark,
                       "catmullClark"),
        std::make_pair(GeomMesh::SubdivisionScheme::Loop, "loop"),
        std::make_pair(GeomMesh::SubdivisionScheme::Bilinear, "bilinear"),
    };
    return EnumHandler<GeomMesh::SubdivisionScheme>("subdivisionScheme", tok,
                                                    enums);
  };

  auto InterpolateBoundaryHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::InterpolateBoundary, std::string> {
    using EnumTy = std::pair<GeomMesh::InterpolateBoundary, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::InterpolateBoundary::None, "none"),
        std::make_pair(GeomMesh::InterpolateBoundary::EdgeAndCorner,
                       "edgeAndCorner"),
        std::make_pair(GeomMesh::InterpolateBoundary::EdgeOnly, "edgeOnly"),
    };
    return EnumHandler<GeomMesh::InterpolateBoundary>("interpolateBoundary",
                                                      tok, enums);
  };

  auto FacevaryingLinearInterpolationHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::FacevaryingLinearInterpolation,
                          std::string> {
    using EnumTy =
        std::pair<GeomMesh::FacevaryingLinearInterpolation, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::CornersPlus1,
                       "cornersPlus1"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::CornersPlus2,
                       "cornersPlus2"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::CornersOnly,
                       "cornersOnly"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::Boundaries,
                       "boundaries"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::None, "none"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::All, "all"),
    };
    return EnumHandler<GeomMesh::FacevaryingLinearInterpolation>(
        "facevaryingLinearInterpolation", tok, enums);
  };

  std::set<std::string> table;

  for (const auto &prop : properties) {
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        // Must be relation of type Path.
        if (prop.second.IsRel() && prop.second.IsEmpty()) {
          PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relation with Path target.", kMaterialBinding));
        }

        {
          const Relation &rel = prop.second.rel;
          if (rel.IsPath()) {
            DCOUT("materialBinding");
            MaterialBindingAPI m;
            m.binding = rel.targetPath;
            mesh->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(fmt::format("`{}` target must be Path.", kMaterialBinding));
          }
        }
      } else if (prop.first == "skel:skeleton") {
        // Must be relation of type Path.
        if (prop.second.IsRel() && prop.second.IsEmpty()) {
          PUSH_ERROR_AND_RETURN(
              "`skel:skeleton` must be a Relation with Path target.");
        }

        {
          const Relation &rel = prop.second.rel;
          if (rel.IsPath()) {
            DCOUT("skelBinding");
            mesh->skeleton = rel.targetPath;
          } else {
            PUSH_ERROR_AND_RETURN("`skel:skeleton` target must be Path.");
          }
        }

      } else {
        PUSH_WARN("TODO: rel : " << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "points", GeomMesh, mesh->points)
      PARSE_TYPED_PROPERTY(table, prop, "normals", GeomMesh, mesh->normals)
      PARSE_TYPED_PROPERTY(table, prop, "faceVertexCounts", GeomMesh,
                           mesh->faceVertexCounts)
      PARSE_TYPED_PROPERTY(table, prop, "faceVertexIndices", GeomMesh,
                           mesh->faceVertexIndices)
      // Subd
      PARSE_TYPED_PROPERTY(table, prop, "cornerIndices", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "cornerSharpnesses", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "creaseIndices", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "creaseLengths", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "creaseSharpnesses", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "holeIndices", GeomMesh,
                           mesh->cornerIndices)
      //
      PARSE_TYPED_ATTRIBUTE(table, prop, "doubleSided", GeomMesh, mesh->doubleSided)

      PARSE_ENUM_PROPETY(table, prop, "subdivisionScheme",
                         SubdivisioSchemeHandler, GeomMesh,
                         mesh->subdivisionScheme)
      PARSE_ENUM_PROPETY(table, prop, "interpolateBoundary",
                         InterpolateBoundaryHandler, GeomMesh,
                         mesh->interpolateBoundary)
      PARSE_ENUM_PROPETY(table, prop, "facevaryingLinearInterpolation",
                         FacevaryingLinearInterpolationHandler, GeomMesh,
                         mesh->facevaryingLinearInterpolation)
      ADD_PROPERY(table, prop, GeomMesh, mesh->props)
      PARSE_PROPERTY_END_MAKE_WARN(prop)
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      // TODO
    }
  }

  return true;
}


template <>
bool ReconstructPrim<GeomCamera>(
    const PropertyMap &properties,
    const ReferenceList &references,
    GeomCamera *camera,
    std::string *warn,
    std::string *err) {

  (void)references;
  (void)warn;

  auto ProjectionHandler = [](const std::string &tok)
      -> nonstd::expected<GeomCamera::Projection, std::string> {
    using EnumTy = std::pair<GeomCamera::Projection, const char *>;
    constexpr std::array<EnumTy, 2> enums = {
        std::make_pair(GeomCamera::Projection::Perspective, "perspective"),
        std::make_pair(GeomCamera::Projection::Orthographic, "orthographic"),
    };

    auto ret =
        CheckAllowedTokens<GeomCamera::Projection, enums.size()>(enums, tok);
    if (!ret) {
      return nonstd::make_unexpected(ret.error());
    }

    for (auto &item : enums) {
      if (tok == item.second) {
        return item.first;
      }
    }

    // Should never reach here, though.
    return nonstd::make_unexpected(
        quote(tok) + " is invalid token for `projection` propety");
  };

  auto StereoRoleHandler = [](const std::string &tok)
      -> nonstd::expected<GeomCamera::StereoRole, std::string> {
    using EnumTy = std::pair<GeomCamera::StereoRole, const char *>;
    constexpr std::array<EnumTy, 3> enums = {
        std::make_pair(GeomCamera::StereoRole::Mono, "mono"),
        std::make_pair(GeomCamera::StereoRole::Left, "left"),
        std::make_pair(GeomCamera::StereoRole::Right, "right"),
    };

    auto ret =
        CheckAllowedTokens<GeomCamera::StereoRole, enums.size()>(enums, tok);
    if (!ret) {
      return nonstd::make_unexpected(ret.error());
    }

    for (auto &item : enums) {
      if (tok == item.second) {
        return item.first;
      }
    }

    // Should never reach here, though.
    return nonstd::make_unexpected(
        quote(tok) + " is invalid token for `stereoRole` propety");
  };

  std::set<std::string> table;
  for (const auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "focalLength", GeomCamera, camera->focalLength)
    PARSE_TYPED_ATTRIBUTE(table, prop, "focusDistance", GeomCamera,
                   camera->focusDistance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "exposure", GeomCamera, camera->exposure)
    PARSE_TYPED_ATTRIBUTE(table, prop, "fStop", GeomCamera, camera->fStop)
    PARSE_TYPED_ATTRIBUTE(table, prop, "horizontalAperture", GeomCamera,
                   camera->horizontalAperture)
    PARSE_TYPED_ATTRIBUTE(table, prop, "horizontalApertureOffset", GeomCamera,
                   camera->horizontalApertureOffset)
    PARSE_TYPED_ATTRIBUTE(table, prop, "horizontalApertureOffset", GeomCamera,
                   camera->horizontalApertureOffset)
    PARSE_TYPED_ATTRIBUTE(table, prop, "clippingRange", GeomCamera,
                   camera->clippingRange)
    PARSE_TYPED_ATTRIBUTE(table, prop, "clippingPlanes", GeomCamera,
                   camera->clippingPlanes)
    PARSE_TYPED_ATTRIBUTE(table, prop, "shutter:open", GeomCamera, camera->shutterOpen)
    PARSE_TYPED_ATTRIBUTE(table, prop, "shutter:close", GeomCamera,
                   camera->shutterClose)
    PARSE_ENUM_PROPETY(table, prop, "projection", ProjectionHandler, GeomCamera,
                       camera->projection)
    PARSE_ENUM_PROPETY(table, prop, "stereoRole", StereoRoleHandler, GeomCamera,
                       camera->stereoRole)
    ADD_PROPERY(table, prop, GeomCamera, camera->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

} // namespace prim
} // namespace tinyusdz
