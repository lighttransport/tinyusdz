// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present Light Transport Entertainment Inc.

#include "tydra/urdf-to-usd.hh"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/attribute.hh"
#include "core/composition-types.hh"
#include "core/path.hh"
#include "core/prim-enums.hh"
#include "core/prim.hh"
#include "core/property.hh"
#include "core/relationship.hh"
#include "core/xform-op.hh"
#include "mjcPhysics.hh"
#include "stage.hh"
#include "usdGeom.hh"
#include "usdPhysics.hh"
#include "value-types.hh"
#include "xform.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {
namespace tydra {
namespace {

std::string SanitizeUSDIdentifier(const std::string &name,
                                  const std::string &fallback) {
  std::string out;
  out.reserve(name.empty() ? fallback.size() : name.size());
  for (char c : name) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '_') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    out = fallback;
  }
  if (!(std::isalpha(static_cast<unsigned char>(out[0])) || out[0] == '_')) {
    out.insert(out.begin(), '_');
  }
  return out;
}

std::string UniqueUSDIdentifier(const std::string &name,
                                std::set<std::string> &used,
                                const std::string &fallback) {
  const std::string base = SanitizeUSDIdentifier(name, fallback);
  std::string candidate = base;
  int suffix = 1;
  while (used.count(candidate)) {
    candidate = base + "_" + std::to_string(suffix++);
  }
  used.insert(candidate);
  return candidate;
}

bool JsonNumber(const nlohmann::json &j, const char *key, double *out) {
  if (!out || !j.is_object() || !j.contains(key) || !j.at(key).is_number()) {
    return false;
  }
  *out = j.at(key).get<double>();
  return true;
}

bool JsonNumber(const nlohmann::json &j, const char *key, float *out) {
  double v = 0.0;
  if (!JsonNumber(j, key, &v)) {
    return false;
  }
  *out = static_cast<float>(v);
  return true;
}

std::string JsonString(const nlohmann::json &j, const char *key,
                       const std::string &fallback = std::string()) {
  if (!j.is_object() || !j.contains(key) || !j.at(key).is_string()) {
    return fallback;
  }
  return j.at(key).get<std::string>();
}

std::vector<double> JsonDoubleArray(const nlohmann::json &j, const char *key) {
  std::vector<double> out;
  if (!j.is_object() || !j.contains(key) || !j.at(key).is_array()) {
    return out;
  }
  for (const auto &item : j.at(key)) {
    if (item.is_number()) {
      out.push_back(item.get<double>());
    }
  }
  return out;
}

std::vector<float> JsonFloatArray(const nlohmann::json &j, const char *key) {
  std::vector<float> out;
  if (!j.is_object() || !j.contains(key) || !j.at(key).is_array()) {
    return out;
  }
  for (const auto &item : j.at(key)) {
    if (item.is_number()) {
      out.push_back(static_cast<float>(item.get<double>()));
    }
  }
  return out;
}

int32_t JsonInt(const nlohmann::json &j, const char *key,
                int32_t fallback) {
  if (!j.is_object() || !j.contains(key)) {
    return fallback;
  }
  const auto &item = j.at(key);
  if (item.is_number_integer()) {
    return item.get<int32_t>();
  }
  if (item.is_number()) {
    return static_cast<int32_t>(item.get<double>());
  }
  return fallback;
}

std::vector<int32_t> JsonIntArray(const nlohmann::json &j, const char *key) {
  std::vector<int32_t> out;
  if (!j.is_object() || !j.contains(key) || !j.at(key).is_array()) {
    return out;
  }
  for (const auto &item : j.at(key)) {
    if (item.is_number_integer()) {
      out.push_back(item.get<int32_t>());
    } else if (item.is_number()) {
      out.push_back(static_cast<int32_t>(item.get<double>()));
    }
  }
  return out;
}

value::matrix4d MatrixFromUSDArray(const std::vector<double> &flat) {
  value::matrix4d m;
  Identity(&m);
  if (flat.size() == 16) {
    size_t idx = 0;
    for (size_t r = 0; r < 4; r++) {
      for (size_t c = 0; c < 4; c++) {
        m.m[r][c] = flat[idx++];
      }
    }
  }
  return m;
}

Axis AxisFromToken(const std::string &axis) {
  if (axis == "X" || axis == "x") {
    return Axis::X;
  }
  if (axis == "Y" || axis == "y") {
    return Axis::Y;
  }
  return Axis::Z;
}

