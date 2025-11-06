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
std::string serializeOpenPBRToJson(const OpenPBRSurfaceShader& shader, const RenderScene* renderScene = nullptr) {
  std::stringstream json;
  json << "{";
  json << "\"type\": \"OpenPBRSurfaceShader\",";

  // Helper function to convert ColorSpace enum to string
  auto colorSpaceToString = [](tydra::ColorSpace cs) -> const char* {
    switch (cs) {
      case tydra::ColorSpace::sRGB: return "sRGB";
      case tydra::ColorSpace::Lin_sRGB: return "lin_sRGB";
      case tydra::ColorSpace::Rec709: return "rec709";
      case tydra::ColorSpace::Lin_Rec709: return "lin_rec709";
      case tydra::ColorSpace::g22_Rec709: return "g22_rec709";
      case tydra::ColorSpace::g18_Rec709: return "g18_rec709";
      case tydra::ColorSpace::sRGB_Texture: return "srgb_texture";
      case tydra::ColorSpace::Raw: return "raw";
      case tydra::ColorSpace::Lin_ACEScg: return "lin_acescg";
      case tydra::ColorSpace::ACES2065_1: return "aces2065_1";
      case tydra::ColorSpace::Lin_Rec2020: return "lin_rec2020";
      case tydra::ColorSpace::OCIO: return "ocio";
      case tydra::ColorSpace::Lin_DisplayP3: return "lin_displayp3";
      case tydra::ColorSpace::sRGB_DisplayP3: return "srgb_displayp3";
      case tydra::ColorSpace::Custom: return "custom";
      case tydra::ColorSpace::Unknown: return "unknown";
      default: return "unknown";
    }
  };

  // Macro to serialize shader parameter with optional texture info
  auto serializeParam = [&](const std::string& paramName, auto param) {
    json << "\"" << paramName << "\": {";
    json << "\"name\": \"" << paramName << "\",";

    if (param.is_texture() && renderScene) {
      // Texture parameter
      int texId = param.texture_id;
      json << "\"type\": \"texture\",";
      json << "\"textureId\": " << texId;

      // Look up texture image information
      if (texId >= 0 && texId < renderScene->textures.size()) {
        const auto& uvTexture = renderScene->textures[texId];
        if (uvTexture.texture_image_id >= 0 && uvTexture.texture_image_id < renderScene->images.size()) {
          const auto& image = renderScene->images[uvTexture.texture_image_id];
          json << ", \"assetIdentifier\": \"" << image.asset_identifier << "\"";

          // Include texture metadata
          json << ", \"texture\": {";
          json << "\"width\": " << image.width << ",";
          json << "\"height\": " << image.height << ",";
          json << "\"channels\": " << image.channels << ",";
          json << "\"colorSpace\": \"" << colorSpaceToString(image.colorSpace) << "\",";
          json << "\"usdColorSpace\": \"" << colorSpaceToString(image.usdColorSpace) << "\",";
          json << "\"decoded\": " << (image.decoded ? "true" : "false");
          json << "}";
        }
      }
    } else {
      // Scalar parameter
      json << "\"type\": \"value\", \"value\": ";
      if constexpr(std::is_same_v<decltype(param.value), float>) {
        json << param.value;
      } else if constexpr(std::is_same_v<decltype(param.value), std::array<float, 3>>) {
        json << vec3ToJson(param.value);
      } else {
        json << param.value;
      }
    }
    json << "}";
  };

  // Base layer
  json << "\"base\": {";
  serializeParam("base_weight", shader.base_weight); json << ",";
  serializeParam("base_color", shader.base_color); json << ",";
  serializeParam("base_roughness", shader.base_roughness); json << ",";
  serializeParam("base_metalness", shader.base_metalness);
  json << "},";

  // Specular layer
  json << "\"specular\": {";
  serializeParam("specular_weight", shader.specular_weight); json << ",";
  serializeParam("specular_color", shader.specular_color); json << ",";
  serializeParam("specular_roughness", shader.specular_roughness); json << ",";
  serializeParam("specular_ior", shader.specular_ior); json << ",";
  serializeParam("specular_anisotropy", shader.specular_anisotropy); json << ",";
  serializeParam("specular_rotation", shader.specular_rotation);
  json << "},";

  // Transmission layer
  json << "\"transmission\": {";
  serializeParam("transmission_weight", shader.transmission_weight); json << ",";
  serializeParam("transmission_color", shader.transmission_color); json << ",";
  serializeParam("transmission_depth", shader.transmission_depth); json << ",";
  serializeParam("transmission_scatter", shader.transmission_scatter); json << ",";
  serializeParam("transmission_scatter_anisotropy", shader.transmission_scatter_anisotropy); json << ",";
  serializeParam("transmission_dispersion", shader.transmission_dispersion);
  json << "},";

  // Subsurface layer
  json << "\"subsurface\": {";
  serializeParam("subsurface_weight", shader.subsurface_weight); json << ",";
  serializeParam("subsurface_color", shader.subsurface_color); json << ",";
  serializeParam("subsurface_scale", shader.subsurface_scale); json << ",";
  serializeParam("subsurface_anisotropy", shader.subsurface_anisotropy);
  json << "},";

  // Coat layer
  json << "\"coat\": {";
  serializeParam("coat_weight", shader.coat_weight); json << ",";
  serializeParam("coat_color", shader.coat_color); json << ",";
  serializeParam("coat_roughness", shader.coat_roughness); json << ",";
  serializeParam("coat_anisotropy", shader.coat_anisotropy); json << ",";
  serializeParam("coat_rotation", shader.coat_rotation); json << ",";
  serializeParam("coat_ior", shader.coat_ior); json << ",";
  serializeParam("coat_affect_color", shader.coat_affect_color); json << ",";
  serializeParam("coat_affect_roughness", shader.coat_affect_roughness);
  json << "},";

  // Emission
  json << "\"emission\": {";
  serializeParam("emission_luminance", shader.emission_luminance); json << ",";
  serializeParam("emission_color", shader.emission_color);
  json << "},";

  // Geometry
  json << "\"geometry\": {";
  serializeParam("opacity", shader.opacity); json << ",";
  serializeParam("normal", shader.normal); json << ",";
  serializeParam("tangent", shader.tangent);
  json << "}";

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
    SerializationFormat format,
    const RenderScene* renderScene) {

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
      json << ",\"openPBR\": " << serializeOpenPBRToJson(*material.openPBRShader, renderScene);
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
