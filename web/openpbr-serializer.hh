// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment, Inc.
//
// OpenPBR Material Serializer for Web/JS binding
// Converts OpenPBR material data from Tydra RenderMaterial to JSON or XML format

#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include "nonstd/expected.hpp"
#include "tydra/render-data.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "external/jsonhpp/nlohmann/json.hpp"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {
namespace tydra {

enum class SerializationFormat {
  JSON,
  XML
};

// Helper to serialize float ShaderParam to JSON
static nlohmann::json serializeShaderParamFloat(const ShaderParam<float>& param, const std::string& name) {
  nlohmann::json j;
  j["name"] = name;

  if (param.is_texture()) {
    j["type"] = "texture";
    j["textureId"] = param.texture_id;
  } else {
    j["type"] = "value";
    j["value"] = param.value;
  }

  return j;
}

// Helper to serialize vec3 ShaderParam to JSON
static nlohmann::json serializeShaderParamVec3(const ShaderParam<vec3>& param, const std::string& name) {
  nlohmann::json j;
  j["name"] = name;

  if (param.is_texture()) {
    j["type"] = "texture";
    j["textureId"] = param.texture_id;
  } else {
    j["type"] = "value";
    j["value"] = {param.value[0], param.value[1], param.value[2]};
  }

  return j;
}

// Helper to serialize float ShaderParam to XML
static std::string serializeShaderParamXMLFloat(const ShaderParam<float>& param, const std::string& name, int indent = 0) {
  std::stringstream ss;
  std::string indentStr(indent * 2, ' ');

  ss << indentStr << "<parameter name=\"" << name << "\"";

  if (param.is_texture()) {
    ss << " type=\"texture\" textureId=\"" << param.texture_id << "\"/>\n";
  } else {
    ss << " type=\"value\">";
    ss << std::fixed << std::setprecision(6) << param.value;
    ss << "</parameter>\n";
  }

  return ss.str();
}

// Helper to serialize vec3 ShaderParam to XML
static std::string serializeShaderParamXMLVec3(const ShaderParam<vec3>& param, const std::string& name, int indent = 0) {
  std::stringstream ss;
  std::string indentStr(indent * 2, ' ');

  ss << indentStr << "<parameter name=\"" << name << "\"";

  if (param.is_texture()) {
    ss << " type=\"texture\" textureId=\"" << param.texture_id << "\"/>\n";
  } else {
    ss << " type=\"value\">";
    ss << std::fixed << std::setprecision(6)
       << param.value[0] << ", "
       << param.value[1] << ", "
       << param.value[2];
    ss << "</parameter>\n";
  }

  return ss.str();
}

// Serialize OpenPBR material to JSON
static nlohmann::json serializeOpenPBRToJSON(const OpenPBRSurfaceShader& shader) {
  nlohmann::json j;
  j["type"] = "OpenPBR";

  // Base layer
  nlohmann::json baseLayer;
  baseLayer["weight"] = serializeShaderParamFloat(shader.base_weight, "base_weight");
  baseLayer["color"] = serializeShaderParamVec3(shader.base_color, "base_color");
  baseLayer["roughness"] = serializeShaderParamFloat(shader.base_roughness, "base_roughness");
  baseLayer["metalness"] = serializeShaderParamFloat(shader.base_metalness, "base_metalness");
  j["base"] = baseLayer;

  // Specular layer
  nlohmann::json specularLayer;
  specularLayer["weight"] = serializeShaderParamFloat(shader.specular_weight, "specular_weight");
  specularLayer["color"] = serializeShaderParamVec3(shader.specular_color, "specular_color");
  specularLayer["roughness"] = serializeShaderParamFloat(shader.specular_roughness, "specular_roughness");
  specularLayer["ior"] = serializeShaderParamFloat(shader.specular_ior, "specular_ior");
  specularLayer["iorLevel"] = serializeShaderParamFloat(shader.specular_ior_level, "specular_ior_level");
  specularLayer["anisotropy"] = serializeShaderParamFloat(shader.specular_anisotropy, "specular_anisotropy");
  specularLayer["rotation"] = serializeShaderParamFloat(shader.specular_rotation, "specular_rotation");
  j["specular"] = specularLayer;

  // Transmission
  nlohmann::json transmissionLayer;
  transmissionLayer["weight"] = serializeShaderParamFloat(shader.transmission_weight, "transmission_weight");
  transmissionLayer["color"] = serializeShaderParamVec3(shader.transmission_color, "transmission_color");
  transmissionLayer["depth"] = serializeShaderParamFloat(shader.transmission_depth, "transmission_depth");
  transmissionLayer["scatter"] = serializeShaderParamVec3(shader.transmission_scatter, "transmission_scatter");
  transmissionLayer["scatterAnisotropy"] = serializeShaderParamFloat(shader.transmission_scatter_anisotropy, "transmission_scatter_anisotropy");
  transmissionLayer["dispersion"] = serializeShaderParamFloat(shader.transmission_dispersion, "transmission_dispersion");
  j["transmission"] = transmissionLayer;

  // Subsurface
  nlohmann::json subsurfaceLayer;
  subsurfaceLayer["weight"] = serializeShaderParamFloat(shader.subsurface_weight, "subsurface_weight");
  subsurfaceLayer["color"] = serializeShaderParamVec3(shader.subsurface_color, "subsurface_color");
  subsurfaceLayer["radius"] = serializeShaderParamVec3(shader.subsurface_radius, "subsurface_radius");
  subsurfaceLayer["scale"] = serializeShaderParamFloat(shader.subsurface_scale, "subsurface_scale");
  subsurfaceLayer["anisotropy"] = serializeShaderParamFloat(shader.subsurface_anisotropy, "subsurface_anisotropy");
  j["subsurface"] = subsurfaceLayer;

  // Sheen
  nlohmann::json sheenLayer;
  sheenLayer["weight"] = serializeShaderParamFloat(shader.sheen_weight, "sheen_weight");
  sheenLayer["color"] = serializeShaderParamVec3(shader.sheen_color, "sheen_color");
  sheenLayer["roughness"] = serializeShaderParamFloat(shader.sheen_roughness, "sheen_roughness");
  j["sheen"] = sheenLayer;

  // Coat
  nlohmann::json coatLayer;
  coatLayer["weight"] = serializeShaderParamFloat(shader.coat_weight, "coat_weight");
  coatLayer["color"] = serializeShaderParamVec3(shader.coat_color, "coat_color");
  coatLayer["roughness"] = serializeShaderParamFloat(shader.coat_roughness, "coat_roughness");
  coatLayer["anisotropy"] = serializeShaderParamFloat(shader.coat_anisotropy, "coat_anisotropy");
  coatLayer["rotation"] = serializeShaderParamFloat(shader.coat_rotation, "coat_rotation");
  coatLayer["ior"] = serializeShaderParamFloat(shader.coat_ior, "coat_ior");
  coatLayer["affectColor"] = serializeShaderParamVec3(shader.coat_affect_color, "coat_affect_color");
  coatLayer["affectRoughness"] = serializeShaderParamFloat(shader.coat_affect_roughness, "coat_affect_roughness");
  j["coat"] = coatLayer;

  // Emission
  nlohmann::json emissionLayer;
  emissionLayer["luminance"] = serializeShaderParamFloat(shader.emission_luminance, "emission_luminance");
  emissionLayer["color"] = serializeShaderParamVec3(shader.emission_color, "emission_color");
  j["emission"] = emissionLayer;

  // Geometry modifiers
  nlohmann::json geometryLayer;
  geometryLayer["opacity"] = serializeShaderParamFloat(shader.opacity, "opacity");
  geometryLayer["normal"] = serializeShaderParamVec3(shader.normal, "normal");
  geometryLayer["tangent"] = serializeShaderParamVec3(shader.tangent, "tangent");
  j["geometry"] = geometryLayer;

  return j;
}

// Serialize OpenPBR material to XML (MaterialX style)
static std::string serializeOpenPBRToXML(const OpenPBRSurfaceShader& shader) {
  std::stringstream ss;

  ss << "<?xml version=\"1.0\"?>\n";
  ss << "<materialx version=\"1.38\">\n";
  ss << "  <surfacematerial name=\"OpenPBR_Material\" type=\"material\">\n";
  ss << "    <shaderref name=\"OpenPBR_Surface\" node=\"OpenPBR_Shader\"/>\n";
  ss << "  </surfacematerial>\n";
  ss << "\n";
  ss << "  <open_pbr_surface name=\"OpenPBR_Shader\" type=\"surfaceshader\">\n";

  // Base layer
  ss << "    <!-- Base Layer -->\n";
  ss << serializeShaderParamXMLFloat(shader.base_weight, "base_weight", 2);
  ss << serializeShaderParamXMLVec3(shader.base_color, "base_color", 2);
  ss << serializeShaderParamXMLFloat(shader.base_roughness, "base_roughness", 2);
  ss << serializeShaderParamXMLFloat(shader.base_metalness, "base_metalness", 2);
  ss << "\n";

  // Specular layer
  ss << "    <!-- Specular Layer -->\n";
  ss << serializeShaderParamXMLFloat(shader.specular_weight, "specular_weight", 2);
  ss << serializeShaderParamXMLVec3(shader.specular_color, "specular_color", 2);
  ss << serializeShaderParamXMLFloat(shader.specular_roughness, "specular_roughness", 2);
  ss << serializeShaderParamXMLFloat(shader.specular_ior, "specular_ior", 2);
  ss << serializeShaderParamXMLFloat(shader.specular_ior_level, "specular_ior_level", 2);
  ss << serializeShaderParamXMLFloat(shader.specular_anisotropy, "specular_anisotropy", 2);
  ss << serializeShaderParamXMLFloat(shader.specular_rotation, "specular_rotation", 2);
  ss << "\n";

  // Transmission
  ss << "    <!-- Transmission Layer -->\n";
  ss << serializeShaderParamXMLFloat(shader.transmission_weight, "transmission_weight", 2);
  ss << serializeShaderParamXMLVec3(shader.transmission_color, "transmission_color", 2);
  ss << serializeShaderParamXMLFloat(shader.transmission_depth, "transmission_depth", 2);
  ss << serializeShaderParamXMLVec3(shader.transmission_scatter, "transmission_scatter", 2);
  ss << serializeShaderParamXMLFloat(shader.transmission_scatter_anisotropy, "transmission_scatter_anisotropy", 2);
  ss << serializeShaderParamXMLFloat(shader.transmission_dispersion, "transmission_dispersion", 2);
  ss << "\n";

  // Subsurface
  ss << "    <!-- Subsurface Scattering -->\n";
  ss << serializeShaderParamXMLFloat(shader.subsurface_weight, "subsurface_weight", 2);
  ss << serializeShaderParamXMLVec3(shader.subsurface_color, "subsurface_color", 2);
  ss << serializeShaderParamXMLVec3(shader.subsurface_radius, "subsurface_radius", 2);
  ss << serializeShaderParamXMLFloat(shader.subsurface_scale, "subsurface_scale", 2);
  ss << serializeShaderParamXMLFloat(shader.subsurface_anisotropy, "subsurface_anisotropy", 2);
  ss << "\n";

  // Sheen
  ss << "    <!-- Sheen Layer -->\n";
  ss << serializeShaderParamXMLFloat(shader.sheen_weight, "sheen_weight", 2);
  ss << serializeShaderParamXMLVec3(shader.sheen_color, "sheen_color", 2);
  ss << serializeShaderParamXMLFloat(shader.sheen_roughness, "sheen_roughness", 2);
  ss << "\n";

  // Coat
  ss << "    <!-- Coat Layer -->\n";
  ss << serializeShaderParamXMLFloat(shader.coat_weight, "coat_weight", 2);
  ss << serializeShaderParamXMLVec3(shader.coat_color, "coat_color", 2);
  ss << serializeShaderParamXMLFloat(shader.coat_roughness, "coat_roughness", 2);
  ss << serializeShaderParamXMLFloat(shader.coat_anisotropy, "coat_anisotropy", 2);
  ss << serializeShaderParamXMLFloat(shader.coat_rotation, "coat_rotation", 2);
  ss << serializeShaderParamXMLFloat(shader.coat_ior, "coat_ior", 2);
  ss << serializeShaderParamXMLVec3(shader.coat_affect_color, "coat_affect_color", 2);
  ss << serializeShaderParamXMLFloat(shader.coat_affect_roughness, "coat_affect_roughness", 2);
  ss << "\n";

  // Emission
  ss << "    <!-- Emission -->\n";
  ss << serializeShaderParamXMLFloat(shader.emission_luminance, "emission_luminance", 2);
  ss << serializeShaderParamXMLVec3(shader.emission_color, "emission_color", 2);
  ss << "\n";

  // Geometry modifiers
  ss << "    <!-- Geometry Modifiers -->\n";
  ss << serializeShaderParamXMLFloat(shader.opacity, "opacity", 2);
  ss << serializeShaderParamXMLVec3(shader.normal, "normal", 2);
  ss << serializeShaderParamXMLVec3(shader.tangent, "tangent", 2);

  ss << "  </open_pbr_surface>\n";
  ss << "</materialx>\n";

  return ss.str();
}

// Main serialization function for RenderMaterial
static nonstd::expected<std::string, std::string>
serializeMaterial(const RenderMaterial& material, SerializationFormat format) {

  if (format == SerializationFormat::JSON) {
    nlohmann::json j;
    j["name"] = material.name;
    j["absPath"] = material.abs_path;
    j["displayName"] = material.display_name;

    // Check what material types are available
    j["hasUsdPreviewSurface"] = material.hasUsdPreviewSurface();
    j["hasOpenPBR"] = material.hasOpenPBR();

    if (material.hasOpenPBR()) {
      j["openPBR"] = serializeOpenPBRToJSON(*material.openPBRShader);
    }

    if (material.hasUsdPreviewSurface()) {
      // Also include UsdPreviewSurface data if available
      nlohmann::json usdPreview;
      const auto& shader = *material.surfaceShader;

      usdPreview["type"] = "UsdPreviewSurface";
      usdPreview["useSpecularWorkflow"] = shader.useSpecularWorkflow;
      usdPreview["diffuseColor"] = serializeShaderParamVec3(shader.diffuseColor, "diffuseColor");
      usdPreview["emissiveColor"] = serializeShaderParamVec3(shader.emissiveColor, "emissiveColor");
      usdPreview["specularColor"] = serializeShaderParamVec3(shader.specularColor, "specularColor");
      usdPreview["metallic"] = serializeShaderParamFloat(shader.metallic, "metallic");
      usdPreview["roughness"] = serializeShaderParamFloat(shader.roughness, "roughness");
      usdPreview["clearcoat"] = serializeShaderParamFloat(shader.clearcoat, "clearcoat");
      usdPreview["clearcoatRoughness"] = serializeShaderParamFloat(shader.clearcoatRoughness, "clearcoatRoughness");
      usdPreview["opacity"] = serializeShaderParamFloat(shader.opacity, "opacity");
      usdPreview["opacityThreshold"] = serializeShaderParamFloat(shader.opacityThreshold, "opacityThreshold");
      usdPreview["ior"] = serializeShaderParamFloat(shader.ior, "ior");
      usdPreview["normal"] = serializeShaderParamVec3(shader.normal, "normal");
      usdPreview["displacement"] = serializeShaderParamFloat(shader.displacement, "displacement");
      usdPreview["occlusion"] = serializeShaderParamFloat(shader.occlusion, "occlusion");

      j["usdPreviewSurface"] = usdPreview;
    }

    return j.dump(2); // Pretty print with 2-space indent

  } else if (format == SerializationFormat::XML) {

    if (!material.hasOpenPBR()) {
      return nonstd::make_unexpected("Material does not have OpenPBR shader data");
    }

    return serializeOpenPBRToXML(*material.openPBRShader);

  } else {
    return nonstd::make_unexpected("Unsupported serialization format");
  }
}

} // namespace tydra
} // namespace tinyusdz