void AddAPISchemas(
    PrimMeta &meta,
    const std::vector<std::pair<APISchemas::APIName, std::string>> &schemas) {
  APISchemas api;
  api.listOpQual = ListEditQual::Prepend;
  api.names = schemas;
  meta.set_apiSchemas(api);
}

template <typename T>
void AddAttr(std::map<std::string, Property> &props, const std::string &name,
             const T &value, bool uniform = false) {
  Attribute attr;
  attr.set_value(value);
  if (uniform) {
    attr.variability() = Variability::Uniform;
  }
  props[name] = Property(std::move(attr), false);
}

void AddTransformOp(Xformable &xformable, const value::matrix4d &matrix) {
  XformOp op;
  op.op_type = XformOp::OpType::Transform;
  op.set_value(matrix);
  xformable.xformOps.push_back(op);
}

template <typename GeomT>
void AddCollisionAPIs(GeomT &geom, bool mesh_collision,
                      const nlohmann::json &src) {
  // Schemas applied to every collider geom: UsdPhysics core, plus the
  // codeless MjcPhysics mirror (read by `prim-reconstruct-physics.cc`
  // and consumed by the lightgeom + mujoco-usd-converter pipelines).
  // `MjcImageableAPI` carries `mjc:group` for round-trip into MuJoCo.
  std::vector<std::pair<APISchemas::APIName, std::string>> apis{
      {APISchemas::APIName::PhysicsCollisionAPI, ""},
      {APISchemas::APIName::MjcCollisionAPI, ""},
      {APISchemas::APIName::MjcImageableAPI, ""},
  };
  if (mesh_collision) {
    apis.push_back({APISchemas::APIName::PhysicsMeshCollisionAPI, ""});
    apis.push_back({APISchemas::APIName::MjcMeshCollisionAPI, ""});
  }
  AddAPISchemas(geom.metas(), apis);
  AddAttr(geom.props, "physics:collisionEnabled", true);
  if (mesh_collision) {
    // Default approximation is `convexHull` — matches NVIDIA / Newton's
    // mujoco-usd-converter (`_impl/geom.py:321`) and the convention
    // documented in lightgeom's `doc/usd.md`. The previous default
    // `none` left readers with arbitrary triangle-soup collision,
    // which neither MuJoCo nor PhysX/Newton handle natively without
    // an explicit decomposition pass.
    AddAttr(geom.props, "physics:approximation",
            value::token(JsonString(src, "approximation", "convexHull")), true);
    AddAttr(geom.props, "mjc:inertia", value::token("legacy"), true);
  }
  AddAttr(geom.props, "mjc:group", JsonInt(src, "group", 3), true);
  AddAttr(geom.props, "mjc:condim", int32_t(3), true);
  AddAttr(geom.props, "mjc:solmix", 1.0, true);
  AddAttr(geom.props, "mjc:margin", 0.0, true);

  // `purpose=guide` hides the collider from default Hydra renders but
  // keeps it discoverable to schema-aware consumers (lightgeom's
  // web/sim viewer, usdview's purpose toggle, etc.). Mirrors
  // mujoco-usd-converter/_impl/utils.py:20 which applies this when
  // MuJoCo's `geom.group ∉ {0,1,2}`; URDF <collision> elements are
  // conceptually equivalent to that group and get the same treatment.
  geom.purpose.set_value(Purpose::Guide);
}

template <typename GeomT>
bool AddGeomChild(Prim &link_prim, GeomT &&geom, const std::string &name,
                  std::string *err) {
  std::string add_err;
  if (!link_prim.add_child(Prim(std::forward<GeomT>(geom)), true, &add_err)) {
    if (err) {
      *err = "Failed to add collision shape `" + name + "`: " + add_err;
    }
    return false;
  }
  return true;
}

void AppendWarn(std::string *warn, const std::string &msg) {
  if (warn) {
    (*warn) += msg;
  }
}

void SetErr(std::string *err, const std::string &msg) {
  if (err) {
    (*err) = msg;
  }
}

