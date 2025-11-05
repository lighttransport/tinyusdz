// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment, Inc.
//
// Material serialization utilities for WASM/JS bindings

#include "material-serializer.hh"
#include <sstream>
#include <iomanip>
#include "external/yyjson.h"

namespace tinyusdz {
namespace tydra {

namespace {

// Helper to convert vec3/array<float,3> to JSON array
template<typename T>
std::string vec3ToJson(const T& vec) {
  std::stringstream ss;
  ss << "[" << vec[0] << ", " << vec[1] << ", " << vec[2] << "]";
  return ss.str();
}


// Serialize OpenPBRSurfaceShader to JSON
std::string serializeOpenPBRToJson(const OpenPBRSurfaceShader& shader) {
  std::stringstream json;
  json << "{";
  json << "\"type\": \"OpenPBRSurfaceShader\",";

  // Base layer
  json << "\"base_weight\": " << shader.base_weight.value << ",";
  json << "\"base_color\": " << vec3ToJson(shader.base_color.value) << ",";
  json << "\"base_roughness\": " << shader.base_roughness.value << ",";
  json << "\"base_metalness\": " << shader.base_metalness.value << ",";

  // Specular layer
  json << "\"specular_weight\": " << shader.specular_weight.value << ",";
  json << "\"specular_color\": " << vec3ToJson(shader.specular_color.value) << ",";
  json << "\"specular_roughness\": " << shader.specular_roughness.value << ",";
  json << "\"specular_ior\": " << shader.specular_ior.value << ",";
  json << "\"specular_anisotropy\": " << shader.specular_anisotropy.value << ",";
  json << "\"specular_rotation\": " << shader.specular_rotation.value << ",";

  // Transmission layer
  json << "\"transmission_weight\": " << shader.transmission_weight.value << ",";
  json << "\"transmission_color\": " << vec3ToJson(shader.transmission_color.value) << ",";
  json << "\"transmission_depth\": " << shader.transmission_depth.value << ",";
  json << "\"transmission_scatter\": " << vec3ToJson(shader.transmission_scatter.value) << ",";
  json << "\"transmission_scatter_anisotropy\": " << shader.transmission_scatter_anisotropy.value << ",";
  json << "\"transmission_dispersion\": " << shader.transmission_dispersion.value << ",";

  // Subsurface layer
  json << "\"subsurface_weight\": " << shader.subsurface_weight.value << ",";
  json << "\"subsurface_color\": " << vec3ToJson(shader.subsurface_color.value) << ",";
  json << "\"subsurface_scale\": " << shader.subsurface_scale.value << ",";
  json << "\"subsurface_anisotropy\": " << shader.subsurface_anisotropy.value << ",";

  // Coat layer
  json << "\"coat_weight\": " << shader.coat_weight.value << ",";
  json << "\"coat_color\": " << vec3ToJson(shader.coat_color.value) << ",";
  json << "\"coat_roughness\": " << shader.coat_roughness.value << ",";
  json << "\"coat_anisotropy\": " << shader.coat_anisotropy.value << ",";
  json << "\"coat_rotation\": " << shader.coat_rotation.value << ",";
  json << "\"coat_ior\": " << shader.coat_ior.value << ",";
  json << "\"coat_affect_color\": " << vec3ToJson(shader.coat_affect_color.value) << ",";
  json << "\"coat_affect_roughness\": " << shader.coat_affect_roughness.value << ",";

  // Emission
  json << "\"emission_luminance\": " << shader.emission_luminance.value << ",";
  json << "\"emission_color\": " << vec3ToJson(shader.emission_color.value) << ",";

  // Geometry
  json << "\"opacity\": " << shader.opacity.value << ",";
  json << "\"normal\": " << vec3ToJson(shader.normal.value) << ",";
  json << "\"tangent\": " << vec3ToJson(shader.tangent.value);

  json << "}";
  return json.str();
}

// Serialize PreviewSurfaceShader to JSON
std::string serializePreviewSurfaceToJson(const PreviewSurfaceShader& shader) {
  std::stringstream json;
  json << "{";
  json << "\"type\": \"PreviewSurfaceShader\",";

  json << "\"useSpecularWorkflow\": " << (shader.useSpecularWorkflow ? "true" : "false") << ",";
  json << "\"diffuseColor\": " << vec3ToJson(shader.diffuseColor.value) << ",";
  json << "\"emissiveColor\": " << vec3ToJson(shader.emissiveColor.value) << ",";
  json << "\"specularColor\": " << vec3ToJson(shader.specularColor.value) << ",";
  json << "\"metallic\": " << shader.metallic.value << ",";
  json << "\"roughness\": " << shader.roughness.value << ",";
  json << "\"clearcoat\": " << shader.clearcoat.value << ",";
  json << "\"clearcoatRoughness\": " << shader.clearcoatRoughness.value << ",";
  json << "\"opacity\": " << shader.opacity.value << ",";
  json << "\"opacityThreshold\": " << shader.opacityThreshold.value << ",";
  json << "\"ior\": " << shader.ior.value << ",";
  json << "\"normal\": " << vec3ToJson(shader.normal.value) << ",";
  json << "\"displacement\": " << shader.displacement.value << ",";
  json << "\"occlusion\": " << shader.occlusion.value;

  json << "}";
  return json.str();
}

// Serialize OpenPBRSurfaceShader to MaterialX XML
std::string serializeOpenPBRToXml(const OpenPBRSurfaceShader& shader) {
  std::stringstream xml;
  xml << "<?xml version=\"1.0\"?>\n";
  xml << "<materialx version=\"1.39\">\n";
  xml << "  <surfacematerial name=\"Material\" type=\"material\">\n";
  xml << "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"OpenPBR_Surface\" />\n";
  xml << "  </surfacematerial>\n";

  xml << "  <open_pbr_surface name=\"OpenPBR_Surface\" type=\"surfaceshader\">\n";

  // Base layer
  xml << "    <input name=\"base_weight\" type=\"float\" value=\"" << shader.base_weight.value << "\" />\n";
  xml << "    <input name=\"base_color\" type=\"color3\" value=\""
      << shader.base_color.value[0] << ", "
      << shader.base_color.value[1] << ", "
      << shader.base_color.value[2] << "\" />\n";
  xml << "    <input name=\"base_roughness\" type=\"float\" value=\"" << shader.base_roughness.value << "\" />\n";
  xml << "    <input name=\"base_metalness\" type=\"float\" value=\"" << shader.base_metalness.value << "\" />\n";

  // Specular layer
  xml << "    <input name=\"specular_weight\" type=\"float\" value=\"" << shader.specular_weight.value << "\" />\n";
  xml << "    <input name=\"specular_color\" type=\"color3\" value=\""
      << shader.specular_color.value[0] << ", "
      << shader.specular_color.value[1] << ", "
      << shader.specular_color.value[2] << "\" />\n";
  xml << "    <input name=\"specular_roughness\" type=\"float\" value=\"" << shader.specular_roughness.value << "\" />\n";
  xml << "    <input name=\"specular_ior\" type=\"float\" value=\"" << shader.specular_ior.value << "\" />\n";

  // Add other significant properties...
  if (shader.coat_weight.value > 0.0f) {
    xml << "    <input name=\"coat_weight\" type=\"float\" value=\"" << shader.coat_weight.value << "\" />\n";
    xml << "    <input name=\"coat_roughness\" type=\"float\" value=\"" << shader.coat_roughness.value << "\" />\n";
  }

  if (shader.transmission_weight.value > 0.0f) {
    xml << "    <input name=\"transmission_weight\" type=\"float\" value=\"" << shader.transmission_weight.value << "\" />\n";
  }

  if (shader.emission_luminance.value > 0.0f) {
    xml << "    <input name=\"emission_luminance\" type=\"float\" value=\"" << shader.emission_luminance.value << "\" />\n";
    xml << "    <input name=\"emission_color\" type=\"color3\" value=\""
        << shader.emission_color.value[0] << ", "
        << shader.emission_color.value[1] << ", "
        << shader.emission_color.value[2] << "\" />\n";
  }

  xml << "  </open_pbr_surface>\n";
  xml << "</materialx>\n";

  return xml.str();
}

} // anonymous namespace

nonstd::expected<std::string, std::string> serializeMaterial(
    const RenderMaterial& material,
    SerializationFormat format) {

  if (format == SerializationFormat::JSON) {
    std::stringstream json;
    json << "{";
    json << "\"name\": \"" << material.name << "\",";
    json << "\"abs_path\": \"" << material.abs_path << "\",";
    json << "\"display_name\": \"" << material.display_name << "\",";

    // Flags for shader presence
    json << "\"hasUsdPreviewSurface\": " << (material.surfaceShader.has_value() ? "true" : "false") << ",";
    json << "\"hasOpenPBR\": " << (material.openPBRShader.has_value() ? "true" : "false");

    // Add shader data
    if (material.surfaceShader.has_value()) {
      json << ",\"surfaceShader\": " << serializePreviewSurfaceToJson(*material.surfaceShader);
    }

    if (material.openPBRShader.has_value()) {
      json << ",\"openPBRShader\": " << serializeOpenPBRToJson(*material.openPBRShader);
    }

    json << "}";
    return json.str();

  } else if (format == SerializationFormat::XML) {
    // For XML, prioritize MaterialX OpenPBR if available
    if (material.openPBRShader.has_value()) {
      return serializeOpenPBRToXml(*material.openPBRShader);
    } else if (material.surfaceShader.has_value()) {
      // Fallback: convert PreviewSurface to basic MaterialX
      std::stringstream xml;
      xml << "<?xml version=\"1.0\"?>\n";
      xml << "<materialx version=\"1.39\">\n";
      xml << "  <!-- UsdPreviewSurface material (OpenPBR not available) -->\n";
      xml << "  <surfacematerial name=\"" << (material.name.empty() ? "Material" : material.name) << "\" type=\"material\">\n";
      xml << "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"PreviewSurface\" />\n";
      xml << "  </surfacematerial>\n";

      const auto& shader = *material.surfaceShader;
      xml << "  <standard_surface name=\"PreviewSurface\" type=\"surfaceshader\">\n";
      xml << "    <input name=\"base_color\" type=\"color3\" value=\""
          << shader.diffuseColor.value[0] << ", "
          << shader.diffuseColor.value[1] << ", "
          << shader.diffuseColor.value[2] << "\" />\n";
      xml << "    <input name=\"metalness\" type=\"float\" value=\"" << shader.metallic.value << "\" />\n";
      xml << "    <input name=\"specular_roughness\" type=\"float\" value=\"" << shader.roughness.value << "\" />\n";
      xml << "  </standard_surface>\n";
      xml << "</materialx>\n";

      return xml.str();
    } else {
      return nonstd::make_unexpected("Material has no surface shader");
    }
  }

  return nonstd::make_unexpected("Unsupported serialization format");
}

} // namespace tydra
} // namespace tinyusdz
