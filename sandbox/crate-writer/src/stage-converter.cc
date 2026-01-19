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
#include "../../../src/layer.hh"
#include "../../../src/pprinter.hh"  // For to_string(Specifier), to_string(Variability)

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

  std::cerr << "DEBUG: Converting prim: " << abs_path_str
            << " (type: " << prim.prim_type_name() << ")\n";

  // Extract properties from this prim
  crate::FieldValuePairVector fields;

  if (!ExtractPrimProperties(prim, fields, err)) {
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

  // Add typeName if present
  if (!type_name.empty()) {
    crate::TokenIndex typename_tok = GetOrCreateToken(type_name);
    crate::CrateValue typename_value;
    typename_value.Set(typename_tok.value);
    fields.push_back({"typeName", typename_value});
  }

  // Extract type-specific properties
  if (!ExtractTypeSpecificProperties(prim, type_name, fields, err)) {
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
    // TODO: Handle time samples
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

  // Collect property names for the prim's "properties" field
  std::vector<std::string> property_names;

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

    std::cerr << "DEBUG: Extracting xformOp: " << op_name << "\n";

    // Add property name to the list
    property_names.push_back(op_name);

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
    // NOTE: We store TimeSamples directly with the op_name, and add ".timeSamples" suffix
    // for the field name (to match OpenUSD's format which stores both default and timeSamples)
    if (xformOp.has_timesamples()) {
      const value::TimeSamples& ts = xformOp._var._ts;

      crate::CrateValue ts_crate_val;
      ts_crate_val.Set(ts);
      // Use the property name with .timeSamples suffix
      // This matches how OpenUSD stores animated values on the prim spec
      fields.push_back({op_name + ".timeSamples", ts_crate_val});

      std::cerr << "DEBUG: Successfully extracted animated xformOp: " << op_name
                << " with " << ts.size() << " samples\n";
    }
  }

  // Add xformOpOrder to property names
  const std::vector<value::token>& xform_op_order = xformable->xformOpOrder();
  if (!xform_op_order.empty()) {
    property_names.push_back("xformOpOrder");
  }

  // Add "properties" field to the prim listing all property names
  if (!property_names.empty()) {
    crate::CrateValue props_value;
    props_value.Set(property_names);
    fields.push_back({"properties", props_value});
    std::cerr << "DEBUG: Added properties field with " << property_names.size() << " properties\n";
  }

  // Extract xformOpOrder value
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
    // TODO: Handle animated visibility (TimeSamples)
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

  // 1. Add PseudoRoot spec
  // The PseudoRoot is a special prim at "/" that serves as the root of the scene
  {
    Path root_path("/", "");  // Root path - use standard constructor
    crate::FieldValuePairVector root_fields;

    // PseudoRoot needs at least one field to create a valid fieldset
    // Add specifier field (PseudoRoot always uses Def)
    crate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    root_fields.push_back({"specifier", spec_value});

    // Add PseudoRoot spec
    if (!AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
      if (err) *err = "Failed to add PseudoRoot spec";
      return false;
    }
  }

  // 2. Convert all primspecs in the layer
  size_t prim_count = 0;
  for (const auto& [prim_name, primspec] : layer.primspecs()) {
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
  for (const auto& [prop_name, prop] : primspec.props()) {
    if (!ConvertPropertyToFields(prop_name, prop, fields, err)) {
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
    for (const auto& [api_name, instance_name] : api_schemas.names) {
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
    crate::FieldValuePairVector& fields,
    std::string* err) {

  // Dispatch based on property type
  if (prop.is_empty()) {
    // Empty property - skip
    return true;
  } else if (prop.is_attribute()) {
    // Convert attribute
    bool success = ConvertAttributeToFields(prop_name, prop.get_attribute(), fields, err);

    // Add custom flag if set (Property level, not Attribute level)
    if (success && prop.has_custom()) {
      crate::CrateValue custom_value;
      custom_value.Set(true);
      fields.push_back({prop_name + ".custom", custom_value});
    }

    return success;
  } else if (prop.is_relationship()) {
    // Convert relationship
    return ConvertRelationshipToFields(prop_name, prop.get_relationship(), fields, err);
  } else if (prop.is_attribute_connection()) {
    // Convert connection
    return ConvertConnectionToFields(prop_name, prop.get_attribute(), fields, err);
  } else {
    if (err) *err = "Unknown property type for: " + prop_name;
    return false;
  }
}

bool CrateWriter::ConvertAttributeToFields(
    const std::string& attr_name,
    const Attribute& attr,
    crate::FieldValuePairVector& fields,
    std::string* err) {

  // NOTE: This function adds fields to the parent prim spec
  // In a proper implementation, attributes should create separate specs
  // with SpecType::Attribute, but for now we add fields to the parent

  // 1. Add type name
  if (!attr.type_name().empty()) {
    crate::CrateValue type_value;
    // GetOrCreateToken returns TokenIndex, extract the uint32_t value
    crate::TokenIndex tok_idx = GetOrCreateToken(attr.type_name());
    type_value.Set(tok_idx.value);
    fields.push_back({attr_name + ".typeName", type_value});
  }

  // 2. Extract value from PrimVar
  const primvar::PrimVar& pvar = attr.get_var();

  // 2a. Check for blocked value
  if (pvar.is_blocked()) {
    // Add a ValueBlock indicator
    // CrateValue doesn't have SetValueBlock(), need to use value::ValueBlock
    crate::CrateValue blocked_value;
    blocked_value.Set(value::ValueBlock());
    fields.push_back({attr_name, blocked_value});
    return true;
  }

  // 2b. Extract default value
  if (pvar.has_value()) {
    const value::Value& val = pvar.value_raw();
    crate::CrateValue crate_val;

    // Use existing ConvertValue helper
    if (!ConvertValue(val, crate_val, err)) {
      if (err) *err = "Failed to convert attribute value: " + attr_name;
      return false;
    }

    fields.push_back({attr_name, crate_val});
  }

  // 2c. Extract time samples
  if (pvar.has_timesamples()) {
    // Convert TimeSamples to CrateValue
    const value::TimeSamples& ts = pvar._ts;

    // Create a CrateValue with TimeSamples
    crate::CrateValue ts_crate_val;
    ts_crate_val.Set(ts);

    // Add the timeSamples field
    fields.push_back({attr_name + ".timeSamples", ts_crate_val});

    std::cerr << "[ConvertAttributeToFields] Added TimeSamples for " << attr_name
              << " with " << ts.size() << " samples\n";
  }

  // 3. Add variability if not default
  if (attr.variability() != Variability::Varying) {
    crate::CrateValue var_value;
    crate::TokenIndex var_tok = GetOrCreateToken(to_string(attr.variability()));
    var_value.Set(var_tok.value);
    fields.push_back({attr_name + ".variability", var_value});
  }

  // 4. Add custom flag if set (Property level, not AttrMeta)
  // Note: The custom flag is typically set at the Property level
  // We'll need to handle this when calling from ConvertPropertyToFields

  // 5. Add metadata from AttrMetas
  const AttrMetas& metas = attr.metas();

  // Add interpolation if specified
  if (metas.interpolation) {
    crate::CrateValue interp_value;
    crate::TokenIndex interp_tok = GetOrCreateToken(to_string(metas.interpolation.value()));
    interp_value.Set(interp_tok.value);
    fields.push_back({attr_name + ".interpolation", interp_value});
  }

  // Add hidden if specified
  if (metas.hidden) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.hidden.value());
    fields.push_back({attr_name + ".hidden", hidden_value});
  }

  // Add customData if present
  if (metas.customData) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.customData.value());
    fields.push_back({attr_name + ".customData", custom_data_value});
    std::cerr << "[ConvertAttributeToFields] Added customData for " << attr_name
              << " with " << metas.customData.value().size() << " entries\n";
  }

  return true;
}

