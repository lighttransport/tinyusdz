// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// Physics annotations to JSON Converter Implementation
//

#include "physics-to-json.hh"

#include <sstream>
#include <functional>
#include <vector>

#include "core/prim.hh"
#include "stage.hh"
#include "usdPhysics.hh"
#include "mjcPhysics.hh"

namespace tinyusdz {
namespace tydra {

namespace {

std::string EscapeJson(const std::string &input) {
  std::string output;
  output.reserve(input.size() + 16);
  for (char c : input) {
    switch (c) {
      case '\"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output += c; break;
    }
  }
  return output;
}

std::string Indent(int level, int spaces) {
  return std::string(static_cast<size_t>(level * spaces), ' ');
}

// JSON value helpers
std::string JsonStr(const std::string &s) {
  return "\"" + EscapeJson(s) + "\"";
}

std::string JsonBool(bool b) {
  return b ? "true" : "false";
}

template <typename T>
std::string JsonNum(T v) {
  std::ostringstream oss;
  oss << v;
  return oss.str();
}

std::string JsonVec3f(const value::vector3f &v) {
  std::ostringstream oss;
  oss << "[" << v[0] << ", " << v[1] << ", " << v[2] << "]";
  return oss.str();
}

std::string JsonDouble3(const value::double3 &v) {
  std::ostringstream oss;
  oss << "[" << v[0] << ", " << v[1] << ", " << v[2] << "]";
  return oss.str();
}

std::string JsonDoubleArray(const std::vector<double> &arr) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < arr.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << arr[i];
  }
  oss << "]";
  return oss.str();
}

std::string JsonIntArray(const std::vector<int> &arr) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < arr.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << arr[i];
  }
  oss << "]";
  return oss.str();
}

// Helper to emit a key-value pair
void EmitKV(std::ostringstream &ss, int ind, int sp, const std::string &key,
            const std::string &val, bool comma = true) {
  ss << Indent(ind, sp) << JsonStr(key) << ": " << val;
  if (comma) ss << ",";
  ss << "\n";
}

// Helper to emit optional TypedAttribute<std::vector<T>> as JSON array
template <typename T>
void EmitOptionalArray(std::ostringstream &ss, int ind, int sp,
                       const std::string &key,
                       const TypedAttribute<std::vector<T>> &attr,
                       bool comma = true) {
  auto opt = attr.get_value();
  if (opt.has_value()) {
    if constexpr (std::is_same_v<T, double>) {
      EmitKV(ss, ind, sp, key, JsonDoubleArray(opt.value()), comma);
    } else {
      EmitKV(ss, ind, sp, key, JsonIntArray(opt.value()), comma);
    }
  }
}

// Helper to get relationship target path as string
std::string RelTargetStr(const RelationshipProperty &rp) {
  if (!rp.authored()) return "";
  const auto &rel = rp.relationship();
  if (rel.is_path()) {
    return rel.targetPath.full_path_name();
  }
  return "";
}

