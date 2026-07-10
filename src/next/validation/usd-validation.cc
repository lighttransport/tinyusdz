// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Light Transport Entertainment Inc.
//
// TinyUSDZ Next - AOUSD Core semantic validation.
//
// Port of the legacy validator (src/usd-validation.cc) onto the next-core
// layer model. Rule ids / severities / messages mirror the legacy validator.
//
// Legacy checks that are NOT expressible on next::Layer / next::PrimSpec and
// are therefore skipped or structurally approximated:
//  - crate.* rules: the next crate reader does not expose Crate
//    TOC/field/spec tables for introspection. The `crate` option is accepted
//    but the group never runs.
//  - core.apiSchema.unknown: next-core has no API-schema registry (schema
//    names are plain strings), so "unknown schema" warnings are not emitted.
//    The generic single-apply-with-instance-name error is likewise limited to
//    schemas known to be single-apply (ColorSpaceAPI/ColorSpaceDefinitionAPI)
//    to avoid false positives on custom multiple-apply schemas.
//  - core.layer.owner: next LayerMeta has no `owner` field.
//  - core.layer.colorConfiguration / colorManagementSystem emptiness: next
//    stores these as plain strings where authored-empty is indistinguishable
//    from unauthored, so the "authored but empty" error cannot fire.
//  - core.relationship.variability: next relationships carry no
//    "varying authored" flag.
//  - Invalid list-edit qualifiers: next ArcEdit qualifiers are typed enums;
//    an invalid qualifier is rejected at parse time and cannot be
//    represented.
//  - layerRelocates live per-prim in next (PrimSpecMeta::relocates); they are
//    validated at the prim location under the legacy core.layer.layerRelocates
//    rule ids.
//  - Variant OPTION bodies (properties/prims inside `variant "x" { ... }`)
//    are stored as VariantData/graft layers rather than PrimSpecs; only
//    variant set / variant / selection names are validated (deep validation
//    of variant bodies is a structural approximation gap).

#include "usd-validation.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../composition/composition.hh"
#include "../layer/prim-spec.hh"
#include "../pcp/layer-registry.hh"
#include "../prim/path.hh"
#include "../types/type-id.hh"
#include "../types/value.hh"

namespace tinyusdz {
namespace next {
namespace {

constexpr const char *kLayerLocation = "<layer>";
constexpr const char *kSpecVersion = "AOUSD Core Spec v1.0.1 (2025-12-12)";

struct AppliedSchema {
  std::string name;
  std::string instance_name;
};

using ColorSpaceSet = std::set<std::string>;
using PrimTypeByPath = std::unordered_map<std::string, std::string>;
using PrimSpecifierByPath = std::unordered_map<std::string, PrimSpecifier>;
using SkeletonJointCountByPath = std::unordered_map<std::string, size_t>;

// ---------------------------------------------------------------------------
// Small string utilities
// ---------------------------------------------------------------------------

std::vector<std::string> SplitString(const std::string &s, char delim) {
  std::vector<std::string> items;
  std::string item;
  for (char c : s) {
    if (c == delim) {
      items.emplace_back(item);
      item.clear();
    } else {
      item.push_back(c);
    }
  }
  items.emplace_back(item);
  return items;
}

bool StartsWith(const std::string &s, const std::string &prefix) {
  return s.rfind(prefix, 0) == 0;
}

bool EndsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Approximation of the legacy is_valid_utf8_identifier(): ASCII
// [A-Za-z_][A-Za-z0-9_]* with any non-ASCII (UTF-8 continuation/lead) byte
// accepted as an identifier character.
bool IsValidIdentifier(const std::string &name) {
  if (name.empty()) {
    return false;
  }
  auto is_alpha = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  };
  auto is_digit = [](unsigned char c) { return c >= '0' && c <= '9'; };
  const unsigned char c0 = static_cast<unsigned char>(name[0]);
  if (!(is_alpha(c0) || c0 == '_' || c0 >= 0x80)) {
    return false;
  }
  for (char ch : name) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (!(is_alpha(c) || is_digit(c) || c == '_' || c >= 0x80)) {
      return false;
    }
  }
  return true;
}

bool IsValidNamespacedIdentifier(const std::string &name) {
  if (name.empty()) {
    return false;
  }
  for (const std::string &part : SplitString(name, ':')) {
    if (part.empty() || !IsValidIdentifier(part)) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Issue helpers
// ---------------------------------------------------------------------------

std::string MakePropertyLocation(const std::string &prim_location,
                                 const std::string &prop_name) {
  if (prim_location.empty() || prim_location == kLayerLocation) {
    return prop_name;
  }
  return prim_location + "." + prop_name;
}

void AddIssue(USDValidationResult *result, USDValidationSeverity severity,
              const std::string &rule_id, const std::string &location,
              const std::string &message) {
  if (!result) {
    return;
  }
  result->issues.push_back({severity, rule_id, location, message});
}

void AddError(USDValidationResult *result, const std::string &rule_id,
              const std::string &location, const std::string &message) {
  AddIssue(result, USDValidationSeverity::Error, rule_id, location, message);
}

void AddWarning(USDValidationResult *result, const std::string &rule_id,
                const std::string &location, const std::string &message) {
  AddIssue(result, USDValidationSeverity::Warning, rule_id, location, message);
}

// ---------------------------------------------------------------------------
// Type-name sets (kept in sync with the legacy validator)
// ---------------------------------------------------------------------------

bool IsGprimTypeName(const std::string &type_name) {
  static const std::unordered_set<std::string> kGprimTypes = {
      "GPrim",       "Mesh",        "Sphere",       "Cube",
      "Cone",        "Cylinder",    "Cylinder_1",   "Capsule",
      "Capsule_1",   "Points",      "BasisCurves",  "NurbsCurves",
      "NurbsPatch",  "HermiteCurves", "Plane",      "TetMesh",
      "PointInstancer",
  };
  return kGprimTypes.count(type_name) > 0;
}

bool IsShadeContainerTypeName(const std::string &type_name) {
  return type_name == "Material" || type_name == "NodeGraph";
}

bool IsShadeConnectableTypeName(const std::string &type_name) {
  return type_name == "Shader" || type_name == "NodeGraph";
}

bool IsShadeConnectionTargetTypeName(const std::string &type_name) {
  return type_name == "Material" || IsShadeConnectableTypeName(type_name);
}

bool IsLuxLightTypeName(const std::string &type_name) {
  static const std::set<std::string> kLightTypes = {
      "SphereLight", "CylinderLight", "DomeLight",  "DomeLight_1",
      "DiskLight",   "RectLight",     "DistantLight", "GeometryLight",
      "PortalLight", "PluginLight"};
  return kLightTypes.count(type_name) > 0;
}

bool IsPhysicsPrimTypeName(const std::string &type_name) {
  static const std::set<std::string> kPhysicsTypes = {
      "PhysicsScene",      "PhysicsJoint",       "PhysicsRevoluteJoint",
      "PhysicsPrismaticJoint", "PhysicsSphericalJoint",
      "PhysicsFixedJoint", "PhysicsDistanceJoint",
      "PhysicsCollisionGroup", "MjcActuator", "MjcTendon", "MjcKeyframe",
      "NewtonActuator", "Preliminary_PhysicsGravitationalForce",
      "Preliminary_InfiniteColliderPlane"};
  return kPhysicsTypes.count(type_name) > 0;
}

bool IsPhysicsSchemaName(const std::string &schema_name) {
  static const std::unordered_set<std::string> kNames = {
      "PhysicsRigidBodyAPI", "PhysicsCollisionAPI", "PhysicsMaterialAPI",
      "PhysicsMeshCollisionAPI", "PhysicsMassAPI", "PhysicsFilteredPairsAPI",
      "PhysicsArticulationRootAPI", "PhysicsDriveAPI", "PhysicsLimitAPI",
      "MjcSceneAPI", "MjcJointAPI", "MjcCollisionAPI", "MjcMeshCollisionAPI",
      "MjcMaterialAPI", "MjcSiteAPI", "MjcImageableAPI", "MjcEqualityAPI",
      "MjcEqualityConnectAPI", "MjcEqualityWeldAPI", "MjcEqualityJointAPI",
      "NewtonSceneAPI", "NewtonXpbdSceneAPI", "NewtonKaminoSceneAPI",
      "NewtonArticulationRootAPI", "NewtonCollisionAPI",
      "NewtonMeshCollisionAPI", "NewtonMaterialAPI", "NewtonMimicAPI",
      "NewtonActuatorDelayAPI", "NewtonPDControlAPI", "NewtonPIDControlAPI",
      "NewtonNeuralControlAPI", "NewtonMaxEffortClampingAPI",
      "NewtonDCMotorClampingAPI", "NewtonPositionBasedClampingAPI",
      "Preliminary_PhysicsMaterialAPI", "Preliminary_PhysicsRigidBodyAPI",
      "Preliminary_PhysicsColliderAPI"};
  return kNames.count(schema_name) > 0;
}

bool IsShaderInputName(const std::string &prop_name) {
  return StartsWith(prop_name, "inputs:") &&
         prop_name.size() > std::string("inputs:").size();
}

bool IsShaderOutputName(const std::string &prop_name) {
  return StartsWith(prop_name, "outputs:") &&
         prop_name.size() > std::string("outputs:").size();
}

bool IsMaterialTerminalOutputName(const std::string &prop_name) {
  return prop_name == "outputs:surface" ||
         prop_name == "outputs:displacement" || prop_name == "outputs:volume";
}

bool IsUsdPrimvarReaderId(const std::string &shader_id) {
  return StartsWith(shader_id, "UsdPrimvarReader_");
}

const std::string *UsdPrimvarReaderResultType(const std::string &shader_id) {
  static const std::unordered_map<std::string, std::string> kResultTypes = {
      {"UsdPrimvarReader_int", "int"},
      {"UsdPrimvarReader_float", "float"},
      {"UsdPrimvarReader_float2", "float2"},
      {"UsdPrimvarReader_float3", "float3"},
      {"UsdPrimvarReader_float4", "float4"},
      {"UsdPrimvarReader_string", "string"},
      {"UsdPrimvarReader_vector", "vector3f"},
      {"UsdPrimvarReader_normal", "normal3f"},
      {"UsdPrimvarReader_point", "point3f"},
      {"UsdPrimvarReader_matrix", "matrix4d"},
  };
  auto it = kResultTypes.find(shader_id);
  if (it == kResultTypes.end()) {
    return nullptr;
  }
  return &it->second;
}

bool IsMaterialXShaderId(const std::string &shader_id) {
  return StartsWith(shader_id, "ND_") || StartsWith(shader_id, "MaterialX") ||
         shader_id == "MtlxUsdPreviewSurface" ||
         shader_id == "MtlxAutodeskStandardSurface" ||
         shader_id == "MtlxOpenPBRSurface";
}

bool IsMaterialXAssetPath(const std::string &asset_path) {
  return EndsWith(asset_path, ".mtlx") || EndsWith(asset_path, ".MTLX");
}

bool IsMultipleApplySchemaName(const std::string &schema_name) {
  return schema_name == "CollectionAPI" || schema_name == "PhysicsDriveAPI" ||
         schema_name == "PhysicsLimitAPI";
}

// UsdPreviewSurface input name -> expected (role) value type. Shares the
// generated table with the legacy validator (see
// scripts/gen-usd-preview-surface-inputs.py).
const std::unordered_map<std::string, std::string> &
UsdPreviewSurfaceInputTypes() {
  static const std::unordered_map<std::string, std::string> kInputs = {
#include "../../usd-preview-surface-inputs.inc"
  };
  return kInputs;
}

std::string BaseValueType(const std::string &type_name) {
  if (type_name == "texCoord2f") {
    return "float2";
  }
  if (type_name == "color3f" || type_name == "normal3f" ||
      type_name == "point3f" || type_name == "vector3f") {
    return "float3";
  }
  if (type_name == "color4f") {
    return "float4";
  }
  return type_name;
}

bool ValueTypesAgree(const std::string &authored, const std::string &expected) {
  if (authored == expected) {
    return true;
  }
  return BaseValueType(authored) == BaseValueType(expected);
}

// ---------------------------------------------------------------------------
// Path validation (string-based; next::Path is an unvalidated string wrapper)
// ---------------------------------------------------------------------------

void SplitScenePathString(const std::string &path, std::string *prim_part,
                          std::string *prop_part) {
  const size_t dot = path.find('.');
  if (dot == std::string::npos) {
    if (prim_part) *prim_part = path;
    if (prop_part) prop_part->clear();
    return;
  }
  if (prim_part) *prim_part = path.substr(0, dot);
  if (prop_part) *prop_part = path.substr(dot + 1);
}

// Absolute prim path: "/", or "/Elem(/Elem)*" with identifier elements and no
// property part.
bool IsValidAbsolutePrimPathString(const std::string &path, std::string *err) {
  if (path.empty()) {
    if (err) *err = "path is empty";
    return false;
  }
  if (path[0] != '/') {
    if (err) *err = "path must be absolute (start with '/')";
    return false;
  }
  if (path.find('.') != std::string::npos) {
    if (err) *err = "prim path must not contain a property part";
    return false;
  }
  if (path == "/") {
    return true;
  }
  if (path.find("//") != std::string::npos) {
    if (err) *err = "path contains an empty element (`//`)";
    return false;
  }
  if (path.back() == '/') {
    if (err) *err = "path must not end with '/'";
    return false;
  }
  const std::vector<std::string> elems = SplitString(path.substr(1), '/');
  for (const std::string &elem : elems) {
    if (!IsValidIdentifier(elem)) {
      if (err) *err = "path element `" + elem + "` is not a valid identifier";
      return false;
    }
  }
  return true;
}

// Relationship / composition-arc target: absolute non-root prim path.
bool IsValidAbsolutePrimTargetPathString(const std::string &path,
                                         std::string *err) {
  if (!IsValidAbsolutePrimPathString(path, err)) {
    return false;
  }
  if (path == "/") {
    if (err) *err = "target must be an absolute non-root prim path";
    return false;
  }
  return true;
}

// Scene path (relationship target that may address a property).
bool IsValidScenePathString(const std::string &path, std::string *err) {
  std::string prim_part;
  std::string prop_part;
  SplitScenePathString(path, &prim_part, &prop_part);
  if (path.find('.') != std::string::npos && prop_part.empty()) {
    if (err) *err = "path has an empty property part";
    return false;
  }
  if (!IsValidAbsolutePrimPathString(prim_part, err)) {
    return false;
  }
  if (!prop_part.empty() && !IsValidNamespacedIdentifier(prop_part)) {
    if (err) *err = "property part `" + prop_part + "` is not a valid name";
    return false;
  }
  return true;
}

// Connection target: absolute prim path plus a mandatory property part.
bool IsValidConnectionTargetPathString(const std::string &path,
                                       std::string *err) {
  std::string prim_part;
  std::string prop_part;
  SplitScenePathString(path, &prim_part, &prop_part);
  if (prop_part.empty()) {
    if (err) *err = "connection target must be a prim property path";
    return false;
  }
  if (!IsValidAbsolutePrimPathString(prim_part, err)) {
    return false;
  }
  if (!IsValidNamespacedIdentifier(prop_part)) {
    if (err) *err = "property part `" + prop_part + "` is not a valid name";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// PrimSpec property adapters. In next-core, attributes live in the PropIndex
// (PropSlot) while relationships live in a separate name->targets map.
// ---------------------------------------------------------------------------

const PropSlot *GetSlot(const PrimSpec &ps, const std::string &name) {
  return ps.property(name);
}

bool IsRelationshipProp(const PrimSpec &ps, const std::string &name) {
  if (ps.relationship(name)) {
    return true;
  }
  const PropSlot *slot = GetSlot(ps, name);
  return slot && slot->is_relationship();
}

bool HasProperty(const PrimSpec &ps, const std::string &name) {
  return GetSlot(ps, name) != nullptr || ps.relationship(name) != nullptr;
}

bool HasAttributeProp(const PrimSpec &ps, const std::string &name) {
  const PropSlot *slot = GetSlot(ps, name);
  return slot && !slot->is_relationship();
}

bool HasConnections(const PrimSpec &ps, const std::string &name) {
  const std::vector<Path> *conns = ps.connection(name);
  if (conns && !conns->empty()) {
    return true;
  }
  const PropSlot *slot = GetSlot(ps, name);
  return slot && slot->is_connection();
}

// Authored default value of an attribute (nullptr when the slot is missing, a
// relationship, connection/declared-only, or an authored `= None` block).
const Value *GetAttrValue(const PrimSpec &ps, const std::string &name) {
  const PropSlot *slot = GetSlot(ps, name);
  if (!slot || slot->is_relationship()) {
    return nullptr;
  }
  const Value *v = ps.property_value(name);
  if (!v || v->is_empty() || v->is_block()) {
    return nullptr;
  }
  return v;
}

const std::vector<Path> &RelTargets(const PrimSpec &ps,
                                    const std::string &name) {
  static const std::vector<Path> kEmpty;
  const std::vector<Path> *targets = ps.relationship(name);
  return targets ? *targets : kEmpty;
}

// Declared USD type name of a property, or a name derived from the authored
// value type. Empty when neither is available.
std::string AttrTypeNameOf(const PrimSpec &ps, const std::string &name) {
  const std::string *declared = ps.property_type_name(name);
  if (declared && !declared->empty()) {
    std::string s = *declared;
    // Compare the element type; role information is preserved in declared
    // names ("color3f", "texCoord2f", ...).
    if (EndsWith(s, "[]")) {
      s = s.substr(0, s.size() - 2);
    }
    return s;
  }
  const Value *v = GetAttrValue(ps, name);
  if (!v) {
    return std::string();
  }
  const char *n = GetTypeName(v->type_id());
  return n ? std::string(n) : std::string();
}

// ---------------------------------------------------------------------------
// Value adapters
// ---------------------------------------------------------------------------

bool ValueToDouble(const Value &v, double *out) {
  if (!out || v.is_array()) {
    return false;
  }
  if (const float *f = v.as_float()) {
    *out = static_cast<double>(*f);
    return true;
  }
  if (const double *d = v.as_double()) {
    *out = *d;
    return true;
  }
  if (const int32_t *i = v.as_int()) {
    *out = static_cast<double>(*i);
    return true;
  }
  if (const int64_t *i64 = v.as_int64()) {
    *out = static_cast<double>(*i64);
    return true;
  }
  if (const uint32_t *u = v.as_uint()) {
    *out = static_cast<double>(*u);
    return true;
  }
  return false;
}

bool ValueToInt64(const Value &v, int64_t *out) {
  if (!out || v.is_array()) {
    return false;
  }
  if (const int32_t *i = v.as_int()) {
    *out = *i;
    return true;
  }
  if (const int64_t *i64 = v.as_int64()) {
    *out = *i64;
    return true;
  }
  if (const uint32_t *u = v.as_uint()) {
    *out = static_cast<int64_t>(*u);
    return true;
  }
  return false;
}

bool IsFloatTriple(TypeId id) {
  return id == TypeId::Float3 || id == TypeId::Point3f ||
         id == TypeId::Vector3f || id == TypeId::Normal3f;
}

bool IsDoubleTriple(TypeId id) {
  return id == TypeId::Double3 || id == TypeId::Point3d ||
         id == TypeId::Vector3d || id == TypeId::Normal3d;
}

bool ValueToVec3(const Value &v, std::array<double, 3> *out) {
  if (!out || v.is_array()) {
    return false;
  }
  if (IsFloatTriple(v.type_id())) {
    const float *p = static_cast<const float *>(v.raw_data());
    if (!p) return false;
    (*out)[0] = static_cast<double>(p[0]);
    (*out)[1] = static_cast<double>(p[1]);
    (*out)[2] = static_cast<double>(p[2]);
    return true;
  }
  if (IsDoubleTriple(v.type_id())) {
    const double *p = static_cast<const double *>(v.raw_data());
    if (!p) return false;
    (*out)[0] = p[0];
    (*out)[1] = p[1];
    (*out)[2] = p[2];
    return true;
  }
  return false;
}

bool ValueToQuat(const Value &v, std::array<double, 4> *out) {
  if (!out || v.is_array()) {
    return false;
  }
  if (v.type_id() == TypeId::Quatf) {
    const float *p = static_cast<const float *>(v.raw_data());
    if (!p) return false;
    for (size_t i = 0; i < 4; i++) (*out)[i] = static_cast<double>(p[i]);
    return true;
  }
  if (v.type_id() == TypeId::Quatd) {
    const double *p = static_cast<const double *>(v.raw_data());
    if (!p) return false;
    for (size_t i = 0; i < 4; i++) (*out)[i] = p[i];
    return true;
  }
  return false;
}

bool ValueToFloat2(const Value &v, std::array<float, 2> *out) {
  if (!out || v.is_array()) {
    return false;
  }
  if (v.type_id() == TypeId::Float2 || v.type_id() == TypeId::Texcoord2f) {
    const float *p = static_cast<const float *>(v.raw_data());
    if (!p) return false;
    (*out)[0] = p[0];
    (*out)[1] = p[1];
    return true;
  }
  return false;
}

bool GetTokenProperty(const PrimSpec &ps, const std::string &name,
                      std::string *out) {
  if (!out) return false;
  const Value *v = GetAttrValue(ps, name);
  if (!v) return false;
  const std::string *tok = v->as_token();
  if (!tok) return false;
  *out = *tok;
  return true;
}

bool GetStringProperty(const PrimSpec &ps, const std::string &name,
                       std::string *out) {
  if (!out) return false;
  const Value *v = GetAttrValue(ps, name);
  if (!v) return false;
  const std::string *str = v->as_string();
  if (!str) return false;
  *out = *str;
  return true;
}

bool GetStringLikeProperty(const PrimSpec &ps, const std::string &name,
                           std::string *out) {
  return GetTokenProperty(ps, name, out) || GetStringProperty(ps, name, out);
}

bool GetAssetPathProperty(const PrimSpec &ps, const std::string &name,
                          std::string *out) {
  if (!out) return false;
  const Value *v = GetAttrValue(ps, name);
  if (!v) return false;
  const std::string *asset = v->as_asset_path();
  if (!asset) return false;
  *out = *asset;
  return true;
}

bool GetNumericScalarProperty(const PrimSpec &ps, const std::string &name,
                              double *out) {
  const Value *v = GetAttrValue(ps, name);
  if (!v) return false;
  return ValueToDouble(*v, out);
}

bool GetIntegerScalarProperty(const PrimSpec &ps, const std::string &name,
                              int64_t *out) {
  const Value *v = GetAttrValue(ps, name);
  if (!v) return false;
  return ValueToInt64(*v, out);
}

bool GetIntArrayProperty(const PrimSpec &ps, const std::string &name,
                         std::vector<int32_t> *out) {
  if (!out) return false;
  const Value *v = GetAttrValue(ps, name);
  if (!v || v->type_id() != TypeId::Int || !v->is_array()) return false;
  const std::vector<int32_t> *arr = v->as_int_array();
  if (!arr) return false;
  *out = *arr;
  return true;
}

// Scalar float[] / double[] arrays converted to double.
bool ValueToDoubleScalarArray(const Value &v, std::vector<double> *out) {
  if (!out || !v.is_array()) return false;
  if (v.type_id() == TypeId::Double || v.type_id() == TypeId::TimeCode) {
    const std::vector<double> *arr = v.as_double_array();
    if (!arr) return false;
    *out = *arr;
    return true;
  }
  if (v.type_id() == TypeId::Float) {
    const std::vector<float> *arr = v.as_float_array();
    if (!arr) return false;
    out->assign(arr->begin(), arr->end());
    return true;
  }
  return false;
}

bool GetDoubleArrayProperty(const PrimSpec &ps, const std::string &name,
                            std::vector<double> *out) {
  const Value *v = GetAttrValue(ps, name);
  if (!v) return false;
  return ValueToDoubleScalarArray(*v, out);
}

bool GetFloatArrayProperty(const PrimSpec &ps, const std::string &name,
                           std::vector<float> *out) {
  if (!out) return false;
  const Value *v = GetAttrValue(ps, name);
  if (!v || !v->is_array()) return false;
  if (v->type_id() == TypeId::Float) {
    const std::vector<float> *arr = v->as_float_array();
    if (!arr) return false;
    *out = *arr;
    return true;
  }
  if (v->type_id() == TypeId::Double) {
    const std::vector<double> *arr = v->as_double_array();
    if (!arr) return false;
    out->assign(arr->begin(), arr->end());
    return true;
  }
  return false;
}

bool GetTokenArrayProperty(const PrimSpec &ps, const std::string &name,
                           std::vector<std::string> *out) {
  if (!out) return false;
  const Value *v = GetAttrValue(ps, name);
  if (!v || !v->is_array()) return false;
  const std::vector<std::string> *arr = v->as_token_array();
  if (!arr) return false;
  *out = *arr;
  return true;
}

// Generic array length (any element type; lazy arrays report their size
// without decoding).
bool GetValueArrayLength(const Value &v, size_t *size) {
  if (!size || !v.is_array() || v.is_block()) {
    return false;
  }
  *size = v.array_size();
  return true;
}

bool GetArrayLengthProperty(const PrimSpec &ps, const std::string &name,
                            size_t *size) {
  const Value *v = GetAttrValue(ps, name);
  if (!v) return false;
  return GetValueArrayLength(*v, size);
}

// points/positions: array of 3-component float/double elements.
bool GetPoint3ArrayLen(const PrimSpec &ps, const std::string &name,
                       size_t *size) {
  if (!size) return false;
  const Value *v = GetAttrValue(ps, name);
  if (!v || !v->is_array()) return false;
  if (!IsFloatTriple(v->type_id()) && !IsDoubleTriple(v->type_id())) {
    return false;
  }
  *size = v->array_size();
  return true;
}

// extent: float3[2] (Float3-family array of 2, or the crate Extent scalar).
bool GetExtentProperty(const PrimSpec &ps, const std::string &name,
                       std::vector<std::array<float, 3>> *out) {
  if (!out) return false;
  const Value *v = GetAttrValue(ps, name);
  if (!v) return false;
  out->clear();
  if (!v->is_array() && v->type_id() == TypeId::Extent) {
    const float *p = static_cast<const float *>(v->raw_data());
    if (!p) return false;
    out->push_back({p[0], p[1], p[2]});
    out->push_back({p[3], p[4], p[5]});
    return true;
  }
  if (v->is_array() && IsFloatTriple(v->type_id())) {
    const std::vector<float> *flat = v->as_float_array();
    if (!flat) return false;
    for (size_t i = 0; i + 2 < flat->size(); i += 3) {
      out->push_back({(*flat)[i], (*flat)[i + 1], (*flat)[i + 2]});
    }
    return true;
  }
  if (v->is_array() && IsDoubleTriple(v->type_id())) {
    const std::vector<double> *flat = v->as_double_array();
    if (!flat) return false;
    for (size_t i = 0; i + 2 < flat->size(); i += 3) {
      out->push_back({static_cast<float>((*flat)[i]),
                      static_cast<float>((*flat)[i + 1]),
                      static_cast<float>((*flat)[i + 2])});
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Time samples
// ---------------------------------------------------------------------------

const std::vector<std::pair<double, uint32_t>> *GetTimeSamplesOf(
    const PrimSpec &ps, const std::string &name) {
  const PropNameId id = GetPropNameTable().find(name);
  if (!id.is_valid()) {
    return nullptr;
  }
  return ps.time_samples(id);
}

void ValidateTimeSamplesProp(const PrimSpec &ps, const std::string &prop_name,
                             const std::string &rule_id,
                             const std::string &location,
                             USDValidationResult *result) {
  const auto *samples = GetTimeSamplesOf(ps, prop_name);
  if (!samples) {
    return;
  }
  // NOTE: next TimeSampleStorage keeps samples sorted with last-wins upsert,
  // so the legacy "strictly increasing" violation is normally unrepresentable;
  // the check is kept as a defensive guard.
  bool have_prev = false;
  double prev = 0.0;
  for (size_t i = 0; i < samples->size(); i++) {
    const double t = (*samples)[i].first;
    if (!std::isfinite(t)) {
      AddError(result, rule_id, location,
               "timeSamples sample " + std::to_string(i) +
                   " has a non-finite time");
    }
    if (have_prev && t <= prev) {
      AddError(result, rule_id, location,
               "timeSamples must be strictly increasing; sample " +
                   std::to_string(i) + " is at " + std::to_string(t) +
                   " after " + std::to_string(prev));
    }
    have_prev = true;
    prev = t;
  }
}

bool IsLengthCompatible(size_t count, size_t expected, bool allow_singleton) {
  return count == expected || (allow_singleton && count == 1);
}

// Default + every time sample of `prop_name` must be an array of `expected`
// elements (optionally allowing a length-1 constant).
void ValidateArrayLengthAtAllSamples(const PrimSpec &ps,
                                     const std::string &prop_name,
                                     const std::string &rule_id,
                                     const std::string &location,
                                     size_t expected,
                                     const std::string &expected_name,
                                     USDValidationResult *result) {
  const Value *v = GetAttrValue(ps, prop_name);
  if (v) {
    size_t count = 0;
    if (GetValueArrayLength(*v, &count) && count != expected) {
      AddError(result, rule_id, location,
               prop_name + " has " + std::to_string(count) +
                   " default elements, but " + expected_name + " has " +
                   std::to_string(expected) + " elements");
    }
  }

  const auto *samples = GetTimeSamplesOf(ps, prop_name);
  if (!samples) {
    return;
  }
  ValidateTimeSamplesProp(ps, prop_name, rule_id, location, result);
  for (size_t i = 0; i < samples->size(); i++) {
    const Value *sv = ps.time_sample_value((*samples)[i].second);
    if (!sv || sv->is_empty() || sv->is_block()) {
      continue;
    }
    size_t count = 0;
    if (!GetValueArrayLength(*sv, &count)) {
      AddWarning(result, rule_id, location,
                 prop_name + " timeSamples sample " + std::to_string(i) +
                     " should be an array value");
      continue;
    }
    if (count != expected) {
      AddError(result, rule_id, location,
               prop_name + " timeSamples sample " + std::to_string(i) +
                   " at time " + std::to_string((*samples)[i].first) +
                   " has " + std::to_string(count) + " elements, but " +
                   expected_name + " has " + std::to_string(expected) +
                   " elements");
    }
  }
}

// ---------------------------------------------------------------------------
// Generic property validators (rule-id parameterized; ported from legacy)
// ---------------------------------------------------------------------------

bool IsPrimvarPropertyName(const std::string &prop_name) {
  return StartsWith(prop_name, "primvars:") &&
         prop_name.size() > std::string("primvars:").size() &&
         !EndsWith(prop_name, ":indices");
}

bool IsValidPrimvarInterpolation(const std::string &interp) {
  return interp == "constant" || interp == "uniform" ||
         interp == "vertex" || interp == "varying" ||
         interp == "faceVarying";
}

void ValidateTokenSetProperty(const PrimSpec &ps, const std::string &prop_name,
                              const std::set<std::string> &allowed,
                              const std::string &rule_id,
                              const std::string &prim_location,
                              USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             prop_name + " must be a token attribute");
    return;
  }
  std::string token;
  if (!GetTokenProperty(ps, prop_name, &token)) {
    AddError(result, rule_id, location,
             prop_name + " must have a token value");
    return;
  }
  if (allowed.count(token) == 0) {
    AddError(result, rule_id, location,
             prop_name + " token `" + token + "` is not valid");
  }
}

void ValidatePositiveNumericProperty(const PrimSpec &ps,
                                     const std::string &prop_name,
                                     const std::string &rule_id,
                                     const std::string &prim_location,
                                     USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             prop_name + " must be a numeric attribute");
    return;
  }
  double value = 0.0;
  if (!GetNumericScalarProperty(ps, prop_name, &value)) {
    AddError(result, rule_id, location,
             prop_name + " must have a numeric scalar value");
    return;
  }
  if (!std::isfinite(value) || value <= 0.0) {
    AddError(result, rule_id, location,
             prop_name + " must be finite and greater than 0");
  }
}

void ValidateNonNegativeNumericProperty(const PrimSpec &ps,
                                        const std::string &prop_name,
                                        const std::string &rule_id,
                                        const std::string &prim_location,
                                        USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  double value = 0.0;
  if (IsRelationshipProp(ps, prop_name) ||
      !GetNumericScalarProperty(ps, prop_name, &value)) {
    AddError(result, rule_id, location,
             prop_name + " must have a numeric scalar value");
    return;
  }
  if (!std::isfinite(value) || value < 0.0) {
    AddError(result, rule_id, location,
             prop_name + " must be finite and non-negative");
  }
}

void ValidateNonNegativeIntegerProperty(const PrimSpec &ps,
                                        const std::string &prop_name,
                                        const std::string &rule_id,
                                        const std::string &prim_location,
                                        USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  int64_t value = 0;
  if (IsRelationshipProp(ps, prop_name) ||
      !GetIntegerScalarProperty(ps, prop_name, &value)) {
    AddError(result, rule_id, location,
             prop_name + " must have an integer scalar value");
    return;
  }
  if (value < 0) {
    AddError(result, rule_id, location, prop_name + " must be non-negative");
  }
}

void ValidateFiniteVec3Property(const PrimSpec &ps,
                                const std::string &prop_name,
                                const std::string &rule_id,
                                const std::string &prim_location,
                                USDValidationResult *result,
                                bool require_nonzero) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             prop_name + " must be a 3-component numeric attribute");
    return;
  }
  std::array<double, 3> value;
  const Value *v = GetAttrValue(ps, prop_name);
  if (!v || !ValueToVec3(*v, &value)) {
    AddError(result, rule_id, location,
             prop_name + " must have a 3-component numeric value");
    return;
  }
  const double len2 =
      value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
  if (!std::isfinite(value[0]) || !std::isfinite(value[1]) ||
      !std::isfinite(value[2]) || (require_nonzero && len2 == 0.0)) {
    AddError(result, rule_id, location,
             prop_name + " must be finite" +
                 std::string(require_nonzero ? " and non-zero" : ""));
  }
}

void ValidateFiniteNonNegativeVec3Property(const PrimSpec &ps,
                                           const std::string &prop_name,
                                           const std::string &rule_id,
                                           const std::string &prim_location,
                                           USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             prop_name + " must be a 3-component numeric attribute");
    return;
  }
  std::array<double, 3> value;
  const Value *v = GetAttrValue(ps, prop_name);
  if (!v || !ValueToVec3(*v, &value)) {
    AddError(result, rule_id, location,
             prop_name + " must have a 3-component numeric value");
    return;
  }
  for (double c : value) {
    if (!std::isfinite(c) || c < 0.0) {
      AddError(result, rule_id, location,
               prop_name + " must be finite and non-negative");
      return;
    }
  }
}

