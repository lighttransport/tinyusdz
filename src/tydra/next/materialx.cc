// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Tydra/Next - MaterialX Support Implementation

#include "materialx.hh"
#include "../../mtlx-dom.hh"
#include "../../next/schema/usd-shade.hh"
#include <cmath>
#include <algorithm>
#include <memory>

namespace tinyusdz {
namespace tydra {
namespace next {

MtlxConverter::MtlxConverter() = default;
MtlxConverter::~MtlxConverter() = default;

namespace {

// Helper to set ShaderParam from float values
void SetShaderParam(ShaderParam& param, float v) {
  param.texture_id = -1;
  param.value = {v, 0, 0, 1};
}

void SetShaderParam(ShaderParam& param, float r, float g, float b) {
  param.texture_id = -1;
  param.value = {r, g, b, 1};
}

void SetShaderParam(ShaderParam& param, float r, float g, float b, float a) {
  param.texture_id = -1;
  param.value = {r, g, b, a};
}

}  // namespace

bool MtlxConverter::ConvertToRenderMaterial(const std::string& mtlx_content,
                                             const std::string& material_name,
                                             RenderMaterial* out) {
  if (!out) {
    error_ = "Output material is null";
    return false;
  }

  // Parse MaterialX document
  mtlx::MtlxDocument doc;
  if (!doc.ParseFromXML(mtlx_content)) {
    error_ = "Failed to parse MaterialX: " + doc.GetError();
    return false;
  }

  // Find the target material
  mtlx::MtlxMaterialPtr mat;
  if (material_name.empty()) {
    // Use first material
    const auto& materials = doc.GetMaterials();
    if (materials.empty()) {
      error_ = "No materials found in MaterialX document";
      return false;
    }
    mat = materials[0];
  } else {
    mat = doc.FindMaterial(material_name);
    if (!mat) {
      error_ = "Material not found: " + material_name;
      return false;
    }
  }

  out->name = mat->GetName();

  // Find surface shader node
  std::string surface_shader = mat->GetSurfaceShader();
  if (surface_shader.empty()) {
    error_ = "Material has no surface shader";
    return false;
  }

  // Look up the shader node
  mtlx::MtlxNodePtr shader_node = doc.FindNode(surface_shader);
  if (!shader_node) {
    // Try finding in node graphs
    for (const auto& ng : doc.GetNodeGraphs()) {
      shader_node = ng->GetNode(surface_shader);
      if (shader_node) break;
    }
  }

  if (!shader_node) {
    error_ = "Surface shader node not found: " + surface_shader;
    return false;
  }

  // Get shader category
  std::string category = shader_node->GetCategory();

  // Convert based on shader category
  if (category == "standard_surface") {
    StandardSurfaceData ss_data;
    MtlxNodeInfo node_info;
    node_info.name = shader_node->GetName();
    node_info.category = category;

    // Parse inputs from shader node
    for (const auto& input : shader_node->GetInputs()) {
      std::string input_name = input->GetName();
      const auto& value = input->GetValue();

      if (value.type == mtlx::MtlxValue::TYPE_FLOAT) {
        node_info.input_floats[input_name] = value.float_val;
      } else if (value.type == mtlx::MtlxValue::TYPE_FLOAT_VECTOR) {
        node_info.input_vectors[input_name] = value.float_vec;
      } else if (value.type == mtlx::MtlxValue::TYPE_STRING) {
        node_info.input_strings[input_name] = value.string_val;
      }

      // Check for node connections
      if (!input->GetNodeName().empty()) {
        std::string conn = input->GetNodeName();
        if (!input->GetOutput().empty()) {
          conn += "." + input->GetOutput();
        }
        node_info.input_connections[input_name] = conn;
      }
    }

    if (!ParseStandardSurface(node_info, &ss_data)) {
      return false;
    }

    // Convert to preview surface
    MtlxPreviewSurfaceData ps_data;
    if (!ConvertStandardSurface(ss_data, &ps_data)) {
      return false;
    }

    // Collect texture data from connected nodes
    std::map<std::string, MtlxTextureData> textures;
    for (const auto& conn : node_info.input_connections) {
      // Find connected node
      size_t dot_pos = conn.second.find('.');
      std::string node_name = (dot_pos != std::string::npos) ?
                               conn.second.substr(0, dot_pos) : conn.second;

      mtlx::MtlxNodePtr tex_node = doc.FindNode(node_name);
      if (tex_node && tex_node->GetCategory() == "image") {
        MtlxTextureData tex_data;
        MtlxNodeInfo tex_info;
        tex_info.name = tex_node->GetName();
        tex_info.category = tex_node->GetCategory();

        for (const auto& input : tex_node->GetInputs()) {
          const auto& val = input->GetValue();
          if (val.type == mtlx::MtlxValue::TYPE_STRING) {
            tex_info.input_strings[input->GetName()] = val.string_val;
          }
        }

        if (ParseTextureNode(tex_info, &tex_data)) {
          textures[conn.first] = tex_data;
        }
      }
    }

    // Populate render material
    PopulateRenderMaterial(ps_data, textures, out);

  } else if (category == "UsdPreviewSurface") {
    // Already UsdPreviewSurface - direct conversion
    MtlxPreviewSurfaceData ps_data;

    for (const auto& input : shader_node->GetInputs()) {
      std::string input_name = input->GetName();
      const auto& value = input->GetValue();

      if (input_name == "diffuseColor" && value.type == mtlx::MtlxValue::TYPE_FLOAT_VECTOR) {
        if (value.float_vec.size() >= 3) {
          ps_data.diffuse_color[0] = value.float_vec[0];
          ps_data.diffuse_color[1] = value.float_vec[1];
          ps_data.diffuse_color[2] = value.float_vec[2];
        }
      } else if (input_name == "metallic" && value.type == mtlx::MtlxValue::TYPE_FLOAT) {
        ps_data.metallic = value.float_val;
      } else if (input_name == "roughness" && value.type == mtlx::MtlxValue::TYPE_FLOAT) {
        ps_data.roughness = value.float_val;
      } else if (input_name == "opacity" && value.type == mtlx::MtlxValue::TYPE_FLOAT) {
        ps_data.opacity = value.float_val;
      } else if (input_name == "ior" && value.type == mtlx::MtlxValue::TYPE_FLOAT) {
        ps_data.ior = value.float_val;
      }

      // Texture connections
      if (!input->GetNodeName().empty()) {
        if (input_name == "diffuseColor") {
          ps_data.diffuse_texture = input->GetNodeName();
        } else if (input_name == "normal") {
          ps_data.normal_texture = input->GetNodeName();
        } else if (input_name == "metallic") {
          ps_data.metallic_texture = input->GetNodeName();
        } else if (input_name == "roughness") {
          ps_data.roughness_texture = input->GetNodeName();
        }
      }
    }

    std::map<std::string, MtlxTextureData> textures;
    PopulateRenderMaterial(ps_data, textures, out);

  } else {
    warning_ = "Unknown shader category: " + category + ", using defaults";
    // Set basic defaults using PreviewSurface
    out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
    out->preview_surface = std::make_unique<PreviewSurfaceShader>();
    SetShaderParam(out->preview_surface->diffuse_color, 0.5f, 0.5f, 0.5f);
  }

  return true;
}

bool MtlxConverter::ConvertFileToRenderMaterial(const std::string& filename,
                                                 const std::string& material_name,
                                                 RenderMaterial* out) {
  mtlx::MtlxDocument doc;
  if (!doc.ParseFromFile(filename)) {
    error_ = "Failed to load MaterialX file: " + doc.GetError();
    return false;
  }

  // Re-serialize to string and use main convert function
  // This is inefficient but keeps the code simple
  // TODO: Add direct file-based conversion

  error_ = "ConvertFileToRenderMaterial not yet implemented - use ConvertToRenderMaterial with file content";
  (void)material_name;
  (void)out;
  return false;
}

bool MtlxConverter::ConvertUsdMtlxMaterial(const tinyusdz::next::Stage& stage,
                                            const tinyusdz::next::UsdPrim& material_prim,
                                            RenderMaterial* out) {
  if (!out) {
    error_ = "Output material is null";
    return false;
  }

  // Check if this is a material
  if (!tinyusdz::next::IsMaterial(material_prim)) {
    error_ = "Prim is not a Material";
    return false;
  }

  // Get material name
  out->name = material_prim.GetName();

  // Get surface shader path
  std::string surface_shader_path = tinyusdz::next::GetSurfaceShader(stage, material_prim);
  if (surface_shader_path.empty()) {
    error_ = "Material has no surface shader";
    return false;
  }

  // Get the shader prim
  tinyusdz::next::UsdPrim shader_prim = stage.GetPrimAtPath(surface_shader_path);
  if (!shader_prim.IsValid()) {
    error_ = "Surface shader not found: " + surface_shader_path;
    return false;
  }

  // Check shader ID for MaterialX
  std::string shader_id = tinyusdz::next::GetShaderId(shader_prim);

  // If it's a standard UsdPreviewSurface, use the existing schema
  if (shader_id == "UsdPreviewSurface") {
    tinyusdz::next::PreviewSurfaceData ps_data;
    if (!tinyusdz::next::GetPreviewSurfaceData(stage, shader_prim, &ps_data)) {
      error_ = "Failed to get PreviewSurface data";
      return false;
    }

    // Convert PreviewSurfaceData to RenderMaterial
    out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
    out->preview_surface = std::make_unique<PreviewSurfaceShader>();

    SetShaderParam(out->preview_surface->diffuse_color,
                   ps_data.diffuse_color[0], ps_data.diffuse_color[1], ps_data.diffuse_color[2]);
    SetShaderParam(out->preview_surface->metallic, ps_data.metallic);
    SetShaderParam(out->preview_surface->roughness, ps_data.roughness);
    SetShaderParam(out->preview_surface->clearcoat, ps_data.clearcoat);
    SetShaderParam(out->preview_surface->clearcoat_roughness, ps_data.clearcoat_roughness);
    SetShaderParam(out->preview_surface->opacity, ps_data.opacity);
    SetShaderParam(out->preview_surface->ior, ps_data.ior);

    return true;
  }

  // Check for MaterialX shader binding (implementationSource = "mtlx")
  std::string impl_source = tinyusdz::next::GetShaderImplementationSource(shader_prim);
  if (impl_source != "mtlx" && shader_id.find("ND_") != 0) {
    // Not MaterialX - try default conversion
    warning_ = "Shader is not MaterialX: " + shader_id;
    out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
    out->preview_surface = std::make_unique<PreviewSurfaceShader>();
    SetShaderParam(out->preview_surface->diffuse_color, 0.5f, 0.5f, 0.5f);
    return true;
  }

  // MaterialX shader - extract inputs and convert
  // TODO: Full MaterialX shader graph evaluation
  warning_ = "MaterialX shader graph evaluation not yet implemented";

  return true;
}

bool MtlxConverter::ParseMtlx(const std::string& mtlx_content,
                               std::vector<MtlxMaterialInfo>* materials) {
  if (!materials) {
    error_ = "Output materials is null";
    return false;
  }

  mtlx::MtlxDocument doc;
  if (!doc.ParseFromXML(mtlx_content)) {
    error_ = "Failed to parse MaterialX: " + doc.GetError();
    return false;
  }

  for (const auto& mat : doc.GetMaterials()) {
    MtlxMaterialInfo info;
    info.name = mat->GetName();
    info.surface_shader = mat->GetSurfaceShader();
    info.displacement_shader = mat->GetDisplacementShader();
    info.volume_shader = mat->GetVolumeShader();

    // Collect nodes
    for (const auto& node : doc.GetNodes()) {
      MtlxNodeInfo node_info;
      node_info.name = node->GetName();
      node_info.category = node->GetCategory();
      node_info.type = node->GetType();

      for (const auto& input : node->GetInputs()) {
        const auto& value = input->GetValue();
        if (value.type == mtlx::MtlxValue::TYPE_FLOAT) {
          node_info.input_floats[input->GetName()] = value.float_val;
        } else if (value.type == mtlx::MtlxValue::TYPE_FLOAT_VECTOR) {
          node_info.input_vectors[input->GetName()] = value.float_vec;
        } else if (value.type == mtlx::MtlxValue::TYPE_STRING) {
          node_info.input_strings[input->GetName()] = value.string_val;
        }

        if (!input->GetNodeName().empty()) {
          std::string conn = input->GetNodeName();
          if (!input->GetOutput().empty()) {
            conn += "." + input->GetOutput();
          }
          node_info.input_connections[input->GetName()] = conn;
        }
      }

      info.nodes.push_back(std::move(node_info));
    }

    materials->push_back(std::move(info));
  }

  return true;
}

bool MtlxConverter::ConvertStandardSurface(const StandardSurfaceData& ss,
                                            MtlxPreviewSurfaceData* out) {
  if (!out) return false;

  // Base color -> diffuse (weighted by base)
  out->diffuse_color[0] = ss.base * ss.base_color[0];
  out->diffuse_color[1] = ss.base * ss.base_color[1];
  out->diffuse_color[2] = ss.base * ss.base_color[2];

  // Metalness direct mapping
  out->metallic = ss.metalness;

  // Specular roughness
  out->roughness = ss.specular_roughness;

  // IOR
  out->ior = ss.specular_IOR;

  // Coat -> clearcoat
  out->clearcoat = ss.coat;
  out->clearcoat_roughness = ss.coat_roughness;

  // Opacity (use average of RGB opacity)
  out->opacity = (ss.opacity[0] + ss.opacity[1] + ss.opacity[2]) / 3.0f;

  // Emission
  if (ss.emission > 0.0f) {
    out->emissive_color[0] = ss.emission * ss.emission_color[0];
    out->emissive_color[1] = ss.emission * ss.emission_color[1];
    out->emissive_color[2] = ss.emission * ss.emission_color[2];
  }

  // Texture connections
  out->diffuse_texture = ss.base_color_texture;
  out->normal_texture = ss.normal_texture;
  out->metallic_texture = ss.metalness_texture;
  out->roughness_texture = ss.specular_roughness_texture;
  out->emissive_texture = ss.emission_texture;
  out->occlusion_texture = ss.opacity_texture;

  return true;
}

bool MtlxConverter::ParseStandardSurface(const MtlxNodeInfo& node,
                                          StandardSurfaceData* out) {
  if (!out) return false;

  // Parse float inputs
  auto get_float = [&node](const std::string& name, float default_val) -> float {
    auto it = node.input_floats.find(name);
    return (it != node.input_floats.end()) ? it->second : default_val;
  };

  auto get_color3 = [&node](const std::string& name, float* out3,
                            float r, float g, float b) {
    auto it = node.input_vectors.find(name);
    if (it != node.input_vectors.end() && it->second.size() >= 3) {
      out3[0] = it->second[0];
      out3[1] = it->second[1];
      out3[2] = it->second[2];
    } else {
      out3[0] = r;
      out3[1] = g;
      out3[2] = b;
    }
  };

  auto get_connection = [&node](const std::string& name) -> std::string {
    auto it = node.input_connections.find(name);
    return (it != node.input_connections.end()) ? it->second : "";
  };

  // Base
  out->base = get_float("base", 1.0f);
  get_color3("base_color", out->base_color, 0.8f, 0.8f, 0.8f);
  out->base_color_texture = get_connection("base_color");

  // Specular
  out->specular = get_float("specular", 1.0f);
  get_color3("specular_color", out->specular_color, 1.0f, 1.0f, 1.0f);
  out->specular_roughness = get_float("specular_roughness", 0.2f);
  out->specular_IOR = get_float("specular_IOR", 1.5f);
  out->specular_anisotropy = get_float("specular_anisotropy", 0.0f);
  out->specular_rotation = get_float("specular_rotation", 0.0f);
  out->specular_roughness_texture = get_connection("specular_roughness");

  // Metalness
  out->metalness = get_float("metalness", 0.0f);
  out->metalness_texture = get_connection("metalness");

  // Transmission
  out->transmission = get_float("transmission", 0.0f);
  get_color3("transmission_color", out->transmission_color, 1.0f, 1.0f, 1.0f);
  out->transmission_depth = get_float("transmission_depth", 0.0f);
  out->transmission_dispersion = get_float("transmission_dispersion", 0.0f);

  // Subsurface
  out->subsurface = get_float("subsurface", 0.0f);
  get_color3("subsurface_color", out->subsurface_color, 1.0f, 1.0f, 1.0f);
  out->subsurface_scale = get_float("subsurface_scale", 1.0f);

  // Sheen
  out->sheen = get_float("sheen", 0.0f);
  get_color3("sheen_color", out->sheen_color, 1.0f, 1.0f, 1.0f);
  out->sheen_roughness = get_float("sheen_roughness", 0.3f);

  // Coat
  out->coat = get_float("coat", 0.0f);
  get_color3("coat_color", out->coat_color, 1.0f, 1.0f, 1.0f);
  out->coat_roughness = get_float("coat_roughness", 0.1f);
  out->coat_IOR = get_float("coat_IOR", 1.5f);
  out->coat_normal_texture = get_connection("coat_normal");

  // Thin Film
  out->thin_film_thickness = get_float("thin_film_thickness", 0.0f);
  out->thin_film_IOR = get_float("thin_film_IOR", 1.5f);

  // Emission
  out->emission = get_float("emission", 0.0f);
  get_color3("emission_color", out->emission_color, 1.0f, 1.0f, 1.0f);
  out->emission_texture = get_connection("emission_color");

  // Geometry
  get_color3("opacity", out->opacity, 1.0f, 1.0f, 1.0f);
  out->opacity_texture = get_connection("opacity");
  out->normal_texture = get_connection("normal");
  out->tangent_texture = get_connection("tangent");

  return true;
}

bool MtlxConverter::ParseTextureNode(const MtlxNodeInfo& node,
                                      MtlxTextureData* out) {
  if (!out) return false;

  auto it = node.input_strings.find("file");
  if (it != node.input_strings.end()) {
    out->file = it->second;
  }

  it = node.input_strings.find("uaddressmode");
  if (it != node.input_strings.end()) {
    out->uaddressmode = it->second;
  }

  it = node.input_strings.find("vaddressmode");
  if (it != node.input_strings.end()) {
    out->vaddressmode = it->second;
  }

  it = node.input_strings.find("filtertype");
  if (it != node.input_strings.end()) {
    out->filtertype = it->second;
  }

  return true;
}

void MtlxConverter::PopulateRenderMaterial(
    const MtlxPreviewSurfaceData& data,
    const std::map<std::string, MtlxTextureData>& /* textures */,
    RenderMaterial* out) {

  out->shader_type = RenderMaterial::ShaderType::PreviewSurface;
  out->preview_surface = std::make_unique<PreviewSurfaceShader>();

  SetShaderParam(out->preview_surface->diffuse_color,
                 data.diffuse_color[0], data.diffuse_color[1], data.diffuse_color[2]);

  SetShaderParam(out->preview_surface->emissive_color,
                 data.emissive_color[0], data.emissive_color[1], data.emissive_color[2]);

  SetShaderParam(out->preview_surface->specular_color,
                 data.specular_color[0], data.specular_color[1], data.specular_color[2]);

  SetShaderParam(out->preview_surface->metallic, data.metallic);
  SetShaderParam(out->preview_surface->roughness, data.roughness);
  SetShaderParam(out->preview_surface->clearcoat, data.clearcoat);
  SetShaderParam(out->preview_surface->clearcoat_roughness, data.clearcoat_roughness);
  SetShaderParam(out->preview_surface->opacity, data.opacity);
  SetShaderParam(out->preview_surface->ior, data.ior);

  SetShaderParam(out->preview_surface->normal,
                 data.normal[0], data.normal[1], data.normal[2]);

  out->preview_surface->use_specular_workflow = data.use_specular_workflow;

  // Note: Texture indices would need to be set up by the caller
  // based on the texture data and asset loading
}

// ============================================================
// Utility functions
// ============================================================

bool HasMtlxBinding(const tinyusdz::next::UsdPrim& prim) {
  if (!prim.IsValid()) return false;

  // Check for mtlx file reference or MaterialX shader ID
  // This is a simplified check - full implementation would check
  // for info:implementationSource = "mtlx" or mtlx:/* properties

  return false;  // TODO: Implement
}

std::string GetMtlxFilePath(const tinyusdz::next::UsdPrim& /* material_prim */) {
  // TODO: Look for mtlx:file or asset path
  return "";
}

ColorSpace ParseMtlxColorSpace(const std::string& cs) {
  if (cs == "srgb_texture" || cs == "sRGB") {
    return ColorSpace::sRGB;
  } else if (cs == "lin_rec709" || cs == "linear") {
    return ColorSpace::Linear;
  } else if (cs == "raw" || cs == "none") {
    return ColorSpace::Raw;
  } else if (cs == "acescg" || cs == "ACEScg") {
    return ColorSpace::ACEScg;
  }
  return ColorSpace::sRGB;  // Default
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
