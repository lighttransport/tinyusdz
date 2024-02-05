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
    (*err) += msg;     \
  }


namespace {

bool ToTerminalAttributeValue(
    const Attribute &attr, TerminalAttributeValue *value, std::string *err,
    const double t, const value::TimeSampleInterpolationType tinterp) {
  if (!value) {
    // ???
    return false;
  }

  if (attr.is_blocked()) {
    PUSH_ERROR_AND_RETURN("Attribute is None(Value Blocked).");
  }

  const primvar::PrimVar &var = attr.get_var();

  value->meta() = attr.metas();
  value->variability() = attr.variability();

  if (!var.is_valid()) {
    PUSH_ERROR_AND_RETURN("[InternalError] Attribute is invalid.");
  } else if (var.is_scalar()) {
    const value::Value &v = var.value_raw();
    DCOUT("Attribute is scalar type:" << v.type_name());
    DCOUT("Attribute value = " << pprint_value(v));

    value->set_value(v);
  } else if (var.is_timesamples()) {
    value::Value v;
    if (!var.get_interpolated_value(t, tinterp, &v)) {
      PUSH_ERROR_AND_RETURN("Interpolate TimeSamples failed.");
      return false;
    }

    value->set_value(v);
  }

  return true;
}

//
// visited_paths : To prevent circular referencing of attribute connection.
//
bool EvaluateAttributeImpl(
    const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, std::set<std::string> &visited_paths, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {
  // TODO:
  (void)tinterp;

  DCOUT("Prim : " << prim.element_path().element_name() << "("
                  << prim.type_name() << ") attr_name " << attr_name);

  Property prop;
  if (!GetProperty(prim, attr_name, &prop, err)) {
    return false;
  }

  if (prop.is_connection()) {
    // Follow connection target Path(singple targetPath only).
    std::vector<Path> pv = prop.get_attribute().connections();
    if (pv.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("Connection targetPath is empty for Attribute {}.", attr_name));
    }

    if (pv.size() > 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Multiple targetPaths assigned to .connection."));
    }

    auto target = pv[0];

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connetion
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (visited_paths.count(abs_path)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }
      visited_paths.insert(abs_path);

      return EvaluateAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                   value, err, visited_paths, t, tinterp);

    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
    }
  } else if (prop.is_relationship()) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Property `{}` is a Relation.", attr_name));
  } else if (prop.is_empty()) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "Attribute `{}` is a define-only attribute(no value assigned).",
        attr_name));
  } else if (prop.is_attribute()) {
    DCOUT("IsAttrib");

    const Attribute &attr = prop.get_attribute();

    if (attr.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
    }

    if (!ToTerminalAttributeValue(attr, value, err, t, tinterp)) {
      return false;
    }

  } else {
    // ???
    PUSH_ERROR_AND_RETURN(
        fmt::format("[InternalError] Invalid Attribute `{}`.", attr_name));
  }

  return true;
}

}  // namespace

