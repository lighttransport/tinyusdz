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
#include "layer.hh"
#include "pprinter.hh"  // For to_string(Specifier), to_string(Variability)
#include "usdShade.hh"  // For Material and Shader

namespace tinyusdz {
namespace experimental {

// ============================================================================
// Main Conversion Entry Point
// ============================================================================

bool CrateWriter::ConvertStageToSpecs(const Stage& stage, std::string* err) {
  std::cerr << "DEBUG: ConvertStageToSpecs called with "
            << stage.root_prims().size() << " root prims\n";

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
    std::cerr << "[ConvertStageToSpecs] Added timeCodesPerSecond: "
              << metas.timeCodesPerSecond.get_value() << "\n";
  }

  // Add framesPerSecond
  if (metas.framesPerSecond.authored()) {
    crate::CrateValue fps_value;
    fps_value.Set(metas.framesPerSecond.get_value());
    root_fields.push_back({"framesPerSecond", fps_value});
    std::cerr << "[ConvertStageToSpecs] Added framesPerSecond: "
              << metas.framesPerSecond.get_value() << "\n";
  }

  // Add startTimeCode
  if (metas.startTimeCode.authored()) {
    crate::CrateValue start_value;
    start_value.Set(metas.startTimeCode.get_value());
    root_fields.push_back({"startTimeCode", start_value});
    std::cerr << "[ConvertStageToSpecs] Added startTimeCode: "
              << metas.startTimeCode.get_value() << "\n";
  }

  // Add endTimeCode
  if (metas.endTimeCode.authored()) {
    crate::CrateValue end_value;
    end_value.Set(metas.endTimeCode.get_value());
    root_fields.push_back({"endTimeCode", end_value});
    std::cerr << "[ConvertStageToSpecs] Added endTimeCode: "
              << metas.endTimeCode.get_value() << "\n";
  }

  // Add upAxis
  if (metas.upAxis.authored()) {
    crate::CrateValue axis_value;
    // Convert Axis enum to token string
    std::string axis_str = to_string(metas.upAxis.get_value());
    crate::TokenIndex axis_tok = GetOrCreateToken(axis_str);
    axis_value.Set(axis_tok.value);
    root_fields.push_back({"upAxis", axis_value});
    std::cerr << "[ConvertStageToSpecs] Added upAxis: " << axis_str << "\n";
  }

  // Add metersPerUnit
  if (metas.metersPerUnit.authored()) {
    crate::CrateValue mpu_value;
    mpu_value.Set(metas.metersPerUnit.get_value());
    root_fields.push_back({"metersPerUnit", mpu_value});
    std::cerr << "[ConvertStageToSpecs] Added metersPerUnit: "
              << metas.metersPerUnit.get_value() << "\n";
  }

  // Add defaultPrim
  if (!metas.defaultPrim.str().empty()) {
    crate::CrateValue dp_value;
    crate::TokenIndex dp_tok = GetOrCreateToken(metas.defaultPrim.str());
    dp_value.Set(dp_tok.value);
    root_fields.push_back({"defaultPrim", dp_value});
    std::cerr << "[ConvertStageToSpecs] Added defaultPrim: "
              << metas.defaultPrim.str() << "\n";
  }

  // Add doc (documentation)
  if (!metas.doc.value.empty()) {
    crate::CrateValue doc_value;
    doc_value.Set(metas.doc.value);
    root_fields.push_back({"documentation", doc_value});
    std::cerr << "[ConvertStageToSpecs] Added documentation\n";
  }

  // Add customLayerData
  if (!metas.customLayerData.empty()) {
    crate::CrateValue cld_value;
    cld_value.Set(metas.customLayerData);
    root_fields.push_back({"customLayerData", cld_value});
    std::cerr << "[ConvertStageToSpecs] Added customLayerData with "
              << metas.customLayerData.size() << " entries\n";
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
    std::cerr << "[ConvertStageToSpecs] Added subLayers with "
              << sublayer_paths.size() << " layers\n";

    // Serialize subLayerOffsets as LayerOffset array (only if non-default)
    if (has_non_default_offsets) {
      crate::CrateValue offsets_value;
      offsets_value.Set(sublayer_offsets);
      root_fields.push_back({"subLayerOffsets", offsets_value});
      std::cerr << "[ConvertStageToSpecs] Added subLayerOffsets with "
                << sublayer_offsets.size() << " offsets\n";
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

  std::cerr << "DEBUG: Converted " << stage.root_prims().size() << " root prims successfully\n";
  return true;
}

// ============================================================================
// Recursive Prim Conversion
// ============================================================================

bool CrateWriter::ConvertPrimRecursive(
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

  std::cerr << "DEBUG: Converting prim: " << abs_path_str
            << " (type: " << type_name << ")\n";

  // Extract properties from this prim
  crate::FieldValuePairVector fields;

  if (!ExtractPrimProperties(prim, prim_path, fields, err)) {
    if (err) *err = "Failed to extract properties for " + abs_path_str + ": " + *err;
    return false;
  }

  // Add spec for this prim
  SpecType spec_type = SpecType::Prim;

  if (!AddSpec(prim_path, spec_type, fields, err)) {
    if (err) *err = "Failed to add spec for: " + abs_path_str + ": " + *err;
    return false;
  }

  std::cerr << "DEBUG: Added spec for " << abs_path_str << " with "
            << fields.size() << " fields\n";

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
    std::cerr << "WARNING: Failed to add material binding specs: " << (err ? *err : "unknown error") << "\n";
  }

  // Recursively process children
  for (const auto& child : prim.children()) {
    if (!ConvertPrimRecursive(child, prim_path, err)) {
      return false;
    }
  }

  return true;
}

// ============================================================================
// Property Extraction
// ============================================================================

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
    std::cerr << "WARNING: Failed to extract type-specific properties for "
              << type_name << ": " << (err ? *err : "unknown error") << "\n";
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
    return ExtractXformProperties(prim, fields, err);
  } else if (type_name == "Mesh") {
    return ExtractMeshProperties(prim, fields, err);
  } else if (type_name == "Cube") {
    return ExtractCubeProperties(prim, fields, err);
  } else if (type_name == "Sphere") {
    return ExtractSphereProperties(prim, fields, err);
  } else if (type_name == "Cylinder") {
    return ExtractCylinderProperties(prim, fields, err);
  } else if (type_name == "Cone") {
    return ExtractConeProperties(prim, fields, err);
  } else if (type_name == "Capsule") {
    return ExtractCapsuleProperties(prim, fields, err);
  } else if (type_name == "Points") {
    return ExtractPointsProperties(prim, fields, err);
  } else if (type_name == "Material") {
    return ExtractMaterialProperties(prim, prim_path, fields, err);
  } else if (type_name == "Shader") {
    return ExtractShaderProperties(prim, fields, err);
  }