std::string AxisToken(const nlohmann::json &joint_json) {
  const std::string authored = JsonString(joint_json, "axisToken");
  if (!authored.empty()) {
    return {authored};
  }
  const std::vector<float> axis = JsonFloatArray(joint_json, "axis");
  if (axis.size() < 3) {
    return "X";
  }
  const float ax = std::fabs(axis[0]);
  const float ay = std::fabs(axis[1]);
  const float az = std::fabs(axis[2]);
  if (ay >= ax && ay >= az) {
    return "Y";
  }
  if (az >= ax && az >= ay) {
    return "Z";
  }
  return "X";
}

std::string JointDofName(const std::string &axis, bool rotational) {
  const char c = axis.empty() ? 'X' : axis[0];
  return std::string(rotational ? "rot" : "trans") + c;
}

value::point3f LocalPos0FromJoint(const nlohmann::json &joint_json) {
  const std::vector<double> m = JsonDoubleArray(joint_json, "originMatrix");
  if (m.size() == 16) {
    return value::point3f{
        static_cast<float>(m[12]), static_cast<float>(m[13]),
        static_cast<float>(m[14])};
  }
  const std::vector<float> xyz = JsonFloatArray(joint_json, "origin");
  if (xyz.size() >= 3) {
    return value::point3f{xyz[0], xyz[1], xyz[2]};
  }
  return value::point3f{0.0f, 0.0f, 0.0f};
}

value::point3f Point3FromJoint(const nlohmann::json &joint_json,
                               const char *key,
                               const value::point3f &fallback) {
  const std::vector<float> values = JsonFloatArray(joint_json, key);
  if (values.size() >= 3) {
    return value::point3f{values[0], values[1], values[2]};
  }
  return fallback;
}

value::quatf QuatFromJoint(const nlohmann::json &joint_json, const char *key) {
  const std::vector<float> q = JsonFloatArray(joint_json, key);
  if (q.size() >= 4) {
    // JSON uses USD text order: real, imaginary x, y, z.
    return value::quatf{{q[1], q[2], q[3]}, q[0]};
  }
  return value::quatf{{0.0f, 0.0f, 0.0f}, 1.0f};
}

template <typename JointT>
void AssignJointBase(JointT &joint, const nlohmann::json &joint_json,
                     const std::string &parent_name,
                     const std::string &child_name) {
  joint.body0.set(Path("/World/Links/" + parent_name, ""));
  joint.body1.set(Path("/World/Links/" + child_name, ""));
  const value::point3f local_pos0 = Point3FromJoint(
      joint_json, "localPos0", LocalPos0FromJoint(joint_json));
  joint.localPos0.set_value(local_pos0);
  joint.localPos1.set_value(Point3FromJoint(
      joint_json, "localPos1", value::point3f{0.0f, 0.0f, 0.0f}));
  joint.localRot0.set_value(QuatFromJoint(joint_json, "localRot0"));
  joint.localRot1.set_value(QuatFromJoint(joint_json, "localRot1"));
  joint.jointEnabled.set_value(true);
  joint.collisionEnabled.set_value(false);

  // Dispatch the per-axis (angular vs linear) PhysX limit namespace
  // based on the joint subtype the JSON declares. revolute -> angular,
  // prismatic -> linear; any other type still gets the canonical mjc:*
  // attributes but skips the physxLimit:* mirror because PhysX has no
  // direct analog. Matches the SchemaResolverPhysx mapping in
  // ref/newton/newton/_src/usd/schemas.py.
  const std::string joint_subtype = JsonString(joint_json, "type");
  const bool is_revolute = (joint_subtype == "revolute");
  const bool is_prismatic = (joint_subtype == "prismatic");
  const char *limit_ns = is_prismatic ? "physxLimit:linear:"
                                       : "physxLimit:angular:";

  MjcJointAPI mjc;
  if (joint_json.contains("dynamics") && joint_json["dynamics"].is_object()) {
    const auto &dyn = joint_json["dynamics"];
    double damping = 0.0;
    if (JsonNumber(dyn, "damping", &damping)) {
      mjc.damping.set_value(damping);
      AddAttr(joint.props, "mjc:damping", damping);
      if (is_revolute || is_prismatic) {
        AddAttr(joint.props, std::string(limit_ns) + "damping", damping);
      }
    }
    double friction = 0.0;
    if (JsonNumber(dyn, "friction", &friction)) {
      mjc.frictionloss.set_value(friction);
      AddAttr(joint.props, "mjc:frictionloss", friction);
      AddAttr(joint.props, "physxJoint:jointFriction", friction);
    }
    double stiffness = 0.0;
    if (JsonNumber(dyn, "stiffness", &stiffness)) {
      // mjcJoint's stiffness lives on the MjcJointAPI extension —
      // expose both forms so MuJoCo + PhysX consumers see it.
      AddAttr(joint.props, "mjc:stiffness", stiffness);
      if (is_revolute || is_prismatic) {
        AddAttr(joint.props, std::string(limit_ns) + "stiffness", stiffness);
      }
    }
    double armature = 0.0;
    if (JsonNumber(dyn, "armature", &armature)) {
      AddAttr(joint.props, "mjc:armature", armature);
      AddAttr(joint.props, "physxJoint:armature", armature);
    }
  }
  // Initial joint configuration. MJCF and URDF both surface this via
  // <joint range>/<joint pos> respectively; we promote it into the
  // Newton state:*:physics:position schema, which Genesis/Isaac/Newton
  // all read for joint qpos initialization. Revolute joints carry the
  // value in *degrees* per the PhysX/Newton convention; prismatic in
  // *meters*.
  double init_q = 0.0;
  if (JsonNumber(joint_json, "initPosition", &init_q) && init_q != 0.0) {
    if (is_revolute) {
      AddAttr(joint.props, "state:angular:physics:position",
              init_q * 180.0 / 3.14159265358979323846);
    } else if (is_prismatic) {
      AddAttr(joint.props, "state:linear:physics:position", init_q);
    }
  }
  joint.mjcJoint = mjc;
  AddAPISchemas(joint.metas(), {{APISchemas::APIName::MjcJointAPI, ""}});
}

