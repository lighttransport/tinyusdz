// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// MaterialX NodeGraph to JSON Converter Implementation
//

#include "materialx-to-json.hh"

#include <sstream>
#include <algorithm>
#include <cctype>

#include "prim-types.hh"
#include "stage.hh"
#include "value-types.hh"
#include "value-pprint.hh"
#include "usdMtlx.hh"
#include "mtlx-dom.hh"
#include "str-util.hh"

namespace tinyusdz {
namespace tydra {

// Helper: Escape JSON string
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

// Helper: Convert float vector to JSON array
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

// Helper: Convert int vector to JSON array
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

// Helper: Convert string vector to JSON array
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
    case mtlx::MtlxValue::TYPE_BOOL:
      return value.bool_val ? "true" : "false";

    case mtlx::MtlxValue::TYPE_INT: {
      std::stringstream ss;
      ss << value.int_val;
      return ss.str();
    }

    case mtlx::MtlxValue::TYPE_FLOAT: {
      std::stringstream ss;
      ss << value.float_val;
      return ss.str();
    }

    case mtlx::MtlxValue::TYPE_STRING:
      return "\"" + EscapeJsonString(value.string_val) + "\"";

    case mtlx::MtlxValue::TYPE_FLOAT_VECTOR:
      return FloatVectorToJsonArray(value.float_vec);

    case mtlx::MtlxValue::TYPE_INT_VECTOR:
      return IntVectorToJsonArray(value.int_vec);

    case mtlx::MtlxValue::TYPE_STRING_VECTOR:
      return StringVectorToJsonArray(value.string_vec);

    case mtlx::MtlxValue::TYPE_NONE:
    default:
      return "null";
  }
}

// Helper: Convert USD Attribute value to JSON value string
static std::string AttributeValueToJsonValue(const value::Value &attr_value) {
  std::stringstream ss;

  if (attr_value.is<bool>()) {
    ss << (attr_value.as<bool>() ? "true" : "false");
  } else if (attr_value.is<int>()) {
    ss << attr_value.as<int>();
  } else if (attr_value.is<float>()) {
    ss << attr_value.as<float>();
  } else if (attr_value.is<double>()) {
    ss << attr_value.as<double>();
  } else if (attr_value.is<std::string>()) {
    ss << "\"" << EscapeJsonString(attr_value.as<std::string>()) << "\"";
  } else if (attr_value.is<value::token>()) {
    ss << "\"" << EscapeJsonString(attr_value.as<value::token>().str()) << "\"";
  } else if (attr_value.is<value::float2>()) {
    auto v = attr_value.as<value::float2>();
    ss << "[" << v[0] << ", " << v[1] << "]";
  } else if (attr_value.is<value::float3>()) {
    auto v = attr_value.as<value::float3>();
    ss << "[" << v[0] << ", " << v[1] << ", " << v[2] << "]";
  } else if (attr_value.is<value::float4>()) {
    auto v = attr_value.as<value::float4>();
    ss << "[" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << "]";
  } else if (attr_value.is<value::color3f>()) {
    auto v = attr_value.as<value::color3f>();
    ss << "[" << v.r << ", " << v.g << ", " << v.b << "]";
  } else if (attr_value.is<value::color4f>()) {
    auto v = attr_value.as<value::color4f>();
    ss << "[" << v.r << ", " << v.g << ", " << v.b << ", " << v.a << "]";
  } else {
    // Fallback: Try to convert to string
    ss << "\"" << EscapeJsonString(value::pprint_value(attr_value)) << "\"";
  }

  return ss.str();
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
  ss << "  \"version\": \"1.38\",\n"; // MaterialX version
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

    if (!node->GetNodeDef().empty()) {
      ss << "        \"nodedef\": \"" << EscapeJsonString(node->GetNodeDef()) << "\",\n";
    }

    // Serialize node inputs
    const auto &inputs = node->GetInputs();
    ss << "        \"inputs\": [\n";
    for (size_t j = 0; j < inputs.size(); j++) {
      const auto &input = inputs[j];
      if (j > 0) ss << ",\n";

      ss << "          {\n";
      ss << "            \"name\": \"" << EscapeJsonString(input->GetName()) << "\",\n";
      ss << "            \"type\": \"" << EscapeJsonString(input->GetType()) << "\"";

      // Value or connection
      if (!input->GetNodeName().empty()) {
        ss << ",\n";
        ss << "            \"nodename\": \"" << EscapeJsonString(input->GetNodeName()) << "\"";
        if (!input->GetOutput().empty()) {
          ss << ",\n";
          ss << "            \"output\": \"" << EscapeJsonString(input->GetOutput()) << "\"";
        }
      } else if (input->GetValue().type != mtlx::MtlxValue::TYPE_NONE) {
        ss << ",\n";
        ss << "            \"value\": " << MtlxValueToJsonValue(input->GetValue());
      }

      if (!input->GetChannels().empty()) {
        ss << ",\n";
        ss << "            \"channels\": \"" << EscapeJsonString(input->GetChannels()) << "\"";
      }

      if (!input->GetInterfaceName().empty()) {
        ss << ",\n";
        ss << "            \"interfacename\": \"" << EscapeJsonString(input->GetInterfaceName()) << "\"";
      }

      ss << "\n          }";
    }
    ss << "\n        ],\n";

    // Serialize node outputs
    const auto &outputs = node->GetOutputs();
    ss << "        \"outputs\": [\n";
    for (size_t j = 0; j < outputs.size(); j++) {
      const auto &output = outputs[j];
      if (j > 0) ss << ",\n";

      ss << "          {\n";
      ss << "            \"name\": \"" << EscapeJsonString(output->GetName()) << "\",\n";
      ss << "            \"type\": \"" << EscapeJsonString(output->GetType()) << "\"\n";
      ss << "          }";
    }
    ss << "\n        ]\n";
    ss << "      }";
  }
  ss << "\n    ],\n";

  // Serialize nodegraph outputs
  ss << "    \"outputs\": [\n";
  const auto &ng_outputs = nodegraph.GetOutputs();
  for (size_t i = 0; i < ng_outputs.size(); i++) {
    const auto &output = ng_outputs[i];
    if (i > 0) ss << ",\n";

    ss << "      {\n";
    ss << "        \"name\": \"" << EscapeJsonString(output->GetName()) << "\",\n";
    ss << "        \"type\": \"" << EscapeJsonString(output->GetType()) << "\"";

    if (!output->GetNodeName().empty()) {
      ss << ",\n";
      ss << "        \"nodename\": \"" << EscapeJsonString(output->GetNodeName()) << "\"";
      if (!output->GetOutput().empty()) {
        ss << ",\n";
        ss << "        \"output\": \"" << EscapeJsonString(output->GetOutput()) << "\"";
      }
    }

    ss << "\n      }";
  }
  ss << "\n    ]\n";

  ss << "  }\n";
  ss << "}\n";

  *json_str = ss.str();
  return true;
}

