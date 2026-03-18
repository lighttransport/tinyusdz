// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Stage to Crate Conversion Implementation
//
// This file contains the implementation for converting TinyUSDZ Stage
// to Crate format specs.
//

#include "crate-writer.hh"
#include <iostream>
#include <set>
#include "layer.hh"
#include "common-macros.inc"
#include "pprinter.hh"  // For to_string(Specifier), to_string(Variability)
#include "usdShade.hh"  // For Material and Shader
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

  if (!AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    if (err) *err = "Failed to add root spec: " + *err;
    return false;
  }

  // Convert all root prims
  for (const auto& prim : stage.root_prims()) {
    Path parent_path("/", "");
    if (!ConvertPrimRecursive(prim, parent_path, err)) {
      if (err) *err = "Failed to convert prim: " + *err;
      return false;
    }
  }

  return true;
}

// ============================================================================
// Recursive Prim Conversion
// ============================================================================

bool CrateWriter::ConvertPrimRecursive(
  const Prim& prim,
  const Path& parent_path,
  std::string* err,
  uint32_t depth
) {
  if (size_t(depth) > kMaxDefaultTraversalLimit) {
    if (err) *err = "ConvertPrimRecursive: recursion too deep.";
    return false;
  }

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

  // Extract properties from this prim
  crate::FieldValuePairVector fields;

  if (!ExtractPrimProperties(prim, prim_path, fields, err)) {
    if (err) *err = "Failed to extract properties for " + abs_path_str + ": " + *err;
    return false;
  }

  // Split fields: prim-level fields (specifier, typeName) stay on the prim
  // spec; property fields become separate Attribute specs.
  crate::FieldValuePairVector prim_fields;
  // Collect property fields: name -> {default_value, timeSamples_value}
  struct PropEntry {
    std::string name;
    crate::CrateValue default_val;
    bool has_default{false};
    crate::CrateValue ts_val;
    bool has_ts{false};
    crate::CrateValue variability_val;
    bool has_variability{false};
  };
  std::vector<PropEntry> prop_entries;

  // Set of field names that belong on the prim spec, not as separate attributes
  static const std::set<std::string> kPrimFields = {
    "specifier", "typeName", "primChildren", "properties",
    "variantSetNames", "variantSelection", "kind",
    "active", "hidden", "documentation", "comment", "customData",
    "apiSchemas", "inherits", "specializes", "references", "payload",
  };

  for (auto& fv : fields) {
    if (kPrimFields.count(fv.first)) {
      prim_fields.push_back(std::move(fv));
      continue;
    }

    // Check for ".timeSamples" suffix
    std::string base_name = fv.first;
    bool is_ts = false;
    const std::string ts_suffix = ".timeSamples";
    if (base_name.size() > ts_suffix.size() &&
        base_name.compare(base_name.size() - ts_suffix.size(),
                          ts_suffix.size(), ts_suffix) == 0) {
      base_name = base_name.substr(0, base_name.size() - ts_suffix.size());
      is_ts = true;
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
      prop_entries.push_back({base_name, {}, false, {}, false, {}, false});
      entry = &prop_entries.back();
    }

    if (is_ts) {
      entry->ts_val = std::move(fv.second);
      entry->has_ts = true;
    } else {
      entry->default_val = std::move(fv.second);
      entry->has_default = true;
    }
  }

  // Add spec for this prim (only prim-level fields)
  SpecType spec_type = SpecType::Prim;

  if (!AddSpec(prim_path, spec_type, prim_fields, err)) {
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
    "joints", "bindTransforms", "restTransforms",  // Skeleton
    "inactiveIds",  // PointInstancer
    "purpose",  // GPrim
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

    if (!attr_fields.empty()) {
      if (!AddSpec(attr_path, SpecType::Attribute, attr_fields, err)) {
        DCOUT("WARNING: Failed to add attribute spec for: " << pe.name);
      }
    }
  }

  // After adding the prim spec, handle Material outputs as separate attribute specs
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
        }
      } else if (shader->info_id == "UsdUVTexture") {
        // Try to get UsdUVTexture from shader value
        if (auto* uv_texture = shader->value.as<UsdUVTexture>()) {
          if (!AddUsdUVTextureInputSpecs(uv_texture, prim_path, err)) {
            if (err) *err = "Failed to add UsdUVTexture input specs: " + *err;
            return false;
          }
        }
      } else if (shader->info_id == "UsdTransform2d") {
        // Try to get UsdTransform2d from shader value
        if (auto* transform2d = shader->value.as<UsdTransform2d>()) {
          if (!AddUsdTransform2dInputSpecs(transform2d, prim_path, err)) {
            if (err) *err = "Failed to add UsdTransform2d input specs: " + *err;
            return false;
          }
        }
      } else if (shader->info_id.find("UsdPrimvarReader_") == 0) {
        // Handle all UsdPrimvarReader_* variants (float, float2, float3, etc.)
        if (!AddUsdPrimvarReaderInputSpecs(shader->value, shader->info_id, prim_path, err)) {
          if (err) *err = "Failed to add UsdPrimvarReader input specs: " + *err;
          return false;
        }
      }
    }
  }

  // After adding prim spec, handle material bindings as separate relationship specs
  // This applies to any prim that might have material bindings (typically geometry)
  if (!AddMaterialBindingSpecs(prim, prim_path, err)) {
    // Don't fail on material binding errors - just warn
  }

  // After adding light prim spec, handle light filter relationships as separate relationship specs
  // This applies to any light prim that has light:filters relationships
  if (!AddLightFilterSpecs(prim, prim_path, err)) {
    // Don't fail on light filter relationship errors - just warn
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

  // Recursively process children
  for (const auto& child : prim.children()) {
    if (!ConvertPrimRecursive(child, prim_path, err, depth + 1)) {
      return false;
    }
  }

  return true;
}

// ============================================================================
// Property Extraction
// ============================================================================

template<typename T>
bool CrateWriter::ExtractAnimatableDefault(
    const Animatable<T>& anim, const char* name,
    crate::FieldValuePairVector& fields, std::string* err) {
  if (anim.has_default()) {
    T val;
    if (anim.get_default(&val)) {
      crate::CrateValue crate_val;
      value::Value v(val);
      if (!ConvertValue(v, crate_val, err)) return false;
      fields.push_back({name, crate_val});
    }
  }
  if (anim.has_timesamples()) {
    const auto& typed_ts = anim.get_timesamples();
    value::TimeSamples ts;
    for (size_t i = 0; i < typed_ts.size(); i++) {
      value::Value v(typed_ts.get_samples()[i].value);
      ts.add_sample(typed_ts.get_samples()[i].t, v);
    }
    crate::CrateValue ts_crate_val;
    ts_crate_val.Set(ts);
    fields.push_back({std::string(name) + ".timeSamples", ts_crate_val});
  }
  return true;
}

bool CrateWriter::ExtractPrimProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  // Get prim type name
  std::string type_name = prim.prim_type_name();

  // Add specifier field (as Specifier enum, not as token)
  Specifier spec = prim.specifier();
  // USD files don't allow Invalid specifier - default to Def if we get Invalid
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
  } else if (type_name == "Skeleton") {
    return ExtractSkeletonProperties(prim, prim_path, fields, err);
  } else if (type_name == "SkelAnimation") {
    return ExtractSkelAnimationProperties(prim, prim_path, fields, err);
  } else if (type_name == "SkelRoot") {
    return ExtractSkelRootProperties(prim, prim_path, fields, err);
  }

  // For unknown types or types without specific handlers,
  // try extracting from the generic GPrim properties
  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

// ============================================================================
// Xform Property Extraction
// ============================================================================

bool CrateWriter::ExtractXformProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const Xform* xform = prim.data().as<Xform>();
  if (!xform) {
    if (err) *err = "Failed to cast prim to Xform";
    return false;
  }

  // Extract xformOps from the Xformable base class
  // (Xform inherits from Xformable, so this will handle xformOps and xformOpOrder)
  return ExtractXformOpsFromXformable(prim, prim_path, fields, err);
}

// ============================================================================
// Mesh Property Extraction
// ============================================================================

bool CrateWriter::ExtractMeshProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomMesh* mesh = prim.data().as<GeomMesh>();
  if (!mesh) {
    if (err) *err = "Failed to cast prim to GeomMesh";
    return false;
  }

  // Extract points
  if (mesh->points.has_value()) {
    auto points_animatable = mesh->points.get_value();
    if (points_animatable && points_animatable->has_default()) {
      std::vector<value::point3f> points_val;
      if (points_animatable->get_default(&points_val)) {
        crate::CrateValue crate_val;
        // Convert point3f[] to float3[]
        value::Value points_value(points_val);
        if (ConvertValue(points_value, crate_val, err)) {
          fields.push_back({"points", crate_val});
        }
      }
    }

    // Handle time samples for animated points
    if (points_animatable && points_animatable->has_timesamples()) {
      const auto& typed_ts = points_animatable->get_timesamples();

      // Convert TypedTimeSamples<std::vector<value::point3f>> to value::TimeSamples
      value::TimeSamples ts;
      for (size_t i = 0; i < typed_ts.size(); i++) {
        std::vector<value::point3f> sample_value;
        double time;

        // Get time from the typed timesamples
        time = typed_ts.get_samples()[i].t;
        sample_value = typed_ts.get_samples()[i].value;

        value::Value v(sample_value);
        ts.add_sample(time, v);
      }

      crate::CrateValue ts_crate_val;
      ts_crate_val.Set(ts);
      fields.push_back({"points.timeSamples", ts_crate_val});

    }
  } else {
    // Try extracting from props map as fallback
    for (const auto& prop_pair : mesh->props) {
      if (prop_pair.first == "points") {
        const Property& prop = prop_pair.second;
        if (prop.is_attribute()) {
          const Attribute& attr = prop.get_attribute();
          if (attr.is_value()) {
            const primvar::PrimVar& pvar = attr.get_var();
            const value::Value& val = pvar.value_raw();
            crate::CrateValue crate_val;
            if (ConvertValue(val, crate_val, err)) {
              fields.push_back({"points", crate_val});
            }
          }
        }
      }
    }
  }

  // Extract normals
  if (mesh->normals.has_value()) {
    auto normals_animatable = mesh->normals.get_value();
    if (normals_animatable && normals_animatable->has_default()) {
      std::vector<value::normal3f> normals_val;
      if (normals_animatable->get_default(&normals_val)) {
        crate::CrateValue crate_val;
        value::Value normals_value(normals_val);
        if (ConvertValue(normals_value, crate_val, err)) {
          fields.push_back({"normals", crate_val});
        }
      }
    }
  }

  // Extract faceVertexCounts
  if (mesh->faceVertexCounts.has_value()) {
    auto counts_animatable = mesh->faceVertexCounts.get_value();
    if (counts_animatable && counts_animatable->has_default()) {
      std::vector<int32_t> counts_val;
      if (counts_animatable->get_default(&counts_val)) {
        crate::CrateValue crate_val;
        value::Value counts_value(counts_val);
        if (ConvertValue(counts_value, crate_val, err)) {
          fields.push_back({"faceVertexCounts", crate_val});
        }
      }
    }
  }

  // Extract faceVertexIndices
  if (mesh->faceVertexIndices.has_value()) {
    auto indices_animatable = mesh->faceVertexIndices.get_value();
    if (indices_animatable && indices_animatable->has_default()) {
      std::vector<int32_t> indices_val;
      if (indices_animatable->get_default(&indices_val)) {
        crate::CrateValue crate_val;
        value::Value indices_value(indices_val);
        if (ConvertValue(indices_value, crate_val, err)) {
          fields.push_back({"faceVertexIndices", crate_val});
        }
      }
    }
  }

  // Extract subdivision surface corner properties
  // cornerIndices - indices of corner vertices
  if (mesh->cornerIndices.has_value()) {
    auto corner_indices_animatable = mesh->cornerIndices.get_value();
    if (corner_indices_animatable && corner_indices_animatable->has_default()) {
      std::vector<int32_t> corner_indices_val;
      if (corner_indices_animatable->get_default(&corner_indices_val)) {
        crate::CrateValue crate_val;
        value::Value corner_indices_value(corner_indices_val);
        if (ConvertValue(corner_indices_value, crate_val, err)) {
          fields.push_back({"cornerIndices", crate_val});
        }
      }
    }
  }

  // cornerSharpnesses - sharpness values for corners
  if (mesh->cornerSharpnesses.has_value()) {
    auto corner_sharp_animatable = mesh->cornerSharpnesses.get_value();
    if (corner_sharp_animatable && corner_sharp_animatable->has_default()) {
      std::vector<float> corner_sharp_val;
      if (corner_sharp_animatable->get_default(&corner_sharp_val)) {
        crate::CrateValue crate_val;
        value::Value corner_sharp_value(corner_sharp_val);
        if (ConvertValue(corner_sharp_value, crate_val, err)) {
          fields.push_back({"cornerSharpnesses", crate_val});
        }
      }
    }
  }

  // Extract crease properties
  // creaseIndices - indices of crease edge endpoints
  if (mesh->creaseIndices.has_value()) {
    auto crease_indices_animatable = mesh->creaseIndices.get_value();
    if (crease_indices_animatable && crease_indices_animatable->has_default()) {
      std::vector<int32_t> crease_indices_val;
      if (crease_indices_animatable->get_default(&crease_indices_val)) {
        crate::CrateValue crate_val;
        value::Value crease_indices_value(crease_indices_val);
        if (ConvertValue(crease_indices_value, crate_val, err)) {
          fields.push_back({"creaseIndices", crate_val});
        }
      }
    }
  }

  // creaseLengths - number of edges in each crease chain
  if (mesh->creaseLengths.has_value()) {
    auto crease_lengths_animatable = mesh->creaseLengths.get_value();
    if (crease_lengths_animatable && crease_lengths_animatable->has_default()) {
      std::vector<int32_t> crease_lengths_val;
      if (crease_lengths_animatable->get_default(&crease_lengths_val)) {
        crate::CrateValue crate_val;
        value::Value crease_lengths_value(crease_lengths_val);
        if (ConvertValue(crease_lengths_value, crate_val, err)) {
          fields.push_back({"creaseLengths", crate_val});
        }
      }
    }
  }

  // creaseSharpnesses - sharpness values for creases
  if (mesh->creaseSharpnesses.has_value()) {
    auto crease_sharp_animatable = mesh->creaseSharpnesses.get_value();
    if (crease_sharp_animatable && crease_sharp_animatable->has_default()) {
      std::vector<float> crease_sharp_val;
      if (crease_sharp_animatable->get_default(&crease_sharp_val)) {
        crate::CrateValue crate_val;
        value::Value crease_sharp_value(crease_sharp_val);
        if (ConvertValue(crease_sharp_value, crate_val, err)) {
          fields.push_back({"creaseSharpnesses", crate_val});
        }
      }
    }
  }

  // Extract hole indices
  if (mesh->holeIndices.has_value()) {
    auto hole_indices_animatable = mesh->holeIndices.get_value();
    if (hole_indices_animatable && hole_indices_animatable->has_default()) {
      std::vector<int32_t> hole_indices_val;
      if (hole_indices_animatable->get_default(&hole_indices_val)) {
        crate::CrateValue crate_val;
        value::Value hole_indices_value(hole_indices_val);
        if (ConvertValue(hole_indices_value, crate_val, err)) {
          fields.push_back({"holeIndices", crate_val});
        }
      }
    }
  }

  // Extract subdivision control attributes
  // interpolateBoundary - TypedAttributeWithFallback<Animatable<InterpolateBoundary>>
  if (mesh->interpolateBoundary.has_value()) {
    auto interp_boundary_anim = mesh->interpolateBoundary.get_value();
    if (interp_boundary_anim.has_default()) {
      GeomMesh::InterpolateBoundary interp_val;
      if (interp_boundary_anim.get_default(&interp_val)) {
        crate::CrateValue crate_val;
        value::token tok(to_string(interp_val));
        crate_val.Set(tok);
        fields.push_back({"interpolateBoundary", crate_val});
      }
    }
  }

  // subdivisionScheme - TypedAttributeWithFallback<SubdivisionScheme> (no Animatable!)
  if (mesh->subdivisionScheme.has_value()) {
    const auto& subdiv_scheme = mesh->subdivisionScheme.get_value();
    crate::CrateValue crate_val;
    value::token tok(to_string(subdiv_scheme));
    crate_val.Set(tok);
    fields.push_back({"subdivisionScheme", crate_val});
  }

  // faceVaryingLinearInterpolation - TypedAttributeWithFallback<Animatable<FaceVaryingLinearInterpolation>>
  if (mesh->faceVaryingLinearInterpolation.has_value()) {
    auto fv_interp_anim = mesh->faceVaryingLinearInterpolation.get_value();
    if (fv_interp_anim.has_default()) {
      GeomMesh::FaceVaryingLinearInterpolation fv_interp_val;
      if (fv_interp_anim.get_default(&fv_interp_val)) {
        crate::CrateValue crate_val;
        value::token tok(to_string(fv_interp_val));
        crate_val.Set(tok);
        fields.push_back({"faceVaryingLinearInterpolation", crate_val});
      }
    }
  }

  // Extract blend shapes (token array)
  if (mesh->blendShapes.has_value()) {
    auto blend_shapes_val = mesh->blendShapes.get_value();
    if (blend_shapes_val) {
      crate::CrateValue crate_val;
      value::Value val(*blend_shapes_val);
      if (ConvertValue(val, crate_val, err)) {
        fields.push_back({"blendShapes", crate_val});
      }
    }
  }

  // Extract skeleton relationship
  if (mesh->skeleton) {
    // Relationships are handled separately
  }

  // Extract blend shape targets relationship
  if (mesh->blendShapeTargets) {
    // Relationships are handled separately
  }

  // Extract common GPrim properties
  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

