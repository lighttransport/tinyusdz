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

namespace tinyusdz {
namespace experimental {

// ============================================================================
// Main Conversion Entry Point
// ============================================================================

bool CrateWriter::ConvertStageToSpecs(const Stage& stage, std::string* err) {
  std::cerr << "DEBUG: ConvertStageToSpecs called with "
            << stage.root_prims().size() << " root prims\n";

  // Add root spec first
  Path root_path("/", "");
  crate::FieldValuePairVector root_fields;

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

    // TODO: Handle time samples
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
  // For now, we'll skip these as they require TypedAttributeWithFallback handling
  // TODO: Extract visibility, purpose, extent, doubleSided, orientation

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

} // namespace experimental
} // namespace tinyusdz
