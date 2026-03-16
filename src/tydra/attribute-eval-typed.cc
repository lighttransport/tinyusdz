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


//
// visited_paths : To prevent circular referencing of attribute connection.
//
template<typename T>
bool EvaluateTypedAttributeImpl(
    const tinyusdz::Stage &stage, const TypedAttribute<T> &attr,
    const std::string &attr_name,
    T *value,
    std::string *err,
    const double t, const value::TimeSampleInterpolationType tinterp)
{

  if (attr.is_connection()) {
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

    return attr.get_value(value);

  }

  return false;
}


template<typename T>
bool EvaluateTypedAttribute(
    const tinyusdz::Stage &stage, const TypedAttribute<T> &tattr,
    const std::string &attr_name,
    T *value_out,
    std::string *err) {

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
        TypedAttribute<T>, Variability::Uniform>(tattr);

    return detail::FollowConnection(
        stage, attr, attr_name, value_out, err,
        value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);

  } else {
    if (tattr.get_value(value_out)) {
      return true;
    }

    if (err) {
      (*err) += fmt::format("[Internal error] Invalid TypedAttribute? : {} \n", attr_name);
    }
  }
  return false;
}

// std::string specialization — token coercion is handled inside
// detail::FollowConnection.
template<typename T>
bool EvaluateTypedAttribute(
    const tinyusdz::Stage &stage, const TypedAttribute<std::string> &tattr,
    const std::string &attr_name,
    std::string *value_out,
    std::string *err) {

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
        TypedAttribute<std::string>, Variability::Uniform>(tattr);

    return detail::FollowConnection(
        stage, attr, attr_name, value_out, err,
        value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);

  } else {
    if (tattr.get_value(value_out)) {
      return true;
    }

    if (err) {
      (*err) += fmt::format("[Internal error] Invalid TypedAttribute? : {} \n", attr_name);
    }
  }
  return false;
}

// template instantiations
#define EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE(__ty) \
template bool EvaluateTypedAttribute(const tinyusdz::Stage &stage, const TypedAttribute<__ty> &attr, const std::string &attr_name, __ty *value, std::string *err);

APPLY_FUNC_TO_VALUE_TYPES_NO_STRING(EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE)

#undef EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE




}  // namespace tydra
}  // namespace tinyusdz