// ============================================================================
// Cube Property Extraction
// ============================================================================

bool CrateWriter::ExtractCubeProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCube* cube = prim.data().as<GeomCube>();
  if (!cube) {
    if (err) *err = "Failed to cast prim to GeomCube";
    return false;
  }

  if (cube->size.authored()) {
    if (!ExtractAnimatableDefault(cube->size.get_value(), "size", fields, err)) return false;
  }

  // Extract extent
  if (cube->extent.has_value()) {
    auto extent_animatable = cube->extent.get_value();
    if (extent_animatable && extent_animatable->has_default()) {
      Extent extent_val;
      if (extent_animatable->get_default(&extent_val)) {
        // Extent is a pair of float3
        crate::CrateValue crate_val;
        // Create array of 2 float3 values
        std::vector<value::float3> extent_array = {extent_val.lower, extent_val.upper};
        value::Value extent_value(extent_array);
        if (ConvertValue(extent_value, crate_val, err)) {
          fields.push_back({"extent", crate_val});
        }
      }
    }
  }

  // Extract common GPrim properties (including xformOps)
  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

// ============================================================================
// Sphere Property Extraction
// ============================================================================

bool CrateWriter::ExtractSphereProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomSphere* sphere = prim.data().as<GeomSphere>();
  if (!sphere) {
    if (err) *err = "Failed to cast prim to GeomSphere";
    return false;
  }

  if (sphere->radius.authored()) {
    if (!ExtractAnimatableDefault(sphere->radius.get_value(), "radius", fields, err)) return false;
  }

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

// ============================================================================
// Cylinder Property Extraction
// ============================================================================

bool CrateWriter::ExtractCylinderProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCylinder* cylinder = prim.data().as<GeomCylinder>();
  if (!cylinder) {
    if (err) *err = "Failed to cast prim to GeomCylinder";
    return false;
  }

  if (cylinder->radius.authored()) {
    if (!ExtractAnimatableDefault(cylinder->radius.get_value(), "radius", fields, err)) return false;
  }
  if (cylinder->height.authored()) {
    if (!ExtractAnimatableDefault(cylinder->height.get_value(), "height", fields, err)) return false;
  }

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

bool CrateWriter::ExtractConeProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCone* cone = prim.data().as<GeomCone>();
  if (!cone) {
    if (err) *err = "Failed to cast prim to GeomCone";
    return false;
  }

  if (cone->radius.authored()) {
    if (!ExtractAnimatableDefault(cone->radius.get_value(), "radius", fields, err)) return false;
  }
  if (cone->height.authored()) {
    if (!ExtractAnimatableDefault(cone->height.get_value(), "height", fields, err)) return false;
  }

  // Extract axis
  if (cone->axis.authored()) {
    const Axis& axis_val = cone->axis.get_value();
    std::string axis_str = to_string(axis_val);
    crate::CrateValue axis_crate_val;
    value::token axis_token(axis_str);
    axis_crate_val.Set(axis_token);
    fields.push_back({"axis", axis_crate_val});
  }

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

bool CrateWriter::ExtractCapsuleProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCapsule* capsule = prim.data().as<GeomCapsule>();
  if (!capsule) {
    if (err) *err = "Failed to cast prim to GeomCapsule";
    return false;
  }

  if (capsule->radius.authored()) {
    if (!ExtractAnimatableDefault(capsule->radius.get_value(), "radius", fields, err)) return false;
  }
  if (capsule->height.authored()) {
    if (!ExtractAnimatableDefault(capsule->height.get_value(), "height", fields, err)) return false;
  }

  // Extract axis
  if (capsule->axis.authored()) {
    const Axis& axis_val = capsule->axis.get_value();
    std::string axis_str = to_string(axis_val);
    crate::CrateValue axis_crate_val;
    value::token axis_token(axis_str);
    axis_crate_val.Set(axis_token);
    fields.push_back({"axis", axis_crate_val});
  }

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