  // For unknown types or types without specific handlers,
  // try extracting from the generic GPrim properties
  return ExtractGPrimProperties(prim, fields, err);
}

// ============================================================================
// Xform Property Extraction
// ============================================================================

bool CrateWriter::ExtractXformProperties(
  const Prim& prim,
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
  return ExtractXformOpsFromXformable(prim, fields, err);
}

// ============================================================================
// Mesh Property Extraction
// ============================================================================

bool CrateWriter::ExtractMeshProperties(
  const Prim& prim,
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
#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
        time = typed_ts.get_samples()[i].t;
        sample_value = typed_ts.get_samples()[i].value;
#else
        time = typed_ts.get_times()[i];
        sample_value = typed_ts.get_values()[i];
#endif

        value::Value v(sample_value);
        ts.add_sample(time, v);
      }

      crate::CrateValue ts_crate_val;
      ts_crate_val.Set(ts);
      fields.push_back({"points.timeSamples", ts_crate_val});

      std::cerr << "DEBUG: Successfully extracted animated mesh points with "
                << ts.size() << " samples\n";
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
          std::cerr << "DEBUG: Extracted " << indices_val.size() << " faceVertexIndices\n";
        }
      }
    }
  }

  // Extract common GPrim properties
  return ExtractGPrimProperties(prim, fields, err);
}

// ============================================================================
// Cube Property Extraction
// ============================================================================

bool CrateWriter::ExtractCubeProperties(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCube* cube = prim.data().as<GeomCube>();
  if (!cube) {
    if (err) *err = "Failed to cast prim to GeomCube";
    return false;
  }

  // Extract size
  if (cube->size.authored()) {
    const Animatable<double>& size_animatable = cube->size.get_value();
    if (size_animatable.has_default()) {
      double size_val;
      if (size_animatable.get_default(&size_val)) {
        crate::CrateValue crate_val;
        value::Value size_value(size_val);
        if (ConvertValue(size_value, crate_val, err)) {
          fields.push_back({"size", crate_val});
          std::cerr << "DEBUG: Extracted cube size: " << size_val << "\n";
        }
      }
    }
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
          std::cerr << "DEBUG: Extracted cube extent\n";
        }
      }
    }
  }

  // Extract common GPrim properties (including xformOps)
  return ExtractGPrimProperties(prim, fields, err);
}

// ============================================================================
// Sphere Property Extraction
// ============================================================================

bool CrateWriter::ExtractSphereProperties(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomSphere* sphere = prim.data().as<GeomSphere>();
  if (!sphere) {
    if (err) *err = "Failed to cast prim to GeomSphere";
    return false;
  }

  // Extract radius
  if (sphere->radius.authored()) {
    const Animatable<double>& radius_animatable = sphere->radius.get_value();
    if (radius_animatable.has_default()) {
      double radius_val;
      if (radius_animatable.get_default(&radius_val)) {
        crate::CrateValue crate_val;
        value::Value radius_value(radius_val);
        if (ConvertValue(radius_value, crate_val, err)) {
          fields.push_back({"radius", crate_val});
          std::cerr << "DEBUG: Extracted sphere radius: " << radius_val << "\n";
        }
      }
    }
  }

  return ExtractGPrimProperties(prim, fields, err);
}

// ============================================================================
// Cylinder Property Extraction
// ============================================================================

bool CrateWriter::ExtractCylinderProperties(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCylinder* cylinder = prim.data().as<GeomCylinder>();
  if (!cylinder) {
    if (err) *err = "Failed to cast prim to GeomCylinder";
    return false;
  }

  // Extract radius
  if (cylinder->radius.authored()) {
    const Animatable<double>& radius_animatable = cylinder->radius.get_value();
    if (radius_animatable.has_default()) {
      double radius_val;
      if (radius_animatable.get_default(&radius_val)) {
        crate::CrateValue crate_val;
        value::Value radius_value(radius_val);
        if (ConvertValue(radius_value, crate_val, err)) {
          fields.push_back({"radius", crate_val});
        }
      }
    }
  }

  // Extract height
  if (cylinder->height.authored()) {
    const Animatable<double>& height_animatable = cylinder->height.get_value();
    if (height_animatable.has_default()) {
      double height_val;
      if (height_animatable.get_default(&height_val)) {
        crate::CrateValue crate_val;
        value::Value height_value(height_val);
        if (ConvertValue(height_value, crate_val, err)) {
          fields.push_back({"height", crate_val});
        }
      }
    }
  }

  return ExtractGPrimProperties(prim, fields, err);
}

