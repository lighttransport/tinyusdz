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
#include "value-pprint.hh"

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
    const Stage &stage,
    std::string *json_str,
    std::string *err) {

  (void)shader_prim; // Unused for now
  (void)stage; // Unused for now

  if (!json_str) {
    if (err) *err = "json_str is nullptr";
    return false;
  }

  // STUB: Not yet implemented - requires proper understanding of Prim API
  if (err) *err = "ConvertShaderWithNodeGraphToJson not yet implemented";
  return false;
}

} // namespace tydra
} // namespace tinyusdz