bool CrateWriter::ConvertRelationshipToFields(
    const std::string& rel_name,
    const Relationship& rel,
    crate::FieldValuePairVector& fields,
    std::string* err) {

  // IMPORTANT: In proper USD Crate format, relationships should create
  // separate specs with SpecType::Relationship. For now, we add fields
  // to the parent prim spec.

  // TODO: Create separate relationship spec:
  // Path rel_path = parent_path.AppendProperty(rel_name);
  // AddSpec(rel_path, SpecType::Relationship, rel_fields, err);

  // 1. Check relationship type and add targetPaths
  if (rel.is_blocked()) {
    // Add ValueBlock for the relationship
    crate::CrateValue blocked_value;
    blocked_value.Set(value::ValueBlock());
    fields.push_back({rel_name, blocked_value});
    return true;
  } else if (rel.is_path()) {
    // Single target path - wrap in a vector since CrateValue doesn't have Set(Path)
    crate::CrateValue path_value;
    std::vector<Path> targets;
    targets.push_back(rel.targetPath);
    path_value.Set(targets);
    fields.push_back({rel_name + ".targetPaths", path_value});
  } else if (rel.is_pathvector()) {
    // Multiple target paths
    crate::CrateValue paths_value;
    // Make a copy of the path vector
    std::vector<Path> targets = rel.targetPathVector;
    paths_value.Set(targets);
    fields.push_back({rel_name + ".targetPaths", paths_value});
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
    fields.push_back({rel_name + ".listOpQual", qual_value});
  }

  // 3. Relationships have implicit uniform variability
  // Add it explicitly in the Crate format
  crate::CrateValue var_value;
  crate::TokenIndex var_tok = GetOrCreateToken("uniform");
  var_value.Set(var_tok.value);
  fields.push_back({rel_name + ".variability", var_value});

  // 4. Add relationship metadata from AttrMetas
  const AttrMeta& metas = rel.metas();

  // Add comment if present
  if (metas.comment) {
    crate::CrateValue comment_value;
    comment_value.Set(metas.comment.value().value);  // StringData.value
    fields.push_back({rel_name + ".comment", comment_value});
    std::cerr << "[ConvertRelationshipToFields] Added comment for " << rel_name << "\n";
  }

  // Add hidden if present
  if (metas.hidden) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.hidden.value());
    fields.push_back({rel_name + ".hidden", hidden_value});
    std::cerr << "[ConvertRelationshipToFields] Added hidden for " << rel_name << "\n";
  }

  // Add customData if present
  if (metas.customData) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.customData.value());
    fields.push_back({rel_name + ".customData", custom_data_value});
    std::cerr << "[ConvertRelationshipToFields] Added customData for " << rel_name
              << " with " << metas.customData.value().size() << " entries\n";
  }

  // Add displayName if present
  if (metas.displayName) {
    crate::CrateValue display_name_value;
    display_name_value.Set(metas.displayName.value());
    fields.push_back({rel_name + ".displayName", display_name_value});
    std::cerr << "[ConvertRelationshipToFields] Added displayName for " << rel_name << "\n";
  }

  // Add displayGroup if present
  if (metas.displayGroup) {
    crate::CrateValue display_group_value;
    display_group_value.Set(metas.displayGroup.value());
    fields.push_back({rel_name + ".displayGroup", display_group_value});
    std::cerr << "[ConvertRelationshipToFields] Added displayGroup for " << rel_name << "\n";
  }

  return true;
}

