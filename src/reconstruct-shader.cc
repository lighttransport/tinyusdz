// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Shader and Material primitive reconstruction - Implementation

#include "reconstruct-shader.hh"
#include "reconstruct-common.hh"
#include "prim-reconstruct.hh"
#include "str-util.hh"
#include "common-macros.inc"
#include "usdShade.hh"

namespace tinyusdz {
namespace prim {

// Material binding properties helper function
bool ReconstructMaterialBindingProperties(
    std::set<std::string> &table,
    const PropertyMap &properties,
    MaterialBinding *materialBinding,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)warn;
  
  for (const auto &prop : properties) {
    // Material binding relationships
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kMaterialBinding, materialBinding->materialBinding)
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kMaterialBindingPreview, materialBinding->materialBindingPreview)
    // Note: materialBindingCollection is deprecated, use materialBindingCollectionMap instead
    
    // TODO: Add other material binding properties as needed
  }

  return true;
}

// Collection properties helper function  
bool ReconstructCollectionProperties(
    std::set<std::string> &table,
    const PropertyMap &properties,
    Collection *collection,
    std::string *collection_name,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)collection;
  (void)collection_name;
  (void)warn;
  (void)err;
  (void)options;
  (void)table;
  (void)properties;
  
  // TODO: Implement collection properties reconstruction
  // This is a complex function that would need to be extracted from the original
  return true;
}

// Generic shader reconstruction template
template <typename T>
bool ReconstructShader(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    T *out,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)properties;
  (void)references;
  (void)out;
  (void)warn;
  (void)err;
  (void)options;
  
  // TODO: Implement generic shader reconstruction
  return true;
}

