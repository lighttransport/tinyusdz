// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// MaterialX NodeGraph to JSON Converter Implementation
//

#include "materialx-to-json.hh"

#include <sstream>
#include <cstdio>

#include "mtlx-dom.hh"
#include "prim-types.hh"
#include "stage.hh"
#include "usdShade.hh"  // For NodeGraph, Shader, ShaderNode
#include "usdMtlx.hh"  // For MtlxOpenPBRSurface
#include "value-pprint.hh"
#include "color-space.hh"
#include "render-data.hh"  // For SpectralData, SpectralIOR, SpectralEmission

namespace tinyusdz {
namespace tydra {

std::string EscapeJsonString(const std::string &input) {
  std::string output;
  output.reserve(input.size() * 2); // Reserve space for worst case

  for (char c : input) {
    switch (c) {
      case '\"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (c < 0x20) {
          // Control characters - use \uXXXX notation
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          output += buf;
        } else {
          output += c;
        }
        break;
    }
  }
  return output;
}

std::string FloatVectorToJsonArray(const std::vector<float> &vec) {
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < vec.size(); i++) {
    if (i > 0) ss << ", ";
    ss << vec[i];
  }
  ss << "]";
  return ss.str();
}

std::string IntVectorToJsonArray(const std::vector<int> &vec) {
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < vec.size(); i++) {
    if (i > 0) ss << ", ";
    ss << vec[i];
  }
  ss << "]";
  return ss.str();
}

std::string StringVectorToJsonArray(const std::vector<std::string> &vec) {
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < vec.size(); i++) {
    if (i > 0) ss << ", ";
    ss << "\"" << EscapeJsonString(vec[i]) << "\"";
  }
  ss << "]";
  return ss.str();
}

// Helper: Convert MaterialX value to JSON value string
static std::string MtlxValueToJsonValue(const mtlx::MtlxValue &value) {
  switch (value.type) {
    case mtlx::MtlxValue::TYPE_NONE:
      return "null";

    case mtlx::MtlxValue::TYPE_FLOAT:
      return std::to_string(value.float_val);

    case mtlx::MtlxValue::TYPE_INT:
      return std::to_string(value.int_val);

    case mtlx::MtlxValue::TYPE_BOOL:
      return value.bool_val ? "true" : "false";

    case mtlx::MtlxValue::TYPE_STRING:
      return "\"" + EscapeJsonString(value.string_val) + "\"";

    case mtlx::MtlxValue::TYPE_FLOAT_VECTOR:
      return FloatVectorToJsonArray(value.float_vec);

    case mtlx::MtlxValue::TYPE_INT_VECTOR:
      return IntVectorToJsonArray(value.int_vec);

    case mtlx::MtlxValue::TYPE_STRING_VECTOR:
      return StringVectorToJsonArray(value.string_vec);
  }

  // Unreachable, but needed for some compilers
  return "null";
}