bool EvaluateAttribute(
    const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {
  std::set<std::string> visited_paths;

  return EvaluateAttributeImpl(stage, prim, attr_name, value, err,
                               visited_paths, t, tinterp);
}

//
// visited_paths : To prevent circular referencing of attribute connection.
//
template<typename T>
bool EvaluateTypedAttributeImpl(
    const tinyusdz::Stage &stage, const TypedAttribute<T> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err, std::set<std::string> &visited_paths,
    const double t, const value::TimeSampleInterpolationType tinterp)
{

  if (attr.is_connection()) {
    // Follow connection target Path(singple targetPath only).
    std::vector<Path> pv = attr.connections();
    if (pv.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("Connection targetPath is empty for Attribute {}.", attr_name));
    }

    if (pv.size() > 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Multiple targetPaths assigned to .connection for Attribute {}.", attr_name));
    }

    auto target = pv[0];

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connetion
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (visited_paths.count(abs_path)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }
      visited_paths.insert(abs_path);

      TerminalAttributeValue attr_value;

      bool ret = EvaluateAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                   &attr_value, err, visited_paths, t, tinterp);

      if (!ret) {
        return false;
      }

      if (const auto pav = attr_value.as<T>()) {
        (*value) = (*pav);
        return true;
      } else {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Attribute of Connection targetPath has different type `{}. Expected `{}`. Attribute `{}`.", attr_value.type_name(), value::TypeTraits<T>::type_name(), attr_name));
      }


    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
    }
  } else if (attr.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
  } else {

    return attr.get_value(value);

  }

  return false;
}

template<typename T>
bool EvaluateTypedAttributeImpl(
    const tinyusdz::Stage &stage, const TypedAttributeWithFallback<T> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err, std::set<std::string> &visited_paths,
    const double t, const value::TimeSampleInterpolationType tinterp)
{

  if (attr.is_connection()) {
    // Follow connection target Path(singple targetPath only).
    std::vector<Path> pv = attr.connections();
    if (pv.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("Connection targetPath is empty for Attribute {}.", attr_name));
    }

    if (pv.size() > 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Multiple targetPaths assigned to .connection for Attribute {}.", attr_name));
    }

    auto target = pv[0];

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connetion
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (visited_paths.count(abs_path)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }
      visited_paths.insert(abs_path);

      TerminalAttributeValue attr_value;

      bool ret = EvaluateAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                   &attr_value, err, visited_paths, t, tinterp);

      if (!ret) {
        return false;
      }

      if (const auto pav = attr_value.as<T>()) {
        (*value) = (*pav);
        return true;
      } else {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Attribute of Connection targetPath has different type `{}. Expected `{}`. Attribute `{}`.", attr_value.type_name(), value::TypeTraits<T>::type_name(), attr_name));
      }


    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
    }
  } else if (attr.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
  } else {

    (*value) = attr.get_value();
    return true;

  }

  return false;
}

template<typename T>
bool EvaluateTypedAttributeImpl(
    const tinyusdz::Stage &stage, const TypedAttribute<Animatable<T>> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err, std::set<std::string> &visited_paths,
    const double t, const value::TimeSampleInterpolationType tinterp)
{

  if (attr.is_connection()) {
    // Follow connection target Path(singple targetPath only).
    std::vector<Path> pv = attr.connections();
    if (pv.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("Connection targetPath is empty for Attribute {}.", attr_name));
    }

    if (pv.size() > 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Multiple targetPaths assigned to .connection for Attribute {}.", attr_name));
    }

    auto target = pv[0];

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connetion
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (visited_paths.count(abs_path)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }
      visited_paths.insert(abs_path);

      TerminalAttributeValue attr_value;

      bool ret = EvaluateAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                   &attr_value, err, visited_paths, t, tinterp);

      if (!ret) {
        return false;
      }

      if (const auto pav = attr_value.as<T>()) {
        (*value) = (*pav);
        return true;
      } else {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Attribute of Connection targetPath has different type `{}. Expected `{}`. Attribute `{}`.", attr_value.type_name(), value::TypeTraits<T>::type_name(), attr_name));
      }


    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
    }
  } else if (attr.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
  } else {

    Animatable<T> v;
    if (!attr.get_value(&v)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("[Internal error] failed to get value.\n"));
    }

    if (!v.get(t, value, tinterp)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("[Internal error] failed to get value.\n"));
    }

  }

  return false;
}

template<typename T>
bool EvaluateTypedAttributeImpl(
    const tinyusdz::Stage &stage, const TypedAttributeWithFallback<Animatable<T>> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err, std::set<std::string> &visited_paths,
    const double t, const value::TimeSampleInterpolationType tinterp)
{

  if (attr.is_connection()) {
    // Follow connection target Path(singple targetPath only).
    std::vector<Path> pv = attr.connections();
    if (pv.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("Connection targetPath is empty for Attribute {}.", attr_name));
    }

    if (pv.size() > 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Multiple targetPaths assigned to .connection for Attribute {}.", attr_name));
    }

    auto target = pv[0];

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connetion
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (visited_paths.count(abs_path)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }
      visited_paths.insert(abs_path);

      TerminalAttributeValue attr_value;

      bool ret = EvaluateAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                   &attr_value, err, visited_paths, t, tinterp);

      if (!ret) {
        return false;
      }

      if (const auto pav = attr_value.as<T>()) {
        (*value) = (*pav);
        return true;
      } else {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Attribute of Connection targetPath has different type `{}. Expected `{}`. Attribute `{}`.", attr_value.type_name(), value::TypeTraits<T>::type_name(), attr_name));
      }


    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
    }
  } else if (attr.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
  } else {

    const Animatable<T> &v = attr.get_value();

    if (!v.get(t, value, tinterp)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("[Internal error] failed to get value.\n"));
    }

  }

  return false;
}


