// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Stage to Crate Conversion Implementation
//
// This file contains the implementation for converting TinyUSDZ Stage
// to Crate format specs.
//

#include "crate-writer.hh"
#include "sconv-detail.hh"
#include <iostream>
#include <set>
#include <unordered_set>
#include "layer.hh"
#include "common-macros.inc"
#include "pprinter.hh"  // For to_string(Specifier), to_string(Variability)
#include "pprint-enum.hh"  // For to_string(APISchemas::APIName)
#include "usdShade.hh"  // For Material and Shader
#include "usdPhysics.hh"  // For PhysicsScene, PhysicsJoint, PhysicsCollisionGroup
#include "common-macros.inc"

// Disable specific clang warnings for this file
// - unused-parameter: functions have consistent signatures for API purposes
// - covered-switch-default: default cases provide fallback safety
// - switch-enum: some enum values are intentionally not handled
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

namespace tinyusdz {
namespace experimental {

namespace {

const std::map<std::string, Property> *GetPrimProps(const value::Value &v) {
#define GET_PRIM_PROPS(__ty)        \
  if (v.as<__ty>()) {               \
    return &(v.as<__ty>()->props);  \
  }

  GET_PRIM_PROPS(Model)
  GET_PRIM_PROPS(Scope)
  GET_PRIM_PROPS(Xform)
  GET_PRIM_PROPS(GPrim)
  GET_PRIM_PROPS(GeomMesh)
  GET_PRIM_PROPS(GeomPoints)
  GET_PRIM_PROPS(GeomCube)
  GET_PRIM_PROPS(GeomCapsule)
  GET_PRIM_PROPS(GeomCylinder)
  GET_PRIM_PROPS(GeomSphere)
  GET_PRIM_PROPS(GeomCone)
  GET_PRIM_PROPS(GeomSubset)
  GET_PRIM_PROPS(GeomCamera)
  GET_PRIM_PROPS(GeomBasisCurves)
  GET_PRIM_PROPS(GeomNurbsCurves)
  GET_PRIM_PROPS(GeomHermiteCurves)
  GET_PRIM_PROPS(GeomPointInstancer)
  GET_PRIM_PROPS(GeomPlane)
  GET_PRIM_PROPS(GeomCylinder_1)
  GET_PRIM_PROPS(GeomCapsule_1)
  GET_PRIM_PROPS(GeomTetMesh)
  GET_PRIM_PROPS(GeomNurbsPatch)
  GET_PRIM_PROPS(DomeLight)
  GET_PRIM_PROPS(DomeLight_1)
  GET_PRIM_PROPS(SphereLight)
  GET_PRIM_PROPS(CylinderLight)
  GET_PRIM_PROPS(DiskLight)
  GET_PRIM_PROPS(DistantLight)
  GET_PRIM_PROPS(RectLight)
  GET_PRIM_PROPS(Material)
  // Shader stores generic/unknown shader inputs inside Shader::value
  // (a ShaderNode) rather than Shader::props. Prefer the inner map when
  // the outer one is empty so generic shaders keep their inputs/outputs
  // through USDC roundtrip.
  if (auto *sh = v.as<Shader>()) {
    if (sh->props.empty()) {
      if (auto *node = sh->value.as<ShaderNode>()) {
        return &node->props;
      }
    }
    return &sh->props;
  }
  GET_PRIM_PROPS(NodeGraph)
  GET_PRIM_PROPS(GeometryLight)
  GET_PRIM_PROPS(PortalLight)
  GET_PRIM_PROPS(SkelRoot)
  GET_PRIM_PROPS(Skeleton)
  GET_PRIM_PROPS(SkelAnimation)
  GET_PRIM_PROPS(BlendShape)

  // UsdPhysics
  GET_PRIM_PROPS(PhysicsScene)
  GET_PRIM_PROPS(PhysicsJoint)
  GET_PRIM_PROPS(PhysicsRevoluteJoint)
  GET_PRIM_PROPS(PhysicsPrismaticJoint)
  GET_PRIM_PROPS(PhysicsSphericalJoint)
  GET_PRIM_PROPS(PhysicsFixedJoint)
  GET_PRIM_PROPS(PhysicsDistanceJoint)
  GET_PRIM_PROPS(PhysicsCollisionGroup)
  // UsdSkel — required so authored attributes in the props map (joints,
  // jointNames, blendShapeWeights, etc.) survive the USDC writer when the
  // typed-builtin fields are unauthored.
  GET_PRIM_PROPS(SkelRoot)
  GET_PRIM_PROPS(Skeleton)
  GET_PRIM_PROPS(SkelAnimation)
  GET_PRIM_PROPS(BlendShape)

#undef GET_PRIM_PROPS

  return nullptr;
}

// Returns the Collection mixin pointer for prims that inherit from
// Collection (GPrim and GeomSubset families). Used by the writer to
// re-emit `collection:<instance>:{includes,excludes,includeRoot,
// expansionRule}` properties from the typed Collection storage on
// USDC output. The reconstruct path consumes these from the props
// map into Collection::instances(); without this re-emit, USDC
// round-trip drops them entirely.
const Collection *GetPrimCollection(const value::Value &v) {
#define GET_PRIM_COLLECTION(__ty)              \
  if (auto *p = v.as<__ty>()) {                \
    return static_cast<const Collection *>(p); \
  }

  GET_PRIM_COLLECTION(Xform)
  GET_PRIM_COLLECTION(GPrim)
  GET_PRIM_COLLECTION(GeomMesh)
  GET_PRIM_COLLECTION(GeomPoints)
  GET_PRIM_COLLECTION(GeomCube)
  GET_PRIM_COLLECTION(GeomCapsule)
  GET_PRIM_COLLECTION(GeomCylinder)
  GET_PRIM_COLLECTION(GeomSphere)
  GET_PRIM_COLLECTION(GeomCone)
  GET_PRIM_COLLECTION(GeomSubset)
  GET_PRIM_COLLECTION(GeomBasisCurves)
  GET_PRIM_COLLECTION(GeomNurbsCurves)
  GET_PRIM_COLLECTION(GeomHermiteCurves)
  GET_PRIM_COLLECTION(GeomPointInstancer)
  GET_PRIM_COLLECTION(GeomPlane)
  GET_PRIM_COLLECTION(GeomCylinder_1)
  GET_PRIM_COLLECTION(GeomCapsule_1)
  GET_PRIM_COLLECTION(GeomTetMesh)
  GET_PRIM_COLLECTION(GeomNurbsPatch)

#undef GET_PRIM_COLLECTION
  return nullptr;
}

}  // namespace

// ============================================================================
// Shared helpers to reduce per-Extract* lambda duplication
// ============================================================================

bool CrateWriter::AddArrayAttribute(
    const std::string& attr_name, const value::Value& val,
    crate::FieldValuePairVector& fields, std::string* err) {
  crate::CrateValue crate_val;
  return ConvertValue(val, crate_val, err) &&
         (fields.push_back({attr_name, crate_val}), true);
}

bool CrateWriter::AddArrayAttributeWithMetas(
    const std::string& attr_name, const value::Value& val,
    const AttrMeta& metas, const Path& prim_path, std::string* err) {
  // Build a transient Attribute carrying both the value and the typed
  // attribute's authored metadata, then emit a proper property spec.
  Attribute attr;
  attr.set_type_name(val.type_name());
  primvar::PrimVar pv;
  pv.set_value(val);
  attr.set_var(std::move(pv));
  attr.metas() = metas;
  return ConvertAttributeToFields(attr_name, attr, prim_path,
                                  /*is_custom=*/false, err);
}

void CrateWriter::AddEnumAttribute(
    const std::string& attr_name, const std::string& enum_val,
    crate::FieldValuePairVector& fields) {
  crate::CrateValue crate_val;
  value::token tok(enum_val);
  crate_val.Set(tok);
  fields.push_back({attr_name, crate_val});
}


// Template definitions for AddTypedArrayAttribute and ExtractAnimatableDefault
// are in sconv-detail.hh (needed by split files).

// ============================================================================
// Main Conversion Entry Point
// ============================================================================

bool CrateWriter::ConvertStageToSpecs(const Stage& stage, std::string* err) {
  // Add root spec first with layer metadata
  Path root_path("/", "");
  crate::FieldValuePairVector root_fields;

  // Extract layer metadata from Stage
  const StageMetas& metas = stage.metas();

  // Add timeCodesPerSecond
  if (metas.timeCodesPerSecond.authored()) {
    crate::CrateValue tcps_value;
    tcps_value.Set(metas.timeCodesPerSecond.get_value());
    root_fields.push_back({"timeCodesPerSecond", tcps_value});
  }

  // Add framesPerSecond
  if (metas.framesPerSecond.authored()) {
    crate::CrateValue fps_value;
    fps_value.Set(metas.framesPerSecond.get_value());
    root_fields.push_back({"framesPerSecond", fps_value});
  }

  // Add startTimeCode
  if (metas.startTimeCode.authored()) {
    crate::CrateValue start_value;
    start_value.Set(metas.startTimeCode.get_value());
    root_fields.push_back({"startTimeCode", start_value});
  }

  // Add endTimeCode
  if (metas.endTimeCode.authored()) {
    crate::CrateValue end_value;
    end_value.Set(metas.endTimeCode.get_value());
    root_fields.push_back({"endTimeCode", end_value});
  }

  // Add upAxis
  if (metas.upAxis.authored()) {
    crate::CrateValue axis_value;
    // Convert Axis enum to token - must be actual token, not TokenIndex
    std::string axis_str = to_string(metas.upAxis.get_value());
    value::token axis_tok(axis_str);
    axis_value.Set(axis_tok);
    root_fields.push_back({"upAxis", axis_value});
  }

  // Add metersPerUnit
  if (metas.metersPerUnit.authored()) {
    crate::CrateValue mpu_value;
    mpu_value.Set(metas.metersPerUnit.get_value());
    root_fields.push_back({"metersPerUnit", mpu_value});
  }

  // Add defaultPrim
  if (!metas.defaultPrim.str().empty()) {
    crate::CrateValue dp_value;
    // defaultPrim must be actual token, not TokenIndex
    value::token dp_tok(metas.defaultPrim.str());
    dp_value.Set(dp_tok);
    root_fields.push_back({"defaultPrim", dp_value});
  }

  // Add doc (documentation)
  if (!metas.doc.value.empty()) {
    crate::CrateValue doc_value;
    doc_value.Set(metas.doc.value);
    root_fields.push_back({"documentation", doc_value});
  }

  // Add comment (string)
  if (!metas.comment.value.empty()) {
    crate::CrateValue comment_value;
    comment_value.Set(metas.comment.value);
    root_fields.push_back({"comment", comment_value});
  }

  // Add customLayerData
  if (!metas.customLayerData.empty()) {
    crate::CrateValue cld_value;
    cld_value.Set(metas.customLayerData);
    root_fields.push_back({"customLayerData", cld_value});
  }

  // Add subLayers
  if (!metas.subLayers.empty()) {
    // Convert SubLayer array to string array (asset paths)
    std::vector<std::string> sublayer_paths;
    std::vector<LayerOffset> sublayer_offsets;
    sublayer_paths.reserve(metas.subLayers.size());
    sublayer_offsets.reserve(metas.subLayers.size());

    bool has_non_default_offsets = false;

    for (const auto& sublayer : metas.subLayers) {
      // Extract asset path as string
      std::string path_str = sublayer.assetPath.GetAssetPath();
      sublayer_paths.push_back(path_str);

      // Collect layer offset
      sublayer_offsets.push_back(sublayer.layerOffset);

      // Check if any offset is non-default
      if (sublayer.layerOffset._offset != 0.0 || sublayer.layerOffset._scale != 1.0) {
        has_non_default_offsets = true;
      }
    }

    // Serialize subLayers as string array
    crate::CrateValue sublayers_value;
    sublayers_value.Set(sublayer_paths);
    root_fields.push_back({"subLayers", sublayers_value});

    // Serialize subLayerOffsets as LayerOffset array (only if non-default)
    if (has_non_default_offsets) {
      crate::CrateValue offsets_value;
      offsets_value.Set(sublayer_offsets);
      root_fields.push_back({"subLayerOffsets", offsets_value});
    }
  }

  // Add primChildren listing root prim names
  {
    std::vector<value::token> root_children;
    for (const auto& prim : stage.root_prims()) {
      root_children.push_back(value::token(prim.element_name()));
    }
    if (!root_children.empty()) {
      crate::CrateValue pc_value;
      pc_value.Set(root_children);
      root_fields.push_back({"primChildren", pc_value});
    }
  }

  if (!AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    if (err) *err = "Failed to add root spec: " + *err;
    return false;
  }

  // Convert all root prims
  for (const auto& prim : stage.root_prims()) {
    Path parent_path("/", "");
    if (!ConvertPrimIterative(prim, parent_path, err)) {
      if (err) *err = "Failed to convert prim: " + *err;
      return false;
    }
  }

  return true;
}

// ============================================================================
// Per-Prim Conversion (no recursion into children)
// ============================================================================

bool CrateWriter::ConvertSinglePrim(
  const Prim& prim,
  const Path& parent_path,
  std::string* err
) {
  // Build absolute path for this prim
  std::string prim_name = prim.element_name();
  std::string parent_str = parent_path.prim_part();
  std::string abs_path_str;

  if (parent_str == "/") {
    abs_path_str = "/" + prim_name;
  } else {
    abs_path_str = parent_str + "/" + prim_name;
  }

  Path prim_path(abs_path_str, "");
  std::string type_name = prim.prim_type_name();

  DCOUT("ConvertSinglePrim: name=" << prim_name << " abs=" << abs_path_str << " type=" << type_name);

  // Extract properties from this prim
  crate::FieldValuePairVector fields;

  if (!ExtractPrimProperties(prim, prim_path, fields, err)) {
    if (err) *err = "Failed to extract properties for " + abs_path_str + ": " + *err;
    return false;
  }

  // Split fields: prim-level fields (specifier, typeName) stay on the prim
  // spec; property fields become separate Attribute specs.
  crate::FieldValuePairVector prim_fields;
  // Collect property fields: name -> {default_value, timeSamples_value, ...}
  // `extra_metas` covers AttrMeta keys beyond .interpolation: elementSize,
  // customData, displayName, displayGroup, documentation, hidden,
  // colorSpace, comment, weight, allowedTokens. Pairs are emitted into
  // the attribute spec as-is.
  struct PropEntry {
    std::string name;
    crate::CrateValue default_val;
    bool has_default{false};
    crate::CrateValue ts_val;
    bool has_ts{false};
    crate::CrateValue variability_val;
    bool has_variability{false};
    crate::CrateValue interpolation_val;
    bool has_interpolation{false};
    std::vector<std::pair<std::string, crate::CrateValue>> extra_metas;
  };
  std::vector<PropEntry> prop_entries;

  // AttrMeta suffixes other than `.interpolation` that schema-typed
  // extractors may emit as `<base>.<key>`. The router stuffs the
  // matching value into the prop's attribute spec under field name `key`.
  static const std::set<std::string> kAttrMetaSuffixes = {
    "elementSize", "customData", "displayName", "displayGroup",
    "documentation", "doc", "hidden", "colorSpace", "comment", "weight",
    "allowedTokens",
  };

  // Set of field names that belong on the prim spec, not as separate attributes
  static const std::set<std::string> kPrimFields = {
    "specifier", "typeName", "primChildren", "properties",
    "variantSetNames", "variantSelection", "kind",
    "active", "hidden", "documentation", "comment", "customData",
    "assetInfo", "instanceable", "clips",
    "apiSchemas", "inherits", "inheritPaths", "specializes", "references", "payload",
    "displayName", "displayGroup", "sceneName",
  };

  for (auto& fv : fields) {
    if (kPrimFields.count(fv.first)) {
      prim_fields.push_back(std::move(fv));
      continue;
    }

    // Check for ".timeSamples" / ".interpolation" / other AttrMeta suffixes
    std::string base_name = fv.first;
    bool is_ts = false;
    bool is_interp = false;
    std::string meta_key;  // non-empty when fv.first is `<base>.<meta_key>`
    const std::string ts_suffix = ".timeSamples";
    const std::string interp_suffix = ".interpolation";
    if (base_name.size() > ts_suffix.size() &&
        base_name.compare(base_name.size() - ts_suffix.size(),
                          ts_suffix.size(), ts_suffix) == 0) {
      base_name = base_name.substr(0, base_name.size() - ts_suffix.size());
      is_ts = true;
    } else if (base_name.size() > interp_suffix.size() &&
               base_name.compare(base_name.size() - interp_suffix.size(),
                                 interp_suffix.size(), interp_suffix) == 0) {
      base_name = base_name.substr(0, base_name.size() - interp_suffix.size());
      is_interp = true;
    } else {
      // Generic `<base>.<meta_key>` suffix (elementSize, customData, ...).
      // Only consume when the suffix matches a known AttrMeta key, to
      // avoid swallowing unrelated dot-bearing names.
      auto dot = base_name.rfind('.');
      if (dot != std::string::npos && dot + 1 < base_name.size()) {
        std::string suffix = base_name.substr(dot + 1);
        if (kAttrMetaSuffixes.count(suffix)) {
          meta_key = std::move(suffix);
          base_name = base_name.substr(0, dot);
        }
      }
    }

    // Check for "variability" field
    bool is_variability = (fv.first == "variability");
    if (is_variability) {
      // variability belongs on the prim spec
      prim_fields.push_back(std::move(fv));
      continue;
    }

    // Find or create prop entry
    PropEntry* entry = nullptr;
    for (auto& pe : prop_entries) {
      if (pe.name == base_name) {
        entry = &pe;
        break;
      }
    }
    if (!entry) {
      prop_entries.push_back({base_name, {}, false, {}, false, {}, false, {}, false});
      entry = &prop_entries.back();
    }

    if (is_ts) {
      entry->ts_val = std::move(fv.second);
      entry->has_ts = true;
    } else if (is_interp) {
      entry->interpolation_val = std::move(fv.second);
      entry->has_interpolation = true;
    } else if (!meta_key.empty()) {
      // documentation alias: `doc` -> `documentation` for the spec
      std::string spec_key = (meta_key == "doc") ? "documentation" : meta_key;
      entry->extra_metas.emplace_back(std::move(spec_key),
                                      std::move(fv.second));
    } else {
      entry->default_val = std::move(fv.second);
      entry->has_default = true;
    }
  }

  // Pre-collect props map pointer for later processing
  const auto* props_map = GetPrimProps(prim.data());

  // Add "primChildren" field (ordered list of child prim names)
  if (!prim.children().empty()) {
    std::vector<value::token> child_tokens;
    for (const auto& child : prim.children()) {
      child_tokens.push_back(value::token(child.element_name()));
    }
    crate::CrateValue children_value;
    children_value.Set(child_tokens);
    prim_fields.push_back({"primChildren", children_value});
  }

  // Add spec for this prim (prim-level fields only; "properties" added later)
  if (!AddSpec(prim_path, SpecType::Prim, prim_fields, err)) {
    if (err) *err = "Failed to add spec for: " + abs_path_str + ": " + *err;
    return false;
  }

  // Known uniform properties (must have Variability::Uniform in their Attribute spec)
  static const std::set<std::string> kUniformProps = {
    "offsets", "normalOffsets", "pointIndices",  // BlendShape
    "subdivisionScheme", "interpolateBoundary", "faceVaryingLinearInterpolation",  // Mesh
    "elementType", "familyName",  // GeomSubset
    "projection", "stereoRole",  // Camera
    "type", "basis", "wrap",  // BasisCurves
    "joints", "jointNames", "bindTransforms", "restTransforms",  // Skeleton
    "blendShapes",  // SkelAnimation
    "inactiveIds",  // PointInstancer
    "purpose", "doubleSided",  // GPrim
  };

  // Create separate Attribute specs for each property
  for (auto& pe : prop_entries) {
    Path attr_path = prim_path.AppendProperty(pe.name);
    crate::FieldValuePairVector attr_fields;

    // Determine type name from the value
    std::string prop_type_name;
    if (pe.has_default) {
      prop_type_name = pe.default_val.type_name();
    } else if (pe.has_ts) {
      // For timeSamples, get the element type from the first sample
      auto ts_opt = pe.ts_val.get_value<value::TimeSamples>();
      if (ts_opt && ts_opt->size() > 0) {
        prop_type_name = ts_opt->get_samples()[0].value.type_name();
      }
    }

    if (!prop_type_name.empty()) {
      crate::CrateValue type_value;
      value::token type_tok(prop_type_name);
      type_value.Set(type_tok);
      attr_fields.push_back({"typeName", type_value});
    }

    // Add variability for uniform properties
    if (kUniformProps.count(pe.name)) {
      crate::CrateValue var_value;
      var_value.Set(Variability::Uniform);
      attr_fields.push_back({"variability", var_value});
    }

    if (pe.has_default) {
      attr_fields.push_back({"default", std::move(pe.default_val)});
    }

    if (pe.has_ts) {
      attr_fields.push_back({"timeSamples", std::move(pe.ts_val)});
    }

    if (pe.has_interpolation) {
      attr_fields.push_back({"interpolation", std::move(pe.interpolation_val)});
    }

    for (auto& mk : pe.extra_metas) {
      attr_fields.push_back({mk.first, std::move(mk.second)});
    }

    if (!attr_fields.empty()) {
      if (!AddSpec(attr_path, SpecType::Attribute, attr_fields, err)) {
        DCOUT("WARNING: Failed to add attribute spec for: " << pe.name);
      }
    }
  }

  // After property extraction, handle Material outputs as separate attribute specs
  // This must happen AFTER the prim spec is added to maintain correct ordering
  if (type_name == "Material") {
    const Material* material = prim.data().as<Material>();
    if (material) {
      // These will be added as separate attribute specs after the Material prim
      if (!AddMaterialOutputSpecs(material, prim_path, err)) {
        if (err) *err = "Failed to add Material output specs: " + *err;
        return false;
      }
    }
  }

  // After adding Shader prim spec, handle shader-specific inputs as separate attribute specs
  if (type_name == "Shader") {
    const Shader* shader = prim.data().as<Shader>();
    if (shader) {
      // Check shader type and extract inputs accordingly
      if (shader->info_id == "UsdPreviewSurface") {
        // Try to get UsdPreviewSurface from shader value
        if (auto* preview_surface = shader->value.as<UsdPreviewSurface>()) {
          if (!AddUsdPreviewSurfaceInputSpecs(preview_surface, prim_path, err)) {
            if (err) *err = "Failed to add UsdPreviewSurface input specs: " + *err;
            return false;
          }
          // Terminal output declarations — call ConvertAttributeToFields
          // directly (bypasses is_empty() check in ConvertPropertyToFields)
          if (preview_surface->outputsSurface.authored()) {
            Attribute a;
            a.set_type_name(value::kToken);
            ConvertAttributeToFields("outputs:surface", a, prim_path, false, err);
          }
          if (preview_surface->outputsDisplacement.authored()) {
            Attribute a;
            a.set_type_name(value::kToken);
            ConvertAttributeToFields("outputs:displacement", a, prim_path, false, err);
          }
        }
      } else if (shader->info_id == "UsdUVTexture") {
        if (auto* uv_texture = shader->value.as<UsdUVTexture>()) {
          if (!AddUsdUVTextureInputSpecs(uv_texture, prim_path, err)) {
            if (err) *err = "Failed to add UsdUVTexture input specs: " + *err;
            return false;
          }
          // Terminal outputs — use actual_type_name if available, else the template type
          auto add_t = [&](const char *n, const auto &term_attr, const char *default_type) {
            if (!term_attr.authored()) return;
            Attribute attr;
            attr.set_type_name(term_attr.has_actual_type() ? term_attr.get_actual_type_name() : default_type);
            ConvertAttributeToFields(n, attr, prim_path, false, err);
          };
          add_t("outputs:r", uv_texture->outputsR, "float");
          add_t("outputs:g", uv_texture->outputsG, "float");
          add_t("outputs:b", uv_texture->outputsB, "float");
          add_t("outputs:a", uv_texture->outputsA, "float");
          add_t("outputs:rgb", uv_texture->outputsRGB, "float3");
        }
      } else if (shader->info_id == "UsdTransform2d") {
        if (auto* transform2d = shader->value.as<UsdTransform2d>()) {
          if (!AddUsdTransform2dInputSpecs(transform2d, prim_path, err)) {
            if (err) *err = "Failed to add UsdTransform2d input specs: " + *err;
            return false;
          }
          if (transform2d->result.authored()) {
            Attribute a; a.set_type_name("float2");
            ConvertAttributeToFields("outputs:result", a, prim_path, false, err);
          }
        }
      } else if (shader->info_id.find("UsdPrimvarReader_") == 0) {
        if (!AddUsdPrimvarReaderInputSpecs(shader->value, shader->info_id, prim_path, err)) {
          if (err) *err = "Failed to add UsdPrimvarReader input specs: " + *err;
          return false;
        }
        // Terminal output for all PrimvarReader variants
        auto add_pr_terminal = [&](auto *pr) {
          if (pr && pr->result.authored()) {
            Attribute a; a.set_type_name(pr->result.type_name().empty() ? "float2" : pr->result.type_name());
            ConvertAttributeToFields("outputs:result", a, prim_path, false, err);
          }
        };
        if (auto *p0 = shader->value.as<UsdPrimvarReader_float2>()) add_pr_terminal(p0);
        else if (auto *p1 = shader->value.as<UsdPrimvarReader_float>()) add_pr_terminal(p1);
        else if (auto *p2 = shader->value.as<UsdPrimvarReader_float3>()) add_pr_terminal(p2);
        else if (auto *p3 = shader->value.as<UsdPrimvarReader_float4>()) add_pr_terminal(p3);
        else if (auto *p4 = shader->value.as<UsdPrimvarReader_int>()) add_pr_terminal(p4);
        else if (auto *p5 = shader->value.as<UsdPrimvarReader_string>()) add_pr_terminal(p5);
      }
    }
  }

  // After adding prim spec, handle material bindings as separate relationship specs
  // This applies to any prim that might have material bindings (typically geometry)
  if (!AddMaterialBindingSpecs(prim, prim_path, err)) {
    // Don't fail on material binding errors - just warn
  }

  // Handle doubleSided for GPrim-derived types via ConvertPropertyToFields
  // (The fields path doesn't preserve the bool type correctly in crate format)
  {
    const GPrim* gprim = nullptr;
    // Try all GPrim-derived types
    if (auto* g0 = prim.data().as<GeomMesh>()) gprim = g0;
    else if (auto* g1 = prim.data().as<GeomCube>()) gprim = g1;
    else if (auto* g2 = prim.data().as<GeomSphere>()) gprim = g2;
    else if (auto* g3 = prim.data().as<GeomCone>()) gprim = g3;
    else if (auto* g4 = prim.data().as<GeomCylinder>()) gprim = g4;
    else if (auto* g5 = prim.data().as<GeomCapsule>()) gprim = g5;
    else if (auto* g6 = prim.data().as<GeomPoints>()) gprim = g6;
    else if (auto* g7 = prim.data().as<GeomBasisCurves>()) gprim = g7;

    if (gprim && gprim->doubleSided.authored()) {
      Attribute ds_attr;
      ds_attr.set_type_name("bool");
      ds_attr.variability() = Variability::Uniform;
      primvar::PrimVar pvar;
      pvar.set_value(gprim->doubleSided.get_value());
      ds_attr.set_var(std::move(pvar));
      crate::FieldValuePairVector dummy;
      ConvertPropertyToFields("doubleSided", Property(ds_attr, /* custom */ false), prim_path, dummy, err);
    }
  }

  // After adding light prim spec, handle light filter relationships as separate relationship specs
  // This applies to any light prim that has light:filters relationships
  if (!AddLightFilterSpecs(prim, prim_path, err)) {
    // Don't fail on light filter relationship errors - just warn
  }

  // PointInstancer prototypes relationship: emit as a separate relationship
  // spec so `rel prototypes = [...]` survives USDC roundtrip.
  if (prim.data().as<GeomPointInstancer>()) {
    if (!AddPointInstancerPrototypesSpec(prim, prim_path, err)) {
      DCOUT("WARNING: Failed to add prototypes relationship spec");
    }
  }

  // Process generic props map for custom properties, primvars, relationships, etc.
  // This covers ALL prim types and handles properties not extracted by type-specific handlers.
  // Skip properties already extracted by type-specific handlers to avoid duplicates.
  // Re-emit Collection (`collection:<inst>:{includes,excludes,
  // includeRoot,expansionRule}`) from the typed Collection storage.
  // The reconstruct path consumed these from the props map and stored
  // them on `Collection::instances()`; without this re-emit, USDC
  // round-trip would drop them.
  if (const Collection *coll_storage = GetPrimCollection(prim.data())) {
    const auto &instances = coll_storage->instances();
    for (size_t i = 0; i < instances.size(); ++i) {
      const std::string &inst_name = instances.keys()[i];
      CollectionInstance inst;
      if (!instances.at(i, &inst)) {
        continue;
      }
      const std::string prefix = "collection:" + inst_name;

      if (inst.includes.authored()) {
        std::string ce;
        if (!ConvertRelationshipToFields(prefix + ":includes",
                                         inst.includes.relationship(),
                                         prim_path, &ce)) {
          DCOUT("WARNING: Collection includes emit failed: " << ce);
        }
      }
      if (inst.excludes.authored()) {
        std::string ce;
        if (!ConvertRelationshipToFields(prefix + ":excludes",
                                         inst.excludes.relationship(),
                                         prim_path, &ce)) {
          DCOUT("WARNING: Collection excludes emit failed: " << ce);
        }
      }
      if (inst.expansionRule.authored()) {
        // expansionRule is a uniform token attribute. Map the
        // ExpansionRule enum back to its token spelling.
        const char *tok = nullptr;
        switch (inst.expansionRule.get_value()) {
          case CollectionInstance::ExpansionRule::ExplicitOnly:
            tok = kExplicitOnly; break;
          case CollectionInstance::ExpansionRule::ExpandPrims:
            tok = kExpandPrims; break;
          case CollectionInstance::ExpansionRule::ExpandPrimsAndProperties:
            tok = kExpandPrimsAndProperties; break;
        }
        if (tok) {
          Attribute a = Attribute::Uniform(value::token(tok));
          a.set_type_name("token");
          std::string ce;
          if (!ConvertAttributeToFields(prefix + ":expansionRule", a,
                                        prim_path, /*is_custom=*/false,
                                        &ce)) {
            DCOUT("WARNING: Collection expansionRule emit failed: " << ce);
          }
        }
      }
      if (inst.includeRoot.authored()) {
        bool b = false;
        if (inst.includeRoot.get_value().get_scalar(&b)) {
          Attribute a = Attribute::Uniform(b);
          a.set_type_name("bool");
          std::string ce;
          if (!ConvertAttributeToFields(prefix + ":includeRoot", a,
                                        prim_path, /*is_custom=*/false,
                                        &ce)) {
            DCOUT("WARNING: Collection includeRoot emit failed: " << ce);
          }
        }
      }
    }
  }

  if (props_map) {
    // Build set of names already extracted by type-specific handlers
    std::unordered_set<std::string> extracted_names;
    for (const auto& pe : prop_entries) {
      extracted_names.insert(pe.name);
    }

    crate::FieldValuePairVector dummy_fields;
    for (const auto& kv : *props_map) {
      if (extracted_names.count(kv.first)) {
        continue;  // Already handled by type-specific extractor
      }
      // Skip xformOp properties when the typed xformable path already
      // produced specs for them (avoids duplicates). When the typed path
      // didn't extract anything (extracted_names has no xformOp entries),
      // fall through so props-authored xformOps survive USDC round-trip.
      if (kv.first.find("xformOp:") == 0 || kv.first == "xformOpOrder") {
        bool typed_emitted = false;
        for (const auto &en : extracted_names) {
          if (en.find("xformOp:") == 0 || en == "xformOpOrder") {
            typed_emitted = true;
            break;
          }
        }
        if (typed_emitted) continue;
      }

      // For attribute properties, check if the value type is writable.
      // ConvertAttributeToFields calls AddSpec which can't be undone if
      // Finalize later fails on unsupported types.
      // Note: empty array attributes encoded via the TRY_INLINE_EMPTY_ARRAY
      // path (IsArray + payload=0). Reader handles this via per-type
      // checks at e.g. crate-reader-values.cc:983.

      std::string prop_err;
      if (!ConvertPropertyToFields(kv.first, kv.second, prim_path, dummy_fields, &prop_err)) {
        DCOUT("WARNING: Failed to convert property from props map: " << kv.first
                  << ": " << prop_err);
      }
    }
  }

  // After adding prim spec, process VariantSets if present
  // Variants represent alternative versions of prims/properties
  const auto& variant_sets = prim.variantSets();
  if (!variant_sets.empty()) {
    for (const auto& vs_item : variant_sets) {
      const auto& variantset_name = vs_item.first;
      const auto& variantset_data = vs_item.second;
      if (!ConvertVariantSetToFields(variantset_name, variantset_data, prim_path, err)) {
        if (err) *err = "Failed to convert VariantSet '" + variantset_name + "' for " + abs_path_str + ": " + *err;
        return false;
      }
    }
  }

  // Build "properties" field by scanning accumulated specs for child
  // Attribute/Relationship specs of this prim.  This field is required by
  // Pixar's USD reader and also drives propertyNames() ordering.
  {
    const std::string prim_prefix = abs_path_str + ".";
    std::vector<value::token> property_names;
    std::set<std::string> seen;
    for (const auto &sd : spec_data_) {
      if (sd.spec_type == SpecType::Attribute ||
          sd.spec_type == SpecType::Relationship) {
        const std::string &fp = sd.path.full_path_name();
        if (fp.size() > prim_prefix.size() &&
            fp.compare(0, prim_prefix.size(), prim_prefix) == 0) {
          std::string prop_name = fp.substr(prim_prefix.size());
          // Only direct children (no further '/' or '.')
          if (prop_name.find('/') == std::string::npos &&
              prop_name.find('.') == std::string::npos) {
            if (seen.insert(prop_name).second) {
              property_names.push_back(value::token(prop_name));
            }
          }
        }
      }
    }
    if (!property_names.empty()) {
      // Append "properties" field to the existing prim spec
      for (auto &sd : spec_data_) {
        if (sd.path.full_path_name() == abs_path_str &&
            sd.spec_type == SpecType::Prim) {
          crate::CrateValue props_value;
          props_value.Set(property_names);
          sd.fields.push_back({"properties", props_value});
          break;
        }
      }
    }
  }

  return true;
}

// ============================================================================
// Iterative Depth-First Prim Conversion
// ============================================================================

bool CrateWriter::ConvertPrimIterative(
  const Prim& root_prim,
  const Path& parent_path,
  std::string* err
) {
  // Explicit DFS stack: (prim pointer, parent path)
  struct WorkItem {
    const Prim* prim;
    Path parent_path;
  };

  std::vector<WorkItem> stack;
  stack.push_back({&root_prim, parent_path});

  while (!stack.empty()) {
    if (stack.size() > kMaxDefaultTraversalLimit) {
      if (err) *err = "ConvertPrimIterative: tree too deep.";
      return false;
    }

    WorkItem item = std::move(stack.back());
    stack.pop_back();

    if (!ConvertSinglePrim(*item.prim, item.parent_path, err)) {
      return false;
    }

    // Build this prim's path for its children
    std::string parent_str = item.parent_path.prim_part();
    std::string abs_path_str;
    if (parent_str == "/") {
      abs_path_str = "/" + item.prim->element_name();
    } else {
      abs_path_str = parent_str + "/" + item.prim->element_name();
    }
    Path prim_path(abs_path_str, "");

    // Push children in reverse order so left-most child is processed first
    const auto& children = item.prim->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      stack.push_back({&(*it), prim_path});
    }
  }

