// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Stage-converter: Shader/Material property extraction.
//

#include "sconv-detail.hh"
#include "usdShade.hh"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

namespace tinyusdz {
namespace experimental {

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

  // MaterialXConfigAPI properties
  if (material->materialXConfig) {
    const auto &cfg = *material->materialXConfig;
    if (cfg.mtlx_version.authored()) {
      crate::CrateValue cv;
      cv.Set(cfg.mtlx_version.get_value());
      fields.push_back({"config:mtlx:version", cv});
    }
    if (cfg.mtlx_namespace.authored()) {
      crate::CrateValue cv;
      cv.Set(cfg.mtlx_namespace.get_value());
      fields.push_back({"config:mtlx:namespace", cv});
    }
    if (cfg.mtlx_colorspace.authored()) {
      crate::CrateValue cv;
      cv.Set(cfg.mtlx_colorspace.get_value());
      fields.push_back({"config:mtlx:colorspace", cv});
    }
    if (cfg.mtlx_sourceUri.authored()) {
      crate::CrateValue cv;
      cv.Set(cfg.mtlx_sourceUri.get_value());
      fields.push_back({"config:mtlx:sourceUri", cv});
    }
  }

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

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