bool AddMeshFromJson(Prim &link_prim, const nlohmann::json &mesh_json,
                     const std::map<std::string, URDFMeshBuffer> *mesh_buffers,
                     const std::string &fallback_name, bool collision,
                     std::string *warn, std::string *err) {
  const nlohmann::json *geom = &mesh_json;
  if (mesh_json.contains("geometry") && mesh_json["geometry"].is_object()) {
    geom = &mesh_json["geometry"];
  }
  std::vector<float> json_positions;
  std::vector<float> json_normals;
  std::vector<float> json_uvs;
  std::vector<int32_t> json_indices;
  const std::vector<float> *positions = nullptr;
  const std::vector<float> *normals = nullptr;
  const std::vector<float> *uvs = nullptr;
  const std::vector<int32_t> *source_indices = nullptr;
  const std::string mesh_ref = JsonString(mesh_json, "meshRef");
  if (!mesh_ref.empty()) {
    if (!mesh_buffers) {
      AppendWarn(warn, "Skipping mesh `" + fallback_name +
                           "`: meshRef `" + mesh_ref + "` was not registered.\n");
      return true;
    }
    auto buffer_it = mesh_buffers->find(mesh_ref);
    if (buffer_it == mesh_buffers->end()) {
      AppendWarn(warn, "Skipping mesh `" + fallback_name +
                           "`: meshRef `" + mesh_ref + "` was not registered.\n");
      return true;
    }
    const URDFMeshBuffer &buffer = buffer_it->second;
    positions = &buffer.positions;
    normals = &buffer.normals;
    uvs = &buffer.uvs;
    source_indices = &buffer.indices;
  } else {
    json_positions = JsonFloatArray(*geom, "positions");
    json_normals = JsonFloatArray(*geom, "normals");
    json_uvs = JsonFloatArray(*geom, "uvs");
    json_indices = JsonIntArray(*geom, "indices");
    positions = &json_positions;
    normals = &json_normals;
    uvs = &json_uvs;
    source_indices = &json_indices;
  }
  if (!positions || positions->size() < 9 || (positions->size() % 3) != 0) {
    AppendWarn(warn, "Skipping mesh `" + fallback_name +
                         "`: positions must contain at least 3 points.\n");
    return true;
  }

  std::vector<int32_t> indices;
  if (source_indices && !source_indices->empty()) {
    indices = *source_indices;
  } else {
    indices.resize(positions->size() / 3);
    for (size_t i = 0; i < indices.size(); i++) {
      indices[i] = static_cast<int32_t>(i);
    }
  }
  if ((indices.size() % 3) != 0) {
    AppendWarn(warn,
               "Skipping mesh `" + fallback_name + "`: indices must be triangles.\n");
    return true;
  }

  const std::string mesh_name = SanitizeUSDIdentifier(
      JsonString(mesh_json, "name", fallback_name), fallback_name);
  GeomMesh mesh;
  mesh.name = mesh_name;
  mesh.subdivisionScheme.set_value(
      GeomMesh::SubdivisionScheme::SubdivisionSchemeNone);

  std::vector<value::point3f> points;
  points.reserve(positions->size() / 3);
  for (size_t i = 0; i + 2 < positions->size(); i += 3) {
    points.push_back(
        {(*positions)[i + 0], (*positions)[i + 1], (*positions)[i + 2]});
  }
  mesh.points.set_value(std::move(points));

  std::vector<int32_t> counts(indices.size() / 3, 3);
  mesh.faceVertexCounts.set_value(std::move(counts));
  mesh.faceVertexIndices.set_value(std::move(indices));

  if (normals && normals->size() == positions->size()) {
    std::vector<value::normal3f> ns;
    ns.reserve(normals->size() / 3);
    for (size_t i = 0; i + 2 < normals->size(); i += 3) {
      ns.push_back({(*normals)[i + 0], (*normals)[i + 1], (*normals)[i + 2]});
    }
    mesh.normals.set_value(std::move(ns));
    mesh.normals.metas().set_interpolation_enum(Interpolation::Vertex);
  }

  if (uvs && uvs->size() == (positions->size() / 3) * 2) {
    Attribute uv_attr;
    std::vector<value::texcoord2f> st;
    st.reserve(uvs->size() / 2);
    for (size_t i = 0; i + 1 < uvs->size(); i += 2) {
      st.push_back({(*uvs)[i + 0], (*uvs)[i + 1]});
    }
    uv_attr.set_value(std::move(st));
    uv_attr.metas().set_interpolation_enum(Interpolation::Vertex);
    mesh.props.emplace("primvars:st", Property(std::move(uv_attr), false));
  }

  const std::vector<double> matrix = JsonDoubleArray(mesh_json, "matrix");
  if (matrix.size() == 16) {
    AddTransformOp(mesh, MatrixFromUSDArray(matrix));
  }

  if (collision) {
    AddCollisionAPIs(mesh, true, mesh_json);
  } else {
    AddAPISchemas(mesh.metas(), {{APISchemas::APIName::MjcImageableAPI, ""}});
    AddAttr(mesh.props, "mjc:group", JsonInt(mesh_json, "group", 2), true);
  }

  std::string add_err;
  if (!link_prim.add_child(Prim(mesh), true, &add_err)) {
    SetErr(err, "Failed to add mesh `" + mesh_name + "`: " + add_err);
    return false;
  }
  return true;
}