  return true;
}

// ============================================================================
// Property Extraction
// ============================================================================


void CrateWriter::ExtractPrimMeta(
    const PrimMeta& metas,
    crate::FieldValuePairVector& fields) {

  if (metas.has_active()) {
    crate::CrateValue v;
    v.Set(metas.get_active());
    fields.push_back({"active", v});
  }

  if (metas.has_hidden()) {
    crate::CrateValue v;
    v.Set(metas.get_hidden());
    fields.push_back({"hidden", v});
  }

  if (metas.has_kind()) {
    crate::CrateValue v;
    value::token tok(metas.get_kind());
    v.Set(tok);
    fields.push_back({"kind", v});
  }

  if (metas.has_displayName()) {
    crate::CrateValue v;
    v.Set(metas.get_displayName());
    fields.push_back({"displayName", v});
  }

  if (metas.has_doc()) {
    crate::CrateValue v;
    v.Set(metas.get_doc().value);
    fields.push_back({"documentation", v});
  }

  if (metas.has_comment()) {
    crate::CrateValue v;
    v.Set(metas.get_comment().value);
    fields.push_back({"comment", v});
  }

  if (metas.has_sceneName()) {
    // pxrUSD stores `sceneName` (USDZ scene-library extension) as a
    // `string`, not a `token`. The reader rejects token-typed values.
    crate::CrateValue v;
    v.Set(std::string(metas.get_sceneName()));
    fields.push_back({"sceneName", v});
  }

  if (metas.variants) {
    crate::CrateValue v;
    v.Set(metas.variants.value());
    fields.push_back({"variantSelection", v});
  }

  if (metas.variantSets) {
    ListOp<std::string> vs_listop;

    for (const auto& variantSets_op : metas.variantSets.value()) {
      const ListEditQual& qual = variantSets_op.first;
      const std::vector<std::string>& vs_list = variantSets_op.second;

      switch (qual) {
        case ListEditQual::ResetToExplicit:
          vs_listop.ClearAndMakeExplicit();
          vs_listop.SetExplicitItems(vs_list);
          break;
        case ListEditQual::Append:
          vs_listop.SetAppendedItems(vs_list);
          break;
        case ListEditQual::Prepend:
          vs_listop.SetPrependedItems(vs_list);
          break;
        case ListEditQual::Add:
          vs_listop.SetAddedItems(vs_list);
          break;
        case ListEditQual::Delete:
          vs_listop.SetDeletedItems(vs_list);
          break;
        default:
          vs_listop.SetPrependedItems(vs_list);
          break;
      }
    }

    crate::CrateValue v;
    v.Set(vs_listop);
    fields.push_back({"variantSetNames", v});
  }

  // TODO: references, payload, customData, apiSchemas, inherits, specializes,
  // assetInfo, and unregisteredMetas are implemented in the code below but
  // currently disabled because the Stage path's binary encoding causes
  // fieldset decode errors and CustomDataType serialization issues.
  // The Layer path (ConvertPrimSpecRecursive) handles these correctly.
  // Enable these after fixing the encoding/decoding issues.
  //
  // Fields that need fixing before enabling:
  // - references/payload: cause "Failed to decode fieldset id" errors
  // - customData/assetInfo: int2, int2[], asset types unsupported
  // - apiSchemas: ListOp encoding issues for non-Explicit qualifiers
  // - inherits/specializes: similar to references

  if (metas.has_instanceable()) {
    crate::CrateValue v;
    v.Set(metas.get_instanceable());
    fields.push_back({"instanceable", v});
  }

  // customData (Dictionary). Skipped historically due to dict serialization
  // worries, but the dict path on the writer side is now exercised by
  // attribute customData and works for primitive value types
  // (string/int/bool/float/double/token). We emit here too so prim-level
  // customData survives USDC round-trip.
  if (metas.has_customData()) {
    crate::CrateValue v;
    v.Set(metas.get_customData());
    fields.push_back({"customData", v});
  }

  // assetInfo (Dictionary).
  if (metas.has_assetInfo()) {
    crate::CrateValue v;
    v.Set(metas.get_assetInfo());
    fields.push_back({"assetInfo", v});
  }

  // clips (Dictionary). Value-clip declarations for animation streaming.
  if (metas.has_clips()) {
    crate::CrateValue v;
    v.Set(metas.get_clips());
    fields.push_back({"clips", v});
  }

  // apiSchemas
  if (metas.has_apiSchemas()) {
    const auto schemas = metas.get_apiSchemas();
    // Convert APISchemas to ListOp<value::token>
    std::vector<value::token> schema_tokens;
    for (const auto &s : schemas.names) {
      std::string name = to_string(s.first);
      if (!s.second.empty()) {
        name += ":" + s.second;  // Multi-apply instance
      }
      schema_tokens.push_back(value::token(name));
    }
    for (const auto &s : schemas.unknownSchemas) {
      std::string name = s.first;
      if (!s.second.empty()) {
        name += ":" + s.second;
      }
      schema_tokens.push_back(value::token(name));
    }
    if (!schema_tokens.empty()) {
      ListOp<value::token> listop;
      if (schemas.listOpQual == ListEditQual::Prepend) {
        listop.SetPrependedItems(schema_tokens);
      } else {
        listop.SetExplicitItems(schema_tokens);
      }
      crate::CrateValue v;
      v.Set(listop);
      fields.push_back({"apiSchemas", v});
    }
  }

  // Composition arcs — references, inherits, specializes, payload.
  // Build ListOp<T> objects with the parsed list-edit qualifiers and emit them
  // as crate fields. Mirrors the working layer-path writer in sconv-layer.cc.

  auto convert_path_listop = [](
      const std::vector<std::pair<ListEditQual, std::vector<Path>>>& src) {
    ListOp<Path> out;
    for (const auto& op : src) {
      switch (op.first) {
        case ListEditQual::ResetToExplicit:
          out.ClearAndMakeExplicit();
          out.SetExplicitItems(op.second);
          break;
        case ListEditQual::Append:  out.SetAppendedItems(op.second); break;
        case ListEditQual::Prepend: out.SetPrependedItems(op.second); break;
        case ListEditQual::Add:     out.SetAddedItems(op.second); break;
        case ListEditQual::Delete:  out.SetDeletedItems(op.second); break;
        default:
          out.ClearAndMakeExplicit();
          out.SetExplicitItems(op.second);
          break;
      }
    }
    return out;
  };

  auto convert_ref_listop = [](
      const std::vector<std::pair<ListEditQual, std::vector<Reference>>>& src) {
    ListOp<Reference> out;
    for (const auto& op : src) {
      switch (op.first) {
        case ListEditQual::ResetToExplicit:
          out.ClearAndMakeExplicit();
          out.SetExplicitItems(op.second);
          break;
        case ListEditQual::Append:  out.SetAppendedItems(op.second); break;
        case ListEditQual::Prepend: out.SetPrependedItems(op.second); break;
        case ListEditQual::Add:     out.SetAddedItems(op.second); break;
        case ListEditQual::Delete:  out.SetDeletedItems(op.second); break;
        default:
          out.ClearAndMakeExplicit();
          out.SetExplicitItems(op.second);
          break;
      }
    }
    return out;
  };

  auto convert_payload_listop = [](
      const std::vector<std::pair<ListEditQual, std::vector<Payload>>>& src) {
    ListOp<Payload> out;
    for (const auto& op : src) {
      switch (op.first) {
        case ListEditQual::ResetToExplicit:
          out.ClearAndMakeExplicit();
          out.SetExplicitItems(op.second);
          break;
        case ListEditQual::Append:  out.SetAppendedItems(op.second); break;
        case ListEditQual::Prepend: out.SetPrependedItems(op.second); break;
        case ListEditQual::Add:     out.SetAddedItems(op.second); break;
        case ListEditQual::Delete:  out.SetDeletedItems(op.second); break;
        default:
          out.ClearAndMakeExplicit();
          out.SetExplicitItems(op.second);
          break;
      }
    }
    return out;
  };

  if (metas.references.has_value()) {
    crate::CrateValue v;
    v.Set(convert_ref_listop(metas.references.value()));
    fields.push_back({"references", v});
  }

  if (metas.payload.has_value()) {
    crate::CrateValue v;
    v.Set(convert_payload_listop(metas.payload.value()));
    fields.push_back({"payload", v});
  }

  if (metas.inherits.has_value()) {
    crate::CrateValue v;
    v.Set(convert_path_listop(metas.inherits.value()));
    fields.push_back({"inheritPaths", v});
  }

  if (metas.specializes.has_value()) {
    crate::CrateValue v;
    v.Set(convert_path_listop(metas.specializes.value()));
    fields.push_back({"specializes", v});
  }
}

bool CrateWriter::ExtractPrimProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  // Get prim type name. prim_type_name() carries the parser-set or
  // user-set string; for in-memory authored prims that field is empty,
  // so fall back to the typed schema name from the underlying value.
  // Without this fallback the writer skips both the `typeName` crate
  // field and the type-specific property extraction, and the reader sees
  // a generic prim with no schema and no attributes.
  std::string type_name = prim.prim_type_name();
  if (type_name.empty()) {
    type_name = prim.type_name();
  }

