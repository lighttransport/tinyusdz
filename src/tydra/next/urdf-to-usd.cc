// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - Present Light Transport Entertainment Inc.
// URDF/MJCF payload validation, authoring order, and stage publication.
#include "urdf-to-usd.hh"
#include "urdf-to-usd-internal.hh"
#include "next/layer/layer.hh"
#include "next/layer/prim-spec.hh"
#include "next/stage/stage.hh"
#include "next/types/value.hh"
#include "next/reader/obj-reader.hh"
#include <cmath>
#include <utility>

namespace lightusd { namespace tydra { namespace next {
using namespace urdf_detail;

static bool ConvertPayload(
    const ::lightusd::tydra::detail::URDFPayload &payload,
    const std::map<std::string, URDFMeshBuffer> *mesh_buffers,
    ::lightusd::next::Stage *out_stage, std::string *warn, std::string *err) {
  if (!out_stage) {
    if (err) *err = "Output Stage pointer is null";
    return false;
  }
  if (warn) warn->clear();
  if (err) err->clear();

  const Json &root = payload.Root();
  if (JsonString(root, "sourceFormat") == "obj") {
#if defined(LIGHTUSD_WITH_USDOBJ)
    tn::Layer obj_layer;
    if (!tn::ConvertOBJPayloadToLayer(root, &obj_layer, err)) return false;
    out_stage->SetRootLayer(std::move(obj_layer));
    return true;
#else
    if (err) *err = "OBJ support is disabled";
    return false;
#endif
  }
  const Json &links = payload.Array("links");
  const Json &joints = payload.Array("joints");
  const bool mjcf_source = payload.mjcf_source;
  bool no_physics = false;
  ReadBool(root, "noPhysics", &no_physics);

  tn::Layer layer;
  // Builders keep PrimSpec pointers while adding shader/subset children.
  // Reserve from structural payload records, never from numeric mesh lanes.
  size_t prim_capacity = 128;
  std::vector<const Json*> pending{&root};
  while (!pending.empty()) {
    const Json* object = pending.back();
    pending.pop_back();
    if (object->is_object()) {
      if (prim_capacity > size_t(UINT32_MAX) - 16) {
        if (err) *err = "Robot prim count limit exceeded";
        return false;
      }
      prim_capacity += 16;
      if (const auto* names = JsonArray(*object, "materialNames")) {
        if (names->size() > size_t(UINT32_MAX) - prim_capacity) {
          if (err) *err = "Robot material count limit exceeded";
          return false;
        }
        prim_capacity += names->size();
      }
      for (const auto& item : *object->object_items())
        if (item.value().is_object() || item.value().is_array())
          pending.push_back(&item.value());
    } else if (object->is_array()) {
      // Heterogeneous arrays may start with a scalar and contain records
      // later. Count those too, without retaining numeric mesh lanes.
      for (const auto& item : *object)
        if (item.is_object() || item.is_array()) pending.push_back(&item);
    }
  }
  layer.reserve(prim_capacity);
  layer.meta().defaultPrim = "World";
  const std::string up_axis = JsonString(root, "upAxis", "Y");
  const bool y_up = !(up_axis == "Z" || up_axis == "z");
  layer.meta().upAxis = y_up ? "Y" : "Z";
  layer.meta().upAxis_set = true;
  // MuJoCo authors in SI units (1 stage unit = 1 m / 1 kg).
  layer.meta().metersPerUnit = no_physics ? 0.01 : 1.0;
  layer.meta().metersPerUnit_set = !no_physics;
  layer.meta().kilogramsPerUnit = 1.0;
  layer.meta().kilogramsPerUnit_set = !no_physics;

  tn::PrimSpec *world = Define(&layer, "/World", "Xform");
  if (!world) {
    if (err) *err = "Failed to define /World";
    return false;
  }
  world->meta().kind() = "assembly";
  // URDF/MJCF sources are Z-up; a Y-up stage gets a single corrective root
  // rotation Rx(-90deg) so local-frame physics data rides along unchanged
  // (doc/usd-physics-upAxis.md).
  if (y_up && !no_physics) {
    Set(world, "xformOp:rotateX", tn::Value(-90.0), "double");
    Set(world, "xformOpOrder",
        tn::Value::MakeTokenArray(std::vector<std::string>{"xformOp:rotateX"}),
        "token[]", true);
  }

  if (!no_physics) AddPhysicsScene(&layer, root);
  Define(&layer, "/World/Links", "Xform");
  Define(&layer, "/World/Joints", "Xform");

  // --- Links ---------------------------------------------------------------
  std::set<std::string> child_links;
  for (const Json &joint : joints) {
    const std::string child = JsonString(joint, "child");
    if (!child.empty()) child_links.insert(child);
  }
  bool self_collision_enabled = true;
  if (const Json *newton = JsonObject(root, "newton")) {
    ReadBool(*newton, "selfCollisionEnabled", &self_collision_enabled);
  }
  NameMap link_names;
  std::set<std::string> used_links;
  for (size_t i = 0; i < links.size(); ++i) {
    const Json &link = links[i];
    const std::string source =
        JsonString(link, "name", "link_" + std::to_string(i));
    const std::string name = Unique(source, "link", &used_links);
    link_names[source] = name;
    const std::string path = "/World/Links/" + name;
    tn::PrimSpec *prim = Define(&layer, path, "Xform");
    if (!prim) {
      if (err) *err = "Failed to define link `" + name + "`";
      return false;
    }
    // A world-fixed link (e.g. the synthetic "world" link holding worldbody
    // floor/ground geoms) is a static collider, not a dynamic body.
    bool is_static = false;
    ReadBool(link, "static", &is_static);
    SetTransformFromJson(prim, link);
    if (!no_physics) {
      AddAPIs(prim, {"PhysicsRigidBodyAPI", "PhysicsMassAPI"});
      Set(prim, "physics:rigidBodyEnabled", tn::Value(!is_static), "bool");
      Set(prim, "physics:startsAsleep", tn::Value(false), "bool");
      bool flag = false;
      if (ReadBool(link, "mocap", &flag) && flag) {
        Set(prim, "mjc:mocap", tn::Value(true), "bool", true);
      }
      // MuJoCo <freejoint>: distinguishes a floating-base articulation root
      // from a fixed (anchored) parentless base.
      flag = false;
      ReadBool(link, "floating", &flag);
      if (flag) Set(prim, "mjc:freeJoint", tn::Value(true), "bool", true);
      bool articulation_root = false;
      ReadBool(link, "articulationRoot", &articulation_root);
      if (!is_static && (!child_links.count(source) || articulation_root)) {
        AddAPIs(prim,
                {"PhysicsArticulationRootAPI", "NewtonArticulationRootAPI"});
        Set(prim, "newton:selfCollisionEnabled",
            tn::Value(self_collision_enabled), "bool");
      }
      if (const Json* inertial = JsonObject(link, "inertial")) {
        float mass = 0.0f;
        if (ReadNumber(*inertial, "mass", &mass) && mass > 0.0f) {
          Set(prim, "physics:mass", tn::Value(mass), "float");
        }
        const std::vector<float> com = JsonFloats(*inertial, "centerOfMass");
        if (com.size() >= 3)
          SetPoint3(prim, "physics:centerOfMass", com, false);
        // A full inertia tensor is diagonalized into principal moments +
        // principal-axes rotation (the lossless USD representation).
        const std::vector<double> full = JsonDoubles(*inertial, "fullInertia");
        if (full.size() >= 6) {
          // [Ixx, Iyy, Izz, Ixy, Ixz, Iyz] -> symmetric 3x3.
          const double tensor[3][3] = {{full[0], full[3], full[4]},
                                       {full[3], full[1], full[5]},
                                       {full[4], full[5], full[2]}};
          double eval[3];
          double evec[3][3];
          JacobiEigenSymmetric3(tensor, eval, evec);
          Set(prim, "physics:diagonalInertia",
              tn::Value::MakeFloat3(static_cast<float>(eval[0]),
                                    static_cast<float>(eval[1]),
                                    static_cast<float>(eval[2])),
              "float3");
          Set(prim, "physics:principalAxes", RotationMatrixToQuatf(evec),
              "quatf");
        } else {
          const std::vector<float> inertia =
              JsonFloats(*inertial, "diagonalInertia");
          if (inertia.size() >= 3) {
            Set(prim, "physics:diagonalInertia",
                tn::Value::MakeFloat3(inertia[0], inertia[1], inertia[2]),
                "float3");
          }
        }
        const auto principal = JsonFloats(*inertial, "principalAxes");
        if (full.size() < 6 && principal.size() == 4)
          Set(prim, "physics:principalAxes",
              tn::Value::MakeQuatf(principal[0], principal[1], principal[2],
                                   principal[3]),
              "quatf");
      }

    }  // physics-bearing link

    // Visuals and collisions share the link's child namespace.
    std::set<std::string> siblings;
    if (const Json *visuals = JsonArray(link, "visuals")) {
      size_t n = 0;
      for (const Json &visual : *visuals) {
        if (!AddMeshFromJson(&layer, path, &siblings, visual, mesh_buffers,
                             "visual_" + std::to_string(n++), false,
                             mjcf_source, warn, err)) {
          return false;
        }
      }
    }
    if (const Json *collisions = JsonArray(link, "collisions")) {
      size_t n = 0;
      for (const Json &collision : *collisions) {
        const std::string fallback = "collision_" + std::to_string(n++);
        const bool ok =
            JsonObject(collision, "shape")
                ? AddNativeCollisionShapeFromJson(&layer, path, &siblings,
                                                  collision, fallback,
                                                  mjcf_source, warn, err)
                : AddMeshFromJson(&layer, path, &siblings, collision,
                                  mesh_buffers, fallback, true, mjcf_source,
                                  warn, err);
        if (!ok) return false;
      }
    }
  }

  // MJCF <contact><exclude> -> physics:filteredPairs on the first body.
  if (const Json *pairs = JsonArray(root, "filteredPairs")) {
    std::map<std::string, std::vector<std::string>> filtered;
    for (const Json &pair : *pairs) {
      if (!pair.is_object()) continue;
      const std::string body1 = JsonString(pair, "body1");
      const std::string body2 = JsonString(pair, "body2");
      auto it1 = link_names.find(body1);
      auto it2 = link_names.find(body2);
      if (it1 == link_names.end() || it2 == link_names.end()) {
        AppendWarn(warn, "Skipping filtered pair `" + body1 + "` / `" + body2 +
                             "`: link was not exported.\n");
        continue;
      }
      filtered[body1].push_back("/World/Links/" + it2->second);
    }
    for (const auto &entry : filtered) {
      tn::PrimSpec *prim = layer.prim_at_path_mutable(
          "/World/Links/" + link_names[entry.first]);
      if (!prim) continue;
      AddAPI(prim, "PhysicsFilteredPairsAPI");
      SetRel(prim, "physics:filteredPairs", entry.second);
    }
  }

  // --- Joints --------------------------------------------------------------
  NameMap joint_names;
  std::vector<std::string> joint_usd_names(joints.size());
  std::set<std::string> used_joints;
  for (size_t i = 0; i < joints.size(); ++i) {
    const std::string source =
        JsonString(joints[i], "name", "joint_" + std::to_string(i));
    joint_usd_names[i] = Unique(source, "joint", &used_joints);
    joint_names[source] = joint_usd_names[i];
  }
  constexpr double kRadToDeg = 57.2957795130823208768;
  for (size_t i = 0; i < joints.size(); ++i) {
    const Json &joint = joints[i];
    const std::string type = JsonString(joint, "type", "fixed");
    const std::string parent = JsonString(joint, "parent");
    const std::string child = JsonString(joint, "child");
    if (!link_names.count(parent) || !link_names.count(child)) {
      AppendWarn(warn, "Skipping joint `" + JsonString(joint, "name", "joint") +
                           "`: parent or child link was not exported.\n");
      continue;
    }
    const std::string &name = joint_usd_names[i];
    const std::string axis = AxisToken(joint);
    // localRot0 is authoritative for the joint frame rotation; originMatrix
    // contributes only its translation, so surface a dropped rotation.
    if (!joint.contains("localRot0")) {
      const std::vector<double> m = JsonDoubles(joint, "originMatrix");
      const size_t idx[9] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
      const double expect[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
      bool rotated = false;
      for (size_t k = 0; m.size() == 16 && k < 9; k++) {
        rotated = rotated || std::abs(m[idx[k]] - expect[k]) > 1e-6;
      }
      if (rotated) {
        AppendWarn(warn, "Joint `" + name +
                             "` has a rotation in originMatrix but no "
                             "localRot0; the rotation is ignored (supply "
                             "localRot0).\n");
      }
    }
    std::string usd_type = "PhysicsFixedJoint";
    if (type == "revolute" || type == "continuous") {
      usd_type = "PhysicsRevoluteJoint";
    } else if (type == "prismatic") {
      usd_type = "PhysicsPrismaticJoint";
    } else if (type == "spherical") {
      // MuJoCo ball joint; cone limits are not carried in the JSON.
      usd_type = "PhysicsSphericalJoint";
    } else if (type != "fixed") {
      AppendWarn(warn, "Joint `" + name + "` type `" + type +
                           "` is exported as PhysicsFixedJoint.\n");
    }
    tn::PrimSpec *prim = Define(&layer, "/World/Joints/" + name, usd_type);
    if (!prim) {
      if (err) *err = "Failed to add joint `" + name + "`";
      return false;
    }
    AssignJointBase(prim, joint, link_names[parent], link_names[child],
                    joint_names);
    if (usd_type != "PhysicsFixedJoint") {
      SetToken(prim, "physics:axis", axis, true);
    }
    // Limits: revolute in degrees, prismatic in meters (continuous joints
    // are unlimited).
    const Json *limit = JsonObject(joint, "limit");
    const bool revolute_limit = type == "revolute" && limit;
    const bool prismatic_limit = type == "prismatic" && limit;
    if (revolute_limit || prismatic_limit) {
      const double scale = revolute_limit ? kRadToDeg : 1.0;
      double lower = 0.0;
      double upper = 0.0;
      if (ReadNumber(*limit, "lower", &lower)) {
        Set(prim, "physics:lowerLimit",
            tn::Value(static_cast<float>(lower * scale)), "float", true);
      }
      if (ReadNumber(*limit, "upper", &upper)) {
        Set(prim, "physics:upperLimit",
            tn::Value(static_cast<float>(upper * scale)), "float", true);
      }
      const std::string dof = JointDofName(axis, revolute_limit);
      AddAPI(prim, "PhysicsLimitAPI:" + dof);
      Set(prim, "physics:limit:" + dof + ":low",
          tn::Value(static_cast<float>(lower * scale)), "float");
      Set(prim, "physics:limit:" + dof + ":high",
          tn::Value(static_cast<float>(upper * scale)), "float");
    }
  }

  // --- MJCF / Newton scopes, in the legacy /World child order ---------------
  Scope actuators_scope(&layer, "Actuators");
  {
    std::set<std::string> siblings;
    const Json &items = payload.Array("actuators");
    for (size_t i = 0; i < items.size(); ++i) {
      AddNewtonActuatorFromJson(&actuators_scope, &siblings, items[i],
                                joint_names, i, warn);
    }
  }

  // Site names are resolved up front: spatial tendons route through them
  // although the Sites scope follows Tendons/Equalities.
  const Json &sites = payload.Array("sites");
  NameMap site_names;
  std::vector<std::string> site_usd_names(sites.size());
  {
    std::set<std::string> used;
    for (size_t i = 0; i < sites.size(); ++i) {
      const std::string source =
          JsonString(sites[i], "name", "site_" + std::to_string(i));
      site_usd_names[i] = Unique(source, "site", &used);
      site_names[source] = site_usd_names[i];
    }
  }

  NameMap tendon_names;
  Scope tendons_scope(&layer, "Tendons");
  {
    std::set<std::string> siblings;
    const Json &items = payload.Array("tendons");
    for (size_t i = 0; i < items.size(); ++i) {
      AddMjcTendonFromJson(&tendons_scope, &siblings, items[i], joint_names,
                           site_names, &tendon_names, i, warn);
    }
  }

  Scope equalities_scope(&layer, "Equalities");
  {
    std::set<std::string> siblings;
    const Json &items = payload.Array("equalities");
    for (size_t i = 0; i < items.size(); ++i) {
      AddMjcEqualityFromJson(&equalities_scope, &siblings, items[i],
                             link_names, joint_names, i);
    }
  }

  Scope sites_scope(&layer, "Sites");
  for (size_t i = 0; i < sites.size(); ++i) {
    AddMjcSiteFromJson(&sites_scope, sites[i], site_usd_names[i]);
  }

  Scope mjc_actuators_scope(&layer, "MjcActuators");
  {
    std::set<std::string> used;
    const Json &items = payload.Array("mjcActuators");
    for (size_t i = 0; i < items.size(); ++i) {
      AddMjcActuatorFromJson(&mjc_actuators_scope, &used, items[i],
                             joint_names, tendon_names, site_names, link_names,
                             i);
    }
  }

  Scope keyframes_scope(&layer, "Keyframes");
  {
    std::set<std::string> used;
    const Json &items = payload.Array("keyframes");
    for (size_t i = 0; i < items.size(); ++i) {
      AddMjcKeyframeFromJson(&keyframes_scope, &used, items[i], i);
    }
  }

  Scope lights_scope(&layer, "Lights");
  {
    std::set<std::string> used;
    const Json &items = payload.Array("lights");
    for (size_t i = 0; i < items.size(); ++i) {
      AddLightFromJson(&lights_scope, &used, items[i], i);
    }
  }

  Scope cameras_scope(&layer, "Cameras");
  {
    std::set<std::string> used;
    const Json &items = payload.Array("cameras");
    for (size_t i = 0; i < items.size(); ++i) {
      AddCameraFromJson(&cameras_scope, &used, items[i], i);
    }
  }

  // Geoms bind via /World/Materials/<sanitized-name> (see AddMeshFromJson).
  Scope materials_scope(&layer, "Materials");
  {
    std::set<std::string> used;
    const Json &items = payload.Array("materials");
    for (size_t i = 0; i < items.size(); ++i) {
      AddMaterialFromJson(&materials_scope, &used, items[i], i);
    }
  }

  Scope sensors_scope(&layer, "Sensors");
  {
    std::set<std::string> used;
    const Json &items = payload.Array("sensors");
    for (size_t i = 0; i < items.size(); ++i) {
      AddMjcSensorFromJson(&sensors_scope, &used, items[i], i);
    }
  }

  Scope contacts_scope(&layer, "Contacts");
  {
    std::set<std::string> used;
    const Json &items = payload.Array("contactPairs");
    for (size_t i = 0; i < items.size(); ++i) {
      AddContactPairFromJson(&contacts_scope, &used, items[i], i);
    }
  }

  // MJCF <custom><numeric|text> -> /World/MjcCustom (model metadata / MJX
  // knobs), authored only when it carries at least one entry.
  if (const Json *custom = JsonObject(root, "custom")) {
    std::vector<std::pair<std::string, const Json *>> numeric, text;
    if (const Json *items = JsonArray(*custom, "numeric")) {
      for (const Json &n : *items) {
        const std::string nm = JsonString(n, "name");
        if (!nm.empty()) numeric.emplace_back(nm, &n);
      }
    }
    if (const Json *items = JsonArray(*custom, "text")) {
      for (const Json &t : *items) {
        const std::string nm = JsonString(t, "name");
        if (!nm.empty()) text.emplace_back(nm, &t);
      }
    }
    if (!numeric.empty() || !text.empty()) {
      tn::PrimSpec *prim = Define(&layer, "/World/MjcCustom", "Xform");
      for (const auto &entry : numeric) {
        SetDoubles(prim, "mjc:custom:" + entry.first,
                   JsonDoubles(*entry.second, "data"));
      }
      for (const auto &entry : text) {
        SetToken(prim, "mjc:customtext:" + entry.first,
                 JsonString(*entry.second, "data"));
      }
    }
  }

  // MJCF <extension><plugin><instance><config> -> /World/MjcPlugins; the
  // instance config (e.g. a PID actuator's kp/ki/kd) is referenced by an
  // MjcActuator's mjc:instance.
  if (const Json *plugins = JsonArray(root, "plugins")) {
    std::vector<std::pair<std::string, std::string>> tokens;
    for (const Json &p : *plugins) {
      const std::string inst = JsonString(p, "instance");
      if (inst.empty()) continue;
      const std::string plugin_id = JsonString(p, "plugin");
      if (!plugin_id.empty()) {
        tokens.emplace_back("mjc:plugin:" + inst + ":plugin", plugin_id);
      }
      const Json *config = JsonObject(p, "config");
      const auto *members = config ? config->object_items() : nullptr;
      if (!members) continue;
      for (const auto &member : *members) {
        if (!member.value().is_string()) continue;
        tokens.emplace_back("mjc:plugin:" + inst + ":config:" + member.key,
                            member.value().get<std::string>());
      }
    }
    if (!tokens.empty()) {
      tn::PrimSpec *prim = Define(&layer, "/World/MjcPlugins", "Xform");
      for (const auto &entry : tokens) SetToken(prim, entry.first, entry.second);
    }
  }

  layer.finalize();
  tn::Stage stage;
  stage.SetRootLayer(std::move(layer));
  *out_stage = std::move(stage);
  return true;
}

bool ConvertURDFJsonToUSDStage(const std::string& robot_json,
    const std::map<std::string, URDFMeshBuffer>* mesh_buffers,
    ::lightusd::next::Stage* out_stage, std::string* warn, std::string* err) {
  ::lightusd::tydra::detail::URDFPayload payload;
  if (!::lightusd::tydra::detail::URDFPayload::Parse(robot_json, &payload, err)) return false;
  return ConvertPayload(payload, mesh_buffers, out_stage, warn, err);
}
bool ConvertURDFPayloadToUSDStage(const ::lightusd::minijson::Value& value,
    ::lightusd::next::Stage* out_stage, std::string* warn, std::string* err) {
  ::lightusd::tydra::detail::URDFPayload payload;
  if (!::lightusd::tydra::detail::URDFPayload::Borrow(value, &payload, err)) return false;
  return ConvertPayload(payload, nullptr, out_stage, warn, err);
}

bool ConvertURDFJsonToUSDStage(const std::string &robot_json,
                               ::lightusd::next::Stage *out_stage,
                               std::string *warn, std::string *err) {
  return ConvertURDFJsonToUSDStage(robot_json, nullptr, out_stage, warn, err);
}


} } }  // namespace lightusd::tydra::next
