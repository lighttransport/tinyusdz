#include <string>

#include "mcp-tools.hh"
#include "mcp-server.hh"
#include "mcp-context.hh"
#include "tinyusdz.hh"
#include "uuid-gen.hh"
#include "str-util.hh"
#include "pprinter.hh"

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

inline std::string decode_datauri(const std::string &data) {

  const std::string prefix = "data:application/octet-stream;base64,";

  if (!startsWith(data, prefix)) {
    return {};
  }

  if (data.size() <= prefix.size()) {
    return {};
  }

  // TODO: save memory
  std::string binary = base64_decode(removePrefix(data, prefix));

  return binary;


}

bool GetVersion(nlohmann::json &result);
bool LoadUSDLayerFromFile(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
bool LoadUSDLayerFromDataURI(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);


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
  DCOUT("args " << args);
  if (!args.contains("uri")) {
    DCOUT("uri param not found");
    err = "`uri` param not found.\n";
    return false; 
  }

  std::string uri = args["uri"];
  
  Layer layer;
  std::string warn;
  USDLoadOptions options; 
  if (!LoadLayerFromFile(uri, &layer, &warn, &err, options)) {
    DCOUT("Failed to load layer from file: " << err);
    err = "Failed to load layer from file: " + err + "\n";
    return false;
  }

  if (!warn.empty()) {
    result["warnings"] = warn;
  }

  std::string uuid = generateUUID();

  if (ctx.layers.count(uuid)) {
    DCOUT("uuid conflict");
    // This should not be happen.
    err = "Internal error. UUID conflict\n";
    return false;
  }

  USDLayer usd_layer;
  usd_layer.uri = uri;
  usd_layer.layer = std::move(layer);

  ctx.layers.emplace(uuid, std::move(usd_layer));
  ctx.resources.emplace(uri, uuid);

  DCOUT("loaded USD as Layer");

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = uuid;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool LoadUSDLayerFromDataURI(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("uri")) {
    DCOUT("uri param not found");
    err = "`uri` param not found.\n";
    return false; 
  }
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args["name"];

  std::string binary = decode_datauri(args.at("uri"));
  
  Layer layer;
  std::string warn;
  USDLoadOptions options; 
  if (!LoadLayerFromMemory(reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(), name, &layer, &warn, &err, options)) {
    DCOUT("Failed to load layer from DataURI: " << err);
    err = "Failed to load layer from DataURI: " + err + "\n";
    return false;
  }

  if (!warn.empty()) {
    result["warnings"] = warn;
  }

  std::string uuid = generateUUID();

  if (ctx.layers.count(uuid)) {
    DCOUT("uuid conflict");
    // This should not be happen.
    err = "Internal error. UUID conflict\n";
    return false;
  }

  USDLayer usd_layer;
  usd_layer.uri = name;
  usd_layer.layer = std::move(layer);

  ctx.layers.emplace(uuid, std::move(usd_layer));
  ctx.resources.emplace(name, uuid);

  DCOUT("loaded USD as Layer");

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = uuid;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool ListPrimSpecs(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("uuid")) {
    DCOUT("uuid param not found");
    err = "`uuid` param not found.\n";
    return false; 
  }

  std::string uuid = args["uuid"];

  if (!ctx.layers.count(uuid)) {
    DCOUT("Layer not found: " << uuid);
    err = "Layer not found: " + uuid;
    return false;
  }

  const USDLayer &usd_layer = ctx.layers.at(uuid);

  result["content"] = nlohmann::json::array();

  for (const auto &ps : usd_layer.layer.primspecs()) {
    nlohmann::json content;
    content["type"] = "text";
    content["text"] = ps.first;
    result["content"].push_back(content);
  }
  
  return true;
}

bool ListScreenshots(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  (void)args;
  (void)err;

  result["content"] = nlohmann::json::array();

  for (const auto &it : ctx.screenshots) {
    nlohmann::json content;
    content["type"] = "text";
    content["text"] = it.first; // name
    result["content"].push_back(content);
  }
  
  return true;
}

bool SaveScreenshot(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }
  if (!args.contains("data")) {
    DCOUT("data param not found");
    err = "`data` param not found.\n";
    return false; 
  }
  if (!args.contains("mimeType")) {
    DCOUT("mimeType param not found");
    err = "`mimeType` param not found.\n";
    return false; 
  }

  std::string name = args["name"];
  std::string data = args["data"];
  std::string mimeType = args["mimeType"];

  Screenshot screenshot;
  screenshot.uuid = UUIDGenerator::generateUUID();
  screenshot.data = data;
  screenshot.mimeType = mimeType;

  ctx.screenshots[name] = screenshot;

  result["content"] = nlohmann::json::array();

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = screenshot.uuid;
  result["content"].push_back(content);
  
  return true;
}

