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

constexpr int32_t kMjcfDefaultCondim = 3;
constexpr double kMjcfDefaultSolmix = 1.0;
constexpr double kMjcfDefaultMargin = 0.0;
constexpr double kMjcfDefaultGap = 0.0;
constexpr int32_t kLegacyUrdfCollisionGroup = 3;
constexpr int32_t kLegacyUrdfVisualGroup = 2;
constexpr int32_t kDefaultNewtonMaxHullVertices = 64;


void AddCollisionAPIs(tn::PrimSpec *prim, bool mesh_collision,
                      const Json &src, bool mjcf_source) {
  AddAPIs(prim, {"PhysicsCollisionAPI", "MjcCollisionAPI", "MjcImageableAPI",
                 "NewtonCollisionAPI"});
  if (mesh_collision) {
    AddAPIs(prim, {"PhysicsMeshCollisionAPI", "MjcMeshCollisionAPI",
                   "NewtonMeshCollisionAPI"});
  }
  Set(prim, "physics:collisionEnabled", tn::Value(true), "bool");
  if (mesh_collision) {
    // Default approximation is `convexHull`, matching mujoco-usd-converter.
    SetToken(prim, "physics:approximation",
             JsonString(src, "approximation", "convexHull"), true);
    SetToken(prim, "mjc:inertia", "legacy", true);
  }
  if (mjcf_source) {
    int32_t ivalue = 0;
    double dvalue = 0.0;
    if (ReadIntFrom(src, "mjc", "group", &ivalue)) {
      Set(prim, "mjc:group", tn::Value(ivalue), "int", true);
    } else if (src.contains("group")) {
      Set(prim, "mjc:group", tn::Value(JsonInt(src, "group", 0)), "int", true);
    }
    if (ReadIntFrom(src, "mjc", "condim", &ivalue)) {
      Set(prim, "mjc:condim", tn::Value(ivalue), "int", true);
    } else if (src.contains("condim")) {
      Set(prim, "mjc:condim",
          tn::Value(JsonInt(src, "condim", kMjcfDefaultCondim)), "int", true);
    }
    if (ReadIntFrom(src, "mjc", "geomContype", &ivalue) ||
        ReadIntFrom(src, "mjc", "contype", &ivalue)) {
      Set(prim, "mjc:geomContype", tn::Value(ivalue), "int", true);
    }
    if (ReadIntFrom(src, "mjc", "geomConaffinity", &ivalue) ||
        ReadIntFrom(src, "mjc", "conaffinity", &ivalue)) {
      Set(prim, "mjc:geomConaffinity", tn::Value(ivalue), "int", true);
    }
    if (ReadIntFrom(src, "mjc", "priority", &ivalue)) {
      Set(prim, "mjc:priority", tn::Value(ivalue), "int", true);
    }
    if (ReadNumberFrom(src, "mjc", "solmix", &dvalue)) {
      Set(prim, "mjc:solmix", tn::Value(dvalue), "double", true);
    }
    if (ReadNumberFrom(src, "mjc", "margin", &dvalue)) {
      Set(prim, "mjc:margin", tn::Value(dvalue), "double", true);
      Set(prim, "newton:contactMargin",
          tn::Value(static_cast<float>(dvalue)), "float");
    } else if (ReadNumberFrom(src, "newton", "contactMargin", &dvalue)) {
      Set(prim, "newton:contactMargin",
          tn::Value(static_cast<float>(dvalue)), "float");
    }
    if (ReadNumberFrom(src, "mjc", "gap", &dvalue)) {
      Set(prim, "mjc:gap", tn::Value(dvalue), "double", true);
      Set(prim, "newton:contactGap", tn::Value(static_cast<float>(dvalue)),
          "float");
    } else if (ReadNumberFrom(src, "newton", "contactGap", &dvalue)) {
      Set(prim, "newton:contactGap", tn::Value(static_cast<float>(dvalue)),
          "float");
    }
    std::vector<double> values = JsonDoublesFrom(src, "mjc", "geomFriction");
    if (values.empty()) values = JsonDoublesFrom(src, "mjc", "friction");
    if (!values.empty()) SetDoubles(prim, "mjc:geomFriction", values, true);
    values = JsonDoublesFrom(src, "mjc", "solref");
    if (!values.empty()) SetDoubles(prim, "mjc:solref", values, true);
    values = JsonDoublesFrom(src, "mjc", "solimp");
    if (!values.empty()) SetDoubles(prim, "mjc:solimp", values, true);
    values = JsonDoublesFrom(src, "mjc", "geomSize");
    if (!values.empty()) SetDoubles(prim, "mjc:geomSize", values, true);
  } else {
    Set(prim, "mjc:group",
        tn::Value(JsonInt(src, "group", kLegacyUrdfCollisionGroup)), "int",
        true);
    Set(prim, "mjc:condim",
        tn::Value(JsonInt(src, "condim", kMjcfDefaultCondim)), "int", true);
    Set(prim, "mjc:solmix",
        tn::Value(JsonNumberFrom(src, "mjc", "solmix", kMjcfDefaultSolmix)),
        "double", true);
    const double margin =
        JsonNumberFrom(src, "mjc", "margin", kMjcfDefaultMargin);
    Set(prim, "mjc:margin", tn::Value(margin), "double", true);
    Set(prim, "newton:contactMargin",
        tn::Value(static_cast<float>(
            JsonNumberFrom(src, "newton", "contactMargin", margin))),
        "float");
    Set(prim, "newton:contactGap",
        tn::Value(static_cast<float>(
            JsonNumberFrom(src, "newton", "contactGap", kMjcfDefaultGap))),
        "float");
  }
  if (mesh_collision) {
    Set(prim, "newton:maxHullVertices",
        tn::Value(JsonIntFrom(src, "newton", "maxHullVertices",
                              kDefaultNewtonMaxHullVertices)),
        "int", true);
  }
  // next-only: MuJoCo contact filter bits under their MJCF attribute names
  // (the legacy converter only authors mjc:geomContype/geomConaffinity).
  const Json *mjc = JsonObject(src, "mjc");
  auto contact_bits = [&](const char *key) {
    double fallback = 1.0;
    ReadNumber(src, key, &fallback);
    double value = fallback;
    if (mjc) ReadNumber(*mjc, key, &value);
    return static_cast<int32_t>(value);
  };
  Set(prim, "mjc:contype", tn::Value(contact_bits("contype")), "int", true);
  Set(prim, "mjc:conaffinity", tn::Value(contact_bits("conaffinity")), "int",
      true);
  // `purpose=guide` hides the collider from default renders but keeps it
  // discoverable to schema-aware consumers (mujoco-usd-converter applies
  // the same to non-visual MuJoCo geom groups).
  SetToken(prim, "purpose", "guide", true);
}