bool CrateWriter::ConvertConnectionToFields(
    const std::string& conn_name,
    const Attribute& attr,
    crate::FieldValuePairVector& fields,
    std::string* err) {

  // IMPORTANT: In proper USD Crate format, attribute connections should create
  // separate specs with SpecType::Connection. For now, we add fields
  // to the parent prim spec.

  // TODO: Create separate connection spec:
  // Path conn_path = parent_path.AppendProperty(conn_name).AppendProperty("connect");
  // AddSpec(conn_path, SpecType::Connection, conn_fields, err);

  // Attribute connections represent ".connect" relationships
  if (!attr.has_connections()) {
    // No connections to process
    return true;
  }

  // 1. Add connection paths
  const std::vector<Path>& conn_paths = attr.connections();

  if (conn_paths.size() == 1) {
    // Single connection path - wrap in a vector since CrateValue doesn't have Set(Path)
    crate::CrateValue conn_value;
    std::vector<Path> wrapped_path;
    wrapped_path.push_back(conn_paths[0]);
    conn_value.Set(wrapped_path);
    fields.push_back({conn_name + ".connect", conn_value});
  } else if (conn_paths.size() > 1) {
    // Multiple connection paths
    crate::CrateValue conns_value;
    // Make a copy of the paths vector
    std::vector<Path> paths_copy = conn_paths;
    conns_value.Set(paths_copy);
    fields.push_back({conn_name + ".connect", conns_value});
  }

  // 2. Add type name for the connection (same as attribute type)
  if (!attr.type_name().empty()) {
    crate::CrateValue type_value;
    crate::TokenIndex type_tok = GetOrCreateToken(attr.type_name());
    type_value.Set(type_tok.value);
    fields.push_back({conn_name + ".connect.typeName", type_value});
  }

  // 3. Connections don't have variability or other metadata typically
  // They inherit from the attribute they're connected to

  return true;
}

} // namespace experimental
} // namespace tinyusdz
