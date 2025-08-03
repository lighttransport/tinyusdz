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

#if 0
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
#endif

inline std::string decode_data(const std::string &data) {

  // TODO: save memory
  std::string binary = base64_decode(data);

  return binary;


}

static std::string FindUUID(const std::string &name, const std::unordered_map<std::string, USDLayer> &layers) {

  for (const auto &it : layers) {
    if (it.second.name == name) {
      return it.first;
    }
  }

  return {};
}
 

bool GetVersion(nlohmann::json &result);
bool GetUSDDescription(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
bool GetAllUSDDescriptions(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
#if !defined(__EMSCRIPTEN__)
bool LoadUSDLayerFromFile(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
#endif
bool LoadUSDLayerFromData(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
bool StoreAsset(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
bool ReadAsset(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
bool ReadAssetPreview(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
bool GetAssetDescription(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);
bool GetAllAssetDescriptions(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err);


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


#if !defined(__EMSCRIPTEN__)
bool LoadUSDLayerFromFile(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
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

  std::string uri = args["uri"];
  std::string name = args["name"];
  std::string description = args["description"];
  
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

  std::string uuid = FindUUID(name, ctx.layers);

  if (uuid.empty()) {
    uuid = generateUUID();
  }

  USDLayer usd_layer;
  usd_layer.uri = uri;
  usd_layer.name = name;
  usd_layer.layer = std::move(layer);
  usd_layer.description = description;

  ctx.layers.emplace(uuid, std::move(usd_layer));

  DCOUT("loaded USD as Layer");

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = uuid;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}
#endif

bool LoadUSDLayerFromData(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("data")) {
    DCOUT("data param not found");
    err = "`data` param not found.\n";
    return false; 
  }
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args["name"];
  const std::string& data = args["data"];
  std::string description = args["description"];

  std::string binary = decode_data(data);
  
  Layer layer;
  std::string warn;
  USDLoadOptions options; 
  if (!LoadLayerFromMemory(reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(), name, &layer, &warn, &err, options)) {
    DCOUT("Failed to load layer from Data: " << err);
    err = "Failed to load layer from Data: " + err + "\n";
    return false;
  }

  if (!warn.empty()) {
    result["warnings"] = warn;
  }

  // Replace content if `name` already exists.
  std::string uuid = FindUUID(name, ctx.layers);

  if (uuid.empty()) {
    uuid = generateUUID();
  }

  USDLayer usd_layer;
  usd_layer.name = name;
  usd_layer.uri = name; // FIXME
  usd_layer.description = description;
  usd_layer.layer = std::move(layer);

  ctx.layers.emplace(uuid, std::move(usd_layer));

  DCOUT("loaded USD as Layer");

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = uuid;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool ReadAsset(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args["name"];

  if (!ctx.assets.count(name)) {
    err = "Asset not found: " + name + "\n";
    return false;
  }
  const std::string& data = ctx.assets.at(name).data;

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = data;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool StoreAsset(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("data")) {
    DCOUT("data param not found");
    err = "`data` param not found.\n";
    return false; 
  }
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args["name"];
  const std::string& data = args["data"];
  std::string description = args["description"];

  std::string uuid = generateUUID();

  MCPAsset asset;
  asset.name = name;
  asset.data = data;
  asset.description = description;
  asset.uuid = uuid;

  // Handle preview image if provided
  if (args.contains("preview") && args["preview"].is_object()) {
    const auto& preview = args["preview"];
    
    // Check for required preview fields
    if (preview.contains("data") && preview.contains("mimeType")) {
      asset.preview.data = preview["data"];
      asset.preview.mimeType = preview["mimeType"];
      
      // Optional preview name
      if (preview.contains("name")) {
        asset.preview.name = preview["name"];
      }
    }
  }

  ctx.assets.emplace(name, std::move(asset));

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = uuid;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool ListPrimSpecs(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["uuid"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

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

bool ReadAssetPreview(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args["name"];

  // Assets are stored by name
  if (!ctx.assets.count(name)) {
    err = "Asset not found: " + name + "\n";
    return false;
  }

  const auto& asset = ctx.assets.at(name);
  
  if (asset.preview.data.empty()) {
    err = "Asset '" + name + "' has no preview image\n";
    return false;
  }

  result["content"] = nlohmann::json::array();

  nlohmann::json content;
  content["type"] = "image";
  content["data"] = asset.preview.data;
  content["mimeType"] = asset.preview.mimeType;
  
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

bool GetUSDDescription(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args.at("name");

  std::string uuid = FindUUID(name, ctx.layers);
  
  if (!ctx.layers.count(uuid)) {
    // This should not happen though.
    err = "Internal error. No corresponding Layer found\n";
    return false;
  }

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = ctx.layers.at(uuid).description;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool GetAllUSDDescriptions(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  (void)args;
  (void)err;

  result["content"] = nlohmann::json::array();

  for (const auto &it : ctx.layers) {
    nlohmann::json content;
    content["type"] = "text";
    content["text"] = it.second.name + ":" + it.second.description;

    result["content"].push_back(content);
  }

  return true;
}

bool GetAssetDescription(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args.at("name");

  // Assets are stored by name, not UUID
  if (!ctx.assets.count(name)) {
    err = "Asset not found: " + name + "\n";
    return false;
  }

  const auto& asset = ctx.assets.at(name);

  nlohmann::json content;
  content["type"] = "text";
  
  // Build description including preview info if available
  std::string desc = asset.description;
  if (!asset.preview.data.empty()) {
    desc += "\n[Has preview: " + asset.preview.mimeType;
    if (!asset.preview.name.empty()) {
      desc += ", name: " + asset.preview.name;
    }
    desc += "]";
  }
  content["text"] = desc;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool GetAllAssetDescriptions(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  (void)args;
  (void)err;

  result["content"] = nlohmann::json::array();

  for (const auto &it : ctx.assets) {
    {
      nlohmann::json desc_content;
      desc_content["type"] = "text";
      desc_content["text"] = it.second.name + ":" + it.second.description;

      result["content"].push_back(desc_content);
    }

    {
      nlohmann::json img_content;
      img_content["type"] = "text";
      img_content["mimeType"] = it.second.preview.mimeType;
      img_content["data"] = it.second.preview.data;

      result["content"].push_back(img_content);
    }

  }

  return true;
}

bool ToUSDA(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("uri")) {
    DCOUT("name param not found");
    err = "`name` param not found.\n";
    return false; 
  }

  std::string name = args.at("name");
  
  std::string uuid = FindUUID(name, ctx.layers);

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

bool SelectAssets(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("names")) {
    DCOUT("names param not found");
    err = "`names` param not found.\n";
    return false; 
  }

  std::vector<std::string> names = args.at("names");

  ctx.selected_assets.clear();
  for (const auto &name : names) {
    if (ctx.assets.count(name)) {
      ctx.selected_assets.push_back(name);
    }
  }

  //nlohmann::json content;
  //content["type"] = "text";
  //content["text"] = ctx.assets.at(uuid).description;

  result["content"] = nlohmann::json::array();
  //result["content"].push_back(content);

  return true;
}

bool GetSelectedAssets(Context &ctx, const nlohmann::json &args, nlohmann::json &result, std::string &err) {
  (void)err;
  (void)args;
  DCOUT("args " << args);

  result["content"] = nlohmann::json::array();
  for (const auto &name : ctx.selected_assets) {
    nlohmann::json content;
    content["type"] = "text";
    content["text"] = name;
    result["content"].push_back(content);
  }


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
    j["name"] = "get_all_usd_descriptions";
    j["description"] = "Get description of all loaded USD Layers";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    //schena["required"] = nlohmann::json::array();

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_usd_description";
    j["description"] = "Get description of loaded USD Layer";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] ={{"type", "string"}}; // TODO: accept multiple names
    
    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_all_asset_descriptions";
    j["description"] = "Get description of all Assets";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    //schena["required"] = nlohmann::json::array();

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_asset_description";
    j["description"] = "Get description of Asset";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] ={{"type", "string"}}; // TODO: accept multiple names
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
    schema["properties"]["name"] ={{"type", "string"}};
    schema["properties"]["description"] ={{"type", "string"}}; // optional

    schema["required"] = nlohmann::json::array({"uri", "name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "load_usd_layer_from_data";
    j["description"] = "Load USD as Layer from base64 encoded data string";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["data"] ={{"type", "string"}};
    schema["properties"]["name"] ={{"type", "string"}};
    schema["properties"]["description"] ={{"type", "string"}}; // optional

    schema["required"] = nlohmann::json::array({"data", "name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "load_usd_layer_from_asset";
    j["description"] = "Load USD as Layer from Asset";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }

  {
    nlohmann::json j;
    j["name"] = "read_asset";
    j["description"] = "Read asset as base64 string";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "store_asset";
    j["description"] = "Store asset(e.g. USD, texture) with optional preview image. `data` is base64 encoded string.";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["data"] ={{"type", "string"}};
    schema["properties"]["name"] ={{"type", "string"}};
    schema["properties"]["description"] ={{"type", "string"}}; // optional
    
    // Add preview object schema
    nlohmann::json previewSchema;
    previewSchema["type"] = "object";
    previewSchema["properties"] = nlohmann::json::object();
    previewSchema["properties"]["data"] = {{"type", "string"}, {"description", "Base64 encoded preview image"}};
    previewSchema["properties"]["mimeType"] = {{"type", "string"}, {"description", "MIME type (e.g. 'image/png', 'image/jpeg')"}};
    previewSchema["properties"]["name"] = {{"type", "string"}, {"description", "Optional name for the preview image"}};
    previewSchema["required"] = nlohmann::json::array({"data", "mimeType"});
    
    schema["properties"]["preview"] = previewSchema; // optional

    schema["required"] = nlohmann::json::array({"data", "name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }


  {
    nlohmann::json j;
    j["name"] = "list_primspecs";
    j["description"] = "List root PrimSpecs in loaded USD Layer(uuid or name)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] ={{"type", "string"}};
    schema["properties"]["name"] ={{"type", "string"}};

    //schema["required"] = nlohmann::json::array({"uuid"});

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
    schema["properties"]["name"] ={{"type", "string"}};

    schema["required"] = nlohmann::json::array({"name"});

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

  {
    nlohmann::json j;
    j["name"] = "select_assets";
    j["description"] = "Select assets. Specify by the array of asset names.";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    // string[]
    schema["properties"]["names"] ={{"type", "array"}, {"items", {"type", "string"}}};
    schema["required"] = nlohmann::json::array({"names"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_selected_assets";
    j["description"] = "Get selected asset names";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();

    j["inputSchema"] = schema;

    result["tools"].push_back(j);

  }
  
  {
    nlohmann::json j;
    j["name"] = "read_asset_preview";
    j["description"] = "Read preview image from an asset";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Asset name"}};

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
  } else if (tool_name == "get_all_usd_descriptions") {
    return GetAllUSDDescriptions(ctx, args, result, err);
  } else if (tool_name == "get_usd_description") {
    return GetUSDDescription(ctx, args, result, err);
#if !defined(__EMSCRIPTEN__)
  } else if (tool_name == "load_usd_layer_from_file") {
    DCOUT("load_usd_layer_from_file");
    return LoadUSDLayerFromFile(ctx, args, result, err);
#endif
  } else if (tool_name == "to_usda") {
    DCOUT("to_usda");
    return ToUSDA(ctx, args, result, err);
  } else if (tool_name == "load_usd_layer_from_data") {
    DCOUT("load_usd_layer_data");
    return LoadUSDLayerFromData(ctx, args, result, err);
  } else if (tool_name == "list_primspecs") {
    DCOUT("list_primspecs");
    return ListPrimSpecs(ctx, args, result, err);
  } else if (tool_name == "list_screenshots") {
    return ListScreenshots(ctx, args, result, err);
  } else if (tool_name == "save_screenshot") {
    return SaveScreenshot(ctx, args, result, err);
  } else if (tool_name == "read_screenshot") {
    return ReadScreenshot(ctx, args, result, err);
  } else if (tool_name == "read_asset") {
    DCOUT("read_asset");
    return ReadAsset(ctx, args, result, err);
  } else if (tool_name == "read_asset_preview") {
    DCOUT("read_asset_preview");
    return ReadAssetPreview(ctx, args, result, err);
  } else if (tool_name == "store_asset") {
    DCOUT("store_asset");
    return StoreAsset(ctx, args, result, err);
  } else if (tool_name == "get_all_asset_descriptions") {
    return GetAllAssetDescriptions(ctx, args, result, err);
  } else if (tool_name == "get_asset_description") {
    return GetAssetDescription(ctx, args, result, err);
  } else if (tool_name == "select_assets") {
    return SelectAssets(ctx, args, result, err);
  } else if (tool_name == "get_selected_assets") {
    return GetSelectedAssets(ctx, args, result, err);
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