template<typename T>
bool EvaluateTypedAttribute(
    const tinyusdz::Stage &stage, const TypedAttribute<T> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err) {

  std::set<std::string> visited_paths;

  return EvaluateTypedAttributeImpl(stage, attr, attr_name, value, err,
                               visited_paths, value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);
}

// template instanciations
#define EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE(__ty) \
template bool EvaluateTypedAttribute(const tinyusdz::Stage &stage, const TypedAttribute<__ty> &attr, const std::string &attr_name, __ty *value, std::string *err);

APPLY_FUNC_TO_VALUE_TYPES(EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE)

#undef EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE


template<class T>
bool EvaluateTypedAnimatableAttribute(
    const tinyusdz::Stage &stage, const TypedAttribute<Animatable<T>> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {

  std::set<std::string> visited_paths;

  return EvaluateTypedAttributeImpl(stage, attr, attr_name, value, err,
                               visited_paths, t, tinterp);

}

#define EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE(__ty) \
template bool EvaluateTypedAnimatableAttribute(const tinyusdz::Stage &stage, const TypedAttribute<Animatable<__ty>> &attr, const std::string &attr_name, __ty *value, std::string *err, const double t, const tinyusdz::value::TimeSampleInterpolationType tinterp);

APPLY_FUNC_TO_VALUE_TYPES(EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE)

#undef EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE

template<typename T>
bool EvaluateTypedAttributeWithFallback(
    const tinyusdz::Stage &stage, const TypedAttributeWithFallback<T> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err) {

  std::set<std::string> visited_paths;

  return EvaluateTypedAttributeImpl(stage, attr, attr_name, value, err,
                               visited_paths, value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);
}

// template instanciations
#define EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE(__ty) \
template bool EvaluateTypedAttributeWithFallback(const tinyusdz::Stage &stage, const TypedAttributeWithFallback<__ty> &attr, const std::string &attr_name, __ty *value, std::string *err);

APPLY_FUNC_TO_VALUE_TYPES(EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE)

#undef EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE


template<class T>
bool EvaluateTypedAnimatableAttributeWithFallback(
    const tinyusdz::Stage &stage, const TypedAttributeWithFallback<Animatable<T>> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {

  std::set<std::string> visited_paths;

  return EvaluateTypedAttributeImpl(stage, attr, attr_name, value, err,
                               visited_paths, t, tinterp);

}

#define EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE(__ty) \
template bool EvaluateTypedAnimatableAttributeWithFallback(const tinyusdz::Stage &stage, const TypedAttributeWithFallback<Animatable<__ty>> &attr, const std::string &attr_name, __ty *value, std::string *err, const double t, const tinyusdz::value::TimeSampleInterpolationType tinterp);

APPLY_FUNC_TO_VALUE_TYPES(EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE)

#undef EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE


}  // namespace tydra
}  // namespace tinyusdz