bool ReadScreenshot(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args["name"];

  if (!ctx.screenshots.count(name)) {
    DCOUT("Screenshot not found: " << name);
    err = "Screenshot not found: " + name;
    return false;
  }

  const auto &screenshot = ctx.screenshots.at(name);

  result["content"] = nlohmann::json::array();

  nlohmann::json content;
  content["type"] = "image";
  content["data"] = screenshot.data; // base64-encoded-data
  content["mimeType"] = screenshot.mimeType;

  // optional
  content["annotations"] = nlohmann::json::object();
  content["annotations"]["audience"] = nlohmann::json::array();
  content["annotations"]["audience"].push_back("user");
  content["annotations"]["priority"] = 0.9;

  result["content"].push_back(content);
  
  return true;
}

bool ToUSDA(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("uri")) {
    DCOUT("uri param not found");
    err = "`uri` param not found.\n";
    return false; 
  }

  std::string uri = args.at("uri");
  
  if (!ctx.resources.count(uri)) {
    err = "Resource not found: " + uri + "\n";
    return false; 
  }

  std::string uuid = ctx.resources.at(uri);

  if (!ctx.layers.count(uuid)) {
    // This should not happen though.
    err = "Internal error. No corresponding Layer found\n";
    return false;
  }

  nlohmann::json content;
  content["type"] = "text";
  content["mimeType"] = "text/plain";

  const Layer &layer = ctx.layers.at(uuid).layer;
  std::string str = to_string(layer); // to USDA
  content["text"] = str;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

} // namespace

bool GetToolsList(Context &ctx, nlohmann::json &result) {
  (void)ctx;

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

  {
    nlohmann::json j;
    j["name"] = "load_usd_layer_from_file";
    j["description"] = "Load USD as Layer from a file(only works in C++ native binary)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uri"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"uri"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "load_usd_layer_from_datauri";
    j["description"] = "Load USD as Layer from DataURI";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uri"] ={{"type", "string"}};
    schema["properties"]["name"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"uri", "name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "list_primspecs";
    j["description"] = "List root PrimSpecs in loaded USD Layer";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"uuid"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "to_usda";
    j["description"] = "Convert USD Layer to USDA text";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uri"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"uri"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "save_screenshot";
    j["description"] = "Save screenshot image(`data` is a base64 encoded string of image data)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["data"] ={{"type", "string"}};
    schema["properties"]["name"] ={{"type", "string"}};
    schema["properties"]["mimeType"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"data", "name", "mimeType"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "list_screenshots";
    j["description"] = "List screenshot image names";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "read_screenshot";
    j["description"] = "Read screenshot image";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }


  std::cout << result << "\n";

  return true;
}


bool CallTool(Context &ctx, const std::string &tool_name, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  (void)args;

  if (tool_name == "get_version") {
    return GetVersion(result);
  } else if (tool_name == "load_usd_layer_from_file") {
    DCOUT("load_usd_layer_from_file");
    return LoadUSDLayerFromFile(ctx, args, result, err);
  } else if (tool_name == "to_usda") {
    DCOUT("to_usda");
    return ToUSDA(ctx, args, result, err);
  } else if (tool_name == "load_usd_layer_from_datauri") {
    DCOUT("load_usd_layer_datauri");
    return LoadUSDLayerFromDataURI(ctx, args, result, err);
  } else if (tool_name == "list_primspecs") {
    DCOUT("list_primspecs");
    return ListPrimSpecs(ctx, args, result, err);
  } else if (tool_name == "list_screenshots") {
    return ListScreenshots(ctx, args, result, err);
  } else if (tool_name == "save_screenshot") {
    return SaveScreenshot(ctx, args, result, err);
  } else if (tool_name == "read_screenshot") {
    return ReadScreenshot(ctx, args, result, err);
#if 0
  } else if (tool_name == "get_texture_asset") {
    return GetTextureAsset(ctx, args, result, err);
  } else if (tool_name == "change_texture_asset") {
    return ChangeTextureAsset(ctx, args, result, err);
#endif
  }

  // tool not found.
  return false;
}


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