// Convert MaterialX DOM NodeGraph to JSON
bool ConvertMtlxNodeGraphToJson(
    const mtlx::MtlxNodeGraph &nodegraph,
    std::string *json_str,
    std::string *err) {

  if (!json_str) {
    if (err) *err = "json_str is nullptr";
    return false;
  }

  std::stringstream ss;
  ss << "{\n";
  ss << "  \"version\": \"1.39\",\n"; // MaterialX version (Blender 4.5+ compatible)
  ss << "  \"nodegraph\": {\n";
  ss << "    \"name\": \"" << EscapeJsonString(nodegraph.GetName()) << "\",\n";

  // Serialize nodes
  ss << "    \"nodes\": [\n";
  const auto &nodes = nodegraph.GetNodes();
  for (size_t i = 0; i < nodes.size(); i++) {
    const auto &node = nodes[i];
    if (i > 0) ss << ",\n";

    ss << "      {\n";
    ss << "        \"name\": \"" << EscapeJsonString(node->GetName()) << "\",\n";
    ss << "        \"category\": \"" << EscapeJsonString(node->GetCategory()) << "\",\n";
    ss << "        \"type\": \"" << EscapeJsonString(node->GetType()) << "\",\n";

    // Serialize inputs
    ss << "        \"inputs\": [\n";
    const auto &inputs = node->GetInputs();
    for (size_t j = 0; j < inputs.size(); j++) {
      const auto &input = inputs[j];
      if (j > 0) ss << ",\n";

      ss << "          {\n";
      ss << "            \"name\": \"" << EscapeJsonString(input->GetName()) << "\",\n";
      ss << "            \"type\": \"" << EscapeJsonString(input->GetType()) << "\"";

      // Check for connection
      if (!input->GetNodeName().empty()) {
        ss << ",\n";
        ss << "            \"nodename\": \"" << EscapeJsonString(input->GetNodeName()) << "\",\n";
        ss << "            \"output\": \"" << EscapeJsonString(input->GetOutput()) << "\"";
      } else if (input->GetValue().type != mtlx::MtlxValue::TYPE_NONE) {
        // Direct value
        ss << ",\n";
        ss << "            \"value\": " << MtlxValueToJsonValue(input->GetValue());
      }

      // Add colorspace if present and not default (lin_rec709_scene)
      std::string colorspace = input->GetColorSpace();
      if (!colorspace.empty() && colorspace != colorspace::kLinRec709Scene) {
        ss << ",\n";
        ss << "            \"colorspace\": \"" << EscapeJsonString(colorspace) << "\"";
      }

      ss << "\n          }";
    }
    ss << "\n        ]";

    ss << "\n      }";
  }
  ss << "\n    ],\n";

  // Serialize nodegraph outputs
  ss << "    \"outputs\": [\n";
  const auto &outputs = nodegraph.GetOutputs();
  for (size_t i = 0; i < outputs.size(); i++) {
    const auto &output = outputs[i];
    if (i > 0) ss << ",\n";

    ss << "      {\n";
    ss << "        \"name\": \"" << EscapeJsonString(output->GetName()) << "\",\n";
    ss << "        \"type\": \"" << EscapeJsonString(output->GetType()) << "\"";

    // Connection from node
    if (!output->GetNodeName().empty()) {
      ss << ",\n";
      ss << "        \"nodename\": \"" << EscapeJsonString(output->GetNodeName()) << "\",\n";
      ss << "        \"output\": \"" << EscapeJsonString(output->GetOutput()) << "\"";
    }

    ss << "\n      }";
  }
  ss << "\n    ]\n";

  ss << "  }\n";
  ss << "}\n";

  *json_str = ss.str();
  return true;
}

// Convert USD NodeGraph Prim to JSON
// TODO: Implement proper Prim API parsing
bool ConvertNodeGraphToJson(
    const Prim &nodegraph_prim,
    std::string *json_str,
    std::string *err) {

  (void)nodegraph_prim; // Unused for now

  if (!json_str) {
    if (err) *err = "json_str is nullptr";
    return false;
  }

  // STUB: Not yet implemented - requires proper understanding of Prim API
  if (err) *err = "ConvertNodeGraphToJson not yet implemented";
  return false;
}