void ValidateQuaternionProperty(const PrimSpec &ps,
                                const std::string &prop_name,
                                const std::string &rule_id,
                                const std::string &prim_location,
                                USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             prop_name + " must be a quaternion attribute");
    return;
  }
  std::array<double, 4> value;
  const Value *v = GetAttrValue(ps, prop_name);
  if (!v || !ValueToQuat(*v, &value)) {
    AddError(result, rule_id, location,
             prop_name + " must have a quaternion value");
    return;
  }
  const double len2 = value[0] * value[0] + value[1] * value[1] +
                      value[2] * value[2] + value[3] * value[3];
  for (double c : value) {
    if (!std::isfinite(c)) {
      AddError(result, rule_id, location,
               prop_name + " must be finite and non-zero");
      return;
    }
  }
  if (len2 == 0.0) {
    AddError(result, rule_id, location,
             prop_name + " must be finite and non-zero");
  } else if (std::fabs(len2 - 1.0) > 1.0e-3) {
    AddWarning(result, rule_id, location,
               prop_name + " should usually be unit length");
  }
}

void ValidateNumericArrayLengthProperty(const PrimSpec &ps,
                                        const std::string &prop_name,
                                        size_t expected,
                                        const std::string &rule_id,
                                        const std::string &prim_location,
                                        USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             prop_name + " must be a numeric array attribute");
    return;
  }
  std::vector<double> values;
  if (!GetDoubleArrayProperty(ps, prop_name, &values)) {
    AddError(result, rule_id, location,
             prop_name + " must have a numeric array value");
    return;
  }
  if (values.size() != expected) {
    AddError(result, rule_id, location,
             prop_name + " must have " + std::to_string(expected) +
                 " elements");
  }
  for (double value : values) {
    if (!std::isfinite(value)) {
      AddError(result, rule_id, location,
               prop_name + " elements must be finite");
      return;
    }
  }
}

void ValidateNumericArrayFiniteProperty(const PrimSpec &ps,
                                        const std::string &prop_name,
                                        const std::string &rule_id,
                                        const std::string &prim_location,
                                        USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             prop_name + " must be a numeric array attribute");
    return;
  }
  std::vector<double> values;
  if (!GetDoubleArrayProperty(ps, prop_name, &values)) {
    AddError(result, rule_id, location,
             prop_name + " must have a numeric array value");
    return;
  }
  for (double value : values) {
    if (!std::isfinite(value)) {
      AddError(result, rule_id, location,
               prop_name + " elements must be finite");
      return;
    }
  }
}

bool GetRelationshipTargetCount(const PrimSpec &ps,
                                const std::string &rel_name, size_t *count) {
  if (!count) {
    return false;
  }
  if (!IsRelationshipProp(ps, rel_name)) {
    return false;
  }
  *count = RelTargets(ps, rel_name).size();
  return true;
}

void ValidateIntArrayLengthMatchesRelationship(
    const PrimSpec &ps, const std::string &array_prop,
    const std::string &rel_prop, const std::string &rule_id,
    const std::string &prim_location, USDValidationResult *result) {
  if (!HasProperty(ps, array_prop)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, array_prop);
  std::vector<int32_t> values;
  if (IsRelationshipProp(ps, array_prop) ||
      !GetIntArrayProperty(ps, array_prop, &values)) {
    AddError(result, rule_id, location,
             array_prop + " must have an int array value");
    return;
  }
  size_t target_count = 0;
  if (!GetRelationshipTargetCount(ps, rel_prop, &target_count)) {
    AddError(result, rule_id, location,
             array_prop + " requires relationship " + rel_prop);
    return;
  }
  if (values.size() != target_count) {
    AddError(result, rule_id, location,
             array_prop + " has " + std::to_string(values.size()) +
                 " elements, but " + rel_prop + " has " +
                 std::to_string(target_count) + " targets");
  }
}

void ValidateNumericArrayLengthMatchesRelationship(
    const PrimSpec &ps, const std::string &array_prop,
    const std::string &rel_prop, const std::string &rule_id,
    const std::string &prim_location, USDValidationResult *result) {
  if (!HasProperty(ps, array_prop)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, array_prop);
  std::vector<double> values;
  if (IsRelationshipProp(ps, array_prop) ||
      !GetDoubleArrayProperty(ps, array_prop, &values)) {
    AddError(result, rule_id, location,
             array_prop + " must have a numeric array value");
    return;
  }
  size_t target_count = 0;
  if (!GetRelationshipTargetCount(ps, rel_prop, &target_count)) {
    AddError(result, rule_id, location,
             array_prop + " requires relationship " + rel_prop);
    return;
  }
  if (values.size() != target_count) {
    AddError(result, rule_id, location,
             array_prop + " has " + std::to_string(values.size()) +
                 " elements, but " + rel_prop + " has " +
                 std::to_string(target_count) + " targets");
  }
}

void ValidateNumericMinMaxPair(const PrimSpec &ps, const std::string &min_prop,
                               const std::string &max_prop,
                               const std::string &rule_id,
                               const std::string &prim_location,
                               USDValidationResult *result) {
  for (const std::string &prop_name : {min_prop, max_prop}) {
    if (HasProperty(ps, prop_name)) {
      double value = 0.0;
      if (IsRelationshipProp(ps, prop_name) ||
          !GetNumericScalarProperty(ps, prop_name, &value) ||
          !std::isfinite(value)) {
        AddError(result, rule_id,
                 MakePropertyLocation(prim_location, prop_name),
                 prop_name + " must have a finite numeric scalar value");
      }
    }
  }
  double min_value = 0.0;
  double max_value = 0.0;
  if (GetNumericScalarProperty(ps, min_prop, &min_value) &&
      GetNumericScalarProperty(ps, max_prop, &max_value) &&
      min_value > max_value) {
    AddError(result, rule_id, prim_location,
             min_prop + " must be <= " + max_prop);
  }
}

void ValidateArrayLengthProperty(const PrimSpec &ps,
                                 const std::string &prop_name, size_t expected,
                                 bool allow_singleton,
                                 const std::string &rule_id,
                                 const std::string &prim_location,
                                 const std::string &expected_name,
                                 USDValidationResult *result) {
  if (!HasAttributeProp(ps, prop_name)) {
    return;
  }
  const std::string location = MakePropertyLocation(prim_location, prop_name);
  const Value *v = GetAttrValue(ps, prop_name);
  size_t count = 0;
  if (v && GetValueArrayLength(*v, &count) &&
      !IsLengthCompatible(count, expected, allow_singleton)) {
    AddError(result, rule_id, location,
             prop_name + " has " + std::to_string(count) +
                 " elements, but must have " +
                 (allow_singleton ? "1 or " : "") + std::to_string(expected) +
                 " elements to match " + expected_name);
  }
  const auto *samples = GetTimeSamplesOf(ps, prop_name);
  if (!samples) {
    return;
  }
  ValidateTimeSamplesProp(ps, prop_name, rule_id, location, result);
  for (size_t i = 0; i < samples->size(); i++) {
    const Value *sv = ps.time_sample_value((*samples)[i].second);
    if (!sv || sv->is_empty() || sv->is_block()) {
      continue;
    }
    size_t sample_count = 0;
    if (!GetValueArrayLength(*sv, &sample_count)) {
      AddWarning(result, rule_id, location,
                 prop_name + " timeSamples sample " + std::to_string(i) +
                     " should be an array value");
      continue;
    }
    if (!IsLengthCompatible(sample_count, expected, allow_singleton)) {
      AddError(result, rule_id, location,
               prop_name + " timeSamples sample " + std::to_string(i) +
                   " at time " + std::to_string((*samples)[i].first) +
                   " has " + std::to_string(sample_count) +
                   " elements, but must have " +
                   (allow_singleton ? "1 or " : "") +
                   std::to_string(expected) + " elements to match " +
                   expected_name);
    }
  }
}

void ValidateNumericRangeProperty(const PrimSpec &ps,
                                  const std::string &prop_name,
                                  double min_value, double max_value,
                                  const std::string &rule_id,
                                  const std::string &prim_location,
                                  USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  double value = 0.0;
  if (IsRelationshipProp(ps, prop_name) ||
      !GetNumericScalarProperty(ps, prop_name, &value)) {
    AddError(result, rule_id, MakePropertyLocation(prim_location, prop_name),
             prop_name + " must be a numeric attribute");
    return;
  }
  if (!std::isfinite(value) || value < min_value || value > max_value) {
    AddError(result, rule_id, MakePropertyLocation(prim_location, prop_name),
             prop_name + " must be finite and in range [" +
                 std::to_string(min_value) + ", " +
                 std::to_string(max_value) + "]");
  }
}

void ValidateNumericMinProperty(const PrimSpec &ps,
                                const std::string &prop_name, double min_value,
                                const std::string &rule_id,
                                const std::string &prim_location,
                                USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  double value = 0.0;
  if (IsRelationshipProp(ps, prop_name) ||
      !GetNumericScalarProperty(ps, prop_name, &value)) {
    AddError(result, rule_id, MakePropertyLocation(prim_location, prop_name),
             prop_name + " must be a numeric attribute");
    return;
  }
  if (!std::isfinite(value) || value < min_value) {
    AddError(result, rule_id, MakePropertyLocation(prim_location, prop_name),
             prop_name + " must be finite and >= " + std::to_string(min_value));
  }
}

void ValidateFiniteNumericProperty(const PrimSpec &ps,
                                   const std::string &prop_name,
                                   const std::string &rule_id,
                                   const std::string &prim_location,
                                   USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  double value = 0.0;
  if (IsRelationshipProp(ps, prop_name) ||
      !GetNumericScalarProperty(ps, prop_name, &value)) {
    AddError(result, rule_id, MakePropertyLocation(prim_location, prop_name),
             prop_name + " must be a numeric attribute");
    return;
  }
  if (!std::isfinite(value)) {
    AddError(result, rule_id, MakePropertyLocation(prim_location, prop_name),
             prop_name + " must be finite");
  }
}

void ValidateAssetPathProperty(const PrimSpec &ps,
                               const std::string &prop_name,
                               const std::string &rule_id,
                               const std::string &prim_location,
                               USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  std::string asset;
  if (IsRelationshipProp(ps, prop_name) ||
      !GetAssetPathProperty(ps, prop_name, &asset)) {
    // A connected asset input has no local value; treat a connection-only
    // property as satisfying the "asset attribute" requirement.
    if (HasConnections(ps, prop_name) && !IsRelationshipProp(ps, prop_name)) {
      return;
    }
    AddError(result, rule_id, MakePropertyLocation(prim_location, prop_name),
             prop_name + " must be an asset attribute");
    return;
  }
  if (asset.empty()) {
    AddWarning(result, rule_id, MakePropertyLocation(prim_location, prop_name),
               prop_name + " is authored as an empty asset path");
  }
}

// ---------------------------------------------------------------------------
// Relationship / connection target validators
// ---------------------------------------------------------------------------

const std::string *FindPrimType(const PrimTypeByPath &prim_types,
                                const std::string &prim_path) {
  auto it = prim_types.find(prim_path);
  if (it == prim_types.end()) {
    return nullptr;
  }
  return &it->second;
}

const PrimSpecifier *FindPrimSpecifier(
    const PrimSpecifierByPath &prim_specifiers, const std::string &prim_path) {
  auto it = prim_specifiers.find(prim_path);
  if (it == prim_specifiers.end()) {
    return nullptr;
  }
  return &it->second;
}

// Targets must be absolute non-root prim paths; empty target list warns.
void ValidateRelationshipTargetPaths(const PrimSpec &ps,
                                     const std::string &rel_name,
                                     const std::string &rule_id,
                                     const std::string &location,
                                     USDValidationResult *result) {
  if (!IsRelationshipProp(ps, rel_name)) {
    AddError(result, rule_id, location, "expected a relationship");
    return;
  }
  const std::vector<Path> &targets = RelTargets(ps, rel_name);
  if (targets.empty()) {
    AddWarning(result, rule_id, location, "relationship has no targets");
    return;
  }
  for (const Path &target : targets) {
    std::string err;
    if (!IsValidAbsolutePrimTargetPathString(target.str(), &err)) {
      AddError(result, rule_id, location,
               "contains invalid target path `" + target.str() + "`: " + err);
    }
  }
}

// Targets may be scene paths (prim or property paths); used by CollectionAPI.
void ValidateRelationshipScenePaths(const PrimSpec &ps,
                                    const std::string &rel_name,
                                    const std::string &rule_id,
                                    const std::string &location,
                                    USDValidationResult *result) {
  for (const Path &target : RelTargets(ps, rel_name)) {
    std::string err;
    if (!IsValidScenePathString(target.str(), &err)) {
      AddError(result, rule_id, location,
               "contains invalid target path `" + target.str() + "`: " + err);
    }
  }
}

void ValidateRelationshipTargetPrimTypes(
    const PrimSpec &ps, const std::string &rel_name,
    const std::string &rule_id, const std::string &location,
    const PrimTypeByPath &prim_types,
    const std::set<std::string> &expected_types, USDValidationResult *result) {
  if (!IsRelationshipProp(ps, rel_name)) {
    return;
  }
  for (const Path &target : RelTargets(ps, rel_name)) {
    std::string prim_part;
    SplitScenePathString(target.str(), &prim_part, nullptr);
    const std::string *target_type = FindPrimType(prim_types, prim_part);
    if (target_type && !target_type->empty() &&
        expected_types.count(*target_type) == 0) {
      AddWarning(result, rule_id, location,
                 "relationship target `" + prim_part + "` has type `" +
                     *target_type + "`");
    }
  }
}

