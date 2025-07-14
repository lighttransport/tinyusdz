#include "mcp-tools.hh"
#include "mcp-server.hh"
#include "mcp-context.hh"
#include <string>
#include "tinyusdz.hh"
#include "uuid-gen.hh"

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
bool LoadUSDLayerFromFile(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);


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


bool LoadUSDLayerFromFile(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  if (!args.contains("uri")) {
    err = "`uri` param not found.\n";
    return false; 
  }

  std::string uri = args["uri"];

  
  Layer layer;
  std::string warn;
  USDLoadOptions options; 
  if (!LoadLayerFromFile(uri, &layer, &warn, &err, options)) {
    err = "Failed to load layer from file: " + err + "\n";
    return false;
  }

  if (!warn.empty()) {
    result["warnings"] = warn;
  }

  std::string uuid = generateUUID();

  if (ctx.layers.count(uuid)) {
    // This should not be happen.
    err = "Internal error. UUID conflict\n";
    return false;
  }

  USDLayer usd_layer;
  usd_layer.uri = uri;
  usd_layer.layer = std::move(layer);

  ctx.layers.emplace(uuid, std::move(usd_layer));

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


bool CallTool(Context &ctx, const std::string &tool_name, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  (void)args;

  if (tool_name == "get_version") {
    return GetVersion(result);
  } else if (tool_name == "load_usd_layer") {
    return LoadUSDLayerFromFile(ctx, args, result, err);
  }

  // tool not found.
  return false;
}


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