// Convert USD/MaterialX NodeGraph Prim to JSON
bool ConvertNodeGraphToJson(
    const Prim &nodegraph_prim,
    std::string *json_str,
    std::string *err) {

  if (!json_str) {
    if (err) *err = "json_str is nullptr";
    return false;
  }

  // Check if it's a NodeGraph prim
  if (nodegraph_prim.typeName() != "NodeGraph") {
    if (err) *err = "Prim is not a NodeGraph (type: " + nodegraph_prim.typeName() + ")";
    return false;
  }

  std::stringstream ss;
  ss << "{\n";
  ss << "  \"version\": \"1.38\",\n";
  ss << "  \"nodegraph\": {\n";
  ss << "    \"name\": \"" << EscapeJsonString(nodegraph_prim.element_name()) << "\",\n";
  ss << "    \"nodes\": [\n";

  // Iterate through child prims (nodes)
  bool first_node = true;
  for (const auto &child : nodegraph_prim.children()) {
    // Skip non-node children
    if (child.typeName() == "Shader" || child.is_shader()) {
      if (!first_node) ss << ",\n";
      first_node = false;

      ss << "      {\n";
      ss << "        \"name\": \"" << EscapeJsonString(child.element_name()) << "\",\n";

      // Get shader info:id for category
      std::string category;
      if (child.metas().has("info:id")) {
        auto info_id = child.metas().get("info:id");
        if (info_id) {
          category = info_id->as<std::string>();
        }
      }

      ss << "        \"category\": \"" << EscapeJsonString(category) << "\",\n";
      ss << "        \"type\": \"" << EscapeJsonString(child.typeName()) << "\",\n";

      // Serialize inputs
      ss << "        \"inputs\": [\n";
      bool first_input = true;

      for (const auto &attr_name : child.get_attribute_names()) {
        // Filter for inputs (starting with "inputs:")
        if (startsWith(attr_name, "inputs:")) {
          std::string input_name = attr_name.substr(7); // Remove "inputs:" prefix

          if (!first_input) ss << ",\n";
          first_input = false;

          ss << "          {\n";
          ss << "            \"name\": \"" << EscapeJsonString(input_name) << "\",\n";

          // Get attribute
          auto attr_opt = child.get_attribute(attr_name);
          if (attr_opt) {
            const auto &attr = attr_opt.value();

            // Type name
            std::string type_name = attr.type_name();
            ss << "            \"type\": \"" << EscapeJsonString(type_name) << "\"";

            // Check for connection
            if (attr.is_connection()) {
              auto paths = attr.get_connections();
              if (!paths.empty()) {
                ss << ",\n";
                std::string conn_path = paths[0].full_path_name();

                // Parse connection path (e.g., "/NodeGraph/node1.output")
                size_t dot_pos = conn_path.rfind('.');
                if (dot_pos != std::string::npos) {
                  std::string nodename = conn_path.substr(0, dot_pos);
                  std::string output = conn_path.substr(dot_pos + 1);

                  // Remove nodegraph prefix from nodename
                  size_t last_slash = nodename.rfind('/');
                  if (last_slash != std::string::npos) {
                    nodename = nodename.substr(last_slash + 1);
                  }

                  ss << "            \"nodename\": \"" << EscapeJsonString(nodename) << "\",\n";
                  ss << "            \"output\": \"" << EscapeJsonString(output) << "\"";
                }
              }
            } else if (attr.has_value()) {
              // Direct value
              ss << ",\n";
              ss << "            \"value\": " << AttributeValueToJsonValue(attr.get_value());
            }
          }

          ss << "\n          }";
        }
      }

      ss << "\n        ],\n";
      ss << "        \"outputs\": [\n";
      ss << "          {\n";
      ss << "            \"name\": \"out\",\n";
      ss << "            \"type\": \"color3\"\n"; // Default for now
      ss << "          }\n";
      ss << "        ]\n";
      ss << "      }";
    }
  }

  ss << "\n    ],\n";
  ss << "    \"outputs\": []\n"; // Outputs handled separately
  ss << "  }\n";
  ss << "}\n";

  *json_str = ss.str();
  return true;
}

