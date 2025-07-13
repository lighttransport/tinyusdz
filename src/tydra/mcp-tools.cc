#include "mcp-tools.hh"
#include "tinyusdz.hh"

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
namespace mcp {

namespace {
bool GetVersion(nlohmann::json &result);

bool GetVersion(nlohmann::json &result) {
  
  std::string ver_str = std::to_string(tinyusdz::version_major) + "." + std::to_string(tinyusdz::version_minor) + "." + std::to_string(tinyusdz::version_micro);
  std::string rev = tinyusdz::version_rev;
  
  if (rev.size()) {
    ver_str += "." + rev;
  }

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = ver_str;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;

}

} // namespace

bool GetToolsList(nlohmann::json &result) {

  result["tools"] = nlohmann::json::array();

  {
    nlohmann::json j;
    j["name"] = "get_version";
    j["description"] = "Get TinyUSDZ MCP server version";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    //schena["required"] = nlohmann::json::array();

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  return true;
}


bool CallTool(const std::string &tool_name, const nlohmann::json &args, nlohmann::json &result) {
  (void)args;

  if (tool_name == "get_version") {
    return GetVersion(result);
  }

  // tool not found.
  return false;
}


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
