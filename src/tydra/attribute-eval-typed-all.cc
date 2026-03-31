// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
// Consolidated typed attribute evaluation.
// Merges the former attribute-eval-typed.cc, attribute-eval-typed-animatable.cc,
// attribute-eval-typed-fallback.cc, and attribute-eval-typed-animatable-fallback.cc.
//
#include "attribute-eval-internal.hh"

namespace tinyusdz {
namespace tydra {

// For PUSH_ERROR_AND_RETURN
#define PushError(msg) \
  if (err) {           \
    (*err) +=  msg;     \
  }

// -----------------------------------------------------------------------
// TypedAttribute<T>
// -----------------------------------------------------------------------

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
template<>
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

// -----------------------------------------------------------------------
// TypedAttribute<Animatable<T>>
// -----------------------------------------------------------------------

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

// -----------------------------------------------------------------------
// TypedAttributeWithFallback<T>
// -----------------------------------------------------------------------

template<typename T>
bool EvaluateTypedAttribute(
    const tinyusdz::Stage &stage, const TypedAttributeWithFallback<T> &tattr,
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
  } else if (tattr.has_value()) {
    (*value_out) = tattr.get_value();
    return true;
  } else if (tattr.has_connections()) {

    Attribute attr = detail::ToAttributeConnection<
        TypedAttributeWithFallback<T>, Variability::Uniform>(tattr);

    return detail::FollowConnection(
        stage, attr, attr_name, value_out, err,
        value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);

  } else if (tattr.is_value_empty()) {
    if (err) {
      (*err) += "Attribute value is empty.\n";
    }
    return false;
  }

  PUSH_ERROR_AND_RETURN(fmt::format("Internal error. Attribute {} has invalid form of TypedAttributeWithFallback<{}>.",
     attr_name, value::TypeTraits<T>::type_name()));

}

// std::string specialization — token coercion handled by detail::FollowConnection.
template<>
bool EvaluateTypedAttribute(
    const tinyusdz::Stage &stage, const TypedAttributeWithFallback<std::string> &tattr,
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
  } else if (tattr.is_connection()) {

    Attribute attr = detail::ToAttributeConnection<
        TypedAttributeWithFallback<std::string>, Variability::Uniform>(tattr);

    return detail::FollowConnection(
        stage, attr, attr_name, value_out, err,
        value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);

  } else if (tattr.is_value_empty()) {
    if (err) {
      (*err) += "Attribute value is empty.\n";
    }
    return false;
  } else {
    (*value_out) = tattr.get_value();
    return true;
  }
}

// -----------------------------------------------------------------------
// TypedAttributeWithFallback<Animatable<T>>
// -----------------------------------------------------------------------

template<typename T>
bool EvaluateTypedAnimatableAttribute(
    const tinyusdz::Stage &stage, const TypedAttributeWithFallback<Animatable<T>> &tattr,
    const std::string &attr_name,
    T *value_out,
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
  } else if (tattr.has_value()) {
    const Animatable<T> &value = tattr.get_value();
    T v;
    if (value.get(t, &v, tinterp)) {
      *value_out = v;
      return true;
    } else {
      if (err) {
        (*err) += fmt::format("Failed to get TypedAnimatableAttribute value: {} \n", attr_name);
      }
      return false;
    }
  } else if (tattr.has_connections()) {

    Attribute attr = detail::ToAttributeConnection<
        TypedAttributeWithFallback<Animatable<T>>, Variability::Varying>(
            tattr, value::TypeTraits<T>::type_name());

    return detail::FollowConnection(
        stage, attr, attr_name, value_out, err,
        value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);

  } else if (tattr.is_value_empty()) {
    if (err) {
      (*err) += "Attribute value is empty.\n";
    }
    return false;
  } else {
    if (err) {
      (*err) += fmt::format("Unsupported/Invalid TypedAnimatableAttribute value: {}", attr_name);
    }
  }
  return false;
}

// std::string specialization — token coercion handled by detail::FollowConnection.
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
  } else if (tattr.has_value()) {
    const Animatable<std::string> &value = tattr.get_value();
    std::string v;
    if (value.get(t, &v, tinterp)) {
      *value_out = v;
      return true;
    } else {
      if (err) {
        (*err) += fmt::format("Failed to get TypedAnimatableAttribute value: {} \n", attr_name);
      }
      return false;
    }

  } else if (tattr.has_connections()) {

    Attribute attr = detail::ToAttributeConnection<
        TypedAttributeWithFallback<Animatable<std::string>>,
        Variability::Varying>(tattr, value::TypeTraits<std::string>::type_name());

    return detail::FollowConnection(
        stage, attr, attr_name, value_out, err,
        value::TimeCode::Default(), value::TimeSampleInterpolationType::Held);

  } else if (tattr.is_value_empty()) {
    if (err) {
      (*err) += "Attribute value is empty.\n";
    }
    return false;
  } else {
    if (err) {
      (*err) += fmt::format("Unsupported/Invalid TypedAnimatableAttribute value: {}", attr_name);
    }
  }
  return false;
}

// -----------------------------------------------------------------------
// Template instantiations (all four wrapper types, excluding std::string)
// -----------------------------------------------------------------------

#define EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE(__ty) \
template bool EvaluateTypedAttribute(const tinyusdz::Stage &stage, const TypedAttribute<__ty> &attr, const std::string &attr_name, __ty *value, std::string *err); \
template bool EvaluateTypedAnimatableAttribute(const tinyusdz::Stage &stage, const TypedAttribute<Animatable<__ty>> &attr, const std::string &attr_name, __ty *value, std::string *err, const double t, const value::TimeSampleInterpolationType tinterp); \
template bool EvaluateTypedAttribute(const tinyusdz::Stage &stage, const TypedAttributeWithFallback<__ty> &attr, const std::string &attr_name, __ty *value, std::string *err); \
template bool EvaluateTypedAnimatableAttribute(const tinyusdz::Stage &stage, const TypedAttributeWithFallback<Animatable<__ty>> &attr, const std::string &attr_name, __ty *value, std::string *err, const double t, const value::TimeSampleInterpolationType tinterp);

APPLY_FUNC_TO_VALUE_TYPES_NO_STRING(EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE)

#undef EVALUATE_TYPED_ATTRIBUTE_INSTANCIATE


}  // namespace tydra
}  // namespace tinyusdz
