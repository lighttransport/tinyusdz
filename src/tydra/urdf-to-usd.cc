// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present Light Transport Entertainment Inc.

#include "tydra/urdf-to-usd.hh"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
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

constexpr int32_t kMjcfDefaultGroup = 0;
constexpr int32_t kMjcfDefaultCondim = 3;
constexpr double kMjcfDefaultSolmix = 1.0;
constexpr double kMjcfDefaultMargin = 0.0;
constexpr double kMjcfDefaultGap = 0.0;
constexpr int32_t kLegacyUrdfCollisionGroup = 3;
constexpr int32_t kLegacyUrdfVisualGroup = 2;
constexpr int32_t kDefaultNewtonMaxHullVertices = 64;

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

bool JsonBool(const nlohmann::json &j, const char *key, bool *out) {
  if (!out || !j.is_object() || !j.contains(key)) {
    return false;
  }
  const auto &item = j.at(key);
  if (item.is_boolean()) {
    *out = item.get<bool>();
    return true;
  }
  if (item.is_number_integer()) {
    *out = item.get<int32_t>() != 0;
    return true;
  }
  return false;
}

std::string JsonString(const nlohmann::json &j, const char *key,
                       const std::string &fallback = std::string()) {
  if (!j.is_object() || !j.contains(key) || !j.at(key).is_string()) {
    return fallback;
  }
  return j.at(key).get<std::string>();
}