bool CrateWriter::ExtractConeProperties(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCone* cone = prim.data().as<GeomCone>();
  if (!cone) {
    if (err) *err = "Failed to cast prim to GeomCone";
    return false;
  }

  // Extract radius
  if (cone->radius.authored()) {
    const Animatable<double>& radius_animatable = cone->radius.get_value();
    if (radius_animatable.has_default()) {
      double radius_val;
      if (radius_animatable.get_default(&radius_val)) {
        crate::CrateValue crate_val;
        value::Value radius_value(radius_val);
        if (ConvertValue(radius_value, crate_val, err)) {
          fields.push_back({"radius", crate_val});
          std::cerr << "DEBUG: Extracted cone radius: " << radius_val << "\n";
        }
      }
    }
  }

  // Extract height
  if (cone->height.authored()) {
    const Animatable<double>& height_animatable = cone->height.get_value();
    if (height_animatable.has_default()) {
      double height_val;
      if (height_animatable.get_default(&height_val)) {
        crate::CrateValue crate_val;
        value::Value height_value(height_val);
        if (ConvertValue(height_value, crate_val, err)) {
          fields.push_back({"height", crate_val});
          std::cerr << "DEBUG: Extracted cone height: " << height_val << "\n";
        }
      }
    }
  }

  // Extract axis
  if (cone->axis.authored()) {
    const Axis& axis_val = cone->axis.get_value();
    std::string axis_str = to_string(axis_val);
    crate::CrateValue axis_crate_val;
    value::token axis_token(axis_str);
    axis_crate_val.Set(axis_token);
    fields.push_back({"axis", axis_crate_val});
    std::cerr << "DEBUG: Extracted cone axis: " << axis_str << "\n";
  }

  return ExtractGPrimProperties(prim, fields, err);
}

bool CrateWriter::ExtractCapsuleProperties(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  const GeomCapsule* capsule = prim.data().as<GeomCapsule>();
  if (!capsule) {
    if (err) *err = "Failed to cast prim to GeomCapsule";
    return false;
  }

  // Extract radius
  if (capsule->radius.authored()) {
    const Animatable<double>& radius_animatable = capsule->radius.get_value();
    if (radius_animatable.has_default()) {
      double radius_val;
      if (radius_animatable.get_default(&radius_val)) {
        crate::CrateValue crate_val;
        value::Value radius_value(radius_val);
        if (ConvertValue(radius_value, crate_val, err)) {
          fields.push_back({"radius", crate_val});
          std::cerr << "DEBUG: Extracted capsule radius: " << radius_val << "\n";
        }
      }
    }
  }

  // Extract height
  if (capsule->height.authored()) {
    const Animatable<double>& height_animatable = capsule->height.get_value();
    if (height_animatable.has_default()) {
      double height_val;
      if (height_animatable.get_default(&height_val)) {
        crate::CrateValue crate_val;
        value::Value height_value(height_val);
        if (ConvertValue(height_value, crate_val, err)) {
          fields.push_back({"height", crate_val});
          std::cerr << "DEBUG: Extracted capsule height: " << height_val << "\n";
        }
      }
    }
  }

  // Extract axis
  if (capsule->axis.authored()) {
    const Axis& axis_val = capsule->axis.get_value();
    std::string axis_str = to_string(axis_val);
    crate::CrateValue axis_crate_val;
    value::token axis_token(axis_str);
    axis_crate_val.Set(axis_token);
    fields.push_back({"axis", axis_crate_val});
    std::cerr << "DEBUG: Extracted capsule axis: " << axis_str << "\n";
  }

  return ExtractGPrimProperties(prim, fields, err);
}

bool CrateWriter::ExtractPointsProperties(
  const Prim& prim,
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
          std::cerr << "DEBUG: Extracted " << points_val.size() << " points\n";
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
          std::cerr << "DEBUG: Extracted " << widths_val.size() << " widths\n";
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
          std::cerr << "DEBUG: Extracted " << normals_val.size() << " normals\n";
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
          std::cerr << "DEBUG: Extracted " << velocities_val.size() << " velocities\n";
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
          std::cerr << "DEBUG: Extracted " << accelerations_val.size() << " accelerations\n";
        }
      }
    }
  }

  return ExtractGPrimProperties(prim, fields, err);
}

// ============================================================================
// XformOp Extraction Helper
// ============================================================================

