#include "prim-reconstruct.hh"

#include "prim-types.hh"
#include "str-util.hh"

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

namespace {

struct ParseResult
{
  enum class ResultCode
  {
    Success,
    Unmatched,
    AlreadyProcessed,
    TypeMismatch,
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

template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttribute<T> &target) /* out */
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
#if 0
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
#endif

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

    PARSE_TYPED_ATTRIBUTE(table, prop, "bindTransforms", Skeleton, skel->bindTransforms)
    PARSE_TYPED_ATTRIBUTE(table, prop, "joints", Skeleton, skel->joints)
    PARSE_TYPED_ATTRIBUTE(table, prop, "jointNames", Skeleton, skel->jointNames)
    PARSE_TYPED_ATTRIBUTE(table, prop, "restTransforms", Skeleton, skel->restTransforms)
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
    PARSE_TYPED_ATTRIBUTE(table, prop, "joints", SkelAnimation, skelanim->joints)
    PARSE_TYPED_ATTRIBUTE(table, prop, "translations", SkelAnimation, skelanim->translations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "rotations", SkelAnimation, skelanim->rotations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "scales", SkelAnimation, skelanim->scales)
    PARSE_TYPED_ATTRIBUTE(table, prop, "blendShapes", SkelAnimation, skelanim->blendShapes)
    PARSE_TYPED_ATTRIBUTE(table, prop, "blendShapeWeights", SkelAnimation, skelanim->blendShapeWeights)
    //PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
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
    PARSE_TYPED_ATTRIBUTE(table, prop, kOffsets, BlendShape, bs->offsets)
    PARSE_TYPED_ATTRIBUTE(table, prop, kNormalOffsets, BlendShape, bs->normalOffsets)
    PARSE_TYPED_ATTRIBUTE(table, prop, kPointIndices, BlendShape, bs->pointIndices)
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
    PARSE_TYPED_ATTRIBUTE(table, prop, "curveVertexCounts", GeomBasisCurves,
                         curves->curveVertexCounts)
    PARSE_TYPED_ATTRIBUTE(table, prop, "points", GeomBasisCurves, curves->points)
    PARSE_TYPED_ATTRIBUTE(table, prop, "velocities", GeomBasisCurves,
                          curves->velocities)
    PARSE_TYPED_ATTRIBUTE(table, prop, "normals", GeomBasisCurves,
                  curves->normals)
    PARSE_TYPED_ATTRIBUTE(table, prop, "accelerations", GeomBasisCurves,
                 curves->accelerations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "widths", GeomBasisCurves, curves->widths)
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
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:color", LuxSphereLight, light->color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:radius", LuxSphereLight, light->radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:intensity", LuxSphereLight,
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
    PARSE_TYPED_ATTRIBUTE(table, prop, "guideRadius", LuxDomeLight, light->guideRadius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:diffuse", LuxDomeLight, light->diffuse)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular", LuxDomeLight,
                   light->specular)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:colorTemperature", LuxDomeLight,
                   light->colorTemperature)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:color", LuxDomeLight, light->color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:intensity", LuxDomeLight,
                   light->intensity)
    ADD_PROPERY(table, prop, LuxDomeLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  DCOUT("Implement DomeLight");
  return true;
}

} // namespace prim
} // namespace tinyusdz