// Emit MjcSceneAPI to JSON
void EmitMjcSceneAPI(std::ostringstream &ss, const MjcSceneAPI &api,
                     int ind, int sp) {
  ss << Indent(ind, sp) << "\"mjc\": {\n";
  int i = ind + 1;

  // Option
  ss << Indent(i, sp) << "\"option\": {\n";
  int i2 = i + 1;
  EmitKV(ss, i2, sp, "timestep", JsonNum(api.timestep.get_value()));
  EmitKV(ss, i2, sp, "impratio", JsonNum(api.impratio.get_value()));
  EmitKV(ss, i2, sp, "wind", JsonDouble3(api.wind.get_value()));
  EmitKV(ss, i2, sp, "magnetic", JsonDouble3(api.magnetic.get_value()));
  EmitKV(ss, i2, sp, "density", JsonNum(api.density.get_value()));
  EmitKV(ss, i2, sp, "viscosity", JsonNum(api.viscosity.get_value()));
  EmitKV(ss, i2, sp, "o_margin", JsonNum(api.o_margin.get_value()));
  EmitOptionalArray(ss, i2, sp, "o_solref", api.o_solref);
  EmitOptionalArray(ss, i2, sp, "o_solimp", api.o_solimp);
  EmitOptionalArray(ss, i2, sp, "o_friction", api.o_friction);
  EmitKV(ss, i2, sp, "integrator", JsonStr(api.integrator.get_value().str()));
  EmitKV(ss, i2, sp, "cone", JsonStr(api.cone.get_value().str()));
  EmitKV(ss, i2, sp, "jacobian", JsonStr(api.jacobian.get_value().str()));
  EmitKV(ss, i2, sp, "solver", JsonStr(api.solver.get_value().str()));
  EmitKV(ss, i2, sp, "iterations", JsonNum(api.iterations.get_value()));
  EmitKV(ss, i2, sp, "tolerance", JsonNum(api.tolerance.get_value()));
  EmitKV(ss, i2, sp, "ls_iterations", JsonNum(api.ls_iterations.get_value()));
  EmitKV(ss, i2, sp, "ls_tolerance", JsonNum(api.ls_tolerance.get_value()));
  EmitKV(ss, i2, sp, "noslip_iterations", JsonNum(api.noslip_iterations.get_value()));
  EmitKV(ss, i2, sp, "noslip_tolerance", JsonNum(api.noslip_tolerance.get_value()));
  EmitKV(ss, i2, sp, "ccd_iterations", JsonNum(api.ccd_iterations.get_value()));
  EmitKV(ss, i2, sp, "ccd_tolerance", JsonNum(api.ccd_tolerance.get_value()));
  EmitKV(ss, i2, sp, "sdf_iterations", JsonNum(api.sdf_iterations.get_value()));
  EmitKV(ss, i2, sp, "sdf_initpoints", JsonNum(api.sdf_initpoints.get_value()));
  EmitOptionalArray(ss, i2, sp, "actuatorgroupdisable", api.actuatorgroupdisable, false);
  ss << Indent(i, sp) << "},\n";

  // Flags (all 24)
  ss << Indent(i, sp) << "\"flag\": {\n";
  EmitKV(ss, i2, sp, "constraint", JsonBool(api.flag_constraint.get_value()));
  EmitKV(ss, i2, sp, "equality", JsonBool(api.flag_equality.get_value()));
  EmitKV(ss, i2, sp, "frictionloss", JsonBool(api.flag_frictionloss.get_value()));
  EmitKV(ss, i2, sp, "limit", JsonBool(api.flag_limit.get_value()));
  EmitKV(ss, i2, sp, "contact", JsonBool(api.flag_contact.get_value()));
  EmitKV(ss, i2, sp, "spring", JsonBool(api.flag_spring.get_value()));
  EmitKV(ss, i2, sp, "damper", JsonBool(api.flag_damper.get_value()));
  EmitKV(ss, i2, sp, "gravity", JsonBool(api.flag_gravity.get_value()));
  EmitKV(ss, i2, sp, "clampctrl", JsonBool(api.flag_clampctrl.get_value()));
  EmitKV(ss, i2, sp, "warmstart", JsonBool(api.flag_warmstart.get_value()));
  EmitKV(ss, i2, sp, "filterparent", JsonBool(api.flag_filterparent.get_value()));
  EmitKV(ss, i2, sp, "actuation", JsonBool(api.flag_actuation.get_value()));
  EmitKV(ss, i2, sp, "refsafe", JsonBool(api.flag_refsafe.get_value()));
  EmitKV(ss, i2, sp, "sensor", JsonBool(api.flag_sensor.get_value()));
  EmitKV(ss, i2, sp, "midphase", JsonBool(api.flag_midphase.get_value()));
  EmitKV(ss, i2, sp, "nativeccd", JsonBool(api.flag_nativeccd.get_value()));
  EmitKV(ss, i2, sp, "eulerdamp", JsonBool(api.flag_eulerdamp.get_value()));
  EmitKV(ss, i2, sp, "autoreset", JsonBool(api.flag_autoreset.get_value()));
  EmitKV(ss, i2, sp, "island", JsonBool(api.flag_island.get_value()));
  EmitKV(ss, i2, sp, "override", JsonBool(api.flag_override.get_value()));
  EmitKV(ss, i2, sp, "energy", JsonBool(api.flag_energy.get_value()));
  EmitKV(ss, i2, sp, "fwdinv", JsonBool(api.flag_fwdinv.get_value()));
  EmitKV(ss, i2, sp, "invdiscrete", JsonBool(api.flag_invdiscrete.get_value()));
  EmitKV(ss, i2, sp, "multiccd", JsonBool(api.flag_multiccd.get_value()), false);
  ss << Indent(i, sp) << "},\n";

  // Compiler (all 14)
  ss << Indent(i, sp) << "\"compiler\": {\n";
  EmitKV(ss, i2, sp, "autoLimits", JsonBool(api.compiler_autoLimits.get_value()));
  EmitKV(ss, i2, sp, "boundMass", JsonNum(api.compiler_boundMass.get_value()));
  EmitKV(ss, i2, sp, "boundInertia", JsonNum(api.compiler_boundInertia.get_value()));
  EmitKV(ss, i2, sp, "setTotalMass", JsonNum(api.compiler_setTotalMass.get_value()));
  EmitKV(ss, i2, sp, "useThread", JsonBool(api.compiler_useThread.get_value()));
  EmitKV(ss, i2, sp, "balanceInertia", JsonBool(api.compiler_balanceInertia.get_value()));
  EmitKV(ss, i2, sp, "angle", JsonStr(api.compiler_angle.get_value().str()));
  EmitKV(ss, i2, sp, "fitAABB", JsonBool(api.compiler_fitAABB.get_value()));
  EmitKV(ss, i2, sp, "fuseStatic", JsonBool(api.compiler_fuseStatic.get_value()));
  EmitKV(ss, i2, sp, "inertiaFromGeom", JsonStr(api.compiler_inertiaFromGeom.get_value().str()));
  EmitKV(ss, i2, sp, "alignFree", JsonBool(api.compiler_alignFree.get_value()));
  EmitKV(ss, i2, sp, "inertiaGroupRangeMin", JsonNum(api.compiler_inertiaGroupRangeMin.get_value()));
  EmitKV(ss, i2, sp, "inertiaGroupRangeMax", JsonNum(api.compiler_inertiaGroupRangeMax.get_value()));
  EmitKV(ss, i2, sp, "saveInertial", JsonBool(api.compiler_saveInertial.get_value()), false);
  ss << Indent(i, sp) << "}\n";

  ss << Indent(ind, sp) << "}";
}