void ValidateConnectionTargets(const PrimSpec &ps,
                               const std::string &prop_name,
                               const std::string &rule_id,
                               const std::string &location,
                               const PrimTypeByPath &prim_types,
                               USDValidationResult *result,
                               bool require_output_target) {
  const std::vector<Path> *conns = ps.connection(prop_name);
  if (!conns) {
    return;
  }
  for (const Path &target : *conns) {
    std::string err;
    if (!IsValidConnectionTargetPathString(target.str(), &err)) {
      AddError(result, rule_id, location,
               "contains invalid connection target `" + target.str() +
                   "`: " + err);
      continue;
    }
    std::string prim_part;
    std::string target_prop;
    SplitScenePathString(target.str(), &prim_part, &target_prop);
    if (require_output_target && !IsShaderOutputName(target_prop)) {
      AddWarning(result, rule_id, location,
                 "connection target `" + target.str() +
                     "` is not an outputs:* source");
    } else if (!require_output_target && !IsShaderOutputName(target_prop) &&
               !IsShaderInputName(target_prop)) {
      AddWarning(result, rule_id, location,
                 "connection target `" + target.str() +
                     "` is not an inputs:* or outputs:* shade port");
    }
    const std::string *target_type = FindPrimType(prim_types, prim_part);
    if (target_type && !target_type->empty() &&
        !IsShadeConnectionTargetTypeName(*target_type)) {
      AddWarning(result, rule_id, location,
                 "connection target prim `" + prim_part + "` has type `" +
                     *target_type + "`, expected Material, Shader, or "
                     "NodeGraph");
    }
  }
}

// ---------------------------------------------------------------------------
// geom rules
// ---------------------------------------------------------------------------

// Ancestor-derived context threaded through the recursive prim walk.
struct AncestorContext {
  bool has_gprim_ancestor{false};
  std::string nearest_gprim_path;  // valid only when has_gprim_ancestor
  std::string parent_type;         // immediate parent typeName ("" at root)
  bool parent_mesh_face_count_known{false};
  size_t parent_mesh_face_count{0};
};

void ValidateGeomCommonProperties(const PrimSpec &ps,
                                  const std::string &prim_location,
                                  USDValidationResult *result) {
  static const std::set<std::string> kVisibility = {"inherited", "invisible"};
  static const std::set<std::string> kPurpose = {"default", "render", "proxy",
                                                 "guide"};
  static const std::set<std::string> kOrientation = {"rightHanded",
                                                     "leftHanded"};

  ValidateTokenSetProperty(ps, "visibility", kVisibility,
                           "geom.gprim.visibility", prim_location, result);
  ValidateTokenSetProperty(ps, "purpose", kPurpose, "geom.gprim.purpose",
                           prim_location, result);
  ValidateTokenSetProperty(ps, "orientation", kOrientation,
                           "geom.gprim.orientation", prim_location, result);

  std::vector<std::array<float, 3>> extent;
  if (GetExtentProperty(ps, "extent", &extent)) {
    const std::string location = MakePropertyLocation(prim_location, "extent");
    if (extent.size() != 2) {
      AddError(result, "geom.gprim.extent", location,
               "extent must contain exactly 2 points");
    } else {
      auto finite_point = [](const std::array<float, 3> &p) {
        return std::isfinite(p[0]) && std::isfinite(p[1]) &&
               std::isfinite(p[2]);
      };
      if (!finite_point(extent[0]) || !finite_point(extent[1])) {
        AddError(result, "geom.gprim.extent", location,
                 "extent bounds must be finite");
      }
      for (size_t axis = 0; axis < 3; axis++) {
        if (extent[0][axis] > extent[1][axis]) {
          AddError(result, "geom.gprim.extent", location,
                   "extent minimum must be <= maximum on every axis");
          break;
        }
      }
    }
  }

  size_t point_count = 0;
  const std::string &type_name = ps.type_name();
  if (type_name != "Points" && type_name != "PointInstancer" &&
      type_name != "BasisCurves" && type_name != "NurbsCurves" &&
      type_name != "HermiteCurves" && type_name != "NurbsPatch" &&
      GetPoint3ArrayLen(ps, "points", &point_count)) {
    ValidateArrayLengthProperty(ps, "velocities", point_count, false,
                                "geom.gprim.arraySize", prim_location,
                                "points", result);
    ValidateArrayLengthProperty(ps, "accelerations", point_count, false,
                                "geom.gprim.arraySize", prim_location,
                                "points", result);
  }
}

void ValidateGeomPrimitive(const PrimSpec &ps,
                           const std::string &prim_location,
                           USDValidationResult *result) {
  static const std::set<std::string> kAxis = {"X", "Y", "Z"};
  const std::string &type_name = ps.type_name();

  if (type_name == "Sphere") {
    ValidatePositiveNumericProperty(ps, "radius", "geom.primitive.size",
                                    prim_location, result);
  } else if (type_name == "Cube") {
    ValidatePositiveNumericProperty(ps, "size", "geom.primitive.size",
                                    prim_location, result);
  } else if (type_name == "Cone" || type_name == "Cylinder" ||
             type_name == "Capsule") {
    ValidatePositiveNumericProperty(ps, "radius", "geom.primitive.size",
                                    prim_location, result);
    ValidatePositiveNumericProperty(ps, "height", "geom.primitive.size",
                                    prim_location, result);
    ValidateTokenSetProperty(ps, "axis", kAxis, "geom.primitive.axis",
                             prim_location, result);
  } else if (type_name == "Cylinder_1" || type_name == "Capsule_1") {
    ValidatePositiveNumericProperty(ps, "radiusTop", "geom.primitive.size",
                                    prim_location, result);
    ValidatePositiveNumericProperty(ps, "radiusBottom", "geom.primitive.size",
                                    prim_location, result);
    ValidatePositiveNumericProperty(ps, "height", "geom.primitive.size",
                                    prim_location, result);
    ValidateTokenSetProperty(ps, "axis", kAxis, "geom.primitive.axis",
                             prim_location, result);
  } else if (type_name == "Plane") {
    ValidatePositiveNumericProperty(ps, "width", "geom.primitive.size",
                                    prim_location, result);
    ValidatePositiveNumericProperty(ps, "length", "geom.primitive.size",
                                    prim_location, result);
    ValidateTokenSetProperty(ps, "axis", kAxis, "geom.primitive.axis",
                             prim_location, result);
  }
}

void ValidateGeomPoints(const PrimSpec &ps, const std::string &prim_location,
                        USDValidationResult *result) {
  size_t point_count = 0;
  if (!GetPoint3ArrayLen(ps, "points", &point_count)) {
    return;
  }

  ValidateArrayLengthProperty(ps, "ids", point_count, false,
                              "geom.points.size", prim_location, "points",
                              result);
  ValidateArrayLengthProperty(ps, "widths", point_count, true,
                              "geom.points.size", prim_location, "points",
                              result);
  ValidateArrayLengthProperty(ps, "normals", point_count, true,
                              "geom.points.size", prim_location, "points",
                              result);
  ValidateArrayLengthProperty(ps, "velocities", point_count, false,
                              "geom.points.size", prim_location, "points",
                              result);
  ValidateArrayLengthProperty(ps, "accelerations", point_count, false,
                              "geom.points.size", prim_location, "points",
                              result);

  std::vector<float> widths;
  if (GetFloatArrayProperty(ps, "widths", &widths)) {
    for (size_t i = 0; i < widths.size(); i++) {
      if (!std::isfinite(widths[i]) || widths[i] < 0.0f) {
        AddError(result, "geom.points.widths",
                 MakePropertyLocation(prim_location, "widths"),
                 "widths[" + std::to_string(i) +
                     "] must be finite and non-negative");
      }
    }
  }
}

void ValidateGeomCurves(const PrimSpec &ps, const std::string &prim_location,
                        USDValidationResult *result) {
  static const std::set<std::string> kBasisCurveTypes = {"linear", "cubic"};
  static const std::set<std::string> kBasis = {"bezier", "bspline",
                                               "catmullRom"};
  static const std::set<std::string> kWrap = {"nonperiodic", "periodic",
                                              "pinned"};
  static const std::set<std::string> kForm = {"open", "closed", "periodic"};

  if (ps.type_name() == "BasisCurves") {
    ValidateTokenSetProperty(ps, "type", kBasisCurveTypes,
                             "geom.curves.tokens", prim_location, result);
    ValidateTokenSetProperty(ps, "basis", kBasis, "geom.curves.tokens",
                             prim_location, result);
    ValidateTokenSetProperty(ps, "wrap", kWrap, "geom.curves.tokens",
                             prim_location, result);
  } else if (ps.type_name() == "NurbsPatch") {
    ValidateTokenSetProperty(ps, "uForm", kForm, "geom.curves.tokens",
                             prim_location, result);
    ValidateTokenSetProperty(ps, "vForm", kForm, "geom.curves.tokens",
                             prim_location, result);
  }

  size_t point_count = 0;
  const bool have_points = GetPoint3ArrayLen(ps, "points", &point_count);
  std::vector<int32_t> counts;
  const bool have_counts =
      GetIntArrayProperty(ps, "curveVertexCounts", &counts);
  if (have_counts) {
    size_t total = 0;
    for (size_t i = 0; i < counts.size(); i++) {
      if (counts[i] <= 0) {
        AddError(result, "geom.curves.topology",
                 MakePropertyLocation(prim_location, "curveVertexCounts"),
                 "curveVertexCounts[" + std::to_string(i) +
                     "] must be positive");
        continue;
      }
      total += static_cast<size_t>(counts[i]);
    }
    if (have_points && total != point_count) {
      AddError(result, "geom.curves.topology",
               MakePropertyLocation(prim_location, "curveVertexCounts"),
               "sum(curveVertexCounts) is " + std::to_string(total) +
                   " but points has " + std::to_string(point_count) +
                   " elements");
    }
  }

  std::vector<float> widths;
  if (GetFloatArrayProperty(ps, "widths", &widths)) {
    for (size_t i = 0; i < widths.size(); i++) {
      if (!std::isfinite(widths[i]) || widths[i] < 0.0f) {
        AddError(result, "geom.curves.widths",
                 MakePropertyLocation(prim_location, "widths"),
                 "widths[" + std::to_string(i) +
                     "] must be finite and non-negative");
      }
    }

    const bool length_ok =
        widths.size() == 1 || (have_points && widths.size() == point_count) ||
        (have_counts && widths.size() == counts.size());
    if (!length_ok) {
      AddError(result, "geom.curves.widths",
               MakePropertyLocation(prim_location, "widths"),
               "widths length must be 1, match points, or match curve count");
    }
  }
}

void ValidatePointInstancer(const PrimSpec &ps,
                            const std::string &prim_location,
                            USDValidationResult *result) {
  size_t position_count = 0;
  const bool have_positions =
      GetPoint3ArrayLen(ps, "positions", &position_count);

  size_t prototype_count = 0;
  if (!HasProperty(ps, "prototypes")) {
    AddError(result, "geom.pointInstancer.prototypes",
             MakePropertyLocation(prim_location, "prototypes"),
             "PointInstancer must author rel prototypes");
  } else if (!IsRelationshipProp(ps, "prototypes")) {
    AddError(result, "geom.pointInstancer.prototypes",
             MakePropertyLocation(prim_location, "prototypes"),
             "prototypes must be a relationship");
  } else {
    const std::vector<Path> &targets = RelTargets(ps, "prototypes");
    prototype_count = targets.size();
    if (targets.empty()) {
      AddError(result, "geom.pointInstancer.prototypes",
               MakePropertyLocation(prim_location, "prototypes"),
               "prototypes relationship must target at least one prototype");
    }
    for (const Path &target : targets) {
      std::string err;
      if (!IsValidScenePathString(target.str(), &err)) {
        AddError(result, "geom.pointInstancer.prototypes",
                 MakePropertyLocation(prim_location, "prototypes"),
                 "prototypes target `" + target.str() +
                     "` is invalid: " + err);
      }
    }
  }

  std::vector<int32_t> proto_indices;
  if (GetIntArrayProperty(ps, "protoIndices", &proto_indices)) {
    if (have_positions && proto_indices.size() != position_count) {
      AddError(result, "geom.pointInstancer.indices",
               MakePropertyLocation(prim_location, "protoIndices"),
               "protoIndices length must match positions length");
    }
    for (size_t i = 0; i < proto_indices.size(); i++) {
      if (proto_indices[i] < 0 ||
          static_cast<size_t>(proto_indices[i]) >= prototype_count) {
        AddError(result, "geom.pointInstancer.indices",
                 MakePropertyLocation(prim_location, "protoIndices"),
                 "protoIndices[" + std::to_string(i) +
                     "] does not reference an authored prototype");
      }
    }
  }

  if (have_positions) {
    for (const char *prop_name :
         {"orientations", "scales", "ids", "velocities", "accelerations",
          "angularVelocities"}) {
      ValidateArrayLengthProperty(ps, prop_name, position_count, false,
                                  "geom.pointInstancer.arraySize",
                                  prim_location, "positions", result);
    }
  }
}

void ValidateCamera(const PrimSpec &ps, const std::string &prim_location,
                    USDValidationResult *result) {
  static const std::set<std::string> kProjection = {"perspective",
                                                    "orthographic"};
  static const std::set<std::string> kStereoRole = {"mono", "left", "right"};

  ValidateTokenSetProperty(ps, "projection", kProjection,
                           "geom.camera.tokens", prim_location, result);
  ValidateTokenSetProperty(ps, "stereoRole", kStereoRole,
                           "geom.camera.tokens", prim_location, result);

  const Value *cr = GetAttrValue(ps, "clippingRange");
  std::array<float, 2> clipping_range = {{0.0f, 0.0f}};
  if (cr && ValueToFloat2(*cr, &clipping_range)) {
    if (!std::isfinite(clipping_range[0]) ||
        !std::isfinite(clipping_range[1]) || clipping_range[0] < 0.0f ||
        clipping_range[1] <= clipping_range[0]) {
      AddError(result, "geom.camera.range",
               MakePropertyLocation(prim_location, "clippingRange"),
               "clippingRange must be finite with near >= 0 and far > near");
    }
  }

  ValidatePositiveNumericProperty(ps, "focalLength", "geom.camera.optics",
                                  prim_location, result);
  ValidatePositiveNumericProperty(ps, "horizontalAperture",
                                  "geom.camera.optics", prim_location, result);
  ValidatePositiveNumericProperty(ps, "verticalAperture",
                                  "geom.camera.optics", prim_location, result);
  ValidateNonNegativeNumericProperty(ps, "fStop", "geom.camera.optics",
                                     prim_location, result);

  double shutter_open = 0.0;
  double shutter_close = 0.0;
  const bool have_open =
      GetNumericScalarProperty(ps, "shutter:open", &shutter_open) ||
      GetNumericScalarProperty(ps, "shutterOpen", &shutter_open);
  const bool have_close =
      GetNumericScalarProperty(ps, "shutter:close", &shutter_close) ||
      GetNumericScalarProperty(ps, "shutterClose", &shutter_close);
  if (have_open && have_close &&
      (!std::isfinite(shutter_open) || !std::isfinite(shutter_close) ||
       shutter_close < shutter_open)) {
    AddError(result, "geom.camera.shutter", prim_location,
             "camera shutter close must be finite and >= shutter open");
  }
}

void ValidateMeshTopology(const PrimSpec &ps, const std::string &prim_location,
                          USDValidationResult *result,
                          size_t *out_face_count) {
  std::vector<int32_t> counts;
  std::vector<int32_t> indices;
  const bool have_counts = GetIntArrayProperty(ps, "faceVertexCounts", &counts);
  const bool have_indices =
      GetIntArrayProperty(ps, "faceVertexIndices", &indices);

  if (out_face_count && have_counts) {
    *out_face_count = counts.size();
  }

  if (!have_counts && !have_indices) {
    return;
  }

  size_t total = 0;
  if (have_counts) {
    for (size_t i = 0; i < counts.size(); i++) {
      if (counts[i] <= 0) {
        AddError(result, "geom.mesh.topology.count",
                 MakePropertyLocation(prim_location, "faceVertexCounts"),
                 "faceVertexCounts[" + std::to_string(i) + "] must be "
                 "positive, but is " + std::to_string(counts[i]));
        continue;
      }
      total += static_cast<size_t>(counts[i]);
    }
  }

  if (have_counts && have_indices && total != indices.size()) {
    AddError(result, "geom.mesh.topology.size",
             MakePropertyLocation(prim_location, "faceVertexIndices"),
             "sum(faceVertexCounts) is " + std::to_string(total) +
                 " but faceVertexIndices has " +
                 std::to_string(indices.size()) + " elements");
  }

  size_t point_count = 0;
  const bool have_points = GetPoint3ArrayLen(ps, "points", &point_count);
  if (have_indices && have_points) {
    for (size_t i = 0; i < indices.size(); i++) {
      if (indices[i] < 0 || static_cast<size_t>(indices[i]) >= point_count) {
        AddError(result, "geom.mesh.topology.index",
                 MakePropertyLocation(prim_location, "faceVertexIndices"),
                 "faceVertexIndices[" + std::to_string(i) + "] = " +
                     std::to_string(indices[i]) +
                     " is outside points range [0, " +
                     std::to_string(point_count) + ")");
      }
    }
  }
}

void ValidateGeomSubsetTopology(const PrimSpec &ps,
                                const std::string &prim_location,
                                const AncestorContext &ancestors,
                                USDValidationResult *result) {
  if (ancestors.parent_type != "Mesh" ||
      !ancestors.parent_mesh_face_count_known) {
    return;
  }

  std::string element_type = "face";
  (void)GetTokenProperty(ps, "elementType", &element_type);
  if (element_type != "face") {
    return;
  }

  std::vector<int32_t> indices;
  if (!GetIntArrayProperty(ps, "indices", &indices)) {
    return;
  }

  std::set<int32_t> seen;
  for (size_t i = 0; i < indices.size(); i++) {
    const int32_t index = indices[i];
    if (index < 0 ||
        static_cast<size_t>(index) >= ancestors.parent_mesh_face_count) {
      AddError(result, "geom.subset.indices",
               MakePropertyLocation(prim_location, "indices"),
               "indices[" + std::to_string(i) + "] = " +
                   std::to_string(index) +
                   " is outside parent face range [0, " +
                   std::to_string(ancestors.parent_mesh_face_count) + ")");
      continue;
    }
    if (!seen.insert(index).second) {
      AddWarning(result, "geom.subset.indices",
                 MakePropertyLocation(prim_location, "indices"),
                 "indices contains duplicate element " +
                     std::to_string(index));
    }
  }
}