  // Add specifier field (as Specifier enum, not as token)
  Specifier spec = prim.specifier();
  // USDA loader sets the spec on the typed-prim wrapper inside `prim.data()`
  // but historically did not propagate it to the outer Prim wrapper. Fall
  // back to the typed prim's `.spec` so `class`/`over` survive USDC writing.
  if (spec == Specifier::Invalid) {
    const auto &v = prim.data();
#define GET_SPEC(__TY) if (auto *t = v.as<__TY>()) { spec = t->spec; goto spec_resolved; }
    GET_SPEC(Xform)
    GET_SPEC(GeomMesh)
    GET_SPEC(GeomSphere)
    GET_SPEC(GeomCube)
    GET_SPEC(GeomCylinder)
    GET_SPEC(GeomCone)
    GET_SPEC(GeomCapsule)
    GET_SPEC(GeomCamera)
    GET_SPEC(GeomPoints)
    GET_SPEC(GeomSubset)
    GET_SPEC(GeomBasisCurves)
    GET_SPEC(GeomNurbsCurves)
    GET_SPEC(GeomHermiteCurves)
    GET_SPEC(GeomPlane)
    GET_SPEC(GeomCylinder_1)
    GET_SPEC(GeomCapsule_1)
    GET_SPEC(GeomTetMesh)
    GET_SPEC(GeomNurbsPatch)
    GET_SPEC(GeomPointInstancer)
    GET_SPEC(SphereLight)
    GET_SPEC(RectLight)
    GET_SPEC(DiskLight)
    GET_SPEC(DistantLight)
    GET_SPEC(CylinderLight)
    GET_SPEC(DomeLight)
    GET_SPEC(DomeLight_1)
    GET_SPEC(GeometryLight)
    GET_SPEC(PortalLight)
    GET_SPEC(Scope)
    GET_SPEC(Model)
    GET_SPEC(Material)
    GET_SPEC(Shader)
    GET_SPEC(NodeGraph)
    GET_SPEC(SkelRoot)
    GET_SPEC(Skeleton)
    GET_SPEC(SkelAnimation)
    GET_SPEC(BlendShape)
#undef GET_SPEC
spec_resolved:;
  }
  if (spec == Specifier::Invalid) {
    spec = Specifier::Def;
  }
  crate::CrateValue spec_value;
  spec_value.Set(spec);  // CrateValue supports Specifier directly
  fields.push_back({"specifier", spec_value});