// Emit MjcJointAPI to JSON
void EmitMjcJointAPI(std::ostringstream &ss, const MjcJointAPI &api,
                     int ind, int sp) {
  ss << Indent(ind, sp) << "\"mjc\": {\n";
  int i = ind + 1;
  EmitKV(ss, i, sp, "group", JsonNum(api.group.get_value()));
  EmitKV(ss, i, sp, "stiffness", JsonNum(api.stiffness.get_value()));
  EmitKV(ss, i, sp, "damping", JsonNum(api.damping.get_value()));
  EmitKV(ss, i, sp, "armature", JsonNum(api.armature.get_value()));
  EmitKV(ss, i, sp, "frictionloss", JsonNum(api.frictionloss.get_value()));
  EmitOptionalArray(ss, i, sp, "springdamper", api.springdamper);
  EmitKV(ss, i, sp, "springref", JsonNum(api.springref.get_value()));
  EmitKV(ss, i, sp, "ref", JsonNum(api.ref.get_value()));
  EmitKV(ss, i, sp, "margin", JsonNum(api.margin.get_value()));
  EmitOptionalArray(ss, i, sp, "solreflimit", api.solreflimit);
  EmitOptionalArray(ss, i, sp, "solimplimit", api.solimplimit);
  EmitOptionalArray(ss, i, sp, "solreffriction", api.solreffriction);
  EmitOptionalArray(ss, i, sp, "solimpfriction", api.solimpfriction);
  EmitKV(ss, i, sp, "actuatorfrcrange_min", JsonNum(api.actuatorfrcrange_min.get_value()));
  EmitKV(ss, i, sp, "actuatorfrcrange_max", JsonNum(api.actuatorfrcrange_max.get_value()));
  EmitKV(ss, i, sp, "actuatorfrclimited", JsonStr(api.actuatorfrclimited.get_value().str()));
  EmitKV(ss, i, sp, "actuatorgravcomp", JsonBool(api.actuatorgravcomp.get_value()), false);
  ss << Indent(ind, sp) << "}";
}

