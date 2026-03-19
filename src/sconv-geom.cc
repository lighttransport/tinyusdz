// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Stage-converter: Geometry prim property extraction.
//

#include "sconv-detail.hh"
#include "usdGeom.hh"
#include "usdSkel.hh"  // For BlendShape

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

namespace tinyusdz {
namespace experimental {

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


  // Extract points (point3f[]) - TypedAttribute returns optional
  if (points->points.authored()) {
    auto points_opt = points->points.get_value();
    if (points_opt.has_value()) {
      const Animatable<std::vector<value::point3f>>& points_anim = points_opt.value();
      if (points_anim.has_default()) {
        std::vector<value::point3f> points_val;
        if (points_anim.get_default(&points_val)) {
          value::Value points_value(points_val);
          if (!AddArrayAttribute("points", points_value, fields, err)) {
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
          if (!AddArrayAttribute("widths", widths_value, fields, err)) {
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
          if (!AddArrayAttribute("normals", normals_value, fields, err)) {
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
          if (!AddArrayAttribute("velocities", velocities_value, fields, err)) {
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
          if (!AddArrayAttribute("accelerations", accelerations_value, fields, err)) {
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



  // Extract type enum (Cubic/Linear)
  {
    const GeomBasisCurves::Type& type_val = basis_curves->type.get_value();
    std::string type_str = (type_val == GeomBasisCurves::Type::Cubic) ? "cubic" : "linear";
    AddEnumAttribute("type", type_str, fields);
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
    AddEnumAttribute("basis", basis_str, fields);
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
    AddEnumAttribute("wrap", wrap_str, fields);
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
          if (!AddArrayAttribute("points", points_value, fields, err)) {
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
          if (!AddArrayAttribute("curveVertexCounts", counts_value, fields, err)) {
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
          if (!AddArrayAttribute("widths", widths_value, fields, err)) {
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
          if (!AddArrayAttribute("normals", normals_value, fields, err)) {
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
          if (!AddArrayAttribute("velocities", velocities_value, fields, err)) {
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
          if (!AddArrayAttribute("accelerations", accelerations_value, fields, err)) {
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


  // Extract points (point3f[]) - TypedAttribute returns optional
  if (nurbs_curves->points.authored()) {
    auto points_opt = nurbs_curves->points.get_value();
    if (points_opt.has_value()) {
      const Animatable<std::vector<value::point3f>>& points_anim = points_opt.value();
      if (points_anim.has_default()) {
        std::vector<value::point3f> points_val;
        if (points_anim.get_default(&points_val)) {
          value::Value points_value(points_val);
          if (!AddArrayAttribute("points", points_value, fields, err)) {
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
          if (!AddArrayAttribute("order", order_value, fields, err)) {
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
          if (!AddArrayAttribute("knots", knots_value, fields, err)) {
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
          if (!AddArrayAttribute("ranges", ranges_value, fields, err)) {
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
          if (!AddArrayAttribute("pointWeights", weights_value, fields, err)) {
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
          if (!AddArrayAttribute("curveVertexCounts", counts_value, fields, err)) {
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
          if (!AddArrayAttribute("widths", widths_value, fields, err)) {
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
          if (!AddArrayAttribute("normals", normals_value, fields, err)) {
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
          if (!AddArrayAttribute("velocities", velocities_value, fields, err)) {
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
          if (!AddArrayAttribute("accelerations", accelerations_value, fields, err)) {
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


  // Extract protoIndices (int32[]) - Indices into prototypes array
  if (instancer->protoIndices.authored()) {
    auto indices_opt = instancer->protoIndices.get_value();
    if (indices_opt.has_value()) {
      const Animatable<std::vector<int32_t>>& indices_anim = indices_opt.value();
      if (indices_anim.has_default()) {
        std::vector<int32_t> indices_val;
        if (indices_anim.get_default(&indices_val)) {
          value::Value indices_value(indices_val);
          if (!AddArrayAttribute("protoIndices", indices_value, fields, err)) {
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
          if (!AddArrayAttribute("positions", positions_value, fields, err)) {
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
          if (!AddArrayAttribute("scales", scales_value, fields, err)) {
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
          if (!AddArrayAttribute("velocities", velocities_value, fields, err)) {
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
          if (!AddArrayAttribute("accelerations", accelerations_value, fields, err)) {
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
          if (!AddArrayAttribute("angularVelocities", ang_vel_value, fields, err)) {
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
          if (!AddArrayAttribute("ids", ids_value, fields, err)) {
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
          if (!AddArrayAttribute("orientations", orientations_value, fields, err)) {
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
          if (!AddArrayAttribute("invisibleIds", invisible_value, fields, err)) {
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
      if (!AddArrayAttribute("inactiveIds", inactive_value, fields, err)) {
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
    AddEnumAttribute("elementType", elem_str, fields);
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
          if (!AddArrayAttribute("indices", indices_value, fields, err)) {
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


  // Extract offsets (vector3f[]) - Position deltas for each vertex
  if (blend_shape->offsets.has_value()) {
    auto offsets_val = blend_shape->offsets.get_value();
    if (offsets_val) {
      value::Value offsets_value(*offsets_val);
      if (!AddArrayAttribute("offsets", offsets_value, fields, err)) {
        return false;
      }
    }
  }

  // Extract normalOffsets (vector3f[]) - Normal deltas for each vertex
  if (blend_shape->normalOffsets.has_value()) {
    auto normal_offsets_val = blend_shape->normalOffsets.get_value();
    if (normal_offsets_val) {
      value::Value normal_offsets_value(*normal_offsets_val);
      if (!AddArrayAttribute("normalOffsets", normal_offsets_value, fields, err)) {
        return false;
      }
    }
  }

  // Extract pointIndices (int[]) - Subset of vertices affected (optional)
  if (blend_shape->pointIndices.has_value()) {
    auto point_indices_val = blend_shape->pointIndices.get_value();
    if (point_indices_val) {
      value::Value point_indices_value(*point_indices_val);
      if (!AddArrayAttribute("pointIndices", point_indices_value, fields, err)) {
        return false;
      }
    }
  }

  // TODO: Handle inbetween blend shapes (stored in props with "inbetweens:" namespace)
  // For now, just extract basic properties

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

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