// Convert shader with node graph connections to JSON
// TODO: Implement proper Prim API parsing
bool ConvertShaderWithNodeGraphToJson(
    const Prim &shader_prim,
    const Path &shader_abs_path,
    const Stage &stage,
    std::string *json_str,
    std::string *err) {

  if (!json_str) {
    if (err) *err = "json_str is nullptr";
    return false;
  }

  // Get the shader to check for connections to NodeGraph
  const Shader *shader = shader_prim.as<Shader>();
  if (!shader) {
    if (err) *err = "Prim is not a Shader";
    return false;
  }

  // Find the parent Material prim to locate NodeGraph children
  // The shader path is like /root/_materials/Material/bnode__Principled_BSDF
  // The Material is the parent, and NodeGraph is a sibling
  std::string shader_path = shader_abs_path.prim_part();
  size_t last_slash = shader_path.rfind('/');
  if (last_slash == std::string::npos || last_slash == 0) {
    // No parent found, no NodeGraph
    *json_str = "";
    return true;
  }
  
  std::string parent_path = shader_path.substr(0, last_slash);
  const Prim *parent_prim = nullptr;
  std::string lookup_err;
  if (!stage.find_prim_at_path(Path(parent_path, ""), parent_prim, &lookup_err) || !parent_prim) {
    // Parent not found
    *json_str = "";
    return true;
  }

  // Look for NodeGraph children in the parent Material
  const NodeGraph *nodegraph = nullptr;
  const Prim *nodegraph_prim = nullptr;
  for (const auto &child : parent_prim->children()) {
    if (child.as<NodeGraph>()) {
      nodegraph = child.as<NodeGraph>();
      nodegraph_prim = &child;
      break;
    }
  }

  if (!nodegraph || !nodegraph_prim) {
    // No NodeGraph found
    *json_str = "";
    return true;
  }

  // Build JSON from the NodeGraph structure
  std::stringstream ss;
  ss << "{\n";
  ss << "  \"version\": \"1.39\",\n";
  ss << "  \"nodegraph\": {\n";
  ss << "    \"name\": \"" << EscapeJsonString(nodegraph_prim->element_name()) << "\",\n";

  // Collect all shader nodes in the NodeGraph
  ss << "    \"nodes\": [\n";
  bool first_node = true;
  for (const auto &ng_child : nodegraph_prim->children()) {
    const Shader *node_shader = ng_child.as<Shader>();
    if (!node_shader) continue;

    if (!first_node) ss << ",\n";
    first_node = false;

    ss << "      {\n";
    ss << "        \"name\": \"" << EscapeJsonString(ng_child.element_name()) << "\",\n";
    
    // Get node type from info:id
    std::string node_type = node_shader->info_id;
    // Extract category from info:id (e.g., "ND_multiply_color3" -> "multiply")
    std::string category = node_type;
    if (node_type.find("ND_") == 0) {
      size_t underscore = node_type.find('_', 3);
      if (underscore != std::string::npos) {
        // Find the last underscore to separate category from type suffix
        size_t last_under = node_type.rfind('_');
        if (last_under > underscore) {
          category = node_type.substr(3, last_under - 3);
        } else {
          category = node_type.substr(3);
        }
      }
    }
    ss << "        \"category\": \"" << EscapeJsonString(category) << "\",\n";
    ss << "        \"type\": \"" << EscapeJsonString(node_type) << "\",\n";

    // Get properties from ShaderNode if available
    const std::map<std::string, Property> *props = &node_shader->props;
    if (const ShaderNode *shader_node = node_shader->value.as<ShaderNode>()) {
      props = &shader_node->props;
    }

    // Serialize inputs
    ss << "        \"inputs\": [\n";
    bool first_input = true;
    for (const auto &prop_pair : *props) {
      const std::string &prop_name = prop_pair.first;
      if (prop_name.find("inputs:") != 0) continue;
      
      std::string input_name = prop_name.substr(7); // Remove "inputs:" prefix
      
      if (!first_input) ss << ",\n";
      first_input = false;

      ss << "          {\n";
      ss << "            \"name\": \"" << EscapeJsonString(input_name) << "\"";

      if (prop_pair.second.is_attribute()) {
        const Attribute &attr = prop_pair.second.get_attribute();
        
        // Check for connection
        if (attr.has_connections()) {
          const auto &conns = attr.connections();
          if (!conns.empty()) {
            // Parse connection path to extract nodename and output
            std::string conn_path = conns[0].full_path_name();
            // Connection looks like </Material/NodeGraphs/node_001.outputs:out>
            size_t dot_pos = conn_path.rfind('.');
            size_t last_slash_pos = conn_path.rfind('/');
            if (dot_pos != std::string::npos && last_slash_pos != std::string::npos) {
              std::string nodename = conn_path.substr(last_slash_pos + 1, dot_pos - last_slash_pos - 1);
              std::string output = conn_path.substr(dot_pos + 1);
              if (output.find("outputs:") == 0) {
                output = output.substr(8);
              }
              ss << ",\n            \"nodename\": \"" << EscapeJsonString(nodename) << "\"";
              ss << ",\n            \"output\": \"" << EscapeJsonString(output) << "\"";
            }
          }
        } else {
          // Direct value
          ss << ",\n            \"type\": \"" << attr.type_name() << "\"";
          
          // Serialize value based on type
          if (auto v = attr.get_value<float>()) {
            ss << ",\n            \"value\": " << v.value();
          } else if (auto v = attr.get_value<int>()) {
            ss << ",\n            \"value\": " << v.value();
          } else if (auto v = attr.get_value<bool>()) {
            ss << ",\n            \"value\": " << (v.value() ? "true" : "false");
          } else if (auto v = attr.get_value<std::string>()) {
            ss << ",\n            \"value\": \"" << EscapeJsonString(v.value()) << "\"";
          } else if (auto v = attr.get_value<value::float2>()) {
            ss << ",\n            \"value\": [" << v.value()[0] << ", " << v.value()[1] << "]";
          } else if (auto v = attr.get_value<value::float3>()) {
            ss << ",\n            \"value\": [" << v.value()[0] << ", " << v.value()[1] << ", " << v.value()[2] << "]";
          } else if (auto v = attr.get_value<value::float4>()) {
            ss << ",\n            \"value\": [" << v.value()[0] << ", " << v.value()[1] << ", " << v.value()[2] << ", " << v.value()[3] << "]";
          } else if (auto v = attr.get_value<value::color3f>()) {
            ss << ",\n            \"value\": [" << v.value()[0] << ", " << v.value()[1] << ", " << v.value()[2] << "]";
          } else if (auto v = attr.get_value<value::color4f>()) {
            ss << ",\n            \"value\": [" << v.value()[0] << ", " << v.value()[1] << ", " << v.value()[2] << ", " << v.value()[3] << "]";
          } else if (auto v = attr.get_value<value::AssetPath>()) {
            ss << ",\n            \"value\": \"" << EscapeJsonString(v.value().GetAssetPath()) << "\"";
          }

          // Add colorSpace metadata if present on the attribute
          if (attr.metas().has_colorSpace()) {
            value::token cs_token = attr.metas().get_colorSpace();
            if (!cs_token.str().empty()) {
              ss << ",\n            \"colorspace\": \"" << EscapeJsonString(cs_token.str()) << "\"";
            }
          }
        }
      }

      ss << "\n          }";
    }
    ss << "\n        ]";
    ss << "\n      }";
  }
  ss << "\n    ],\n";

  // Serialize NodeGraph outputs
  ss << "    \"outputs\": [\n";
  bool first_output = true;
  for (const auto &prop_pair : nodegraph->props) {
    const std::string &prop_name = prop_pair.first;
    if (prop_name.find("outputs:") != 0) continue;
    
    std::string output_name = prop_name.substr(8); // Remove "outputs:" prefix
    
    if (!first_output) ss << ",\n";
    first_output = false;

    ss << "      {\n";
    ss << "        \"name\": \"" << EscapeJsonString(output_name) << "\"";

    if (prop_pair.second.is_attribute()) {
      const Attribute &attr = prop_pair.second.get_attribute();
      ss << ",\n        \"type\": \"" << attr.type_name() << "\"";
      
      if (attr.has_connections()) {
        const auto &conns = attr.connections();
        if (!conns.empty()) {
          std::string conn_path = conns[0].full_path_name();
          size_t dot_pos = conn_path.rfind('.');
          size_t last_slash_pos = conn_path.rfind('/');
          if (dot_pos != std::string::npos && last_slash_pos != std::string::npos) {
            std::string nodename = conn_path.substr(last_slash_pos + 1, dot_pos - last_slash_pos - 1);
            std::string output = conn_path.substr(dot_pos + 1);
            if (output.find("outputs:") == 0) {
              output = output.substr(8);
            }
            ss << ",\n        \"nodename\": \"" << EscapeJsonString(nodename) << "\"";
            ss << ",\n        \"output\": \"" << EscapeJsonString(output) << "\"";
          }
        }
      }
    }

    ss << "\n      }";
  }
  ss << "\n    ]\n";

  ss << "  },\n";

  // Add shader connections (which inputs connect to NodeGraph outputs)
  ss << "  \"connections\": [\n";
  bool first_conn = true;

  // Helper: emit a connection entry from a connection path
  std::string ng_elem_name = nodegraph_prim->element_name();
  auto emitConnection = [&](const std::string &input_name, const std::string &conn_path) {
    // Check if connection points to our NodeGraph
    if (conn_path.find(ng_elem_name) == std::string::npos) return;

    // Parse output name from connection path
    size_t dot_pos = conn_path.rfind('.');
    std::string output_name;
    if (dot_pos != std::string::npos) {
      output_name = conn_path.substr(dot_pos + 1);
      if (output_name.find("outputs:") == 0) {
        output_name = output_name.substr(8);
      }
    }

    if (!first_conn) ss << ",\n";
    first_conn = false;

    ss << "    {\n";
    ss << "      \"input\": \"" << EscapeJsonString(input_name) << "\",\n";
    ss << "      \"nodegraph\": \"" << EscapeJsonString(ng_elem_name) << "\",\n";
    ss << "      \"output\": \"" << EscapeJsonString(output_name) << "\"\n";
    ss << "    }";
  };

  // Helper: check a typed attribute field for connections
  auto checkTypedField = [&](const std::string &field_name, const auto &field) {
    if (field.has_connections()) {
      const auto &paths = field.get_connections();
      if (!paths.empty()) {
        emitConnection(field_name, paths[0].full_path_name());
      }
    }
  };

  // First: try generic props map (works for ShaderNode-based shaders)
  const std::map<std::string, Property> *shader_props = &shader->props;
  if (const ShaderNode *shader_node = shader->value.as<ShaderNode>()) {
    shader_props = &shader_node->props;
  }

  for (const auto &prop_pair : *shader_props) {
    const std::string &prop_name = prop_pair.first;
    if (prop_name.find("inputs:") != 0) continue;

    if (!prop_pair.second.is_attribute()) continue;
    const Attribute &attr = prop_pair.second.get_attribute();
    if (!attr.has_connections()) continue;

    const auto &conns = attr.connections();
    if (conns.empty()) continue;

    emitConnection(prop_name.substr(7), conns[0].full_path_name());
  }

  // Second: for MtlxOpenPBRSurface, check typed fields directly
  // (props map is empty for typed shaders - connections stored in typed fields)
  // All fields from MtlxOpenPBRSurface struct (usdMtlx.hh) must be listed here
  // to ensure no NodeGraph connections are silently dropped.
  if (const MtlxOpenPBRSurface *opbr = shader->value.as<MtlxOpenPBRSurface>()) {
    // Base
    checkTypedField("base_weight", opbr->base_weight);
    checkTypedField("base_color", opbr->base_color);
    checkTypedField("base_metalness", opbr->base_metalness);
    checkTypedField("base_diffuse_roughness", opbr->base_diffuse_roughness);
    // Specular
    checkTypedField("specular_weight", opbr->specular_weight);
    checkTypedField("specular_color", opbr->specular_color);
    checkTypedField("specular_roughness", opbr->specular_roughness);
    checkTypedField("specular_ior", opbr->specular_ior);
    checkTypedField("specular_anisotropy", opbr->specular_anisotropy);
    checkTypedField("specular_rotation", opbr->specular_rotation);
    checkTypedField("specular_roughness_anisotropy", opbr->specular_roughness_anisotropy);
    // Transmission
    checkTypedField("transmission_weight", opbr->transmission_weight);
    checkTypedField("transmission_color", opbr->transmission_color);
    checkTypedField("transmission_depth", opbr->transmission_depth);
    checkTypedField("transmission_scatter", opbr->transmission_scatter);
    checkTypedField("transmission_scatter_anisotropy", opbr->transmission_scatter_anisotropy);
    checkTypedField("transmission_dispersion", opbr->transmission_dispersion);
    checkTypedField("transmission_dispersion_abbe_number", opbr->transmission_dispersion_abbe_number);
    checkTypedField("transmission_dispersion_scale", opbr->transmission_dispersion_scale);
    // Subsurface
    checkTypedField("subsurface_weight", opbr->subsurface_weight);
    checkTypedField("subsurface_color", opbr->subsurface_color);
    checkTypedField("subsurface_radius", opbr->subsurface_radius);
    checkTypedField("subsurface_radius_scale", opbr->subsurface_radius_scale);
    checkTypedField("subsurface_scale", opbr->subsurface_scale);
    checkTypedField("subsurface_anisotropy", opbr->subsurface_anisotropy);
    checkTypedField("subsurface_scatter_anisotropy", opbr->subsurface_scatter_anisotropy);
    // Coat
    checkTypedField("coat_weight", opbr->coat_weight);
    checkTypedField("coat_color", opbr->coat_color);
    checkTypedField("coat_roughness", opbr->coat_roughness);
    checkTypedField("coat_anisotropy", opbr->coat_anisotropy);
    checkTypedField("coat_rotation", opbr->coat_rotation);
    checkTypedField("coat_roughness_anisotropy", opbr->coat_roughness_anisotropy);
    checkTypedField("coat_ior", opbr->coat_ior);
    checkTypedField("coat_darkening", opbr->coat_darkening);
    checkTypedField("coat_affect_color", opbr->coat_affect_color);
    checkTypedField("coat_affect_roughness", opbr->coat_affect_roughness);
    // Fuzz
    checkTypedField("fuzz_weight", opbr->fuzz_weight);
    checkTypedField("fuzz_color", opbr->fuzz_color);
    checkTypedField("fuzz_roughness", opbr->fuzz_roughness);
    // Thin film
    checkTypedField("thin_film_thickness", opbr->thin_film_thickness);
    checkTypedField("thin_film_ior", opbr->thin_film_ior);
    checkTypedField("thin_film_weight", opbr->thin_film_weight);
    // Emission
    checkTypedField("emission_luminance", opbr->emission_luminance);
    checkTypedField("emission_color", opbr->emission_color);
    // Geometry
    checkTypedField("geometry_opacity", opbr->geometry_opacity);
    checkTypedField("geometry_thin_walled", opbr->geometry_thin_walled);
    checkTypedField("geometry_normal", opbr->geometry_normal);
    checkTypedField("geometry_tangent", opbr->geometry_tangent);
    checkTypedField("geometry_coat_normal", opbr->geometry_coat_normal);
    checkTypedField("geometry_coat_tangent", opbr->geometry_coat_tangent);
  }

  ss << "\n  ]\n";

  ss << "}\n";

  *json_str = ss.str();
  return true;
}