// Shader template specializations
template <>
bool ReconstructShader<ShaderNode>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    ShaderNode *shader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)references;
  (void)options;
  
  std::set<std::string> table;

  for (const auto &prop : properties) {
    // Generic shader properties
    ADD_PROPERTY(table, prop, ShaderNode, shader->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<UsdPreviewSurface>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    UsdPreviewSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)references;
  (void)options;
  
  std::set<std::string> table;

  for (const auto &prop : properties) {
    // UsdPreviewSurface input properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:diffuseColor", UsdPreviewSurface, surface->diffuseColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emissiveColor", UsdPreviewSurface, surface->emissiveColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:useSpecularWorkflow", UsdPreviewSurface, surface->useSpecularWorkflow)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specularColor", UsdPreviewSurface, surface->specularColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:metallic", UsdPreviewSurface, surface->metallic)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:roughness", UsdPreviewSurface, surface->roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:clearcoat", UsdPreviewSurface, surface->clearcoat)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:clearcoatRoughness", UsdPreviewSurface, surface->clearcoatRoughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacity", UsdPreviewSurface, surface->opacity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacityThreshold", UsdPreviewSurface, surface->opacityThreshold)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:ior", UsdPreviewSurface, surface->ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:normal", UsdPreviewSurface, surface->normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:displacement", UsdPreviewSurface, surface->displacement)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:occlusion", UsdPreviewSurface, surface->occlusion)
    
    // Output properties
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:surface", UsdPreviewSurface, surface->outputsSurface)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:displacement", UsdPreviewSurface, surface->outputsDisplacement)

    ADD_PROPERTY(table, prop, UsdPreviewSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Shader>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    Shader *shader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)options;

  bool is_generic_shader{false};
  auto info_id_prop = properties.find("info:id");
  if (info_id_prop == properties.end()) {
    // Guess MaterialX shader. info:id will be resolved by importing referenced .mtlx.
    // Treat generic Shader at the moment.
    is_generic_shader = true;
  }

  std::string shader_type;
  if (!is_generic_shader) {
    if (info_id_prop->second.is_attribute()) {
      const Attribute &attr = info_id_prop->second.get_attribute();
      if ((attr.type_name() == value::kToken)) {
        if (auto pv = attr.get_value<value::token>()) {
          shader_type = pv.value().str();
        } else {
          PUSH_ERROR_AND_RETURN("Internal error. `info:id` has invalid type.");
        }
      } else {
        PUSH_ERROR_AND_RETURN("`info:id` attribute must be `token` type.");
      }

      // For some corrupted? USDZ file does not have `uniform` variability.
      if (attr.variability() != Variability::Uniform) {
        PUSH_WARN("`info:id` attribute must have `uniform` variability.");
      }
    } else {
      PUSH_ERROR_AND_RETURN("Invalid type or value for `info:id` property in `Shader`.");
    }

    DCOUT("info:id = " << shader_type);
  }

  if (shader_type.compare("UsdPreviewSurface") == 0) {
    UsdPreviewSurface surface;
    if (!ReconstructShader<UsdPreviewSurface>(spec, properties, references,
                                              &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct UsdPreviewSurface");
    }
    shader->info_id = "UsdPreviewSurface";
    shader->value = surface;
    DCOUT("info_id = " << shader->info_id);
  } else if (shader_type.compare("UsdUVTexture") == 0) {
    UsdUVTexture texture;
    if (!ReconstructShader<UsdUVTexture>(spec, properties, references,
                                         &texture, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct UsdUVTexture");
    }
    shader->info_id = "UsdUVTexture";
    shader->value = texture;
  } else if (shader_type.compare("UsdPrimvarReader_float") == 0) {
    UsdPrimvarReader_float preader;
    if (!ReconstructShader<UsdPrimvarReader_float>(spec, properties, references,
                                                   &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct UsdPrimvarReader_float");
    }
    shader->info_id = "UsdPrimvarReader_float";
    shader->value = preader;
  } else if (shader_type.compare("UsdPrimvarReader_float3") == 0) {
    UsdPrimvarReader_float3 preader;
    if (!ReconstructShader<UsdPrimvarReader_float3>(spec, properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct UsdPrimvarReader_float3");
    }
    shader->info_id = "UsdPrimvarReader_float3";
    shader->value = preader;
  } else if (shader_type.compare("UsdTransform2d") == 0) {
    UsdTransform2d xform;
    if (!ReconstructShader<UsdTransform2d>(spec, properties, references,
                                           &xform, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct UsdTransform2d");
    }
    shader->info_id = "UsdTransform2d";
    shader->value = xform;
  } else if (is_generic_shader) {
    ShaderNode node;
    if (!ReconstructShader<ShaderNode>(spec, properties, references,
                                       &node, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct generic ShaderNode");
    }
    shader->info_id = ""; // Generic shader
    shader->value = node;
  } else {
    PUSH_ERROR_AND_RETURN("Unsupported/Unimplemented shader type: " + shader_type);
  }

  return true;
}

template <>
bool ReconstructPrim<Material>(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    Material *material,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)references;
  (void)options;
  
  std::set<std::string> table;

  for (const auto &prop : properties) {
    // Material surface connections
    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:surface", Material, material->surface)
    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:displacement", Material, material->displacement)
    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:volume", Material, material->volume)
    
    // Generic properties
    ADD_PROPERTY(table, prop, Material, material->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

// Explicit template instantiations for common shader types
template bool ReconstructShader<UsdUVTexture>(const Specifier &, const PropertyMap &, const ReferenceList &, UsdUVTexture *, std::string *, std::string *, const PrimReconstructOptions &);
template bool ReconstructShader<UsdPrimvarReader_float>(const Specifier &, const PropertyMap &, const ReferenceList &, UsdPrimvarReader_float *, std::string *, std::string *, const PrimReconstructOptions &);
template bool ReconstructShader<UsdPrimvarReader_float3>(const Specifier &, const PropertyMap &, const ReferenceList &, UsdPrimvarReader_float3 *, std::string *, std::string *, const PrimReconstructOptions &);
template bool ReconstructShader<UsdTransform2d>(const Specifier &, const PropertyMap &, const ReferenceList &, UsdTransform2d *, std::string *, std::string *, const PrimReconstructOptions &);

} // namespace prim
} // namespace tinyusdz