bool AddMeshFromJson(tn::Layer *layer, const std::string &link_path,
                     std::set<std::string> *siblings, const Json &mesh_json,
                     const std::map<std::string, URDFMeshBuffer> *mesh_buffers,
                     const std::string &fallback_name, bool collision,
                     bool mjcf_source, std::string *warn, std::string *err) {
  const Json *geom = JsonObject(mesh_json, "geometry");
  if (!geom) geom = &mesh_json;
  std::vector<float> json_positions;
  std::vector<float> json_normals;
  std::vector<float> json_uvs;
  std::vector<int32_t> json_indices;
  const std::vector<float> *positions = &json_positions;
  const std::vector<float> *normals = &json_normals;
  const std::vector<float> *uvs = &json_uvs;
  const std::vector<int32_t> *source_indices = &json_indices;
  const std::string mesh_ref = JsonString(mesh_json, "meshRef");
  if (!mesh_ref.empty()) {
    auto found = mesh_buffers ? mesh_buffers->find(mesh_ref)
                              : std::map<std::string, URDFMeshBuffer>::const_iterator();
    if (!mesh_buffers || found == mesh_buffers->end()) {
      AppendWarn(warn, "Skipping mesh `" + fallback_name + "`: meshRef `" +
                           mesh_ref + "` was not registered.\n");
      return true;
    }
    positions = &found->second.positions;
    normals = &found->second.normals;
    uvs = &found->second.uvs;
    source_indices = &found->second.indices;
  } else {
    json_positions = JsonFloats(*geom, "positions");
    json_normals = JsonFloats(*geom, "normals");
    json_uvs = JsonFloats(*geom, "uvs");
    json_indices = JsonInts(*geom, "indices");
  }
  if (positions->size() < 9 || positions->size() % 3 != 0) {
    AppendWarn(warn, "Skipping mesh `" + fallback_name +
                         "`: positions must contain at least 3 points.\n");
    return true;
  }
  const size_t point_count = positions->size() / 3;
  std::vector<int32_t> indices;
  if (!source_indices->empty()) {
    indices = *source_indices;
  } else {
    indices.resize(point_count);
    for (size_t i = 0; i < point_count; ++i) indices[i] = static_cast<int32_t>(i);
  }
  if (indices.size() % 3 != 0) {
    AppendWarn(warn, "Skipping mesh `" + fallback_name +
                         "`: indices must be triangles.\n");
    return true;
  }
  bool identity_indices = indices.size() == point_count;
  for (size_t i = 0; i < indices.size(); ++i) {
    const int32_t index = indices[i];
    if (index < 0 || static_cast<size_t>(index) >= point_count) {
      AppendWarn(warn, "Skipping mesh `" + fallback_name +
                           "`: index out of range.\n");
      return true;
    }
    if (identity_indices && static_cast<size_t>(index) != i) {
      identity_indices = false;
    }
  }

  const std::string name = ChildName(
      Sanitize(JsonString(mesh_json, "name", fallback_name), fallback_name),
      siblings);
  tn::PrimSpec *prim = Define(layer, link_path + "/" + name, "Mesh");
  if (!prim) {
    if (err) *err = "Failed to add mesh `" + name + "`";
    return false;
  }
  SetToken(prim, "subdivisionScheme", "none", true);
  Set(prim, "points",
      tn::Value::MakeFloatCompArray(std::vector<float>(*positions),
                                    tn::TypeId::Point3f, 3),
      "point3f[]");
  Set(prim, "faceVertexCounts",
      tn::Value::MakeIntArray(std::vector<int32_t>(indices.size() / 3, 3)),
      "int[]");
  // Non-identity indexing: per-vertex normals/UVs are expanded to
  // faceVarying so the authored data stays consistent with the topology.
  if (normals->size() == positions->size()) {
    std::vector<float> ns;
    if (identity_indices) {
      ns = *normals;
    } else {
      ns.reserve(indices.size() * 3);
      for (int32_t index : indices) {
        const size_t src = static_cast<size_t>(index) * 3;
        ns.insert(ns.end(), normals->begin() + src, normals->begin() + src + 3);
      }
    }
    Set(prim, "normals",
        tn::Value::MakeFloatCompArray(std::move(ns), tn::TypeId::Normal3f, 3),
        "normal3f[]");
    SetInterpolation(prim, "normals", identity_indices ? "vertex" : "faceVarying");
  }
  if (uvs->size() == point_count * 2) {
    std::vector<float> st;
    if (identity_indices) {
      st = *uvs;
    } else {
      st.reserve(indices.size() * 2);
      for (int32_t index : indices) {
        const size_t src = static_cast<size_t>(index) * 2;
        st.insert(st.end(), uvs->begin() + src, uvs->begin() + src + 2);
      }
    }
    Set(prim, "primvars:st",
        tn::Value::MakeFloatCompArray(std::move(st), tn::TypeId::Texcoord2f, 2),
        "texCoord2f[]");
    SetInterpolation(prim, "primvars:st",
                     identity_indices ? "vertex" : "faceVarying");
  }
  Set(prim, "faceVertexIndices", tn::Value::MakeIntArray(std::move(indices)),
      "int[]");
  SetTransformFromJson(prim, mesh_json);

  if (collision) {
    AddCollisionAPIs(prim, true, mesh_json, mjcf_source);
    return true;
  }
  AddAPI(prim, "MjcImageableAPI");
  // Bind a UsdShade material (MJCF <geom material=..> ->
  // /World/Materials/<m>); MJCF material names are unique, so the sanitized
  // path is deterministic and the material prim is emitted later.
  const std::string material = JsonString(mesh_json, "material");
  if (!material.empty()) {
    AddAPI(prim, "MaterialBindingAPI");
    SetRel(prim, "material:binding",
           {"/World/Materials/" + Sanitize(material, "material")});
  }
  if (mjcf_source) {
    int32_t group = 0;
    if (ReadIntFrom(mesh_json, "mjc", "group", &group) ||
        mesh_json.contains("group")) {
      Set(prim, "mjc:group", tn::Value(JsonInt(mesh_json, "group", group)),
          "int", true);
    }
  } else {
    Set(prim, "mjc:group",
        tn::Value(JsonInt(mesh_json, "group", kLegacyUrdfVisualGroup)), "int",
        true);
  }
  return true;
}

