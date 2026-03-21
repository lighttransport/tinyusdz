// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment, Inc.
//
#include "attribute-eval.hh"
#include "scene-access.hh"

#include "common-macros.inc"
#include "layer.hh"
#include "pprint-enum.hh"
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

  DCOUT("var has_default " << var.has_default());
  DCOUT("var has_timesamples " << var.has_default());
  DCOUT("var is_blocked " << var.is_blocked());
  DCOUT("var has_value||has_ts " << (var.has_value() || var.has_timesamples()));

  if (!var.has_value() && !var.has_timesamples()) {
    PUSH_ERROR_AND_RETURN("[InternalError] Attribute is invalid.");
  }

  // AOUSD Core Spec 12.3: Value resolution priority:
  //   timeSamples > spline > default > clips > fallback
  //
  // When time is specified (not default time):
  //   1. If timeSamples exist, interpolate at time t
  //   2. Else if default exists, return default (time ignored)
  //
  // When time is default:
  //   1. Return default if it exists
  //   2. Else return first timeSample value (held at default time)

  bool isDefaultTime = value::TimeCode(t).is_default();

  if (isDefaultTime) {
    // Default time: prefer default value, fall back to timeSamples
    if (var.has_value()) {
      const value::Value &v = var.value_raw();
      DCOUT("Attribute is scalar type:" << v.type_name());
      value->set_value(v);
    } else if (var.has_timesamples()) {
      // No default, use timeSamples at default time (held behavior)
      value::Value v;
      if (!var.get_interpolated_value(t, tinterp, &v)) {
        PUSH_ERROR_AND_RETURN("Interpolate TimeSamples at default time failed.");
        return false;
      }
      value->set_value(v);
    }
  } else {
    // Specific time: prefer timeSamples, fall back to default
    if (var.has_timesamples()) {
      value::Value v;
      if (!var.get_interpolated_value(t, tinterp, &v)) {
        PUSH_ERROR_AND_RETURN("Interpolate TimeSamples failed.");
        return false;
      }
      value->set_value(v);
    } else if (var.has_value()) {
      // No timeSamples: return default regardless of requested time
      const value::Value &v = var.value_raw();
      DCOUT("No timeSamples, returning default value");
      value->set_value(v);
    }
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

  // Iterative connection-following loop (replaces tail recursion)
  const Prim *current_prim = &prim;
  std::string current_attr_name = attr_name;
  constexpr size_t kMaxConnectionChain = 1024;

  for (size_t iter = 0; iter < kMaxConnectionChain; ++iter) {
    DCOUT("Prim : " << current_prim->element_path().element_name() << "("
                    << current_prim->type_name() << ") attr_name " << current_attr_name);

    Property prop;
    if (!GetProperty(*current_prim, current_attr_name, &prop, err)) {
      DCOUT("Get property failed: " << current_attr_name);
      return false;
    }

    if (prop.is_attribute_connection()) {
      // Follow connection target Path(single targetPath only).
      std::vector<Path> pv = prop.get_attribute().connections();
      Path target;
      if (!detail::ResolveSingleConnectionTargetPath(pv, current_attr_name, &target,
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
        std::string abs_path = target.full_path_name();

        if (visited_paths.count(abs_path)) {
          PUSH_ERROR_AND_RETURN(fmt::format(
              "Circular referencing detected. connectionTargetPath = {}",
              to_string(target)));
        }
        visited_paths.insert(abs_path);

        // Continue loop with the target prim/attr (iterative tail call)
        current_prim = targetPrimRet.value();
        current_attr_name = targetPrimPropName;
        continue;

      } else {
        PUSH_ERROR_AND_RETURN(targetPrimRet.error());
        return false;
      }
    } else if (prop.is_attribute()) {
      DCOUT("IsAttrib");

      const Attribute &attr = prop.get_attribute();

      if (attr.is_blocked()) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Attribute `{}` is ValueBlocked(None).", current_attr_name));
      }

      if (!ToTerminalAttributeValue(attr, value, err, t, tinterp)) {
        return false;
      }

      return true;

    } else if (prop.is_relationship()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Property `{}` is a Relation.", current_attr_name));
    } else if (prop.is_empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Attribute `{}` is a define-only attribute(no value assigned).",
          current_attr_name));
    } else {
      PUSH_ERROR_AND_RETURN(
          fmt::format("[InternalError] Invalid Attribute `{}`.", current_attr_name));
    }
  }

  PUSH_ERROR_AND_RETURN("Connection chain too long (possible cycle).");
  return false;
}

