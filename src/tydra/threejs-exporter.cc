// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.

#include "threejs-exporter.hh"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace tinyusdz {
namespace tydra {

// Parameter mapping tables
const std::map<std::string, std::string> MaterialParameterMapping::openpbr_to_physical = {
  {"base_color", "color"},
  {"base_metalness", "metalness"},
  {"base_roughness", "roughness"},
  {"emission_color", "emissive"},
  {"emission_luminance", "emissiveIntensity"},
  {"opacity", "opacity"},
  {"coat_weight", "clearcoat"},
  {"coat_roughness", "clearcoatRoughness"},
  {"sheen_weight", "sheen"},
  {"sheen_color", "sheenColor"},
  {"sheen_roughness", "sheenRoughness"},
  {"specular_ior", "ior"},
  {"transmission_weight", "transmission"},
  {"base_weight", "opacity"}  // base_weight affects overall opacity
};

const std::map<std::string, std::string> MaterialParameterMapping::openpbr_to_nodes = {
  {"base_color", "base_color"},
  {"base_metalness", "metallic"},
  {"base_roughness", "roughness"},
  {"specular_weight", "specular"},
  {"specular_color", "specular_color"},
  {"specular_roughness", "specular_roughness"},
  {"specular_ior", "ior"},
  {"coat_weight", "coat"},
  {"coat_color", "coat_color"},
  {"coat_roughness", "coat_roughness"},
  {"emission_luminance", "emission"},
  {"emission_color", "emission_color"},
  {"normal", "normalMap"},
  {"tangent", "tangentMap"}
};

const std::map<std::string, std::string> MaterialParameterMapping::preview_to_physical = {
  {"diffuseColor", "color"},
  {"metallic", "metalness"},
  {"roughness", "roughness"},
  {"emissiveColor", "emissive"},
  {"opacity", "opacity"},
  {"clearcoat", "clearcoat"},
  {"clearcoatRoughness", "clearcoatRoughness"},
  {"ior", "ior"},
  {"specularColor", "specular"}
};

const std::map<std::string, std::string> MaterialParameterMapping::colorspace_map = {
  {"sRGB", "srgb"},
  {"lin_rec709", "linear-rec709"},
  {"lin_sRGB", "linear-srgb"},
  {"ACEScg", "acescg"},
  {"raw", "raw"}
};

// Helper function to convert vec3 to JSON array
static json vec3ToJson(const vec3& v) {
  return json::array({v[0], v[1], v[2]});
}

// Helper function to convert vec2 to JSON array
static json vec2ToJson(const vec2& v) {
  return json::array({v[0], v[1]});
}

// ThreeJSMaterialExporter implementation

bool ThreeJSMaterialExporter::ExportScene(const RenderScene& scene,
                                         const ExportOptions& options,
                                         json& output) {
  output = json::object();
  output["metadata"] = {
    {"version", "1.0"},
    {"generator", "TinyUSDZ/Tydra ThreeJS Exporter"},
    {"type", "Scene"}
  };

  // Export materials
  json materials = json::array();
  for (const auto& mat : scene.materials) {
    json mat_json;
    if (ExportMaterial(mat, options, mat_json)) {
      materials.push_back(mat_json);
    }
  }
  output["materials"] = materials;

  // Export textures
  json textures = json::array();
  for (const auto& tex : scene.textures) {
    json tex_json = ConvertTextureReference(tex.handle, options);
    tex_json["name"] = tex.prim_name;
    textures.push_back(tex_json);
  }
  output["textures"] = textures;

  // Export meshes (geometry)
  json geometries = json::array();
  for (const auto& mesh : scene.meshes) {
    json geom = {
      {"name", mesh.prim_name},
      {"type", "BufferGeometry"},
      {"uuid", mesh.handle}
    };

    // Add vertex data
    json attributes = json::object();

    // Positions
    if (!mesh.points.empty()) {
      json positions = json::array();
      for (const auto& p : mesh.points) {
        positions.push_back(p[0]);
        positions.push_back(p[1]);
        positions.push_back(p[2]);
      }
      attributes["position"] = {
        {"array", positions},
        {"itemSize", 3},
        {"type", "Float32Array"}
      };
    }

    // Normals
    if (!mesh.normals.data.empty()) {
      json normals = json::array();
      // Convert normals based on variability
      if (mesh.normals.is_vertex()) {
        for (size_t i = 0; i < mesh.normals.vertex_count(); ++i) {
          auto n = mesh.normals.get_data<vec3>()[i];
          normals.push_back(n[0]);
          normals.push_back(n[1]);
          normals.push_back(n[2]);
        }
      }
      attributes["normal"] = {
        {"array", normals},
        {"itemSize", 3},
        {"type", "Float32Array"}
      };
    }

    // UVs
    if (!mesh.texcoords.empty() && !mesh.texcoords[0].data.empty()) {
      json uvs = json::array();
      const auto& texcoord = mesh.texcoords[0];
      if (texcoord.is_vertex()) {
        for (size_t i = 0; i < texcoord.vertex_count(); ++i) {
          auto uv = texcoord.get_data<vec2>()[i];
          uvs.push_back(uv[0]);
          uvs.push_back(uv[1]);
        }
      }
      attributes["uv"] = {
        {"array", uvs},
        {"itemSize", 2},
        {"type", "Float32Array"}
      };
    }

    geom["attributes"] = attributes;

    // Add indices if available
    auto indices = mesh.faceVertexIndices();
    if (!indices.empty()) {
      json index_array = json::array();
      for (auto idx : indices) {
        index_array.push_back(idx);
      }
      geom["index"] = {
        {"array", index_array},
        {"type", indices.size() > 65535 ? "Uint32Array" : "Uint16Array"}
      };
    }

    geometries.push_back(geom);
  }
  output["geometries"] = geometries;

  return true;
}

bool ThreeJSMaterialExporter::ExportMaterial(const RenderMaterial& material,
                                            const ExportOptions& options,
                                            json& output) {
  output = json::object();

  // Basic material info
  output["name"] = material.name;
  output["uuid"] = std::to_string(material.handle);

  // Determine which material representation to use
  bool has_openpbr = material.hasOpenPBR();
  bool has_preview = material.hasUsdPreviewSurface();

  if (has_openpbr && options.use_webgpu) {
    // Export as WebGPU node material
    output["type"] = "NodeMaterial";
    output["nodes"] = ConvertOpenPBRToNodeMaterial(material.openPBRShader.value());
  } else if (has_openpbr && options.generate_fallback) {
    // Export as WebGL MeshPhysicalMaterial
    output["type"] = "MeshPhysicalMaterial";
    json params = ConvertOpenPBRToPhysicalMaterial(material.openPBRShader.value());
    for (auto& [key, value] : params.items()) {
      output[key] = value;
    }
  } else if (has_preview) {
    // Export UsdPreviewSurface as standard material
    if (options.use_webgpu) {
      output["type"] = "NodeMaterial";
      output["nodes"] = ConvertPreviewSurfaceToNodeMaterial(material.surfaceShader.value());
    } else {
      output["type"] = "MeshStandardMaterial";
      json params = ConvertPreviewSurfaceToStandardMaterial(material.surfaceShader.value());
      for (auto& [key, value] : params.items()) {
        output[key] = value;
      }
    }
  }

  // Add metadata about dual materials if both exist
  if (material.hasBothMaterials()) {
    output["metadata"] = {
      {"hasOpenPBR", true},
      {"hasUsdPreviewSurface", true},
      {"preferredShader", has_openpbr ? "OpenPBR" : "UsdPreviewSurface"}
    };
  }

  return true;
}

json ThreeJSMaterialExporter::ConvertOpenPBRToNodeMaterial(const OpenPBRSurfaceShader& shader) {
  json nodes = json::object();

  // Create OpenPBR surface node
  json surface_node = {
    {"type", "OpenPBRSurfaceNode"},
    {"uuid", "surface_" + std::to_string(shader.handle)},
    {"inputs", json::object()}
  };

  // Map all OpenPBR parameters
  auto add_param = [&](const std::string& name, const auto& param) {
    if (param.is_texture()) {
      surface_node["inputs"][name] = {
        {"type", "texture"},
        {"textureId", param.texture_id}
      };
    } else {
      surface_node["inputs"][name] = param.value;
    }
  };

  // Base layer
  add_param("base_weight", shader.base_weight);
  surface_node["inputs"]["base_color"] = vec3ToJson(shader.base_color.value);
  add_param("base_roughness", shader.base_roughness);
  add_param("base_metalness", shader.base_metalness);

  // Specular layer
  add_param("specular_weight", shader.specular_weight);
  surface_node["inputs"]["specular_color"] = vec3ToJson(shader.specular_color.value);
  add_param("specular_roughness", shader.specular_roughness);
  add_param("specular_ior", shader.specular_ior);
  add_param("specular_ior_level", shader.specular_ior_level);
  add_param("specular_anisotropy", shader.specular_anisotropy);
  add_param("specular_rotation", shader.specular_rotation);

  // Transmission
  add_param("transmission_weight", shader.transmission_weight);
  surface_node["inputs"]["transmission_color"] = vec3ToJson(shader.transmission_color.value);
  add_param("transmission_depth", shader.transmission_depth);
  surface_node["inputs"]["transmission_scatter"] = vec3ToJson(shader.transmission_scatter.value);
  add_param("transmission_scatter_anisotropy", shader.transmission_scatter_anisotropy);
  add_param("transmission_dispersion", shader.transmission_dispersion);

  // Subsurface
  add_param("subsurface_weight", shader.subsurface_weight);
  surface_node["inputs"]["subsurface_color"] = vec3ToJson(shader.subsurface_color.value);
  surface_node["inputs"]["subsurface_radius"] = vec3ToJson(shader.subsurface_radius.value);
  add_param("subsurface_scale", shader.subsurface_scale);
  add_param("subsurface_anisotropy", shader.subsurface_anisotropy);

  // Sheen
  add_param("sheen_weight", shader.sheen_weight);
  surface_node["inputs"]["sheen_color"] = vec3ToJson(shader.sheen_color.value);
  add_param("sheen_roughness", shader.sheen_roughness);

  // Coat
  add_param("coat_weight", shader.coat_weight);
  surface_node["inputs"]["coat_color"] = vec3ToJson(shader.coat_color.value);
  add_param("coat_roughness", shader.coat_roughness);
  add_param("coat_anisotropy", shader.coat_anisotropy);
  add_param("coat_rotation", shader.coat_rotation);
  add_param("coat_ior", shader.coat_ior);
  surface_node["inputs"]["coat_affect_color"] = vec3ToJson(shader.coat_affect_color.value);
  add_param("coat_affect_roughness", shader.coat_affect_roughness);

  // Emission
  add_param("emission_luminance", shader.emission_luminance);
  surface_node["inputs"]["emission_color"] = vec3ToJson(shader.emission_color.value);

  // Geometry
  add_param("opacity", shader.opacity);
  surface_node["inputs"]["normal"] = vec3ToJson(shader.normal.value);
  surface_node["inputs"]["tangent"] = vec3ToJson(shader.tangent.value);

  nodes["surface"] = surface_node;

  // Create output node
  nodes["output"] = {
    {"type", "OutputNode"},
    {"uuid", "output_" + std::to_string(shader.handle)},
    {"inputs", {
      {"surface", {
        {"connection", "surface"},
        {"output", "surface"}
      }}
    }}
  };

  return nodes;
}

json ThreeJSMaterialExporter::ConvertOpenPBRToPhysicalMaterial(const OpenPBRSurfaceShader& shader) {
  json params = json::object();

  // Map OpenPBR to MeshPhysicalMaterial parameters
  params["color"] = vec3ToJson(shader.base_color.value);
  params["metalness"] = shader.base_metalness.value;
  params["roughness"] = shader.base_roughness.value;

  // Emission
  if (shader.emission_luminance.value > 0.0f) {
    params["emissive"] = vec3ToJson(shader.emission_color.value);
    params["emissiveIntensity"] = shader.emission_luminance.value;
  }

  // Opacity
  params["opacity"] = shader.opacity.value;
  if (shader.opacity.value < 1.0f) {
    params["transparent"] = true;
  }

  // Clearcoat (coat layer)
  if (shader.coat_weight.value > 0.0f) {
    params["clearcoat"] = shader.coat_weight.value;
    params["clearcoatRoughness"] = shader.coat_roughness.value;
  }

  // Sheen
  if (shader.sheen_weight.value > 0.0f) {
    params["sheen"] = shader.sheen_weight.value;
    params["sheenColor"] = vec3ToJson(shader.sheen_color.value);
    params["sheenRoughness"] = shader.sheen_roughness.value;
  }

  // IOR
  params["ior"] = shader.specular_ior.value;

  // Transmission
  if (shader.transmission_weight.value > 0.0f) {
    params["transmission"] = shader.transmission_weight.value;
    params["transmissionColor"] = vec3ToJson(shader.transmission_color.value);

    // Three.js doesn't have direct equivalents for all transmission parameters
    // Store them in userData for custom shaders
    params["userData"] = {
      {"transmission_depth", shader.transmission_depth.value},
      {"transmission_scatter", vec3ToJson(shader.transmission_scatter.value)},
      {"transmission_scatter_anisotropy", shader.transmission_scatter_anisotropy.value},
      {"transmission_dispersion", shader.transmission_dispersion.value}
    };
  }

  // Subsurface (store in userData as Three.js doesn't have direct support)
  if (shader.subsurface_weight.value > 0.0f) {
    if (!params.contains("userData")) {
      params["userData"] = json::object();
    }
    params["userData"]["subsurface"] = {
      {"weight", shader.subsurface_weight.value},
      {"color", vec3ToJson(shader.subsurface_color.value)},
      {"radius", vec3ToJson(shader.subsurface_radius.value)},
      {"scale", shader.subsurface_scale.value},
      {"anisotropy", shader.subsurface_anisotropy.value}
    };
  }

  // Specular (additional parameters in userData)
  if (!params.contains("userData")) {
    params["userData"] = json::object();
  }
  params["userData"]["specular"] = {
    {"weight", shader.specular_weight.value},
    {"color", vec3ToJson(shader.specular_color.value)},
    {"roughness", shader.specular_roughness.value},
    {"anisotropy", shader.specular_anisotropy.value},
    {"rotation", shader.specular_rotation.value}
  };

  // Handle texture references
  auto handle_texture = [&](const std::string& param_name, const auto& shader_param, const std::string& three_name) {
    if (shader_param.is_texture()) {
      params[three_name + "Map"] = {
        {"type", "Texture"},
        {"uuid", std::to_string(shader_param.texture_id)}
      };
    }
  };

  handle_texture("base_color", shader.base_color, "map");
  handle_texture("base_metalness", shader.base_metalness, "metalnessMap");
  handle_texture("base_roughness", shader.base_roughness, "roughnessMap");
  handle_texture("normal", shader.normal, "normalMap");
  handle_texture("emission_color", shader.emission_color, "emissiveMap");
  handle_texture("opacity", shader.opacity, "alphaMap");

  return params;
}

json ThreeJSMaterialExporter::ConvertPreviewSurfaceToStandardMaterial(const PreviewSurfaceShader& shader) {
  json params = json::object();

  // Basic parameters
  params["color"] = vec3ToJson(shader.diffuseColor.value);
  params["emissive"] = vec3ToJson(shader.emissiveColor.value);
  params["opacity"] = shader.opacity.value;

  if (shader.opacity.value < 1.0f || shader.opacityThreshold.value > 0.0f) {
    params["transparent"] = true;
    if (shader.opacityThreshold.value > 0.0f) {
      params["alphaTest"] = shader.opacityThreshold.value;
    }
  }

  // Workflow detection
  if (shader.useSpecularWorkflow.value) {
    // Specular workflow - use MeshPhongMaterial properties
    params["specular"] = vec3ToJson(shader.specularColor.value);
  } else {
    // Metalness workflow - standard PBR
    params["metalness"] = shader.metallic.value;
  }

  params["roughness"] = shader.roughness.value;

  // Clearcoat
  if (shader.clearcoat.value > 0.0f) {
    params["clearcoat"] = shader.clearcoat.value;
    params["clearcoatRoughness"] = shader.clearcoatRoughness.value;
  }

  // IOR
  params["ior"] = shader.ior.value;

  // Handle texture connections
  auto handle_texture = [&](const std::string& param_name, const auto& shader_param, const std::string& three_name) {
    if (shader_param.is_texture()) {
      params[three_name] = {
        {"type", "Texture"},
        {"uuid", std::to_string(shader_param.texture_id)}
      };
    }
  };

  handle_texture("diffuseColor", shader.diffuseColor, "map");
  handle_texture("metallic", shader.metallic, "metalnessMap");
  handle_texture("roughness", shader.roughness, "roughnessMap");
  handle_texture("normal", shader.normal, "normalMap");
  handle_texture("emissiveColor", shader.emissiveColor, "emissiveMap");
  handle_texture("opacity", shader.opacity, "alphaMap");
  handle_texture("occlusion", shader.occlusion, "aoMap");

  // Displacement (store in userData as Three.js handles it differently)
  if (shader.displacement.value != 0.0f || shader.displacement.is_texture()) {
    params["userData"] = {
      {"displacement", shader.displacement.value},
      {"displacementMap", shader.displacement.is_texture() ?
        std::to_string(shader.displacement.texture_id) : json()}
    };
  }

  return params;
}

json ThreeJSMaterialExporter::ConvertPreviewSurfaceToNodeMaterial(const PreviewSurfaceShader& shader) {
  json nodes = json::object();

  // Create UsdPreviewSurface node
  json surface_node = {
    {"type", "UsdPreviewSurfaceNode"},
    {"uuid", "surface_" + std::to_string(shader.handle)},
    {"inputs", json::object()}
  };

  // Map parameters
  surface_node["inputs"]["diffuseColor"] = vec3ToJson(shader.diffuseColor.value);
  surface_node["inputs"]["emissiveColor"] = vec3ToJson(shader.emissiveColor.value);
  surface_node["inputs"]["useSpecularWorkflow"] = shader.useSpecularWorkflow.value;
  surface_node["inputs"]["specularColor"] = vec3ToJson(shader.specularColor.value);
  surface_node["inputs"]["metallic"] = shader.metallic.value;
  surface_node["inputs"]["clearcoat"] = shader.clearcoat.value;
  surface_node["inputs"]["clearcoatRoughness"] = shader.clearcoatRoughness.value;
  surface_node["inputs"]["roughness"] = shader.roughness.value;
  surface_node["inputs"]["opacity"] = shader.opacity.value;
  surface_node["inputs"]["opacityThreshold"] = shader.opacityThreshold.value;
  surface_node["inputs"]["ior"] = shader.ior.value;
  surface_node["inputs"]["normal"] = vec3ToJson(shader.normal.value);
  surface_node["inputs"]["displacement"] = shader.displacement.value;
  surface_node["inputs"]["occlusion"] = shader.occlusion.value;

  nodes["surface"] = surface_node;

  // Create output node
  nodes["output"] = {
    {"type", "OutputNode"},
    {"uuid", "output_" + std::to_string(shader.handle)},
    {"inputs", {
      {"surface", {
        {"connection", "surface"},
        {"output", "surface"}
      }}
    }}
  };

  return nodes;
}

bool ThreeJSMaterialExporter::ExportMaterialX(const RenderMaterial& material,
                                              std::string& mtlx_output) {
  std::stringstream ss;

  ss << "<?xml version=\"1.0\"?>\n";
  ss << "<materialx version=\"1.38\">\n";

  if (material.hasOpenPBR()) {
    const auto& shader = material.openPBRShader.value();

    ss << "  <OpenPBRSurface name=\"" << material.name << "_surface\" type=\"surfaceshader\">\n";

    // Export all parameters
    auto export_param = [&ss](const std::string& name, const auto& param, const std::string& type) {
      if (!param.is_texture()) {
        ss << "    <input name=\"" << name << "\" type=\"" << type << "\" ";
        // Value writing depends on type
        ss << "/>\n";
      }
    };

    // Base layer
    export_param("base_weight", shader.base_weight, "float");
    ss << "    <input name=\"base_color\" type=\"color3\" value=\""
       << shader.base_color.value[0] << ", "
       << shader.base_color.value[1] << ", "
       << shader.base_color.value[2] << "\"/>\n";
    export_param("base_roughness", shader.base_roughness, "float");
    export_param("base_metalness", shader.base_metalness, "float");

    // ... (export other parameters similarly)

    ss << "  </OpenPBRSurface>\n";

    // Create surface material
    ss << "  <surfacematerial name=\"" << material.name << "\" type=\"material\">\n";
    ss << "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\""
       << material.name << "_surface\"/>\n";
    ss << "  </surfacematerial>\n";
  }

  ss << "</materialx>\n";

  mtlx_output = ss.str();
  return true;
}

json ThreeJSMaterialExporter::ConvertTextureReference(uint64_t texture_id,
                                                      const ExportOptions& options) {
  // Check cache
  if (_texture_cache.find(texture_id) != _texture_cache.end()) {
    return _texture_cache[texture_id];
  }

  json tex_json = {
    {"uuid", std::to_string(texture_id)},
    {"type", "Texture"}
  };

  if (!options.texture_path.empty()) {
    tex_json["url"] = options.texture_path + "/texture_" + std::to_string(texture_id) + ".png";
  }

  // Default wrapping and filtering
  tex_json["wrapS"] = "RepeatWrapping";
  tex_json["wrapT"] = "RepeatWrapping";
  tex_json["magFilter"] = "LinearFilter";
  tex_json["minFilter"] = "LinearMipmapLinearFilter";

  _texture_cache[texture_id] = tex_json;
  return tex_json;
}

// ThreeJSSceneExporter implementation

bool ThreeJSSceneExporter::ExportScene(const RenderScene& scene,
                                       const SceneExportOptions& options,
                                       json& output) {
  output = json::object();

  // Export materials using material exporter
  json materials;
  _material_exporter.ExportScene(scene, options.material_options, materials);
  output["materials"] = materials["materials"];
  output["textures"] = materials["textures"];
  output["geometries"] = materials["geometries"];

  // Build node hierarchy
  json object3d = json::object();
  object3d["type"] = "Scene";
  object3d["children"] = json::array();

  // Convert nodes
  for (const auto& node : scene.nodes) {
    if (node.parent_id == -1) { // Root nodes
      object3d["children"].push_back(ConvertNode(node, scene));
    }
  }

  // Add cameras if requested
  if (options.export_cameras) {
    for (const auto& camera : scene.cameras) {
      object3d["children"].push_back(ConvertCamera(camera));
    }
  }

  // Add lights if requested
  if (options.export_lights) {
    for (const auto& light : scene.lights) {
      object3d["children"].push_back(ConvertLight(light));
    }
  }

  output["object"] = object3d;

  // Add animations if requested
  if (options.export_animations && !scene.animations.empty()) {
    json animations = json::array();
    for (const auto& anim : scene.animations) {
      animations.push_back(ConvertAnimation(anim));
    }
    output["animations"] = animations;
  }

  // Add metadata
  output["metadata"] = {
    {"version", 2},
    {"type", "Object"},
    {"generator", "TinyUSDZ ThreeJS Exporter"},
    {"sourceFile", scene.usd_filename}
  };

  return true;
}

json ThreeJSSceneExporter::ConvertNode(const Node& node, const RenderScene& scene) {
  json obj = json::object();

  obj["name"] = node.prim_name;
  obj["uuid"] = std::to_string(node.handle);

  // Determine node type
  switch (node.nodeType) {
    case NodeType::Mesh:
      obj["type"] = "Mesh";
      // Find associated mesh data
      for (const auto& mesh : scene.meshes) {
        if (mesh.prim_name == node.prim_name) {
          obj["geometry"] = std::to_string(mesh.handle);
          if (mesh.material_id != -1 && mesh.material_id < scene.materials.size()) {
            obj["material"] = std::to_string(scene.materials[mesh.material_id].handle);
          }
          break;
        }
      }
      break;
    case NodeType::Xform:
      obj["type"] = "Group";
      break;
    case NodeType::Camera:
      obj["type"] = "Camera";
      break;
    default:
      obj["type"] = "Object3D";
  }

  // Transform matrix
  if (!node.is_identity_matrix()) {
    json matrix = json::array();
    for (int i = 0; i < 16; ++i) {
      matrix.push_back(node.local_matrix.m[i]);
    }
    obj["matrix"] = matrix;
  }

  // Add children
  json children = json::array();
  for (int child_id : node.children) {
    if (child_id >= 0 && child_id < scene.nodes.size()) {
      children.push_back(ConvertNode(scene.nodes[child_id], scene));
    }
  }
  if (!children.empty()) {
    obj["children"] = children;
  }

  return obj;
}

json ThreeJSSceneExporter::ConvertCamera(const RenderCamera& camera) {
  json cam = {
    {"type", "PerspectiveCamera"},
    {"name", camera.name},
    {"fov", camera.yfov()},
    {"aspect", 1.0 / camera.verticalAspectRatio},
    {"near", camera.znear},
    {"far", camera.zfar}
  };
  return cam;
}

json ThreeJSSceneExporter::ConvertLight(const RenderLight& light) {
  // This would need to be implemented based on RenderLight structure
  json light_json = {
    {"type", "Light"},
    {"name", light.name}
  };
  return light_json;
}

json ThreeJSSceneExporter::ConvertAnimation(const Animation& anim) {
  json anim_json = {
    {"name", anim.prim_name},
    {"duration", 0.0}, // Calculate from samples
    {"tracks", json::array()}
  };

  // Convert animation channels to Three.js tracks
  for (const auto& [target, channel] : anim.channels_map) {
    json track = {
      {"name", target + ".position"}, // or .rotation, .scale
      {"type", "vector3"},
      {"times", json::array()},
      {"values", json::array()}
    };

    // Add keyframes
    if (channel.type == AnimationChannel::ChannelType::Translation) {
      for (const auto& sample : channel.translations.samples) {
        track["times"].push_back(sample.t);
        auto& val = std::get<value::float3>(sample.value);
        track["values"].push_back(val[0]);
        track["values"].push_back(val[1]);
        track["values"].push_back(val[2]);
      }
    }

    anim_json["tracks"].push_back(track);
  }

  return anim_json;
}

bool ThreeJSSceneExporter::ExportGLTF(const RenderScene& scene,
                                      const SceneExportOptions& options,
                                      json& gltf_output) {
  // TODO: Implement full glTF 2.0 export with MaterialX extensions
  _err = "glTF export not yet implemented";
  return false;
}

} // namespace tydra
} // namespace tinyusdz