bool AddNativeCollisionShapeFromJson(Prim &link_prim,
                                     const nlohmann::json &shape_json,
                                     const std::string &fallback_name,
                                     std::string *warn, std::string *err) {
  const nlohmann::json shape =
      (shape_json.contains("shape") && shape_json["shape"].is_object())
          ? shape_json["shape"]
          : shape_json;
  const std::string type = JsonString(shape, "type");
  if (type.empty()) {
    AppendWarn(warn, "Skipping collision shape `" + fallback_name +
                         "`: missing shape.type.\n");
    return true;
  }

  const std::string name = SanitizeUSDIdentifier(
      JsonString(shape_json, "name", fallback_name), fallback_name);
  const std::vector<double> matrix = JsonDoubleArray(shape_json, "matrix");

  if (type == "box" || type == "cube") {
    GeomCube cube;
    cube.name = name;
    cube.size.set_value(2.0);
    if (matrix.size() == 16) {
      AddTransformOp(cube, MatrixFromUSDArray(matrix));
    }
    AddCollisionAPIs(cube, false, shape_json);
    return AddGeomChild(link_prim, std::move(cube), name, err);
  }

  if (type == "sphere") {
    GeomSphere sphere;
    sphere.name = name;
    double radius = 0.5;
    JsonNumber(shape, "radius", &radius);
    sphere.radius.set_value(radius);
    if (matrix.size() == 16) {
      AddTransformOp(sphere, MatrixFromUSDArray(matrix));
    }
    AddCollisionAPIs(sphere, false, shape_json);
    return AddGeomChild(link_prim, std::move(sphere), name, err);
  }

  if (type == "cylinder") {
    GeomCylinder cylinder;
    cylinder.name = name;
    double radius = 0.5;
    double height = 1.0;
    JsonNumber(shape, "radius", &radius);
    JsonNumber(shape, "height", &height);
    cylinder.radius.set_value(radius);
    cylinder.height.set_value(height);
    cylinder.axis.set_value(AxisFromToken(JsonString(shape, "axis", "Z")));
    if (matrix.size() == 16) {
      AddTransformOp(cylinder, MatrixFromUSDArray(matrix));
    }
    AddCollisionAPIs(cylinder, false, shape_json);
    return AddGeomChild(link_prim, std::move(cylinder), name, err);
  }

  if (type == "capsule") {
    GeomCapsule capsule;
    capsule.name = name;
    double radius = 0.5;
    double height = 1.0;
    JsonNumber(shape, "radius", &radius);
    JsonNumber(shape, "height", &height);
    capsule.radius.set_value(radius);
    capsule.height.set_value(height);
    capsule.axis.set_value(AxisFromToken(JsonString(shape, "axis", "Z")));
    if (matrix.size() == 16) {
      AddTransformOp(capsule, MatrixFromUSDArray(matrix));
    }
    AddCollisionAPIs(capsule, false, shape_json);
    return AddGeomChild(link_prim, std::move(capsule), name, err);
  }

  if (type == "plane") {
    GeomPlane plane;
    plane.name = name;
    double width = 2.0;
    double length = 2.0;
    JsonNumber(shape, "width", &width);
    JsonNumber(shape, "length", &length);
    plane.width.set_value(width);
    plane.length.set_value(length);
    plane.axis.set_value(AxisFromToken(JsonString(shape, "axis", "Z")));
    if (matrix.size() == 16) {
      AddTransformOp(plane, MatrixFromUSDArray(matrix));
    }
    AddCollisionAPIs(plane, false, shape_json);
    return AddGeomChild(link_prim, std::move(plane), name, err);
  }

  AppendWarn(warn, "Skipping collision shape `" + fallback_name +
                       "`: unsupported shape type `" + type + "`.\n");
  return true;
}

}  // namespace