bool CrateWriter::ExtractXformOpsFromXformable(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  // GPrim and its subclasses aren't directly castable from value::Value
  // Instead, we need to check the specific type and access the Xformable base
  const Xformable* xformable = nullptr;

  // Try casting to specific geometry types that inherit from GPrim/Xformable
  if (auto* cube = prim.data().as<GeomCube>()) {
    xformable = static_cast<const Xformable*>(cube);
    std::cerr << "DEBUG ExtractXformOps: Cast to GeomCube succeeded\n";
  } else if (auto* mesh = prim.data().as<GeomMesh>()) {
    xformable = static_cast<const Xformable*>(mesh);
    std::cerr << "DEBUG ExtractXformOps: Cast to GeomMesh succeeded\n";
  } else if (auto* sphere = prim.data().as<GeomSphere>()) {
    xformable = static_cast<const Xformable*>(sphere);
    std::cerr << "DEBUG ExtractXformOps: Cast to GeomSphere succeeded\n";
  } else if (auto* cylinder = prim.data().as<GeomCylinder>()) {
    xformable = static_cast<const Xformable*>(cylinder);
    std::cerr << "DEBUG ExtractXformOps: Cast to GeomCylinder succeeded\n";
  } else if (auto* cone = prim.data().as<GeomCone>()) {
    xformable = static_cast<const Xformable*>(cone);
    std::cerr << "DEBUG ExtractXformOps: Cast to GeomCone succeeded\n";
  } else if (auto* capsule = prim.data().as<GeomCapsule>()) {
    xformable = static_cast<const Xformable*>(capsule);
    std::cerr << "DEBUG ExtractXformOps: Cast to GeomCapsule succeeded\n";
  } else if (auto* points = prim.data().as<GeomPoints>()) {
    xformable = static_cast<const Xformable*>(points);
    std::cerr << "DEBUG ExtractXformOps: Cast to GeomPoints succeeded\n";
  } else if (auto* xform = prim.data().as<Xform>()) {
    xformable = xform;
    std::cerr << "DEBUG ExtractXformOps: Cast to Xform succeeded\n";
  } else {
    // Not a type we handle yet
    std::cerr << "DEBUG ExtractXformOps: Type not handled yet\n";
    return true;
  }

  if (!xformable) {
    std::cerr << "DEBUG ExtractXformOps: Not xformable\n";
    return true;
  }

  // Check if there are any xformOps
  if (xformable->xformOps.empty()) {
    std::cerr << "DEBUG ExtractXformOps: xformOps vector is empty\n";
    return true;
  }

  std::cerr << "DEBUG: Found " << xformable->xformOps.size() << " xformOps\n";

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

    // Add suffix if present
    if (!xformOp.suffix.empty()) {
      op_name += xformOp.suffix;
    }

    std::cerr << "DEBUG: Extracting xformOp: " << op_name << "\n";

    // Extract the value from the PrimVar
    if (xformOp.has_default()) {
      const value::Value& val = xformOp._var.value_raw();

      crate::CrateValue crate_val;
      if (ConvertValue(val, crate_val, err)) {
        fields.push_back({op_name, crate_val});
        std::cerr << "DEBUG: Successfully extracted xformOp: " << op_name
                  << " (type: " << val.type_name() << ")\n";
      } else {
        std::cerr << "WARNING: Failed to convert xformOp value for " << op_name
                  << ": " << (err ? *err : "unknown") << "\n";
      }
    }

    // Handle time samples for animated xformOps
    if (xformOp.has_timesamples()) {
      const value::TimeSamples& ts = xformOp._var._ts;

      crate::CrateValue ts_crate_val;
      ts_crate_val.Set(ts);
      fields.push_back({op_name + ".timeSamples", ts_crate_val});

      std::cerr << "DEBUG: Successfully extracted animated xformOp: " << op_name
                << " with " << ts.size() << " samples\n";
    }
  }

  // Extract xformOpOrder
  const std::vector<value::token>& xform_op_order = xformable->xformOpOrder();
  if (!xform_op_order.empty()) {
    std::cerr << "DEBUG: Extracting xformOpOrder with " << xform_op_order.size() << " elements\n";

    // Convert token vector to string vector for CrateValue
    std::vector<std::string> op_order_strings;
    for (const auto& tok : xform_op_order) {
      op_order_strings.push_back(tok.str());
    }

    // Create CrateValue for token array
    crate::CrateValue crate_val;
    value::Value op_order_value(op_order_strings);
    if (ConvertValue(op_order_value, crate_val, err)) {
      fields.push_back({"xformOpOrder", crate_val});
    }
  }

  return true;
}

// ============================================================================
// GPrim Common Property Extraction
// ============================================================================

