// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - Present Light Transport Entertainment Inc.
#include "urdf-to-usd-internal.hh"
#include "next/layer/layer.hh"
#include "next/layer/prim-spec.hh"
#include "next/types/value.hh"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace lightusd { namespace tydra { namespace next {
namespace urdf_detail {

std::vector<std::string> NewtonActuatorSchemas(const Json &act) {
  std::vector<std::string> schemas;
  const std::string control = JsonString(act, "control", "pd");
  if (control == "pid" || act.contains("ki") || act.contains("integralMax")) {
    schemas.push_back("NewtonPIDControlAPI");
  } else {
    schemas.push_back("NewtonPDControlAPI");
  }
  if (act.contains("delaySteps")) schemas.push_back("NewtonActuatorDelayAPI");
  if (act.contains("maxEffort")) schemas.push_back("NewtonMaxEffortClampingAPI");
  if (act.contains("maxMotorEffort") || act.contains("saturationEffort") ||
      act.contains("velocityLimit")) {
    schemas.push_back("NewtonDCMotorClampingAPI");
  }
  if (act.contains("lookupPositions") || act.contains("lookupEfforts")) {
    schemas.push_back("NewtonPositionBasedClampingAPI");
  }
  return schemas;
}

void AddNewtonActuatorFromJson(Scope *scope, std::set<std::string> *siblings,
                               const Json &act, const NameMap &joint_names,
                               size_t index, std::string *warn) {
  const std::string name = Sanitize(
      JsonString(act, "name", "actuator_" + std::to_string(index)), "actuator");
  std::vector<std::string> targets;
  const std::string joint = JsonString(act, "joint");
  if (!joint.empty()) {
    auto found = joint_names.find(joint);
    if (found != joint_names.end()) {
      targets.push_back("/World/Joints/" + found->second);
    }
  }
  if (const Json *items = JsonArray(act, "targets")) {
    for (const Json &target : *items) {
      if (!target.is_string()) continue;
      const std::string name_or_path = target.get<std::string>();
      if (!name_or_path.empty() && name_or_path[0] == '/') {
        targets.push_back(name_or_path);
      } else {
        auto found = joint_names.find(name_or_path);
        if (found != joint_names.end()) {
          targets.push_back("/World/Joints/" + found->second);
        }
      }
    }
  }
  if (targets.empty()) {
    AppendWarn(warn, "Skipping Newton actuator `" + name +
                         "`: no target joint was exported.\n");
    return;
  }
  tn::PrimSpec *prim = scope->Add(ChildName(name, siblings), "NewtonActuator");
  if (!prim) return;
  for (const std::string &schema : NewtonActuatorSchemas(act)) {
    AddAPI(prim, schema);
  }
  SetRel(prim, "newton:targets", targets);
  if (act.contains("delaySteps")) {
    Set(prim, "newton:delaySteps", tn::Value(JsonInt(act, "delaySteps", 1)),
        "int", true);
  }
  for (const char *key : {"constEffort", "kp", "kd", "ki", "integralMax",
                          "maxEffort", "maxMotorEffort", "saturationEffort",
                          "velocityLimit"}) {
    float v = 0.0f;
    if (ReadNumber(act, key, &v)) {
      Set(prim, std::string("newton:") + key, tn::Value(v), "float", true);
    }
  }
  for (const char *key : {"lookupPositions", "lookupEfforts"}) {
    std::vector<float> values = JsonFloats(act, key);
    if (!values.empty()) {
      Set(prim, std::string("newton:") + key,
          tn::Value::MakeFloatArray(std::move(values)), "float[]", true);
    }
  }
}

void AddMjcSiteFromJson(Scope *scope, const Json &site,
                        const std::string &name) {
  tn::PrimSpec *prim = scope->Add(name, "Sphere");
  if (!prim) return;
  double radius = 0.005;
  ReadNumber(site, "size", &radius);
  if (radius <= 0.0) radius = 0.005;
  Set(prim, "radius", tn::Value(radius), "double");
  SetTransformFromJson(prim, site);
  AddAPI(prim, "MjcSiteAPI");
  int32_t group = 0;
  if (ReadIntFrom(site, "", "group", &group)) {
    Set(prim, "mjc:group", tn::Value(group), "int", true);
  }
  // Sites are markers, not colliders / visuals by default (MuJoCo group>=3).
  SetToken(prim, "purpose", "guide", true);
}

void AddMjcTendonFromJson(Scope *scope, std::set<std::string> *siblings,
                          const Json &t, const NameMap &joint_names,
                          const NameMap &site_names, NameMap *tendon_names,
                          size_t index, std::string *warn) {
  std::string source = JsonString(t, "name");
  if (source.empty()) {
    const std::string prim_path = JsonString(t, "path");
    const size_t slash = prim_path.find_last_of('/');
    if (!prim_path.empty() && slash != std::string::npos &&
        slash + 1 < prim_path.size()) {
      source = prim_path.substr(slash + 1);
    }
  }
  if (source.empty()) source = "tendon_" + std::to_string(index);
  const std::string name = Sanitize(source, "tendon");
  (*tendon_names)[source] = name;
  const std::string type = JsonString(t, "type", "fixed");

  auto resolve_in = [](const NameMap &names, const char *scope_path,
                       const std::string &ref, std::string *out) {
    auto found = names.find(ref);
    if (found == names.end()) return false;
    *out = scope_path + found->second;
    return true;
  };
  auto resolve_route_ref = [&](const std::string &ref, std::string *out) {
    if (ref.empty()) return false;
    if (ref[0] == '/') {
      *out = ref;
      return true;
    }
    if (type == "spatial") return resolve_in(site_names, "/World/Sites/", ref, out);
    return resolve_in(joint_names, "/World/Joints/", ref, out);
  };
  auto resolve_site_ref = [&](const std::string &ref, std::string *out) {
    if (ref.empty()) return false;
    if (ref[0] == '/') {
      *out = ref;
      return true;
    }
    return resolve_in(site_names, "/World/Sites/", ref, out);
  };
  auto append_route_ref = [&](const std::string &ref,
                              std::vector<std::string> *out) {
    std::string resolved;
    if (resolve_route_ref(ref, &resolved)) {
      out->push_back(resolved);
      return true;
    }
    if (!ref.empty()) {
      AppendWarn(warn, "Tendon `" + name + "` route reference `" + ref +
                           "` could not be resolved; skipping it.\n");
    }
    return false;
  };
  auto append_route_object = [&](const Json &item,
                                 std::vector<std::string> *out) {
    if (!item.is_object()) return false;
    std::string ref = JsonString(item, "target");
    if (ref.empty()) ref = JsonString(item, "path");
    if (ref.empty() && type == "spatial") ref = JsonString(item, "site");
    if (ref.empty() && type == "spatial") ref = JsonString(item, "sidesite");
    if (ref.empty()) ref = JsonString(item, "joint");
    return append_route_ref(ref, out);
  };

  std::vector<std::string> targets;
  std::vector<double> coefs = JsonDoubles(t, "routeCoef");
  const std::vector<std::string> route = JsonStrings(t, "route");
  const Json *route_items = JsonArray(t, "route");
  const Json *path_items = JsonArray(t, "path");
  if (!route.empty()) {
    for (const std::string &ref : route) append_route_ref(ref, &targets);
  } else if (route_items) {
    for (const Json &item : *route_items) append_route_object(item, &targets);
  } else if (path_items && !JsonStrings(t, "path").empty()) {
    for (const std::string &ref : JsonStrings(t, "path")) {
      append_route_ref(ref, &targets);
    }
  } else if (type == "spatial") {
    // Spatial (muscle) tendon: ordered site/sidesite waypoints -> mjc:path
    // (wrap geoms approximated by their sidesite), one coef per site.
    if (path_items) {
      for (const Json &wp : *path_items) {
        if (!wp.is_object()) continue;
        std::string site_ref;
        if (wp.contains("site")) site_ref = JsonString(wp, "site");
        else if (wp.contains("sidesite")) site_ref = JsonString(wp, "sidesite");
        if (site_ref.empty()) {
          append_route_object(wp, &targets);
          continue;  // pulley / unresolvable geom wrap
        }
        append_route_ref(site_ref, &targets);
        coefs.push_back(1.0);
      }
    }
  } else if (const Json *joints = JsonArray(t, "joints")) {
    // Fixed tendon: linear combination of joint coordinates.
    for (const Json &je : *joints) {
      if (!je.is_object()) continue;
      const std::string jname = JsonString(je, "joint");
      auto found = joint_names.find(jname);
      if (found == joint_names.end()) {
        AppendWarn(warn, "Tendon `" + name + "` references joint `" + jname +
                             "` that was not exported; skipping it.\n");
        continue;
      }
      targets.push_back("/World/Joints/" + found->second);
      double coef = 1.0;
      ReadNumber(je, "coef", &coef);
      coefs.push_back(coef);
    }
  }

  if (targets.empty()) {
    AppendWarn(warn, type == "spatial"
                         ? "Skipping spatial tendon `" + name +
                               "`: no routing sites were exported.\n"
                         : "Skipping tendon `" + name +
                               "`: no referenced joint was exported.\n");
    return;
  }
  if (type == "spatial" && targets.size() < 2) {
    AppendWarn(warn, "Spatial tendon `" + name +
                         "` has fewer than 2 routing sites; preserving the "
                         "partial route.\n");
  }

  tn::PrimSpec *prim = scope->Add(ChildName(name, siblings), "MjcTendon");
  if (!prim) return;
  SetToken(prim, "mjc:type", type, true);
  SetRel(prim, "mjc:path", targets);
  std::vector<double> route_coef = JsonDoubles(t, "routeCoef");
  if (!route_coef.empty()) coefs = std::move(route_coef);
  if (!coefs.empty()) SetDoubles(prim, "mjc:path:coef", coefs, true);

  std::vector<std::string> side_sites;
  for (const std::string &ref : JsonStrings(t, "sideSites")) {
    std::string resolved;
    if (resolve_site_ref(ref, &resolved)) {
      side_sites.push_back(resolved);
    } else {
      AppendWarn(warn, "Tendon `" + name + "` sideSite reference `" + ref +
                           "` could not be resolved; skipping it.\n");
    }
  }
  const Json *side_items = JsonArray(t, "sideSites");
  if (side_sites.empty() && side_items) {
    for (const Json &item : *side_items) {
      if (!item.is_object()) continue;
      std::string ref = JsonString(item, "target");
      if (ref.empty()) ref = JsonString(item, "path");
      if (ref.empty()) ref = JsonString(item, "site");
      if (ref.empty()) ref = JsonString(item, "sidesite");
      std::string resolved;
      if (resolve_site_ref(ref, &resolved)) {
        side_sites.push_back(resolved);
      } else if (!ref.empty()) {
        AppendWarn(warn, "Tendon `" + name + "` sideSite reference `" + ref +
                             "` could not be resolved; skipping it.\n");
      }
    }
  }
  SetRel(prim, "mjc:sideSites", side_sites);

  const std::pair<const char *, const char *> int_arrays[] = {
      {"routeIndices", "mjc:path:indices"},
      {"sideSiteIndices", "mjc:sideSites:indices"},
      {"routeSegments", "mjc:path:segments"}};
  for (const auto &entry : int_arrays) {
    std::vector<int32_t> values = JsonInts(t, entry.first);
    if (!values.empty()) {
      Set(prim, entry.second, tn::Value::MakeIntArray(std::move(values)),
          "int[]", true);
    }
  }
  std::vector<double> divisors = JsonDoubles(t, "routeDivisors");
  if (!divisors.empty()) SetDoubles(prim, "mjc:path:divisors", divisors, true);

  const std::vector<double> rgba = JsonDoubles(t, "rgba");
  if (rgba.size() >= 4) {
    Set(prim, "mjc:rgba",
        tn::Value::MakeColor4f(static_cast<float>(rgba[0]),
                               static_cast<float>(rgba[1]),
                               static_cast<float>(rgba[2]),
                               static_cast<float>(rgba[3])),
        "color4f", true);
  }
  if (t.contains("group")) {
    Set(prim, "mjc:group", tn::Value(JsonInt(t, "group", 0)), "int", true);
  }
  for (const char *key : {"stiffness", "damping", "frictionloss", "margin",
                          "armature", "width"}) {
    double v = 0.0;
    if (ReadNumber(t, key, &v)) {
      Set(prim, std::string("mjc:") + key, tn::Value(v), "double", true);
    }
  }
  double lo = 0.0, hi = 0.0;
  if (ReadRange(t, "range", &lo, &hi)) {
    Set(prim, "mjc:range:min", tn::Value(lo), "double", true);
    Set(prim, "mjc:range:max", tn::Value(hi), "double", true);
    SetToken(prim, "mjc:limited", "true", true);
  }
  if (ReadRange(t, "actuatorfrcrange", &lo, &hi)) {
    Set(prim, "mjc:actuatorfrcrange:min", tn::Value(lo), "double", true);
    Set(prim, "mjc:actuatorfrcrange:max", tn::Value(hi), "double", true);
  }
  const std::pair<const char *, const char *> scalars[] = {
      {"range_min", "mjc:range:min"},
      {"range_max", "mjc:range:max"},
      {"actuatorfrcrange_min", "mjc:actuatorfrcrange:min"},
      {"actuatorfrcrange_max", "mjc:actuatorfrcrange:max"}};
  for (const auto &entry : scalars) {
    double v = 0.0;
    if (ReadNumber(t, entry.first, &v)) {
      Set(prim, entry.second, tn::Value(v), "double", true);
    }
  }
  if (t.contains("limited")) {
    SetToken(prim, "mjc:limited", JsonString(t, "limited", "auto"), true);
  }
  if (t.contains("actuatorfrclimited")) {
    SetToken(prim, "mjc:actuatorfrclimited",
             JsonString(t, "actuatorfrclimited", "auto"), true);
  }
  for (const char *key : {"springlength", "solreflimit", "solimplimit",
                          "solreffriction", "solimpfriction"}) {
    std::vector<double> values = JsonDoubles(t, key);
    if (!values.empty()) {
      SetDoubles(prim, std::string("mjc:") + key, std::move(values), true);
    }
  }
}

void AddMjcEqualityFromJson(Scope *scope, std::set<std::string> *siblings,
                            const Json &e, const NameMap &link_names,
                            const NameMap &joint_names, size_t index) {
  const std::string type = JsonString(e, "type", "connect");
  const std::string name = Sanitize(
      JsonString(e, "name", "equality_" + std::to_string(index)), "equality");
  tn::PrimSpec *prim = scope->Add(ChildName(name, siblings), "Xform");
  if (!prim) return;
  AddAPI(prim, type == "weld"    ? "MjcEqualityWeldAPI"
               : type == "joint" ? "MjcEqualityJointAPI"
                                 : "MjcEqualityConnectAPI");
  std::vector<std::string> targets;
  auto resolve = [&](const NameMap &names, const char *scope_path,
                     const std::string &n) {
    auto found = names.find(n);
    if (found != names.end()) targets.push_back(scope_path + found->second);
  };
  if (type == "joint") {
    resolve(joint_names, "/World/Joints/", JsonString(e, "joint1"));
    resolve(joint_names, "/World/Joints/", JsonString(e, "joint2"));
  } else {
    resolve(link_names, "/World/Links/", JsonString(e, "body1"));
    resolve(link_names, "/World/Links/", JsonString(e, "body2"));
  }
  SetRel(prim, "mjc:target", targets);

  double v = 0.0;
  if (type == "weld" && ReadNumber(e, "torquescale", &v)) {
    Set(prim, "mjc:torqueScale", tn::Value(static_cast<float>(v)), "float");
  }
  if (type == "joint") {
    if (const Json *pc = JsonArray(e, "polycoef")) {
      for (size_t i = 0; i < pc->size() && i < 5; i++) {
        if ((*pc)[i].is_number()) {
          Set(prim, "mjc:coef" + std::to_string(i),
              tn::Value((*pc)[i].get<double>()), "double");
        }
      }
    }
  }
  for (const char *key : {"solref", "solimp", "anchor"}) {
    std::vector<double> values = JsonDoubles(e, key);
    if (!values.empty()) {
      SetDoubles(prim, std::string("mjc:") + key, std::move(values));
    }
  }
}

void AddMjcActuatorFromJson(Scope *scope, std::set<std::string> *used,
                            const Json &a, const NameMap &joint_names,
                            const NameMap &tendon_names,
                            const NameMap &site_names,
                            const NameMap &link_names, size_t index) {
  const std::string name = Unique(
      JsonString(a, "name", "actuator_" + std::to_string(index)), "actuator",
      used);
  tn::PrimSpec *prim = scope->Add(name, "MjcActuator");
  if (!prim) return;

  auto resolve = [](const NameMap &names, const char *scope_path,
                    const std::string &n) -> std::vector<std::string> {
    auto found = names.find(n);
    if (found == names.end()) return {};
    return {scope_path + found->second};
  };
  const std::string t_tendon = JsonString(a, "targetTendon");
  const std::string t_joint = JsonString(a, "targetJoint");
  const std::string t_site = JsonString(a, "targetSite");
  const std::string t_body = JsonString(a, "targetBody");
  const std::string t_path = JsonString(a, "target");
  std::vector<std::string> targets;
  if (!t_path.empty() && t_path[0] == '/') {
    targets.push_back(t_path);
  } else if (!t_tendon.empty()) {
    targets = resolve(tendon_names, "/World/Tendons/", t_tendon);
  } else if (!t_joint.empty()) {
    targets = resolve(joint_names, "/World/Joints/", t_joint);
  } else if (!t_site.empty()) {
    targets = resolve(site_names, "/World/Sites/", t_site);
  } else if (!t_body.empty()) {  // <adhesion body=..>
    targets = resolve(link_names, "/World/Links/", t_body);
  }
  SetRel(prim, "mjc:target", targets);

  for (const char *key : {"gainPrm", "biasPrm", "dynPrm", "gear"}) {
    std::vector<double> values = JsonDoubles(a, key);
    if (!values.empty()) {
      SetDoubles(prim, std::string("mjc:") + key, std::move(values), true);
    }
  }
  // `<range>: [min, max]` pairs, then the explicit `<range>_min/_max`
  // scalars override them.
  for (const char *key : {"lengthRange", "ctrlRange", "forceRange",
                          "actRange"}) {
    double lo = 0.0, hi = 0.0;
    if (ReadRange(a, key, &lo, &hi)) {
      Set(prim, std::string("mjc:") + key + ":min", tn::Value(lo), "double",
          true);
      Set(prim, std::string("mjc:") + key + ":max", tn::Value(hi), "double",
          true);
    }
  }
  for (const char *key : {"ctrlRange", "forceRange", "actRange",
                          "lengthRange"}) {
    for (const char *bound : {"min", "max"}) {
      double v = 0.0;
      if (ReadNumber(a, (std::string(key) + "_" + bound).c_str(), &v)) {
        Set(prim, std::string("mjc:") + key + ":" + bound, tn::Value(v),
            "double", true);
      }
    }
  }
  for (const char *key : {"crankLength", "inheritRange"}) {
    double v = 0.0;
    if (ReadNumber(a, key, &v)) {
      Set(prim, std::string("mjc:") + key, tn::Value(v), "double", true);
    }
  }
  for (const char *key : {"group", "actDim"}) {
    int32_t v = 0;
    if (ReadIntFrom(a, "", key, &v)) {
      Set(prim, std::string("mjc:") + key, tn::Value(v), "int", true);
    }
  }
  for (const char *key : {"jointInParent", "actEarly"}) {
    bool v = false;
    if (ReadBool(a, key, &v)) {
      Set(prim, std::string("mjc:") + key, tn::Value(v), "bool", true);
    }
  }
  const std::pair<const char *, const char *> tokens[] = {
      {"gainType", "fixed"},     {"biasType", "none"},
      {"dynType", "none"},       {"ctrlLimited", "auto"},
      {"forceLimited", "auto"},  {"actLimited", "auto"},
      {"plugin", ""},            {"instance", ""}};
  for (const auto &entry : tokens) {
    if (a.contains(entry.first)) {
      SetToken(prim, std::string("mjc:") + entry.first,
               JsonString(a, entry.first, entry.second), true);
    }
  }
  for (const char *key : {"refSite", "sliderSite"}) {
    const std::string site = JsonString(a, key);
    if (site.empty()) continue;
    SetRel(prim, std::string("mjc:") + key,
           site[0] == '/' ? std::vector<std::string>{site}
                          : resolve(site_names, "/World/Sites/", site));
  }
}

void AddMjcKeyframeFromJson(Scope *scope, std::set<std::string> *used,
                            const Json &k, size_t index) {
  const std::string name =
      Unique(JsonString(k, "name", "key_" + std::to_string(index)), "key", used);
  tn::PrimSpec *prim = scope->Add(name, "MjcKeyframe");
  if (!prim) return;
  for (const char *key : {"qpos", "qvel", "act", "ctrl", "mpos", "mquat"}) {
    std::vector<double> values = JsonDoubles(k, key);
    if (!values.empty()) {
      SetDoubles(prim, std::string("mjc:") + key, std::move(values), true);
    }
  }
}

void AddMjcSensorFromJson(Scope *scope, std::set<std::string> *used,
                          const Json &s, size_t index) {
  const std::string name = Unique(
      JsonString(s, "name", "sensor_" + std::to_string(index)), "sensor", used);
  tn::PrimSpec *prim = scope->Add(name, "MjcSensor");
  if (!prim) return;
  const char *keys[5] = {"type", "objtype", "objname", "reftype", "refname"};
  std::string fields[5];
  for (int i = 0; i < 5; i++) fields[i] = JsonString(s, keys[i]);
  auto alias = [&](const char *key, const char *type, std::string *type_field,
                   std::string *name_field) {
    if (!type_field->empty() || !name_field->empty()) return;
    const std::string target = JsonString(s, key);
    if (!target.empty()) {
      *type_field = type;
      *name_field = target;
    }
  };
  const std::pair<const char *, const char *> target_aliases[] = {
      {"site", "site"},         {"joint", "joint"},
      {"actuator", "actuator"}, {"tendon", "tendon"},
      {"body", "body"},         {"xbody", "xbody"},
      {"geom", "geom"},         {"camera", "camera"},
      {"light", "light"},       {"sensor", "sensor"},
      {"numeric", "numeric"},   {"text", "text"},
      {"tuple", "tuple"},       {"key", "key"},
      {"plugin", "plugin"}};
  for (const auto &entry : target_aliases) {
    alias(entry.first, entry.second, &fields[1], &fields[2]);
  }
  const std::pair<const char *, const char *> ref_aliases[] = {
      {"refsite", "site"},         {"refSite", "site"},
      {"refjoint", "joint"},       {"refJoint", "joint"},
      {"refactuator", "actuator"}, {"refActuator", "actuator"},
      {"reftendon", "tendon"},     {"refTendon", "tendon"},
      {"refbody", "body"},         {"refBody", "body"},
      {"refxbody", "xbody"},       {"refXbody", "xbody"},
      {"refgeom", "geom"},         {"refGeom", "geom"},
      {"refcamera", "camera"},     {"refCamera", "camera"},
      {"reflight", "light"},       {"refLight", "light"}};
  for (const auto &entry : ref_aliases) {
    alias(entry.first, entry.second, &fields[3], &fields[4]);
  }
  for (int i = 0; i < 5; i++) {
    if (!fields[i].empty()) {
      SetToken(prim, std::string("mjc:") + keys[i], fields[i], true);
    }
  }
  int32_t group = 0;
  if (ReadIntFrom(s, "", "group", &group)) {
    Set(prim, "mjc:group", tn::Value(group), "int", true);
  }
  for (const char *key : {"cutoff", "noise"}) {
    double v = 0.0;
    if (ReadNumber(s, key, &v)) {
      Set(prim, std::string("mjc:") + key, tn::Value(v), "double", true);
    }
  }
  std::vector<double> user = JsonDoubles(s, "user");
  if (!user.empty()) SetDoubles(prim, "mjc:user", std::move(user), true);
}

void AddContactPairFromJson(Scope *scope, std::set<std::string> *used,
                            const Json &p, size_t index) {
  const std::string name =
      Unique(JsonString(p, "name", "pair_" + std::to_string(index)), "pair",
             used);
  tn::PrimSpec *prim = scope->Add(name, "Xform");
  if (!prim) return;
  SetToken(prim, "mjc:geom1", JsonString(p, "geom1"), true);
  SetToken(prim, "mjc:geom2", JsonString(p, "geom2"), true);
  int32_t condim = 0;
  if (ReadIntFrom(p, "", "condim", &condim)) {
    Set(prim, "mjc:condim", tn::Value(condim), "int", true);
  }
  for (const char *key : {"margin", "gap"}) {
    double v = 0.0;
    if (ReadNumber(p, key, &v)) {
      Set(prim, std::string("mjc:") + key, tn::Value(v), "double");
    }
  }
  for (const char *key : {"friction", "solref", "solimp"}) {
    std::vector<double> values = JsonDoubles(p, key);
    if (!values.empty()) {
      SetDoubles(prim, std::string("mjc:") + key, std::move(values));
    }
  }
}

}  // namespace urdf_detail

} } }  // namespace lightusd::tydra::next