// ============================================================================
// LTE SpectralAPI JSON Conversion Implementations
// ============================================================================

std::string Vec2ArrayToJsonArray(const std::vector<value::float2> &vec) {
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < vec.size(); i++) {
    if (i > 0) ss << ", ";
    ss << "[" << vec[i][0] << ", " << vec[i][1] << "]";
  }
  ss << "]";
  return ss.str();
}

std::string SpectralDataToJson(const SpectralData &data) {
  if (!data.has_data()) {
    return "null";
  }

  std::stringstream ss;
  ss << "{\n";
  ss << "    \"samples\": " << Vec2ArrayToJsonArray(data.samples) << ",\n";
  ss << "    \"interpolation\": \"" << to_string(data.interpolation) << "\",\n";
  ss << "    \"unit\": \"" << to_string(data.unit) << "\"\n";
  ss << "  }";
  return ss.str();
}

std::string SpectralIORToJson(const SpectralIOR &data) {
  if (!data.has_data()) {
    return "null";
  }

  std::stringstream ss;
  ss << "{\n";

  if (!data.samples.empty()) {
    ss << "    \"samples\": " << Vec2ArrayToJsonArray(data.samples) << ",\n";
  }

  ss << "    \"interpolation\": \"" << to_string(data.interpolation) << "\",\n";
  ss << "    \"unit\": \"" << to_string(data.unit) << "\"";

  // Add Sellmeier coefficients if using Sellmeier interpolation
  if (data.interpolation == SpectralInterpolation::Sellmeier) {
    ss << ",\n    \"sellmeier\": {\n";
    ss << "      \"B\": [" << data.sellmeier_B1 << ", " << data.sellmeier_B2 << ", " << data.sellmeier_B3 << "],\n";
    ss << "      \"C\": [" << data.sellmeier_C1 << ", " << data.sellmeier_C2 << ", " << data.sellmeier_C3 << "]\n";
    ss << "    }";
  }

  ss << "\n  }";
  return ss.str();
}

std::string SpectralEmissionToJson(const SpectralEmission &data) {
  if (!data.has_data()) {
    return "null";
  }

  std::stringstream ss;
  ss << "{\n";

  if (!data.samples.empty()) {
    ss << "    \"samples\": " << Vec2ArrayToJsonArray(data.samples) << ",\n";
  }

  ss << "    \"interpolation\": \"" << to_string(data.interpolation) << "\",\n";
  ss << "    \"unit\": \"" << to_string(data.unit) << "\"";

  // Add preset if specified
  if (data.preset != IlluminantPreset::None) {
    ss << ",\n    \"preset\": \"" << to_string(data.preset) << "\"";
  }

  ss << "\n  }";
  return ss.str();
}

} // namespace tydra
} // namespace tinyusdz