bool CrateWriter::ExtractGPrimProperties(
  const Prim& prim,
  crate::FieldValuePairVector& fields,
  std::string* err
) {
  // Extract xformOps (GPrim inherits from Xformable)
  if (!ExtractXformOpsFromXformable(prim, fields, err)) {
    std::cerr << "WARNING: Failed to extract xformOps: " << (err ? *err : "unknown") << "\n";
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
          crate::TokenIndex vis_tok = GetOrCreateToken(to_string(vis_val));
          vis_crate_val.Set(vis_tok.value);
          fields.push_back({"visibility", vis_crate_val});
          std::cerr << "[ExtractGPrimProperties] Added visibility: " << to_string(vis_val) << "\n";
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
#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
        time = typed_ts.get_samples()[i].t;
        sample_vis = typed_ts.get_samples()[i].value;
#else
        time = typed_ts.get_times()[i];
        sample_vis = typed_ts.get_values()[i];
#endif

        // Convert Visibility enum to token string
        std::string vis_str = to_string(sample_vis);
        value::token vis_token(vis_str);
        value::Value v(vis_token);
        ts.add_sample(time, v);
      }

      crate::CrateValue ts_crate_val;
      ts_crate_val.Set(ts);
      fields.push_back({"visibility.timeSamples", ts_crate_val});

      std::cerr << "[ExtractGPrimProperties] Added animated visibility with "
                << ts.size() << " samples\n";
    }
  }

  // Extract purpose
  if (gprim->purpose.authored()) {
    Purpose purpose_val = gprim->purpose.get_value();
    if (purpose_val != Purpose::Default) {  // Only write if not default
      crate::CrateValue purpose_crate_val;
      crate::TokenIndex purpose_tok = GetOrCreateToken(to_string(purpose_val));
      purpose_crate_val.Set(purpose_tok.value);
      fields.push_back({"purpose", purpose_crate_val});
      std::cerr << "[ExtractGPrimProperties] Added purpose: " << to_string(purpose_val) << "\n";
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
          std::cerr << "[ExtractGPrimProperties] Added extent\n";
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
    std::cerr << "[ExtractGPrimProperties] Added doubleSided: " << double_sided_val << "\n";
  }

  // Extract orientation
  if (gprim->orientation.authored()) {
    Orientation orient_val = gprim->orientation.get_value();
    if (orient_val != Orientation::RightHanded) {  // Only write if not default
      crate::CrateValue orient_crate_val;
      crate::TokenIndex orient_tok = GetOrCreateToken(to_string(orient_val));
      orient_crate_val.Set(orient_tok.value);
      fields.push_back({"orientation", orient_crate_val});
      std::cerr << "[ExtractGPrimProperties] Added orientation: " << to_string(orient_val) << "\n";
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

  std::cerr << "[ExtractMaterialProperties] Processing Material: " << material->name << "\n";

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
  std::cerr << "[AddMaterialOutputSpecs] prim_path: " << prim_path.full_path_name() << "\n";
  std::cerr << "[AddMaterialOutputSpecs] surface.authored(): " << material->surface.authored() << "\n";
  std::cerr << "[AddMaterialOutputSpecs] surface.has_value(): " << material->surface.has_value() << "\n";

  // Handle outputs:surface
  if (material->surface.authored() && material->surface.has_value()) {
    const auto& connections = material->surface.get_connections();
    std::cerr << "[AddMaterialOutputSpecs] surface connections.size(): " << connections.size() << "\n";
    if (!connections.empty()) {
      std::string output_name = "outputs:surface";
      Path output_path = prim_path.AppendProperty(output_name);

      std::cerr << "[AddMaterialOutputSpecs] Adding surface output spec at: " << output_path.full_path_name() << "\n";

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

      std::cerr << "[AddMaterialOutputSpecs] Successfully added surface output spec\n";
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
// UsdPreviewSurface Shader Input Specs (called AFTER Shader prim spec is added)
// ============================================================================

bool CrateWriter::AddUsdPreviewSurfaceInputSpecs(
  const UsdPreviewSurface* preview_surface,
  const Path& prim_path,
  std::string* err
) {
  std::cerr << "[AddUsdPreviewSurfaceInputSpecs] prim_path: " << prim_path.full_path_name() << "\n";

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

  // Extract and add common PBR inputs

  // inputs:diffuseColor (color3f)
  if (preview_surface->diffuseColor.authored()) {
    crate::CrateValue diffuse_value;
    // Handle both constant and animated values
    if (preview_surface->diffuseColor.get_value().is_timesamples()) {
      // TODO: Handle timesampled values
      std::cerr << "[AddUsdPreviewSurfaceInputSpecs] Warning: timesampled diffuseColor not yet supported\n";
    } else {
      value::color3f color;
      if (preview_surface->diffuseColor.get_value().get_scalar(&color)) {
        // Convert color3f to float3 for CrateValue (they're binary compatible)
        value::float3 color_as_float3 = {color.r, color.g, color.b};
        diffuse_value.Set(color_as_float3);
        if (!add_input_spec("inputs:diffuseColor", "color3f", diffuse_value)) {
          return false;
        }
      }
    }
  }

  // inputs:emissiveColor (color3f)
  if (preview_surface->emissiveColor.authored()) {
    crate::CrateValue emissive_value;
    if (!preview_surface->emissiveColor.get_value().is_timesamples()) {
      value::color3f color;
      if (preview_surface->emissiveColor.get_value().get_scalar(&color)) {
        // Convert color3f to float3 for CrateValue
        value::float3 color_as_float3 = {color.r, color.g, color.b};
        emissive_value.Set(color_as_float3);
        if (!add_input_spec("inputs:emissiveColor", "color3f", emissive_value)) {
          return false;
        }
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
    crate::CrateValue spec_color_value;
    if (!preview_surface->specularColor.get_value().is_timesamples()) {
      value::color3f color;
      if (preview_surface->specularColor.get_value().get_scalar(&color)) {
        // Convert color3f to float3 for CrateValue
        value::float3 color_as_float3 = {color.r, color.g, color.b};
        spec_color_value.Set(color_as_float3);
        if (!add_input_spec("inputs:specularColor", "color3f", spec_color_value)) {
          return false;
        }
      }
    }
  }

  // inputs:metallic (float) - for metalness workflow
  if (preview_surface->metallic.authored()) {
    crate::CrateValue metallic_value;
    if (!preview_surface->metallic.get_value().is_timesamples()) {
      float metallic;
      if (preview_surface->metallic.get_value().get_scalar(&metallic)) {
        metallic_value.Set(metallic);
        if (!add_input_spec("inputs:metallic", "float", metallic_value)) {
          return false;
        }
      }
    }
  }

  // inputs:roughness (float)
  if (preview_surface->roughness.authored()) {
    crate::CrateValue roughness_value;
    if (!preview_surface->roughness.get_value().is_timesamples()) {
      float roughness;
      if (preview_surface->roughness.get_value().get_scalar(&roughness)) {
        roughness_value.Set(roughness);
        if (!add_input_spec("inputs:roughness", "float", roughness_value)) {
          return false;
        }
      }
    }
  }

  // inputs:clearcoat (float)
  if (preview_surface->clearcoat.authored()) {
    crate::CrateValue clearcoat_value;
    if (!preview_surface->clearcoat.get_value().is_timesamples()) {
      float clearcoat;
      if (preview_surface->clearcoat.get_value().get_scalar(&clearcoat)) {
        clearcoat_value.Set(clearcoat);
        if (!add_input_spec("inputs:clearcoat", "float", clearcoat_value)) {
          return false;
        }
      }
    }
  }

  // inputs:clearcoatRoughness (float)
  if (preview_surface->clearcoatRoughness.authored()) {
    crate::CrateValue clearcoat_rough_value;
    if (!preview_surface->clearcoatRoughness.get_value().is_timesamples()) {
      float clearcoat_rough;
      if (preview_surface->clearcoatRoughness.get_value().get_scalar(&clearcoat_rough)) {
        clearcoat_rough_value.Set(clearcoat_rough);
        if (!add_input_spec("inputs:clearcoatRoughness", "float", clearcoat_rough_value)) {
          return false;
        }
      }
    }
  }

  // inputs:opacity (float)
  if (preview_surface->opacity.authored()) {
    crate::CrateValue opacity_value;
    if (!preview_surface->opacity.get_value().is_timesamples()) {
      float opacity;
      if (preview_surface->opacity.get_value().get_scalar(&opacity)) {
        opacity_value.Set(opacity);
        if (!add_input_spec("inputs:opacity", "float", opacity_value)) {
          return false;
        }
      }
    }
  }

  // inputs:opacityThreshold (float)
  if (preview_surface->opacityThreshold.authored()) {
    crate::CrateValue opacity_thresh_value;
    if (!preview_surface->opacityThreshold.get_value().is_timesamples()) {
      float opacity_thresh;
      if (preview_surface->opacityThreshold.get_value().get_scalar(&opacity_thresh)) {
        opacity_thresh_value.Set(opacity_thresh);
        if (!add_input_spec("inputs:opacityThreshold", "float", opacity_thresh_value)) {
          return false;
        }
      }
    }
  }

  // inputs:ior (float)
  if (preview_surface->ior.authored()) {
    crate::CrateValue ior_value;
    if (!preview_surface->ior.get_value().is_timesamples()) {
      float ior;
      if (preview_surface->ior.get_value().get_scalar(&ior)) {
        ior_value.Set(ior);
        if (!add_input_spec("inputs:ior", "float", ior_value)) {
          return false;
        }
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
    if (!preview_surface->displacement.get_value().is_timesamples()) {
      float displacement;
      if (preview_surface->displacement.get_value().get_scalar(&displacement)) {
        displacement_value.Set(displacement);
        if (!add_input_spec("inputs:displacement", "float", displacement_value)) {
          return false;
        }
      }
    }
  }

  // inputs:occlusion (float)
  if (preview_surface->occlusion.authored()) {
    crate::CrateValue occlusion_value;
    if (!preview_surface->occlusion.get_value().is_timesamples()) {
      float occlusion;
      if (preview_surface->occlusion.get_value().get_scalar(&occlusion)) {
        occlusion_value.Set(occlusion);
        if (!add_input_spec("inputs:occlusion", "float", occlusion_value)) {
          return false;
        }
      }
    }
  }

  std::cerr << "[AddUsdPreviewSurfaceInputSpecs] Successfully added UsdPreviewSurface inputs\n";
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
  std::cerr << "[AddUsdUVTextureInputSpecs] prim_path: " << prim_path.full_path_name() << "\n";

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
    if (!uv_texture->st.get_value().is_timesamples()) {
      value::texcoord2f st;
      if (uv_texture->st.get_value().get_scalar(&st)) {
        // Convert texcoord2f to float2 for CrateValue
        value::float2 st_as_float2 = {st.s, st.t};
        st_value.Set(st_as_float2);
        if (!add_input_spec("inputs:st", "texCoord2f", st_value)) {
          return false;
        }
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

  std::cerr << "[AddUsdUVTextureInputSpecs] Successfully added UsdUVTexture inputs\n";
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
  std::cerr << "[AddUsdPrimvarReaderInputSpecs] prim_path: " << prim_path.full_path_name()
            << ", type: " << reader_type << "\n";

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
    std::cerr << "[AddUsdPrimvarReaderInputSpecs] Added varname: " << varname_str << "\n";
  }

  // Note: We don't extract the fallback value because it's type-specific
  // and would require templated handling for each type variant.
  // The varname is the most important input for UsdPrimvarReader.
  // If fallback support is needed, it can be added later with type-specific handlers.

  std::cerr << "[AddUsdPrimvarReaderInputSpecs] Successfully added UsdPrimvarReader inputs\n";
  return true;
}

bool CrateWriter::AddUsdTransform2dInputSpecs(
  const UsdTransform2d* transform2d,
  const Path& prim_path,
  std::string* err
) {
  std::cerr << "[AddUsdTransform2dInputSpecs] prim_path: " << prim_path.full_path_name() << "\n";

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

  // Extract inputs:in (float2)
  if (transform2d->in.authored()) {
    crate::CrateValue in_crate_value;
    if (!transform2d->in.get_value().is_timesamples()) {
      value::float2 in_value;
      if (transform2d->in.get_value().get_scalar(&in_value)) {
        in_crate_value.Set(in_value);
        if (!add_input_spec("inputs:in", "float2", in_crate_value)) {
          return false;
        }
        std::cerr << "[AddUsdTransform2dInputSpecs] Added in: (" << in_value[0] << ", " << in_value[1] << ")\n";
      }
    }
  }

  // Extract inputs:rotation (float) - in degrees, CCW
  if (transform2d->rotation.authored()) {
    crate::CrateValue rotation_crate_value;
    if (!transform2d->rotation.get_value().is_timesamples()) {
      float rotation_value;
      if (transform2d->rotation.get_value().get_scalar(&rotation_value)) {
        rotation_crate_value.Set(rotation_value);
        if (!add_input_spec("inputs:rotation", "float", rotation_crate_value)) {
          return false;
        }
        std::cerr << "[AddUsdTransform2dInputSpecs] Added rotation: " << rotation_value << "\n";
      }
    }
  }

  // Extract inputs:scale (float2)
  if (transform2d->scale.authored()) {
    crate::CrateValue scale_crate_value;
    if (!transform2d->scale.get_value().is_timesamples()) {
      value::float2 scale_value;
      if (transform2d->scale.get_value().get_scalar(&scale_value)) {
        scale_crate_value.Set(scale_value);
        if (!add_input_spec("inputs:scale", "float2", scale_crate_value)) {
          return false;
        }
        std::cerr << "[AddUsdTransform2dInputSpecs] Added scale: (" << scale_value[0] << ", " << scale_value[1] << ")\n";
      }
    }
  }

  // Extract inputs:translation (float2)
  if (transform2d->translation.authored()) {
    crate::CrateValue translation_crate_value;
    if (!transform2d->translation.get_value().is_timesamples()) {
      value::float2 translation_value;
      if (transform2d->translation.get_value().get_scalar(&translation_value)) {
        translation_crate_value.Set(translation_value);
        if (!add_input_spec("inputs:translation", "float2", translation_crate_value)) {
          return false;
        }
        std::cerr << "[AddUsdTransform2dInputSpecs] Added translation: (" << translation_value[0] << ", " << translation_value[1] << ")\n";
      }
    }
  }

  std::cerr << "[AddUsdTransform2dInputSpecs] Successfully added UsdTransform2d inputs\n";
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

  std::cerr << "[ExtractShaderProperties] Processing Shader: " << shader->name << "\n";

  // Add info:id (shader type identifier)
  if (!shader->info_id.empty()) {
    crate::CrateValue info_id_value;
    value::token tok(shader->info_id);
    info_id_value.Set(tok);
    fields.push_back({"info:id", info_id_value});
    std::cerr << "[ExtractShaderProperties] Added info:id: " << shader->info_id << "\n";
  }

  // Shader inputs and outputs are handled through the props map
  // which will be processed by ConvertPropertyToFields

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
      // Need to convert each token to token index
      std::vector<uint32_t> token_indices;
      token_indices.reserve(v->size());
      for (const auto& tok : *v) {
        crate::TokenIndex tok_idx = GetOrCreateToken(tok.str());
        token_indices.push_back(tok_idx.value);
      }
      // Note: CrateValue doesn't have Set(vector<uint32_t>) for token indices
      // We need to store as InlinedRep or handle differently
      // For now, let's skip this - we'll handle it when we encounter it
      if (err) *err = "token[] conversion requires special handling (not yet implemented)";
      return false;
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
  std::cout << "[ConvertLayerToSpecs] Starting Layer→Crate conversion\n";

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
  size_t prim_count = 0;
  for (const auto& item : layer.primspecs()) {
    const auto& prim_name = item.first;
    const auto& primspec = item.second;
    std::cout << "[ConvertLayerToSpecs] Converting primspec: " << prim_name << "\n";

    // Each top-level primspec is a direct child of root
    Path root_path = Path();  // Root path

    if (!ConvertPrimSpecRecursive(primspec, root_path, err)) {
      if (err) *err = "Failed to convert primspec: " + prim_name + ". Error: " + (*err);
      return false;
    }

    prim_count++;
  }

  std::cout << "[ConvertLayerToSpecs] Successfully converted " << prim_count << " primspecs\n";

  // 3. TODO: Handle sublayers if present
  // if (layer.HasSublayers()) { ... }

  return true;
}

bool CrateWriter::ConvertPrimSpecRecursive(
    const PrimSpec& primspec,
    const Path& parent_path,
    std::string* err) {

  // 1. Build path for this prim
  Path prim_path = parent_path.AppendPrim(primspec.name());

  std::cout << "[ConvertPrimSpecRecursive] Processing prim: " << prim_path.full_path_name() << "\n";

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
  if (metas.active) {
    crate::CrateValue active_value;
    active_value.Set(metas.active.value());
    fields.push_back({"active", active_value});
  }

  if (metas.hidden) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.hidden.value());
    fields.push_back({"hidden", hidden_value});
  }

  if (metas.kind) {
    crate::CrateValue kind_value;
    // Convert Kind enum to string first using to_string()
    std::string kind_str = to_string(metas.kind.value());
    crate::TokenIndex kind_tok = GetOrCreateToken(kind_str);
    kind_value.Set(kind_tok.value);
    fields.push_back({"kind", kind_value});
  }

  if (metas.displayName) {
    crate::CrateValue display_value;
    display_value.Set(metas.displayName.value());
    fields.push_back({"displayName", display_value});
  }

  if (metas.doc) {
    crate::CrateValue doc_value;
    // StringData.value is the std::string, not .str()
    doc_value.Set(metas.doc.value().value);
    fields.push_back({"documentation", doc_value});
  }

  // Add variant selection if present
  if (metas.variants) {
    const VariantSelectionMap& variant_map = metas.variants.value();
    crate::CrateValue variant_value;
    variant_value.Set(variant_map);
    fields.push_back({"variants", variant_value});

    std::cerr << "[ConvertPrimSpecRecursive] Added variants field with "
              << variant_map.size() << " selections\n";
  }

  // Add variantSets list if present
  if (metas.variantSets) {
    const auto& variant_sets_pair = metas.variantSets.value();
    const std::vector<std::string>& variant_sets_list = variant_sets_pair.second;

    // Convert to string array for CrateValue
    crate::CrateValue variant_sets_value;
    variant_sets_value.Set(variant_sets_list);
    fields.push_back({"variantSets", variant_sets_value});

    std::cerr << "[ConvertPrimSpecRecursive] Added variantSets field with "
              << variant_sets_list.size() << " sets\n";
  }

  // Add references if present
  if (metas.references) {
    const auto& references_pair = metas.references.value();
    const ListEditQual& qual = references_pair.first;
    const std::vector<Reference>& ref_list = references_pair.second;

    // Convert to ListOp<Reference>
    ListOp<Reference> ref_listop;

    // Set the appropriate list based on ListEditQual
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

    crate::CrateValue ref_value;
    ref_value.Set(ref_listop);
    fields.push_back({"references", ref_value});

    std::cerr << "[ConvertPrimSpecRecursive] Added references field with "
              << ref_list.size() << " references\n";
  }

  // Add payload if present
  if (metas.payload) {
    const auto& payload_pair = metas.payload.value();
    const ListEditQual& qual = payload_pair.first;
    const std::vector<Payload>& payload_list = payload_pair.second;

    // Convert to ListOp<Payload>
    ListOp<Payload> payload_listop;

    // Set the appropriate list based on ListEditQual
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

    crate::CrateValue payload_value;
    payload_value.Set(payload_listop);
    fields.push_back({"payload", payload_value});

    std::cerr << "[ConvertPrimSpecRecursive] Added payload field with "
              << payload_list.size() << " payloads\n";
  }

  // Add customData if present
  if (metas.customData) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.customData.value());
    fields.push_back({"customData", custom_data_value});
    std::cerr << "[ConvertPrimSpecRecursive] Added customData with "
              << metas.customData.value().size() << " entries\n";
  }

  // Add apiSchemas if present
  if (metas.apiSchemas) {
    const APISchemas& api_schemas = metas.apiSchemas.value();

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

    std::cerr << "[ConvertPrimSpecRecursive] Added apiSchemas with "
              << api_tokens.size() << " schemas\n";
  }

  // Add inherits if present
  if (metas.inherits) {
    const auto& inherits_pair = metas.inherits.value();
    const ListEditQual& qual = inherits_pair.first;
    const std::vector<Path>& inherits_list = inherits_pair.second;

    // Convert to ListOp<Path>
    ListOp<Path> inherits_listop;

    // Set the appropriate list based on ListEditQual
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

    crate::CrateValue inherits_value;
    inherits_value.Set(inherits_listop);
    fields.push_back({"inherits", inherits_value});

    std::cerr << "[ConvertPrimSpecRecursive] Added inherits with "
              << inherits_list.size() << " paths\n";
  }

  // Add assetInfo if present
  if (metas.assetInfo) {
    crate::CrateValue asset_info_value;
    asset_info_value.Set(metas.assetInfo.value());
    fields.push_back({"assetInfo", asset_info_value});
    std::cerr << "[ConvertPrimSpecRecursive] Added assetInfo with "
              << metas.assetInfo.value().size() << " entries\n";
  }

  // Add instanceable if present
  if (metas.instanceable) {
    crate::CrateValue instanceable_value;
    instanceable_value.Set(metas.instanceable.value());
    fields.push_back({"instanceable", instanceable_value});
    std::cerr << "[ConvertPrimSpecRecursive] Added instanceable: "
              << metas.instanceable.value() << "\n";
  }

  // 5. Add spec to file
  if (!AddSpec(prim_path, SpecType::Prim, fields, err)) {
    if (err) *err = "Failed to add spec for prim: " + prim_path.full_path_name();
    return false;
  }

  // 6. Recursively process children
  for (const PrimSpec& child : primspec.children()) {
    if (!ConvertPrimSpecRecursive(child, prim_path, err)) {
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
    bool success = ConvertAttributeToFields(prop_name, prop.get_attribute(), parent_path, err);

    // TODO: Handle custom flag - needs to be added to the attribute spec, not parent prim
    // For now, custom flag handling is deferred

    return success;
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
    std::string* err) {

  // Create separate spec for this attribute (proper USD Crate format)
  // Attributes are property specs, not prim fields

  crate::FieldValuePairVector attr_fields;

  // Create the attribute path
  Path attr_path = parent_path.AppendProperty(attr_name);

  std::cerr << "[ConvertAttributeToFields] Creating separate spec for attribute: "
            << attr_path.full_path_name() << "\n";

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

    std::cerr << "[ConvertAttributeToFields] Added TimeSamples for " << attr_name
              << " with " << ts.size() << " samples\n";
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
  if (metas.interpolation) {
    crate::CrateValue interp_value;
    crate::TokenIndex interp_tok = GetOrCreateToken(to_string(metas.interpolation.value()));
    interp_value.Set(interp_tok.value);
    attr_fields.push_back({"interpolation", interp_value});
  }

  // Add hidden if specified
  if (metas.hidden) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.hidden.value());
    attr_fields.push_back({"hidden", hidden_value});
  }

  // Add customData if present
  if (metas.customData) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.customData.value());
    attr_fields.push_back({"customData", custom_data_value});
    std::cerr << "[ConvertAttributeToFields] Added customData for " << attr_name
              << " with " << metas.customData.value().size() << " entries\n";
  }

  // Create the attribute spec
  if (!AddSpec(attr_path, SpecType::Attribute, attr_fields, err)) {
    if (err) *err = "Failed to add attribute spec: " + attr_path.full_path_name() + ": " + *err;
    return false;
  }

  std::cerr << "[ConvertAttributeToFields] Successfully created attribute spec with "
            << attr_fields.size() << " fields\n";

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

  std::cerr << "[ConvertRelationshipToFields] Creating separate spec for relationship: "
            << rel_path.full_path_name() << "\n";

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
  if (metas.comment) {
    crate::CrateValue comment_value;
    comment_value.Set(metas.comment.value().value);  // StringData.value
    rel_fields.push_back({"comment", comment_value});
    std::cerr << "[ConvertRelationshipToFields] Added comment for " << rel_name << "\n";
  }

  // Add hidden if present
  if (metas.hidden) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.hidden.value());
    rel_fields.push_back({"hidden", hidden_value});
    std::cerr << "[ConvertRelationshipToFields] Added hidden for " << rel_name << "\n";
  }

  // Add customData if present
  if (metas.customData) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.customData.value());
    rel_fields.push_back({"customData", custom_data_value});
    std::cerr << "[ConvertRelationshipToFields] Added customData for " << rel_name
              << " with " << metas.customData.value().size() << " entries\n";
  }

  // Add displayName if present
  if (metas.displayName) {
    crate::CrateValue display_name_value;
    display_name_value.Set(metas.displayName.value());
    rel_fields.push_back({"displayName", display_name_value});
    std::cerr << "[ConvertRelationshipToFields] Added displayName for " << rel_name << "\n";
  }

  // Add displayGroup if present
  if (metas.displayGroup) {
    crate::CrateValue display_group_value;
    display_group_value.Set(metas.displayGroup.value());
    rel_fields.push_back({"displayGroup", display_group_value});
    std::cerr << "[ConvertRelationshipToFields] Added displayGroup for " << rel_name << "\n";
  }

  // Create the relationship spec
  if (!AddSpec(rel_path, SpecType::Relationship, rel_fields, err)) {
    if (err) *err = "Failed to add relationship spec: " + rel_path.full_path_name() + ": " + *err;
    return false;
  }

  std::cerr << "[ConvertRelationshipToFields] Successfully created relationship spec with "
            << rel_fields.size() << " fields\n";

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

  std::cerr << "[ConvertConnectionToFields] Creating separate spec for connection: "
            << conn_path.full_path_name() << "\n";

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

  std::cerr << "[ConvertConnectionToFields] Successfully created connection spec with "
            << conn_fields.size() << " fields\n";

  return true;
}

} // namespace experimental
} // namespace tinyusdz
