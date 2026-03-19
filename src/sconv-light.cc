// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Stage-converter: Light prim property extraction.
//

#include "sconv-detail.hh"
#include "usdLux.hh"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

namespace tinyusdz {
namespace experimental {

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

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