bool ConvertURDFJsonToUSDStage(
    const std::string &robot_json,
    const std::map<std::string, URDFMeshBuffer> *mesh_buffers,
    Stage *out_stage,
    std::string *warn, std::string *err) {
  if (!out_stage) {
    SetErr(err, "Output Stage pointer is null");
    return false;
  }
  if (warn) {
    warn->clear();
  }
  if (err) {
    err->clear();
  }

  nlohmann::json root = nlohmann::json::parse(robot_json, nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    SetErr(err, "URDF export JSON parse failed");
    return false;
  }

  const nlohmann::json empty_array = nlohmann::json::array();
  const nlohmann::json &links_json =
      (root.contains("links") && root["links"].is_array()) ? root["links"]
                                                            : empty_array;
  const nlohmann::json &joints_json =
      (root.contains("joints") && root["joints"].is_array())
          ? root["joints"]
          : empty_array;
  if (links_json.empty()) {
    SetErr(err, "URDF export JSON has no links");
    return false;
  }

  Stage stage;
  stage.metas().defaultPrim = value::token("World");
  const std::string up_axis = JsonString(root, "upAxis", "Y");
  stage.metas().upAxis =
      (up_axis == "Z" || up_axis == "z") ? Axis::Z : Axis::Y;

  Xform world;
  world.name = "World";
  world.metas().set_kind(Kind::Assembly);

  PhysicsScene scene;
  scene.name = "PhysicsScene";
  {
    value::vector3f gravity_dir{0.0f, -1.0f, 0.0f};
    std::vector<float> g = JsonFloatArray(root, "gravity");
    if (g.size() >= 3) {
      gravity_dir = {g[0], g[1], g[2]};
    }
    scene.gravityDirection.set_value(gravity_dir);
    scene.gravityMagnitude.set_value(9.80665f);
    MjcSceneAPI mjc_scene;
    double timestep = 0.002;
    if (JsonNumber(root, "timestep", &timestep)) {
      mjc_scene.timestep.set_value(timestep);
    }
    scene.mjcScene = mjc_scene;
    AddAPISchemas(scene.metas(), {{APISchemas::APIName::MjcSceneAPI, ""}});
  }

  Xform links_scope;
  links_scope.name = "Links";
  Xform joints_scope;
  joints_scope.name = "Joints";

  std::map<std::string, std::string> link_name_to_usd;
  std::set<std::string> used_link_names;
  std::vector<Prim> link_prims;

  for (size_t link_index = 0; link_index < links_json.size(); link_index++) {
    const nlohmann::json &link_json = links_json[link_index];
    const std::string link_name =
        JsonString(link_json, "name", "link_" + std::to_string(link_index));
    const std::string usd_link_name =
        UniqueUSDIdentifier(link_name, used_link_names, "link");
    link_name_to_usd[link_name] = usd_link_name;

    Xform link_xform;
    link_xform.name = usd_link_name;
    AddAPISchemas(link_xform.metas(), {
                                         {APISchemas::APIName::PhysicsRigidBodyAPI,
                                          ""},
                                         {APISchemas::APIName::PhysicsMassAPI, ""},
                                     });
    AddAttr(link_xform.props, "physics:rigidBodyEnabled", true);
    AddAttr(link_xform.props, "physics:startsAsleep", false);

    if (link_json.contains("inertial") && link_json["inertial"].is_object()) {
      const nlohmann::json &inertial = link_json["inertial"];
      float mass = 0.0f;
      if (JsonNumber(inertial, "mass", &mass) && mass > 0.0f) {
        AddAttr(link_xform.props, "physics:mass", mass);
      }
      std::vector<float> com = JsonFloatArray(inertial, "centerOfMass");
      if (com.size() >= 3) {
        AddAttr(link_xform.props, "physics:centerOfMass",
                value::point3f{com[0], com[1], com[2]});
      }
      std::vector<float> inertia = JsonFloatArray(inertial, "diagonalInertia");
      if (inertia.size() >= 3) {
        AddAttr(link_xform.props, "physics:diagonalInertia",
                value::float3{inertia[0], inertia[1], inertia[2]});
      }
    }

    Prim link_prim(link_xform);
    if (link_json.contains("visuals") && link_json["visuals"].is_array()) {
      size_t i = 0;
      for (const auto &visual : link_json["visuals"]) {
        if (!AddMeshFromJson(link_prim, visual, mesh_buffers,
                             "visual_" + std::to_string(i++), false, warn,
                             err)) {
          return false;
        }
      }
    }
    if (link_json.contains("collisions") &&
        link_json["collisions"].is_array()) {
      size_t i = 0;
      for (const auto &collision : link_json["collisions"]) {
        const std::string fallback = "collision_" + std::to_string(i++);
        if (collision.contains("shape") && collision["shape"].is_object()) {
          if (!AddNativeCollisionShapeFromJson(link_prim, collision, fallback,
                                               warn, err)) {
            return false;
          }
          continue;
        }
        if (!AddMeshFromJson(link_prim, collision, mesh_buffers, fallback, true,
                             warn, err)) {
          return false;
        }
      }
    }

    link_prims.push_back(std::move(link_prim));
  }

  Prim joints_prim(joints_scope);
  std::set<std::string> used_joint_names;
  for (size_t joint_index = 0; joint_index < joints_json.size(); joint_index++) {
    const nlohmann::json &joint_json = joints_json[joint_index];
    const std::string type = JsonString(joint_json, "type", "fixed");
    const std::string parent = JsonString(joint_json, "parent");
    const std::string child = JsonString(joint_json, "child");
    if (!link_name_to_usd.count(parent) || !link_name_to_usd.count(child)) {
      AppendWarn(warn, "Skipping joint `" + JsonString(joint_json, "name", "joint") +
                           "`: parent or child link was not exported.\n");
      continue;
    }

    const std::string joint_name = UniqueUSDIdentifier(
        JsonString(joint_json, "name", "joint_" + std::to_string(joint_index)),
        used_joint_names, "joint");
    const std::string parent_usd = link_name_to_usd[parent];
    const std::string child_usd = link_name_to_usd[child];
    const std::string axis = AxisToken(joint_json);
    const bool rotational = (type == "revolute" || type == "continuous");

    std::string add_err;
    if (type == "revolute" || type == "continuous") {
      PhysicsRevoluteJoint joint;
      joint.name = joint_name;
      AssignJointBase(joint, joint_json, parent_usd, child_usd);
      joint.axis.set_value(value::token(axis));

      if (type == "revolute" && joint_json.contains("limit") &&
          joint_json["limit"].is_object()) {
        constexpr double kRadToDeg = 57.2957795130823208768;
        double lower = 0.0;
        double upper = 0.0;
        if (JsonNumber(joint_json["limit"], "lower", &lower)) {
          joint.lowerLimit.set_value(static_cast<float>(lower * kRadToDeg));
        }
        if (JsonNumber(joint_json["limit"], "upper", &upper)) {
          joint.upperLimit.set_value(static_cast<float>(upper * kRadToDeg));
        }
        const std::string dof = JointDofName(axis, rotational);
        APISchemas api = joint.metas().get_apiSchemas();
        api.names.push_back({APISchemas::APIName::PhysicsLimitAPI, dof});
        joint.metas().set_apiSchemas(api);
        AddAttr(joint.props, "physics:limit:" + dof + ":low",
                static_cast<float>(lower * kRadToDeg));
        AddAttr(joint.props, "physics:limit:" + dof + ":high",
                static_cast<float>(upper * kRadToDeg));
      }
      if (!joints_prim.add_child(Prim(joint), true, &add_err)) {
        SetErr(err, "Failed to add revolute joint `" + joint_name +
                        "`: " + add_err);
        return false;
      }
    } else if (type == "prismatic") {
      PhysicsPrismaticJoint joint;
      joint.name = joint_name;
      AssignJointBase(joint, joint_json, parent_usd, child_usd);
      joint.axis.set_value(value::token(axis));

      if (joint_json.contains("limit") && joint_json["limit"].is_object()) {
        double lower = 0.0;
        double upper = 0.0;
        if (JsonNumber(joint_json["limit"], "lower", &lower)) {
          joint.lowerLimit.set_value(static_cast<float>(lower));
        }
        if (JsonNumber(joint_json["limit"], "upper", &upper)) {
          joint.upperLimit.set_value(static_cast<float>(upper));
        }
        const std::string dof = JointDofName(axis, false);
        APISchemas api = joint.metas().get_apiSchemas();
        api.names.push_back({APISchemas::APIName::PhysicsLimitAPI, dof});
        joint.metas().set_apiSchemas(api);
        AddAttr(joint.props, "physics:limit:" + dof + ":low",
                static_cast<float>(lower));
        AddAttr(joint.props, "physics:limit:" + dof + ":high",
                static_cast<float>(upper));
      }
      if (!joints_prim.add_child(Prim(joint), true, &add_err)) {
        SetErr(err, "Failed to add prismatic joint `" + joint_name +
                        "`: " + add_err);
        return false;
      }
    } else {
      if (type != "fixed") {
        AppendWarn(warn, "Joint `" + joint_name + "` type `" + type +
                             "` is exported as PhysicsFixedJoint.\n");
      }
      PhysicsFixedJoint joint;
      joint.name = joint_name;
      AssignJointBase(joint, joint_json, parent_usd, child_usd);
      if (!joints_prim.add_child(Prim(joint), true, &add_err)) {
        SetErr(err,
               "Failed to add fixed joint `" + joint_name + "`: " + add_err);
        return false;
      }
    }
  }

  Prim links_prim(links_scope);
  for (auto &link_prim : link_prims) {
    std::string add_err;
    if (!links_prim.add_child(std::move(link_prim), true, &add_err)) {
      SetErr(err, "Failed to add link: " + add_err);
      return false;
    }
  }

  Prim world_prim(world);
  {
    std::string add_err;
    if (!world_prim.add_child(Prim(scene), true, &add_err) ||
        !world_prim.add_child(std::move(links_prim), true, &add_err) ||
        !world_prim.add_child(std::move(joints_prim), true, &add_err)) {
      SetErr(err, "Failed to assemble URDF USD stage: " + add_err);
      return false;
    }
  }

  if (!stage.add_root_prim(std::move(world_prim))) {
    SetErr(err, "Failed to add World root prim: " + stage.get_error());
    return false;
  }

  *out_stage = std::move(stage);
  return true;
}

bool ConvertURDFJsonToUSDStage(const std::string &robot_json, Stage *out_stage,
                               std::string *warn, std::string *err) {
  return ConvertURDFJsonToUSDStage(robot_json, nullptr, out_stage, warn, err);
}

}  // namespace tydra
}  // namespace tinyusdz