// Recursive prim traversal
using PrimVisitor = std::function<void(const Prim &, const std::string &)>;
void TraversePrims(const std::vector<Prim> &prims, const std::string &parent_path,
                   PrimVisitor visitor) {
  for (const auto &prim : prims) {
    std::string path = parent_path + "/" + prim.element_name();
    visitor(prim, path);
    TraversePrims(prim.children(), path, visitor);
  }
}

}  // anonymous namespace

bool ConvertPhysicsToJson(
    const Stage &stage,
    std::string *json_str,
    std::string *err,
    const PhysicsJsonExportOptions &options) {

  if (!json_str) {
    if (err) *err = "json_str is null";
    return false;
  }

  int sp = options.indent;

  // Collect physics prims by traversal
  struct PhysicsData {
    std::vector<std::pair<std::string, const PhysicsScene*>> scenes;
    std::vector<std::pair<std::string, const PhysicsRevoluteJoint*>> revoluteJoints;
    std::vector<std::pair<std::string, const PhysicsPrismaticJoint*>> prismaticJoints;
    std::vector<std::pair<std::string, const PhysicsSphericalJoint*>> sphericalJoints;
    std::vector<std::pair<std::string, const PhysicsFixedJoint*>> fixedJoints;
    std::vector<std::pair<std::string, const PhysicsDistanceJoint*>> distanceJoints;
    std::vector<std::pair<std::string, const MjcActuator*>> actuators;
    std::vector<std::pair<std::string, const MjcTendon*>> tendons;
    std::vector<std::pair<std::string, const MjcKeyframe*>> keyframes;
  } data;

  TraversePrims(stage.root_prims(), "", [&](const Prim &prim, const std::string &path) {
    if (auto *p = prim.as<PhysicsScene>()) data.scenes.emplace_back(path, p);
    else if (auto *p = prim.as<PhysicsRevoluteJoint>()) data.revoluteJoints.emplace_back(path, p);
    else if (auto *p = prim.as<PhysicsPrismaticJoint>()) data.prismaticJoints.emplace_back(path, p);
    else if (auto *p = prim.as<PhysicsSphericalJoint>()) data.sphericalJoints.emplace_back(path, p);
    else if (auto *p = prim.as<PhysicsFixedJoint>()) data.fixedJoints.emplace_back(path, p);
    else if (auto *p = prim.as<PhysicsDistanceJoint>()) data.distanceJoints.emplace_back(path, p);
    else if (auto *p = prim.as<MjcActuator>()) data.actuators.emplace_back(path, p);
    else if (auto *p = prim.as<MjcTendon>()) data.tendons.emplace_back(path, p);
    else if (auto *p = prim.as<MjcKeyframe>()) data.keyframes.emplace_back(path, p);
  });

  std::ostringstream ss;
  ss << "{\n";

  // Scenes
  if (!data.scenes.empty()) {
    const auto &[path, scene] = data.scenes[0];
    ss << Indent(1, sp) << "\"physicsScene\": {\n";
    EmitKV(ss, 2, sp, "path", JsonStr(path));
    auto gd_opt = scene->gravityDirection.get_value();
    if (gd_opt.has_value()) {
      EmitKV(ss, 2, sp, "gravityDirection", JsonVec3f(gd_opt.value()));
    }
    auto gm_opt = scene->gravityMagnitude.get_value();
    if (gm_opt.has_value()) {
      EmitKV(ss, 2, sp, "gravityMagnitude", JsonNum(gm_opt.value()),
             scene->mjcScene.has_value() && options.include_mjc);
    }

    if (scene->mjcScene.has_value() && options.include_mjc) {
      EmitMjcSceneAPI(ss, scene->mjcScene.value(), 2, sp);
      ss << "\n";
    }

    ss << Indent(1, sp) << "},\n";
  }

  // Joints (all types combined)
  {
    bool has_joints = !data.revoluteJoints.empty() || !data.prismaticJoints.empty() ||
                      !data.sphericalJoints.empty() || !data.fixedJoints.empty() ||
                      !data.distanceJoints.empty();

    ss << Indent(1, sp) << "\"joints\": [";
    if (has_joints) {
      ss << "\n";
      bool first = true;

      auto emitJoint = [&](const std::string &path, const std::string &type,
                           const PhysicsJointBase &base) {
        if (!first) ss << ",\n";
        first = false;
        ss << Indent(2, sp) << "{\n";
        EmitKV(ss, 3, sp, "path", JsonStr(path));
        EmitKV(ss, 3, sp, "type", JsonStr(type));
        // body0/body1 relationships
        auto b0 = RelTargetStr(base.body0);
        auto b1 = RelTargetStr(base.body1);
        if (!b0.empty()) EmitKV(ss, 3, sp, "body0", JsonStr(b0));
        if (!b1.empty()) EmitKV(ss, 3, sp, "body1", JsonStr(b1));
        bool has_mjc = base.mjcJoint.has_value() && options.include_mjc;
        // jointEnabled, collisionEnabled, breakForce, breakTorque
        {
          auto je = base.jointEnabled.get_value();
          if (je.has_value()) EmitKV(ss, 3, sp, "jointEnabled", JsonBool(je.value()));
          auto be = base.breakForce.get_value();
          if (be.has_value()) EmitKV(ss, 3, sp, "breakForce", JsonNum(be.value()));
          auto bt = base.breakTorque.get_value();
          if (bt.has_value()) EmitKV(ss, 3, sp, "breakTorque", JsonNum(bt.value()), has_mjc);
        }
        if (has_mjc) {
          EmitMjcJointAPI(ss, base.mjcJoint.value(), 3, sp);
          ss << "\n";
        }
        ss << Indent(2, sp) << "}";
      };

      for (const auto &[p, j] : data.revoluteJoints) emitJoint(p, "PhysicsRevoluteJoint", *j);
      for (const auto &[p, j] : data.prismaticJoints) emitJoint(p, "PhysicsPrismaticJoint", *j);
      for (const auto &[p, j] : data.sphericalJoints) emitJoint(p, "PhysicsSphericalJoint", *j);
      for (const auto &[p, j] : data.fixedJoints) emitJoint(p, "PhysicsFixedJoint", *j);
      for (const auto &[p, j] : data.distanceJoints) emitJoint(p, "PhysicsDistanceJoint", *j);

      ss << "\n" << Indent(1, sp);
    }
    ss << "],\n";
  }

  // Actuators
  ss << Indent(1, sp) << "\"actuators\": [";
  if (!data.actuators.empty()) {
    ss << "\n";
    for (size_t i = 0; i < data.actuators.size(); ++i) {
      const auto &[path, act] = data.actuators[i];
      ss << Indent(2, sp) << "{\n";
      EmitKV(ss, 3, sp, "path", JsonStr(path));
      auto tgt = RelTargetStr(act->target);
      if (!tgt.empty()) EmitKV(ss, 3, sp, "target", JsonStr(tgt));
      EmitKV(ss, 3, sp, "group", JsonNum(act->group.get_value()));
      EmitKV(ss, 3, sp, "dynType", JsonStr(act->dynType.get_value().str()));
      EmitKV(ss, 3, sp, "gainType", JsonStr(act->gainType.get_value().str()));
      EmitKV(ss, 3, sp, "biasType", JsonStr(act->biasType.get_value().str()));
      EmitKV(ss, 3, sp, "ctrlLimited", JsonStr(act->ctrlLimited.get_value().str()));
      EmitKV(ss, 3, sp, "forceLimited", JsonStr(act->forceLimited.get_value().str()));
      EmitKV(ss, 3, sp, "actLimited", JsonStr(act->actLimited.get_value().str()));
      EmitKV(ss, 3, sp, "forceRange_min", JsonNum(act->forceRange_min.get_value()));
      EmitKV(ss, 3, sp, "forceRange_max", JsonNum(act->forceRange_max.get_value()));
      EmitOptionalArray(ss, 3, sp, "gear", act->gear);
      EmitOptionalArray(ss, 3, sp, "gainPrm", act->gainPrm);
      EmitOptionalArray(ss, 3, sp, "biasPrm", act->biasPrm);
      EmitKV(ss, 3, sp, "actDim", JsonNum(act->actDim.get_value()));
      EmitKV(ss, 3, sp, "inheritRange", JsonNum(act->inheritRange.get_value()), false);
      ss << Indent(2, sp) << "}";
      if (i + 1 < data.actuators.size()) ss << ",";
      ss << "\n";
    }
    ss << Indent(1, sp);
  }
  ss << "],\n";

  // Tendons
  ss << Indent(1, sp) << "\"tendons\": [";
  if (!data.tendons.empty()) {
    ss << "\n";
    for (size_t i = 0; i < data.tendons.size(); ++i) {
      const auto &[path, t] = data.tendons[i];
      ss << Indent(2, sp) << "{\n";
      EmitKV(ss, 3, sp, "path", JsonStr(path));
      EmitKV(ss, 3, sp, "type", JsonStr(t->type.get_value().str()));
      EmitKV(ss, 3, sp, "group", JsonNum(t->group.get_value()));
      EmitKV(ss, 3, sp, "limited", JsonStr(t->limited.get_value().str()));
      EmitKV(ss, 3, sp, "range_min", JsonNum(t->range_min.get_value()));
      EmitKV(ss, 3, sp, "range_max", JsonNum(t->range_max.get_value()));
      EmitKV(ss, 3, sp, "stiffness", JsonNum(t->stiffness.get_value()));
      EmitKV(ss, 3, sp, "damping", JsonNum(t->damping.get_value()));
      EmitKV(ss, 3, sp, "armature", JsonNum(t->armature.get_value()));
      EmitKV(ss, 3, sp, "frictionloss", JsonNum(t->frictionloss.get_value()));
      EmitKV(ss, 3, sp, "margin", JsonNum(t->margin.get_value()));
      EmitOptionalArray(ss, 3, sp, "springlength", t->springlength);
      EmitOptionalArray(ss, 3, sp, "solreflimit", t->solreflimit);
      EmitOptionalArray(ss, 3, sp, "solimplimit", t->solimplimit);
      EmitOptionalArray(ss, 3, sp, "solreffriction", t->solreffriction);
      EmitOptionalArray(ss, 3, sp, "solimpfriction", t->solimpfriction, false);
      ss << Indent(2, sp) << "}";
      if (i + 1 < data.tendons.size()) ss << ",";
      ss << "\n";
    }
    ss << Indent(1, sp);
  }
  ss << "],\n";

  // Keyframes
  ss << Indent(1, sp) << "\"keyframes\": [";
  if (!data.keyframes.empty()) {
    ss << "\n";
    for (size_t i = 0; i < data.keyframes.size(); ++i) {
      const auto &[path, kf] = data.keyframes[i];
      ss << Indent(2, sp) << "{\n";
      EmitKV(ss, 3, sp, "path", JsonStr(path));
      EmitKV(ss, 3, sp, "name", JsonStr(kf->name));
      EmitOptionalArray(ss, 3, sp, "qpos", kf->qpos);
      EmitOptionalArray(ss, 3, sp, "qvel", kf->qvel);
      EmitOptionalArray(ss, 3, sp, "act", kf->act);
      EmitOptionalArray(ss, 3, sp, "ctrl", kf->ctrl);
      EmitOptionalArray(ss, 3, sp, "mpos", kf->mpos);
      EmitOptionalArray(ss, 3, sp, "mquat", kf->mquat, false);
      ss << Indent(2, sp) << "}";
      if (i + 1 < data.keyframes.size()) ss << ",";
      ss << "\n";
    }
    ss << Indent(1, sp);
  }
  ss << "]\n";

  ss << "}\n";

  *json_str = ss.str();
  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