bool EvaluateAttributeImpl(
    const tinyusdz::Stage &stage, const tinyusdz::Attribute &attr,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, std::set<std::string> &visited_paths, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {

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
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (visited_paths.count(abs_path)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }
      visited_paths.insert(abs_path);

      // Delegate to the iterative Prim-based overload
      return EvaluateAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                   value, err, visited_paths, t, tinterp);

    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
      return false;
    }
  } else if (attr.is_blocked()) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
  } else {

    if (!ToTerminalAttributeValue(attr, value, err, t, tinterp)) {
      return false;
    }

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

bool EvaluateAttribute(
    const tinyusdz::Stage &stage, const Attribute &attr,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {
  std::set<std::string> visited_paths;

  return EvaluateAttributeImpl(stage, attr, attr_name, value, err,
                               visited_paths, t, tinterp);
}

// Layer/PrimSpec version
bool EvaluateAttribute(
    const tinyusdz::Layer &layer, const tinyusdz::PrimSpec &ps,
    const std::string &attr_name, TerminalAttributeValue *value,
    std::string *err, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {
  (void)layer;
  
  if (!value) {
    PUSH_ERROR_AND_RETURN("[InternalError] nullptr value is not allowed.");
  }

  DCOUT("PrimSpec : " << ps.name() << "(" << ps.typeName() << ") attr_name " << attr_name);

  // Look up the property in PrimSpec's properties
  const auto &props = ps.props();
  auto it = props.find(attr_name);
  if (it == props.end()) {
    PUSH_ERROR_AND_RETURN(fmt::format("Attribute `{}` not found in PrimSpec `{}`", attr_name, ps.name()));
  }

  const Property &prop = it->second;

  // Handle different property types
  if (prop.is_attribute_connection()) {
    // For Layer/PrimSpec version, we cannot follow connections 
    // since we don't have the full Stage context for path resolution
    PUSH_ERROR_AND_RETURN(fmt::format("Attribute `{}` is a connection. Connection following is not supported in Layer/PrimSpec version of EvaluateAttribute. Use Stage version instead.", attr_name));
    
  } else if (prop.is_attribute()) {
    DCOUT("IsAttrib");

    const Attribute &attr = prop.get_attribute();

    if (attr.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Attribute `{}` is ValueBlocked(None).", attr_name));
    }

    // Check if this is an empty attribute (type info only)
    if (prop.is_empty()) {
      // For empty attributes, set as empty with type info
      std::string type_name = attr.type_name();
      if (type_name.empty()) {
        type_name = "unknown";
      }
      value->set_empty_attribute(type_name);
      DCOUT("Empty attribute with type: " << type_name);
    } else {
      if (!ToTerminalAttributeValue(attr, value, err, t, tinterp)) {
        return false;
      }
    }

  } else if (prop.is_relationship()) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Property `{}` is a Relation.", attr_name));
  } else if (prop.is_empty()) {
    // "empty" attribute - set as empty with type info
    std::string type_name = "unknown"; // Default fallback
    
    // Try to get type information from the attribute if available
    if (prop.is_attribute()) {
      const Attribute &attr = prop.get_attribute();
      const primvar::PrimVar &var = attr.get_var();
      if (var.has_value() || var.has_timesamples()) {
        type_name = var.type_name();
      }
    }
    
    value->set_empty_attribute(type_name);
    DCOUT("Empty attribute with type: " << type_name);
    
  } else {
    // ???
    PUSH_ERROR_AND_RETURN(
        fmt::format("[InternalError] Invalid Property type for `{}`.", attr_name));
  }

  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