void ValidatePrimvars(const PrimSpec &ps, const std::string &prim_location,
                      USDValidationResult *result) {
  for (const PropSlot &slot : ps.properties().slots()) {
    if (slot.is_relationship()) {
      continue;
    }
    const std::string &prop_name = GetPropNameTable().get(slot.name_id);
    if (!IsPrimvarPropertyName(prop_name)) {
      continue;
    }
    const std::string prop_location =
        MakePropertyLocation(prim_location, prop_name);

    const PropMeta *meta = ps.property_meta(prop_name);
    if (meta && (meta->authored & PropMeta::kInterpolation) &&
        !IsValidPrimvarInterpolation(meta->interpolation)) {
      AddError(result, "geom.primvar.metadata", prop_location,
               "invalid primvar interpolation `" + meta->interpolation + "`");
    }
    if (meta && (meta->authored & PropMeta::kElementSize) &&
        meta->elementSize <= 0) {
      AddError(result, "geom.primvar.metadata", prop_location,
               "primvar elementSize must be greater than 0");
    }

    const std::string indices_name = prop_name + ":indices";
    if (!HasProperty(ps, indices_name)) {
      continue;
    }
    const std::string indices_location =
        MakePropertyLocation(prim_location, indices_name);
    if (IsRelationshipProp(ps, indices_name)) {
      AddError(result, "geom.primvar.indices", indices_location,
               "indexed primvar indices must be an int[] attribute");
      continue;
    }

    std::vector<int32_t> indices;
    if (!GetIntArrayProperty(ps, indices_name, &indices)) {
      AddError(result, "geom.primvar.indices", indices_location,
               "indexed primvar indices must have int[] values");
      continue;
    }

    size_t value_count = 0;
    const bool have_value_count =
        GetArrayLengthProperty(ps, prop_name, &value_count);
    for (size_t i = 0; i < indices.size(); i++) {
      if (indices[i] < 0 ||
          (have_value_count &&
           static_cast<size_t>(indices[i]) >= value_count)) {
        std::string msg = "indices[" + std::to_string(i) + "] = " +
                          std::to_string(indices[i]);
        if (have_value_count) {
          msg += " is outside primvar value range [0, " +
                 std::to_string(value_count) + ")";
        } else {
          msg += " must be non-negative";
        }
        AddError(result, "geom.primvar.indices", indices_location, msg);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// skel rules
// ---------------------------------------------------------------------------

void ValidateSkeletonTopology(const PrimSpec &ps,
                              const std::string &prim_location,
                              USDValidationResult *result,
                              size_t *out_joint_count) {
  std::vector<std::string> joints;
  if (!GetTokenArrayProperty(ps, "joints", &joints)) {
    AddWarning(result, "geom.skel.skeleton.joints",
               MakePropertyLocation(prim_location, "joints"),
               "Skeleton should author uniform token[] joints");
    return;
  }

  if (out_joint_count) {
    *out_joint_count = joints.size();
  }

  if (joints.empty()) {
    AddError(result, "geom.skel.skeleton.joints",
             MakePropertyLocation(prim_location, "joints"),
             "Skeleton joints must not be empty");
    return;
  }

  std::unordered_map<std::string, size_t> joint_to_index;
  size_t root_count = 0;
  for (size_t i = 0; i < joints.size(); i++) {
    const std::string &joint = joints[i];
    if (joint.empty() || joint[0] == '/' || EndsWith(joint, "/") ||
        joint.find("//") != std::string::npos) {
      AddError(result, "geom.skel.skeleton.joints",
               MakePropertyLocation(prim_location, "joints"),
               "joints[" + std::to_string(i) +
                   "] must be a relative joint path, but is `" + joint + "`");
      continue;
    }
    if (!joint_to_index.emplace(joint, i).second) {
      AddError(result, "geom.skel.skeleton.joints",
               MakePropertyLocation(prim_location, "joints"),
               "duplicate joint path `" + joint + "`");
      continue;
    }

    const size_t slash = joint.find_last_of('/');
    if (slash == std::string::npos) {
      root_count++;
      continue;
    }

    const std::string parent = joint.substr(0, slash);
    auto parent_it = joint_to_index.find(parent);
    if (parent_it == joint_to_index.end() || parent_it->second >= i) {
      AddError(result, "geom.skel.skeleton.topology",
               MakePropertyLocation(prim_location, "joints"),
               "joint `" + joint +
                   "` must appear after its parent joint `" + parent + "`");
    }
  }

  if (root_count != 1) {
    AddWarning(result, "geom.skel.skeleton.topology",
               MakePropertyLocation(prim_location, "joints"),
               "Skeleton should have one root joint, but has " +
                   std::to_string(root_count));
  }

  if (HasAttributeProp(ps, "jointNames")) {
    size_t count = 0;
    if (GetArrayLengthProperty(ps, "jointNames", &count) &&
        count != joints.size()) {
      AddError(result, "geom.skel.skeleton.jointNames",
               MakePropertyLocation(prim_location, "jointNames"),
               "jointNames has " + std::to_string(count) +
                   " elements, but joints has " +
                   std::to_string(joints.size()) + " elements");
    }
  }

  for (const char *prop_name : {"bindTransforms", "restTransforms"}) {
    if (!HasAttributeProp(ps, prop_name)) {
      continue;
    }
    size_t count = 0;
    if (!GetArrayLengthProperty(ps, prop_name, &count)) {
      AddWarning(result, "geom.skel.skeleton.transforms",
                 MakePropertyLocation(prim_location, prop_name),
                 std::string(prop_name) +
                     " should be an authored matrix4d[] value");
      continue;
    }
    if (count != joints.size()) {
      AddError(result, "geom.skel.skeleton.transforms",
               MakePropertyLocation(prim_location, prop_name),
               std::string(prop_name) + " has " + std::to_string(count) +
                   " elements, but joints has " +
                   std::to_string(joints.size()) + " elements");
    }
    ValidateTimeSamplesProp(ps, prop_name, "geom.skel.skeleton.transforms",
                            MakePropertyLocation(prim_location, prop_name),
                            result);
  }
}

void ValidateSkelAnimation(const PrimSpec &ps,
                           const std::string &prim_location,
                           USDValidationResult *result) {
  std::vector<std::string> joints;
  const bool have_joints = GetTokenArrayProperty(ps, "joints", &joints);
  if (!have_joints) {
    AddWarning(result, "geom.skel.animation.joints",
               MakePropertyLocation(prim_location, "joints"),
               "SkelAnimation should author uniform token[] joints");
  }

  if (have_joints) {
    for (const char *prop_name : {"translations", "rotations", "scales"}) {
      if (!HasAttributeProp(ps, prop_name)) {
        continue;
      }
      ValidateArrayLengthAtAllSamples(
          ps, prop_name, "geom.skel.animation.transforms",
          MakePropertyLocation(prim_location, prop_name), joints.size(),
          "joints", result);
    }
  }

  std::vector<std::string> blend_shapes;
  if (GetTokenArrayProperty(ps, "blendShapes", &blend_shapes)) {
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < blend_shapes.size(); i++) {
      const std::string &name = blend_shapes[i];
      if (name.empty()) {
        AddError(result, "geom.skel.animation.blendShapes",
                 MakePropertyLocation(prim_location, "blendShapes"),
                 "blendShapes[" + std::to_string(i) + "] must not be empty");
      } else if (!seen.emplace(name).second) {
        AddError(result, "geom.skel.animation.blendShapes",
                 MakePropertyLocation(prim_location, "blendShapes"),
                 "duplicate blendShape token `" + name + "`");
      }
    }

    if (HasAttributeProp(ps, "blendShapeWeights")) {
      ValidateArrayLengthAtAllSamples(
          ps, "blendShapeWeights", "geom.skel.animation.blendShapes",
          MakePropertyLocation(prim_location, "blendShapeWeights"),
          blend_shapes.size(), "blendShapes", result);
    }
  } else if (HasAttributeProp(ps, "blendShapeWeights")) {
    AddWarning(result, "geom.skel.animation.blendShapes",
               MakePropertyLocation(prim_location, "blendShapeWeights"),
               "blendShapeWeights is authored without uniform token[] "
               "blendShapes");
  }
}

void ValidateBlendShape(const PrimSpec &ps, const std::string &prim_location,
                        USDValidationResult *result) {
  size_t offset_count = 0;
  bool have_offsets = false;
  if (!HasAttributeProp(ps, "offsets")) {
    AddError(result, "geom.skel.blendShape.offsets",
             MakePropertyLocation(prim_location, "offsets"),
             "BlendShape must author uniform vector3f[] offsets");
  } else if (!GetArrayLengthProperty(ps, "offsets", &offset_count)) {
    AddError(result, "geom.skel.blendShape.offsets",
             MakePropertyLocation(prim_location, "offsets"),
             "BlendShape offsets must be an authored array value");
  } else {
    have_offsets = true;
    if (offset_count == 0) {
      AddWarning(result, "geom.skel.blendShape.offsets",
                 MakePropertyLocation(prim_location, "offsets"),
                 "BlendShape offsets should not be empty");
    }
  }

  if (HasAttributeProp(ps, "normalOffsets")) {
    ValidateTimeSamplesProp(
        ps, "normalOffsets", "geom.skel.blendShape.normalOffsets",
        MakePropertyLocation(prim_location, "normalOffsets"), result);
    if (have_offsets) {
      ValidateArrayLengthAtAllSamples(
          ps, "normalOffsets", "geom.skel.blendShape.normalOffsets",
          MakePropertyLocation(prim_location, "normalOffsets"), offset_count,
          "offsets", result);
    }
  }

  if (HasAttributeProp(ps, "pointIndices")) {
    if (have_offsets) {
      ValidateArrayLengthAtAllSamples(
          ps, "pointIndices", "geom.skel.blendShape.pointIndices",
          MakePropertyLocation(prim_location, "pointIndices"), offset_count,
          "offsets", result);
    }

    std::vector<int32_t> indices;
    if (GetIntArrayProperty(ps, "pointIndices", &indices)) {
      std::unordered_set<int32_t> seen;
      for (size_t i = 0; i < indices.size(); i++) {
        if (indices[i] < 0) {
          AddError(result, "geom.skel.blendShape.pointIndices",
                   MakePropertyLocation(prim_location, "pointIndices"),
                   "pointIndices[" + std::to_string(i) +
                       "] must be non-negative");
        } else if (!seen.emplace(indices[i]).second) {
          AddWarning(result, "geom.skel.blendShape.pointIndices",
                     MakePropertyLocation(prim_location, "pointIndices"),
                     "duplicate pointIndices value " +
                         std::to_string(indices[i]));
        }
      }
    }
  }

  for (const PropSlot &slot : ps.properties().slots()) {
    if (slot.is_relationship()) {
      continue;
    }
    const std::string &prop_name = GetPropNameTable().get(slot.name_id);
    if (!StartsWith(prop_name, "inbetweens:")) {
      continue;
    }
    const std::string location =
        MakePropertyLocation(prim_location, prop_name);
    const PropMeta *meta = ps.property_meta(prop_name);
    if (!meta || !(meta->authored & PropMeta::kWeight)) {
      AddError(result, "geom.skel.blendShape.inbetween", location,
               "BlendShape inbetween `" + prop_name +
                   "` must author weight metadata");
    } else if (!std::isfinite(meta->weight)) {
      AddError(result, "geom.skel.blendShape.inbetween", location,
               "BlendShape inbetween `" + prop_name +
                   "` has a non-finite weight");
    }
    if (have_offsets) {
      ValidateArrayLengthAtAllSamples(ps, prop_name,
                                      "geom.skel.blendShape.inbetween",
                                      location, offset_count, "offsets",
                                      result);
    }
  }
}

void ValidateSkelBinding(const PrimSpec &ps, const std::string &prim_location,
                         const PrimTypeByPath &prim_types,
                         USDValidationResult *result) {
  if (HasProperty(ps, "skel:skeleton")) {
    ValidateRelationshipTargetPaths(
        ps, "skel:skeleton", "geom.skel.binding.skeleton",
        MakePropertyLocation(prim_location, "skel:skeleton"), result);
    if (IsRelationshipProp(ps, "skel:skeleton")) {
      for (const Path &target : RelTargets(ps, "skel:skeleton")) {
        std::string prim_part;
        SplitScenePathString(target.str(), &prim_part, nullptr);
        const std::string *target_type = FindPrimType(prim_types, prim_part);
        if (target_type && *target_type != "Skeleton") {
          AddWarning(result, "geom.skel.binding.skeleton",
                     MakePropertyLocation(prim_location, "skel:skeleton"),
                     "skel:skeleton target `" + target.str() +
                         "` has type `" + *target_type +
                         "`, expected Skeleton");
        }
      }
    }
  }

  if (HasProperty(ps, "skel:animationSource")) {
    ValidateRelationshipTargetPaths(
        ps, "skel:animationSource", "geom.skel.binding.animationSource",
        MakePropertyLocation(prim_location, "skel:animationSource"), result);
    if (IsRelationshipProp(ps, "skel:animationSource")) {
      for (const Path &target : RelTargets(ps, "skel:animationSource")) {
        std::string prim_part;
        SplitScenePathString(target.str(), &prim_part, nullptr);
        const std::string *target_type = FindPrimType(prim_types, prim_part);
        if (target_type && *target_type != "SkelAnimation") {
          AddWarning(
              result, "geom.skel.binding.animationSource",
              MakePropertyLocation(prim_location, "skel:animationSource"),
              "skel:animationSource target `" + target.str() +
                  "` has type `" + *target_type + "`, expected SkelAnimation");
        }
      }
    }
  }
}

void ValidateSkinningPrimvars(const PrimSpec &ps,
                              const std::string &prim_location,
                              const PrimTypeByPath &prim_types,
                              const SkeletonJointCountByPath &skeleton_joints,
                              USDValidationResult *result) {
  const bool have_indices_attr =
      HasAttributeProp(ps, "primvars:skel:jointIndices");
  const bool have_weights_attr =
      HasAttributeProp(ps, "primvars:skel:jointWeights");
  if (!have_indices_attr && !have_weights_attr) {
    return;
  }

  if (!have_indices_attr || !have_weights_attr) {
    AddError(result, "geom.skel.skinning.pair", prim_location,
             "primvars:skel:jointIndices and primvars:skel:jointWeights must "
             "be authored together");
    return;
  }

  const PropMeta *indices_meta = ps.property_meta("primvars:skel:jointIndices");
  const PropMeta *weights_meta = ps.property_meta("primvars:skel:jointWeights");

  const auto validate_skin_interp = [&](const PropMeta *meta,
                                        const std::string &name) {
    const std::string location = MakePropertyLocation(prim_location, name);
    if (!meta || !(meta->authored & PropMeta::kInterpolation)) {
      AddError(result, "geom.skel.skinning.interpolation", location,
               name + " must author interpolation metadata");
      return;
    }
    if (meta->interpolation != "vertex" && meta->interpolation != "varying") {
      AddError(result, "geom.skel.skinning.interpolation", location,
               name + " interpolation must be vertex or varying, but is `" +
                   meta->interpolation + "`");
    }
  };
  validate_skin_interp(indices_meta, "primvars:skel:jointIndices");
  validate_skin_interp(weights_meta, "primvars:skel:jointWeights");

  const bool indices_es_ok = indices_meta &&
                             (indices_meta->authored & PropMeta::kElementSize) &&
                             indices_meta->elementSize > 0;
  const bool weights_es_ok = weights_meta &&
                             (weights_meta->authored & PropMeta::kElementSize) &&
                             weights_meta->elementSize > 0;
  if (!indices_es_ok || !weights_es_ok) {
    AddError(result, "geom.skel.skinning.elementSize", prim_location,
             "skinning primvars must author non-zero elementSize metadata");
  } else if (indices_meta->elementSize != weights_meta->elementSize) {
    AddError(result, "geom.skel.skinning.elementSize", prim_location,
             "primvars:skel:jointIndices and primvars:skel:jointWeights must "
             "use the same elementSize");
  }

  std::vector<int32_t> joint_indices;
  std::vector<float> joint_weights;
  if (GetIntArrayProperty(ps, "primvars:skel:jointIndices", &joint_indices) &&
      GetFloatArrayProperty(ps, "primvars:skel:jointWeights",
                            &joint_weights)) {
    if (joint_indices.size() != joint_weights.size()) {
      AddError(result, "geom.skel.skinning.size", prim_location,
               "jointIndices has " + std::to_string(joint_indices.size()) +
                   " elements, but jointWeights has " +
                   std::to_string(joint_weights.size()) + " elements");
    }

    size_t local_joint_count = 0;
    bool have_local_joint_count = false;
    if (IsRelationshipProp(ps, "skel:skeleton")) {
      const std::vector<Path> &targets = RelTargets(ps, "skel:skeleton");
      if (targets.size() == 1) {
        std::string prim_part;
        SplitScenePathString(targets[0].str(), &prim_part, nullptr);
        const std::string *target_type = FindPrimType(prim_types, prim_part);
        if (target_type && *target_type == "Skeleton") {
          auto joint_count_it = skeleton_joints.find(prim_part);
          if (joint_count_it != skeleton_joints.end()) {
            local_joint_count = joint_count_it->second;
            have_local_joint_count = true;
          }
        }
      }
    }

    for (size_t i = 0; i < joint_indices.size(); i++) {
      if (joint_indices[i] < 0) {
        AddError(result, "geom.skel.skinning.index",
                 MakePropertyLocation(prim_location,
                                      "primvars:skel:jointIndices"),
                 "jointIndices[" + std::to_string(i) +
                     "] must be non-negative");
      } else if (have_local_joint_count &&
                 static_cast<size_t>(joint_indices[i]) >= local_joint_count) {
        AddError(result, "geom.skel.skinning.index",
                 MakePropertyLocation(prim_location,
                                      "primvars:skel:jointIndices"),
                 "jointIndices[" + std::to_string(i) + "] references joint " +
                     std::to_string(joint_indices[i]) + ", but skeleton has " +
                     std::to_string(local_joint_count) + " joints");
      }
    }

    if (weights_meta && (weights_meta->authored & PropMeta::kElementSize)) {
      const size_t element_size =
          weights_meta->elementSize > 0
              ? static_cast<size_t>(weights_meta->elementSize)
              : 0;
      if (element_size != 0 && (joint_weights.size() % element_size) == 0) {
        for (size_t group = 0; group < joint_weights.size() / element_size;
             group++) {
          double sum = 0.0;
          bool all_finite = true;
          for (size_t item = 0; item < element_size; item++) {
            const float weight = joint_weights[group * element_size + item];
            if (!std::isfinite(weight)) {
              all_finite = false;
            }
            sum += static_cast<double>(weight);
          }
          if (!all_finite) {
            AddError(result, "geom.skel.skinning.weight",
                     MakePropertyLocation(prim_location,
                                          "primvars:skel:jointWeights"),
                     "jointWeights group " + std::to_string(group) +
                         " contains a non-finite value");
          } else if (sum <= 0.0) {
            AddWarning(result, "geom.skel.skinning.weight",
                       MakePropertyLocation(prim_location,
                                            "primvars:skel:jointWeights"),
                       "jointWeights group " + std::to_string(group) +
                           " has no positive influence");
          } else if (std::fabs(sum - 1.0) > 1.0e-3) {
            AddWarning(result, "geom.skel.skinning.weight",
                       MakePropertyLocation(prim_location,
                                            "primvars:skel:jointWeights"),
                       "jointWeights group " + std::to_string(group) +
                           " sums to " + std::to_string(sum) +
                           " instead of 1");
          }
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// shade rules
// ---------------------------------------------------------------------------

std::string GetShaderInfoId(const PrimSpec &ps) {
  std::string id;
  (void)GetTokenProperty(ps, "info:id", &id);
  return id;
}

void ValidateUsdPreviewSurface(const PrimSpec &ps,
                               const std::string &prim_location,
                               USDValidationResult *result) {
  static const std::string kInputsPrefix = "inputs:";
  const auto &inputs = UsdPreviewSurfaceInputTypes();

  for (const PropSlot &slot : ps.properties().slots()) {
    if (slot.is_relationship()) {
      continue;
    }
    const std::string &prop_name = GetPropNameTable().get(slot.name_id);
    if (!StartsWith(prop_name, kInputsPrefix)) {
      continue;
    }
    const std::string input_name = prop_name.substr(kInputsPrefix.size());
    const std::string prop_location =
        MakePropertyLocation(prim_location, prop_name);

    auto it = inputs.find(input_name);
    if (it == inputs.end()) {
      AddWarning(result, "shade.preview.unknownInput", prop_location,
                 "`" + prop_name + "` is not a UsdPreviewSurface input");
      continue;
    }

    const std::string authored = AttrTypeNameOf(ps, prop_name);
    if (!authored.empty() && !ValueTypesAgree(authored, it->second)) {
      AddWarning(result, "shade.preview.inputType", prop_location,
                 "UsdPreviewSurface input `" + input_name + "` should be `" +
                     it->second + "` but is `" + authored + "`");
    }
  }

  for (const char *prop_name :
       {"inputs:roughness", "inputs:metallic", "inputs:clearcoat",
        "inputs:clearcoatRoughness", "inputs:opacity", "inputs:occlusion"}) {
    double value = 0.0;
    if (GetNumericScalarProperty(ps, prop_name, &value) &&
        (value < 0.0 || value > 1.0)) {
      AddWarning(result, "shade.preview.inputRange",
                 MakePropertyLocation(prim_location, prop_name),
                 std::string(prop_name) +
                     " should be in range [0, 1], but is " +
                     std::to_string(value));
    }
  }

  double ior = 0.0;
  if (GetNumericScalarProperty(ps, "inputs:ior", &ior) && !(ior > 0.0)) {
    AddWarning(result, "shade.preview.inputRange",
               MakePropertyLocation(prim_location, "inputs:ior"),
               "inputs:ior should be greater than 0, but is " +
                   std::to_string(ior));
  }

  std::string opacity_mode;
  if (GetTokenProperty(ps, "inputs:opacityMode", &opacity_mode) &&
      opacity_mode != "opacity" && opacity_mode != "transparent" &&
      opacity_mode != "presence") {
    AddWarning(result, "shade.preview.opacityMode",
               MakePropertyLocation(prim_location, "inputs:opacityMode"),
               "invalid UsdPreviewSurface opacityMode `" + opacity_mode + "`");
  }
}

bool IsUsdUVTextureWrapToken(const std::string &token) {
  return token == "useMetadata" || token == "black" || token == "clamp" ||
         token == "repeat" || token == "mirror";
}

bool IsUsdUVTextureColorSpaceToken(const std::string &token) {
  return token == "auto" || token == "raw" || token == "sRGB";
}

void ValidateUsdUVTexture(const PrimSpec &ps, const std::string &prim_location,
                          USDValidationResult *result) {
  std::string file;
  if (!HasAttributeProp(ps, "inputs:file")) {
    AddWarning(result, "shade.uvTexture.file",
               MakePropertyLocation(prim_location, "inputs:file"),
               "UsdUVTexture should author inputs:file");
  } else if (!HasConnections(ps, "inputs:file") &&
             (!GetAssetPathProperty(ps, "inputs:file", &file) ||
              file.empty())) {
    AddWarning(result, "shade.uvTexture.file",
               MakePropertyLocation(prim_location, "inputs:file"),
               "UsdUVTexture inputs:file should be a non-empty asset path");
  }

  for (const char *prop_name : {"inputs:wrapS", "inputs:wrapT"}) {
    std::string token;
    if (GetTokenProperty(ps, prop_name, &token) &&
        !IsUsdUVTextureWrapToken(token)) {
      AddWarning(result, "shade.uvTexture.wrap",
                 MakePropertyLocation(prim_location, prop_name),
                 std::string(prop_name) + " has invalid wrap token `" + token +
                     "`");
    }
  }

  std::string color_space;
  if (GetTokenProperty(ps, "inputs:sourceColorSpace", &color_space) &&
      !IsUsdUVTextureColorSpaceToken(color_space)) {
    AddWarning(result, "shade.uvTexture.sourceColorSpace",
               MakePropertyLocation(prim_location, "inputs:sourceColorSpace"),
               "UsdUVTexture sourceColorSpace should be auto, raw, or sRGB, "
               "but is `" +
                   color_space + "`");
  }
}

void ValidateUsdPrimvarReader(const PrimSpec &ps, const std::string &shader_id,
                              const std::string &prim_location,
                              USDValidationResult *result) {
  if (!HasAttributeProp(ps, "inputs:varname")) {
    AddWarning(result, "shade.primvarReader.varname",
               MakePropertyLocation(prim_location, "inputs:varname"),
               "UsdPrimvarReader should author inputs:varname");
    return;
  }

  const std::string varname_type = AttrTypeNameOf(ps, "inputs:varname");
  if (HasConnections(ps, "inputs:varname")) {
    // Connected varname inputs resolve at composition/evaluation time. The
    // output terminal type is still local schema data and must be checked.
  } else if (varname_type != "token" && varname_type != "string") {
    AddWarning(result, "shade.primvarReader.varname",
               MakePropertyLocation(prim_location, "inputs:varname"),
               "UsdPrimvarReader inputs:varname should be token or string, "
               "but is `" +
                   varname_type + "`");
  } else {
    std::string varname;
    if (!GetStringLikeProperty(ps, "inputs:varname", &varname) ||
        varname.empty()) {
      AddWarning(result, "shade.primvarReader.varname",
                 MakePropertyLocation(prim_location, "inputs:varname"),
                 "UsdPrimvarReader inputs:varname should be non-empty");
    }
  }

  const std::string *expected_type = UsdPrimvarReaderResultType(shader_id);
  if (!expected_type) {
    AddWarning(result, "shade.primvarReader.result", prim_location,
               "unknown UsdPrimvarReader shader id `" + shader_id + "`");
    return;
  }

  const std::string location =
      MakePropertyLocation(prim_location, "outputs:result");
  if (!HasProperty(ps, "outputs:result")) {
    AddWarning(result, "shade.primvarReader.result", location,
               shader_id + " should author outputs:result");
    return;
  }
  if (IsRelationshipProp(ps, "outputs:result")) {
    AddWarning(result, "shade.primvarReader.result", location,
               shader_id + " outputs:result should be an attribute");
    return;
  }

  const std::string result_type = AttrTypeNameOf(ps, "outputs:result");
  if (!ValueTypesAgree(result_type, *expected_type)) {
    AddWarning(result, "shade.primvarReader.result", location,
               shader_id + " outputs:result should be " + *expected_type +
                   ", but is `" + result_type + "`");
  }
}

bool HasAppliedSchema(const std::vector<AppliedSchema> &schemas,
                      const std::string &schema_name) {
  for (const auto &schema : schemas) {
    if (schema.name == schema_name) {
      return true;
    }
  }
  return false;
}

void ValidateMaterialXConfig(const PrimSpec &ps,
                             const std::vector<AppliedSchema> &applied_schemas,
                             const std::string &prim_location,
                             USDValidationResult *result) {
  bool has_config_property = false;
  for (const PropSlot &slot : ps.properties().slots()) {
    const std::string &prop_name = GetPropNameTable().get(slot.name_id);
    if (StartsWith(prop_name, "config:mtlx:")) {
      has_config_property = true;
      break;
    }
  }

  const bool has_config_api =
      HasAppliedSchema(applied_schemas, "MaterialXConfigAPI");
  if (has_config_property && !has_config_api) {
    AddWarning(result, "shade.materialX.configAPI", prim_location,
               "config:mtlx:* properties are authored without applying "
               "MaterialXConfigAPI");
  }

  if (has_config_api && ps.type_name() != "Material") {
    AddWarning(result, "shade.materialX.configAPI", prim_location,
               "MaterialXConfigAPI is expected on Material prims");
  }

  std::string version;
  if (GetStringProperty(ps, "config:mtlx:version", &version) &&
      (version.empty() || !StartsWith(version, "1."))) {
    AddWarning(result, "shade.materialX.version",
               MakePropertyLocation(prim_location, "config:mtlx:version"),
               "MaterialX version should be a 1.x version string, but is `" +
                   version + "`");
  }

  std::string source_uri;
  if (GetStringProperty(ps, "config:mtlx:sourceUri", &source_uri) &&
      !source_uri.empty() && !IsMaterialXAssetPath(source_uri)) {
    AddWarning(result, "shade.materialX.sourceUri",
               MakePropertyLocation(prim_location, "config:mtlx:sourceUri"),
               "MaterialX sourceUri should usually reference a .mtlx asset");
  }
}

void ValidateMaterialXShader(const PrimSpec &ps, const std::string &shader_id,
                             const std::string &prim_location,
                             USDValidationResult *result) {
  if (!IsMaterialXShaderId(shader_id)) {
    return;
  }

  if (!HasProperty(ps, "outputs:surface") && !HasProperty(ps, "outputs:out")) {
    AddWarning(result, "shade.materialX.output", prim_location,
               "MaterialX shader `" + shader_id +
                   "` should author outputs:surface or outputs:out");
  }
}

// ---------------------------------------------------------------------------
// lux rules
// ---------------------------------------------------------------------------

void ValidateLuxRelationships(const PrimSpec &ps,
                              const std::string &prim_location,
                              USDValidationResult *result) {
  for (const char *name :
       {"light:filters", "portals", "proxyPrim", "geometry"}) {
    if (!HasProperty(ps, name)) {
      continue;
    }
    if (!IsRelationshipProp(ps, name)) {
      AddError(result, "lux.relationship.target",
               MakePropertyLocation(prim_location, name),
               std::string(name) + " must be a relationship");
      continue;
    }
    ValidateRelationshipTargetPaths(ps, name, "lux.relationship.target",
                                    MakePropertyLocation(prim_location, name),
                                    result);
  }
}

void ValidateLuxLight(const PrimSpec &ps, const std::string &prim_location,
                      USDValidationResult *result) {
  static const std::set<std::string> kTextureFormats = {
      "automatic", "latlong", "mirroredBall", "angular"};
  static const std::set<std::string> kPoleAxis = {"scene", "Y", "Z"};
  const std::string &type_name = ps.type_name();

  ValidateNonNegativeNumericProperty(ps, "inputs:intensity",
                                     "lux.light.inputs", prim_location,
                                     result);
  ValidateFiniteNumericProperty(ps, "inputs:exposure", "lux.light.inputs",
                                prim_location, result);
  ValidateNonNegativeNumericProperty(ps, "inputs:diffuse", "lux.light.inputs",
                                     prim_location, result);
  ValidateNonNegativeNumericProperty(ps, "inputs:specular",
                                     "lux.light.inputs", prim_location,
                                     result);
  ValidatePositiveNumericProperty(ps, "inputs:colorTemperature",
                                  "lux.light.inputs", prim_location, result);
  ValidateNumericRangeProperty(ps, "inputs:shaping:cone:angle", 0.0, 180.0,
                               "lux.shaping.inputs", prim_location, result);
  ValidateNumericRangeProperty(ps, "inputs:shaping:cone:softness", 0.0, 1.0,
                               "lux.shaping.inputs", prim_location, result);
  ValidateNonNegativeNumericProperty(ps, "inputs:shaping:focus",
                                     "lux.shaping.inputs", prim_location,
                                     result);
  ValidateNonNegativeNumericProperty(ps, "inputs:shaping:ies:angleScale",
                                     "lux.shaping.inputs", prim_location,
                                     result);
  ValidateNumericMinProperty(ps, "inputs:shadow:distance", -1.0,
                             "lux.shadow.inputs", prim_location, result);
  ValidateNumericMinProperty(ps, "inputs:shadow:falloff", -1.0,
                             "lux.shadow.inputs", prim_location, result);
  ValidateNumericMinProperty(ps, "inputs:shadow:falloffGamma", 0.0,
                             "lux.shadow.inputs", prim_location, result);

  if (type_name == "SphereLight" || type_name == "DiskLight") {
    ValidatePositiveNumericProperty(ps, "inputs:radius", "lux.light.size",
                                    prim_location, result);
  } else if (type_name == "CylinderLight") {
    ValidatePositiveNumericProperty(ps, "inputs:radius", "lux.light.size",
                                    prim_location, result);
    ValidatePositiveNumericProperty(ps, "inputs:length", "lux.light.size",
                                    prim_location, result);
  } else if (type_name == "RectLight") {
    ValidatePositiveNumericProperty(ps, "inputs:width", "lux.light.size",
                                    prim_location, result);
    ValidatePositiveNumericProperty(ps, "inputs:height", "lux.light.size",
                                    prim_location, result);
    ValidateAssetPathProperty(ps, "inputs:texture:file", "lux.texture.file",
                              prim_location, result);
  } else if (type_name == "DistantLight") {
    ValidateNumericRangeProperty(ps, "inputs:angle", 0.0, 180.0,
                                 "lux.light.angle", prim_location, result);
  } else if (type_name == "DomeLight" || type_name == "DomeLight_1") {
    ValidatePositiveNumericProperty(ps, "guideRadius", "lux.light.size",
                                    prim_location, result);
    ValidateAssetPathProperty(ps, "inputs:texture:file", "lux.texture.file",
                              prim_location, result);
    ValidateTokenSetProperty(ps, "inputs:texture:format", kTextureFormats,
                             "lux.texture.format", prim_location, result);
    if (type_name == "DomeLight_1") {
      ValidateTokenSetProperty(ps, "poleAxis", kPoleAxis, "lux.dome.poleAxis",
                               prim_location, result);
    }
  }

  ValidateLuxRelationships(ps, prim_location, result);
}

// ---------------------------------------------------------------------------
// physics rules
// ---------------------------------------------------------------------------

bool IsPhysicsDofName(const std::string &dof) {
  static const std::set<std::string> kDofs = {
      "transX", "transY", "transZ", "rotX", "rotY", "rotZ", "distance"};
  return kDofs.count(dof) > 0;
}

bool HasAppliedPhysicsSchema(const std::vector<AppliedSchema> &schemas) {
  for (const AppliedSchema &schema : schemas) {
    if (IsPhysicsSchemaName(schema.name)) {
      return true;
    }
  }
  return false;
}

// Every property name of the prim (attribute slots + relationships).
std::vector<std::string> AllPropertyNames(const PrimSpec &ps) {
  std::vector<std::string> names;
  names.reserve(ps.properties().size());
  for (const PropSlot &slot : ps.properties().slots()) {
    names.push_back(GetPropNameTable().get(slot.name_id));
  }
  for (const std::string &rel : ps.relationship_names()) {
    if (!ps.property(rel)) {
      names.push_back(rel);
    }
  }
  return names;
}

bool HasSchemaOrPropertyPrefix(const PrimSpec &ps,
                               const std::vector<AppliedSchema> &schemas,
                               const std::string &schema_prefix,
                               const std::string &prop_prefix) {
  for (const AppliedSchema &schema : schemas) {
    if (StartsWith(schema.name, schema_prefix)) {
      return true;
    }
  }
  for (const std::string &prop_name : AllPropertyNames(ps)) {
    if (StartsWith(prop_name, prop_prefix)) {
      return true;
    }
  }
  return false;
}

void ValidatePhysicsRelationship(const PrimSpec &ps,
                                 const std::string &prop_name,
                                 const std::string &rule_id,
                                 const std::string &prim_location,
                                 const PrimTypeByPath &prim_types,
                                 const std::set<std::string> &target_types,
                                 USDValidationResult *result) {
  if (!HasProperty(ps, prop_name)) {
    return;
  }
  if (!IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, MakePropertyLocation(prim_location, prop_name),
             prop_name + " must be a relationship");
    return;
  }
  ValidateRelationshipTargetPaths(
      ps, prop_name, rule_id, MakePropertyLocation(prim_location, prop_name),
      result);
  if (!target_types.empty()) {
    ValidateRelationshipTargetPrimTypes(
        ps, prop_name, rule_id,
        MakePropertyLocation(prim_location, prop_name), prim_types,
        target_types, result);
  }
}

void ValidatePhysicsScene(const PrimSpec &ps,
                          const std::string &prim_location,
                          USDValidationResult *result) {
  ValidateNonNegativeNumericProperty(ps, "physics:gravityMagnitude",
                                     "physics.scene.gravity", prim_location,
                                     result);

  if (!HasProperty(ps, "physics:gravityDirection")) {
    return;
  }
  std::array<double, 3> v;
  const Value *value = GetAttrValue(ps, "physics:gravityDirection");
  if (IsRelationshipProp(ps, "physics:gravityDirection") || !value ||
      !ValueToVec3(*value, &v)) {
    AddError(result, "physics.scene.gravity",
             MakePropertyLocation(prim_location, "physics:gravityDirection"),
             "physics:gravityDirection must be a vector3f attribute");
    return;
  }
  const double len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]) ||
      len2 == 0.0) {
    AddError(result, "physics.scene.gravity",
             MakePropertyLocation(prim_location, "physics:gravityDirection"),
             "physics:gravityDirection must be finite and non-zero");
  }
}

void ValidatePhysicsInertiaAndMotion(const PrimSpec &ps,
                                     const std::string &prim_location,
                                     USDValidationResult *result) {
  ValidateFiniteVec3Property(ps, "physics:centerOfMass", "physics.inertia",
                             prim_location, result, false);
  ValidateFiniteNonNegativeVec3Property(ps, "physics:diagonalInertia",
                                        "physics.inertia", prim_location,
                                        result);
  ValidateQuaternionProperty(ps, "physics:principalAxes",
                             "physics.quaternion", prim_location, result);
  ValidateFiniteVec3Property(ps, "physics:velocity", "physics.motion",
                             prim_location, result, false);
  ValidateFiniteVec3Property(ps, "physics:angularVelocity", "physics.motion",
                             prim_location, result, false);
}

void ValidatePhysicsJointTransforms(const PrimSpec &ps,
                                    const std::string &prim_location,
                                    USDValidationResult *result) {
  ValidateFiniteVec3Property(ps, "physics:localPos0",
                             "physics.joint.transform", prim_location, result,
                             false);
  ValidateFiniteVec3Property(ps, "physics:localPos1",
                             "physics.joint.transform", prim_location, result,
                             false);
  ValidateQuaternionProperty(ps, "physics:localRot0", "physics.quaternion",
                             prim_location, result);
  ValidateQuaternionProperty(ps, "physics:localRot1", "physics.quaternion",
                             prim_location, result);
}

void ValidatePhysicsScalarProperties(const PrimSpec &ps,
                                     const std::string &prim_location,
                                     USDValidationResult *result) {
  for (const char *prop_name :
       {"physics:mass", "physics:density", "physics:staticFriction",
        "physics:dynamicFriction", "physics:restitution",
        "physics:breakForce", "physics:breakTorque"}) {
    ValidateNonNegativeNumericProperty(ps, prop_name, "physics.value.range",
                                       prim_location, result);
  }
}

void ValidatePhysicsCollisionGroup(const PrimSpec &ps,
                                   const std::string &prim_location,
                                   const PrimTypeByPath &prim_types,
                                   USDValidationResult *result) {
  std::string merge_group;
  if (GetTokenProperty(ps, "physics:mergeGroup", &merge_group) &&
      merge_group.empty()) {
    AddError(result, "physics.collisionGroup.mergeGroup",
             MakePropertyLocation(prim_location, "physics:mergeGroup"),
             "physics:mergeGroup must not be empty when authored");
  }
  ValidatePhysicsRelationship(ps, "physics:filteredGroups",
                              "physics.collisionGroup.filteredGroups",
                              prim_location, prim_types,
                              {"PhysicsCollisionGroup"}, result);
}

void ValidatePhysicsApproximation(const PrimSpec &ps,
                                  const std::string &prim_location,
                                  USDValidationResult *result) {
  static const std::set<std::string> kApproximation = {
      "convexDecomposition", "convexHull",     "meshSimplification",
      "boundingCube",        "boundingSphere", "none"};
  ValidateTokenSetProperty(ps, "physics:approximation", kApproximation,
                           "physics.collision.approximation", prim_location,
                           result);
}

void ValidatePhysicsJointLimits(const PrimSpec &ps,
                                const std::string &prim_location,
                                USDValidationResult *result) {
  static const std::set<std::string> kAxis = {"X", "Y", "Z"};
  ValidateTokenSetProperty(ps, "physics:axis", kAxis, "physics.joint.axis",
                           prim_location, result);

  ValidateFiniteNumericProperty(ps, "physics:lowerLimit",
                                "physics.joint.limit", prim_location, result);
  ValidateFiniteNumericProperty(ps, "physics:upperLimit",
                                "physics.joint.limit", prim_location, result);
  double lower = 0.0;
  double upper = 0.0;
  if (GetNumericScalarProperty(ps, "physics:lowerLimit", &lower) &&
      GetNumericScalarProperty(ps, "physics:upperLimit", &upper) &&
      lower > upper) {
    AddError(result, "physics.joint.limit", prim_location,
             "physics:lowerLimit must be <= physics:upperLimit");
  }

  ValidateNumericRangeProperty(ps, "physics:coneAngle0Limit", 0.0, 180.0,
                               "physics.joint.limit", prim_location, result);
  ValidateNumericRangeProperty(ps, "physics:coneAngle1Limit", 0.0, 180.0,
                               "physics.joint.limit", prim_location, result);

  ValidateNonNegativeNumericProperty(ps, "physics:minDistance",
                                     "physics.joint.distance", prim_location,
                                     result);
  ValidateNonNegativeNumericProperty(ps, "physics:maxDistance",
                                     "physics.joint.distance", prim_location,
                                     result);
  double min_distance = 0.0;
  double max_distance = 0.0;
  if (GetNumericScalarProperty(ps, "physics:minDistance", &min_distance) &&
      GetNumericScalarProperty(ps, "physics:maxDistance", &max_distance) &&
      min_distance > max_distance) {
    AddError(result, "physics.joint.distance", prim_location,
             "physics:minDistance must be <= physics:maxDistance");
  }
}

void ValidatePhysicsSchemaPlacement(
    const PrimSpec &ps, const std::vector<AppliedSchema> &schemas,
    const std::string &prim_location, USDValidationResult *result) {
  const std::string &type_name = ps.type_name();
  for (const AppliedSchema &schema : schemas) {
    if ((schema.name == "PhysicsDriveAPI" ||
         schema.name == "PhysicsLimitAPI" || schema.name == "MjcJointAPI" ||
         schema.name == "NewtonMimicAPI") &&
        type_name.find("Joint") == std::string::npos) {
      AddWarning(result, "physics.schemaPlacement", prim_location,
                 schema.name + " is expected on physics joint prims");
    } else if ((schema.name == "PhysicsMeshCollisionAPI" ||
                schema.name == "MjcMeshCollisionAPI" ||
                schema.name == "NewtonMeshCollisionAPI") &&
               type_name != "Mesh") {
      AddWarning(result, "physics.schemaPlacement", prim_location,
                 schema.name + " is expected on Mesh prims");
    } else if ((schema.name == "MjcSceneAPI" ||
                schema.name == "NewtonSceneAPI" ||
                schema.name == "NewtonXpbdSceneAPI" ||
                schema.name == "NewtonKaminoSceneAPI") &&
               type_name != "PhysicsScene") {
      AddWarning(result, "physics.schemaPlacement", prim_location,
                 schema.name + " is expected on PhysicsScene prims");
    }
  }
}

void ValidatePhysicsDriveAndLimitAPIs(
    const PrimSpec &ps, const std::vector<AppliedSchema> &schemas,
    const std::string &prim_location, USDValidationResult *result) {
  static const std::set<std::string> kDriveTypes = {"force", "acceleration"};

  for (const AppliedSchema &schema : schemas) {
    if ((schema.name == "PhysicsDriveAPI" ||
         schema.name == "PhysicsLimitAPI") &&
        !IsPhysicsDofName(schema.instance_name)) {
      AddError(result,
               schema.name == "PhysicsDriveAPI" ? "physics.drive.dof"
                                                : "physics.limit.dof",
               prim_location,
               schema.name + " instance `" + schema.instance_name +
                   "` is not a supported physics DOF");
    }
  }

  std::map<std::string, std::pair<double, bool>> limit_low;
  std::map<std::string, std::pair<double, bool>> limit_high;
  for (const std::string &prop_name : AllPropertyNames(ps)) {
    if (StartsWith(prop_name, "physics:drive:")) {
      const std::string rest =
          prop_name.substr(std::string("physics:drive:").size());
      const size_t colon = rest.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      const std::string dof = rest.substr(0, colon);
      const std::string field = rest.substr(colon + 1);
      const std::string location =
          MakePropertyLocation(prim_location, prop_name);
      if (!IsPhysicsDofName(dof)) {
        AddError(result, "physics.drive.dof", location,
                 "PhysicsDriveAPI DOF `" + dof + "` is not supported");
      }
      if (field == "type") {
        ValidateTokenSetProperty(ps, prop_name, kDriveTypes,
                                 "physics.drive.type", prim_location, result);
      } else if (field == "maxForce" || field == "damping" ||
                 field == "stiffness") {
        ValidateNonNegativeNumericProperty(ps, prop_name,
                                           "physics.drive.value",
                                           prim_location, result);
      } else if (field == "targetPosition" || field == "targetVelocity") {
        ValidateFiniteNumericProperty(ps, prop_name, "physics.drive.value",
                                      prim_location, result);
      }
    } else if (StartsWith(prop_name, "physics:limit:")) {
      const std::string rest =
          prop_name.substr(std::string("physics:limit:").size());
      const size_t colon = rest.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      const std::string dof = rest.substr(0, colon);
      const std::string field = rest.substr(colon + 1);
      const std::string location =
          MakePropertyLocation(prim_location, prop_name);
      if (!IsPhysicsDofName(dof)) {
        AddError(result, "physics.limit.dof", location,
                 "PhysicsLimitAPI DOF `" + dof + "` is not supported");
      }
      if (field == "low" || field == "high") {
        ValidateFiniteNumericProperty(ps, prop_name, "physics.limit.range",
                                      prim_location, result);
        double value = 0.0;
        const bool have_value =
            GetNumericScalarProperty(ps, prop_name, &value);
        if (field == "low") {
          limit_low[dof] = std::make_pair(value, have_value);
        } else {
          limit_high[dof] = std::make_pair(value, have_value);
        }
      }
    }
  }

  for (const auto &entry : limit_low) {
    const auto high_it = limit_high.find(entry.first);
    if (high_it == limit_high.end()) {
      continue;
    }
    if (entry.second.second && high_it->second.second &&
        entry.second.first > high_it->second.first) {
      AddError(result, "physics.limit.range", prim_location,
               "physics:limit:" + entry.first + " low must be <= high");
    }
  }
}

void ValidateMjcTokenEnums(const PrimSpec &ps,
                           const std::string &prim_location,
                           USDValidationResult *result) {
  static const std::set<std::string> kAutoBool = {"auto", "true", "false"};
  ValidateTokenSetProperty(ps, "mjc:option:integrator",
                           {"euler", "implicit", "implicitfast", "RK4"},
                           "physics.extension.mjc.token", prim_location,
                           result);
  ValidateTokenSetProperty(ps, "mjc:option:cone", {"pyramidal", "elliptic"},
                           "physics.extension.mjc.token", prim_location,
                           result);
  ValidateTokenSetProperty(ps, "mjc:option:jacobian",
                           {"auto", "dense", "sparse"},
                           "physics.extension.mjc.token", prim_location,
                           result);
  ValidateTokenSetProperty(ps, "mjc:option:solver", {"pgs", "cg", "newton"},
                           "physics.extension.mjc.token", prim_location,
                           result);
  ValidateTokenSetProperty(ps, "mjc:compiler:angle", {"degree", "radian"},
                           "physics.extension.mjc.token", prim_location,
                           result);
  ValidateTokenSetProperty(ps, "mjc:compiler:inertiaFromGeom", kAutoBool,
                           "physics.extension.mjc.token", prim_location,
                           result);
  for (const char *prop_name :
       {"mjc:actuatorfrclimited", "mjc:ctrlLimited", "mjc:forceLimited",
        "mjc:actLimited", "mjc:limited"}) {
    ValidateTokenSetProperty(ps, prop_name, kAutoBool,
                             "physics.extension.mjc.token", prim_location,
                             result);
  }
  ValidateTokenSetProperty(ps, "mjc:dynType",
                           {"none", "integrator", "filter", "filterexact",
                            "muscle", "user"},
                           "physics.extension.mjc.token", prim_location,
                           result);
  ValidateTokenSetProperty(ps, "mjc:gainType",
                           {"fixed", "affine", "muscle", "user"},
                           "physics.extension.mjc.token", prim_location,
                           result);
  ValidateTokenSetProperty(ps, "mjc:biasType",
                           {"none", "affine", "muscle", "user"},
                           "physics.extension.mjc.token", prim_location,
                           result);
  if (ps.type_name() == "MjcTendon") {
    ValidateTokenSetProperty(ps, "mjc:type", {"spatial", "fixed"},
                             "physics.extension.mjc.token", prim_location,
                             result);
  } else if (ps.type_name() == "MjcSensor") {
    static const std::set<std::string> kSensorObjectTypes = {
        "body", "xbody", "joint", "geom", "site", "camera", "light",
        "tendon", "actuator", "sensor", "numeric", "text", "tuple", "key",
        "plugin"};
    ValidateTokenSetProperty(
        ps, "mjc:type",
        {"touch", "accelerometer", "velocimeter", "gyro", "force", "torque",
         "magnetometer", "camprojection", "rangefinder", "jointpos",
         "jointvel", "tendonpos", "tendonvel", "actuatorpos", "actuatorvel",
         "actuatorfrc", "ballquat", "ballangvel", "jointlimitpos",
         "jointlimitvel", "jointlimitfrc", "tendonlimitpos",
         "tendonlimitvel", "tendonlimitfrc", "framepos", "framequat",
         "framexaxis", "frameyaxis", "framezaxis", "framelinvel",
         "frameangvel", "framelinacc", "frameangacc", "subtreecom",
         "subtreelinvel", "subtreeangmom", "clock", "user"},
        "physics.extension.mjc.token", prim_location, result);
    ValidateTokenSetProperty(ps, "mjc:objtype", kSensorObjectTypes,
                             "physics.extension.mjc.token", prim_location,
                             result);
    ValidateTokenSetProperty(ps, "mjc:reftype", kSensorObjectTypes,
                             "physics.extension.mjc.token", prim_location,
                             result);
  }
  ValidateTokenSetProperty(ps, "mjc:inertia",
                           {"legacy", "convex", "exact", "shell"},
                           "physics.extension.mjc.token", prim_location,
                           result);
}

void ValidateMjcPhysics(const PrimSpec &ps,
                        const std::vector<AppliedSchema> &schemas,
                        const std::string &prim_location,
                        const PrimTypeByPath &prim_types,
                        USDValidationResult *result) {
  if (!HasSchemaOrPropertyPrefix(ps, schemas, "Mjc", "mjc:") &&
      !StartsWith(ps.type_name(), "Mjc")) {
    return;
  }

  ValidateMjcTokenEnums(ps, prim_location, result);

  for (const char *prop_name :
       {"mjc:option:timestep", "mjc:option:impratio", "mjc:option:tolerance",
        "mjc:option:ls_tolerance", "mjc:option:noslip_tolerance",
        "mjc:option:ccd_tolerance", "mjc:torsionalfriction",
        "mjc:rollingfriction"}) {
    ValidatePositiveNumericProperty(ps, prop_name,
                                    "physics.extension.mjc.range",
                                    prim_location, result);
  }

  for (const char *prop_name :
       {"mjc:option:density", "mjc:option:viscosity", "mjc:option:o_margin",
        "mjc:compiler:boundMass", "mjc:compiler:boundInertia",
        "mjc:stiffness", "mjc:damping", "mjc:armature", "mjc:frictionloss",
        "mjc:margin", "mjc:solmix", "mjc:gap", "mjc:crankLength",
        "mjc:inheritRange", "mjc:width"}) {
    ValidateNonNegativeNumericProperty(ps, prop_name,
                                       "physics.extension.mjc.range",
                                       prim_location, result);
  }

  for (const char *prop_name :
       {"mjc:group", "mjc:priority", "mjc:condim", "mjc:option:iterations",
        "mjc:option:ls_iterations", "mjc:option:noslip_iterations",
        "mjc:option:ccd_iterations", "mjc:option:sdf_iterations",
        "mjc:option:sdf_initpoints"}) {
    ValidateNonNegativeIntegerProperty(ps, prop_name,
                                       "physics.extension.mjc.range",
                                       prim_location, result);
  }
  ValidateNumericMinProperty(ps, "mjc:maxhullvert", -1.0,
                             "physics.extension.mjc.range", prim_location,
                             result);

  ValidateNumericMinMaxPair(ps, "mjc:ctrlRange:min", "mjc:ctrlRange:max",
                            "physics.extension.mjc.range", prim_location,
                            result);
  ValidateNumericMinMaxPair(ps, "mjc:forceRange:min", "mjc:forceRange:max",
                            "physics.extension.mjc.range", prim_location,
                            result);
  ValidateNumericMinMaxPair(ps, "mjc:actRange:min", "mjc:actRange:max",
                            "physics.extension.mjc.range", prim_location,
                            result);
  ValidateNumericMinMaxPair(ps, "mjc:lengthRange:min", "mjc:lengthRange:max",
                            "physics.extension.mjc.range", prim_location,
                            result);
  ValidateNumericMinMaxPair(ps, "mjc:range:min", "mjc:range:max",
                            "physics.extension.mjc.range", prim_location,
                            result);
  ValidateNumericMinMaxPair(ps, "mjc:actuatorfrcrange:min",
                            "mjc:actuatorfrcrange:max",
                            "physics.extension.mjc.range", prim_location,
                            result);

  for (const char *prop_name :
       {"mjc:option:o_solref", "mjc:solref", "mjc:solreflimit",
        "mjc:solreffriction"}) {
    ValidateNumericArrayLengthProperty(ps, prop_name, 2,
                                       "physics.extension.mjc.array",
                                       prim_location, result);
  }
  for (const char *prop_name :
       {"mjc:option:o_solimp", "mjc:solimp", "mjc:solimplimit",
        "mjc:solimpfriction"}) {
    ValidateNumericArrayLengthProperty(ps, prop_name, 5,
                                       "physics.extension.mjc.array",
                                       prim_location, result);
  }
  for (const char *prop_name :
       {"mjc:option:o_friction", "mjc:gear", "mjc:dynPrm", "mjc:gainPrm",
        "mjc:biasPrm", "mjc:path:divisors", "mjc:path:coef",
        "mjc:springdamper", "mjc:springlength", "mjc:qpos", "mjc:qvel",
        "mjc:act", "mjc:ctrl", "mjc:mpos", "mjc:mquat"}) {
    ValidateNumericArrayFiniteProperty(ps, prop_name,
                                       "physics.extension.mjc.array",
                                       prim_location, result);
  }
  ValidateIntArrayLengthMatchesRelationship(
      ps, "mjc:path:indices", "mjc:path", "physics.extension.mjc.array",
      prim_location, result);
  ValidateNumericArrayLengthMatchesRelationship(
      ps, "mjc:path:coef", "mjc:path", "physics.extension.mjc.array",
      prim_location, result);
  ValidateIntArrayLengthMatchesRelationship(
      ps, "mjc:sideSites:indices", "mjc:sideSites",
      "physics.extension.mjc.array", prim_location, result);

  ValidatePhysicsRelationship(ps, "mjc:target",
                              "physics.extension.mjc.relationship",
                              prim_location, prim_types,
                              {"PhysicsJoint", "PhysicsRevoluteJoint",
                               "PhysicsPrismaticJoint",
                               "PhysicsSphericalJoint", "PhysicsFixedJoint",
                               "PhysicsDistanceJoint"},
                              result);
  for (const char *prop_name :
       {"mjc:refSite", "mjc:sliderSite", "mjc:path", "mjc:sideSites"}) {
    ValidatePhysicsRelationship(ps, prop_name,
                                "physics.extension.mjc.relationship",
                                prim_location, prim_types, {}, result);
  }
}

void ValidateNewtonPhysics(const PrimSpec &ps,
                           const std::vector<AppliedSchema> &schemas,
                           const std::string &prim_location,
                           const PrimTypeByPath &prim_types,
                           USDValidationResult *result) {
  if (!HasSchemaOrPropertyPrefix(ps, schemas, "Newton", "newton:") &&
      ps.type_name() != "NewtonActuator") {
    return;
  }

  ValidateNumericMinProperty(ps, "newton:maxSolverIterations", -1.0,
                             "physics.extension.newton.range", prim_location,
                             result);
  ValidatePositiveNumericProperty(ps, "newton:timeStepsPerSecond",
                                  "physics.extension.newton.range",
                                  prim_location, result);
  ValidatePositiveNumericProperty(ps, "newton:delaySteps",
                                  "physics.extension.newton.range",
                                  prim_location, result);

  for (const char *prop_name :
       {"newton:xpbd:softBodyRelaxation", "newton:xpbd:softContactRelaxation",
        "newton:xpbd:jointLinearRelaxation",
        "newton:xpbd:jointAngularRelaxation",
        "newton:xpbd:rigidContactRelaxation"}) {
    ValidateNumericRangeProperty(ps, prop_name, 0.0, 1.0,
                                 "physics.extension.newton.range",
                                 prim_location, result);
  }
  for (const char *prop_name :
       {"newton:xpbd:jointLinearCompliance",
        "newton:xpbd:jointAngularCompliance", "newton:xpbd:angularDamping",
        "newton:kamino:padmm:primalTolerance",
        "newton:kamino:padmm:dualTolerance",
        "newton:kamino:padmm:complementarityTolerance",
        "newton:kamino:constraints:alpha", "newton:kamino:constraints:beta",
        "newton:kamino:constraints:gamma", "newton:contactMargin",
        "newton:torsionalFriction", "newton:rollingFriction", "newton:kp",
        "newton:kd", "newton:ki", "newton:maxEffort",
        "newton:maxMotorEffort", "newton:saturationEffort",
        "newton:velocityLimit"}) {
    ValidateNonNegativeNumericProperty(ps, prop_name,
                                       "physics.extension.newton.range",
                                       prim_location, result);
  }
  ValidateTokenSetProperty(ps, "newton:kamino:padmm:warmstarting",
                           {"none", "containers", "reduced"},
                           "physics.extension.newton.token", prim_location,
                           result);
  ValidateTokenSetProperty(ps, "newton:kamino:jointCorrection",
                           {"twopi", "quaternion"},
                           "physics.extension.newton.token", prim_location,
                           result);
  ValidateAssetPathProperty(ps, "newton:modelPath",
                            "physics.extension.newton.asset", prim_location,
                            result);
  ValidatePhysicsRelationship(ps, "newton:mimicJoint",
                              "physics.extension.newton.relationship",
                              prim_location, prim_types,
                              {"PhysicsJoint", "PhysicsRevoluteJoint",
                               "PhysicsPrismaticJoint",
                               "PhysicsSphericalJoint", "PhysicsFixedJoint",
                               "PhysicsDistanceJoint"},
                              result);
  ValidatePhysicsRelationship(ps, "newton:targets",
                              "physics.extension.newton.relationship",
                              prim_location, prim_types,
                              {"PhysicsJoint", "PhysicsRevoluteJoint",
                               "PhysicsPrismaticJoint",
                               "PhysicsSphericalJoint", "PhysicsFixedJoint",
                               "PhysicsDistanceJoint"},
                              result);

  std::vector<double> lookup_positions;
  std::vector<double> lookup_efforts;
  const bool have_positions =
      GetDoubleArrayProperty(ps, "newton:lookupPositions", &lookup_positions);
  const bool have_efforts =
      GetDoubleArrayProperty(ps, "newton:lookupEfforts", &lookup_efforts);
  if (have_positions || have_efforts) {
    if (!have_positions || !have_efforts ||
        lookup_positions.size() != lookup_efforts.size()) {
      AddError(result, "physics.extension.newton.lookup", prim_location,
               "newton:lookupPositions and newton:lookupEfforts must both be "
               "authored with matching lengths");
    }
    for (size_t i = 1; i < lookup_positions.size(); i++) {
      if (lookup_positions[i] <= lookup_positions[i - 1]) {
        AddError(result, "physics.extension.newton.lookup",
                 MakePropertyLocation(prim_location,
                                      "newton:lookupPositions"),
                 "newton:lookupPositions must be strictly increasing");
        break;
      }
    }
    ValidateNumericArrayFiniteProperty(ps, "newton:lookupPositions",
                                       "physics.extension.newton.lookup",
                                       prim_location, result);
    ValidateNumericArrayFiniteProperty(ps, "newton:lookupEfforts",
                                       "physics.extension.newton.lookup",
                                       prim_location, result);
  }
}

void ValidatePreliminaryPhysics(const PrimSpec &ps,
                                const std::vector<AppliedSchema> &schemas,
                                const std::string &prim_location,
                                const PrimTypeByPath &prim_types,
                                USDValidationResult *result) {
  bool has_preliminary = false;
  for (const AppliedSchema &schema : schemas) {
    if (StartsWith(schema.name, "Preliminary_Physics")) {
      has_preliminary = true;
    }
  }
  for (const std::string &prop_name : AllPropertyNames(ps)) {
    if (StartsWith(prop_name, "preliminary:physics:") ||
        StartsWith(prop_name, "physics:gravitationalForce:") ||
        prop_name == "normal") {
      has_preliminary = true;
    }
  }
  if (!has_preliminary && !StartsWith(ps.type_name(), "Preliminary_Physics") &&
      ps.type_name() != "Preliminary_InfiniteColliderPlane") {
    return;
  }

  ValidateNonNegativeNumericProperty(
      ps, "preliminary:physics:material:restitution",
      "physics.preliminary.range", prim_location, result);
  ValidateNonNegativeNumericProperty(
      ps, "preliminary:physics:material:friction:static",
      "physics.preliminary.range", prim_location, result);
  ValidateNonNegativeNumericProperty(
      ps, "preliminary:physics:material:friction:dynamic",
      "physics.preliminary.range", prim_location, result);
  ValidatePositiveNumericProperty(ps, "preliminary:physics:rigidBody:mass",
                                  "physics.preliminary.range", prim_location,
                                  result);
  ValidateFiniteVec3Property(ps, "physics:gravitationalForce:acceleration",
                             "physics.preliminary.acceleration",
                             prim_location, result, false);
  ValidateFiniteVec3Property(ps, "normal", "physics.preliminary.normal",
                             prim_location, result, true);
  ValidatePhysicsRelationship(ps, "preliminary:physics:collider:convexShape",
                              "physics.preliminary.relationship",
                              prim_location, prim_types, {}, result);
}

void ValidatePhysics(const PrimSpec &ps,
                     const std::vector<AppliedSchema> &applied_schemas,
                     const std::string &prim_location,
                     const PrimTypeByPath &prim_types,
                     USDValidationResult *result) {
  const std::string &type_name = ps.type_name();
  if (!IsPhysicsPrimTypeName(type_name) &&
      !HasAppliedPhysicsSchema(applied_schemas) &&
      !HasSchemaOrPropertyPrefix(ps, applied_schemas, "Mjc", "mjc:") &&
      !HasSchemaOrPropertyPrefix(ps, applied_schemas, "Newton", "newton:") &&
      !HasSchemaOrPropertyPrefix(ps, applied_schemas, "Preliminary_Physics",
                                 "preliminary:physics:")) {
    return;
  }

  if (type_name == "PhysicsScene") {
    ValidatePhysicsScene(ps, prim_location, result);
  }

  ValidatePhysicsScalarProperties(ps, prim_location, result);
  ValidatePhysicsInertiaAndMotion(ps, prim_location, result);
  ValidatePhysicsApproximation(ps, prim_location, result);
  ValidatePhysicsSchemaPlacement(ps, applied_schemas, prim_location, result);

  if (type_name.find("Joint") != std::string::npos) {
    ValidatePhysicsRelationship(ps, "physics:body0", "physics.joint.body",
                                prim_location, prim_types, {}, result);
    ValidatePhysicsRelationship(ps, "physics:body1", "physics.joint.body",
                                prim_location, prim_types, {}, result);
    ValidatePhysicsJointLimits(ps, prim_location, result);
    ValidatePhysicsJointTransforms(ps, prim_location, result);
  }

  ValidatePhysicsRelationship(ps, "physics:simulationOwner",
                              "physics.relationship.target", prim_location,
                              prim_types, {"PhysicsScene"}, result);
  ValidatePhysicsRelationship(ps, "physics:filteredPairs",
                              "physics.relationship.target", prim_location,
                              prim_types, {}, result);
  if (type_name == "PhysicsCollisionGroup") {
    ValidatePhysicsCollisionGroup(ps, prim_location, prim_types, result);
  } else {
    ValidatePhysicsRelationship(ps, "physics:filteredGroups",
                                "physics.relationship.target", prim_location,
                                prim_types, {"PhysicsCollisionGroup"},
                                result);
  }

  ValidatePhysicsDriveAndLimitAPIs(ps, applied_schemas, prim_location, result);
  ValidateMjcPhysics(ps, applied_schemas, prim_location, prim_types, result);
  ValidateNewtonPhysics(ps, applied_schemas, prim_location, prim_types,
                        result);
  ValidatePreliminaryPhysics(ps, applied_schemas, prim_location, prim_types,
                             result);
}

// ---------------------------------------------------------------------------
// Dictionary metadata helpers (next::Dict)
// ---------------------------------------------------------------------------

bool GetMetaDouble(const Dict &dict, const std::string &key, double *out) {
  const Value *v = dict.find(key);
  if (!v || !out) {
    return false;
  }
  return ValueToDouble(*v, out);
}

bool GetMetaAssetPath(const Dict &dict, const std::string &key,
                      std::string *out) {
  const Value *v = dict.find(key);
  if (!v || !out) {
    return false;
  }
  if (const std::string *asset = v->as_asset_path()) {
    *out = *asset;
    return true;
  }
  if (const std::string *str = v->as_string()) {
    *out = *str;
    return true;
  }
  return false;
}

bool GetMetaString(const Dict &dict, const std::string &key,
                   std::string *out) {
  const Value *v = dict.find(key);
  if (!v || !out) {
    return false;
  }
  if (const std::string *str = v->as_string()) {
    *out = *str;
    return true;
  }
  if (const std::string *tok = v->as_token()) {
    *out = *tok;
    return true;
  }
  return false;
}

bool GetMetaStringArray(const Dict &dict, const std::string &key,
                        std::vector<std::string> *out) {
  const Value *v = dict.find(key);
  if (!v || !out || !v->is_array()) {
    return false;
  }
  const std::vector<std::string> *arr = v->as_token_array();
  if (!arr) {
    return false;
  }
  *out = *arr;
  return true;
}

bool GetMetaDouble2Array(const Dict &dict, const std::string &key,
                         std::vector<std::array<double, 2>> *out) {
  const Value *v = dict.find(key);
  if (!v || !out || !v->is_array() || v->type_id() != TypeId::Double2) {
    return false;
  }
  const std::vector<double> *flat = v->as_double_array();
  if (!flat) {
    return false;
  }
  out->clear();
  for (size_t i = 0; i + 1 < flat->size(); i += 2) {
    out->push_back({{(*flat)[i], (*flat)[i + 1]}});
  }
  return true;
}

void ValidateDictionaryKeys(const Dict &dict, const std::string &rule_id,
                            const std::string &location,
                            USDValidationResult *result) {
  for (const auto &entry : dict.entries) {
    const std::string key_location = location + "." + entry.first;
    if (!IsValidNamespacedIdentifier(entry.first)) {
      AddError(result, rule_id, location,
               "dictionary key `" + entry.first +
                   "` is not a valid namespaced identifier");
    }
    if (const Dict *child = entry.second.as_dictionary()) {
      ValidateDictionaryKeys(*child, rule_id, key_location, result);
    }
  }
}

void ValidateDictValue(const Value &v, const std::string &rule_id,
                       const std::string &location,
                       USDValidationResult *result) {
  if (const Dict *dict = v.as_dictionary()) {
    ValidateDictionaryKeys(*dict, rule_id, location, result);
  }
}

// ---------------------------------------------------------------------------
// Composition metadata (arcs, variants, relocates)
// ---------------------------------------------------------------------------

// True if `ps` authors any composition arc (used to gate locality-dependent
// rules; a composed prim may pull opinions from another layer).
bool HasCompositionArc(const PrimSpec &ps) {
  const PrimSpecMeta &m = ps.meta();
  if (!m.references.empty() || !m.payloads.empty() || !m.inherits.empty() ||
      !m.specializes.empty()) {
    return true;
  }
  if (!m.variantSelection.empty() || !m.variantSelections().empty() ||
      !m.variantSets().empty()) {
    return true;
  }
  const ArcListOpEdits *edits = m.arc_edits();
  if (edits && (edits->references.has_authored_opinion() ||
                edits->payloads.has_authored_opinion() ||
                edits->inherits.has_authored_opinion() ||
                edits->specializes.has_authored_opinion())) {
    return true;
  }
  return false;
}

bool IsOverride(const PrimSpec &ps) {
  return ps.specifier() == PrimSpecifier::Over;
}

// Gather every authored arc string of one arc field (inline explicit list +
// any list-op edit buckets).
std::vector<std::string> GatherArcStrings(const std::vector<std::string> &inline_items,
                                          const ArcEdit *edit) {
  std::vector<std::string> items = inline_items;
  if (edit) {
    items.insert(items.end(), edit->prepended.begin(), edit->prepended.end());
    items.insert(items.end(), edit->appended.begin(), edit->appended.end());
    items.insert(items.end(), edit->deleted.begin(), edit->deleted.end());
    items.insert(items.end(), edit->ordered.begin(), edit->ordered.end());
  }
  return items;
}

void ValidateRefArcString(const std::string &arc_str, bool is_payload,
                          const std::string &rule_id,
                          const std::string &prim_location,
                          USDValidationResult *result) {
  CompositionArc arc = is_payload ? Compositor::ParsePayload(arc_str)
                                  : Compositor::ParseReference(arc_str);
  // Programmatically-built layers may store a bare prim path (the USDA parser
  // stores the canonical "</path>" form). Accept both, like the compositor.
  if (arc.prim_path.empty() && arc.asset_path.empty() && !arc_str.empty() &&
      arc_str[0] == '/') {
    arc.prim_path = arc_str;
  }
  const char *what = is_payload ? "payload" : "reference";
  if (arc.asset_path.empty() && arc.prim_path.empty()) {
    AddError(result, rule_id, prim_location,
             std::string(what) + " must author an asset path or prim path");
  }
  if (!arc.prim_path.empty()) {
    std::string err;
    if (!IsValidAbsolutePrimTargetPathString(arc.prim_path, &err)) {
      AddError(result, rule_id, prim_location,
               std::string(what) + " primPath `" + arc.prim_path +
                   "` is invalid: " + err);
    }
  }
  if (!arc.layer_offset.empty()) {
    double offset = 0.0;
    double scale = 1.0;
    Compositor::ParseLayerOffset(arc.layer_offset, offset, scale);
    if (scale == 0.0) {
      AddError(result, rule_id, prim_location,
               "composition layerOffset scale must not be 0");
    }
  }
}

void ValidatePathArcString(const std::string &arc_str,
                           const std::string &rule_id,
                           const std::string &prim_location,
                           const PrimSpecifierByPath &prim_specifiers,
                           USDValidationResult *result,
                           bool expect_class_target) {
  CompositionArc arc = Compositor::ParseReference(arc_str);
  if (arc.prim_path.empty() && arc.asset_path.empty() && !arc_str.empty() &&
      arc_str[0] == '/') {
    arc.prim_path = arc_str;
  }
  const std::string &target = arc.prim_path;
  std::string err;
  if (!IsValidAbsolutePrimTargetPathString(target, &err)) {
    AddError(result, rule_id, prim_location,
             "target `" + (target.empty() ? arc_str : target) +
                 "` is invalid: " + err);
    return;
  }
  if (target == prim_location) {
    AddError(result, rule_id, prim_location,
             "composition arc must not target the prim itself");
  }
  const PrimSpecifier *target_spec = FindPrimSpecifier(prim_specifiers, target);
  if (expect_class_target && target_spec &&
      *target_spec != PrimSpecifier::Class) {
    AddWarning(result, rule_id, prim_location,
               "local target `" + target + "` is not a class prim");
  }
}

void ValidateVariantSetsAndSelections(const PrimSpec &ps,
                                      const std::string &prim_location,
                                      USDValidationResult *result) {
  // NOTE (structural approximation): variant OPTION bodies (properties/child
  // prims inside a variant) are stored as VariantData / graft layers in
  // next-core rather than PrimSpecs, and are not deep-validated here. Only
  // set / variant / selection names are checked (legacy additionally walks
  // the variant PrimSpec bodies).
  std::set<std::string> local_sets;
  std::map<std::string, std::set<std::string>> local_variants;
  for (const VariantSetData &vs : ps.meta().variantSets()) {
    local_sets.insert(vs.name);
    if (vs.name.empty() || !IsValidIdentifier(vs.name)) {
      AddError(result, "core.composition.variantSet", prim_location,
               "variant set statement name `" + vs.name +
                   "` is not a valid identifier");
    }
    if (vs.variants.empty()) {
      AddWarning(result, "core.composition.variantSet", prim_location,
                 "variant set `" + vs.name + "` has no variants");
    }
    for (const VariantData &variant : vs.variants) {
      local_variants[vs.name].insert(variant.name);
      if (variant.name.empty() || !IsValidIdentifier(variant.name)) {
        AddError(result, "core.composition.variantSet", prim_location,
                 "variant name `" + variant.name + "` in set `" + vs.name +
                     "` is not a valid identifier");
      }
    }
  }

  // Collect selections: the plural list plus the legacy inline single
  // selection (the USDA parser mirrors the first selection into both fields,
  // so deduplicate).
  std::vector<std::pair<std::string, std::string>> selections =
      ps.meta().variantSelections();
  if (!ps.meta().variantSelection.empty()) {
    const VariantSelection sel =
        Compositor::ParseVariantSelection(ps.meta().variantSelection);
    if (!sel.variant_set.empty() || !sel.variant_name.empty()) {
      const std::pair<std::string, std::string> entry(sel.variant_set,
                                                      sel.variant_name);
      if (std::find(selections.begin(), selections.end(), entry) ==
          selections.end()) {
        selections.push_back(entry);
      }
    }
  }

  for (const auto &selection : selections) {
    const std::string &set_name = selection.first;
    const std::string &variant_name = selection.second;
    if (set_name.empty() || !IsValidIdentifier(set_name)) {
      AddError(result, "core.composition.variantSelection", prim_location,
               "variant selection set name `" + set_name +
                   "` is not a valid identifier");
    }
    if (variant_name.empty() || !IsValidIdentifier(variant_name)) {
      AddError(result, "core.composition.variantSelection", prim_location,
               "variant selection `" + variant_name + "` for set `" +
                   set_name + "` is not a valid identifier");
      continue;
    }
    if (local_sets.count(set_name) &&
        local_variants[set_name].count(variant_name) == 0) {
      AddError(result, "core.composition.variantSelection", prim_location,
               "variant selection `" + variant_name + "` is not authored in "
                   "local variant set `" +
                   set_name + "`");
    }
  }
}

// next-core stores relocates per-prim (PrimSpecMeta::relocates); validated
// under the legacy core.layer.layerRelocates rule ids at the prim location.
void ValidateRelocates(const PrimSpec &ps, const std::string &prim_location,
                       USDValidationResult *result) {
  std::set<std::string> relocated_sources;
  for (const auto &entry : ps.meta().relocates()) {
    const std::string &source = entry.first;
    const std::string &target = entry.second;

    std::string source_err;
    if (!IsValidAbsolutePrimTargetPathString(source, &source_err)) {
      AddError(result, "core.layer.layerRelocates.source", prim_location,
               "layerRelocates source `" + source +
                   "` must be a valid absolute prim path");
    }
    std::string target_err;
    if (!target.empty() &&
        !IsValidAbsolutePrimPathString(target, &target_err)) {
      AddError(result, "core.layer.layerRelocates.target", prim_location,
               "layerRelocates target `" + target +
                   "` must be empty or a valid absolute prim path");
    }
    if (!source.empty() && !target.empty() && source == target) {
      AddError(result, "core.layer.layerRelocates.identity", prim_location,
               "layerRelocates source and target must not be identical");
    }
    if (!source.empty() && !relocated_sources.insert(source).second) {
      AddError(result, "core.layer.layerRelocates.source", prim_location,
               "layerRelocates contains duplicate source `" + source + "`");
    }
  }
}

void ValidateCompositionMetadata(const PrimSpec &ps,
                                 const std::string &prim_location,
                                 const PrimSpecifierByPath &prim_specifiers,
                                 USDValidationResult *result) {
  const PrimSpecMeta &m = ps.meta();
  const ArcListOpEdits *edits = m.arc_edits();

  // NOTE: invalid list-edit qualifiers are unrepresentable in next-core
  // (ArcEdit qualifiers are typed), so the legacy "invalid list-edit
  // qualifier" errors have no equivalent here.
  for (const std::string &ref : GatherArcStrings(
           m.references, edits ? &edits->references : nullptr)) {
    ValidateRefArcString(ref, /* is_payload */ false,
                         "core.composition.reference", prim_location, result);
  }
  for (const std::string &pl :
       GatherArcStrings(m.payloads, edits ? &edits->payloads : nullptr)) {
    ValidateRefArcString(pl, /* is_payload */ true,
                         "core.composition.payload", prim_location, result);
  }
  for (const std::string &inh :
       GatherArcStrings(m.inherits, edits ? &edits->inherits : nullptr)) {
    ValidatePathArcString(inh, "core.composition.inherits", prim_location,
                          prim_specifiers, result,
                          /* expect_class_target */ true);
  }
  for (const std::string &sp : GatherArcStrings(
           m.specializes, edits ? &edits->specializes : nullptr)) {
    ValidatePathArcString(sp, "core.composition.specializes", prim_location,
                          prim_specifiers, result,
                          /* expect_class_target */ true);
  }

  // The prim-level layer offset holds the offset applied by composition arcs
  // authored on this prim; a zero scale is always invalid.
  if (m.layer_offset.second == 0.0) {
    AddError(result, "core.composition.reference", prim_location,
             "composition layerOffset scale must not be 0");
  }

  ValidateVariantSetsAndSelections(ps, prim_location, result);
  ValidateRelocates(ps, prim_location, result);

  if (ps.specifier() == PrimSpecifier::Over && ps.type_name().empty() &&
      !HasCompositionArc(ps) && ps.properties().size() == 0 &&
      ps.relationship_names().empty() && ps.child_indices().empty()) {
    AddWarning(result, "core.composition.over", prim_location,
               "empty typeless over has no local opinions or composition "
               "arcs");
  }
}

// ---------------------------------------------------------------------------
// apiSchemas / prim metadata
// ---------------------------------------------------------------------------

std::vector<AppliedSchema> CollectAppliedSchemas(const PrimSpec &ps) {
  std::vector<AppliedSchema> schemas;
  for (const std::string &entry : ps.meta().apiSchemas()) {
    std::string schema_name = entry;
    std::string instance_name;
    const size_t colon_pos = entry.find(':');
    if (colon_pos != std::string::npos) {
      schema_name = entry.substr(0, colon_pos);
      instance_name = entry.substr(colon_pos + 1);
    }
    schemas.push_back({schema_name, instance_name});
  }
  return schemas;
}

std::set<std::string> CollectAppliedCollectionInstances(
    const std::vector<AppliedSchema> &schemas) {
  std::set<std::string> instances;
  for (const auto &schema : schemas) {
    if (schema.name == "CollectionAPI" && !schema.instance_name.empty()) {
      instances.insert(schema.instance_name);
    }
  }
  return instances;
}

void ValidateAPISchemasMetadata(const std::vector<AppliedSchema> &schemas,
                                const std::string &prim_location,
                                USDValidationResult *result) {
  // NOTE (structural approximation): next-core has no API-schema registry, so
  // the legacy core.apiSchema.unknown warning and the generic
  // single-apply-with-instance-name error cannot be evaluated; only the known
  // multiple-apply schemas' instance requirement is enforced.
  std::set<std::string> seen;
  for (const AppliedSchema &schema : schemas) {
    if (schema.name.empty() || !IsValidIdentifier(schema.name)) {
      AddError(result, "core.apiSchema.name", prim_location,
               "apiSchemas entry `" + schema.name +
                   "` is not a valid schema identifier");
    }
    if (!schema.instance_name.empty() &&
        !IsValidNamespacedIdentifier(schema.instance_name)) {
      AddError(result, "core.apiSchema.instance", prim_location,
               "apiSchemas instance `" + schema.instance_name +
                   "` is not a valid namespaced identifier");
    }
    if (IsMultipleApplySchemaName(schema.name) &&
        schema.instance_name.empty()) {
      AddError(result, "core.apiSchema.instance", prim_location,
               schema.name + " is a multiple-apply API schema and requires "
                             "an instance name");
    }
    const std::string key = schema.name + ":" + schema.instance_name;
    if (!seen.insert(key).second) {
      AddWarning(result, "core.apiSchema.duplicate", prim_location,
                 "duplicate apiSchemas entry `" + key + "`");
    }
  }
}

void ValidatePrimMetadata(const PrimSpec &ps, const std::string &prim_location,
                          const std::vector<AppliedSchema> &applied_schemas,
                          USDValidationResult *result) {
  const PrimSpecMeta &m = ps.meta();

  if (!m.kind().empty() && !IsValidNamespacedIdentifier(m.kind())) {
    AddError(result, "core.prim.kind", prim_location,
             "kind `" + m.kind() + "` is not a valid token");
  }

  const ArcListOpEdits *edits = m.arc_edits();
  const bool has_ref_or_payload =
      !m.references.empty() || !m.payloads.empty() ||
      (edits && (edits->references.has_authored_opinion() ||
                 edits->payloads.has_authored_opinion()));
  if (m.instanceable && !has_ref_or_payload && ps.child_indices().empty()) {
    AddWarning(result, "core.prim.instanceable", prim_location,
               "instanceable prim should usually compose a prototype via "
               "references or payloads, or contain authored children");
  }

  ValidateDictValue(m.customData(), "core.prim.customData",
                    prim_location + ".customData", result);

  if (const Dict *asset_info = m.assetInfo().as_dictionary()) {
    ValidateDictionaryKeys(*asset_info, "core.prim.assetInfo",
                           prim_location + ".assetInfo", result);

    std::string identifier;
    if (GetMetaAssetPath(*asset_info, "identifier", &identifier) &&
        identifier.empty()) {
      AddError(result, "core.prim.assetInfo",
               prim_location + ".assetInfo.identifier",
               "assetInfo identifier must not be empty");
    }
    std::string name;
    if (GetMetaString(*asset_info, "name", &name) && name.empty()) {
      AddWarning(result, "core.prim.assetInfo",
                 prim_location + ".assetInfo.name",
                 "assetInfo name should not be empty");
    }
    std::vector<std::string> deps;
    if (GetMetaStringArray(*asset_info, "payloadAssetDependencies", &deps)) {
      for (size_t i = 0; i < deps.size(); i++) {
        if (deps[i].empty()) {
          AddError(result, "core.prim.assetInfo",
                   prim_location + ".assetInfo.payloadAssetDependencies",
                   "payloadAssetDependencies[" + std::to_string(i) +
                       "] must not be empty");
        }
      }
    }
  }

  ValidateAPISchemasMetadata(applied_schemas, prim_location, result);
}

void ValidateMaterialXReferenceConventions(const PrimSpec &ps,
                                           const std::string &prim_location,
                                           USDValidationResult *result) {
  const ArcListOpEdits *edits = ps.meta().arc_edits();
  for (const std::string &ref : GatherArcStrings(
           ps.meta().references, edits ? &edits->references : nullptr)) {
    const CompositionArc arc = Compositor::ParseReference(ref);
    if (!IsMaterialXAssetPath(arc.asset_path)) {
      continue;
    }
    if (arc.prim_path != "/MaterialX") {
      AddWarning(result, "shade.materialX.referencePrimPath", prim_location,
                 "MaterialX references should target </MaterialX>");
    }
  }
}

// ---------------------------------------------------------------------------
// clips metadata
// ---------------------------------------------------------------------------

void ValidateClipPairArray(const std::vector<std::array<double, 2>> &pairs,
                           const std::string &rule_id,
                           const std::string &location,
                           const std::string &field_name,
                           USDValidationResult *result) {
  bool have_prev_stage_time = false;
  double prev_stage_time = 0.0;
  for (size_t i = 0; i < pairs.size(); i++) {
    const double stage_time = pairs[i][0];
    const double mapped_value = pairs[i][1];
    if (!std::isfinite(stage_time) || !std::isfinite(mapped_value)) {
      AddError(result, rule_id, location,
               field_name + "[" + std::to_string(i) +
                   "] must contain finite values");
    }
    if (have_prev_stage_time && stage_time <= prev_stage_time) {
      AddError(result, rule_id, location,
               field_name + " stage times must be strictly increasing");
    }
    have_prev_stage_time = true;
    prev_stage_time = stage_time;
  }
}

void ValidateClipSetMetadata(const std::string &clip_set_name,
                             const Dict &clip_set,
                             const std::string &prim_location,
                             USDValidationResult *result) {
  const std::string location = prim_location + ".clips[" + clip_set_name + "]";
  std::vector<std::string> asset_paths;
  const bool have_asset_paths =
      GetMetaStringArray(clip_set, "assetPaths", &asset_paths);

  std::string template_asset_path;
  const bool have_template_asset =
      GetMetaAssetPath(clip_set, "templateAssetPath", &template_asset_path);

  if (!have_asset_paths && !have_template_asset) {
    AddError(result, "core.clips.assets", location,
             "clip set must author assetPaths or templateAssetPath");
  }

  if (have_asset_paths) {
    if (asset_paths.empty()) {
      AddError(result, "core.clips.assets", location + ".assetPaths",
               "assetPaths must not be empty");
    }
    for (size_t i = 0; i < asset_paths.size(); i++) {
      if (asset_paths[i].empty()) {
        AddError(result, "core.clips.assets", location + ".assetPaths",
                 "assetPaths[" + std::to_string(i) + "] must not be empty");
      }
    }
  }

  if (have_template_asset) {
    if (template_asset_path.empty()) {
      AddError(result, "core.clips.template", location + ".templateAssetPath",
               "templateAssetPath must not be empty");
    }
    double start_time = 0.0;
    double end_time = 0.0;
    double stride = 1.0;
    const bool have_start =
        GetMetaDouble(clip_set, "templateStartTime", &start_time);
    const bool have_end =
        GetMetaDouble(clip_set, "templateEndTime", &end_time);
    const bool have_stride =
        GetMetaDouble(clip_set, "templateStride", &stride);
    if (!have_start || !have_end) {
      AddError(result, "core.clips.template", location,
               "template clip sets must author templateStartTime and "
               "templateEndTime");
    } else {
      if (!std::isfinite(start_time) || !std::isfinite(end_time)) {
        AddError(result, "core.clips.template", location,
                 "templateStartTime and templateEndTime must be finite");
      }
      if (end_time < start_time) {
        AddError(result, "core.clips.template", location,
                 "templateEndTime must be greater than or equal to "
                 "templateStartTime");
      }
    }
    if (have_stride && (!std::isfinite(stride) || stride <= 0.0)) {
      AddError(result, "core.clips.template", location + ".templateStride",
               "templateStride must be positive and finite");
    }
  }

  std::vector<std::array<double, 2>> active;
  if (GetMetaDouble2Array(clip_set, "active", &active)) {
    if (active.empty()) {
      AddError(result, "core.clips.active", location + ".active",
               "active must not be empty");
    }
    ValidateClipPairArray(active, "core.clips.active", location + ".active",
                          "active", result);
    if (have_asset_paths) {
      for (size_t i = 0; i < active.size(); i++) {
        const double asset_index = active[i][1];
        const double rounded = std::floor(asset_index);
        if (std::fabs(asset_index - rounded) > 1.0e-9 || asset_index < 0.0 ||
            static_cast<size_t>(asset_index) >= asset_paths.size()) {
          AddError(result, "core.clips.active", location + ".active",
                   "active[" + std::to_string(i) +
                       "] references asset index " +
                       std::to_string(asset_index) + ", but assetPaths has " +
                       std::to_string(asset_paths.size()) + " entries");
        }
      }
    }
  } else if (have_asset_paths) {
    AddError(result, "core.clips.active", location,
             "explicit clip sets with assetPaths must author active");
  }

  std::vector<std::array<double, 2>> times;
  if (GetMetaDouble2Array(clip_set, "times", &times)) {
    ValidateClipPairArray(times, "core.clips.times", location + ".times",
                          "times", result);
  }

  std::string prim_path;
  if (GetMetaString(clip_set, "primPath", &prim_path)) {
    std::string err;
    if (!IsValidAbsolutePrimPathString(prim_path, &err) || prim_path == "/") {
      AddError(result, "core.clips.primPath", location + ".primPath",
               "primPath must be an absolute prim path, but is `" + prim_path +
                   "`");
    }
  }

  std::string manifest_asset_path;
  if (GetMetaAssetPath(clip_set, "manifestAssetPath", &manifest_asset_path) &&
      manifest_asset_path.empty()) {
    AddError(result, "core.clips.assets", location + ".manifestAssetPath",
             "manifestAssetPath must not be empty");
  }
}

void ValidateClipsMetadata(const PrimSpec &ps,
                           const std::string &prim_location,
                           USDValidationResult *result) {
  const Dict *clips = ps.meta().clips().as_dictionary();
  if (!clips) {
    return;
  }
  if (clips->empty()) {
    AddError(result, "core.clips", prim_location + ".clips",
             "clips dictionary must contain at least one clip set");
    return;
  }

  for (const auto &clip_entry : clips->entries) {
    const Dict *clip_set = clip_entry.second.as_dictionary();
    if (!clip_set) {
      AddError(result, "core.clips", prim_location + ".clips",
               "clip set `" + clip_entry.first + "` must be a dictionary");
      continue;
    }
    ValidateClipSetMetadata(clip_entry.first, *clip_set, prim_location,
                            result);
  }
}

// ---------------------------------------------------------------------------
// Attribute / relationship metadata (PropMeta), attribute type checks
// ---------------------------------------------------------------------------

void ValidateTokenAttributeType(const PrimSpec &ps,
                                const std::string &prop_name,
                                const std::string &rule_id,
                                const std::string &location,
                                USDValidationResult *result) {
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             "expected an attribute, but found a relationship");
    return;
  }
  const std::string type_name = AttrTypeNameOf(ps, prop_name);
  if (type_name != "token") {
    AddError(result, rule_id, location,
             "expected attribute type `token`, but found `" + type_name +
                 "`");
  }
}

void ValidateExactAttributeType(const PrimSpec &ps,
                                const std::string &prop_name,
                                const std::string &expected,
                                const std::string &rule_id,
                                const std::string &location,
                                USDValidationResult *result) {
  if (IsRelationshipProp(ps, prop_name)) {
    AddError(result, rule_id, location,
             "expected an attribute, but found a relationship");
    return;
  }
  const std::string type_name = AttrTypeNameOf(ps, prop_name);
  if (type_name != expected) {
    AddError(result, rule_id, location,
             "expected attribute type `" + expected + "`, but found `" +
                 type_name + "`");
  }
}

void ValidateUniformAttribute(const PrimSpec &ps, const std::string &prop_name,
                              const std::string &rule_id,
                              const std::string &location,
                              USDValidationResult *result) {
  const PropSlot *slot = GetSlot(ps, prop_name);
  if (!slot || slot->is_relationship()) {
    return;
  }
  if (!slot->is_uniform()) {
    AddError(result, rule_id, location, "expected `uniform` variability");
  }
}

void ValidateVaryingAttribute(const PrimSpec &ps, const std::string &prop_name,
                              const std::string &rule_id,
                              const std::string &location,
                              USDValidationResult *result) {
  const PropSlot *slot = GetSlot(ps, prop_name);
  if (!slot || slot->is_relationship()) {
    return;
  }
  if (slot->is_uniform()) {
    AddError(result, rule_id, location, "expected `varying` variability");
  }
}

void ValidateAttributeMetadata(const PrimSpec &ps,
                               const std::string &prop_name,
                               const std::string &location,
                               USDValidationResult *result) {
  const PropMeta *metas = ps.property_meta(prop_name);
  if (!metas) {
    return;
  }

  if (metas->authored & PropMeta::kCustomData) {
    ValidateDictValue(metas->customData, "core.attr.customData",
                      location + ".customData", result);
  }
  if (metas->authored & PropMeta::kSdrMetadata) {
    ValidateDictValue(metas->sdrMetadata, "core.attr.sdrMetadata",
                      location + ".sdrMetadata", result);
  }

  if ((metas->authored & PropMeta::kInterpolation) &&
      !IsValidPrimvarInterpolation(metas->interpolation)) {
    AddError(result, "core.attr.interpolation", location,
             "invalid interpolation token `" + metas->interpolation + "`");
  }
  if ((metas->authored & PropMeta::kElementSize) && metas->elementSize <= 0) {
    AddError(result, "core.attr.elementSize", location,
             "elementSize must be greater than 0");
  }
  if ((metas->authored & PropMeta::kUnauthoredIdx) &&
      metas->unauthoredValuesIndex < -1) {
    AddError(result, "core.attr.unauthoredValuesIndex", location,
             "unauthoredValuesIndex must be -1 or greater");
  }

  if (metas->authored & PropMeta::kAllowedTokens) {
    const std::vector<std::string> &allowed = metas->allowedTokens;
    if (allowed.empty()) {
      AddWarning(result, "core.attr.allowedTokens", location,
                 "allowedTokens should not be empty when authored");
    }

    std::set<std::string> allowed_set;
    for (size_t i = 0; i < allowed.size(); i++) {
      const std::string &token = allowed[i];
      if (token.empty()) {
        AddError(result, "core.attr.allowedTokens", location,
                 "allowedTokens[" + std::to_string(i) +
                     "] must not be empty");
      } else if (!allowed_set.insert(token).second) {
        AddWarning(result, "core.attr.allowedTokens", location,
                   "duplicate allowedTokens entry `" + token + "`");
      }
    }

    const Value *v = GetAttrValue(ps, prop_name);
    if (v && !allowed_set.empty()) {
      if (const std::string *token_value = v->as_token()) {
        if (allowed_set.count(*token_value) == 0) {
          AddError(result, "core.attr.allowedTokens", location,
                   prop_name + " value `" + *token_value +
                       "` is not listed in allowedTokens");
        }
      }
      if (v->is_array() && v->type_id() == TypeId::Token) {
        const std::vector<std::string> *token_array = v->as_token_array();
        if (token_array) {
          for (size_t i = 0; i < token_array->size(); i++) {
            const std::string &token = (*token_array)[i];
            if (allowed_set.count(token) == 0) {
              AddError(result, "core.attr.allowedTokens", location,
                       prop_name + "[" + std::to_string(i) + "] value `" +
                           token + "` is not listed in allowedTokens");
            }
          }
        }
      }
    }
  }

  if (metas->authored & PropMeta::kConnectability) {
    if (metas->connectability != "full" &&
        metas->connectability != "interfaceOnly") {
      AddError(result, "core.attr.connectability", location,
               "connectability must be `full` or `interfaceOnly`, but is `" +
                   metas->connectability + "`");
    }
  }
  if ((metas->authored & PropMeta::kRenderType) &&
      metas->renderType.empty()) {
    AddWarning(result, "core.attr.renderType", location,
               "renderType should not be empty when authored");
  }
  if ((metas->authored & PropMeta::kOutputName) &&
      metas->outputName.empty()) {
    AddWarning(result, "core.attr.outputName", location,
               "outputName should not be empty when authored");
  }
}

void ValidateRelationshipMetadata(const PrimSpec &ps,
                                  const std::string &rel_name,
                                  const std::string &location,
                                  USDValidationResult *result) {
  // NOTE: the legacy core.relationship.variability warning ("authored varying
  // variability is ignored") is unrepresentable in next-core (relationships
  // carry no varying-authored flag).
  const PropMeta *metas = ps.property_meta(rel_name);
  if (!metas) {
    return;
  }
  if (metas->authored & PropMeta::kCustomData) {
    ValidateDictValue(metas->customData, "core.relationship.customData",
                      location + ".customData", result);
  }
  if (metas->authored & PropMeta::kBindMaterialAs) {
    const std::string &value = metas->bindMaterialAs;
    if (value != "strongerThanDescendants" &&
        value != "weakerThanDescendants") {
      AddError(result, "core.relationship.bindMaterialAs", location,
               "bindMaterialAs must be `strongerThanDescendants` or "
               "`weakerThanDescendants`, but is `" +
                   value + "`");
    }
  }
}

// ---------------------------------------------------------------------------
// ColorSpace / CollectionAPI schema rules
// ---------------------------------------------------------------------------

bool IsCanonicalColorSpaceToken(const std::string &token) {
  static const std::unordered_set<std::string> kCanonicalTokens = {
      "lin_ap1_scene",     "lin_ap0_scene",      "lin_rec709_scene",
      "lin_p3d65_scene",   "lin_rec2020_scene",  "lin_adobergb_scene",
      "lin_ciexyzd65_scene", "srgb_rec709_scene", "g22_rec709_scene",
      "g18_rec709_scene",  "srgb_ap1_scene",     "g22_ap1_scene",
      "srgb_p3d65_scene",  "g22_adobergb_scene", "data",
      "unknown",           "raw",                "identity",
  };
  return kCanonicalTokens.count(token) > 0;
}

bool IsColorSpaceDefinitionPropertyName(const std::string &prop_name) {
  return prop_name == "name" || prop_name == "redChroma" ||
         prop_name == "greenChroma" || prop_name == "blueChroma" ||
         prop_name == "whitePoint" || prop_name == "gamma" ||
         prop_name == "linearBias" ||
         prop_name == "colorSpaceDefinition:name" ||
         prop_name == "colorSpaceDefinition:redChroma" ||
         prop_name == "colorSpaceDefinition:greenChroma" ||
         prop_name == "colorSpaceDefinition:blueChroma" ||
         prop_name == "colorSpaceDefinition:whitePoint" ||
         prop_name == "colorSpaceDefinition:gamma" ||
         prop_name == "colorSpaceDefinition:linearBias";
}

ColorSpaceSet CollectLocalColorSpaceDefinitions(
    const PrimSpec &ps, const std::vector<AppliedSchema> &applied_schemas) {
  ColorSpaceSet local_defs;
  if (!HasAppliedSchema(applied_schemas, "ColorSpaceDefinitionAPI")) {
    return local_defs;
  }
  local_defs.insert("custom");
  for (const char *prop_name : {"name", "colorSpaceDefinition:name"}) {
    std::string value;
    if (GetTokenProperty(ps, prop_name, &value) && !value.empty()) {
      local_defs.insert(value);
    }
  }
  return local_defs;
}

bool IsKnownColorSpaceToken(const std::string &token,
                            const ColorSpaceSet &visible_custom_spaces) {
  if (token.empty()) {
    return true;
  }
  if (IsCanonicalColorSpaceToken(token)) {
    return true;
  }
  return visible_custom_spaces.count(token) > 0;
}

void ValidateColorSpaceToken(const std::string &token,
                             const ColorSpaceSet &visible_custom_spaces,
                             const std::string &rule_id,
                             const std::string &location,
                             USDValidationResult *result) {
  if (IsKnownColorSpaceToken(token, visible_custom_spaces)) {
    return;
  }
  AddError(result, rule_id, location,
           "uses unknown color space token `" + token +
               "` with no visible ColorSpaceDefinitionAPI definition");
}

void ValidateColorSpaceDefinitionProperty(const PrimSpec &ps,
                                          const std::string &prop_name,
                                          const std::string &location,
                                          USDValidationResult *result) {
  if (prop_name == "name" || prop_name == "colorSpaceDefinition:name") {
    ValidateTokenAttributeType(ps, prop_name,
                               "core.schema.ColorSpaceDefinitionAPI.name",
                               location, result);
    ValidateUniformAttribute(ps, prop_name,
                             "core.schema.ColorSpaceDefinitionAPI.name",
                             location, result);
    return;
  }

  if (prop_name == "redChroma" ||
      prop_name == "colorSpaceDefinition:redChroma" ||
      prop_name == "greenChroma" ||
      prop_name == "colorSpaceDefinition:greenChroma" ||
      prop_name == "blueChroma" ||
      prop_name == "colorSpaceDefinition:blueChroma" ||
      prop_name == "whitePoint" ||
      prop_name == "colorSpaceDefinition:whitePoint") {
    ValidateExactAttributeType(
        ps, prop_name, "float2",
        "core.schema.ColorSpaceDefinitionAPI.coordinates", location, result);
    ValidateVaryingAttribute(ps, prop_name,
                             "core.schema.ColorSpaceDefinitionAPI.coordinates",
                             location, result);
    return;
  }

  if (prop_name == "gamma" || prop_name == "colorSpaceDefinition:gamma" ||
      prop_name == "linearBias" ||
      prop_name == "colorSpaceDefinition:linearBias") {
    ValidateExactAttributeType(ps, prop_name, "float",
                               "core.schema.ColorSpaceDefinitionAPI.curve",
                               location, result);
    ValidateVaryingAttribute(ps, prop_name,
                             "core.schema.ColorSpaceDefinitionAPI.curve",
                             location, result);
  }
}

void ValidateCollectionProperty(const PrimSpec &ps,
                                const std::vector<std::string> &segments,
                                const std::string &prop_name,
                                const std::set<std::string> &collection_instances,
                                const std::string &location,
                                USDValidationResult *result) {
  if (segments.size() < 2) {
    AddError(result, "core.schema.CollectionAPI.property", location,
             "collection property must include an instance name");
    return;
  }

  const std::string &instance_name = segments[1];
  if (instance_name.empty()) {
    AddError(result, "core.schema.CollectionAPI.instance", location,
             "collection property must use a non-empty instance name");
    return;
  }

  if (collection_instances.count(instance_name) == 0) {
    AddError(result, "core.schema.CollectionAPI.applied", location,
             "collection property is authored without applying "
             "`CollectionAPI:" +
                 instance_name + "`");
  }

  if (segments.size() == 2) {
    return;
  }

  if (segments.size() != 3) {
    AddError(result, "core.schema.CollectionAPI.property", location,
             "collection property has an unsupported namespace shape");
    return;
  }

  const std::string &member = segments[2];
  if (member == "expansionRule") {
    ValidateTokenAttributeType(ps, prop_name,
                               "core.schema.CollectionAPI.expansionRule",
                               location, result);
    ValidateUniformAttribute(ps, prop_name,
                             "core.schema.CollectionAPI.expansionRule",
                             location, result);
    std::string value;
    if (GetTokenProperty(ps, prop_name, &value)) {
      if (value != "explicitOnly" && value != "expandPrims" &&
          value != "expandPrimsAndProperties") {
        AddError(result, "core.schema.CollectionAPI.expansionRule", location,
                 "invalid expansionRule `" + value + "`");
      }
    }
    return;
  }

  if (member == "includeRoot") {
    ValidateExactAttributeType(ps, prop_name, "bool",
                               "core.schema.CollectionAPI.includeRoot",
                               location, result);
    ValidateUniformAttribute(ps, prop_name,
                             "core.schema.CollectionAPI.includeRoot",
                             location, result);
    return;
  }

  if (member == "includes" || member == "excludes") {
    if (!IsRelationshipProp(ps, prop_name)) {
      AddError(result, "core.schema.CollectionAPI.relationship", location,
               "expected a relationship");
      return;
    }

    ValidateRelationshipScenePaths(ps, prop_name,
                                   "core.schema.CollectionAPI.relationship",
                                   location, result);

    if (member == "excludes") {
      for (const Path &target : RelTargets(ps, prop_name)) {
        std::string prop_part;
        SplitScenePathString(target.str(), nullptr, &prop_part);
        if (!prop_part.empty() && StartsWith(prop_part, "collection:")) {
          AddError(result, "core.schema.CollectionAPI.excludes", location,
                   "excludes must not target a property referring to another "
                   "collection");
        }
      }
    }
    return;
  }

  AddError(result, "core.schema.CollectionAPI.property", location,
           "unsupported CollectionAPI property `" + member + "`");
}

// ---------------------------------------------------------------------------
// Layer metadata
// ---------------------------------------------------------------------------

void ValidateLayerMetas(const Layer &layer, USDValidationResult *result) {
  const LayerMeta &metas = layer.meta();

  if (!metas.defaultPrim.empty()) {
    const std::string &default_prim = metas.defaultPrim;
    if (!IsValidIdentifier(default_prim)) {
      AddError(result, "core.layer.defaultPrim", kLayerLocation,
               "defaultPrim `" + default_prim +
                   "` is not a valid root prim identifier");
    }
    bool found = false;
    for (uint32_t root_index : layer.root_indices()) {
      const PrimSpec *root = layer.prim(root_index);
      if (root && root->name() == default_prim) {
        found = true;
        break;
      }
    }
    if (!found) {
      AddError(result, "core.layer.defaultPrim", kLayerLocation,
               "defaultPrim `" + default_prim +
                   "` does not match any root PrimSpec in the layer");
    }
  }

  // NOTE: layerRelocates are stored per-prim in next-core; see
  // ValidateRelocates() in the prim walk.

  // upAxis: the next parser stores the raw token; guard against non-X/Y/Z.
  if (metas.upAxis_set && metas.upAxis != "X" && metas.upAxis != "Y" &&
      metas.upAxis != "Z") {
    AddWarning(result, "core.layer.upAxis", kLayerLocation,
               "upAxis is authored but is not one of X, Y, or Z");
  }

  if (metas.metersPerUnit_set && !(metas.metersPerUnit > 0.0)) {
    AddError(result, "core.layer.metersPerUnit", kLayerLocation,
             "metersPerUnit must be greater than 0");
  }

  if (metas.timeCodesPerSecond_set && !(metas.timeCodesPerSecond > 0.0)) {
    AddError(result, "core.layer.timeCodesPerSecond", kLayerLocation,
             "timeCodesPerSecond must be greater than 0");
  }

  if (metas.framesPerSecond_set && !(metas.framesPerSecond > 0.0)) {
    AddError(result, "core.layer.framesPerSecond", kLayerLocation,
             "framesPerSecond must be greater than 0");
  }

  if (metas.kilogramsPerUnit_set && !(metas.kilogramsPerUnit > 0.0)) {
    AddError(result, "core.layer.kilogramsPerUnit", kLayerLocation,
             "kilogramsPerUnit must be greater than 0");
  }

  if (metas.startTimeCode_set && metas.endTimeCode_set &&
      metas.startTimeCode > metas.endTimeCode) {
    AddWarning(result, "core.layer.timeCodeRange", kLayerLocation,
               "startTimeCode is greater than endTimeCode");
  }

  for (size_t i = 0; i < metas.subLayers.size(); i++) {
    const std::string location =
        std::string(kLayerLocation) + ".subLayers[" + std::to_string(i) + "]";
    if (metas.subLayers[i].empty()) {
      AddError(result, "core.layer.subLayers", location,
               "subLayer asset path must not be empty");
    }
    if (i < metas.subLayerOffsets.size() &&
        metas.subLayerOffsets[i].second == 0.0) {
      AddError(result, "core.layer.subLayerOffset", location,
               "composition layerOffset scale must not be 0");
    }
  }

  // NOTE: core.layer.colorConfiguration / core.layer.colorManagementSystem /
  // core.layer.owner are unrepresentable in next-core (plain string storage
  // cannot distinguish authored-empty from unauthored; no owner field).

  ValidateDictValue(metas.customLayerData, "core.layer.customLayerData",
                    "<layer>.customLayerData", result);
  ValidateDictValue(metas.expressionVariables,
                    "core.layer.expressionVariables",
                    "<layer>.expressionVariables", result);
}

// ---------------------------------------------------------------------------
// Prim index (path -> type / specifier / skeleton joint count)
// ---------------------------------------------------------------------------

void BuildPrimIndexRecursive(const Layer &layer, uint32_t prim_index,
                             const std::string &prim_path,
                             PrimTypeByPath *prim_types,
                             PrimSpecifierByPath *prim_specifiers,
                             SkeletonJointCountByPath *skeleton_joints,
                             std::unordered_set<uint32_t> *visited) {
  const PrimSpec *ps = layer.prim(prim_index);
  if (!ps || !prim_types || !prim_specifiers || !visited ||
      !visited->insert(prim_index).second) {
    return;
  }

  (*prim_types)[prim_path] = ps->type_name();
  (*prim_specifiers)[prim_path] = ps->specifier();
  if (skeleton_joints && ps->type_name() == "Skeleton") {
    std::vector<std::string> joints;
    if (GetTokenArrayProperty(*ps, "joints", &joints)) {
      (*skeleton_joints)[prim_path] = joints.size();
    }
  }

  for (uint32_t child_index : ps->child_indices()) {
    const PrimSpec *child = layer.prim(child_index);
    if (!child) {
      continue;
    }
    BuildPrimIndexRecursive(layer, child_index,
                            prim_path + "/" + child->name(), prim_types,
                            prim_specifiers, skeleton_joints, visited);
  }
}

// ---------------------------------------------------------------------------
// Recursive prim validation
// ---------------------------------------------------------------------------

void ValidatePrimSpecRecursive(const Layer &layer, uint32_t prim_index,
                               const std::string &prim_location,
                               const ColorSpaceSet &inherited_color_spaces,
                               const AncestorContext &ancestors,
                               const ValidationOptions &options,
                               const PrimTypeByPath &prim_types,
                               const PrimSpecifierByPath &prim_specifiers,
                               const SkeletonJointCountByPath &skeleton_joints,
                               std::unordered_set<uint32_t> *visited,
                               USDValidationResult *result) {
  const PrimSpec *psp = layer.prim(prim_index);
  if (!psp || !visited || !visited->insert(prim_index).second) {
    return;
  }
  const PrimSpec &ps = *psp;
  const std::vector<AppliedSchema> applied_schemas = CollectAppliedSchemas(ps);
  const std::string &type_name = ps.type_name();
  const bool has_arc = HasCompositionArc(ps);
  const bool is_over = IsOverride(ps);

  // ---- core.prim.name: prim name must be a valid USD identifier ----
  if (options.core && !ps.name().empty() && !IsValidIdentifier(ps.name())) {
    AddError(result, "core.prim.name", prim_location,
             "prim name `" + ps.name() + "` is not a valid identifier");
  }

  // ---- geom.encapsulation.nestedGprim ----
  if (options.geom && IsGprimTypeName(type_name) &&
      ancestors.has_gprim_ancestor) {
    AddWarning(result, "geom.encapsulation.nestedGprim", prim_location,
               "Gprim `" + type_name + "` is nested under another Gprim at `" +
                   ancestors.nearest_gprim_path + "`");
  }

  size_t local_mesh_face_count = 0;
  bool local_mesh_face_count_known = false;
  if (options.geom && !has_arc && !is_over) {
    ValidateGeomCommonProperties(ps, prim_location, result);
    ValidateGeomPrimitive(ps, prim_location, result);
    if (type_name == "Mesh") {
      ValidateMeshTopology(ps, prim_location, result, &local_mesh_face_count);
      std::vector<int32_t> face_counts;
      local_mesh_face_count_known =
          GetIntArrayProperty(ps, "faceVertexCounts", &face_counts);
      if (local_mesh_face_count_known) {
        local_mesh_face_count = face_counts.size();
      }
    } else if (type_name == "GeomSubset") {
      ValidateGeomSubsetTopology(ps, prim_location, ancestors, result);
    } else if (type_name == "Points") {
      ValidateGeomPoints(ps, prim_location, result);
    } else if (type_name == "BasisCurves" || type_name == "NurbsCurves" ||
               type_name == "HermiteCurves" || type_name == "NurbsPatch") {
      ValidateGeomCurves(ps, prim_location, result);
    } else if (type_name == "PointInstancer") {
      ValidatePointInstancer(ps, prim_location, result);
    } else if (type_name == "Camera") {
      ValidateCamera(ps, prim_location, result);
    }
    ValidatePrimvars(ps, prim_location, result);
    if (type_name == "Mesh") {
      ValidateSkinningPrimvars(ps, prim_location, prim_types, skeleton_joints,
                               result);
    } else if (type_name == "Skeleton") {
      ValidateSkeletonTopology(ps, prim_location, result, nullptr);
    } else if (type_name == "SkelAnimation") {
      ValidateSkelAnimation(ps, prim_location, result);
    } else if (type_name == "BlendShape") {
      ValidateBlendShape(ps, prim_location, result);
    }
  }

  if (options.lux && !has_arc && !is_over && IsLuxLightTypeName(type_name)) {
    ValidateLuxLight(ps, prim_location, result);
  }

  if (options.physics && !has_arc && !is_over) {
    ValidatePhysics(ps, applied_schemas, prim_location, prim_types, result);
  }

  if (options.geom && !has_arc && !is_over) {
    ValidateSkelBinding(ps, prim_location, prim_types, result);
  }

  if (options.core) {
    ValidateCompositionMetadata(ps, prim_location, prim_specifiers, result);
    ValidatePrimMetadata(ps, prim_location, applied_schemas, result);
    ValidateClipsMetadata(ps, prim_location, result);
  }

  // ---- shade.encapsulation.shaderParent ----
  if (options.shade && IsShadeConnectableTypeName(type_name) && !has_arc &&
      !is_over && !ancestors.parent_type.empty() &&
      !IsShadeContainerTypeName(ancestors.parent_type)) {
    AddWarning(result, "shade.encapsulation.shaderParent", prim_location,
               type_name + " must be parented by a Material or NodeGraph, but "
               "its parent is `" + ancestors.parent_type + "`");
  }

  const std::string shader_id = (options.shade && type_name == "Shader")
                                    ? GetShaderInfoId(ps)
                                    : std::string();

  // ---- shade.preview.* / shade.uvTexture.* / shade.primvarReader.* ----
  if (options.shade && shader_id == "UsdPreviewSurface") {
    ValidateUsdPreviewSurface(ps, prim_location, result);
  } else if (options.shade && shader_id == "UsdUVTexture") {
    ValidateUsdUVTexture(ps, prim_location, result);
  } else if (options.shade && IsUsdPrimvarReaderId(shader_id)) {
    ValidateUsdPrimvarReader(ps, shader_id, prim_location, result);
  }
  if (options.shade) {
    ValidateMaterialXConfig(ps, applied_schemas, prim_location, result);
    if (type_name == "Shader") {
      ValidateMaterialXShader(ps, shader_id, prim_location, result);
    }
    ValidateMaterialXReferenceConventions(ps, prim_location, result);
  }

  std::set<std::string> collection_instances;
  if (options.core) {
    collection_instances = CollectAppliedCollectionInstances(applied_schemas);
  }

  if (options.core) {
    for (const auto &schema : applied_schemas) {
      if ((schema.name == "ColorSpaceAPI" ||
           schema.name == "ColorSpaceDefinitionAPI") &&
          !schema.instance_name.empty()) {
        AddError(result, "core.schema.singleApply.instance", prim_location,
                 schema.name + " must not be authored with an instance name");
      }
    }
  }

  ColorSpaceSet visible_color_spaces = inherited_color_spaces;
  if (options.core) {
    const ColorSpaceSet local_defs =
        CollectLocalColorSpaceDefinitions(ps, applied_schemas);
    visible_color_spaces.insert(local_defs.begin(), local_defs.end());
  }

  const bool has_color_space_api =
      HasAppliedSchema(applied_schemas, "ColorSpaceAPI");
  const bool has_color_space_definition_api =
      HasAppliedSchema(applied_schemas, "ColorSpaceDefinitionAPI");

  // Per-property checks (attribute slots + relationships).
  for (const std::string &prop_name : AllPropertyNames(ps)) {
    const bool is_rel = IsRelationshipProp(ps, prop_name);
    const std::string prop_location =
        MakePropertyLocation(prim_location, prop_name);

    if (options.core && !is_rel) {
      ValidateAttributeMetadata(ps, prop_name, prop_location, result);
      ValidateTimeSamplesProp(ps, prop_name, "core.attr.timeSamples",
                              prop_location, result);
    } else if (options.core && is_rel) {
      ValidateRelationshipMetadata(ps, prop_name, prop_location, result);
    }

    if (options.core && prop_name == "colorSpace:name") {
      if (!has_color_space_api) {
        AddError(result, "core.schema.ColorSpaceAPI.applied", prop_location,
                 "`colorSpace:name` is authored without applying "
                 "ColorSpaceAPI");
      }
      ValidateTokenAttributeType(ps, prop_name,
                                 "core.schema.ColorSpaceAPI.name",
                                 prop_location, result);
      ValidateUniformAttribute(ps, prop_name, "core.schema.ColorSpaceAPI.name",
                               prop_location, result);
      std::string token;
      if (GetTokenProperty(ps, prop_name, &token)) {
        ValidateColorSpaceToken(token, visible_color_spaces,
                                "core.schema.ColorSpaceAPI.name",
                                prop_location, result);
      }
    } else if (options.core && StartsWith(prop_name, "collection:")) {
      ValidateCollectionProperty(ps, SplitString(prop_name, ':'), prop_name,
                                 collection_instances, prop_location, result);
    } else if (options.core &&
               StartsWith(prop_name, "colorSpaceDefinition:")) {
      if (!has_color_space_definition_api) {
        AddError(result, "core.schema.ColorSpaceDefinitionAPI.applied",
                 prop_location,
                 "ColorSpaceDefinitionAPI property is authored without "
                 "applying ColorSpaceDefinitionAPI");
      }
      ValidateColorSpaceDefinitionProperty(ps, prop_name, prop_location,
                                           result);
    } else if (options.core && has_color_space_definition_api &&
               IsColorSpaceDefinitionPropertyName(prop_name)) {
      ValidateColorSpaceDefinitionProperty(ps, prop_name, prop_location,
                                           result);
    }

    if (options.core && !is_rel) {
      const PropMeta *meta = ps.property_meta(prop_name);
      if (meta && (meta->authored & PropMeta::kColorSpace)) {
        ValidateColorSpaceToken(meta->colorSpace, visible_color_spaces,
                                "core.attr.colorSpace", prop_location, result);
      }
    }

    // ---- shade.connection.target ----
    if (options.shade && !has_arc && !is_over && !is_rel &&
        HasConnections(ps, prop_name) &&
        (type_name == "Material" || type_name == "Shader" ||
         type_name == "NodeGraph") &&
        (IsShaderInputName(prop_name) || IsShaderOutputName(prop_name))) {
      const bool require_output_target =
          (type_name == "Material" && IsMaterialTerminalOutputName(prop_name));
      ValidateConnectionTargets(ps, prop_name, "shade.connection.target",
                                prop_location, prim_types, result,
                                require_output_target);
    }

    // ---- shade.material.outputConnection ----
    if (options.shade && !has_arc && !is_over && type_name == "Material" &&
        IsMaterialTerminalOutputName(prop_name)) {
      if (is_rel) {
        AddError(result, "shade.material.outputConnection", prop_location,
                 "`" + prop_name + "` must be a connection attribute");
      } else if (!HasConnections(ps, prop_name)) {
        AddError(result, "shade.material.outputConnection", prop_location,
                 "`" + prop_name +
                     "` must connect to a shader or node graph output");
      }
    }

    // ---- shade.material.binding ----
    if (options.shade && (prop_name == "material:binding" ||
                          StartsWith(prop_name, "material:binding:"))) {
      if (!is_rel) {
        // Pure local syntax error -- always reported.
        AddError(result, "shade.material.binding", prop_location,
                 "`" + prop_name + "` must be a relationship");
      } else if (!has_arc && !is_over &&
                 !HasAppliedSchema(applied_schemas, "MaterialBindingAPI")) {
        AddWarning(result, "shade.material.bindingAPI", prop_location,
                   "`" + prop_name +
                       "` is authored without applying MaterialBindingAPI");
      }
    }
  }

  // ---- core.xformOp.order ----
  if (options.core && !has_arc && !is_over) {
    std::vector<std::string> order;
    if (GetTokenArrayProperty(ps, "xformOpOrder", &order)) {
      for (const std::string &op_name : order) {
        if (op_name == "!resetXformStack!") {
          continue;
        }
        if (!HasProperty(ps, op_name)) {
          AddError(result, "core.xformOp.order",
                   MakePropertyLocation(prim_location, "xformOpOrder"),
                   "xformOpOrder references `" + op_name +
                       "` but no such attribute is authored on the prim");
        }
      }
    }
  }

  // Propagate ancestor context to children.
  AncestorContext child_ctx;
  child_ctx.parent_type = type_name;
  child_ctx.parent_mesh_face_count_known = local_mesh_face_count_known;
  child_ctx.parent_mesh_face_count = local_mesh_face_count;
  if (IsGprimTypeName(type_name)) {
    child_ctx.has_gprim_ancestor = true;
    child_ctx.nearest_gprim_path = prim_location;
  } else {
    child_ctx.has_gprim_ancestor = ancestors.has_gprim_ancestor;
    child_ctx.nearest_gprim_path = ancestors.nearest_gprim_path;
  }

  for (uint32_t child_index : ps.child_indices()) {
    const PrimSpec *child = layer.prim(child_index);
    if (!child) {
      continue;
    }
    ValidatePrimSpecRecursive(layer, child_index,
                              prim_location + "/" + child->name(),
                              visible_color_spaces, child_ctx, options,
                              prim_types, prim_specifiers, skeleton_joints,
                              visited, result);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

size_t USDValidationResult::error_count() const {
  size_t count = 0;
  for (const auto &issue : issues) {
    if (issue.severity == USDValidationSeverity::Error) {
      count++;
    }
  }
  return count;
}

size_t USDValidationResult::warning_count() const {
  size_t count = 0;
  for (const auto &issue : issues) {
    if (issue.severity == USDValidationSeverity::Warning) {
      count++;
    }
  }
  return count;
}

bool USDValidationResult::ok() const { return error_count() == 0; }

const char *GetAOUSDCoreSpecVersionString() { return kSpecVersion; }

ValidationOptions MakeValidateAllOptions() {
  ValidationOptions options;
  options.core = true;
  options.geom = true;
  options.shade = true;
  options.lux = true;
  options.physics = true;
  options.crate = true;
  return options;
}

std::vector<std::string> GetValidationGroupNames(
    const ValidationOptions &options) {
  std::vector<std::string> groups;
  if (options.core) {
    groups.emplace_back("core");
  }
  if (options.geom) {
    groups.emplace_back("geom");
  }
  if (options.shade) {
    groups.emplace_back("shade");
  }
  if (options.lux) {
    groups.emplace_back("lux");
  }
  if (options.physics) {
    groups.emplace_back("physics");
  }
  if (options.crate) {
    groups.emplace_back("crate");
  }
  return groups;
}

std::vector<const USDValidationIssue *> GetOrderedValidationIssues(
    const USDValidationResult &result) {
  std::vector<const USDValidationIssue *> ordered;
  ordered.reserve(result.issues.size());
  for (const auto &issue : result.issues) {
    ordered.push_back(&issue);
  }
  std::stable_sort(
      ordered.begin(), ordered.end(),
      [](const USDValidationIssue *a, const USDValidationIssue *b) {
        if (a->severity != b->severity) {
          return static_cast<int>(a->severity) < static_cast<int>(b->severity);
        }
        const bool a_layer = (a->location == kLayerLocation);
        const bool b_layer = (b->location == kLayerLocation);
        if (a_layer != b_layer) {
          return a_layer;
        }
        if (a->location != b->location) {
          return a->location < b->location;
        }
        return a->rule_id < b->rule_id;
      });
  return ordered;
}

void MergeValidationResults(USDValidationResult *dst,
                            const USDValidationResult &src) {
  if (!dst) {
    return;
  }
  dst->issues.insert(dst->issues.end(), src.issues.begin(), src.issues.end());
  dst->checked_groups.core =
      dst->checked_groups.core || src.checked_groups.core;
  dst->checked_groups.geom =
      dst->checked_groups.geom || src.checked_groups.geom;
  dst->checked_groups.shade =
      dst->checked_groups.shade || src.checked_groups.shade;
  dst->checked_groups.lux = dst->checked_groups.lux || src.checked_groups.lux;
  dst->checked_groups.physics =
      dst->checked_groups.physics || src.checked_groups.physics;
  dst->checked_groups.crate =
      dst->checked_groups.crate || src.checked_groups.crate;
}

USDValidationResult ValidateLayerAgainstAOUSDCore(const Layer &layer) {
  return ValidateLayerAgainstAOUSDCore(layer, ValidationOptions());
}

USDValidationResult ValidateLayerAgainstAOUSDCore(
    const Layer &layer, const ValidationOptions &options) {
  USDValidationResult result;
  result.checked_groups = options;
  // Crate container introspection is not implemented on the next crate
  // reader; the crate group never runs.
  result.checked_groups.crate = false;

  if (options.core) {
    ValidateLayerMetas(layer, &result);
  }

  PrimTypeByPath prim_types;
  PrimSpecifierByPath prim_specifiers;
  SkeletonJointCountByPath skeleton_joints;
  {
    std::unordered_set<uint32_t> visited;
    for (uint32_t root_index : layer.root_indices()) {
      const PrimSpec *root = layer.prim(root_index);
      if (!root) {
        continue;
      }
      BuildPrimIndexRecursive(layer, root_index, "/" + root->name(),
                              &prim_types, &prim_specifiers, &skeleton_joints,
                              &visited);
    }
  }

  std::unordered_set<uint32_t> visited;
  for (uint32_t root_index : layer.root_indices()) {
    const PrimSpec *root = layer.prim(root_index);
    if (!root) {
      continue;
    }
    ValidatePrimSpecRecursive(layer, root_index, "/" + root->name(),
                              ColorSpaceSet(), AncestorContext(), options,
                              prim_types, prim_specifiers, skeleton_joints,
                              &visited, &result);
  }

  return result;
}

bool ValidateUSDFromMemoryAgainstAOUSDCore(
    const uint8_t *data, size_t size, const std::string &filename,
    const ValidationOptions &options, USDValidationResult *result,
    std::string *warn, std::string *err) {
  if (!result) {
    if (err) {
      (*err) += "null pointer for `result` argument.\n";
    }
    return false;
  }

  std::shared_ptr<Layer> layer =
      pcp::LoadLayerFromMemory(filename, data, size, warn, err,
                               pcp::LayerLoadOptions());
  if (!layer) {
    if (err && err->empty()) {
      (*err) += "Failed to parse USD data.\n";
    }
    return false;
  }

  *result = ValidateLayerAgainstAOUSDCore(*layer, options);
  return true;
}

namespace {

std::string Pluralize(size_t n, const char *noun) {
  std::ostringstream ss;
  ss << n << " " << noun;
  if (n != 1) {
    ss << "s";
  }
  return ss.str();
}

}  // namespace

std::string FormatValidationResult(const USDValidationResult &result) {
  std::ostringstream ss;
  ss << "AOUSD validation report (" << GetAOUSDCoreSpecVersionString()
     << ")\n";
  ss << "Checked rule groups: " << result.checked_groups.group_summary()
     << "\n";

  const std::vector<const USDValidationIssue *> ordered =
      GetOrderedValidationIssues(result);

  if (!ordered.empty()) {
    ss << "\n";
    for (const USDValidationIssue *issue : ordered) {
      ss << (issue->severity == USDValidationSeverity::Error ? "ERROR"
                                                             : "WARN ")
         << " [" << issue->rule_id << "] ";
      if (!issue->location.empty()) {
        ss << issue->location << ": ";
      }
      ss << issue->message << "\n";
    }
  }

  const size_t errors = result.error_count();
  const size_t warnings = result.warning_count();

  ss << "\nResult: ";
  if (errors > 0) {
    ss << "FAILED - " << Pluralize(errors, "error") << ", "
       << Pluralize(warnings, "warning") << "\n";
  } else if (warnings > 0) {
    ss << "PASSED with warnings - " << Pluralize(warnings, "warning") << "\n";
  } else {
    ss << "PASSED - no issues found\n";
  }

  return ss.str();
}

std::string ValidationOptions::group_summary() const {
  std::string s;
  const auto add = [&](bool enabled, const char *name) {
    if (!enabled) {
      return;
    }
    if (!s.empty()) {
      s += ", ";
    }
    s += name;
  };
  add(core, "core");
  add(geom, "geom");
  add(shade, "shade");
  add(lux, "lux");
  add(physics, "physics");
  add(crate, "crate");
  if (s.empty()) {
    s = "(none)";
  }
  return s;
}

}  // namespace next
}  // namespace tinyusdz
