// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
#include "attribute-eval-internal.hh"

namespace tinyusdz {
namespace tydra {

// For PUSH_ERROR_AND_RETURN
#define PushError(msg) \
  if (err) {           \
    (*err) +=  msg;     \
  }


template<typename T>
bool EvaluateTypedAttributeImpl(
    const tinyusdz::Stage &stage, const TypedAttribute<Animatable<T>> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err,
    const double t, const value::TimeSampleInterpolationType tinterp)
{

  if (attr.has_value()) {

    return attr.get_value(value);

  } else if (attr.has_connection()) {
    // Follow connection target Path(single targetPath only).
    std::vector<Path> pv = attr.connections();
    Path target;
    if (!detail::ResolveSingleConnectionTargetPath(pv, attr_name, &target,
                                                   err)) {
      return false;
    }

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connection
      const Prim *targetPrim = targetPrimRet.value();

      TerminalAttributeValue attr_value;

      bool ret = EvaluateAttribute(stage, *targetPrim, targetPrimPropName,
                                   &attr_value, err, t, tinterp);

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
      return false;
    }
  } else if (attr.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
  } else {

    PUSH_ERROR_AND_RETURN("Internal error. Invalid TypedAttribute<Animatable<T>> value.");

  }

  return false;
}


// std::string specialization — token coercion handled by detail::FollowConnection.
template<> bool EvaluateTypedAnimatableAttribute<std::string>(
    const tinyusdz::Stage &stage, const TypedAttribute<Animatable<std::string>> &tattr,
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
  } else if (tattr.is_connection()) {

    Attribute attr = detail::ToAttributeConnection<
        TypedAttribute<Animatable<std::string>>, Variability::Varying,
        /* copyMeta */ true>(tattr, value::TypeTraits<std::string>::type_name());

    return detail::FollowConnection(
        stage, attr, attr_name, value_out, err, t, tinterp);

  } else {
    Animatable<std::string> value;
    if (tattr.get_value(&value)) {
      if (value.get(t, value_out, tinterp)) {
        return true;
      } else {
        if (err) {
          (*err) += fmt::format("Failed to get TypedAnimatableAttribute value: {} \n", attr_name);
        }
        return false;
      }
    }

    if (err) {
      (*err) += fmt::format("[Internal error] Invalid TypedAttribute? : {} \n", attr_name);
    }
  }
  return false;
}

template<typename T>
bool EvaluateTypedAnimatableAttribute(
    const tinyusdz::Stage &stage, const TypedAttribute<Animatable<T>> &tattr,
    const std::string &attr_name,
    T *value_out,
    std::string *err,
    const double t,
    const value::TimeSampleInterpolationType tinterp) {

  if (!value_out) {
    PUSH_ERROR_AND_RETURN("`value_out` param is nullptr.");
  }

  // Eval order:
  // - ValueBlocked?
  // - has value?(default value or timesampled value)
  // - has connection?

  if (tattr.is_blocked()) {
    if (err) {
      (*err) += "Attribute is Blocked.\n";
    }
    return false;
  } else if (tattr.has_value()) {
    Animatable<T> value;
    if (tattr.get_value(&value)) {
      if (value.get(t, value_out, tinterp)) {
        return true;
      } else {
        if (err) {
          (*err) += fmt::format("Failed to get TypedAnimatableAttribute value: {} \n", attr_name);
        }
        return false;
      }
    }
  } else if (tattr.has_connections()) {

    Attribute attr = detail::ToAttributeConnection<
        TypedAttribute<Animatable<T>>, Variability::Varying,
        /* copyMeta */ true>(tattr, value::TypeTraits<T>::type_name());

    return detail::FollowConnection(
        stage, attr, attr_name, value_out, err, t, tinterp);

  } else if (tattr.is_value_empty()) {
    if (err) {
      (*err) += "Attribute value is empty.\n";
    }
    return false;
  } else {
    if (err) {
      (*err) += fmt::format("[Internal error] Invalid TypedAttribute? : {} \n", attr_name);
    }
  }
  return false;
}


// template instantiations
#define EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE(__ty) \
template bool EvaluateTypedAnimatableAttribute(const tinyusdz::Stage &stage, const TypedAttribute<Animatable<__ty>> &attr, const std::string &attr_name, __ty *value, std::string *err, const double t, const value::TimeSampleInterpolationType tinterp);

APPLY_FUNC_TO_VALUE_TYPES_NO_STRING(EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE)

#undef EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE



}  // namespace tydra
}  // namespace tinyusdz
