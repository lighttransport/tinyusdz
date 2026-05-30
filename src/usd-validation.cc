// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 Light Transport Entertainment Inc.

#include "usd-validation.hh"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "core/composition-types.hh"
#include "core/layer-types.hh"
#include "core/path.hh"
#include "core/prim-enums.hh"
#include "core/prim-spec.hh"
#include "path-util.hh"
#include "pprint-enum.hh"  // to_string(APISchemas::APIName)
#include "str-util.hh"

namespace tinyusdz {
namespace {

constexpr const char *kLayerLocation = "<layer>";
constexpr const char *kSpecVersion = "AOUSD Core Spec v1.0.1 (2025-12-12)";

struct AppliedSchema {
  std::string name;
  std::string instance_name;
};

using ColorSpaceSet = std::set<std::string>;

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

// Type-name sets for geom/shade encapsulation checks.
//
// Hardcoded here (rather than including the heavy usdGeom.hh/usdShade.hh) to
// keep this TU lightweight. Keep in sync with the kGeom*/kGPrim constants in
// src/usdGeom.hh and the kMaterial/kShader/kNodeGraph constants in
// src/usdShade.hh.
bool IsGprimTypeName(const std::string &type_name) {
  static const std::unordered_set<std::string> kGprimTypes = {
      "GPrim",       "Mesh",        "Sphere",       "Cube",
      "Cone",        "Cylinder",    "Cylinder_1",   "Capsule",
      "Capsule_1",   "Points",      "BasisCurves",  "NurbsCurves",
      "NurbsPatch",  "HermiteCurves", "Plane",      "TetMesh",
      "PointInstancer",
  };
  // NOTE: Xform/Scope/Camera/GeomSubset and lights are intentionally excluded
  // -- they legitimately contain or sit beside geometry.
  return kGprimTypes.count(type_name) > 0;
}

bool IsShadeContainerTypeName(const std::string &type_name) {
  // Containers that may legitimately parent a Shader/NodeGraph.
  return type_name == "Material" || type_name == "NodeGraph";
}

bool IsShadeConnectableTypeName(const std::string &type_name) {
  return type_name == "Shader" || type_name == "NodeGraph";
}

// UsdPreviewSurface input name -> expected (role) value type.
//
// The table entries are generated from src/usd-preview-surface-schema.json by
// scripts/gen-usd-preview-surface-inputs.py (the same schema drives the
// validation fixtures). Regenerate after editing the schema; the
// `preview-surface-schema-sync` CTest fails if usd-preview-surface-inputs.inc
// is stale.
const std::unordered_map<std::string, std::string> &
UsdPreviewSurfaceInputTypes() {
  static const std::unordered_map<std::string, std::string> kInputs = {
#include "usd-preview-surface-inputs.inc"
  };
  return kInputs;
}

// Underlying base type for role-type agreement (Spec 6.5): color3f agrees with
// float3, etc. Used so an input authored as float3 is accepted where color3f is
// expected.
std::string BaseValueType(const std::string &type_name) {
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

// True if `ps` authors any composition arc. Used to gate locality-dependent
// rules: when a prim pulls opinions from a referenced/inherited/etc. layer, an
// attribute or parent type referenced here may be supplied by composition and
// is not a local error.
bool HasCompositionArc(const PrimSpec &ps) {
  const auto &m = ps.metas();
  return m.references.has_value() || m.payload.has_value() ||
         m.inherits.has_value() || m.specializes.has_value() ||
         m.variants.has_value() || m.variantSets.has_value();
}

bool IsOverride(const PrimSpec &ps) {
  return ps.specifier() == Specifier::Over;
}

// Ancestor-derived context threaded through the recursive prim walk.
struct AncestorContext {
  bool has_gprim_ancestor{false};
  std::string nearest_gprim_path;  // valid only when has_gprim_ancestor
  std::string parent_type;         // immediate parent typeName ("" at root)
};

// Returns the `info:id` token authored on a Shader prim, or empty.
std::string GetShaderInfoId(const PrimSpec &ps) {
  auto it = ps.props().find("info:id");
  if (it == ps.props().end() || !it->second.is_attribute()) {
    return std::string();
  }
  auto info_id = it->second.get_attribute().get_value<value::token>();
  return info_id ? info_id->str() : std::string();
}

// Validate a UsdPreviewSurface Shader's `inputs:*` against its schema: unknown
// inputs and input type mismatches. Reported as warnings -- a UsdPreviewSurface
// with off-schema inputs still loads, but the content is non-conformant.
void ValidateUsdPreviewSurface(const PrimSpec &ps,
                               const std::string &prim_location,
                               USDValidationResult *result) {
  static const std::string kInputsPrefix = "inputs:";
  const auto &inputs = UsdPreviewSurfaceInputTypes();

  for (const auto &prop_entry : ps.props()) {
    const std::string &prop_name = prop_entry.first;
    if (prop_name.rfind(kInputsPrefix, 0) != 0) {
      continue;
    }

    const Property &prop = prop_entry.second;
    if (!prop.is_attribute()) {
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

    const std::string authored = prop.value_type_name();
    if (!authored.empty() && !ValueTypesAgree(authored, it->second)) {
      AddWarning(result, "shade.preview.inputType", prop_location,
                 "UsdPreviewSurface input `" + input_name + "` should be `" +
                     it->second + "` but is `" + authored + "`");
    }
  }
}

bool ValidateScenePath(const Path &path, std::string *err) {
  return pathutil::ValidatePath(path, err);
}

bool ValidatePrimPath(const Path &path, std::string *err) {
  return pathutil::ValidatePrimPath(path, err);
}

bool IsAbsolutePrimPath(const Path &path) {
  return path.is_valid() && path.is_prim_path() && path.is_absolute_path();
}

bool IsEmptyPath(const Path &path) {
  return path.prim_part().empty() && path.prop_part().empty();
}

bool IsCanonicalColorSpaceToken(const std::string &token) {
  static const std::unordered_set<std::string> kCanonicalTokens = {
      "lin_ap1_scene",
      "lin_ap0_scene",
      "lin_rec709_scene",
      "lin_p3d65_scene",
      "lin_rec2020_scene",
      "lin_adobergb_scene",
      "lin_ciexyzd65_scene",
      "srgb_rec709_scene",
      "g22_rec709_scene",
      "g18_rec709_scene",
      "srgb_ap1_scene",
      "g22_ap1_scene",
      "srgb_p3d65_scene",
      "g22_adobergb_scene",
      "data",
      "unknown",
      "raw",
      "identity",
  };

  return kCanonicalTokens.count(token) > 0;
}

bool IsColorSpaceDefinitionPropertyName(const std::string &prop_name) {
  return prop_name == "name" || prop_name == "redChroma" ||
         prop_name == "greenChroma" || prop_name == "blueChroma" ||
         prop_name == "whitePoint" || prop_name == "gamma" ||
         prop_name == "linearBias" || prop_name == "colorSpaceDefinition:name" ||
         prop_name == "colorSpaceDefinition:redChroma" ||
         prop_name == "colorSpaceDefinition:greenChroma" ||
         prop_name == "colorSpaceDefinition:blueChroma" ||
         prop_name == "colorSpaceDefinition:whitePoint" ||
         prop_name == "colorSpaceDefinition:gamma" ||
         prop_name == "colorSpaceDefinition:linearBias";
}

std::vector<AppliedSchema> CollectAppliedSchemas(const PrimMeta &metas) {
  std::vector<AppliedSchema> schemas;
  if (!metas.has_apiSchemas()) {
    return schemas;
  }

  const APISchemas api_schemas = metas.get_apiSchemas();
  for (const auto &entry : api_schemas.names) {
    // Emit the canonical schema name for every known applied schema so callers
    // (e.g. MaterialBindingAPI / CollectionAPI checks) can look it up by name.
    schemas.push_back({to_string(entry.first), entry.second});
  }

  for (const auto &entry : api_schemas.unknownSchemas) {
    std::string schema_name = entry.first;
    std::string instance_name = entry.second;

    const size_t colon_pos = schema_name.find(':');
    if (colon_pos != std::string::npos) {
      if (instance_name.empty()) {
        instance_name = schema_name.substr(colon_pos + 1);
      }
      schema_name = schema_name.substr(0, colon_pos);
    }

    schemas.push_back({schema_name, instance_name});
  }

  return schemas;
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

void ValidateTokenAttributeType(const Property &prop, const std::string &rule_id,
                                const std::string &location,
                                USDValidationResult *result) {
  if (!prop.is_attribute()) {
    AddError(result, rule_id, location,
             "expected an attribute, but found a relationship");
    return;
  }

  if (prop.value_type_name() != "token") {
    AddError(result, rule_id, location,
             "expected attribute type `token`, but found `" +
                 prop.value_type_name() + "`");
  }
}

void ValidateFloat2AttributeType(const Property &prop, const std::string &rule_id,
                                 const std::string &location,
                                 USDValidationResult *result) {
  if (!prop.is_attribute()) {
    AddError(result, rule_id, location,
             "expected an attribute, but found a relationship");
    return;
  }

  if (prop.value_type_name() != "float2") {
    AddError(result, rule_id, location,
             "expected attribute type `float2`, but found `" +
                 prop.value_type_name() + "`");
  }
}

void ValidateFloatAttributeType(const Property &prop, const std::string &rule_id,
                                const std::string &location,
                                USDValidationResult *result) {
  if (!prop.is_attribute()) {
    AddError(result, rule_id, location,
             "expected an attribute, but found a relationship");
    return;
  }

  if (prop.value_type_name() != "float") {
    AddError(result, rule_id, location,
             "expected attribute type `float`, but found `" +
                 prop.value_type_name() + "`");
  }
}

void ValidateBoolAttributeType(const Property &prop, const std::string &rule_id,
                               const std::string &location,
                               USDValidationResult *result) {
  if (!prop.is_attribute()) {
    AddError(result, rule_id, location,
             "expected an attribute, but found a relationship");
    return;
  }

  if (prop.value_type_name() != "bool") {
    AddError(result, rule_id, location,
             "expected attribute type `bool`, but found `" +
                 prop.value_type_name() + "`");
  }
}

void ValidateUniformAttribute(const Property &prop, const std::string &rule_id,
                              const std::string &location,
                              USDValidationResult *result) {
  if (!prop.is_attribute()) {
    return;
  }

  const Attribute &attr = prop.get_attribute();
  if (attr.variability() != Variability::Uniform) {
    AddError(result, rule_id, location,
             "expected `uniform` variability");
  }
}

void ValidateVaryingAttribute(const Property &prop, const std::string &rule_id,
                              const std::string &location,
                              USDValidationResult *result) {
  if (!prop.is_attribute()) {
    return;
  }

  const Attribute &attr = prop.get_attribute();
  if (attr.variability() != Variability::Varying) {
    AddError(result, rule_id, location,
             "expected `varying` variability");
  }
}

void ValidateRelationshipTargets(const Relationship &relationship,
                                 const std::string &rule_id,
                                 const std::string &location,
                                 USDValidationResult *result) {
  for (const Path &target : Property(relationship).get_relationTargets()) {
    std::string err;
    if (!ValidateScenePath(target, &err)) {
      AddError(result, rule_id, location,
               "contains invalid target path `" + target.full_path_name() +
                   "`: " + err);
    }
  }
}

ColorSpaceSet CollectLocalColorSpaceDefinitions(const PrimSpec &ps) {
  ColorSpaceSet local_defs;

  const auto applied_schemas = CollectAppliedSchemas(ps.metas());
  if (!HasAppliedSchema(applied_schemas, "ColorSpaceDefinitionAPI")) {
    return local_defs;
  }

  local_defs.insert("custom");

  const auto &props = ps.props();
  const std::array<const char *, 2> name_props = {"name",
                                                  "colorSpaceDefinition:name"};
  for (const char *prop_name : name_props) {
    auto it = props.find(prop_name);
    if (it == props.end()) {
      continue;
    }

    if (!it->second.is_attribute()) {
      continue;
    }

    const Attribute &attr = it->second.get_attribute();
    auto value = attr.get_value<value::token>();
    if (value && !value->str().empty()) {
      local_defs.insert(value->str());
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

void ValidateLayerMetas(const Layer &layer, USDValidationResult *result) {
  const LayerMetas &metas = layer.metas();

  if (!metas.defaultPrim.str().empty()) {
    const std::string &default_prim = metas.defaultPrim.str();
    if (layer.primspecs().count(default_prim) == 0) {
      AddError(result, "core.layer.defaultPrim", kLayerLocation,
               "defaultPrim `" + default_prim +
                   "` does not match any root PrimSpec in the layer");
    }
  }

  for (const auto &entry : metas.layerRelocates) {
    const Path &source = entry.first;
    const Path &target = entry.second;

    std::string source_err;
    if (!ValidatePrimPath(source, &source_err) || !IsAbsolutePrimPath(source) ||
        source.is_root_path()) {
      AddError(result, "core.layer.layerRelocates.source", kLayerLocation,
               "layerRelocates source `" + source.full_path_name() +
                   "` must be a valid absolute prim path");
    }

    std::string target_err;
    if ((!IsEmptyPath(target)) &&
        (!ValidatePrimPath(target, &target_err) || !IsAbsolutePrimPath(target))) {
      AddError(result, "core.layer.layerRelocates.target", kLayerLocation,
               "layerRelocates target `" + target.full_path_name() +
                   "` must be empty or a valid absolute prim path");
    }

    if (source.is_valid() && target.is_valid() &&
        source.full_path_name() == target.full_path_name()) {
      AddError(result, "core.layer.layerRelocates.identity", kLayerLocation,
               "layerRelocates source and target must not be identical");
    }
  }

  // upAxis: the parser only ever stores X/Y/Z, so this is defensive against
  // other loaders leaving an Invalid axis.
  if (metas.upAxis.authored() && metas.upAxis.get_value() == Axis::Invalid) {
    AddWarning(result, "core.layer.upAxis", kLayerLocation,
               "upAxis is authored but is not one of X, Y, or Z");
  }

  // metersPerUnit must be a positive scale factor.
  if (metas.metersPerUnit.authored() && !(metas.metersPerUnit.get_value() > 0.0)) {
    AddError(result, "core.layer.metersPerUnit", kLayerLocation,
             "metersPerUnit must be greater than 0");
  }

  if (metas.timeCodesPerSecond.authored() &&
      !(metas.timeCodesPerSecond.get_value() > 0.0)) {
    AddError(result, "core.layer.timeCodesPerSecond", kLayerLocation,
             "timeCodesPerSecond must be greater than 0");
  }

  if (metas.framesPerSecond.authored() &&
      !(metas.framesPerSecond.get_value() > 0.0)) {
    AddError(result, "core.layer.framesPerSecond", kLayerLocation,
             "framesPerSecond must be greater than 0");
  }

  // startTimeCode must not exceed endTimeCode. Warning, since a single layer's
  // range may be overridden by sublayers or the session layer.
  if (metas.startTimeCode.authored() && metas.endTimeCode.authored() &&
      metas.startTimeCode.get_value() > metas.endTimeCode.get_value()) {
    AddWarning(result, "core.layer.timeCodeRange", kLayerLocation,
               "startTimeCode is greater than endTimeCode");
  }
}

void ValidateColorSpaceDefinitionProperty(
    const std::string &prop_name, const Property &prop,
    const std::string &location, USDValidationResult *result) {
  if (prop_name == "name" || prop_name == "colorSpaceDefinition:name") {
    ValidateTokenAttributeType(prop, "core.schema.ColorSpaceDefinitionAPI.name",
                               location, result);
    ValidateUniformAttribute(prop, "core.schema.ColorSpaceDefinitionAPI.name",
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
    ValidateFloat2AttributeType(
        prop, "core.schema.ColorSpaceDefinitionAPI.coordinates", location,
        result);
    ValidateVaryingAttribute(
        prop, "core.schema.ColorSpaceDefinitionAPI.coordinates", location,
        result);
    return;
  }

  if (prop_name == "gamma" || prop_name == "colorSpaceDefinition:gamma" ||
      prop_name == "linearBias" ||
      prop_name == "colorSpaceDefinition:linearBias") {
    ValidateFloatAttributeType(
        prop, "core.schema.ColorSpaceDefinitionAPI.curve", location, result);
    ValidateVaryingAttribute(prop, "core.schema.ColorSpaceDefinitionAPI.curve",
                             location, result);
  }
}

void ValidateCollectionProperty(
    const std::vector<std::string> &segments, const Property &prop,
    const std::set<std::string> &collection_instances,
    const std::string &location, USDValidationResult *result) {
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
             "collection property is authored without applying `CollectionAPI:" +
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
    ValidateTokenAttributeType(prop, "core.schema.CollectionAPI.expansionRule",
                               location, result);
    ValidateUniformAttribute(prop, "core.schema.CollectionAPI.expansionRule",
                             location, result);

    if (prop.is_attribute()) {
      auto token = prop.get_attribute().get_value<value::token>();
      if (token) {
        const std::string &value = token->str();
        if (value != "explicitOnly" && value != "expandPrims" &&
            value != "expandPrimsAndProperties") {
          AddError(result, "core.schema.CollectionAPI.expansionRule", location,
                   "invalid expansionRule `" + value + "`");
        }
      }
    }
    return;
  }

  if (member == "includeRoot") {
    ValidateBoolAttributeType(prop, "core.schema.CollectionAPI.includeRoot",
                              location, result);
    ValidateUniformAttribute(prop, "core.schema.CollectionAPI.includeRoot",
                             location, result);
    return;
  }

  if (member == "includes" || member == "excludes") {
    if (!prop.is_relationship()) {
      AddError(result, "core.schema.CollectionAPI.relationship", location,
               "expected a relationship");
      return;
    }

    ValidateRelationshipTargets(prop.get_relationship(),
                                "core.schema.CollectionAPI.relationship",
                                location, result);

    if (member == "excludes") {
      for (const Path &target : prop.get_relationTargets()) {
        if (target.is_property_path() &&
            target.prop_part().rfind("collection:", 0) == 0) {
          AddError(
              result, "core.schema.CollectionAPI.excludes", location,
              "excludes must not target a property referring to another collection");
        }
      }
    }
    return;
  }

  AddError(result, "core.schema.CollectionAPI.property", location,
           "unsupported CollectionAPI property `" + member + "`");
}

void ValidatePrimSpecRecursive(const PrimSpec &ps, const Path &prim_path,
                               const ColorSpaceSet &inherited_color_spaces,
                               const AncestorContext &ancestors,
                               const ValidationOptions &options,
                               USDValidationResult *result) {
  const std::string prim_location = prim_path.full_path_name();
  const std::vector<AppliedSchema> applied_schemas =
      CollectAppliedSchemas(ps.metas());
  const std::string &type_name = ps.typeName();
  const bool has_arc = HasCompositionArc(ps);
  const bool is_over = IsOverride(ps);

  // ---- core.prim.name: prim name must be a valid USD identifier ----
  if (options.core && !ps.name().empty() &&
      !is_valid_utf8_identifier(ps.name())) {
    AddError(result, "core.prim.name", prim_location,
             "prim name `" + ps.name() + "` is not a valid identifier");
  }

  // ---- geom.encapsulation.nestedGprim: a Gprim must not nest under a Gprim ----
  if (options.geom && IsGprimTypeName(type_name) &&
      ancestors.has_gprim_ancestor) {
    AddWarning(result, "geom.encapsulation.nestedGprim", prim_location,
               "Gprim `" + type_name + "` is nested under another Gprim at `" +
                   ancestors.nearest_gprim_path + "`");
  }

  // ---- shade.encapsulation.shaderParent: Shader/NodeGraph need a container ----
  // Gated: a local typeless `over` or a composed parent may legitimately supply
  // the Material/NodeGraph container.
  if (options.shade && IsShadeConnectableTypeName(type_name) && !has_arc &&
      !is_over && !ancestors.parent_type.empty() &&
      !IsShadeContainerTypeName(ancestors.parent_type)) {
    AddWarning(result, "shade.encapsulation.shaderParent", prim_location,
               type_name + " must be parented by a Material or NodeGraph, but "
               "its parent is `" + ancestors.parent_type + "`");
  }

  // ---- shade.preview.*: UsdPreviewSurface input schema conformance ----
  if (options.shade && type_name == "Shader" &&
      GetShaderInfoId(ps) == "UsdPreviewSurface") {
    ValidateUsdPreviewSurface(ps, prim_location, result);
  }

  std::set<std::string> collection_instances =
      CollectAppliedCollectionInstances(applied_schemas);

  for (const auto &schema : applied_schemas) {
    if (schema.name == "CollectionAPI" && schema.instance_name.empty()) {
      AddError(result, "core.schema.CollectionAPI.instance", prim_location,
               "CollectionAPI is a multiple-apply schema and requires an instance name");
    }

    if ((schema.name == "ColorSpaceAPI" ||
         schema.name == "ColorSpaceDefinitionAPI") &&
        !schema.instance_name.empty()) {
      AddError(result, "core.schema.singleApply.instance", prim_location,
               schema.name + " must not be authored with an instance name");
    }
  }

  ColorSpaceSet visible_color_spaces = inherited_color_spaces;
  const ColorSpaceSet local_defs = CollectLocalColorSpaceDefinitions(ps);
  visible_color_spaces.insert(local_defs.begin(), local_defs.end());

  const bool has_color_space_api =
      HasAppliedSchema(applied_schemas, "ColorSpaceAPI");
  const bool has_color_space_definition_api =
      HasAppliedSchema(applied_schemas, "ColorSpaceDefinitionAPI");

  for (const auto &prop_entry : ps.props()) {
    const std::string &prop_name = prop_entry.first;
    const Property &prop = prop_entry.second;
    const std::string prop_location =
        MakePropertyLocation(prim_location, prop_name);

    if (prop_name == "colorSpace:name") {
      if (!has_color_space_api) {
        AddError(result, "core.schema.ColorSpaceAPI.applied", prop_location,
                 "`colorSpace:name` is authored without applying ColorSpaceAPI");
      }

      ValidateTokenAttributeType(prop, "core.schema.ColorSpaceAPI.name",
                                 prop_location, result);
      ValidateUniformAttribute(prop, "core.schema.ColorSpaceAPI.name",
                               prop_location, result);

      if (prop.is_attribute()) {
        auto token = prop.get_attribute().get_value<value::token>();
        if (token) {
          ValidateColorSpaceToken(token->str(), visible_color_spaces,
                                  "core.schema.ColorSpaceAPI.name",
                                  prop_location, result);
        }
      }
    } else if (prop_name.rfind("collection:", 0) == 0) {
      ValidateCollectionProperty(SplitString(prop_name, ':'), prop,
                                 collection_instances, prop_location, result);
    } else if (prop_name.rfind("colorSpaceDefinition:", 0) == 0) {
      if (!has_color_space_definition_api) {
        AddError(result, "core.schema.ColorSpaceDefinitionAPI.applied",
                 prop_location,
                 "ColorSpaceDefinitionAPI property is authored without "
                 "applying ColorSpaceDefinitionAPI");
      }
      ValidateColorSpaceDefinitionProperty(prop_name, prop, prop_location,
                                           result);
    } else if (has_color_space_definition_api &&
               IsColorSpaceDefinitionPropertyName(prop_name)) {
      ValidateColorSpaceDefinitionProperty(prop_name, prop, prop_location,
                                           result);
    }

    if (prop.is_attribute() && prop.get_attribute().metas().has_colorSpace()) {
      const std::string color_space =
          prop.get_attribute().metas().get_colorSpace().str();
      ValidateColorSpaceToken(color_space, visible_color_spaces,
                              "core.attr.colorSpace", prop_location, result);
    }

    // ---- shade.material.binding: material:binding* must be a relationship ----
    if (options.shade &&
        (prop_name == "material:binding" ||
         prop_name.rfind("material:binding:", 0) == 0)) {
      if (!prop.is_relationship()) {
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

  // ---- core.xformOp.order: every op in xformOpOrder must be authored here ----
  // Gated: an override or composed prim may inherit the op attributes.
  if (options.core && !has_arc && !is_over) {
    auto it = ps.props().find("xformOpOrder");
    if (it != ps.props().end() && it->second.is_attribute()) {
      auto order =
          it->second.get_attribute().get_value<std::vector<value::token>>();
      if (order) {
        for (const value::token &op : order.value()) {
          const std::string &op_name = op.str();
          if (op_name == "!resetXformStack!") {
            continue;
          }
          if (ps.props().count(op_name) == 0) {
            AddError(result, "core.xformOp.order",
                     MakePropertyLocation(prim_location, "xformOpOrder"),
                     "xformOpOrder references `" + op_name +
                         "` but no such attribute is authored on the prim");
          }
        }
      }
    }
  }

  // Propagate ancestor context to children.
  AncestorContext child_ctx;
  child_ctx.parent_type = type_name;
  if (IsGprimTypeName(type_name)) {
    child_ctx.has_gprim_ancestor = true;
    child_ctx.nearest_gprim_path = prim_location;
  } else {
    child_ctx.has_gprim_ancestor = ancestors.has_gprim_ancestor;
    child_ctx.nearest_gprim_path = ancestors.nearest_gprim_path;
  }

  for (const auto &child_entry : ps.children()) {
    Path child_path = prim_path.AppendPrim(child_entry.name());
    ValidatePrimSpecRecursive(child_entry, child_path, visible_color_spaces,
                              child_ctx, options, result);
  }

  for (const auto &variant_set_entry : ps.variantSets()) {
    const std::string &variant_set_name = variant_set_entry.first;
    const VariantSetSpec &variant_set = variant_set_entry.second;

    for (const auto &variant_entry : variant_set.variantSet) {
      const std::string &variant_name = variant_entry.first;
      const PrimSpec &variant_ps = variant_entry.second;

      Path variant_path = prim_path;
      std::string variant_location =
          prim_location + "{" + variant_set_name + "=" + variant_name + "}";
      // Preserve precise child/property paths when walking the variant body.
      if (!variant_ps.name().empty() && variant_ps.name() != ps.name()) {
        variant_path = prim_path.AppendPrim(variant_ps.name());
        variant_location = variant_path.full_path_name() + "{" +
                           variant_set_name + "=" + variant_name + "}";
      }

      (void)variant_location;
      // Variant body shares the owning prim's ancestor context.
      ValidatePrimSpecRecursive(variant_ps, variant_path, visible_color_spaces,
                                child_ctx, options, result);
    }
  }
}

}  // namespace

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

USDValidationResult ValidateLayerAgainstAOUSDCore(const Layer &layer) {
  return ValidateLayerAgainstAOUSDCore(layer, ValidationOptions());
}

USDValidationResult ValidateLayerAgainstAOUSDCore(
    const Layer &layer, const ValidationOptions &options) {
  USDValidationResult result;
  result.checked_groups = options;

  if (options.core) {
    ValidateLayerMetas(layer, &result);
  }

  for (const auto &root_entry : layer.primspecs()) {
    const PrimSpec &ps = root_entry.second;
    Path prim_path("/", "");
    prim_path = prim_path.AppendPrim(ps.name());
    ValidatePrimSpecRecursive(ps, prim_path, ColorSpaceSet(), AncestorContext(),
                              options, &result);
  }

  return result;
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
  ss << "AOUSD validation report (" << GetAOUSDCoreSpecVersionString() << ")\n";
  ss << "Checked rule groups: " << result.checked_groups.group_summary()
     << "\n";

  // Sort a copy so the report is deterministic and scannable: errors before
  // warnings, then grouped by location, then by rule id.
  std::vector<const USDValidationIssue *> ordered;
  ordered.reserve(result.issues.size());
  for (const auto &issue : result.issues) {
    ordered.push_back(&issue);
  }
  std::stable_sort(
      ordered.begin(), ordered.end(),
      [](const USDValidationIssue *a, const USDValidationIssue *b) {
        if (a->severity != b->severity) {
          // Error (0) sorts before Warning (1).
          return static_cast<int>(a->severity) < static_cast<int>(b->severity);
        }
        // Layer-scoped issues are the most general -> show them first.
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

  if (!ordered.empty()) {
    ss << "\n";
    for (const USDValidationIssue *issue : ordered) {
      // Fixed-width severity keeps rule ids aligned.
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
    ss << "PASSED with warnings - " << Pluralize(warnings, "warning")
       << "\n";
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
  if (s.empty()) {
    s = "(none)";
  }
  return s;
}

}  // namespace tinyusdz