// Convert shader with node graph connections to JSON
bool ConvertShaderWithNodeGraphToJson(
    const Prim &shader_prim,
    const Stage &stage,
    std::string *json_str,
    std::string *err) {

  if (!json_str) {
    if (err) *err = "json_str is nullptr";
    return false;
  }

  // First, find connected node graphs
  std::vector<std::string> nodegraph_jsons;
  std::vector<std::pair<std::string, std::string>> connections; // input -> nodegraph.output

  // Iterate through shader inputs to find connections
  for (const auto &attr_name : shader_prim.get_attribute_names()) {
    if (startsWith(attr_name, "inputs:")) {
      std::string input_name = attr_name.substr(7);

      auto attr_opt = shader_prim.get_attribute(attr_name);
      if (attr_opt && attr_opt.value().is_connection()) {
        auto paths = attr_opt.value().get_connections();
        if (!paths.empty()) {
          std::string conn_path = paths[0].full_path_name();

          // Parse connection (e.g., "/NodeGraph1.output")
          size_t dot_pos = conn_path.rfind('.');
          if (dot_pos != std::string::npos) {
            std::string nodegraph_path = conn_path.substr(0, dot_pos);
            std::string output_name = conn_path.substr(dot_pos + 1);

            // Get nodegraph name
            size_t last_slash = nodegraph_path.rfind('/');
            std::string nodegraph_name = (last_slash != std::string::npos)
                ? nodegraph_path.substr(last_slash + 1)
                : nodegraph_path;

            connections.push_back({input_name, nodegraph_name + "." + output_name});

            // Try to get the nodegraph prim and convert it
            if (auto ng_prim_opt = stage.GetPrimAtPath(Path(nodegraph_path))) {
              std::string ng_json;
              if (ConvertNodeGraphToJson(ng_prim_opt.value(), &ng_json, err)) {
                nodegraph_jsons.push_back(ng_json);
              }
            }
          }
        }
      }
    }
  }

  // Build combined JSON
  std::stringstream ss;
  ss << "{\n";
  ss << "  \"version\": \"1.38\",\n";
  ss << "  \"nodegraphs\": [\n";

  for (size_t i = 0; i < nodegraph_jsons.size(); i++) {
    if (i > 0) ss << ",\n";
    ss << "    " << nodegraph_jsons[i];
  }

  ss << "\n  ],\n";
  ss << "  \"connections\": [\n";

  for (size_t i = 0; i < connections.size(); i++) {
    if (i > 0) ss << ",\n";
    ss << "    {\n";
    ss << "      \"input\": \"" << EscapeJsonString(connections[i].first) << "\",\n";

    // Parse nodegraph.output
    const auto &conn_str = connections[i].second;
    size_t dot_pos = conn_str.find('.');
    if (dot_pos != std::string::npos) {
      std::string ng_name = conn_str.substr(0, dot_pos);
      std::string output_name = conn_str.substr(dot_pos + 1);
      ss << "      \"nodegraph\": \"" << EscapeJsonString(ng_name) << "\",\n";
      ss << "      \"output\": \"" << EscapeJsonString(output_name) << "\"\n";
    }

    ss << "    }";
  }

  ss << "\n  ]\n";
  ss << "}\n";

  *json_str = ss.str();
  return true;
}

} // namespace tydra
} // namespace tinyusdz