bool CrateWriter::ExtractPointsProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomPoints* points = prim.data().as<GeomPoints>();
  if (!points) {
    if (err) *err = "Failed to cast prim to GeomPoints";
    return false;
  }

  // Helper lambda to convert array values
  auto add_array_attribute = [&](const std::string& attr_name, const value::Value& val) -> bool {
    crate::CrateValue crate_val;
    return ConvertValue(val, crate_val, err) &&
           (fields.push_back({attr_name, crate_val}), true);
  };

  // Extract points (point3f[]) - TypedAttribute returns optional
  if (points->points.authored()) {
    auto points_opt = points->points.get_value();
    if (points_opt.has_value()) {
      const Animatable<std::vector<value::point3f>>& points_anim = points_opt.value();
      if (points_anim.has_default()) {
        std::vector<value::point3f> points_val;
        if (points_anim.get_default(&points_val)) {
          value::Value points_value(points_val);
          if (!add_array_attribute("points", points_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract widths (float[]) - TypedAttribute returns optional
  if (points->widths.authored()) {
    auto widths_opt = points->widths.get_value();
    if (widths_opt.has_value()) {
      const Animatable<std::vector<float>>& widths_anim = widths_opt.value();
      if (widths_anim.has_default()) {
        std::vector<float> widths_val;
        if (widths_anim.get_default(&widths_val)) {
          value::Value widths_value(widths_val);
          if (!add_array_attribute("widths", widths_value)) {
            return false;
          }
        }
      }
    }
  }

  // Note: ids (int64[]) is not yet supported by ConvertValue
  // This can be added in future when int64[] conversion is implemented

  // Extract normals (normal3f[]) - TypedAttribute returns optional
  if (points->normals.authored()) {
    auto normals_opt = points->normals.get_value();
    if (normals_opt.has_value()) {
      const Animatable<std::vector<value::normal3f>>& normals_anim = normals_opt.value();
      if (normals_anim.has_default()) {
        std::vector<value::normal3f> normals_val;
        if (normals_anim.get_default(&normals_val)) {
          value::Value normals_value(normals_val);
          if (!add_array_attribute("normals", normals_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract velocities (vector3f[]) - TypedAttribute returns optional
  if (points->velocities.authored()) {
    auto velocities_opt = points->velocities.get_value();
    if (velocities_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& velocities_anim = velocities_opt.value();
      if (velocities_anim.has_default()) {
        std::vector<value::vector3f> velocities_val;
        if (velocities_anim.get_default(&velocities_val)) {
          value::Value velocities_value(velocities_val);
          if (!add_array_attribute("velocities", velocities_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract accelerations (vector3f[]) - TypedAttribute returns optional
  if (points->accelerations.authored()) {
    auto accelerations_opt = points->accelerations.get_value();
    if (accelerations_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& accelerations_anim = accelerations_opt.value();
      if (accelerations_anim.has_default()) {
        std::vector<value::vector3f> accelerations_val;
        if (accelerations_anim.get_default(&accelerations_val)) {
          value::Value accelerations_value(accelerations_val);
          if (!add_array_attribute("accelerations", accelerations_value)) {
            return false;
          }
        }
      }
    }
  }

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

bool CrateWriter::ExtractCameraProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCamera* camera = prim.data().as<GeomCamera>();
  if (!camera) {
    if (err) *err = "Failed to cast prim to GeomCamera";
    return false;
  }

  // Helper lambda to add float attributes with fallback
  // TypedAttributeWithFallback<Animatable<T>>::get_value() returns const Animatable<T>&
  auto add_float_attr = [&](const std::string& name, const TypedAttributeWithFallback<Animatable<float>>& attr) -> bool {
    const Animatable<float>& anim = attr.get_value();  // Returns either authored or fallback value
    float scalar_val;
    if (anim.get_scalar(&scalar_val)) {
      crate::CrateValue crate_val;
      crate_val.Set(scalar_val);
      fields.push_back({name, crate_val});
      return true;
    }
    return true;
  };

  // Helper lambda to add double attributes with fallback
  // TypedAttributeWithFallback<Animatable<T>>::get_value() returns const Animatable<T>&
  auto add_double_attr = [&](const std::string& name, const TypedAttributeWithFallback<Animatable<double>>& attr) -> bool {
    const Animatable<double>& anim = attr.get_value();  // Returns either authored or fallback value
    double scalar_val;
    if (anim.get_scalar(&scalar_val)) {
      crate::CrateValue crate_val;
      crate_val.Set(scalar_val);
      fields.push_back({name, crate_val});
      return true;
    }
    return true;
  };

  // Extract float2 clippingRange
  // clippingRange is TypedAttributeWithFallback<Animatable<float2>>, so get_value() returns the Animatable
  {
    const Animatable<value::float2>& anim = camera->clippingRange.get_value();
    value::float2 range_val;
    if (anim.get_scalar(&range_val)) {
      crate::CrateValue crate_val;
      crate_val.Set(range_val);
      fields.push_back({"clippingRange", crate_val});
    }
  }

  // Extract exposure (float)
  add_float_attr("exposure", camera->exposure);

  // Extract focalLength (float, default 50.0mm)
  add_float_attr("focalLength", camera->focalLength);

  // Extract focusDistance (float)
  add_float_attr("focusDistance", camera->focusDistance);

  // Extract horizontalAperture (float)
  add_float_attr("horizontalAperture", camera->horizontalAperture);

  // Extract horizontalApertureOffset (float)
  add_float_attr("horizontalApertureOffset", camera->horizontalApertureOffset);

  // Extract verticalAperture (float)
  add_float_attr("verticalAperture", camera->verticalAperture);

  // Extract verticalApertureOffset (float)
  add_float_attr("verticalApertureOffset", camera->verticalApertureOffset);

  // Extract fStop (float, 0.0 = no focusing)
  add_float_attr("fStop", camera->fStop);

  // Extract projection (token enum)
  // projection is TypedAttributeWithFallback<Animatable<Projection>>, so get_value() returns the Animatable
  {
    const Animatable<GeomCamera::Projection>& proj_anim = camera->projection.get_value();
    GeomCamera::Projection proj_val;
    if (proj_anim.get_scalar(&proj_val)) {
      std::string proj_str = (proj_val == GeomCamera::Projection::Perspective) ? "perspective" : "orthographic";
      crate::CrateValue crate_val;
      value::token tok(proj_str);
      crate_val.Set(tok);
      fields.push_back({"projection", crate_val});
    }
  }

  // Extract stereoRole (uniform token enum)
  // stereoRole is TypedAttributeWithFallback<StereoRole>, so get_value() returns the StereoRole
  {
    std::string stereo_str;
    const GeomCamera::StereoRole& stereo = camera->stereoRole.get_value();
    if (stereo == GeomCamera::StereoRole::Left) {
      stereo_str = "left";
    } else if (stereo == GeomCamera::StereoRole::Right) {
      stereo_str = "right";
    } else {
      stereo_str = "mono";  // default
    }
    crate::CrateValue crate_val;
    value::token tok(stereo_str);
    crate_val.Set(tok);
    fields.push_back({"stereoRole", crate_val});
  }

  // Extract shutterOpen (double)
  add_double_attr("shutterOpen", camera->shutterOpen);

  // Extract shutterClose (double)
  add_double_attr("shutterClose", camera->shutterClose);

  // Extract clippingPlanes (float4[]) if present - TypedAttribute (no fallback)
  if (camera->clippingPlanes.authored()) {
    auto planes_opt = camera->clippingPlanes.get_value();
    if (planes_opt.has_value()) {
      const Animatable<std::vector<value::float4>>& planes_anim = planes_opt.value();
      std::vector<value::float4> planes_val;
      if (planes_anim.get_default(&planes_val)) {
        crate::CrateValue crate_val;
        if (ConvertValue(value::Value(planes_val), crate_val, err)) {
          fields.push_back({"clippingPlanes", crate_val});
        }
      }
    }
  }

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

bool CrateWriter::ExtractBasisCurvesProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomBasisCurves* basis_curves = prim.data().as<GeomBasisCurves>();
  if (!basis_curves) {
    if (err) *err = "Failed to cast prim to GeomBasisCurves";
    return false;
  }

  // Helper lambda to convert enum to token
  auto add_enum_attribute = [&](const std::string& attr_name, const std::string& enum_val) -> bool {
    crate::CrateValue crate_val;
    value::token tok(enum_val);
    crate_val.Set(tok);
    fields.push_back({attr_name, crate_val});
    return true;
  };

  // Helper lambda to convert array values
  auto add_array_attribute = [&](const std::string& attr_name, const value::Value& val) -> bool {
    crate::CrateValue crate_val;
    return ConvertValue(val, crate_val, err) &&
           (fields.push_back({attr_name, crate_val}), true);
  };

  // Extract type enum (Cubic/Linear)
  {
    const GeomBasisCurves::Type& type_val = basis_curves->type.get_value();
    std::string type_str = (type_val == GeomBasisCurves::Type::Cubic) ? "cubic" : "linear";
    if (!add_enum_attribute("type", type_str)) {
      return false;
    }
  }

  // Extract basis enum (Bezier/Bspline/CatmullRom)
  {
    const GeomBasisCurves::Basis& basis_val = basis_curves->basis.get_value();
    std::string basis_str;
    if (basis_val == GeomBasisCurves::Basis::Bezier) {
      basis_str = "bezier";
    } else if (basis_val == GeomBasisCurves::Basis::Bspline) {
      basis_str = "bspline";
    } else {  // CatmullRom
      basis_str = "catmullRom";
    }
    if (!add_enum_attribute("basis", basis_str)) {
      return false;
    }
  }

  // Extract wrap enum (Nonperiodic/Periodic/Pinned)
  {
    const GeomBasisCurves::Wrap& wrap_val = basis_curves->wrap.get_value();
    std::string wrap_str;
    if (wrap_val == GeomBasisCurves::Wrap::Nonperiodic) {
      wrap_str = "nonperiodic";
    } else if (wrap_val == GeomBasisCurves::Wrap::Periodic) {
      wrap_str = "periodic";
    } else {  // Pinned
      wrap_str = "pinned";
    }
    if (!add_enum_attribute("wrap", wrap_str)) {
      return false;
    }
  }

  // Extract points (point3f[]) - TypedAttribute returns optional
  if (basis_curves->points.authored()) {
    auto points_opt = basis_curves->points.get_value();
    if (points_opt.has_value()) {
      const Animatable<std::vector<value::point3f>>& points_anim = points_opt.value();
      if (points_anim.has_default()) {
        std::vector<value::point3f> points_val;
        if (points_anim.get_default(&points_val)) {
          value::Value points_value(points_val);
          if (!add_array_attribute("points", points_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract curveVertexCounts (int[]) - TypedAttribute returns optional
  if (basis_curves->curveVertexCounts.authored()) {
    auto counts_opt = basis_curves->curveVertexCounts.get_value();
    if (counts_opt.has_value()) {
      const Animatable<std::vector<int>>& counts_anim = counts_opt.value();
      if (counts_anim.has_default()) {
        std::vector<int> counts_val;
        if (counts_anim.get_default(&counts_val)) {
          value::Value counts_value(counts_val);
          if (!add_array_attribute("curveVertexCounts", counts_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract widths (float[]) - TypedAttribute returns optional
  if (basis_curves->widths.authored()) {
    auto widths_opt = basis_curves->widths.get_value();
    if (widths_opt.has_value()) {
      const Animatable<std::vector<float>>& widths_anim = widths_opt.value();
      if (widths_anim.has_default()) {
        std::vector<float> widths_val;
        if (widths_anim.get_default(&widths_val)) {
          value::Value widths_value(widths_val);
          if (!add_array_attribute("widths", widths_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract normals (normal3f[]) - TypedAttribute returns optional
  if (basis_curves->normals.authored()) {
    auto normals_opt = basis_curves->normals.get_value();
    if (normals_opt.has_value()) {
      const Animatable<std::vector<value::normal3f>>& normals_anim = normals_opt.value();
      if (normals_anim.has_default()) {
        std::vector<value::normal3f> normals_val;
        if (normals_anim.get_default(&normals_val)) {
          value::Value normals_value(normals_val);
          if (!add_array_attribute("normals", normals_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract velocities (vector3f[]) - TypedAttribute returns optional
  if (basis_curves->velocities.authored()) {
    auto velocities_opt = basis_curves->velocities.get_value();
    if (velocities_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& velocities_anim = velocities_opt.value();
      if (velocities_anim.has_default()) {
        std::vector<value::vector3f> velocities_val;
        if (velocities_anim.get_default(&velocities_val)) {
          value::Value velocities_value(velocities_val);
          if (!add_array_attribute("velocities", velocities_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract accelerations (vector3f[]) - TypedAttribute returns optional
  if (basis_curves->accelerations.authored()) {
    auto accelerations_opt = basis_curves->accelerations.get_value();
    if (accelerations_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& accelerations_anim = accelerations_opt.value();
      if (accelerations_anim.has_default()) {
        std::vector<value::vector3f> accelerations_val;
        if (accelerations_anim.get_default(&accelerations_val)) {
          value::Value accelerations_value(accelerations_val);
          if (!add_array_attribute("accelerations", accelerations_value)) {
            return false;
          }
        }
      }
    }
  }

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

bool CrateWriter::ExtractNurbsCurvesProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomNurbsCurves* nurbs_curves = prim.data().as<GeomNurbsCurves>();
  if (!nurbs_curves) {
    if (err) *err = "Failed to cast prim to GeomNurbsCurves";
    return false;
  }

  // Helper lambda to convert array values
  auto add_array_attribute = [&](const std::string& attr_name, const value::Value& val) -> bool {
    crate::CrateValue crate_val;
    return ConvertValue(val, crate_val, err) &&
           (fields.push_back({attr_name, crate_val}), true);
  };

  // Extract points (point3f[]) - TypedAttribute returns optional
  if (nurbs_curves->points.authored()) {
    auto points_opt = nurbs_curves->points.get_value();
    if (points_opt.has_value()) {
      const Animatable<std::vector<value::point3f>>& points_anim = points_opt.value();
      if (points_anim.has_default()) {
        std::vector<value::point3f> points_val;
        if (points_anim.get_default(&points_val)) {
          value::Value points_value(points_val);
          if (!add_array_attribute("points", points_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract order (int[]) - NURBS curve order per curve
  if (nurbs_curves->order.authored()) {
    auto order_opt = nurbs_curves->order.get_value();
    if (order_opt.has_value()) {
      const Animatable<std::vector<int>>& order_anim = order_opt.value();
      if (order_anim.has_default()) {
        std::vector<int> order_val;
        if (order_anim.get_default(&order_val)) {
          value::Value order_value(order_val);
          if (!add_array_attribute("order", order_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract knots (double[]) - NURBS knot vector
  if (nurbs_curves->knots.authored()) {
    auto knots_opt = nurbs_curves->knots.get_value();
    if (knots_opt.has_value()) {
      const Animatable<std::vector<double>>& knots_anim = knots_opt.value();
      if (knots_anim.has_default()) {
        std::vector<double> knots_val;
        if (knots_anim.get_default(&knots_val)) {
          value::Value knots_value(knots_val);
          if (!add_array_attribute("knots", knots_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract ranges (double2[]) - NURBS curve parameter ranges
  if (nurbs_curves->ranges.authored()) {
    auto ranges_opt = nurbs_curves->ranges.get_value();
    if (ranges_opt.has_value()) {
      const Animatable<std::vector<value::double2>>& ranges_anim = ranges_opt.value();
      if (ranges_anim.has_default()) {
        std::vector<value::double2> ranges_val;
        if (ranges_anim.get_default(&ranges_val) && !ranges_val.empty()) {
          value::Value ranges_value(ranges_val);
          if (!add_array_attribute("ranges", ranges_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract pointWeights (double[]) - Weights for curve control points
  if (nurbs_curves->pointWeights.authored()) {
    auto weights_opt = nurbs_curves->pointWeights.get_value();
    if (weights_opt.has_value()) {
      const Animatable<std::vector<double>>& weights_anim = weights_opt.value();
      if (weights_anim.has_default()) {
        std::vector<double> weights_val;
        if (weights_anim.get_default(&weights_val)) {
          value::Value weights_value(weights_val);
          if (!add_array_attribute("pointWeights", weights_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract curveVertexCounts (int[]) - Vertices per curve
  if (nurbs_curves->curveVertexCounts.authored()) {
    auto counts_opt = nurbs_curves->curveVertexCounts.get_value();
    if (counts_opt.has_value()) {
      const Animatable<std::vector<int>>& counts_anim = counts_opt.value();
      if (counts_anim.has_default()) {
        std::vector<int> counts_val;
        if (counts_anim.get_default(&counts_val)) {
          value::Value counts_value(counts_val);
          if (!add_array_attribute("curveVertexCounts", counts_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract widths (float[]) - TypedAttribute returns optional
  if (nurbs_curves->widths.authored()) {
    auto widths_opt = nurbs_curves->widths.get_value();
    if (widths_opt.has_value()) {
      const Animatable<std::vector<float>>& widths_anim = widths_opt.value();
      if (widths_anim.has_default()) {
        std::vector<float> widths_val;
        if (widths_anim.get_default(&widths_val)) {
          value::Value widths_value(widths_val);
          if (!add_array_attribute("widths", widths_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract normals (normal3f[]) - TypedAttribute returns optional
  if (nurbs_curves->normals.authored()) {
    auto normals_opt = nurbs_curves->normals.get_value();
    if (normals_opt.has_value()) {
      const Animatable<std::vector<value::normal3f>>& normals_anim = normals_opt.value();
      if (normals_anim.has_default()) {
        std::vector<value::normal3f> normals_val;
        if (normals_anim.get_default(&normals_val)) {
          value::Value normals_value(normals_val);
          if (!add_array_attribute("normals", normals_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract velocities (vector3f[]) - TypedAttribute returns optional
  if (nurbs_curves->velocities.authored()) {
    auto velocities_opt = nurbs_curves->velocities.get_value();
    if (velocities_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& velocities_anim = velocities_opt.value();
      if (velocities_anim.has_default()) {
        std::vector<value::vector3f> velocities_val;
        if (velocities_anim.get_default(&velocities_val)) {
          value::Value velocities_value(velocities_val);
          if (!add_array_attribute("velocities", velocities_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract accelerations (vector3f[]) - TypedAttribute returns optional
  if (nurbs_curves->accelerations.authored()) {
    auto accelerations_opt = nurbs_curves->accelerations.get_value();
    if (accelerations_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& accelerations_anim = accelerations_opt.value();
      if (accelerations_anim.has_default()) {
        std::vector<value::vector3f> accelerations_val;
        if (accelerations_anim.get_default(&accelerations_val)) {
          value::Value accelerations_value(accelerations_val);
          if (!add_array_attribute("accelerations", accelerations_value)) {
            return false;
          }
        }
      }
    }
  }

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

bool CrateWriter::ExtractPointInstancerProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomPointInstancer* instancer = prim.data().as<GeomPointInstancer>();
  if (!instancer) {
    if (err) *err = "Failed to cast prim to GeomPointInstancer";
    return false;
  }

  // Helper lambda to convert array values
  auto add_array_attribute = [&](const std::string& attr_name, const value::Value& val) -> bool {
    crate::CrateValue crate_val;
    return ConvertValue(val, crate_val, err) &&
           (fields.push_back({attr_name, crate_val}), true);
  };

  // Extract protoIndices (int32[]) - Indices into prototypes array
  if (instancer->protoIndices.authored()) {
    auto indices_opt = instancer->protoIndices.get_value();
    if (indices_opt.has_value()) {
      const Animatable<std::vector<int32_t>>& indices_anim = indices_opt.value();
      if (indices_anim.has_default()) {
        std::vector<int32_t> indices_val;
        if (indices_anim.get_default(&indices_val)) {
          value::Value indices_value(indices_val);
          if (!add_array_attribute("protoIndices", indices_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract positions (point3f[]) - Instance positions
  if (instancer->positions.authored()) {
    auto positions_opt = instancer->positions.get_value();
    if (positions_opt.has_value()) {
      const Animatable<std::vector<value::point3f>>& positions_anim = positions_opt.value();
      if (positions_anim.has_default()) {
        std::vector<value::point3f> positions_val;
        if (positions_anim.get_default(&positions_val)) {
          value::Value positions_value(positions_val);
          if (!add_array_attribute("positions", positions_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract scales (float3[]) - Instance scales
  if (instancer->scales.authored()) {
    auto scales_opt = instancer->scales.get_value();
    if (scales_opt.has_value()) {
      const Animatable<std::vector<value::float3>>& scales_anim = scales_opt.value();
      if (scales_anim.has_default()) {
        std::vector<value::float3> scales_val;
        if (scales_anim.get_default(&scales_val)) {
          value::Value scales_value(scales_val);
          if (!add_array_attribute("scales", scales_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract velocities (vector3f[]) - Instance velocities
  if (instancer->velocities.authored()) {
    auto velocities_opt = instancer->velocities.get_value();
    if (velocities_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& velocities_anim = velocities_opt.value();
      if (velocities_anim.has_default()) {
        std::vector<value::vector3f> velocities_val;
        if (velocities_anim.get_default(&velocities_val)) {
          value::Value velocities_value(velocities_val);
          if (!add_array_attribute("velocities", velocities_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract accelerations (vector3f[]) - Instance accelerations
  if (instancer->accelerations.authored()) {
    auto accelerations_opt = instancer->accelerations.get_value();
    if (accelerations_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& accelerations_anim = accelerations_opt.value();
      if (accelerations_anim.has_default()) {
        std::vector<value::vector3f> accelerations_val;
        if (accelerations_anim.get_default(&accelerations_val)) {
          value::Value accelerations_value(accelerations_val);
          if (!add_array_attribute("accelerations", accelerations_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract angularVelocities (vector3f[]) - Instance angular velocities
  if (instancer->angularVelocities.authored()) {
    auto ang_vel_opt = instancer->angularVelocities.get_value();
    if (ang_vel_opt.has_value()) {
      const Animatable<std::vector<value::vector3f>>& ang_vel_anim = ang_vel_opt.value();
      if (ang_vel_anim.has_default()) {
        std::vector<value::vector3f> ang_vel_val;
        if (ang_vel_anim.get_default(&ang_vel_val)) {
          value::Value ang_vel_value(ang_vel_val);
          if (!add_array_attribute("angularVelocities", ang_vel_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract ids (int64[]) - Instance identifiers
  if (instancer->ids.authored()) {
    auto ids_opt = instancer->ids.get_value();
    if (ids_opt.has_value()) {
      const Animatable<std::vector<int64_t>>& ids_anim = ids_opt.value();
      if (ids_anim.has_default()) {
        std::vector<int64_t> ids_val;
        if (ids_anim.get_default(&ids_val)) {
          value::Value ids_value(ids_val);
          if (!add_array_attribute("ids", ids_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract orientations (quath[]) - Instance orientations as quaternions (half precision)
  if (instancer->orientations.authored()) {
    auto orientations_opt = instancer->orientations.get_value();
    if (orientations_opt.has_value()) {
      const Animatable<std::vector<value::quath>>& orientations_anim = orientations_opt.value();
      if (orientations_anim.has_default()) {
        std::vector<value::quath> orientations_val;
        if (orientations_anim.get_default(&orientations_val)) {
          value::Value orientations_value(orientations_val);
          if (!add_array_attribute("orientations", orientations_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract invisibleIds (int64[]) - IDs of instances to hide
  if (instancer->invisibleIds.authored()) {
    auto invisible_opt = instancer->invisibleIds.get_value();
    if (invisible_opt.has_value()) {
      const Animatable<std::vector<int64_t>>& invisible_anim = invisible_opt.value();
      if (invisible_anim.has_default()) {
        std::vector<int64_t> invisible_val;
        if (invisible_anim.get_default(&invisible_val)) {
          value::Value invisible_value(invisible_val);
          if (!add_array_attribute("invisibleIds", invisible_value)) {
            return false;
          }
        }
      }
    }
  }

  // Extract inactiveIds (int64[]) - IDs of instances to deactivate
  if (instancer->inactiveIds.authored()) {
    std::vector<int64_t> inactive_val;
    if (instancer->inactiveIds.get_value(&inactive_val)) {
      value::Value inactive_value(inactive_val);
      if (!add_array_attribute("inactiveIds", inactive_value)) {
        return false;
      }
    }
  }

  // Note: prototypes (relationship) - handled separately in AddPointInstancerPrototypesSpec

  return ExtractGPrimProperties(prim, prim_path, fields, err);
}

bool CrateWriter::ExtractGeomSubsetProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomSubset* subset = prim.data().as<GeomSubset>();
  if (!subset) {
    if (err) *err = "Failed to cast prim to GeomSubset";
    return false;
  }

  // Helper lambda to convert enum to token
  auto add_enum_attribute = [&](const std::string& attr_name, const std::string& enum_val) -> bool {
    crate::CrateValue crate_val;
    value::token tok(enum_val);
    crate_val.Set(tok);
    fields.push_back({attr_name, crate_val});
    return true;
  };

  // Helper lambda to convert array values
  auto add_array_attribute = [&](const std::string& attr_name, const value::Value& val) -> bool {
    crate::CrateValue crate_val;
    return ConvertValue(val, crate_val, err) &&
           (fields.push_back({attr_name, crate_val}), true);
  };

  // Extract elementType enum (Face/Point/Edge/Tetrahedron)
  {
    const GeomSubset::ElementType& elem_type = subset->elementType.get_value();
    std::string elem_str;
    switch (elem_type) {
      case GeomSubset::ElementType::Face: elem_str = "face"; break;
      case GeomSubset::ElementType::Point: elem_str = "point"; break;
      case GeomSubset::ElementType::Edge: elem_str = "edge"; break;
      case GeomSubset::ElementType::Tetrahedron: elem_str = "tetrahedron"; break;
    }
    if (!add_enum_attribute("elementType", elem_str)) {
      return false;
    }
  }

  // Extract familyName token if authored
  if (subset->familyName.authored()) {
    auto familyname_opt = subset->familyName.get_value();
    if (familyname_opt.has_value()) {
      const value::token& familyname_val = familyname_opt.value();
      crate::CrateValue crate_val;
      crate_val.Set(familyname_val);
      fields.push_back({"familyName", crate_val});
    }
  }

  // Extract indices (int[]) - TypedAttribute returns optional
  if (subset->indices.authored()) {
    auto indices_opt = subset->indices.get_value();
    if (indices_opt.has_value()) {
      const Animatable<std::vector<int32_t>>& indices_anim = indices_opt.value();
      if (indices_anim.has_default()) {
        std::vector<int32_t> indices_val;
        if (indices_anim.get_default(&indices_val)) {
          value::Value indices_value(indices_val);
          if (!add_array_attribute("indices", indices_value)) {
            return false;
          }
        }
      }
    }
  }

  return true;
}

// ============================================================================
// BlendShape Property Extraction
// ============================================================================

bool CrateWriter::ExtractBlendShapeProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const BlendShape* blend_shape = prim.data().as<BlendShape>();
  if (!blend_shape) {
    if (err) *err = "Failed to cast prim to BlendShape";
    return false;
  }

  // Helper lambda to convert array values
  auto add_array_attribute = [&](const std::string& attr_name, const value::Value& val) -> bool {
    crate::CrateValue crate_val;
    return ConvertValue(val, crate_val, err) &&
           (fields.push_back({attr_name, crate_val}), true);
  };

  // Extract offsets (vector3f[]) - Position deltas for each vertex
  if (blend_shape->offsets.has_value()) {
    auto offsets_val = blend_shape->offsets.get_value();
    if (offsets_val) {
      value::Value offsets_value(*offsets_val);
      if (!add_array_attribute("offsets", offsets_value)) {
        return false;
      }
    }
  }

  // Extract normalOffsets (vector3f[]) - Normal deltas for each vertex
  if (blend_shape->normalOffsets.has_value()) {
    auto normal_offsets_val = blend_shape->normalOffsets.get_value();
    if (normal_offsets_val) {
      value::Value normal_offsets_value(*normal_offsets_val);
      if (!add_array_attribute("normalOffsets", normal_offsets_value)) {
        return false;
      }
    }
  }

  // Extract pointIndices (int[]) - Subset of vertices affected (optional)
  if (blend_shape->pointIndices.has_value()) {
    auto point_indices_val = blend_shape->pointIndices.get_value();
    if (point_indices_val) {
      value::Value point_indices_value(*point_indices_val);
      if (!add_array_attribute("pointIndices", point_indices_value)) {
        return false;
      }
    }
  }

  // TODO: Handle inbetween blend shapes (stored in props with "inbetweens:" namespace)
  // For now, just extract basic properties

  return true;
}

// ============================================================================
// UsdLux Light Property Extraction
// ============================================================================

/// Helper function to extract common light properties
// Helper to extract common light properties (implemented inline in each light type extractor)

bool CrateWriter::ExtractSphereLightProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const SphereLight* light = prim.data().as<SphereLight>();
  if (!light) {
    if (err) *err = "Failed to cast prim to SphereLight";
    return false;
  }

  if (light->radius.has_value()) {
    if (!ExtractAnimatableDefault(light->radius.get_value(), "radius", fields, err)) return false;
  }
  if (light->intensity.has_value()) {
    if (!ExtractAnimatableDefault(light->intensity.get_value(), "intensity", fields, err)) return false;
  }
  if (light->color.has_value()) {
    if (!ExtractAnimatableDefault(light->color.get_value(), "color", fields, err)) return false;
  }
  if (light->exposure.has_value()) {
    if (!ExtractAnimatableDefault(light->exposure.get_value(), "exposure", fields, err)) return false;
  }

  return true;
}

bool CrateWriter::ExtractRectLightProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const RectLight* light = prim.data().as<RectLight>();
  if (!light) {
    if (err) *err = "Failed to cast prim to RectLight";
    return false;
  }

  if (light->width.has_value()) {
    if (!ExtractAnimatableDefault(light->width.get_value(), "width", fields, err)) return false;
  }
  if (light->height.has_value()) {
    if (!ExtractAnimatableDefault(light->height.get_value(), "height", fields, err)) return false;
  }
  if (light->intensity.has_value()) {
    if (!ExtractAnimatableDefault(light->intensity.get_value(), "intensity", fields, err)) return false;
  }

  return true;
}

bool CrateWriter::ExtractDiskLightProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const DiskLight* light = prim.data().as<DiskLight>();
  if (!light) {
    if (err) *err = "Failed to cast prim to DiskLight";
    return false;
  }

  if (light->radius.has_value()) {
    if (!ExtractAnimatableDefault(light->radius.get_value(), "radius", fields, err)) return false;
  }
  if (light->intensity.has_value()) {
    if (!ExtractAnimatableDefault(light->intensity.get_value(), "intensity", fields, err)) return false;
  }

  return true;
}

bool CrateWriter::ExtractCylinderLightProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const CylinderLight* light = prim.data().as<CylinderLight>();
  if (!light) {
    if (err) *err = "Failed to cast prim to CylinderLight";
    return false;
  }

  if (light->radius.has_value()) {
    if (!ExtractAnimatableDefault(light->radius.get_value(), "radius", fields, err)) return false;
  }
  if (light->length.has_value()) {
    if (!ExtractAnimatableDefault(light->length.get_value(), "length", fields, err)) return false;
  }
  if (light->intensity.has_value()) {
    if (!ExtractAnimatableDefault(light->intensity.get_value(), "intensity", fields, err)) return false;
  }

  return true;
}

bool CrateWriter::ExtractDistantLightProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const DistantLight* light = prim.data().as<DistantLight>();
  if (!light) {
    if (err) *err = "Failed to cast prim to DistantLight";
    return false;
  }

  if (light->angle.has_value()) {
    if (!ExtractAnimatableDefault(light->angle.get_value(), "angle", fields, err)) return false;
  }
  if (light->intensity.has_value()) {
    if (!ExtractAnimatableDefault(light->intensity.get_value(), "intensity", fields, err)) return false;
  }

  return true;
}

bool CrateWriter::ExtractDomeLightProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const DomeLight* light = prim.data().as<DomeLight>();
  if (!light) {
    if (err) *err = "Failed to cast prim to DomeLight";
    return false;
  }

  // Extract texture file path if present (special case: optional<Animatable>)
  if (light->file.has_value()) {
    const auto& file_opt = light->file.get_value();
    if (file_opt) {
      const Animatable<value::AssetPath>& file_anim = *file_opt;
      if (file_anim.has_default()) {
        value::AssetPath file_val;
        if (file_anim.get_default(&file_val)) {
          crate::CrateValue crate_val;
          value::Value val(file_val.GetAssetPath());
          if (ConvertValue(val, crate_val, err)) {
            fields.push_back({"file", crate_val});
          }
        }
      }
    }
  }

  if (light->intensity.has_value()) {
    if (!ExtractAnimatableDefault(light->intensity.get_value(), "intensity", fields, err)) return false;
  }

  return true;
}

bool CrateWriter::ExtractGeometryLightProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const GeometryLight* light = prim.data().as<GeometryLight>();
  if (!light) {
    if (err) *err = "Failed to cast prim to GeometryLight";
    return false;
  }

  if (light->intensity.has_value()) {
    if (!ExtractAnimatableDefault(light->intensity.get_value(), "intensity", fields, err)) return false;
  }
  if (light->color.has_value()) {
    if (!ExtractAnimatableDefault(light->color.get_value(), "color", fields, err)) return false;
  }
  if (light->exposure.has_value()) {
    if (!ExtractAnimatableDefault(light->exposure.get_value(), "exposure", fields, err)) return false;
  }

  return true;
}

bool CrateWriter::ExtractPortalLightProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const PortalLight* light = prim.data().as<PortalLight>();
  if (!light) {
    if (err) *err = "Failed to cast prim to PortalLight";
    return false;
  }

  if (light->intensity.has_value()) {
    if (!ExtractAnimatableDefault(light->intensity.get_value(), "intensity", fields, err)) return false;
  }
  if (light->color.has_value()) {
    if (!ExtractAnimatableDefault(light->color.get_value(), "color", fields, err)) return false;
  }
  if (light->exposure.has_value()) {
    if (!ExtractAnimatableDefault(light->exposure.get_value(), "exposure", fields, err)) return false;
  }

  return true;
}

// ============================================================================
// Skeleton Property Extraction
// ============================================================================

bool CrateWriter::ExtractSkeletonProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const Skeleton* skel = prim.data().as<Skeleton>();
  if (!skel) {
    if (err) *err = "Failed to cast prim to Skeleton";
    return false;
  }

  // Note: All Skeleton properties (jointNames, joints, bindTransforms, restTransforms)
  // are extracted by the generic property system instead, which handles them through
  // the props map on the Skeleton structure.
  // This function acts as a type-specific handler but defers to the generic system.
  return true;
}

// ============================================================================
// SkelAnimation Property Extraction
// ============================================================================

bool CrateWriter::ExtractSkelAnimationProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const SkelAnimation* anim = prim.data().as<SkelAnimation>();
  if (!anim) {
    if (err) *err = "Failed to cast prim to SkelAnimation";
    return false;
  }

  // Note: All SkelAnimation properties (blendShapes, joints, rotations, translations, scales, blendShapeWeights)
  // are extracted by the generic property system instead, which handles them through
  // the props map on the SkelAnimation structure.
  // This function acts as a type-specific handler but defers to the generic system.
  return true;
}

// ============================================================================
// SkelRoot Property Extraction
// ============================================================================

bool CrateWriter::ExtractSkelRootProperties(
    const Prim& prim,
    const Path& prim_path,
    crate::FieldValuePairVector& fields,
    std::string* err) {
  const SkelRoot* skel_root = prim.data().as<SkelRoot>();
  if (!skel_root) {
    if (err) *err = "Failed to cast prim to SkelRoot";
    return false;
  }

  // SkelRoot has no dedicated attributes beyond visibility, purpose, and extent
  // Extract visibility
  if (skel_root->visibility.has_value()) {
    const auto& visibility_anim = skel_root->visibility.get_value();
    if (visibility_anim.has_default()) {
      Visibility visibility_val;
      if (visibility_anim.get_default(&visibility_val)) {
        crate::CrateValue crate_val;
        value::token tok(to_string(visibility_val));
        crate_val.Set(tok);
        fields.push_back({"visibility", crate_val});
      }
    }
  }

  // Extract purpose
  if (skel_root->purpose.has_value()) {
    const auto& purpose_val = skel_root->purpose.get_value();
    crate::CrateValue crate_val;
    value::token tok(to_string(purpose_val));
    crate_val.Set(tok);
    fields.push_back({"purpose", crate_val});
  }

  // Extract extent
  if (skel_root->extent.has_value()) {
    const auto& extent_opt = skel_root->extent.get_value();
    if (extent_opt) {
      const auto& extent_animatable = extent_opt.value();
      if (extent_animatable.has_default()) {
        Extent extent_val;
        if (extent_animatable.get_default(&extent_val)) {
          crate::CrateValue crate_val;
          std::vector<value::float3> extent_vec = {extent_val.lower, extent_val.upper};
          value::Value val(extent_vec);
          if (ConvertValue(val, crate_val, err)) {
            fields.push_back({"extent", crate_val});
          }
        }
      }
    }
  }

  return true;
}

// ============================================================================
// XformOp Extraction Helper
// ============================================================================

bool CrateWriter::ExtractXformOpsFromXformable(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  // GPrim and its subclasses aren't directly castable from value::Value
  // Instead, we need to check the specific type and access the Xformable base
  const Xformable* xformable = nullptr;

  // Try casting to specific geometry types that inherit from GPrim/Xformable
  if (auto* cube = prim.data().as<GeomCube>()) {
    xformable = static_cast<const Xformable*>(cube);
  } else if (auto* mesh = prim.data().as<GeomMesh>()) {
    xformable = static_cast<const Xformable*>(mesh);
  } else if (auto* sphere = prim.data().as<GeomSphere>()) {
    xformable = static_cast<const Xformable*>(sphere);
  } else if (auto* cylinder = prim.data().as<GeomCylinder>()) {
    xformable = static_cast<const Xformable*>(cylinder);
  } else if (auto* cone = prim.data().as<GeomCone>()) {
    xformable = static_cast<const Xformable*>(cone);
  } else if (auto* capsule = prim.data().as<GeomCapsule>()) {
    xformable = static_cast<const Xformable*>(capsule);
  } else if (auto* points = prim.data().as<GeomPoints>()) {
    xformable = static_cast<const Xformable*>(points);
  } else if (auto* camera = prim.data().as<GeomCamera>()) {
    xformable = static_cast<const Xformable*>(camera);
  } else if (auto* basis_curves = prim.data().as<GeomBasisCurves>()) {
    xformable = static_cast<const Xformable*>(basis_curves);
  } else if (auto* nurbs_curves = prim.data().as<GeomNurbsCurves>()) {
    xformable = static_cast<const Xformable*>(nurbs_curves);
  } else if (auto* instancer = prim.data().as<GeomPointInstancer>()) {
    xformable = static_cast<const Xformable*>(instancer);
  } else if (auto* xform = prim.data().as<Xform>()) {
    xformable = xform;
  } else {
    // Not a type we handle yet
    return true;
  }

  if (!xformable) {
    return true;
  }

  // Check if there are any xformOps
  if (xformable->xformOps.empty()) {
    return true;
  }


  // Extract each xformOp
  for (const auto& xformOp : xformable->xformOps) {
    // Build attribute name (e.g., "xformOp:translate")
    std::string op_name = "xformOp:";

    switch (xformOp.op_type) {
      case XformOp::OpType::Transform:
        op_name += "transform";
        break;
      case XformOp::OpType::Translate:
        op_name += "translate";
        break;
      case XformOp::OpType::Scale:
        op_name += "scale";
        break;
      case XformOp::OpType::RotateX:
        op_name += "rotateX";
        break;
      case XformOp::OpType::RotateY:
        op_name += "rotateY";
        break;
      case XformOp::OpType::RotateZ:
        op_name += "rotateZ";
        break;
      case XformOp::OpType::RotateXYZ:
        op_name += "rotateXYZ";
        break;
      case XformOp::OpType::RotateXZY:
        op_name += "rotateXZY";
        break;
      case XformOp::OpType::RotateYXZ:
        op_name += "rotateYXZ";
        break;
      case XformOp::OpType::RotateYZX:
        op_name += "rotateYZX";
        break;
      case XformOp::OpType::RotateZXY:
        op_name += "rotateZXY";
        break;
      case XformOp::OpType::RotateZYX:
        op_name += "rotateZYX";
        break;
      case XformOp::OpType::Orient:
        op_name += "orient";
        break;
      case XformOp::OpType::ResetXformStack:
        // Skip this one, it's handled in xformOpOrder
        continue;
    }

    // Add suffix if present (e.g., ":spin" for "xformOp:rotateZ:spin")
    if (!xformOp.suffix.empty()) {
      op_name += ":" + xformOp.suffix;
    }


    // Create a separate Attribute spec for this xformOp
    Path attr_path = prim_path.AppendProperty(op_name);
    crate::FieldValuePairVector attr_fields;

    // Add typeName based on the value type
    std::string type_name;
    if (xformOp.has_default()) {
      type_name = xformOp._var.value_raw().type_name();
    } else if (xformOp.has_timesamples()) {
      // Get type from first sample
      const value::TimeSamples& ts = xformOp._var._ts;
      if (ts.size() > 0) {
        type_name = ts.get_samples()[0].value.type_name();
      } else {
        type_name = "float";  // fallback
      }
    } else {
      type_name = "float";  // fallback
    }

    crate::CrateValue type_value;
    value::token type_tok(type_name);
    type_value.Set(type_tok);
    attr_fields.push_back({"typeName", type_value});

    // Add default value if present
    if (xformOp.has_default()) {
      const value::Value& val = xformOp._var.value_raw();

      crate::CrateValue crate_val;
      if (ConvertValue(val, crate_val, err)) {
        attr_fields.push_back({"default", crate_val});
      } else {
        DCOUT("WARNING: Failed to convert xformOp value for " << op_name
                  << ": " << (err ? *err : "unknown"));
      }
    }

    // Add timeSamples if present (as a field on the same spec)
    if (xformOp.has_timesamples()) {
      const value::TimeSamples& ts = xformOp._var._ts;

      crate::CrateValue ts_crate_val;
      ts_crate_val.Set(ts);
      attr_fields.push_back({"timeSamples", ts_crate_val});

      DCOUT("Successfully extracted animated xformOp: " << op_name
                << " with " << ts.size() << " samples");
    }

    // Create the Attribute spec
    if (!AddSpec(attr_path, SpecType::Attribute, attr_fields, err)) {
      DCOUT("WARNING: Failed to add spec for xformOp: " << op_name);
    }
  }

  // Extract xformOpOrder as a separate Attribute spec
  const std::vector<value::token>& xform_op_order = xformable->xformOpOrder();
  if (!xform_op_order.empty()) {
    Path order_path = prim_path.AppendProperty("xformOpOrder");
    crate::FieldValuePairVector order_fields;

    // Add typeName: token[]
    crate::CrateValue type_value;
    value::token type_tok("token[]");
    type_value.Set(type_tok);
    order_fields.push_back({"typeName", type_value});

    // Add variability: uniform
    crate::CrateValue variability_value;
    variability_value.Set(Variability::Uniform);
    order_fields.push_back({"variability", variability_value});

    // Add default value: token array
    std::vector<value::token> op_order_tokens;
    for (const auto& tok : xform_op_order) {
      op_order_tokens.push_back(tok);
    }

    crate::CrateValue default_value;
    default_value.Set(op_order_tokens);
    order_fields.push_back({"default", default_value});

    // Create the Attribute spec
    if (!AddSpec(order_path, SpecType::Attribute, order_fields, err)) {
      DCOUT("WARNING: Failed to add spec for xformOpOrder");
    }
  }

  return true;
}

// ============================================================================
// GPrim Common Property Extraction
// ============================================================================

bool CrateWriter::ExtractGPrimProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  // Extract xformOps (GPrim inherits from Xformable)
  if (!ExtractXformOpsFromXformable(prim, prim_path, fields, err)) {
  }

  // Try to get as GPrim to access common properties
  const GPrim* gprim = prim.data().as<GPrim>();
  if (!gprim) {
    // Not a GPrim, that's okay
    return true;
  }

  // Extract common GPrim properties

  // Extract visibility
  if (gprim->visibility.authored()) {
    const auto& vis_animatable = gprim->visibility.get_value();
    if (vis_animatable.has_default()) {
      Visibility vis_val;
      if (vis_animatable.get_default(&vis_val)) {
        if (vis_val != Visibility::Inherited) {  // Only write if not default
          crate::CrateValue vis_crate_val;
          value::token vis_tok(to_string(vis_val));
          vis_crate_val.Set(vis_tok);
          fields.push_back({"visibility", vis_crate_val});
        }
      }
    }

    // Handle animated visibility (TimeSamples)
    if (vis_animatable.has_timesamples()) {
      const auto& typed_ts = vis_animatable.get_timesamples();

      // Convert TypedTimeSamples<Visibility> to value::TimeSamples
      value::TimeSamples ts;
      for (size_t i = 0; i < typed_ts.size(); i++) {
        Visibility sample_vis;
        double time;

        // Get time and value from the typed timesamples
        time = typed_ts.get_samples()[i].t;
        sample_vis = typed_ts.get_samples()[i].value;

        // Convert Visibility enum to token string
        std::string vis_str = to_string(sample_vis);
        value::token vis_token(vis_str);
        value::Value v(vis_token);
        ts.add_sample(time, v);
      }

      crate::CrateValue ts_crate_val;
      ts_crate_val.Set(ts);
      fields.push_back({"visibility.timeSamples", ts_crate_val});

      DCOUT("[ExtractGPrimProperties] Added animated visibility with "
                << ts.size() << " samples");
    }
  }

  // Extract purpose
  if (gprim->purpose.authored()) {
    Purpose purpose_val = gprim->purpose.get_value();
    if (purpose_val != Purpose::Default) {  // Only write if not default
      crate::CrateValue purpose_crate_val;
      value::token purpose_tok(to_string(purpose_val));
      purpose_crate_val.Set(purpose_tok);
      fields.push_back({"purpose", purpose_crate_val});
    }
  }

  // Extract extent (bounding box) - stored as float3[2] in USD
  if (gprim->extent.has_value()) {
    auto extent_animatable = gprim->extent.get_value();
    if (extent_animatable && extent_animatable->has_default()) {
      Extent extent_val;
      if (extent_animatable->get_default(&extent_val)) {
        // Convert Extent struct to float3[2]
        std::vector<value::float3> extent_vec = {extent_val.lower, extent_val.upper};
        crate::CrateValue extent_crate_val;
        value::Value extent_value(extent_vec);
        if (ConvertValue(extent_value, extent_crate_val, err)) {
          fields.push_back({"extent", extent_crate_val});
        }
      }
    }
  }

  // Extract doubleSided
  if (gprim->doubleSided.authored()) {
    bool double_sided_val = gprim->doubleSided.get_value();
    crate::CrateValue ds_crate_val;
    ds_crate_val.Set(double_sided_val);
    fields.push_back({"doubleSided", ds_crate_val});
  }

  // Extract orientation
  if (gprim->orientation.authored()) {
    Orientation orient_val = gprim->orientation.get_value();
    if (orient_val != Orientation::RightHanded) {  // Only write if not default
      crate::CrateValue orient_crate_val;
      value::token orient_tok(to_string(orient_val));
      orient_crate_val.Set(orient_tok);
      fields.push_back({"orientation", orient_crate_val});
    }
  }

  return true;
}

// ============================================================================
// Material Property Extraction
// ============================================================================

bool CrateWriter::ExtractMaterialProperties(
  const Prim& prim,
  const Path& prim_path,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const Material* material = prim.data().as<Material>();
  if (!material) {
    if (err) *err = "Failed to cast prim to Material";
    return false;
  }


  // Material outputs (surface, displacement, volume) are handled separately
  // by AddMaterialOutputSpecs() which is called AFTER the Material prim spec is added
  // This ensures correct ordering in the spec list

  // Material prims typically don't have additional properties besides their outputs
  // Any generic properties would be added here

  return true;
}

// ============================================================================
// Material Output Specs (called AFTER Material prim spec is added)
// ============================================================================

bool CrateWriter::AddMaterialOutputSpecs(
  const Material* material,
  const Path& prim_path,
  std::string* err
) {

  // Handle outputs:surface
  if (material->surface.authored() && material->surface.has_value()) {
    const auto& connections = material->surface.get_connections();
    if (!connections.empty()) {
      std::string output_name = "outputs:surface";
      Path output_path = prim_path.AppendProperty(output_name);


      crate::FieldValuePairVector output_fields;

      // Add typeName field
      crate::CrateValue type_value;
      value::token type_tok("token");
      type_value.Set(type_tok);
      output_fields.push_back({"typeName", type_value});

      // Add connectionPaths as a ListOp[Path] (for Attribute connections)
      // Note: Use "connectionPaths" for Attribute connections, "targetPaths" for Relationships
      ListOp<Path> connection_paths_listop;
      connection_paths_listop.ClearAndMakeExplicit();
      connection_paths_listop.SetExplicitItems(connections);

      crate::CrateValue conn_value;
      conn_value.Set(connection_paths_listop);
      output_fields.push_back({"connectionPaths", conn_value});

      if (!AddSpec(output_path, SpecType::Attribute, output_fields, err)) {
        return false;
      }

    }
  }

  // Handle outputs:displacement
  if (material->displacement.authored() && material->displacement.has_value()) {
    const auto& connections = material->displacement.get_connections();
    if (!connections.empty()) {
      std::string output_name = "outputs:displacement";
      Path output_path = prim_path.AppendProperty(output_name);

      crate::FieldValuePairVector output_fields;

      crate::CrateValue type_value;
      value::token type_tok("token");
      type_value.Set(type_tok);
      output_fields.push_back({"typeName", type_value});

      // Add connectionPaths as a ListOp[Path] (for Attribute connections)
      // Note: Use "connectionPaths" for Attribute connections, "targetPaths" for Relationships
      ListOp<Path> connection_paths_listop;
      connection_paths_listop.ClearAndMakeExplicit();
      connection_paths_listop.SetExplicitItems(connections);

      crate::CrateValue conn_value;
      conn_value.Set(connection_paths_listop);
      output_fields.push_back({"connectionPaths", conn_value});

      if (!AddSpec(output_path, SpecType::Attribute, output_fields, err)) {
        return false;
      }
    }
  }

  // Handle outputs:volume
  if (material->volume.authored() && material->volume.has_value()) {
    const auto& connections = material->volume.get_connections();
    if (!connections.empty()) {
      std::string output_name = "outputs:volume";
      Path output_path = prim_path.AppendProperty(output_name);

      crate::FieldValuePairVector output_fields;

      crate::CrateValue type_value;
      value::token type_tok("token");
      type_value.Set(type_tok);
      output_fields.push_back({"typeName", type_value});

      // Add connectionPaths as a ListOp[Path] (for Attribute connections)
      // Note: Use "connectionPaths" for Attribute connections, "targetPaths" for Relationships
      ListOp<Path> connection_paths_listop;
      connection_paths_listop.ClearAndMakeExplicit();
      connection_paths_listop.SetExplicitItems(connections);

      crate::CrateValue conn_value;
      conn_value.Set(connection_paths_listop);
      output_fields.push_back({"connectionPaths", conn_value});

      if (!AddSpec(output_path, SpecType::Attribute, output_fields, err)) {
        return false;
      }
    }
  }

  return true;
}

bool CrateWriter::AddMaterialBindingSpecs(
  const Prim& prim,
  const Path& prim_path,
  std::string* err
) {
  // Extract and write material binding relationships from a Prim
  // Material bindings are relationships that need separate specs in Crate format

  // Try to get MaterialBinding interface from the prim data
  // Geometry types and Group types inherit from GPrim which inherits from MaterialBinding
  const GeomSphere* geom_sphere = prim.data().as<GeomSphere>();
  const GeomCone* geom_cone = nullptr;
  const GeomCapsule* geom_capsule = nullptr;
  const GeomPoints* geom_points = nullptr;
  const GeomCube* geom_cube = nullptr;
  const GeomCylinder* geom_cylinder = nullptr;
  const GeomMesh* geom_mesh = nullptr;
  const GeomSubset* geom_subset = nullptr;
  const GeomCamera* geom_camera = nullptr;
  const GeomBasisCurves* geom_basis_curves = nullptr;
  const GeomNurbsCurves* geom_nurbs_curves = nullptr;
  const GeomPointInstancer* geom_point_instancer = nullptr;
  const Xform* xform = nullptr;
  const Model* model = nullptr;
  const Scope* scope = nullptr;

  // Determine which type the prim is and get the MaterialBinding interface
  const MaterialBinding* mat_binding = nullptr;

  if (geom_sphere) {
    mat_binding = static_cast<const MaterialBinding*>(geom_sphere);
  } else if ((geom_cone = prim.data().as<GeomCone>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_cone);
  } else if ((geom_capsule = prim.data().as<GeomCapsule>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_capsule);
  } else if ((geom_points = prim.data().as<GeomPoints>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_points);
  } else if ((geom_cube = prim.data().as<GeomCube>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_cube);
  } else if ((geom_cylinder = prim.data().as<GeomCylinder>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_cylinder);
  } else if ((geom_mesh = prim.data().as<GeomMesh>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_mesh);
  } else if ((geom_subset = prim.data().as<GeomSubset>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_subset);
  } else if ((geom_camera = prim.data().as<GeomCamera>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_camera);
  } else if ((geom_basis_curves = prim.data().as<GeomBasisCurves>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_basis_curves);
  } else if ((geom_nurbs_curves = prim.data().as<GeomNurbsCurves>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_nurbs_curves);
  } else if ((geom_point_instancer = prim.data().as<GeomPointInstancer>())) {
    mat_binding = static_cast<const MaterialBinding*>(geom_point_instancer);
  } else if ((xform = prim.data().as<Xform>())) {
    mat_binding = static_cast<const MaterialBinding*>(xform);
  } else if ((model = prim.data().as<Model>())) {
    mat_binding = static_cast<const MaterialBinding*>(model);
  } else if ((scope = prim.data().as<Scope>())) {
    mat_binding = static_cast<const MaterialBinding*>(scope);
  }

  if (!mat_binding) {
    // Prim type doesn't support material bindings
    return true;
  }

  // Add material:binding relationship if present
  if (mat_binding->has_materialBinding()) {
    const Relationship& binding = mat_binding->materialBinding.value();
    if (!ConvertRelationshipToFields("material:binding", binding, prim_path, err)) {
      if (err) *err = "Failed to add material:binding relationship: " + *err;
      return false;
    }
  }

  // Add material:binding:preview relationship if present
  if (mat_binding->has_materialBindingPreview()) {
    const Relationship& binding = mat_binding->materialBindingPreview.value();
    if (!ConvertRelationshipToFields("material:binding:preview", binding, prim_path, err)) {
      if (err) *err = "Failed to add material:binding:preview relationship: " + *err;
      return false;
    }
  }

  // Add material:binding:full relationship if present
  if (mat_binding->has_materialBindingFull()) {
    const Relationship& binding = mat_binding->materialBindingFull.value();
    if (!ConvertRelationshipToFields("material:binding:full", binding, prim_path, err)) {
      if (err) *err = "Failed to add material:binding:full relationship: " + *err;
      return false;
    }
  }

  // Add other purpose-specific material bindings from materialBindingMap
  for (const auto& binding_entry : mat_binding->materialBindingMap()) {
    const std::string& purpose = binding_entry.first;
    const Relationship& rel = binding_entry.second;

    std::string rel_name = "material:binding";
    if (!purpose.empty()) {
      rel_name += ":" + purpose;
    }

    if (!ConvertRelationshipToFields(rel_name, rel, prim_path, err)) {
      if (err) *err = "Failed to add " + rel_name + " relationship: " + *err;
      return false;
    }
  }

  // Add collection-based material bindings
  for (const auto& coll_entry : mat_binding->materialBindingCollectionMap()) {
    const std::string& coll_name = coll_entry.first;
    const auto& purpose_map = coll_entry.second;

    // Iterate through all purposes in this collection
    for (const auto& purpose : purpose_map.keys()) {
      const Relationship* rel = nullptr;
      if (purpose_map.at(purpose, &rel)) {
        std::string rel_name = "material:binding:collection:" + coll_name;
        if (!purpose.empty()) {
          rel_name += ":" + purpose;
        }

        if (!ConvertRelationshipToFields(rel_name, *rel, prim_path, err)) {
          if (err) *err = "Failed to add " + rel_name + " relationship: " + *err;
          return false;
        }
      }
    }
  }

  return true;
}

// ============================================================================
// Light Filter Relationship Specs (called AFTER Light prim spec is added)
// ============================================================================

bool CrateWriter::AddLightFilterSpecs(
  const Prim& prim,
  const Path& prim_path,
  std::string* err
) {
  // Extract and write light filter relationships from a Light prim
  // Light filters are relationships that need separate specs in Crate format

  // Get the appropriate light type and extract lightFilters relationship
  // Light filters are supported for lights that inherit from BoundableLight or NonboundableLight
  const SphereLight* sphere_light = prim.data().as<SphereLight>();
  const RectLight* rect_light = nullptr;
  const DiskLight* disk_light = nullptr;
  const CylinderLight* cylinder_light = nullptr;
  const DistantLight* distant_light = nullptr;
  const DomeLight* dome_light = nullptr;
  const GeometryLight* geometry_light = nullptr;
  const PortalLight* portal_light = nullptr;

  // Determine which light type the prim is
  const void* light_base = nullptr;

  if (sphere_light) {
    light_base = static_cast<const void*>(sphere_light);
  } else if ((rect_light = prim.data().as<RectLight>())) {
    light_base = static_cast<const void*>(rect_light);
  } else if ((disk_light = prim.data().as<DiskLight>())) {
    light_base = static_cast<const void*>(disk_light);
  } else if ((cylinder_light = prim.data().as<CylinderLight>())) {
    light_base = static_cast<const void*>(cylinder_light);
  } else if ((distant_light = prim.data().as<DistantLight>())) {
    light_base = static_cast<const void*>(distant_light);
  } else if ((dome_light = prim.data().as<DomeLight>())) {
    light_base = static_cast<const void*>(dome_light);
  } else if ((geometry_light = prim.data().as<GeometryLight>())) {
    light_base = static_cast<const void*>(geometry_light);
  } else if ((portal_light = prim.data().as<PortalLight>())) {
    light_base = static_cast<const void*>(portal_light);
  }

  if (!light_base) {
    // Prim is not a light type with filter support
    return true;
  }

  // Get the lightFilters relationship from the appropriate light type
  nonstd::optional<Relationship> light_filters;

  if (sphere_light) {
    light_filters = sphere_light->lightFilters;
  } else if (rect_light) {
    light_filters = rect_light->lightFilters;
  } else if (disk_light) {
    light_filters = disk_light->lightFilters;
  } else if (cylinder_light) {
    light_filters = cylinder_light->lightFilters;
  } else if (distant_light) {
    light_filters = distant_light->lightFilters;
  } else if (dome_light) {
    light_filters = dome_light->lightFilters;
  } else if (geometry_light) {
    light_filters = geometry_light->lightFilters;
  } else if (portal_light) {
    light_filters = portal_light->lightFilters;
  }

  // Add light:filters relationship if present
  if (light_filters.has_value()) {
    const Relationship& filters = light_filters.value();
    if (!ConvertRelationshipToFields("light:filters", filters, prim_path, err)) {
      if (err) *err = "Failed to add light:filters relationship: " + *err;
      return false;
    }
  }

  return true;
}

// ============================================================================
// PointInstancer prototypes relationship spec
// ============================================================================

bool CrateWriter::AddPointInstancerPrototypesSpec(
  const Prim& prim,
  const Path& prim_path,
  std::string* err
) {
  const GeomPointInstancer* instancer = prim.data().as<GeomPointInstancer>();
  if (!instancer) {
    return true;
  }

  if (instancer->prototypes.has_value()) {
    const Relationship& protos = instancer->prototypes.value();
    if (!ConvertRelationshipToFields("prototypes", protos, prim_path, err)) {
      if (err) *err = "Failed to add prototypes relationship: " + *err;
      return false;
    }
  }

  return true;
}

// ============================================================================
// UsdPreviewSurface Shader Input Specs (called AFTER Shader prim spec is added)
// ============================================================================

bool CrateWriter::AddUsdPreviewSurfaceInputSpecs(
  const UsdPreviewSurface* preview_surface,
  const Path& prim_path,
  std::string* err
) {

  // Helper lambda to add an input attribute spec
  auto add_input_spec = [&](const std::string& input_name, const std::string& type_name, const crate::CrateValue& value) -> bool {
    Path input_path = prim_path.AppendProperty(input_name);
    crate::FieldValuePairVector input_fields;

    // Add typeName
    crate::CrateValue type_value;
    value::token type_tok(type_name);
    type_value.Set(type_tok);
    input_fields.push_back({"typeName", type_value});

    // Add default value
    input_fields.push_back({"default", value});

    return AddSpec(input_path, SpecType::Attribute, input_fields, err);
  };

  // Helper to convert OpacityMode enum to token string
  auto opacity_mode_to_string = [](UsdPreviewSurface::OpacityMode mode) -> std::string {
    switch (mode) {
      case UsdPreviewSurface::OpacityMode::Opacity: return "opacity";
      case UsdPreviewSurface::OpacityMode::Transparent: return "transparent";
      case UsdPreviewSurface::OpacityMode::Presence: return "presence";
    }
    return "transparent"; // fallback
  };

  // Helper to handle timesampled float shader inputs
  auto add_input_spec_with_timesamples = [&](
      const std::string& input_name,
      const std::string& type_name,
      const crate::CrateValue& default_value,
      const Animatable<float>* animatable) -> bool {
    // First add the default value spec
    if (!add_input_spec(input_name, type_name, default_value)) {
      return false;
    }

    // If there are timesamples, create a separate timeSamples spec
    if (animatable && animatable->has_timesamples()) {
      const auto& typed_ts = animatable->get_timesamples();

      // Convert TypedTimeSamples<float> to value::TimeSamples
      value::TimeSamples ts;
      for (size_t i = 0; i < typed_ts.size(); i++) {
        double time;
        float sample_value;

        time = typed_ts.get_samples()[i].t;
        sample_value = typed_ts.get_samples()[i].value;

        value::Value v(sample_value);
        ts.add_sample(time, v);
      }

      // Create timeSamples spec
      Path ts_path = prim_path.AppendProperty(input_name + ".timeSamples");
      crate::FieldValuePairVector ts_fields;

      crate::CrateValue ts_value;
      ts_value.Set(ts);
      ts_fields.push_back({"default", ts_value});

      if (!AddSpec(ts_path, SpecType::Attribute, ts_fields, err)) {
        return false;
      }
    }

    return true;
  };

  // Helper to handle timesampled color3f shader inputs
  auto add_input_spec_with_timesamples_color3f = [&](
      const std::string& input_name,
      const std::string& type_name,
      const crate::CrateValue& default_value,
      const Animatable<value::color3f>* animatable) -> bool {
    // First add the default value spec
    if (!add_input_spec(input_name, type_name, default_value)) {
      return false;
    }

    // If there are timesamples, create a separate timeSamples spec
    if (animatable && animatable->has_timesamples()) {
      const auto& typed_ts = animatable->get_timesamples();

      // Convert TypedTimeSamples<color3f> to value::TimeSamples with float3 values
      value::TimeSamples ts;
      for (size_t i = 0; i < typed_ts.size(); i++) {
        double time;
        value::color3f sample_color;

        time = typed_ts.get_samples()[i].t;
        sample_color = typed_ts.get_samples()[i].value;

        // Convert color3f to float3 for crate format
        value::float3 color_as_float3 = {sample_color.r, sample_color.g, sample_color.b};
        value::Value v(color_as_float3);
        ts.add_sample(time, v);
      }

      // Create timeSamples spec
      Path ts_path = prim_path.AppendProperty(input_name + ".timeSamples");
      crate::FieldValuePairVector ts_fields;

      crate::CrateValue ts_value;
      ts_value.Set(ts);
      ts_fields.push_back({"default", ts_value});

      if (!AddSpec(ts_path, SpecType::Attribute, ts_fields, err)) {
        return false;
      }
    }

    return true;
  };

  // Extract and add common PBR inputs

  // inputs:diffuseColor (color3f)
  if (preview_surface->diffuseColor.authored()) {
    value::color3f color;
    if (preview_surface->diffuseColor.get_value().get_scalar(&color)) {
      // Convert color3f to float3 for CrateValue (they're binary compatible)
      value::float3 color_as_float3 = {color.r, color.g, color.b};
      crate::CrateValue diffuse_value;
      diffuse_value.Set(color_as_float3);
      if (!add_input_spec_with_timesamples_color3f("inputs:diffuseColor", "color3f", diffuse_value, &preview_surface->diffuseColor.get_value())) {
        return false;
      }
    }
  }

  // inputs:emissiveColor (color3f)
  if (preview_surface->emissiveColor.authored()) {
    value::color3f color;
    if (preview_surface->emissiveColor.get_value().get_scalar(&color)) {
      // Convert color3f to float3 for CrateValue
      value::float3 color_as_float3 = {color.r, color.g, color.b};
      crate::CrateValue emissive_value;
      emissive_value.Set(color_as_float3);
      if (!add_input_spec_with_timesamples_color3f("inputs:emissiveColor", "color3f", emissive_value, &preview_surface->emissiveColor.get_value())) {
        return false;
      }
    }
  }

  // inputs:useSpecularWorkflow (int)
  if (preview_surface->useSpecularWorkflow.authored()) {
    crate::CrateValue use_spec_value;
    if (!preview_surface->useSpecularWorkflow.get_value().is_timesamples()) {
      int use_spec;
      if (preview_surface->useSpecularWorkflow.get_value().get_scalar(&use_spec)) {
        use_spec_value.Set(use_spec);
        if (!add_input_spec("inputs:useSpecularWorkflow", "int", use_spec_value)) {
          return false;
        }
      }
    }
  }

  // inputs:specularColor (color3f) - for specular workflow
  if (preview_surface->specularColor.authored()) {
    value::color3f color;
    if (preview_surface->specularColor.get_value().get_scalar(&color)) {
      // Convert color3f to float3 for CrateValue
      value::float3 color_as_float3 = {color.r, color.g, color.b};
      crate::CrateValue spec_color_value;
      spec_color_value.Set(color_as_float3);
      if (!add_input_spec_with_timesamples_color3f("inputs:specularColor", "color3f", spec_color_value, &preview_surface->specularColor.get_value())) {
        return false;
      }
    }
  }

  // inputs:metallic (float) - for metalness workflow
  if (preview_surface->metallic.authored()) {
    crate::CrateValue metallic_value;
    float metallic = 0.0f;
    if (preview_surface->metallic.get_value().get_scalar(&metallic)) {
      metallic_value.Set(metallic);
      if (!add_input_spec_with_timesamples("inputs:metallic", "float", metallic_value, &preview_surface->metallic.get_value())) {
        return false;
      }
    }
  }

  // inputs:roughness (float)
  if (preview_surface->roughness.authored()) {
    crate::CrateValue roughness_value;
    float roughness = 0.0f;
    if (preview_surface->roughness.get_value().get_scalar(&roughness)) {
      roughness_value.Set(roughness);
      if (!add_input_spec_with_timesamples("inputs:roughness", "float", roughness_value, &preview_surface->roughness.get_value())) {
        return false;
      }
    }
  }

  // inputs:clearcoat (float)
  if (preview_surface->clearcoat.authored()) {
    crate::CrateValue clearcoat_value;
    float clearcoat = 0.0f;
    if (preview_surface->clearcoat.get_value().get_scalar(&clearcoat)) {
      clearcoat_value.Set(clearcoat);
      if (!add_input_spec_with_timesamples("inputs:clearcoat", "float", clearcoat_value, &preview_surface->clearcoat.get_value())) {
        return false;
      }
    }
  }

  // inputs:clearcoatRoughness (float)
  if (preview_surface->clearcoatRoughness.authored()) {
    crate::CrateValue clearcoat_rough_value;
    float clearcoat_rough = 0.0f;
    if (preview_surface->clearcoatRoughness.get_value().get_scalar(&clearcoat_rough)) {
      clearcoat_rough_value.Set(clearcoat_rough);
      if (!add_input_spec_with_timesamples("inputs:clearcoatRoughness", "float", clearcoat_rough_value, &preview_surface->clearcoatRoughness.get_value())) {
        return false;
      }
    }
  }

  // inputs:opacity (float)
  if (preview_surface->opacity.authored()) {
    crate::CrateValue opacity_value;
    float opacity = 0.0f;
    if (preview_surface->opacity.get_value().get_scalar(&opacity)) {
      opacity_value.Set(opacity);
      if (!add_input_spec_with_timesamples("inputs:opacity", "float", opacity_value, &preview_surface->opacity.get_value())) {
        return false;
      }
    }
  }

  // inputs:opacityMode (token) - Controls transparency behavior (transparent or presence)
  if (preview_surface->opacityMode.authored()) {
    crate::CrateValue opacity_mode_value;
    if (!preview_surface->opacityMode.get_value().is_timesamples()) {
      UsdPreviewSurface::OpacityMode mode;
      if (preview_surface->opacityMode.get_value().get_scalar(&mode)) {
        value::token mode_tok(opacity_mode_to_string(mode));
        opacity_mode_value.Set(mode_tok);
        if (!add_input_spec("inputs:opacityMode", "token", opacity_mode_value)) {
          return false;
        }
      }
    }
  }

  // inputs:opacityThreshold (float)
  if (preview_surface->opacityThreshold.authored()) {
    crate::CrateValue opacity_thresh_value;
    float opacity_thresh = 0.0f;
    if (preview_surface->opacityThreshold.get_value().get_scalar(&opacity_thresh)) {
      opacity_thresh_value.Set(opacity_thresh);
      if (!add_input_spec_with_timesamples("inputs:opacityThreshold", "float", opacity_thresh_value, &preview_surface->opacityThreshold.get_value())) {
        return false;
      }
    }
  }

  // inputs:ior (float)
  if (preview_surface->ior.authored()) {
    crate::CrateValue ior_value;
    float ior = 0.0f;
    if (preview_surface->ior.get_value().get_scalar(&ior)) {
      ior_value.Set(ior);
      if (!add_input_spec_with_timesamples("inputs:ior", "float", ior_value, &preview_surface->ior.get_value())) {
        return false;
      }
    }
  }

  // inputs:normal (normal3f)
  if (preview_surface->normal.authored()) {
    crate::CrateValue normal_value;
    if (!preview_surface->normal.get_value().is_timesamples()) {
      value::normal3f normal;
      if (preview_surface->normal.get_value().get_scalar(&normal)) {
        // Convert normal3f to float3 for CrateValue (they're binary compatible)
        value::float3 normal_as_float3 = {normal.x, normal.y, normal.z};
        normal_value.Set(normal_as_float3);
        if (!add_input_spec("inputs:normal", "normal3f", normal_value)) {
          return false;
        }
      }
    }
  }

  // inputs:displacement (float)
  if (preview_surface->displacement.authored()) {
    crate::CrateValue displacement_value;
    float displacement = 0.0f;
    if (preview_surface->displacement.get_value().get_scalar(&displacement)) {
      displacement_value.Set(displacement);
      if (!add_input_spec_with_timesamples("inputs:displacement", "float", displacement_value, &preview_surface->displacement.get_value())) {
        return false;
      }
    }
  }

  // inputs:occlusion (float)
  if (preview_surface->occlusion.authored()) {
    crate::CrateValue occlusion_value;
    float occlusion = 0.0f;
    if (preview_surface->occlusion.get_value().get_scalar(&occlusion)) {
      occlusion_value.Set(occlusion);
      if (!add_input_spec_with_timesamples("inputs:occlusion", "float", occlusion_value, &preview_surface->occlusion.get_value())) {
        return false;
      }
    }
  }

  return true;
}

// ============================================================================
// UsdUVTexture Shader Input Specs (called AFTER Shader prim spec is added)
// ============================================================================

bool CrateWriter::AddUsdUVTextureInputSpecs(
  const UsdUVTexture* uv_texture,
  const Path& prim_path,
  std::string* err
) {

  // Helper lambda to add an input spec as a separate attribute
  auto add_input_spec = [&](const std::string& input_name, const std::string& type_name, const crate::CrateValue& value) -> bool {
    Path input_path = prim_path.AppendProperty(input_name);
    crate::FieldValuePairVector input_fields;

    // typeName field
    crate::CrateValue typename_value;
    value::token typename_tok(type_name);
    typename_value.Set(typename_tok);
    input_fields.push_back({"typeName", typename_value});

    // default field (the value)
    input_fields.push_back({"default", value});

    return AddSpec(input_path, SpecType::Attribute, input_fields, err);
  };

  // Helper to convert Wrap enum to token string
  auto wrap_to_string = [](UsdUVTexture::Wrap wrap) -> std::string {
    switch (wrap) {
      case UsdUVTexture::Wrap::UseMetadata: return "useMetadata";
      case UsdUVTexture::Wrap::Black: return "black";
      case UsdUVTexture::Wrap::Clamp: return "clamp";
      case UsdUVTexture::Wrap::Repeat: return "repeat";
      case UsdUVTexture::Wrap::Mirror: return "mirror";
      default: return "useMetadata";
    }
  };

  // Helper to convert SourceColorSpace enum to token string
  auto colorspace_to_string = [](UsdUVTexture::SourceColorSpace cs) -> std::string {
    switch (cs) {
      case UsdUVTexture::SourceColorSpace::Auto: return "auto";
      case UsdUVTexture::SourceColorSpace::Raw: return "raw";
      case UsdUVTexture::SourceColorSpace::SRGB: return "sRGB";
      default: return "auto";
    }
  };

  // Helper to handle timesampled float2 inputs (for st coordinates)
  auto add_input_spec_with_timesamples_float2 = [&](
      const std::string& input_name,
      const crate::CrateValue& default_value,
      const Animatable<value::texcoord2f>* animatable) -> bool {
    // First add the default value spec
    if (!add_input_spec(input_name, "texCoord2f", default_value)) {
      return false;
    }

    // If there are timesamples, create a separate timeSamples spec
    if (animatable && animatable->has_timesamples()) {
      const auto& typed_ts = animatable->get_timesamples();

      // Convert TypedTimeSamples<texcoord2f> to value::TimeSamples
      value::TimeSamples ts;
      for (size_t i = 0; i < typed_ts.size(); i++) {
        double time;
        value::texcoord2f sample_value;

        time = typed_ts.get_samples()[i].t;
        sample_value = typed_ts.get_samples()[i].value;

        // Convert texcoord2f to float2 for TimeSamples
        value::float2 sample_as_float2 = {sample_value.s, sample_value.t};
        value::Value v(sample_as_float2);
        ts.add_sample(time, v);
      }

      // Create timeSamples spec
      Path ts_path = prim_path.AppendProperty(input_name + ".timeSamples");
      crate::FieldValuePairVector ts_fields;

      crate::CrateValue ts_value;
      ts_value.Set(ts);
      ts_fields.push_back({"default", ts_value});

      if (!AddSpec(ts_path, SpecType::Attribute, ts_fields, err)) {
        return false;
      }
    }

    return true;
  };

  // Extract and add texture inputs

  // inputs:file (asset) - texture file path
  if (uv_texture->file.authored()) {
    crate::CrateValue file_value;
    auto file_opt = uv_texture->file.get_value();
    if (file_opt.has_value()) {
      const auto& file_animatable = file_opt.value();
      if (!file_animatable.is_timesamples()) {
        value::AssetPath file_path;
        if (file_animatable.get_scalar(&file_path)) {
          file_value.Set(file_path);
          if (!add_input_spec("inputs:file", "asset", file_value)) {
            return false;
          }
        }
      }
    }
  }

  // inputs:st (texcoord2f) - texture coordinates
  if (uv_texture->st.authored()) {
    crate::CrateValue st_value;
    value::texcoord2f st = {0.0f, 0.0f};
    if (uv_texture->st.get_value().get_scalar(&st)) {
      // Convert texcoord2f to float2 for CrateValue
      value::float2 st_as_float2 = {st.s, st.t};
      st_value.Set(st_as_float2);
      if (!add_input_spec_with_timesamples_float2("inputs:st", st_value, &uv_texture->st.get_value())) {
        return false;
      }
    }
  }

  // inputs:wrapS (token) - S axis wrap mode
  if (uv_texture->wrapS.authored()) {
    crate::CrateValue wraps_value;
    if (!uv_texture->wrapS.get_value().is_timesamples()) {
      UsdUVTexture::Wrap wrap_s;
      if (uv_texture->wrapS.get_value().get_scalar(&wrap_s)) {
        value::token wrap_tok(wrap_to_string(wrap_s));
        wraps_value.Set(wrap_tok);
        if (!add_input_spec("inputs:wrapS", "token", wraps_value)) {
          return false;
        }
      }
    }
  }

  // inputs:wrapT (token) - T axis wrap mode
  if (uv_texture->wrapT.authored()) {
    crate::CrateValue wrapt_value;
    if (!uv_texture->wrapT.get_value().is_timesamples()) {
      UsdUVTexture::Wrap wrap_t;
      if (uv_texture->wrapT.get_value().get_scalar(&wrap_t)) {
        value::token wrap_tok(wrap_to_string(wrap_t));
        wrapt_value.Set(wrap_tok);
        if (!add_input_spec("inputs:wrapT", "token", wrapt_value)) {
          return false;
        }
      }
    }
  }

  // inputs:fallback (color4f) - fallback color when texture is missing
  if (uv_texture->fallback.authored()) {
    crate::CrateValue fallback_value;
    const value::color4f& fallback = uv_texture->fallback.get_value();
    // Convert color4f to float4 for CrateValue
    value::float4 fallback_as_float4 = {fallback.r, fallback.g, fallback.b, fallback.a};
    fallback_value.Set(fallback_as_float4);
    if (!add_input_spec("inputs:fallback", "color4f", fallback_value)) {
      return false;
    }
  }

  // inputs:sourceColorSpace (token) - color space
  if (uv_texture->sourceColorSpace.authored()) {
    crate::CrateValue colorspace_value;
    if (!uv_texture->sourceColorSpace.get_value().is_timesamples()) {
      UsdUVTexture::SourceColorSpace cs;
      if (uv_texture->sourceColorSpace.get_value().get_scalar(&cs)) {
        value::token cs_tok(colorspace_to_string(cs));
        colorspace_value.Set(cs_tok);
        if (!add_input_spec("inputs:sourceColorSpace", "token", colorspace_value)) {
          return false;
        }
      }
    }
  }

  // inputs:scale (float4) - scale factor
  if (uv_texture->scale.authored()) {
    crate::CrateValue scale_value;
    const value::float4& scale = uv_texture->scale.get_value();
    scale_value.Set(scale);
    if (!add_input_spec("inputs:scale", "float4", scale_value)) {
      return false;
    }
  }

  // inputs:bias (float4) - bias offset
  if (uv_texture->bias.authored()) {
    crate::CrateValue bias_value;
    const value::float4& bias = uv_texture->bias.get_value();
    bias_value.Set(bias);
    if (!add_input_spec("inputs:bias", "float4", bias_value)) {
      return false;
    }
  }

  return true;
}

// ============================================================================
// UsdPrimvarReader Shader Input Specs (called AFTER Shader prim spec is added)
// ============================================================================

bool CrateWriter::AddUsdPrimvarReaderInputSpecs(
  const value::Value& shader_value,
  const std::string& reader_type,
  const Path& prim_path,
  std::string* err
) {
  DCOUT("[AddUsdPrimvarReaderInputSpecs] prim_path: " << prim_path.full_path_name()
            << ", type: " << reader_type);

  // Helper lambda to add an input spec as a separate attribute
  auto add_input_spec = [&](const std::string& input_name, const std::string& type_name, const crate::CrateValue& value) -> bool {
    Path input_path = prim_path.AppendProperty(input_name);
    crate::FieldValuePairVector input_fields;

    // typeName field
    crate::CrateValue typename_value;
    value::token typename_tok(type_name);
    typename_value.Set(typename_tok);
    input_fields.push_back({"typeName", typename_value});

    // default field (the value)
    input_fields.push_back({"default", value});

    return AddSpec(input_path, SpecType::Attribute, input_fields, err);
  };

  // Extract varname (string) - common to all UsdPrimvarReader variants
  // Try to get varname from the shader value using type-erased access
  std::string varname_str;
  bool has_varname = false;

  // The varname field is TypedAttribute<Animatable<std::string>>
  // We need to extract it generically from the shader_value

  // Helper macro to try extracting varname from a specific UsdPrimvarReader type
  #define TRY_EXTRACT_VARNAME(ReaderType) \
    if (!has_varname) { \
      if (auto* reader = shader_value.as<ReaderType>()) { \
        if (reader->varname.authored()) { \
          auto varname_opt = reader->varname.get_value(); \
          if (varname_opt.has_value()) { \
            const auto& varname_anim = varname_opt.value(); \
            if (!varname_anim.is_timesamples()) { \
              std::string vn; \
              if (varname_anim.get_scalar(&vn)) { \
                varname_str = vn; \
                has_varname = true; \
              } \
            } \
          } \
        } \
      } \
    }

  // Try all supported UsdPrimvarReader types
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_float)
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_float2)
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_float3)
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_float4)
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_int)
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_string)
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_normal)
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_vector)
  TRY_EXTRACT_VARNAME(UsdPrimvarReader_point)

  #undef TRY_EXTRACT_VARNAME

  // Add inputs:varname (string)
  if (has_varname) {
    crate::CrateValue varname_value;
    varname_value.Set(varname_str);
    if (!add_input_spec("inputs:varname", "string", varname_value)) {
      return false;
    }
  }

  // Note: We don't extract the fallback value because it's type-specific
  // and would require templated handling for each type variant.
  // The varname is the most important input for UsdPrimvarReader.
  // If fallback support is needed, it can be added later with type-specific handlers.

  return true;
}

bool CrateWriter::AddUsdTransform2dInputSpecs(
  const UsdTransform2d* transform2d,
  const Path& prim_path,
  std::string* err
) {

  // Helper lambda to add an input spec as a separate attribute
  auto add_input_spec = [&](const std::string& input_name, const std::string& type_name, const crate::CrateValue& value) -> bool {
    Path input_path = prim_path.AppendProperty(input_name);
    crate::FieldValuePairVector input_fields;

    // typeName field
    crate::CrateValue typename_value;
    value::token typename_tok(type_name);
    typename_value.Set(typename_tok);
    input_fields.push_back({"typeName", typename_value});

    // default field (the value)
    input_fields.push_back({"default", value});

    return AddSpec(input_path, SpecType::Attribute, input_fields, err);
  };

  // Helper to handle timesampled float2 inputs
  auto add_input_spec_with_timesamples_float2 = [&](
      const std::string& input_name,
      const crate::CrateValue& default_value,
      const Animatable<value::float2>* animatable) -> bool {
    // First add the default value spec
    if (!add_input_spec(input_name, "float2", default_value)) {
      return false;
    }

    // If there are timesamples, create a separate timeSamples spec
    if (animatable && animatable->has_timesamples()) {
      const auto& typed_ts = animatable->get_timesamples();

      // Convert TypedTimeSamples<float2> to value::TimeSamples
      value::TimeSamples ts;
      for (size_t i = 0; i < typed_ts.size(); i++) {
        double time;
        value::float2 sample_value;

        time = typed_ts.get_samples()[i].t;
        sample_value = typed_ts.get_samples()[i].value;

        value::Value v(sample_value);
        ts.add_sample(time, v);
      }

      // Create timeSamples spec
      Path ts_path = prim_path.AppendProperty(input_name + ".timeSamples");
      crate::FieldValuePairVector ts_fields;

      crate::CrateValue ts_value;
      ts_value.Set(ts);
      ts_fields.push_back({"default", ts_value});

      if (!AddSpec(ts_path, SpecType::Attribute, ts_fields, err)) {
        return false;
      }
    }

    return true;
  };

  // Helper to handle timesampled float inputs
  auto add_input_spec_with_timesamples_float = [&](
      const std::string& input_name,
      const crate::CrateValue& default_value,
      const Animatable<float>* animatable) -> bool {
    // First add the default value spec
    if (!add_input_spec(input_name, "float", default_value)) {
      return false;
    }

    // If there are timesamples, create a separate timeSamples spec
    if (animatable && animatable->has_timesamples()) {
      const auto& typed_ts = animatable->get_timesamples();

      // Convert TypedTimeSamples<float> to value::TimeSamples
      value::TimeSamples ts;
      for (size_t i = 0; i < typed_ts.size(); i++) {
        double time;
        float sample_value;

        time = typed_ts.get_samples()[i].t;
        sample_value = typed_ts.get_samples()[i].value;

        value::Value v(sample_value);
        ts.add_sample(time, v);
      }

      // Create timeSamples spec
      Path ts_path = prim_path.AppendProperty(input_name + ".timeSamples");
      crate::FieldValuePairVector ts_fields;

      crate::CrateValue ts_value;
      ts_value.Set(ts);
      ts_fields.push_back({"default", ts_value});

      if (!AddSpec(ts_path, SpecType::Attribute, ts_fields, err)) {
        return false;
      }
    }

    return true;
  };

  // Extract inputs:in (float2)
  if (transform2d->in.authored()) {
    crate::CrateValue in_crate_value;
    value::float2 in_value = {0.0f, 0.0f};
    if (transform2d->in.get_value().get_scalar(&in_value)) {
      in_crate_value.Set(in_value);
      if (!add_input_spec_with_timesamples_float2("inputs:in", in_crate_value, &transform2d->in.get_value())) {
        return false;
      }
    }
  }

  // Extract inputs:rotation (float) - in degrees, CCW
  if (transform2d->rotation.authored()) {
    crate::CrateValue rotation_crate_value;
    float rotation_value = 0.0f;
    if (transform2d->rotation.get_value().get_scalar(&rotation_value)) {
      rotation_crate_value.Set(rotation_value);
      if (!add_input_spec_with_timesamples_float("inputs:rotation", rotation_crate_value, &transform2d->rotation.get_value())) {
        return false;
      }
    }
  }

  // Extract inputs:scale (float2)
  if (transform2d->scale.authored()) {
    crate::CrateValue scale_crate_value;
    value::float2 scale_value = {1.0f, 1.0f};
    if (transform2d->scale.get_value().get_scalar(&scale_value)) {
      scale_crate_value.Set(scale_value);
      if (!add_input_spec_with_timesamples_float2("inputs:scale", scale_crate_value, &transform2d->scale.get_value())) {
        return false;
      }
    }
  }

  // Extract inputs:translation (float2)
  if (transform2d->translation.authored()) {
    crate::CrateValue translation_crate_value;
    value::float2 translation_value = {0.0f, 0.0f};
    if (transform2d->translation.get_value().get_scalar(&translation_value)) {
      translation_crate_value.Set(translation_value);
      if (!add_input_spec_with_timesamples_float2("inputs:translation", translation_crate_value, &transform2d->translation.get_value())) {
        return false;
      }
    }
  }

  return true;
}

// ============================================================================
// Shader Property Extraction
// ============================================================================

bool CrateWriter::ExtractShaderProperties(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const Shader* shader = prim.data().as<Shader>();
  if (!shader) {
    if (err) *err = "Failed to cast prim to Shader";
    return false;
  }


  // Add info:id (shader type identifier)
  if (!shader->info_id.empty()) {
    crate::CrateValue info_id_value;
    value::token tok(shader->info_id);
    info_id_value.Set(tok);
    fields.push_back({"info:id", info_id_value});
  }

  // Shader inputs and outputs are handled through the props map
  // which will be processed by ConvertPropertyToFields

  return true;
}

bool CrateWriter::ExtractNodeGraphProperties(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const NodeGraph* node_graph = prim.data().as<NodeGraph>();
  if (!node_graph) {
    if (err) *err = "Failed to cast prim to NodeGraph";
    return false;
  }

  // NodeGraph is primarily a container for organizing shader nodes and interfaces.
  // All specific properties (inputs, outputs, and child nodes) are handled through
  // the generic property system via the props map.
  // This function acts as a type-specific handler but defers to the generic system.

  return true;
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
      crate::TokenIndex tok_idx = GetOrCreateToken(v->str());
      out.Set(tok_idx.value);
      return true;
    }
  } else if (type_name == "string") {
    if (auto v = val.get_value<std::string>()) {
      crate::StringIndex str_idx = GetOrCreateString(*v);
      out.Set(str_idx.value);
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
  }

  // Quaternion types
  else if (type_name == "quatf") {
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
  }

  // Type not yet supported
  if (err) {
    *err = "Value conversion not yet implemented for type: " + type_name;
  }
  return false;
}

// ============================================================================
// Layer/PrimSpec Conversion Implementation
// ============================================================================

bool CrateWriter::ConvertLayerToSpecs(const Layer& layer, std::string* err) {
  // 1. Add PseudoRoot spec with layer metadata
  // The PseudoRoot is a special prim at "/" that serves as the root of the scene
  // Layer metadata (upAxis, metersPerUnit, etc.) is stored as fields on PseudoRoot
  {
    Path root_path("/", "");  // Root path - use standard constructor
    crate::FieldValuePairVector root_fields;

    // PseudoRoot needs at least one field to create a valid fieldset
    // Add specifier field (PseudoRoot always uses Def)
    crate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    root_fields.push_back({"specifier", spec_value});

    // Extract and add layer metadata from Layer.metas()
    const LayerMetas& metas = layer.metas();

    // upAxis (token: "X", "Y", or "Z")
    if (metas.upAxis.authored()) {
      Axis axis = metas.upAxis.get_value();
      std::string axis_str = tinyusdz::to_string(axis);  // "X", "Y", or "Z"
      crate::CrateValue axis_value;
      value::token axis_tok(axis_str);
      axis_value.Set(axis_tok);
      root_fields.push_back({"upAxis", axis_value});
    }

    // metersPerUnit (double)
    if (metas.metersPerUnit.authored()) {
      crate::CrateValue meters_value;
      meters_value.Set(metas.metersPerUnit.get_value());
      root_fields.push_back({"metersPerUnit", meters_value});
    }

    // timeCodesPerSecond (double)
    if (metas.timeCodesPerSecond.authored()) {
      crate::CrateValue time_value;
      time_value.Set(metas.timeCodesPerSecond.get_value());
      root_fields.push_back({"timeCodesPerSecond", time_value});
    }

    // framesPerSecond (double)
    if (metas.framesPerSecond.authored()) {
      crate::CrateValue fps_value;
      fps_value.Set(metas.framesPerSecond.get_value());
      root_fields.push_back({"framesPerSecond", fps_value});
    }

    // startTimeCode (double)
    if (metas.startTimeCode.authored()) {
      crate::CrateValue start_value;
      start_value.Set(metas.startTimeCode.get_value());
      root_fields.push_back({"startTimeCode", start_value});
    }

    // endTimeCode (double)
    if (metas.endTimeCode.authored()) {
      crate::CrateValue end_value;
      end_value.Set(metas.endTimeCode.get_value());
      root_fields.push_back({"endTimeCode", end_value});
    }

    // defaultPrim (token)
    if (!metas.defaultPrim.str().empty()) {
      crate::CrateValue default_prim_value;
      default_prim_value.Set(metas.defaultPrim);
      root_fields.push_back({"defaultPrim", default_prim_value});
    }

    // documentation (string)
    if (!metas.doc.value.empty()) {
      crate::CrateValue doc_value;
      doc_value.Set(metas.doc.value);
      root_fields.push_back({"documentation", doc_value});
    }

    // comment (string)
    if (!metas.comment.value.empty()) {
      crate::CrateValue comment_value;
      comment_value.Set(metas.comment.value);
      root_fields.push_back({"comment", comment_value});
    }

    // kilogramsPerUnit (double) - UsdPhysics
    if (metas.kilogramsPerUnit.authored()) {
      crate::CrateValue kg_value;
      kg_value.Set(metas.kilogramsPerUnit.get_value());
      root_fields.push_back({"kilogramsPerUnit", kg_value});
    }

    // customLayerData (dict)
    if (!metas.customLayerData.empty()) {
      crate::CrateValue custom_data_value;
      custom_data_value.Set(metas.customLayerData);
      root_fields.push_back({"customLayerData", custom_data_value});
    }

    // Add PseudoRoot spec
    if (!AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
      if (err) *err = "Failed to add PseudoRoot spec";
      return false;
    }
  }

  // 2. Convert all primspecs in the layer
  for (const auto& item : layer.primspecs()) {
    const auto& prim_name = item.first;
    const auto& primspec = item.second;
    // Each top-level primspec is a direct child of root
    Path root_path = Path();  // Root path

    if (!ConvertPrimSpecRecursive(primspec, root_path, err)) {
      if (err) *err = "Failed to convert primspec: " + prim_name + ". Error: " + (*err);
      return false;
    }
  }

  // NOTE: Sublayers are exported via layer metadata (subLayers/subLayerOffsets fields)
  // We intentionally do NOT traverse and re-export sublayer content at write time because:
  // 1. Sublayers are composition references (asset paths) to external files
  // 2. The Stage already contains merged/composed prims from all sublayers
  // 3. Re-loading sublayers would duplicate content and break on moved files
  // 4. Sublayer metadata alone is sufficient for reconstruction at load time
  // This design is correct - composition happens at LOAD time, not WRITE time.

  return true;
}

bool CrateWriter::ConvertPrimSpecRecursive(
    const PrimSpec& primspec,
    const Path& parent_path,
    std::string* err,
    uint32_t depth) {

  if (size_t(depth) > kMaxDefaultTraversalLimit) {
    if (err) *err = "ConvertPrimSpecRecursive: recursion too deep.";
    return false;
  }

  // 1. Build path for this prim
  Path prim_path = parent_path.AppendPrim(primspec.name());

  // 2. Create fields for this prim spec
  crate::FieldValuePairVector fields;

  // Add specifier (def, over, class) - use Specifier enum directly, not token
  Specifier spec = primspec.specifier();
  // USD files don't allow Invalid specifier - default to Def if we get Invalid
  if (spec == Specifier::Invalid) {
    spec = Specifier::Def;
  }
  crate::CrateValue spec_value;
  spec_value.Set(spec);  // CrateValue supports Specifier directly
  fields.push_back({"specifier", spec_value});

  // Add typeName if present - use value::token type, not token index
  if (!primspec.typeName().empty()) {
    crate::CrateValue type_value;
    value::token tok(primspec.typeName());
    type_value.Set(tok);
    fields.push_back({"typeName", type_value});
  }

  // 3. Convert properties (attributes, relationships, connections)
  for (const auto& item : primspec.props()) {
    const auto& prop_name = item.first;
    const auto& prop = item.second;
    if (!ConvertPropertyToFields(prop_name, prop, prim_path, fields, err)) {
      if (err) *err = "Failed to convert property: " + prop_name + " on prim: " + prim_path.full_path_name();
      return false;
    }
  }

  // 4. Convert metadata from PrimMeta
  const PrimMeta& metas = primspec.metas();

  // Add common metadata
  if (metas.has_active()) {
    crate::CrateValue active_value;
    active_value.Set(metas.get_active());
    fields.push_back({"active", active_value});
  }

  if (metas.has_hidden()) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.get_hidden());
    fields.push_back({"hidden", hidden_value});
  }

  if (metas.has_kind()) {
    crate::CrateValue kind_value;
    // get_kind() returns the kind as a string
    std::string kind_str = metas.get_kind();
    crate::TokenIndex kind_tok = GetOrCreateToken(kind_str);
    kind_value.Set(kind_tok.value);
    fields.push_back({"kind", kind_value});
  }

  if (metas.has_displayName()) {
    crate::CrateValue display_value;
    display_value.Set(metas.get_displayName());
    fields.push_back({"displayName", display_value});
  }

  if (metas.has_doc()) {
    crate::CrateValue doc_value;
    // get_doc() returns StringData, extract .value for the string
    doc_value.Set(metas.get_doc().value);
    fields.push_back({"documentation", doc_value});
  }

  // Add variant selection if present
  if (metas.variants) {
    const VariantSelectionMap& variant_map = metas.variants.value();
    crate::CrateValue variant_value;
    variant_value.Set(variant_map);
    fields.push_back({"variants", variant_value});

    DCOUT("[ConvertPrimSpecRecursive] Added variants field with "
              << variant_map.size() << " selections");
  }

  // Add variantSets list if present
  if (metas.variantSets) {
    // Collect all variant sets from all listops
    std::vector<std::string> variant_sets_list;
    for (const auto& variantSets_op : metas.variantSets.value()) {
      for (const auto& vs : variantSets_op.second) {
        variant_sets_list.push_back(vs);
      }
    }

    // Convert to string array for CrateValue
    crate::CrateValue variant_sets_value;
    variant_sets_value.Set(variant_sets_list);
    fields.push_back({"variantSets", variant_sets_value});

    DCOUT("[ConvertPrimSpecRecursive] Added variantSets field with "
              << variant_sets_list.size() << " sets");
  }

  // Add references if present
  if (metas.references) {
    // Convert all listops to ListOp<Reference>
    ListOp<Reference> ref_listop;
    size_t total_refs = 0;

    for (const auto& ref_op : metas.references.value()) {
      const ListEditQual& qual = ref_op.first;
      const std::vector<Reference>& ref_list = ref_op.second;
      total_refs += ref_list.size();

      // Accumulate items based on ListEditQual
      switch (qual) {
        case ListEditQual::ResetToExplicit:
          ref_listop.ClearAndMakeExplicit();
          ref_listop.SetExplicitItems(ref_list);
          break;
        case ListEditQual::Append:
          ref_listop.SetAppendedItems(ref_list);
          break;
        case ListEditQual::Prepend:
          ref_listop.SetPrependedItems(ref_list);
          break;
        case ListEditQual::Add:
          ref_listop.SetAddedItems(ref_list);
          break;
        case ListEditQual::Delete:
          ref_listop.SetDeletedItems(ref_list);
          break;
        default:
          // Default to explicit
          ref_listop.ClearAndMakeExplicit();
          ref_listop.SetExplicitItems(ref_list);
          break;
      }
    }

    crate::CrateValue ref_value;
    ref_value.Set(ref_listop);
    fields.push_back({"references", ref_value});

    DCOUT("[ConvertPrimSpecRecursive] Added references field with "
              << total_refs << " references");
    (void)total_refs;
  }

  // Add payload if present
  if (metas.payload) {
    // Convert all listops to ListOp<Payload>
    ListOp<Payload> payload_listop;
    size_t total_payloads = 0;

    for (const auto& payload_op : metas.payload.value()) {
      const ListEditQual& qual = payload_op.first;
      const std::vector<Payload>& payload_list = payload_op.second;
      total_payloads += payload_list.size();

      // Accumulate items based on ListEditQual
      switch (qual) {
        case ListEditQual::ResetToExplicit:
          payload_listop.ClearAndMakeExplicit();
          payload_listop.SetExplicitItems(payload_list);
          break;
        case ListEditQual::Append:
          payload_listop.SetAppendedItems(payload_list);
          break;
        case ListEditQual::Prepend:
          payload_listop.SetPrependedItems(payload_list);
          break;
        case ListEditQual::Add:
          payload_listop.SetAddedItems(payload_list);
          break;
        case ListEditQual::Delete:
          payload_listop.SetDeletedItems(payload_list);
          break;
        default:
          // Default to explicit
          payload_listop.ClearAndMakeExplicit();
          payload_listop.SetExplicitItems(payload_list);
          break;
      }
    }

    crate::CrateValue payload_value;
    payload_value.Set(payload_listop);
    fields.push_back({"payload", payload_value});

    DCOUT("[ConvertPrimSpecRecursive] Added payload field with "
              << total_payloads << " payloads");
    (void)total_payloads;
  }

  // Add customData if present
  if (metas.has_customData()) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.get_customData());
    fields.push_back({"customData", custom_data_value});
    DCOUT("[ConvertPrimSpecRecursive] Added customData with "
              << metas.get_customData().size() << " entries");
  }

  // Add unregistered metadata (OpenUSD-compatible: stored as strings)
  for (const auto &item : metas.unregisteredMetas) {
    crate::CrateValue unreg_value;
    unreg_value.Set(item.second);  // Store as string in crate
    fields.push_back({item.first, unreg_value});
  }

  // Add apiSchemas if present
  if (metas.has_apiSchemas()) {
    const APISchemas& api_schemas = metas.get_apiSchemas();

    // Convert APISchemas to TokenListOp
    ListOp<value::token> api_listop;

    // Build list of API schema names as tokens
    std::vector<value::token> api_tokens;
    for (const auto& item : api_schemas.names) {
      const auto& api_name = item.first;
      const auto& instance_name = item.second;
      std::string api_str = to_string(api_name);

      // For multi-apply API schemas, append instance name
      // e.g. "CollectionAPI:material:MainMaterial"
      if (!instance_name.empty()) {
        api_str += ":" + instance_name;
      }

      api_tokens.push_back(value::token(api_str));
    }

    // Set the appropriate list based on ListEditQual
    switch (api_schemas.listOpQual) {
      case ListEditQual::ResetToExplicit:
        api_listop.ClearAndMakeExplicit();
        api_listop.SetExplicitItems(api_tokens);
        break;
      case ListEditQual::Prepend:
        api_listop.SetPrependedItems(api_tokens);
        break;
      case ListEditQual::Append:
        api_listop.SetAppendedItems(api_tokens);
        break;
      case ListEditQual::Add:
        api_listop.SetAddedItems(api_tokens);
        break;
      case ListEditQual::Delete:
        api_listop.SetDeletedItems(api_tokens);
        break;
      default:
        // Default to prepend (USD convention)
        api_listop.SetPrependedItems(api_tokens);
        break;
    }

    crate::CrateValue api_value;
    api_value.Set(api_listop);
    fields.push_back({"apiSchemas", api_value});

    DCOUT("[ConvertPrimSpecRecursive] Added apiSchemas with "
              << api_tokens.size() << " schemas");
  }

  // Add inherits if present
  if (metas.inherits) {
    // Convert all listops to ListOp<Path>
    ListOp<Path> inherits_listop;
    size_t total_inherits = 0;

    for (const auto& inherits_op : metas.inherits.value()) {
      const ListEditQual& qual = inherits_op.first;
      const std::vector<Path>& inherits_list = inherits_op.second;
      total_inherits += inherits_list.size();

      // Accumulate items based on ListEditQual
      switch (qual) {
        case ListEditQual::ResetToExplicit:
          inherits_listop.ClearAndMakeExplicit();
          inherits_listop.SetExplicitItems(inherits_list);
          break;
        case ListEditQual::Append:
          inherits_listop.SetAppendedItems(inherits_list);
          break;
        case ListEditQual::Prepend:
          inherits_listop.SetPrependedItems(inherits_list);
          break;
        case ListEditQual::Add:
          inherits_listop.SetAddedItems(inherits_list);
          break;
        case ListEditQual::Delete:
          inherits_listop.SetDeletedItems(inherits_list);
          break;
        default:
          // Default to explicit
          inherits_listop.ClearAndMakeExplicit();
          inherits_listop.SetExplicitItems(inherits_list);
          break;
      }
    }

    crate::CrateValue inherits_value;
    inherits_value.Set(inherits_listop);
    fields.push_back({"inherits", inherits_value});

    DCOUT("[ConvertPrimSpecRecursive] Added inherits with "
              << total_inherits << " paths");
    (void)total_inherits;
  }

  // Add specializes if present
  if (metas.specializes) {
    // Convert all listops to ListOp<Path>
    ListOp<Path> specializes_listop;
    size_t total_specializes = 0;

    for (const auto& specializes_op : metas.specializes.value()) {
      const ListEditQual& qual = specializes_op.first;
      const std::vector<Path>& specializes_list = specializes_op.second;
      total_specializes += specializes_list.size();

      // Accumulate items based on ListEditQual
      switch (qual) {
        case ListEditQual::ResetToExplicit:
          specializes_listop.ClearAndMakeExplicit();
          specializes_listop.SetExplicitItems(specializes_list);
          break;
        case ListEditQual::Append:
          specializes_listop.SetAppendedItems(specializes_list);
          break;
        case ListEditQual::Prepend:
          specializes_listop.SetPrependedItems(specializes_list);
          break;
        case ListEditQual::Add:
          specializes_listop.SetAddedItems(specializes_list);
          break;
        case ListEditQual::Delete:
          specializes_listop.SetDeletedItems(specializes_list);
          break;
        default:
          // Default to explicit
          specializes_listop.ClearAndMakeExplicit();
          specializes_listop.SetExplicitItems(specializes_list);
          break;
      }
    }

    crate::CrateValue specializes_value;
    specializes_value.Set(specializes_listop);
    fields.push_back({"specializes", specializes_value});

    DCOUT("[ConvertPrimSpecRecursive] Added specializes with "
              << total_specializes << " paths");
    (void)total_specializes;
  }

  // Add assetInfo if present
  if (metas.has_assetInfo()) {
    crate::CrateValue asset_info_value;
    asset_info_value.Set(metas.get_assetInfo());
    fields.push_back({"assetInfo", asset_info_value});
    DCOUT("[ConvertPrimSpecRecursive] Added assetInfo with "
              << metas.get_assetInfo().size() << " entries");
  }

  // Add instanceable if present
  if (metas.has_instanceable()) {
    crate::CrateValue instanceable_value;
    instanceable_value.Set(metas.get_instanceable());
    fields.push_back({"instanceable", instanceable_value});
    DCOUT("[ConvertPrimSpecRecursive] Added instanceable: "
              << metas.get_instanceable());
  }

  // 5. Add spec to file
  if (!AddSpec(prim_path, SpecType::Prim, fields, err)) {
    if (err) *err = "Failed to add spec for prim: " + prim_path.full_path_name();
    return false;
  }

  // 6. Recursively process children
  for (const PrimSpec& child : primspec.children()) {
    if (!ConvertPrimSpecRecursive(child, prim_path, err, depth + 1)) {
      return false;
    }
  }

  return true;
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
    // Convert relationship - creates separate spec, doesn't add to fields
    return ConvertRelationshipToFields(prop_name, prop.get_relationship(), parent_path, err);
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

  // 3. Add variability if not default
  if (attr.variability() != Variability::Varying) {
    crate::CrateValue var_value;
    crate::TokenIndex var_tok = GetOrCreateToken(to_string(attr.variability()));
    var_value.Set(var_tok.value);
    attr_fields.push_back({"variability", var_value});
  }

  // 4. Add metadata from AttrMetas
  const AttrMetas& metas = attr.metas();

  // Add interpolation if specified
  if (metas.has_interpolation()) {
    crate::CrateValue interp_value;
    crate::TokenIndex interp_tok = GetOrCreateToken(metas.get_interpolation().str());
    interp_value.Set(interp_tok.value);
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

  // 1. Check relationship type and add targetPaths
  if (rel.is_blocked()) {
    // Add ValueBlock for the relationship
    crate::CrateValue blocked_value;
    blocked_value.Set(value::ValueBlock());
    rel_fields.push_back({"targetPaths", blocked_value});
  } else if (rel.is_path()) {
    // Single target path - wrap in ListOp<Path> (required by USDC format)
    // For a single path relationship, set it as explicit with one item
    crate::CrateValue path_value;
    ListOp<Path> listop;
    std::vector<Path> targets;
    targets.push_back(rel.targetPath);
    listop.SetExplicitItems(targets);
    path_value.Set(listop);
    rel_fields.push_back({"targetPaths", path_value});
  } else if (rel.is_pathvector()) {
    // Multiple target paths - wrap in ListOp<Path> (required by USDC format)
    // For a pathvector relationship, set items as explicit list
    crate::CrateValue paths_value;
    ListOp<Path> listop;
    std::vector<Path> targets = rel.targetPathVector;
    listop.SetExplicitItems(targets);
    paths_value.Set(listop);
    rel_fields.push_back({"targetPaths", paths_value});
  }
  // DefineOnly relationships don't add targetPaths field

  // 2. Add list edit qualifier if not default
  if (rel.get_listedit_qual() != ListEditQual::ResetToExplicit) {
    crate::CrateValue qual_value;
    // Convert ListEditQual to token
    std::string qual_str;
    switch (rel.get_listedit_qual()) {
      case ListEditQual::Append:
        qual_str = "append";
        break;
      case ListEditQual::Prepend:
        qual_str = "prepend";
        break;
      case ListEditQual::Add:
        qual_str = "add";
        break;
      case ListEditQual::Delete:
        qual_str = "delete";
        break;
      case ListEditQual::Order:
        qual_str = "order";
        break;
      case ListEditQual::ResetToExplicit:
      default:
        qual_str = "explicit";
        break;
    }
    crate::TokenIndex qual_tok = GetOrCreateToken(qual_str);
    qual_value.Set(qual_tok.value);
    rel_fields.push_back({"listOpQual", qual_value});
  }

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
      if (!ConvertPrimRecursive(child_prim, v_path, err)) {
        if (err) *err = "Failed to convert variant prim child: " + child_prim.element_name() + ": " + *err;
        return false;
      }
    }
  }

  return true;
}

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