  // Add typeName if present - store as value::token, not as token index!
  if (!type_name.empty()) {
    crate::CrateValue typename_value;
    value::token tok(type_name);
    typename_value.Set(tok);  // Store the token, not the index!
    fields.push_back({"typeName", typename_value});
  }

  // Extract PrimMeta (kind, active, hidden, customData, apiSchemas, etc.)
  ExtractPrimMeta(prim.metas(), fields);

  // Extract type-specific properties
  if (!ExtractTypeSpecificProperties(prim, prim_path, type_name, fields, err)) {
    // Log warning but don't fail - we still want to create the prim
    DCOUT("WARNING: Failed to extract type-specific properties for "
              << type_name << ": " << (err ? *err : "unknown error"));
  }

  return true;
}

// ============================================================================
// Type-Specific Property Extraction
// ============================================================================

bool CrateWriter::ExtractTypeSpecificProperties(
  const Prim& prim,
  const Path& prim_path,
  const std::string& type_name,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  // Try to extract properties based on prim type
  if (type_name == "Xform") {
    return ExtractXformProperties(prim, prim_path, fields, err);
  } else if (type_name == "Mesh") {
    return ExtractMeshProperties(prim, prim_path, fields, err);
  } else if (type_name == "Cube") {
    return ExtractCubeProperties(prim, prim_path, fields, err);
  } else if (type_name == "Sphere") {
    return ExtractSphereProperties(prim, prim_path, fields, err);
  } else if (type_name == "Cylinder") {
    return ExtractCylinderProperties(prim, prim_path, fields, err);
  } else if (type_name == "Cone") {
    return ExtractConeProperties(prim, prim_path, fields, err);
  } else if (type_name == "Capsule") {
    return ExtractCapsuleProperties(prim, prim_path, fields, err);
  } else if (type_name == "Points") {
    return ExtractPointsProperties(prim, prim_path, fields, err);
  } else if (type_name == "Camera") {
    return ExtractCameraProperties(prim, prim_path, fields, err);
  } else if (type_name == "BasisCurves") {
    return ExtractBasisCurvesProperties(prim, prim_path, fields, err);
  } else if (type_name == "NurbsCurves") {
    return ExtractNurbsCurvesProperties(prim, prim_path, fields, err);
  } else if (type_name == "Plane") {
    return ExtractGeomPlaneProperties(prim, prim_path, fields, err);
  } else if (type_name == "Cylinder_1") {
    return ExtractGeomCylinder1Properties(prim, prim_path, fields, err);
  } else if (type_name == "Capsule_1") {
    return ExtractGeomCapsule1Properties(prim, prim_path, fields, err);
  } else if (type_name == "TetMesh") {
    return ExtractGeomTetMeshProperties(prim, prim_path, fields, err);
  } else if (type_name == "NurbsPatch") {
    return ExtractGeomNurbsPatchProperties(prim, prim_path, fields, err);
  } else if (type_name == "HermiteCurves") {
    return ExtractGeomHermiteCurvesProperties(prim, prim_path, fields, err);
  } else if (type_name == "PointInstancer") {
    return ExtractPointInstancerProperties(prim, prim_path, fields, err);
  } else if (type_name == "GeomSubset") {
    return ExtractGeomSubsetProperties(prim, prim_path, fields, err);
  } else if (type_name == "Material") {
    return ExtractMaterialProperties(prim, prim_path, fields, err);
  } else if (type_name == "Shader") {
    return ExtractShaderProperties(prim, fields, err);
  } else if (type_name == "NodeGraph") {
    return ExtractNodeGraphProperties(prim, fields, err);
  } else if (type_name == "BlendShape") {
    return ExtractBlendShapeProperties(prim, prim_path, fields, err);
  } else if (type_name == "SphereLight") {
    return ExtractSphereLightProperties(prim, prim_path, fields, err);
  } else if (type_name == "RectLight") {
    return ExtractRectLightProperties(prim, prim_path, fields, err);
  } else if (type_name == "DiskLight") {
    return ExtractDiskLightProperties(prim, prim_path, fields, err);
  } else if (type_name == "CylinderLight") {
    return ExtractCylinderLightProperties(prim, prim_path, fields, err);
  } else if (type_name == "DistantLight") {
    return ExtractDistantLightProperties(prim, prim_path, fields, err);
  } else if (type_name == "DomeLight") {
    return ExtractDomeLightProperties(prim, prim_path, fields, err);
  } else if (type_name == "GeometryLight") {
    return ExtractGeometryLightProperties(prim, prim_path, fields, err);
  } else if (type_name == "PortalLight") {
    return ExtractPortalLightProperties(prim, prim_path, fields, err);
  } else if (type_name == "DomeLight_1") {
    return ExtractDomeLight1Properties(prim, prim_path, fields, err);
  } else if (type_name == "LightFilter") {
    return ExtractLightFilterProperties(prim, prim_path, fields, err);
  } else if (type_name == "PluginLightFilter") {
    return ExtractPluginLightFilterProperties(prim, prim_path, fields, err);
  } else if (type_name == "Skeleton") {
    return ExtractSkeletonProperties(prim, prim_path, fields, err);
  } else if (type_name == "SkelAnimation") {
    return ExtractSkelAnimationProperties(prim, prim_path, fields, err);
  } else if (type_name == "SkelRoot") {
    return ExtractSkelRootProperties(prim, prim_path, fields, err);
  }

  // UsdPhysics + mjcPhysics
  else if (type_name == "PhysicsJoint") {
    return ExtractPhysicsJointProperties(prim, prim_path, fields, err);
  } else if (type_name == "PhysicsScene") {
    return ExtractPhysicsSceneProperties(prim, prim_path, fields, err);
  } else if (type_name == "PhysicsRevoluteJoint") {
    return ExtractPhysicsRevoluteJointProperties(prim, prim_path, fields, err);
  } else if (type_name == "PhysicsPrismaticJoint") {
    return ExtractPhysicsPrismaticJointProperties(prim, prim_path, fields, err);
  } else if (type_name == "PhysicsSphericalJoint") {
    return ExtractPhysicsSphericalJointProperties(prim, prim_path, fields, err);
  } else if (type_name == "PhysicsFixedJoint") {
    return ExtractPhysicsFixedJointProperties(prim, prim_path, fields, err);
  } else if (type_name == "PhysicsDistanceJoint") {
    return ExtractPhysicsDistanceJointProperties(prim, prim_path, fields, err);
  } else if (type_name == "PhysicsCollisionGroup") {
    return ExtractPhysicsCollisionGroupProperties(prim, prim_path, fields, err);
  } else if (type_name == "MjcActuator") {
    return ExtractMjcActuatorProperties(prim, prim_path, fields, err);
  } else if (type_name == "MjcTendon") {
    return ExtractMjcTendonProperties(prim, prim_path, fields, err);
  } else if (type_name == "MjcKeyframe") {
    return ExtractMjcKeyframeProperties(prim, prim_path, fields, err);
  // AR/Interactive (Apple Preliminary_*)
  } else if (type_name == "Preliminary_PhysicsGravitationalForce") {
    return ExtractPreliminaryGravitationalForceProperties(prim, prim_path, fields, err);
  } else if (type_name == "Preliminary_InfiniteColliderPlane") {
    return ExtractPreliminaryInfiniteColliderPlaneProperties(prim, prim_path, fields, err);
  } else if (type_name == "Preliminary_ReferenceImage") {
    return ExtractPreliminaryReferenceImageProperties(prim, prim_path, fields, err);
  } else if (type_name == "Preliminary_Behavior") {
    return ExtractPreliminaryBehaviorProperties(prim, prim_path, fields, err);
  } else if (type_name == "Preliminary_Trigger") {
    return ExtractPreliminaryTriggerProperties(prim, prim_path, fields, err);
  } else if (type_name == "Preliminary_Action") {
    return ExtractPreliminaryActionProperties(prim, prim_path, fields, err);
  } else if (type_name == "Preliminary_Text") {
    return ExtractPreliminaryTextProperties(prim, prim_path, fields, err);
  // usdMedia
  } else if (type_name == "SpatialAudio") {
    return ExtractSpatialAudioProperties(prim, prim_path, fields, err);
  }

  // For unknown types or types without specific handlers,
  // try extracting from the generic GPrim properties
  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

// ============================================================================
// Value Conversion
// ============================================================================

bool CrateWriter::ConvertValue(
  const value::Value& val,
  crate::CrateValue& out,
  std::string* err
) {
  std::string type_name = val.type_name();

  // Scalar types
  if (type_name == "bool") {
    if (auto v = val.get_value<bool>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int" || type_name == "int32") {
    if (auto v = val.get_value<int32_t>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "uint" || type_name == "uint32") {
    if (auto v = val.get_value<uint32_t>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int64") {
    if (auto v = val.get_value<int64_t>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "uint64") {
    if (auto v = val.get_value<uint64_t>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "float") {
    if (auto v = val.get_value<float>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "double") {
    if (auto v = val.get_value<double>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half") {
    if (auto v = val.get_value<value::half>()) {
      out.Set(*v);
      return true;
    }
  }

  // Token and String types
  else if (type_name == "token") {
    if (auto v = val.get_value<value::token>()) {
      // Store the typed token value; the crate packer pools it through
      // the tokens section at serialization time. Storing the raw pool
      // index (uint) here produced files that tinyusdz's reader surfaced
      // as `var_type='uint' (unresolved)` and that pxrusd rejected with
      // "Corrupt path element token index in crate file".
      out.Set(*v);
      return true;
    }
  } else if (type_name == "string") {
    if (auto v = val.get_value<std::string>()) {
      // Same rationale as the token branch above: store the string value,
      // not the strings-section index.
      out.Set(*v);
      return true;
    }
  } else if (type_name == "asset") {
    if (auto v = val.get_value<value::AssetPath>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "asset[]") {
    if (auto v = val.get_value<std::vector<value::AssetPath>>()) {
      out.Set(*v);
      return true;
    }
  }

  // Vector types - float2/3/4
  else if (type_name == "float2") {
    if (auto v = val.get_value<value::float2>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "float3") {
    if (auto v = val.get_value<value::float3>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "float4") {
    if (auto v = val.get_value<value::float4>()) {
      out.Set(*v);
      return true;
    }
  }

  // Vector types - half2/3/4
  else if (type_name == "half2") {
    if (auto v = val.get_value<value::half2>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half3") {
    if (auto v = val.get_value<value::half3>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half4") {
    if (auto v = val.get_value<value::half4>()) {
      out.Set(*v);
      return true;
    }
  }

  // Vector types - double2/3/4
  else if (type_name == "double2") {
    if (auto v = val.get_value<value::double2>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "double3") {
    if (auto v = val.get_value<value::double3>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "double4") {
    if (auto v = val.get_value<value::double4>()) {
      out.Set(*v);
      return true;
    }
  }

  // Vector types - int2/3/4
  else if (type_name == "int2") {
    if (auto v = val.get_value<value::int2>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int3") {
    if (auto v = val.get_value<value::int3>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int4") {
    if (auto v = val.get_value<value::int4>()) {
      out.Set(*v);
      return true;
    }
  }

  // Role types (point, vector, normal, color)
  // Role types are stored with their underlying type (float3, double3, etc.)
  // Use get_value with non-strict cast to get underlying type
  else if (type_name == "point3f") {
    if (auto v = val.get_value<value::float3>(false)) {  // non-strict cast to underlying type
      out.Set(*v);
      return true;
    }
  } else if (type_name == "point3d") {
    if (auto v = val.get_value<value::double3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "vector3f") {
    if (auto v = val.get_value<value::float3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "vector3d") {
    if (auto v = val.get_value<value::double3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "normal3f") {
    if (auto v = val.get_value<value::float3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "normal3d") {
    if (auto v = val.get_value<value::double3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "color3f") {
    if (auto v = val.get_value<value::float3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "color3d") {
    if (auto v = val.get_value<value::double3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "color4f") {
    if (auto v = val.get_value<value::float4>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "color4d") {
    if (auto v = val.get_value<value::double4>(false)) {
      out.Set(*v);
      return true;
    }
  }

  // Matrix types
  else if (type_name == "matrix2d") {
    if (auto v = val.get_value<value::matrix2d>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "matrix3d") {
    if (auto v = val.get_value<value::matrix3d>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "matrix4d") {
    if (auto v = val.get_value<value::matrix4d>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "frame4d") {
    if (auto v = val.get_value<value::frame4d>(false)) {
      value::matrix4d mat;
      for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
          mat.m[row][col] = (*v).m[row][col];
        }
      }
      out.Set(std::move(mat));
      return true;
    }
  }

  // Quaternion types
  else if (type_name == "quath") {
    if (auto v = val.get_value<value::quath>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "quatf") {
    if (auto v = val.get_value<value::quatf>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "quatd") {
    if (auto v = val.get_value<value::quatd>()) {
      out.Set(*v);
      return true;
    }
  }

  // Array types
  else if (type_name == "int[]") {
    if (auto v = val.get_value<std::vector<int32_t>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "uint[]" || type_name == "uint32[]") {
    if (auto v = val.get_value<std::vector<uint32_t>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "float[]") {
    if (auto v = val.get_value<std::vector<float>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "double[]") {
    if (auto v = val.get_value<std::vector<double>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "double2[]") {
    if (auto v = val.get_value<std::vector<value::double2>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "string[]") {
    if (auto v = val.get_value<std::vector<std::string>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "float3[]") {
    if (auto v = val.get_value<std::vector<value::float3>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "double3[]") {
    if (auto v = val.get_value<std::vector<value::double3>>()) {
      out.Set(*v);
      return true;
    }
  }
  // Role type arrays - convert to underlying type
  else if (type_name == "point3f[]") {
    if (auto v = val.get_value<std::vector<value::float3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "point3d[]") {
    if (auto v = val.get_value<std::vector<value::double3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "normal3f[]") {
    if (auto v = val.get_value<std::vector<value::float3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "normal3d[]") {
    if (auto v = val.get_value<std::vector<value::double3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "vector3f[]") {
    if (auto v = val.get_value<std::vector<value::float3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "vector3d[]") {
    if (auto v = val.get_value<std::vector<value::double3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "color3f[]") {
    if (auto v = val.get_value<std::vector<value::float3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "color3d[]") {
    if (auto v = val.get_value<std::vector<value::double3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "color4f[]") {
    if (auto v = val.get_value<std::vector<value::float4>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "color4d[]") {
    if (auto v = val.get_value<std::vector<value::double4>>(false)) {
      out.Set(*v);
      return true;
    }
  }
  // TexCoord role type arrays
  else if (type_name == "texCoord2f[]") {
    if (auto v = val.get_value<std::vector<value::float2>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "texCoord2d[]") {
    if (auto v = val.get_value<std::vector<value::double2>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "texCoord3f[]") {
    if (auto v = val.get_value<std::vector<value::float3>>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "texCoord3d[]") {
    if (auto v = val.get_value<std::vector<value::double3>>(false)) {
      out.Set(*v);
      return true;
    }
  }
  // Base float2/double2 arrays
  else if (type_name == "float2[]") {
    if (auto v = val.get_value<std::vector<value::float2>>()) {
      out.Set(*v);
      return true;
    }
  }
  // TexCoord scalars
  else if (type_name == "texCoord2f") {
    if (auto v = val.get_value<value::float2>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "texCoord2d") {
    if (auto v = val.get_value<value::double2>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "texCoord3f") {
    if (auto v = val.get_value<value::float3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "texCoord3d") {
    if (auto v = val.get_value<value::double3>(false)) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "token[]") {
    if (auto v = val.get_value<std::vector<value::token>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int64[]") {
    if (auto v = val.get_value<std::vector<int64_t>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "uint64[]") {
    if (auto v = val.get_value<std::vector<uint64_t>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "quath[]") {
    if (auto v = val.get_value<std::vector<value::quath>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "quatf[]") {
    if (auto v = val.get_value<std::vector<value::quatf>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "quatd[]") {
    if (auto v = val.get_value<std::vector<value::quatd>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half[]") {
    if (auto v = val.get_value<std::vector<value::half>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half2[]") {
    if (auto v = val.get_value<std::vector<value::half2>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half3[]") {
    if (auto v = val.get_value<std::vector<value::half3>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half4[]") {
    if (auto v = val.get_value<std::vector<value::half4>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "float4[]") {
    if (auto v = val.get_value<std::vector<value::float4>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "double4[]") {
    if (auto v = val.get_value<std::vector<value::double4>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int2[]") {
    if (auto v = val.get_value<std::vector<value::int2>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int3[]") {
    if (auto v = val.get_value<std::vector<value::int3>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int4[]") {
    if (auto v = val.get_value<std::vector<value::int4>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "matrix4d[]") {
    if (auto v = val.get_value<std::vector<value::matrix4d>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "matrix2d[]") {
    if (auto v = val.get_value<std::vector<value::matrix2d>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "matrix3d[]") {
    if (auto v = val.get_value<std::vector<value::matrix3d>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "frame4d[]") {
    if (auto v = val.get_value<std::vector<value::frame4d>>(false)) {
      std::vector<value::matrix4d> mats;
      mats.reserve(v->size());
      for (const auto &frame : *v) {
        value::matrix4d mat;
        for (size_t row = 0; row < 4; ++row) {
          for (size_t col = 0; col < 4; ++col) {
            mat.m[row][col] = frame.m[row][col];
          }
        }
        mats.emplace_back(std::move(mat));
      }
      out.Set(std::move(mats));
      return true;
    }
  }
  // Half-vector arrays
  else if (type_name == "half2[]") {
    if (auto v = val.get_value<std::vector<value::half2>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half3[]") {
    if (auto v = val.get_value<std::vector<value::half3>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "half4[]") {
    if (auto v = val.get_value<std::vector<value::half4>>()) {
      out.Set(*v);
      return true;
    }
  }
  // Int-vector arrays
  else if (type_name == "int2[]") {
    if (auto v = val.get_value<std::vector<value::int2>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int3[]") {
    if (auto v = val.get_value<std::vector<value::int3>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "int4[]") {
    if (auto v = val.get_value<std::vector<value::int4>>()) {
      out.Set(*v);
      return true;
    }
  } else if (type_name == "bool[]") {
    if (auto v = val.get_value<std::vector<bool>>()) {
      out.Set(*v);
      return true;
    }
  }

  // Type not yet supported
  if (err) {
    *err = "Value conversion not yet implemented for type: " + type_name;
  }
  return false;
}

bool CrateWriter::ConvertPropertyToFields(
    const std::string& prop_name,
    const Property& prop,
    const Path& parent_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {

  // Dispatch based on property type
  if (prop.is_empty()) {
    // Empty property - skip
    return true;
  } else if (prop.is_attribute()) {
    // Convert attribute - creates separate spec, doesn't add to fields
    // Pass the custom flag to be added to the attribute spec
    return ConvertAttributeToFields(prop_name, prop.get_attribute(), parent_path, prop.has_custom(), err);
  } else if (prop.is_relationship()) {
    // Convert relationship - creates separate spec, doesn't add to fields.
    // The list-edit qualifier sits on the outer Property, not on the
    // Relationship itself; copy it across so the writer encodes the
    // ListOp<Path> with the matching prepended/appended/... flag.
    Relationship rel_copy = prop.get_relationship();
    if (rel_copy.get_listedit_qual() == ListEditQual::ResetToExplicit &&
        prop.get_listedit_qual() != ListEditQual::ResetToExplicit) {
      rel_copy.set_listedit_qual(prop.get_listedit_qual());
    }
    return ConvertRelationshipToFields(prop_name, rel_copy, parent_path, err);
  } else if (prop.is_attribute_connection()) {
    // Convert connection - creates separate spec, doesn't add to fields
    return ConvertConnectionToFields(prop_name, prop.get_attribute(), parent_path, err);
  } else {
    if (err) *err = "Unknown property type for: " + prop_name;
    return false;
  }
}

bool CrateWriter::ConvertAttributeToFields(
    const std::string& attr_name,
    const Attribute& attr,
    const Path& parent_path,
    bool is_custom,
    std::string* err) {

  // Create separate spec for this attribute (proper USD Crate format)
  // Attributes are property specs, not prim fields

  crate::FieldValuePairVector attr_fields;

  // Create the attribute path
  Path attr_path = parent_path.AppendProperty(attr_name);

  DCOUT("[ConvertAttributeToFields] Creating separate spec for attribute: "
            << attr_path.full_path_name());

  // 1. Add type name - store as value::token, not as token index!
  if (!attr.type_name().empty()) {
    crate::CrateValue type_value;
    value::token tok(attr.type_name());
    type_value.Set(tok);  // Store the token, not the index!
    attr_fields.push_back({"typeName", type_value});
  }

  // 2. Extract value from PrimVar
  const primvar::PrimVar& pvar = attr.get_var();

  // 2a. Check for blocked value
  if (pvar.is_blocked()) {
    // Add a ValueBlock indicator
    crate::CrateValue blocked_value;
    blocked_value.Set(value::ValueBlock());
    attr_fields.push_back({"default", blocked_value});
  }
  // 2b. Extract default value
  else if (pvar.has_value()) {
    const value::Value& val = pvar.value_raw();
    crate::CrateValue crate_val;

    // Use existing ConvertValue helper
    if (!ConvertValue(val, crate_val, err)) {
      if (err) *err = "Failed to convert attribute value: " + attr_name;
      return false;
    }

    attr_fields.push_back({"default", crate_val});
  }

  // 2c. Extract time samples
  if (pvar.has_timesamples()) {
    // Convert TimeSamples to CrateValue
    const value::TimeSamples& ts = pvar._ts;

    // Create a CrateValue with TimeSamples
    crate::CrateValue ts_crate_val;
    ts_crate_val.Set(ts);

    // Add the timeSamples field
    attr_fields.push_back({"timeSamples", ts_crate_val});

    DCOUT("[ConvertAttributeToFields] Added TimeSamples for " << attr_name
              << " with " << ts.size() << " samples");
  }

  // 3. Add variability if not default - store as Variability enum, not token index
  if (attr.variability() != Variability::Varying) {
    crate::CrateValue var_value;
    var_value.Set(attr.variability());
    attr_fields.push_back({"variability", var_value});
  }

  // 4. Add metadata from AttrMetas
  const AttrMetas& metas = attr.metas();

  // Add interpolation if specified - store as value::token, not token index
  if (metas.has_interpolation()) {
    crate::CrateValue interp_value;
    interp_value.Set(metas.get_interpolation());
    attr_fields.push_back({"interpolation", interp_value});
  }

  // Add hidden if specified
  if (metas.has_hidden()) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.get_hidden());
    attr_fields.push_back({"hidden", hidden_value});
  }

  // Add customData if present
  if (metas.has_customData()) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.get_customData());
    attr_fields.push_back({"customData", custom_data_value});
    DCOUT("[ConvertAttributeToFields] Added customData for " << attr_name
              << " with " << metas.get_customData().size() << " entries");
  }

  // Add custom flag if attribute is custom
  if (is_custom) {
    crate::CrateValue custom_value;
    custom_value.Set(true);  // The field name "custom" with value true indicates custom attribute
    attr_fields.push_back({"custom", custom_value});
    DCOUT("[ConvertAttributeToFields] Added custom flag for " << attr_name);
  }

  // Add displayName / displayGroup / documentation attribute metas
  if (metas.has_displayName()) {
    crate::CrateValue v;
    v.Set(metas.get_displayName());
    attr_fields.push_back({"displayName", v});
  }
  if (metas.has_displayGroup()) {
    crate::CrateValue v;
    v.Set(metas.get_displayGroup());
    attr_fields.push_back({"displayGroup", v});
  }
  if (metas.has_doc()) {
    crate::CrateValue v;
    v.Set(metas.get_doc().value);
    attr_fields.push_back({"documentation", v});
  }
  if (metas.has_allowedTokens()) {
    crate::CrateValue v;
    v.Set(metas.get_allowedTokens());
    attr_fields.push_back({"allowedTokens", v});
  }
  if (metas.has_colorSpace()) {
    crate::CrateValue v;
    v.Set(metas.get_colorSpace());
    attr_fields.push_back({"colorSpace", v});
  }
  if (metas.has_elementSize()) {
    // pxr crate stores elementSize as int (signed); AttrMetas exposes
    // it as uint32_t. Cast on the way out so the reader's int-typed
    // unpack succeeds.
    crate::CrateValue v;
    v.Set(static_cast<int32_t>(metas.get_elementSize()));
    attr_fields.push_back({"elementSize", v});
  }
  if (metas.has_weight()) {
    crate::CrateValue v;
    v.Set(metas.get_weight());
    attr_fields.push_back({"weight", v});
  }
  if (metas.has_comment()) {
    crate::CrateValue v;
    v.Set(metas.get_comment().value);
    attr_fields.push_back({"comment", v});
  }

  // Add attribute connection paths if any.
  if (attr.has_connections()) {
    ListOp<Path> connection_paths_listop;
    connection_paths_listop.ClearAndMakeExplicit();
    connection_paths_listop.SetExplicitItems(attr.connections());
    crate::CrateValue conn_value;
    conn_value.Set(connection_paths_listop);
    attr_fields.push_back({"connectionPaths", conn_value});
    DCOUT("[ConvertAttributeToFields] Added " << attr.connections().size()
          << " connectionPaths for " << attr_name);
  }

  // Create the attribute spec
  if (!AddSpec(attr_path, SpecType::Attribute, attr_fields, err)) {
    if (err) *err = "Failed to add attribute spec: " + attr_path.full_path_name() + ": " + *err;
    return false;
  }

  DCOUT("[ConvertAttributeToFields] Successfully created attribute spec with "
            << attr_fields.size() << " fields");

  return true;
}

bool CrateWriter::ConvertRelationshipToFields(
    const std::string& rel_name,
    const Relationship& rel,
    const Path& parent_path,
    std::string* err) {

  // Create separate spec for this relationship (proper USD Crate format)
  // Relationships are property specs, not prim fields

  crate::FieldValuePairVector rel_fields;

  // Create the relationship path
  Path rel_path = parent_path.AppendProperty(rel_name);

  DCOUT("[ConvertRelationshipToFields] Creating separate spec for relationship: "
            << rel_path.full_path_name());

  // 1. Check relationship type and add targetPaths.
  //
  // pxr Crate stores the list-edit qualifier on the ListOp itself
  // (via SetPrependedItems / SetAppendedItems / SetDeletedItems /
  // SetOrderedItems), not as a separate sibling field. Match that
  // pattern so pxr usdcat (and our own reader) can recover the
  // qualifier on round-trip.
  auto apply_listed_qual = [&](ListOp<Path>& listop,
                                const std::vector<Path>& targets) {
    switch (rel.get_listedit_qual()) {
      case ListEditQual::Append:
        listop.SetAppendedItems(targets);
        break;
      case ListEditQual::Prepend:
        listop.SetPrependedItems(targets);
        break;
      case ListEditQual::Add:
        listop.SetAddedItems(targets);
        break;
      case ListEditQual::Delete:
        listop.SetDeletedItems(targets);
        break;
      case ListEditQual::Order:
        listop.SetOrderedItems(targets);
        break;
      case ListEditQual::ResetToExplicit:
      default:
        listop.SetExplicitItems(targets);
        break;
    }
  };

  if (rel.is_blocked()) {
    // Add ValueBlock for the relationship
    crate::CrateValue blocked_value;
    blocked_value.Set(value::ValueBlock());
    rel_fields.push_back({"targetPaths", blocked_value});
  } else if (rel.is_path()) {
    crate::CrateValue path_value;
    ListOp<Path> listop;
    std::vector<Path> targets{rel.targetPath};
    apply_listed_qual(listop, targets);
    path_value.Set(listop);
    rel_fields.push_back({"targetPaths", path_value});
  } else if (rel.is_pathvector()) {
    crate::CrateValue paths_value;
    ListOp<Path> listop;
    apply_listed_qual(listop, rel.targetPathVector);
    paths_value.Set(listop);
    rel_fields.push_back({"targetPaths", paths_value});
  }
  // DefineOnly relationships don't add targetPaths field

  // 3. Relationships have implicit uniform variability
  // Add it explicitly in the Crate format
  crate::CrateValue var_value;
  Variability var = Variability::Uniform;  // Relationships are always uniform
  var_value.Set(var);
  rel_fields.push_back({"variability", var_value});

  // 4. Add relationship metadata from AttrMetas
  const AttrMeta& metas = rel.metas();

  // Add comment if present
  if (metas.has_comment()) {
    crate::CrateValue comment_value;
    comment_value.Set(metas.get_comment().value);  // StringData.value
    rel_fields.push_back({"comment", comment_value});
  }

  // Add hidden if present
  if (metas.has_hidden()) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.get_hidden());
    rel_fields.push_back({"hidden", hidden_value});
  }

  // Add customData if present
  if (metas.has_customData()) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.get_customData());
    rel_fields.push_back({"customData", custom_data_value});
    DCOUT("[ConvertRelationshipToFields] Added customData for " << rel_name
              << " with " << metas.get_customData().size() << " entries");
  }

  // Add displayName if present
  if (metas.has_displayName()) {
    crate::CrateValue display_name_value;
    display_name_value.Set(metas.get_displayName());
    rel_fields.push_back({"displayName", display_name_value});
  }

  // Add displayGroup if present
  if (metas.has_displayGroup()) {
    crate::CrateValue display_group_value;
    display_group_value.Set(metas.get_displayGroup());
    rel_fields.push_back({"displayGroup", display_group_value});
  }

  // Create the relationship spec
  if (!AddSpec(rel_path, SpecType::Relationship, rel_fields, err)) {
    if (err) *err = "Failed to add relationship spec: " + rel_path.full_path_name() + ": " + *err;
    return false;
  }

  DCOUT("[ConvertRelationshipToFields] Successfully created relationship spec with "
            << rel_fields.size() << " fields");

  return true;
}

bool CrateWriter::ConvertConnectionToFields(
    const std::string& conn_name,
    const Attribute& attr,
    const Path& parent_path,
    std::string* err) {

  // Create separate spec for this connection (proper USD Crate format)
  // Connections are specs at path: parent.AppendProperty(attr_name).AppendProperty("connect")

  // Attribute connections represent ".connect" relationships
  if (!attr.has_connections()) {
    // No connections to process
    return true;
  }

  crate::FieldValuePairVector conn_fields;

  // Create the connection path
  // Connection path is: /Prim.attribute.connect
  Path conn_path = parent_path.AppendProperty(conn_name).AppendProperty("connect");

  DCOUT("[ConvertConnectionToFields] Creating separate spec for connection: "
            << conn_path.full_path_name());

  // 1. Add connection paths (targets)
  const std::vector<Path>& conn_paths = attr.connections();

  if (conn_paths.size() == 1) {
    // Single connection path - wrap in a vector since CrateValue doesn't have Set(Path)
    crate::CrateValue conn_value;
    std::vector<Path> wrapped_path;
    wrapped_path.push_back(conn_paths[0]);
    conn_value.Set(wrapped_path);
    conn_fields.push_back({"targetPaths", conn_value});
  } else if (conn_paths.size() > 1) {
    // Multiple connection paths
    crate::CrateValue conns_value;
    std::vector<Path> paths_copy = conn_paths;
    conns_value.Set(paths_copy);
    conn_fields.push_back({"targetPaths", conns_value});
  }

  // 2. Add type name for the connection (same as attribute type) - store as token!
  if (!attr.type_name().empty()) {
    crate::CrateValue type_value;
    value::token tok(attr.type_name());
    type_value.Set(tok);  // Store the token, not the index!
    conn_fields.push_back({"typeName", type_value});
  }

  // 3. Create the connection spec
  if (!AddSpec(conn_path, SpecType::Connection, conn_fields, err)) {
    if (err) *err = "Failed to add connection spec: " + conn_path.full_path_name() + ": " + *err;
    return false;
  }

  DCOUT("[ConvertConnectionToFields] Successfully created connection spec with "
            << conn_fields.size() << " fields");

  return true;
}

// ============================================================================
// Variant Set and Variant Conversion (USD Composition)
// ============================================================================

bool CrateWriter::ConvertVariantSetToFields(
    const std::string& variantset_name,
    const VariantSet& variantset,
    const Path& parent_path,
    std::string* err) {

  // VariantSet path: parent{variantSetName} (e.g., /Chair{materialVariant})
  std::string variantset_path_str = parent_path.prim_part() + "{" + variantset_name + "}";
  Path vs_path(variantset_path_str, "");

  DCOUT("[ConvertVariantSetToFields] Creating VariantSet spec: "
            << vs_path.full_path_name());

  // VariantSet spec needs "variantChildren" field listing all variant names
  crate::FieldValuePairVector vs_fields;

  std::vector<value::token> variant_tokens;
  for (const auto& variant_item : variantset.variantSet) {
    variant_tokens.push_back(value::token(variant_item.first));
  }

  crate::CrateValue variant_children_value;
  variant_children_value.Set(variant_tokens);
  vs_fields.push_back({"variantChildren", variant_children_value});

  if (!AddSpec(vs_path, SpecType::VariantSet, vs_fields, err)) {
    if (err) *err = "Failed to add VariantSet spec: " + vs_path.full_path_name() + ": " + *err;
    return false;
  }

  DCOUT("[ConvertVariantSetToFields] Created VariantSet with "
            << variantset.variantSet.size() << " variants");

  // For each variant, create a Variant spec
  // IMPORTANT: Pass parent_path (the Prim), not vs_path (the VariantSet)
  // Variant path should be /Prim{variantSet=value}, NOT /Prim{variantSet}{variantSet=value}
  for (const auto& variant_item : variantset.variantSet) {
    const auto& variant_name = variant_item.first;
    const auto& variant_data = variant_item.second;

    if (!ConvertVariantToFields(variant_name, variant_data, parent_path, variantset_name, err)) {
      if (err) *err = "Failed to convert variant '" + variant_name + "': " + *err;
      return false;
    }
  }

  return true;
}

bool CrateWriter::ConvertVariantToFields(
    const std::string& variant_name,
    const Variant& variant,
    const Path& parent_prim_path,  // The prim that owns the variantSet (e.g., /Chair)
    const std::string& variantset_name,
    std::string* err) {

  // Variant path: parent_prim_path{variantSetName=variant_name}
  // (e.g., /Chair{materialVariant=plastic})
  // NOTE: The variant path is based on the prim path, NOT the variantSet path
  std::string variant_path_str = parent_prim_path.prim_part() + "{" +
                                 variantset_name + "=" + variant_name + "}";
  Path v_path(variant_path_str, "");

  DCOUT("[ConvertVariantToFields] Creating Variant spec: "
            << v_path.full_path_name());

  // Variant spec contains variant metadata and prim children
  crate::FieldValuePairVector v_fields;

  // Add variant specifier (always "def" for variants)
  crate::CrateValue spec_value;
  spec_value.Set(Specifier::Def);
  v_fields.push_back({"specifier", spec_value});

  // Add variant properties as separate attribute specs
  for (const auto& prop_item : variant.properties()) {
    const auto& prop_name = prop_item.first;
    const auto& prop = prop_item.second;

    // Handle variant properties based on their type
    if (prop.is_attribute()) {
      if (!ConvertAttributeToFields(prop_name, prop.get_attribute(), v_path, prop.has_custom(), err)) {
        if (err) *err = "Failed to convert variant attribute: " + prop_name;
        return false;
      }
    } else if (prop.is_relationship()) {
      if (!ConvertRelationshipToFields(prop_name, prop.get_relationship(), v_path, err)) {
        if (err) *err = "Failed to convert variant relationship: " + prop_name;
        return false;
      }
    }
  }

  // IMPORTANT: Create the variant spec BEFORE adding child prims
  // The parent spec must exist before children in the spec list
  if (!AddSpec(v_path, SpecType::Variant, v_fields, err)) {
    if (err) *err = "Failed to add Variant spec: " + v_path.full_path_name() + ": " + *err;
    return false;
  }

  DCOUT("[ConvertVariantToFields] Created Variant spec with "
            << v_fields.size() << " fields");

  // Add variant prim children (recursively convert each child prim)
  // Variant prim children are full prims that exist only within this variant
  const auto& child_prims = variant.primChildren();
  if (!child_prims.empty()) {
    DCOUT("[ConvertVariantToFields] Processing " << child_prims.size()
              << " prim children for variant: " << variant_name);
    for (const auto& child_prim : child_prims) {
      // Recursively convert each prim child (they inherit the variant path context)
      if (!ConvertPrimIterative(child_prim, v_path, err)) {
        if (err) *err = "Failed to convert variant prim child: " + child_prim.element_name() + ": " + *err;
        return false;
      }
    }
  }

  // Process nested variant sets
  for (const auto& vs_item : variant.variantSets()) {
    if (!ConvertVariantSetToFields(vs_item.first, vs_item.second, v_path, err)) {
      if (err) *err = "Failed to convert nested VariantSet '" + vs_item.first
                     + "' in variant " + variant_name + ": " + *err;
      return false;
    }
  }

  return true;
}

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