bool AddNativeCollisionShapeFromJson(tn::Layer *layer,
                                     const std::string &link_path,
                                     std::set<std::string> *siblings,
                                     const Json &shape_json,
                                     const std::string &fallback_name,
                                     bool mjcf_source, std::string *warn,
                                     std::string *err) {
  const Json *shape_obj = JsonObject(shape_json, "shape");
  const Json &shape = shape_obj ? *shape_obj : shape_json;
  const std::string type = JsonString(shape, "type");
  if (type.empty()) {
    AppendWarn(warn, "Skipping collision shape `" + fallback_name +
                         "`: missing shape.type.\n");
    return true;
  }
  std::string usd_type;
  if (type == "box" || type == "cube") usd_type = "Cube";
  else if (type == "sphere") usd_type = "Sphere";
  else if (type == "cylinder") usd_type = "Cylinder";
  else if (type == "capsule") usd_type = "Capsule";
  else if (type == "plane") usd_type = "Plane";
  else {
    AppendWarn(warn, "Skipping collision shape `" + fallback_name +
                         "`: unsupported shape type `" + type + "`.\n");
    return true;
  }

  const std::string name = ChildName(
      Sanitize(JsonString(shape_json, "name", fallback_name), fallback_name),
      siblings);
  tn::PrimSpec *prim = Define(layer, link_path + "/" + name, usd_type);
  if (!prim) {
    if (err) *err = "Failed to add collision shape `" + name + "`";
    return false;
  }
  auto number = [&](const char *key, double fallback) {
    double value = fallback;
    ReadNumber(shape, key, &value);
    return value;
  };
  if (usd_type == "Cube") {
    Set(prim, "size", tn::Value(2.0), "double");
  } else if (usd_type == "Sphere") {
    Set(prim, "radius", tn::Value(number("radius", 0.5)), "double");
  } else if (usd_type == "Cylinder" || usd_type == "Capsule") {
    Set(prim, "radius", tn::Value(number("radius", 0.5)), "double");
    Set(prim, "height", tn::Value(number("height", 1.0)), "double");
    SetToken(prim, "axis", AxisFromToken(JsonString(shape, "axis", "Z")), true);
  } else {
    Set(prim, "width", tn::Value(number("width", 2.0)), "double");
    Set(prim, "length", tn::Value(number("length", 2.0)), "double");
    SetToken(prim, "axis", AxisFromToken(JsonString(shape, "axis", "Z")), true);
  }
  SetTransformFromJson(prim, shape_json);
  AddCollisionAPIs(prim, false, shape_json, mjcf_source);
  return true;
}

}  // namespace urdf_detail

} } }  // namespace lightusd::tydra::next