const nlohmann::json *JsonObjectOrNull(const nlohmann::json &j,
                                       const char *key) {
  if (!j.is_object() || !j.contains(key) || !j.at(key).is_object()) {
    return nullptr;
  }
  return &j.at(key);
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

std::vector<double> JsonDoubleArrayFromObjectOrParent(
    const nlohmann::json &src, const char *object_key, const char *key) {
  if (const nlohmann::json *obj = JsonObjectOrNull(src, object_key)) {
    std::vector<double> out = JsonDoubleArray(*obj, key);
    if (!out.empty()) {
      return out;
    }
  }
  return JsonDoubleArray(src, key);
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

void AppendAPISchema(PrimMeta &meta, APISchemas::APIName name,
                     const std::string &instance_name = std::string()) {
  APISchemas api;
  if (meta.has_apiSchemas()) {
    api = meta.get_apiSchemas();
  } else {
    api.listOpQual = ListEditQual::Prepend;
  }
  for (const auto &schema : api.names) {
    if (schema.first == name && schema.second == instance_name) {
      meta.set_apiSchemas(api);
      return;
    }
  }
  api.names.push_back({name, instance_name});
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

double JsonNumberFromObjectOrParent(const nlohmann::json &src,
                                    const char *object_key,
                                    const char *key,
                                    double fallback) {
  double value = fallback;
  if (const nlohmann::json *obj = JsonObjectOrNull(src, object_key)) {
    if (JsonNumber(*obj, key, &value)) {
      return value;
    }
  }
  JsonNumber(src, key, &value);
  return value;
}

int32_t JsonIntFromObjectOrParent(const nlohmann::json &src,
                                  const char *object_key,
                                  const char *key,
                                  int32_t fallback) {
  if (const nlohmann::json *obj = JsonObjectOrNull(src, object_key)) {
    if (obj->contains(key)) {
      return JsonInt(*obj, key, fallback);
    }
  }
  return JsonInt(src, key, fallback);
}

bool JsonIntFromObjectOrParent(const nlohmann::json &src,
                               const char *object_key,
                               const char *key,
                               int32_t *out) {
  if (!out) {
    return false;
  }
  auto read_value = [key, out](const nlohmann::json &j) -> bool {
    if (!j.is_object() || !j.contains(key)) {
      return false;
    }
    const auto &item = j.at(key);
    if (item.is_number_integer()) {
      *out = item.get<int32_t>();
      return true;
    }
    if (item.is_number()) {
      *out = static_cast<int32_t>(item.get<double>());
      return true;
    }
    return false;
  };
  if (const nlohmann::json *obj = JsonObjectOrNull(src, object_key)) {
    if (read_value(*obj)) {
      return true;
    }
  }
  return read_value(src);
}

bool JsonNumberFromObjectOrParent(const nlohmann::json &src,
                                  const char *object_key,
                                  const char *key,
                                  double *out) {
  if (!out) {
    return false;
  }
  if (const nlohmann::json *obj = JsonObjectOrNull(src, object_key)) {
    if (JsonNumber(*obj, key, out)) {
      return true;
    }
  }
  return JsonNumber(src, key, out);
}

void AddTransformOp(Xformable &xformable, const value::matrix4d &matrix) {
  XformOp op;
  op.op_type = XformOp::OpType::Transform;
  op.set_value(matrix);
  xformable.xformOps.push_back(op);
}

// Jacobi eigenvalue decomposition of a symmetric 3x3 matrix. Outputs the
// eigenvalues `eval[i]` and their eigenvectors as the columns of `evec`
// (orthonormal). Used to diagonalize a full inertia tensor into principal
// moments (eigenvalues) + a principal-axes rotation (eigenvectors).
void JacobiEigenSymmetric3(const double in[3][3], double eval[3],
                           double evec[3][3]) {
  double a[3][3];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) a[i][j] = in[i][j];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) evec[i][j] = (i == j) ? 1.0 : 0.0;

  for (int sweep = 0; sweep < 100; sweep++) {
    double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
    if (off < 1e-18) break;
    for (int p = 0; p < 2; p++) {
      for (int q = p + 1; q < 3; q++) {
        if (std::fabs(a[p][q]) < 1e-300) continue;
        const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
        const double t = (theta >= 0.0 ? 1.0 : -1.0) /
                         (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
        const double c = 1.0 / std::sqrt(t * t + 1.0);
        const double s = t * c;
        // Apply the Givens rotation a := Jᵀ a J for indices (p, q).
        const double app = a[p][p], aqq = a[q][q], apq = a[p][q];
        a[p][p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
        a[q][q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
        a[p][q] = a[q][p] = 0.0;
        const int r = 3 - p - q;  // the third index
        const double arp = a[r][p], arq = a[r][q];
        a[r][p] = a[p][r] = c * arp - s * arq;
        a[r][q] = a[q][r] = s * arp + c * arq;
        // Accumulate the eigenvectors.
        for (int k = 0; k < 3; k++) {
          const double ekp = evec[k][p], ekq = evec[k][q];
          evec[k][p] = c * ekp - s * ekq;
          evec[k][q] = s * ekp + c * ekq;
        }
      }
    }
  }
  for (int i = 0; i < 3; i++) eval[i] = a[i][i];
}

// Convert a 3x3 rotation matrix (columns = orthonormal basis) into a quatf.
// Forces a right-handed (det +1) frame so the quaternion is well-formed.
value::quatf RotationMatrixToQuatf(double r[3][3]) {
  // Ensure proper rotation: if det < 0, flip the third column.
  const double det =
      r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
      r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
      r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
  if (det < 0.0) {
    r[0][2] = -r[0][2];
    r[1][2] = -r[1][2];
    r[2][2] = -r[2][2];
  }
  const double tr = r[0][0] + r[1][1] + r[2][2];
  double w, x, y, z;
  if (tr > 0.0) {
    double S = std::sqrt(tr + 1.0) * 2.0;
    w = 0.25 * S;
    x = (r[2][1] - r[1][2]) / S;
    y = (r[0][2] - r[2][0]) / S;
    z = (r[1][0] - r[0][1]) / S;
  } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
    double S = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0;
    w = (r[2][1] - r[1][2]) / S;
    x = 0.25 * S;
    y = (r[0][1] + r[1][0]) / S;
    z = (r[0][2] + r[2][0]) / S;
  } else if (r[1][1] > r[2][2]) {
    double S = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0;
    w = (r[0][2] - r[2][0]) / S;
    x = (r[0][1] + r[1][0]) / S;
    y = 0.25 * S;
    z = (r[1][2] + r[2][1]) / S;
  } else {
    double S = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0;
    w = (r[1][0] - r[0][1]) / S;
    x = (r[0][2] + r[2][0]) / S;
    y = (r[1][2] + r[2][1]) / S;
    z = 0.25 * S;
  }
  value::quatf q;
  q.real = static_cast<float>(w);
  q.imag = value::float3{static_cast<float>(x), static_cast<float>(y),
                         static_cast<float>(z)};
  return q;
}

template <typename GeomT>
void AddCollisionAPIs(GeomT &geom, bool mesh_collision,
                      const nlohmann::json &src, bool mjcf_source) {
  // Schemas applied to every collider geom: UsdPhysics core, plus the
  // codeless MjcPhysics mirror (read by `prim-reconstruct-physics.cc`
  // and consumed by the lightgeom + mujoco-usd-converter pipelines).
  // `MjcImageableAPI` carries `mjc:group` for round-trip into MuJoCo.
  std::vector<std::pair<APISchemas::APIName, std::string>> apis{
      {APISchemas::APIName::PhysicsCollisionAPI, ""},
      {APISchemas::APIName::MjcCollisionAPI, ""},
      {APISchemas::APIName::MjcImageableAPI, ""},
      {APISchemas::APIName::NewtonCollisionAPI, ""},
  };
  if (mesh_collision) {
    apis.push_back({APISchemas::APIName::PhysicsMeshCollisionAPI, ""});
    apis.push_back({APISchemas::APIName::MjcMeshCollisionAPI, ""});
    apis.push_back({APISchemas::APIName::NewtonMeshCollisionAPI, ""});
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
  if (mjcf_source) {
    int32_t ivalue = 0;
    double dvalue = 0.0;
    if (JsonIntFromObjectOrParent(src, "mjc", "group", &ivalue)) {
      AddAttr(geom.props, "mjc:group", ivalue, true);
    } else if (src.contains("group")) {
      AddAttr(geom.props, "mjc:group",
              JsonInt(src, "group", kMjcfDefaultGroup), true);
    }
    if (JsonIntFromObjectOrParent(src, "mjc", "condim", &ivalue)) {
      AddAttr(geom.props, "mjc:condim", ivalue, true);
    } else if (src.contains("condim")) {
      AddAttr(geom.props, "mjc:condim",
              JsonInt(src, "condim", kMjcfDefaultCondim), true);
    }
    if (JsonIntFromObjectOrParent(src, "mjc", "geomContype", &ivalue) ||
        JsonIntFromObjectOrParent(src, "mjc", "contype", &ivalue)) {
      AddAttr(geom.props, "mjc:geomContype", ivalue, true);
    }
    if (JsonIntFromObjectOrParent(src, "mjc", "geomConaffinity", &ivalue) ||
        JsonIntFromObjectOrParent(src, "mjc", "conaffinity", &ivalue)) {
      AddAttr(geom.props, "mjc:geomConaffinity", ivalue, true);
    }
    if (JsonIntFromObjectOrParent(src, "mjc", "priority", &ivalue)) {
      AddAttr(geom.props, "mjc:priority", ivalue, true);
    }
    if (JsonNumberFromObjectOrParent(src, "mjc", "solmix", &dvalue)) {
      AddAttr(geom.props, "mjc:solmix", dvalue, true);
    }
    if (JsonNumberFromObjectOrParent(src, "mjc", "margin", &dvalue)) {
      AddAttr(geom.props, "mjc:margin", dvalue, true);
      AddAttr(geom.props, "newton:contactMargin", static_cast<float>(dvalue));
    } else if (JsonNumberFromObjectOrParent(src, "newton", "contactMargin",
                                            &dvalue)) {
      AddAttr(geom.props, "newton:contactMargin", static_cast<float>(dvalue));
    }
    if (JsonNumberFromObjectOrParent(src, "mjc", "gap", &dvalue)) {
      AddAttr(geom.props, "mjc:gap", dvalue, true);
      AddAttr(geom.props, "newton:contactGap", static_cast<float>(dvalue));
    } else if (JsonNumberFromObjectOrParent(src, "newton", "contactGap",
                                            &dvalue)) {
      AddAttr(geom.props, "newton:contactGap", static_cast<float>(dvalue));
    }
    std::vector<double> values =
        JsonDoubleArrayFromObjectOrParent(src, "mjc", "geomFriction");
    if (values.empty()) {
      values = JsonDoubleArrayFromObjectOrParent(src, "mjc", "friction");
    }
    if (!values.empty()) {
      AddAttr(geom.props, "mjc:geomFriction", values, true);
    }
    values = JsonDoubleArrayFromObjectOrParent(src, "mjc", "solref");
    if (!values.empty()) {
      AddAttr(geom.props, "mjc:solref", values, true);
    }
    values = JsonDoubleArrayFromObjectOrParent(src, "mjc", "solimp");
    if (!values.empty()) {
      AddAttr(geom.props, "mjc:solimp", values, true);
    }
    values = JsonDoubleArrayFromObjectOrParent(src, "mjc", "geomSize");
    if (!values.empty()) {
      AddAttr(geom.props, "mjc:geomSize", values, true);
    }
  } else {
    AddAttr(geom.props, "mjc:group",
            JsonInt(src, "group", kLegacyUrdfCollisionGroup), true);
    AddAttr(geom.props, "mjc:condim",
            JsonInt(src, "condim", kMjcfDefaultCondim), true);
    AddAttr(geom.props, "mjc:solmix",
            JsonNumberFromObjectOrParent(src, "mjc", "solmix",
                                         kMjcfDefaultSolmix), true);
    const double margin =
        JsonNumberFromObjectOrParent(src, "mjc", "margin",
                                     kMjcfDefaultMargin);
    AddAttr(geom.props, "mjc:margin", margin, true);
    AddAttr(geom.props, "newton:contactMargin",
            static_cast<float>(JsonNumberFromObjectOrParent(
                src, "newton", "contactMargin", margin)));
    AddAttr(geom.props, "newton:contactGap",
            static_cast<float>(JsonNumberFromObjectOrParent(
                src, "newton", "contactGap", kMjcfDefaultGap)));
  }
  if (mesh_collision) {
    AddAttr(geom.props, "newton:maxHullVertices",
            JsonIntFromObjectOrParent(src, "newton", "maxHullVertices",
                                      kDefaultNewtonMaxHullVertices),
            true);
  }

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

// The joint frame rotation is authored via the JSON `localRot0`/`localRot1`
// quaternions (see QuatFromJoint), which every producer in this repo supplies.
// `originMatrix` is consumed only for its translation (LocalPos0FromJoint); its
// rotation is intentionally NOT decoded into localRot0. Detect the case where a
// producer encoded a rotation only in `originMatrix` so we can warn instead of
// silently dropping it. Returns true if originMatrix has 16 elems and a
// non-identity upper-left 3x3.
bool JointOriginMatrixHasRotation(const nlohmann::json &joint_json) {
  const std::vector<double> m = JsonDoubleArray(joint_json, "originMatrix");
  if (m.size() != 16) return false;
  // Column-major (THREE/USD bridge): rotation basis is indices 0,1,2 / 4,5,6 /
  // 8,9,10. Compare against identity with a loose tolerance.
  const double expect[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  const size_t idx[9] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
  for (size_t i = 0; i < 9; i++) {
    if (std::abs(m[idx[i]] - expect[i]) > 1e-6) return true;
  }
  return false;
}

template <typename JointT>
void AssignJointBase(JointT &joint, const nlohmann::json &joint_json,
                     const std::string &parent_name,
                     const std::string &child_name,
                     const std::map<std::string, std::string> &joint_name_to_usd) {
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
  std::vector<std::pair<APISchemas::APIName, std::string>> schemas{
      {APISchemas::APIName::MjcJointAPI, ""}};
  if (const nlohmann::json *mimic =
          JsonObjectOrNull(joint_json, "mimic")) {
    const std::string mimic_joint = JsonString(*mimic, "joint");
    auto mimic_it = joint_name_to_usd.find(mimic_joint);
    if (mimic_it != joint_name_to_usd.end()) {
      NewtonMimicAPI newton_mimic;
      bool enabled = true;
      JsonBool(*mimic, "enabled", &enabled);
      newton_mimic.mimicEnabled.set_value(enabled);
      newton_mimic.mimicJoint.set(Path("/World/Joints/" + mimic_it->second, ""));
      float offset = 0.0f;
      float multiplier = 1.0f;
      JsonNumber(*mimic, "offset", &offset);
      JsonNumber(*mimic, "multiplier", &multiplier);
      newton_mimic.mimicCoef0.set_value(offset);
      newton_mimic.mimicCoef1.set_value(multiplier);
      joint.newtonMimic = newton_mimic;
      schemas.push_back({APISchemas::APIName::NewtonMimicAPI, ""});
    }
  }
  AddAPISchemas(joint.metas(), schemas);
}

void AddNewtonSceneOptions(const nlohmann::json &root, PhysicsScene &scene) {
  NewtonSceneAPI newton_scene;

  double timestep = 0.002;
  JsonNumber(root, "timestep", &timestep);
  int32_t steps_per_second =
      (timestep > 0.0) ? static_cast<int32_t>(std::lround(1.0 / timestep))
                       : 500;
  bool gravity_enabled = true;
  int32_t max_solver_iterations = -1;
  if (const nlohmann::json *newton = JsonObjectOrNull(root, "newton")) {
    steps_per_second =
        JsonInt(*newton, "timeStepsPerSecond", steps_per_second);
    max_solver_iterations =
        JsonInt(*newton, "maxSolverIterations", max_solver_iterations);
    JsonBool(*newton, "gravityEnabled", &gravity_enabled);
  }
  newton_scene.timeStepsPerSecond.set_value(steps_per_second);
  newton_scene.maxSolverIterations.set_value(max_solver_iterations);
  newton_scene.gravityEnabled.set_value(gravity_enabled);
  scene.newtonScene = newton_scene;
}

std::vector<std::pair<APISchemas::APIName, std::string>>
NewtonActuatorSchemas(const nlohmann::json &act_json) {
  std::vector<std::pair<APISchemas::APIName, std::string>> schemas;
  const std::string control = JsonString(act_json, "control", "pd");
  if (control == "pid" || act_json.contains("ki") ||
      act_json.contains("integralMax")) {
    schemas.push_back({APISchemas::APIName::NewtonPIDControlAPI, ""});
  } else {
    schemas.push_back({APISchemas::APIName::NewtonPDControlAPI, ""});
  }
  if (act_json.contains("delaySteps")) {
    schemas.push_back({APISchemas::APIName::NewtonActuatorDelayAPI, ""});
  }
  if (act_json.contains("maxEffort")) {
    schemas.push_back({APISchemas::APIName::NewtonMaxEffortClampingAPI, ""});
  }
  if (act_json.contains("maxMotorEffort") ||
      act_json.contains("saturationEffort") ||
      act_json.contains("velocityLimit")) {
    schemas.push_back({APISchemas::APIName::NewtonDCMotorClampingAPI, ""});
  }
  if (act_json.contains("lookupPositions") || act_json.contains("lookupEfforts")) {
    schemas.push_back({APISchemas::APIName::NewtonPositionBasedClampingAPI, ""});
  }
  return schemas;
}

bool AddNewtonActuatorFromJson(
    Prim &actuators_prim, const nlohmann::json &act_json,
    const std::map<std::string, std::string> &joint_name_to_usd,
    size_t index, std::string *warn, std::string *err) {
  NewtonActuator act;
  act.name = SanitizeUSDIdentifier(
      JsonString(act_json, "name", "actuator_" + std::to_string(index)),
      "actuator");
  AddAPISchemas(act.metas(), NewtonActuatorSchemas(act_json));

  std::vector<Path> targets;
  const std::string joint_name = JsonString(act_json, "joint");
  if (!joint_name.empty()) {
    auto it = joint_name_to_usd.find(joint_name);
    if (it != joint_name_to_usd.end()) {
      targets.emplace_back("/World/Joints/" + it->second, "");
    }
  }
  if (act_json.contains("targets") && act_json["targets"].is_array()) {
    for (const auto &target : act_json["targets"]) {
      if (!target.is_string()) {
        continue;
      }
      const std::string name_or_path = target.get<std::string>();
      if (!name_or_path.empty() && name_or_path[0] == '/') {
        targets.emplace_back(name_or_path, "");
      } else {
        auto it = joint_name_to_usd.find(name_or_path);
        if (it != joint_name_to_usd.end()) {
          targets.emplace_back("/World/Joints/" + it->second, "");
        }
      }
    }
  }
  if (targets.empty()) {
    AppendWarn(warn, "Skipping Newton actuator `" + act.name +
                         "`: no target joint was exported.\n");
    return true;
  }
  act.targets.set(std::move(targets));

  if (act_json.contains("delaySteps")) {
    act.delaySteps.set_value(JsonInt(act_json, "delaySteps", 1));
  }
  float v = 0.0f;
  if (JsonNumber(act_json, "constEffort", &v)) act.constEffort.set_value(v);
  if (JsonNumber(act_json, "kp", &v)) act.kp.set_value(v);
  if (JsonNumber(act_json, "kd", &v)) act.kd.set_value(v);
  if (JsonNumber(act_json, "ki", &v)) act.ki.set_value(v);
  if (JsonNumber(act_json, "integralMax", &v)) act.integralMax.set_value(v);
  if (JsonNumber(act_json, "maxEffort", &v)) act.maxEffort.set_value(v);
  if (JsonNumber(act_json, "maxMotorEffort", &v)) {
    act.maxMotorEffort.set_value(v);
  }
  if (JsonNumber(act_json, "saturationEffort", &v)) {
    act.saturationEffort.set_value(v);
  }
  if (JsonNumber(act_json, "velocityLimit", &v)) {
    act.velocityLimit.set_value(v);
  }
  std::vector<float> lookup_positions =
      JsonFloatArray(act_json, "lookupPositions");
  if (!lookup_positions.empty()) {
    act.lookupPositions.set_value(std::move(lookup_positions));
  }
  std::vector<float> lookup_efforts = JsonFloatArray(act_json, "lookupEfforts");
  if (!lookup_efforts.empty()) {
    act.lookupEfforts.set_value(std::move(lookup_efforts));
  }

  std::string add_err;
  if (!actuators_prim.add_child(Prim(act), true, &add_err)) {
    SetErr(err, "Failed to add Newton actuator `" + act.name +
                    "`: " + add_err);
    return false;
  }
  return true;
}

// MJCF <tendon><fixed> -> MjcTendon prim. The "fixed" tendon couples joints
// with per-joint coefficients (Sum_i coef_i * q_i is constrained); we map the
// referenced joints into the mjc:path rel and the coefficients into
// mjc:path:coef. Spatial tendons (sites) are not yet emitted — a warning is
// raised by the parser. JSON shape:
//   { "name", "type":"fixed", "joints":[{"joint","coef"},...],
//     "stiffness","damping","range":[lo,hi],"limited","frictionloss","margin",
//     "springlength":[...] }
bool AddMjcTendonFromJson(
    Prim &tendons_prim, const nlohmann::json &t_json,
    const std::map<std::string, std::string> &joint_name_to_usd,
    size_t index, std::string *warn, std::string *err) {
  MjcTendon tendon;
  tendon.name = SanitizeUSDIdentifier(
      JsonString(t_json, "name", "tendon_" + std::to_string(index)), "tendon");
  tendon.type.set_value(value::token(JsonString(t_json, "type", "fixed")));

  std::vector<Path> joint_targets;
  std::vector<double> coefs;
  if (t_json.contains("joints") && t_json["joints"].is_array()) {
    for (const auto &je : t_json["joints"]) {
      if (!je.is_object()) continue;
      const std::string jname = JsonString(je, "joint");
      auto it = joint_name_to_usd.find(jname);
      if (it == joint_name_to_usd.end()) {
        AppendWarn(warn, "Tendon `" + tendon.name + "` references joint `" +
                             jname + "` that was not exported; skipping it.\n");
        continue;
      }
      joint_targets.emplace_back("/World/Joints/" + it->second, "");
      double coef = 1.0;
      JsonNumber(je, "coef", &coef);
      coefs.push_back(coef);
    }
  }
  if (joint_targets.empty()) {
    AppendWarn(warn, "Skipping tendon `" + tendon.name +
                         "`: no referenced joint was exported.\n");
    return true;
  }
  tendon.path.set(std::move(joint_targets));
  tendon.path_coef.set_value(coefs);

  double v = 0.0;
  if (JsonNumber(t_json, "stiffness", &v)) tendon.stiffness.set_value(v);
  if (JsonNumber(t_json, "damping", &v)) tendon.damping.set_value(v);
  if (JsonNumber(t_json, "frictionloss", &v)) tendon.frictionloss.set_value(v);
  if (JsonNumber(t_json, "margin", &v)) tendon.margin.set_value(v);
  if (JsonNumber(t_json, "armature", &v)) tendon.armature.set_value(v);
  if (t_json.contains("range") && t_json["range"].is_array() &&
      t_json["range"].size() >= 2) {
    tendon.range_min.set_value(t_json["range"][0].get<double>());
    tendon.range_max.set_value(t_json["range"][1].get<double>());
    tendon.limited.set_value(value::token("true"));
  }
  if (t_json.contains("limited")) {
    tendon.limited.set_value(value::token(JsonString(t_json, "limited", "auto")));
  }
  std::vector<double> springlen = JsonDoubleArray(t_json, "springlength");
  if (!springlen.empty()) tendon.springlength.set_value(springlen);

  std::string add_err;
  if (!tendons_prim.add_child(Prim(tendon), true, &add_err)) {
    SetErr(err, "Failed to add tendon `" + tendon.name + "`: " + add_err);
    return false;
  }
  return true;
}

// MJCF <equality> -> an Xform host prim carrying the matching MjcEquality*API
// applied schema plus mjc:* attrs in the generic props map (these round-trip
// through the generic props pass, mirroring the physx:* preservation contract).
// connect/weld relate two bodies; joint relates two joints. JSON shape:
//   { "name","type":"connect|weld|joint",
//     "body1","body2","anchor":[x,y,z],            (connect/weld)
//     "joint1","joint2","polycoef":[c0..c4],        (joint)
//     "torquescale",                                 (weld)
//     "solref":[...],"solimp":[...] }
bool AddMjcEqualityFromJson(
    Prim &equalities_prim, const nlohmann::json &e_json,
    const std::map<std::string, std::string> &link_name_to_usd,
    const std::map<std::string, std::string> &joint_name_to_usd,
    size_t index, std::string *warn, std::string *err) {
  const std::string type = JsonString(e_json, "type", "connect");
  Xform host;
  host.name = SanitizeUSDIdentifier(
      JsonString(e_json, "name", "equality_" + std::to_string(index)),
      "equality");

  APISchemas::APIName api_name = APISchemas::APIName::MjcEqualityConnectAPI;
  if (type == "weld") api_name = APISchemas::APIName::MjcEqualityWeldAPI;
  else if (type == "joint") api_name = APISchemas::APIName::MjcEqualityJointAPI;
  AppendAPISchema(host.metas(), api_name);

  // Resolve the two related entities into rel targets so the constraint topology
  // survives. connect/weld -> bodies (links); joint -> joints.
  auto resolve_link = [&](const std::string &n, std::vector<Path> *out) {
    auto it = link_name_to_usd.find(n);
    if (it != link_name_to_usd.end())
      out->emplace_back("/World/Links/" + it->second, "");
  };
  auto resolve_joint = [&](const std::string &n, std::vector<Path> *out) {
    auto it = joint_name_to_usd.find(n);
    if (it != joint_name_to_usd.end())
      out->emplace_back("/World/Joints/" + it->second, "");
  };

  std::vector<Path> targets;
  if (type == "joint") {
    resolve_joint(JsonString(e_json, "joint1"), &targets);
    resolve_joint(JsonString(e_json, "joint2"), &targets);
  } else {
    resolve_link(JsonString(e_json, "body1"), &targets);
    resolve_link(JsonString(e_json, "body2"), &targets);
  }
  if (!targets.empty()) {
    Relationship rel;
    rel.set(targets);
    host.props["mjc:target"] = Property(std::move(rel), false);
  }

  // Type-specific scalar/array attrs as generic mjc:* props.
  double v = 0.0;
  if (type == "weld" && JsonNumber(e_json, "torquescale", &v)) {
    AddAttr(host.props, "mjc:torqueScale", static_cast<float>(v));
  }
  if (type == "joint" && e_json.contains("polycoef") &&
      e_json["polycoef"].is_array()) {
    const auto &pc = e_json["polycoef"];
    const char *coef_names[5] = {"mjc:coef0", "mjc:coef1", "mjc:coef2",
                                 "mjc:coef3", "mjc:coef4"};
    for (size_t i = 0; i < pc.size() && i < 5; i++) {
      if (pc[i].is_number()) AddAttr(host.props, coef_names[i], pc[i].get<double>());
    }
  }
  std::vector<double> solref = JsonDoubleArray(e_json, "solref");
  if (!solref.empty()) AddAttr(host.props, "mjc:solref", solref);
  std::vector<double> solimp = JsonDoubleArray(e_json, "solimp");
  if (!solimp.empty()) AddAttr(host.props, "mjc:solimp", solimp);
  std::vector<double> anchor = JsonDoubleArray(e_json, "anchor");
  if (!anchor.empty()) AddAttr(host.props, "mjc:anchor", anchor);

  std::string add_err;
  if (!equalities_prim.add_child(Prim(host), true, &add_err)) {
    SetErr(err, "Failed to add equality `" + host.name + "`: " + add_err);
    return false;
  }
  (void)warn;
  return true;
}

bool AddMeshFromJson(Prim &link_prim, const nlohmann::json &mesh_json,
                     const std::map<std::string, URDFMeshBuffer> *mesh_buffers,
                     const std::string &fallback_name, bool collision,
                     bool mjcf_source, std::string *warn, std::string *err) {
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
    AddCollisionAPIs(mesh, true, mesh_json, mjcf_source);
  } else {
    AddAPISchemas(mesh.metas(), {{APISchemas::APIName::MjcImageableAPI, ""}});
    if (mjcf_source) {
      int32_t group = 0;
      if (JsonIntFromObjectOrParent(mesh_json, "mjc", "group", &group) ||
          mesh_json.contains("group")) {
        AddAttr(mesh.props, "mjc:group", JsonInt(mesh_json, "group", group),
                true);
      }
    } else {
      AddAttr(mesh.props, "mjc:group",
              JsonInt(mesh_json, "group", kLegacyUrdfVisualGroup), true);
    }
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
                                     bool mjcf_source, std::string *warn,
                                     std::string *err) {
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
    AddCollisionAPIs(cube, false, shape_json, mjcf_source);
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
    AddCollisionAPIs(sphere, false, shape_json, mjcf_source);
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
    AddCollisionAPIs(cylinder, false, shape_json, mjcf_source);
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
    AddCollisionAPIs(capsule, false, shape_json, mjcf_source);
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
    AddCollisionAPIs(plane, false, shape_json, mjcf_source);
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
  const nlohmann::json &actuators_json =
      (root.contains("actuators") && root["actuators"].is_array())
          ? root["actuators"]
          : empty_array;
  const nlohmann::json &tendons_json =
      (root.contains("tendons") && root["tendons"].is_array())
          ? root["tendons"]
          : empty_array;
  const nlohmann::json &equalities_json =
      (root.contains("equalities") && root["equalities"].is_array())
          ? root["equalities"]
          : empty_array;
  const std::string source_format = JsonString(root, "sourceFormat");
  const bool mjcf_source = (source_format == "mjcf" ||
                            source_format == "MJCF" ||
                            JsonString(root, "inputFormat") == "mjcf");
  if (links_json.empty()) {
    SetErr(err, "URDF export JSON has no links");
    return false;
  }

  Stage stage;
  stage.metas().defaultPrim = value::token("World");
  const std::string up_axis = JsonString(root, "upAxis", "Y");
  const bool y_up = !(up_axis == "Z" || up_axis == "z");
  stage.metas().upAxis = y_up ? Axis::Y : Axis::Z;

  Xform world;
  world.name = "World";
  world.metas().set_kind(Kind::Assembly);

  // URDF/MJCF source data is authored Z-up (and all current frontends emit
  // Z-up world transforms). When the target stage is Y-up, reconcile the two
  // with a single corrective root rotation Rx(-90deg) (+Z -> +Y) on `World`.
  //
  // Per doc/usd-physics-upAxis.md this is the only correct way to convert the
  // up axis: a single root rotation propagates through the transform hierarchy
  // to every descendant body/collider world transform, while local-frame data
  // (physics:axis, joint localPos0/1 + localRot0/1, collision shape axis) and
  // the world-space `physics:gravityDirection` attribute correctly ride along
  // unchanged. Per-property/per-axis-token conversion would double-rotate.
  if (y_up) {
    XformOp rot_x;
    rot_x.op_type = XformOp::OpType::RotateX;
    rot_x.set_value(-90.0);  // degrees; row-vector RotateX(-90) maps +Z -> +Y
    world.xformOps.push_back(rot_x);
  }

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
    AddNewtonSceneOptions(root, scene);
    AddAPISchemas(scene.metas(), {
                                     {APISchemas::APIName::MjcSceneAPI, ""},
                                     {APISchemas::APIName::NewtonSceneAPI, ""},
                                 });
  }

  Xform links_scope;
  links_scope.name = "Links";
  Xform joints_scope;
  joints_scope.name = "Joints";

  std::map<std::string, std::string> link_name_to_usd;
  std::map<std::string, size_t> link_name_to_index;
  std::set<std::string> used_link_names;
  std::vector<Prim> link_prims;
  std::set<std::string> child_links;
  for (const auto &joint_json : joints_json) {
    const std::string child = JsonString(joint_json, "child");
    if (!child.empty()) {
      child_links.insert(child);
    }
  }
  bool self_collision_enabled = true;
  if (const nlohmann::json *newton = JsonObjectOrNull(root, "newton")) {
    JsonBool(*newton, "selfCollisionEnabled", &self_collision_enabled);
  }

  for (size_t link_index = 0; link_index < links_json.size(); link_index++) {
    const nlohmann::json &link_json = links_json[link_index];
    const std::string link_name =
        JsonString(link_json, "name", "link_" + std::to_string(link_index));
    const std::string usd_link_name =
        UniqueUSDIdentifier(link_name, used_link_names, "link");
    link_name_to_usd[link_name] = usd_link_name;
    link_name_to_index[link_name] = link_prims.size();

    Xform link_xform;
    link_xform.name = usd_link_name;
    AddAPISchemas(link_xform.metas(), {
                                         {APISchemas::APIName::PhysicsRigidBodyAPI,
                                          ""},
                                         {APISchemas::APIName::PhysicsMassAPI, ""},
                                     });
    AddAttr(link_xform.props, "physics:rigidBodyEnabled", true);
    AddAttr(link_xform.props, "physics:startsAsleep", false);
    if (!child_links.count(link_name)) {
      AppendAPISchema(link_xform.metas(),
                      APISchemas::APIName::PhysicsArticulationRootAPI);
      AppendAPISchema(link_xform.metas(),
                      APISchemas::APIName::NewtonArticulationRootAPI);
      AddAttr(link_xform.props, "newton:selfCollisionEnabled",
              self_collision_enabled);
    }

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
      // Full (non-diagonal) inertia tensor: diagonalize into principal moments
      // (physics:diagonalInertia) + a principal-axes rotation
      // (physics:principalAxes), the lossless USD representation. Falls back to
      // the diagonal-only path when only diaginertia was authored.
      std::vector<double> fullI = JsonDoubleArray(inertial, "fullInertia");
      if (fullI.size() >= 6) {
        // [Ixx, Iyy, Izz, Ixy, Ixz, Iyz] -> symmetric 3x3.
        const double m[3][3] = {{fullI[0], fullI[3], fullI[4]},
                                {fullI[3], fullI[1], fullI[5]},
                                {fullI[4], fullI[5], fullI[2]}};
        double eval[3];
        double evec[3][3];
        JacobiEigenSymmetric3(m, eval, evec);
        AddAttr(link_xform.props, "physics:diagonalInertia",
                value::float3{static_cast<float>(eval[0]),
                              static_cast<float>(eval[1]),
                              static_cast<float>(eval[2])});
        AddAttr(link_xform.props, "physics:principalAxes",
                RotationMatrixToQuatf(evec));
      } else {
        std::vector<float> inertia =
            JsonFloatArray(inertial, "diagonalInertia");
        if (inertia.size() >= 3) {
          AddAttr(link_xform.props, "physics:diagonalInertia",
                  value::float3{inertia[0], inertia[1], inertia[2]});
        }
      }
    }

    Prim link_prim(link_xform);
    if (link_json.contains("visuals") && link_json["visuals"].is_array()) {
      size_t i = 0;
      for (const auto &visual : link_json["visuals"]) {
        if (!AddMeshFromJson(link_prim, visual, mesh_buffers,
                             "visual_" + std::to_string(i++), false,
                             mjcf_source, warn, err)) {
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
                                               mjcf_source, warn, err)) {
            return false;
          }
          continue;
        }
        if (!AddMeshFromJson(link_prim, collision, mesh_buffers, fallback, true,
                             mjcf_source, warn, err)) {
          return false;
        }
      }
    }

    link_prims.push_back(std::move(link_prim));
  }

  if (root.contains("filteredPairs") && root["filteredPairs"].is_array()) {
    std::map<std::string, std::vector<Path>> filtered_targets;
    for (const auto &pair_json : root["filteredPairs"]) {
      if (!pair_json.is_object()) {
        continue;
      }
      const std::string body1 = JsonString(pair_json, "body1");
      const std::string body2 = JsonString(pair_json, "body2");
      auto it1 = link_name_to_usd.find(body1);
      auto it2 = link_name_to_usd.find(body2);
      if (it1 == link_name_to_usd.end() || it2 == link_name_to_usd.end()) {
        AppendWarn(warn, "Skipping filtered pair `" + body1 + "` / `" +
                             body2 + "`: link was not exported.\n");
        continue;
      }
      filtered_targets[body1].push_back(
          Path("/World/Links/" + it2->second, ""));
    }
    for (const auto &kv : filtered_targets) {
      auto index_it = link_name_to_index.find(kv.first);
      if (index_it == link_name_to_index.end() ||
          index_it->second >= link_prims.size() || kv.second.empty()) {
        continue;
      }
      Prim &prim = link_prims[index_it->second];
      AppendAPISchema(prim.metas(), APISchemas::APIName::PhysicsFilteredPairsAPI);
      Relationship rel;
      rel.set(kv.second);
      if (auto *xform = prim.get_data().as<Xform>()) {
        xform->props["physics:filteredPairs"] = Property(std::move(rel), false);
      }
    }
  }

  Prim joints_prim(joints_scope);
  std::set<std::string> used_joint_names;
  std::vector<std::string> joint_usd_names(joints_json.size());
  std::map<std::string, std::string> joint_name_to_usd;
  for (size_t joint_index = 0; joint_index < joints_json.size(); joint_index++) {
    const nlohmann::json &joint_json = joints_json[joint_index];
    const std::string source_name =
        JsonString(joint_json, "name", "joint_" + std::to_string(joint_index));
    const std::string joint_name =
        UniqueUSDIdentifier(source_name, used_joint_names, "joint");
    joint_usd_names[joint_index] = joint_name;
    joint_name_to_usd[source_name] = joint_name;
  }
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

    const std::string joint_name = joint_usd_names[joint_index];
    const std::string parent_usd = link_name_to_usd[parent];
    const std::string child_usd = link_name_to_usd[child];
    const std::string axis = AxisToken(joint_json);
    const bool rotational = (type == "revolute" || type == "continuous");

    // localRot0 is authoritative for the joint frame rotation. If a producer
    // omitted it but baked a rotation into originMatrix, that rotation is lost
    // (we only read originMatrix's translation) — surface it rather than drop
    // it silently.
    if (!joint_json.contains("localRot0") &&
        JointOriginMatrixHasRotation(joint_json)) {
      AppendWarn(warn, "Joint `" + joint_name +
                           "` has a rotation in originMatrix but no localRot0; "
                           "the rotation is ignored (supply localRot0).\n");
    }

    std::string add_err;
    if (type == "revolute" || type == "continuous") {
      PhysicsRevoluteJoint joint;
      joint.name = joint_name;
      AssignJointBase(joint, joint_json, parent_usd, child_usd,
                      joint_name_to_usd);
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
      AssignJointBase(joint, joint_json, parent_usd, child_usd,
                      joint_name_to_usd);
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
    } else if (type == "spherical") {
      // MuJoCo ball joint (3-DOF rotation) -> PhysicsSphericalJoint. Cone-angle
      // limits aren't carried in the JSON, so they're left unauthored (free
      // rotation), which round-trips structurally for the preview/articulation.
      PhysicsSphericalJoint joint;
      joint.name = joint_name;
      AssignJointBase(joint, joint_json, parent_usd, child_usd,
                      joint_name_to_usd);
      joint.axis.set_value(value::token(axis));
      if (!joints_prim.add_child(Prim(joint), true, &add_err)) {
        SetErr(err, "Failed to add spherical joint `" + joint_name +
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
      AssignJointBase(joint, joint_json, parent_usd, child_usd,
                      joint_name_to_usd);
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
  Prim actuators_prim;
  bool has_actuators = false;
  if (!actuators_json.empty()) {
    Xform actuators_scope;
    actuators_scope.name = "Actuators";
    actuators_prim = Prim(actuators_scope);
    for (size_t i = 0; i < actuators_json.size(); i++) {
      if (!AddNewtonActuatorFromJson(actuators_prim, actuators_json[i],
                                     joint_name_to_usd, i, warn, err)) {
        return false;
      }
    }
    has_actuators = !actuators_prim.children().empty();
  }

  // MJCF <tendon> -> /World/Tendons/<name> (MjcTendon prims).
  Prim tendons_prim;
  bool has_tendons = false;
  if (!tendons_json.empty()) {
    Xform tendons_scope;
    tendons_scope.name = "Tendons";
    tendons_prim = Prim(tendons_scope);
    for (size_t i = 0; i < tendons_json.size(); i++) {
      if (!AddMjcTendonFromJson(tendons_prim, tendons_json[i], joint_name_to_usd,
                                i, warn, err)) {
        return false;
      }
    }
    has_tendons = !tendons_prim.children().empty();
  }

  // MJCF <equality> -> /World/Equalities/<name> (Xform + MjcEquality*API).
  Prim equalities_prim;
  bool has_equalities = false;
  if (!equalities_json.empty()) {
    Xform equalities_scope;
    equalities_scope.name = "Equalities";
    equalities_prim = Prim(equalities_scope);
    for (size_t i = 0; i < equalities_json.size(); i++) {
      if (!AddMjcEqualityFromJson(equalities_prim, equalities_json[i],
                                  link_name_to_usd, joint_name_to_usd, i, warn,
                                  err)) {
        return false;
      }
    }
    has_equalities = !equalities_prim.children().empty();
  }
  {
    std::string add_err;
    if (!world_prim.add_child(Prim(scene), true, &add_err) ||
        !world_prim.add_child(std::move(links_prim), true, &add_err) ||
        !world_prim.add_child(std::move(joints_prim), true, &add_err)) {
      SetErr(err, "Failed to assemble URDF USD stage: " + add_err);
      return false;
    }
    if (has_actuators &&
        !world_prim.add_child(std::move(actuators_prim), true, &add_err)) {
      SetErr(err, "Failed to add Newton actuator scope: " + add_err);
      return false;
    }
    if (has_tendons &&
        !world_prim.add_child(std::move(tendons_prim), true, &add_err)) {
      SetErr(err, "Failed to add tendon scope: " + add_err);
      return false;
    }
    if (has_equalities &&
        !world_prim.add_child(std::move(equalities_prim), true, &add_err)) {
      SetErr(err, "Failed to add equality scope: " + add_err);
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
