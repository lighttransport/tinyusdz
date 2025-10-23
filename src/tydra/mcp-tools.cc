#include "mcp-tools.hh"

#include <string>
#include <sstream>

#include "mcp-context.hh"
#include "mcp-server.hh"
#include "pprinter.hh"
#include "str-util.hh"
#include "tinyusdz.hh"
#include "uuid-gen.hh"
#include "stream-reader.hh"
#include "usda-reader.hh"
#include "usdc-reader.hh"

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

static std::string FindUUID(
    const std::string &name,
    const std::unordered_map<std::string, USDLayer> &layers) {
  for (const auto &it : layers) {
    if (it.second.name == name) {
      return it.first;
    }
  }

  return {};
}

bool GetVersion(nlohmann::json &result);
bool GetUSDDescription(Context &ctx, const nlohmann::json &args,
                       nlohmann::json &result, std::string &err);
bool GetAllUSDDescriptions(Context &ctx, const nlohmann::json &args,
                           nlohmann::json &result, std::string &err);
#if !defined(__EMSCRIPTEN__)
bool LoadUSDLayerFromFile(Context &ctx, const nlohmann::json &args,
                          nlohmann::json &result, std::string &err);
#endif
bool LoadUSDLayerFromData(Context &ctx, const nlohmann::json &args,
                          nlohmann::json &result, std::string &err);
bool StoreAsset(Context &ctx, const nlohmann::json &args,
                nlohmann::json &result, std::string &err);
bool ReadAsset(Context &ctx, const nlohmann::json &args, nlohmann::json &result,
               std::string &err);
bool ReadAssetPreview(Context &ctx, const nlohmann::json &args,
                      nlohmann::json &result, std::string &err);
bool GetAssetDescription(Context &ctx, const nlohmann::json &args,
                         nlohmann::json &result, std::string &err);
bool GetAllAssetDescriptions(Context &ctx, const nlohmann::json &args,
                             nlohmann::json &result, std::string &err);
bool ListSublayers(Context &ctx, const nlohmann::json &args,
                   nlohmann::json &result, std::string &err);
bool ListReferences(Context &ctx, const nlohmann::json &args,
                    nlohmann::json &result, std::string &err);
bool ListPayloads(Context &ctx, const nlohmann::json &args,
                  nlohmann::json &result, std::string &err);
bool AddPrim(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err);

bool GetVersion(nlohmann::json &result) {
  std::string ver_str = std::to_string(tinyusdz::version_major) + "." +
                        std::to_string(tinyusdz::version_minor) + "." +
                        std::to_string(tinyusdz::version_micro);
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
bool LoadUSDLayerFromFile(Context &ctx, const nlohmann::json &args,
                          nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("uri")) {
    DCOUT("uri param not found");
    err = "`uri` param not found.";
    return false;
  }

  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
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

bool LoadUSDLayerFromData(Context &ctx, const nlohmann::json &args,
                          nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("data")) {
    DCOUT("data param not found");
    err = "`data` param not found.";
    return false;
  }
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
    return false;
  }

  std::string name = args["name"];
  const std::string &data = args["data"];
  std::string description = args["description"];

  std::string binary = decode_data(data);

  Layer layer;
  std::string warn;
  USDLoadOptions options;
  if (!LoadLayerFromMemory(reinterpret_cast<const uint8_t *>(binary.c_str()),
                           binary.size(), name, &layer, &warn, &err, options)) {
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
  usd_layer.uri = name;  // FIXME
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

bool ReadAsset(Context &ctx, const nlohmann::json &args, nlohmann::json &result,
               std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
    return false;
  }

  std::string name = args["name"];
  int instance_id = -1;
  if (args.contains("instance_id")) {
    instance_id = args["instance_id"];
  }

  AssetSelection asset_selection;
  bool found_selection = false;

  // simple linear search
  for (const auto &selection : ctx.selected_assets) {
    if (selection.asset_name == name && (instance_id == -1 || selection.instance_id == instance_id)) {

      asset_selection = selection;
      found_selection = true;

      DCOUT("Found matching asset selection: " << selection.asset_name);
      break;
    }
  } 

  if (!found_selection) {
    err = "Asset selection not found for name: " + name;
    return false;
  }

  if (!ctx.assets.count(asset_selection.asset_name)) {
    err = "Asset not found: " + name;
    return false;
  }

  const MCPAsset &asset = ctx.assets.at(asset_selection.asset_name);

  // Create JSON response with asset data and transform information
  nlohmann::json asset_data;
  asset_data["name"] = asset.name;
  asset_data["data"] = asset.data;
  asset_data["description"] = asset.description;
  asset_data["uuid"] = asset.uuid;
  
  // Add instance and transform parameters
  asset_data["instance_id"] = asset_selection.instance_id;
  asset_data["position"] = nlohmann::json::array({asset_selection.position[0], asset_selection.position[1], asset_selection.position[2]});
  asset_data["scale"] = nlohmann::json::array({asset_selection.scale[0], asset_selection.scale[1], asset_selection.scale[2]});
  asset_data["rotation"] = nlohmann::json::array({asset_selection.rotation[0], asset_selection.rotation[1], asset_selection.rotation[2]});

  // Add geometry and bounding box parameters
  asset_data["pivot_position"] = nlohmann::json::array({asset.pivot_position[0], asset.pivot_position[1], asset.pivot_position[2]});
  asset_data["bmin"] = nlohmann::json::array({asset.bmin[0], asset.bmin[1], asset.bmin[2]});
  asset_data["bmax"] = nlohmann::json::array({asset.bmax[0], asset.bmax[1], asset.bmax[2]});

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = asset_data.dump();

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool StoreAsset(Context &ctx, const nlohmann::json &args,
                nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("data")) {
    DCOUT("data param not found");
    err = "`data` param not found.";
    return false;
  }
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
    return false;
  }

  std::string name = args["name"];
  const std::string &data = args["data"];
  std::string description = args["description"];

  std::string uuid = generateUUID();

  MCPAsset asset;
  asset.name = name;
  asset.data = data;
  asset.description = description;
  asset.uuid = uuid;

  // Handle preview image if provided
  if (args.contains("preview") && args["preview"].is_object()) {
    const auto &preview = args["preview"];

    // Check for required preview fields
    if (preview.contains("data") && preview.contains("mimeType")) {
      std::cout << "has_preview\n";
      asset.preview.data = preview["data"];
      asset.preview.mimeType = preview["mimeType"];

      // Optional preview name
      if (preview.contains("name")) {
        asset.preview.name = preview["name"];
      }
    }
  }

  // Handle geometry and bounding box parameters if provided
  if (args.contains("pivot_position") && args["pivot_position"].is_array() && args["pivot_position"].size() == 3) {
    asset.pivot_position[0] = args["pivot_position"][0];
    asset.pivot_position[1] = args["pivot_position"][1];
    asset.pivot_position[2] = args["pivot_position"][2];
  }

  if (args.contains("bmin") && args["bmin"].is_array() && args["bmin"].size() == 3) {
    asset.bmin[0] = args["bmin"][0];
    asset.bmin[1] = args["bmin"][1];
    asset.bmin[2] = args["bmin"][2];
  }

  if (args.contains("bmax") && args["bmax"].is_array() && args["bmax"].size() == 3) {
    asset.bmax[0] = args["bmax"][0];
    asset.bmax[1] = args["bmax"][1];
    asset.bmax[2] = args["bmax"][2];
  }

  ctx.assets.emplace(name, std::move(asset));

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = uuid;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool ListPrimSpecs(Context &ctx, const nlohmann::json &args,
                   nlohmann::json &result, std::string &err) {
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

bool ListSublayers(Context &ctx, const nlohmann::json &args,
                   nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];

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
  const auto &sublayers = usd_layer.layer.metas().subLayers;

  result["content"] = nlohmann::json::array();

  // Create JSON array of sublayers
  nlohmann::json sublayer_array;
  sublayer_array = nlohmann::json::array();

  for (const auto &sublayer : sublayers) {
    nlohmann::json item;
    item["assetPath"] = sublayer.assetPath.GetAssetPath();
    item["layerOffset"] = nlohmann::json::object();
    item["layerOffset"]["offset"] = sublayer.layerOffset._offset;
    item["layerOffset"]["scale"] = sublayer.layerOffset._scale;
    sublayer_array.push_back(item);
  }

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = sublayer_array.dump();
  result["content"].push_back(content);

  return true;
}

static std::string ListEditQualToString(ListEditQual qual) {
  switch (qual) {
    case ListEditQual::ResetToExplicit:
      return "resetToExplicit";
    case ListEditQual::Append:
      return "append";
    case ListEditQual::Add:
      return "add";
    case ListEditQual::Delete:
      return "delete";
    case ListEditQual::Prepend:
      return "prepend";
    case ListEditQual::Order:
      return "order";
    default:
      return "invalid";
  }
}

bool ListReferences(Context &ctx, const nlohmann::json &args,
                    nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_name = args["primName"];

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

  // Get primspecs to find references
  const auto &primspecs = usd_layer.layer.primspecs();

  nlohmann::json references_array = nlohmann::json::array();

  for (const auto &ps_pair : primspecs) {
    const auto &prim_name_iter = ps_pair.first;
    const auto &prim_spec = ps_pair.second;

    // If primName filter is specified, only process matching prims
    if (!prim_name.empty() && prim_name_iter != prim_name) {
      continue;
    }

    // Access references from PrimMeta
    const auto &metas = prim_spec.metas();
    if (metas.references.has_value()) {
      const auto &refs_pair = metas.references.value();
      const auto &listop = refs_pair.first;
      const auto &refs = refs_pair.second;

      for (const auto &ref : refs) {
        nlohmann::json item;
        item["primName"] = prim_name_iter;
        item["assetPath"] = ref.asset_path.GetAssetPath();
        item["primPath"] = ref.prim_path.prim_part();
        item["listOp"] = ListEditQualToString(listop);
        item["layerOffset"] = nlohmann::json::object();
        item["layerOffset"]["offset"] = ref.layerOffset._offset;
        item["layerOffset"]["scale"] = ref.layerOffset._scale;
        references_array.push_back(item);
      }
    }
  }

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = references_array.dump();
  result["content"].push_back(content);

  return true;
}

bool ListPayloads(Context &ctx, const nlohmann::json &args,
                  nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_name = args["primName"];

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

  // Get primspecs to find payloads
  const auto &primspecs = usd_layer.layer.primspecs();

  nlohmann::json payloads_array = nlohmann::json::array();

  for (const auto &ps_pair : primspecs) {
    const auto &prim_name_iter = ps_pair.first;
    const auto &prim_spec = ps_pair.second;

    // If primName filter is specified, only process matching prims
    if (!prim_name.empty() && prim_name_iter != prim_name) {
      continue;
    }

    // Access payloads from PrimMeta
    const auto &metas = prim_spec.metas();
    if (metas.payload.has_value()) {
      const auto &payload_pair = metas.payload.value();
      const auto &listop = payload_pair.first;
      const auto &payloads = payload_pair.second;

      for (const auto &payload : payloads) {
        nlohmann::json item;
        item["primName"] = prim_name_iter;
        item["assetPath"] = payload.asset_path.GetAssetPath();
        item["primPath"] = payload.prim_path.prim_part();
        item["listOp"] = ListEditQualToString(listop);
        item["layerOffset"] = nlohmann::json::object();
        item["layerOffset"]["offset"] = payload.layerOffset._offset;
        item["layerOffset"]["scale"] = payload.layerOffset._scale;
        payloads_array.push_back(item);
      }
    }
  }

  nlohmann::json content;
  content["type"] = "text";
  content["text"] = payloads_array.dump();
  result["content"].push_back(content);

  return true;
}

bool AddPrim(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string data = args["data"];
  std::string format = args["format"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
    return false;
  }

  if (data.empty()) {
    err = "`data` argument required\n";
    return false;
  }

  if (format.empty() || (format != "usda" && format != "usdc" && format != "usdj")) {
    err = "`format` must be one of: usda, usdc, usdj\n";
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

  USDLayer &usd_layer = ctx.layers.at(uuid);
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  // Parse prim data based on format
  Layer temp_layer;
  bool parse_success = false;

  if (format == "usda") {
    // Parse USDA (ASCII) format - data is escaped string
    tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t *>(data.data()),
                              static_cast<uint64_t>(data.size()), false);
    tinyusdz::usda::USDAReader usda_reader(&sr);

    tinyusdz::usda::USDAReaderConfig config;
    usda_reader.set_reader_config(config);

    if (usda_reader.Read()) {
      parse_success = usda_reader.get_as_layer(&temp_layer);
      if (!parse_success) {
        err = "Failed to extract layer from USDA data";
      }
    } else {
      err = usda_reader.GetError();
      if (err.empty()) {
        err = "USDA parse error";
      }
    }
  } else if (format == "usdc") {
    // Parse USDC (binary Crate) format - data is base64 encoded
    std::string decoded_data = decode_data(data);
    tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t *>(decoded_data.data()),
                              static_cast<uint64_t>(decoded_data.size()), false);

    tinyusdz::usdc::USDCReaderConfig config;
    config.kMaxAllowedMemoryInMB = 512;  // 512MB limit
    tinyusdz::usdc::USDCReader usdc_reader(&sr, config);

    if (usdc_reader.ReadUSDC()) {
      parse_success = usdc_reader.get_as_layer(&temp_layer);
      if (!parse_success) {
        err = "Failed to extract layer from USDC data";
      }
    } else {
      err = usdc_reader.GetError();
      if (err.empty()) {
        err = "USDC parse error";
      }
    }
  } else if (format == "usdj") {
    // Parse USDJ (JSON) format - data is JSON string
    // For now, return an error as JSON prim parsing is not yet implemented
    err = "USDJ format is not yet supported in this tool";
    return false;
  }

  if (!parse_success) {
    if (err.empty()) {
      err = "Failed to parse prim data in " + format + " format";
    }
    return false;
  }

  // Get the root prim(s) from temp_layer
  if (temp_layer.primspecs().empty()) {
    err = "No prims found in input data";
    return false;
  }

  // Extract first (or only) prim from temp layer
  auto &temp_primspecs = temp_layer.primspecs();
  PrimSpec prim_to_add = temp_primspecs.begin()->second;

  // Determine if prim_path is a root prim or nested
  std::string element_name = prim_path.element_name();
  if (element_name.empty()) {
    err = "Invalid primPath - cannot extract element name";
    return false;
  }

  // For now, only support adding at root level
  if (!prim_path.is_root_prim()) {
    // Check if it's a root level path (starts with /)
    std::string prim_part = prim_path.prim_part();
    if (prim_part.empty() || prim_part == "/") {
      err = "Invalid primPath format";
      return false;
    }

    // For nested paths, split and add hierarchy
    // This is a simplified implementation - only handles one level of nesting for now
    std::vector<std::string> path_parts;
    std::string current = prim_part;
    if (current[0] == '/') {
      current = current.substr(1);
    }

    size_t pos = 0;
    while ((pos = current.find('/')) != std::string::npos) {
      std::string part = current.substr(0, pos);
      if (!part.empty()) {
        path_parts.push_back(part);
      }
      current = current.substr(pos + 1);
    }
    if (!current.empty()) {
      path_parts.push_back(current);
    }

    if (path_parts.empty()) {
      err = "Invalid primPath";
      return false;
    }

    // Get or create parent prim
    std::string root_name = path_parts[0];
    auto &layer_primspecs = usd_layer.layer.primspecs();

    PrimSpec *parent = nullptr;
    if (layer_primspecs.count(root_name)) {
      parent = &layer_primspecs[root_name];
    } else {
      // Create root prim
      PrimSpec root_prim(Specifier::Def, "Scope", root_name);
      layer_primspecs[root_name] = root_prim;
      parent = &layer_primspecs[root_name];
    }

    // Add to parent's children (simplified - only one level deep for now)
    if (path_parts.size() > 1) {
      std::string child_name = path_parts[path_parts.size() - 1];
      prim_to_add.set_name(child_name);
      parent->children().push_back(std::move(prim_to_add));
    } else {
      // Add at root level
      prim_to_add.set_name(root_name);
      usd_layer.layer.add_primspec(root_name, std::move(prim_to_add));
    }
  } else {
    // Add at root level
    prim_to_add.set_name(element_name);
    usd_layer.layer.add_primspec(element_name, std::move(prim_to_add));
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully added prim at path: " + prim_path_str;
  result["content"].push_back(response);

  return true;
}

bool ListScreenshots(Context &ctx, const nlohmann::json &args,
                     nlohmann::json &result, std::string &err) {
  (void)args;
  (void)err;

  result["content"] = nlohmann::json::array();

  for (const auto &it : ctx.screenshots) {
    nlohmann::json content;
    content["type"] = "text";
    content["text"] = it.first;  // name
    result["content"].push_back(content);
  }

  return true;
}

bool SaveScreenshot(Context &ctx, const nlohmann::json &args,
                    nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
    return false;
  }
  if (!args.contains("data")) {
    DCOUT("data param not found");
    err = "`data` param not found.";
    return false;
  }
  if (!args.contains("mimeType")) {
    DCOUT("mimeType param not found");
    err = "`mimeType` param not found.";
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

bool ReadAssetPreview(Context &ctx, const nlohmann::json &args,
                      nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
    return false;
  }

  std::string name = args["name"];

  // Assets are stored by name
  if (!ctx.assets.count(name)) {
    err = "Asset not found: " + name;
    return false;
  }

  const auto &asset = ctx.assets.at(name);

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

bool ReadScreenshot(Context &ctx, const nlohmann::json &args,
                    nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
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
  content["data"] = screenshot.data;  // base64-encoded-data
  content["mimeType"] = screenshot.mimeType;

  // optional
  content["annotations"] = nlohmann::json::object();
  content["annotations"]["audience"] = nlohmann::json::array();
  content["annotations"]["audience"].push_back("user");
  content["annotations"]["priority"] = 0.9;

  result["content"].push_back(content);

  return true;
}

bool GetUSDDescription(Context &ctx, const nlohmann::json &args,
                       nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
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

bool GetAllUSDDescriptions(Context &ctx, const nlohmann::json &args,
                           nlohmann::json &result, std::string &err) {
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

bool GetAssetDescription(Context &ctx, const nlohmann::json &args,
                         nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("name")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
    return false;
  }

  std::string name = args.at("name");

  bool include_preview = false;
  if (args.contains("include_preview")) {
    include_preview = args["include_preview"];
  }

  // Assets are stored by name, not UUID
  if (!ctx.assets.count(name)) {
    err = "Asset not found: " + name;
    return false;
  }

  const auto &asset = ctx.assets.at(name);

  // Create structured JSON for the asset
  nlohmann::json asset_info;
  asset_info["name"] = name;
  asset_info["asset_name"] = asset.name;
  asset_info["description"] = asset.description;
  asset_info["uuid"] = asset.uuid;
  
  // Add geometry and bounding box parameters
  asset_info["pivot_position"] = nlohmann::json::array({asset.pivot_position[0], asset.pivot_position[1], asset.pivot_position[2]});
  asset_info["bmin"] = nlohmann::json::array({asset.bmin[0], asset.bmin[1], asset.bmin[2]});
  asset_info["bmax"] = nlohmann::json::array({asset.bmax[0], asset.bmax[1], asset.bmax[2]});

  // Add preview data if available and requested
  if (include_preview && !asset.preview.data.empty()) {
    asset_info["preview"] = nlohmann::json::object();
    asset_info["preview"]["data"] = asset.preview.data;
    asset_info["preview"]["mimeType"] = asset.preview.mimeType;
    if (!asset.preview.name.empty()) {
      asset_info["preview"]["name"] = asset.preview.name;
    }
  }

  // Return as JSON string
  nlohmann::json content;
  content["type"] = "text";
  content["text"] = asset_info.dump();

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool GetAllAssetDescriptions(Context &ctx, const nlohmann::json &args,
                             nlohmann::json &result, std::string &err) {
  (void)args;
  (void)err;

  bool include_preview = false;
  if (args.contains("include_preview")) {
    include_preview = args["include_preview"];
  }

  result["content"] = nlohmann::json::array();

  for (const auto &it : ctx.assets) {
    // Create structured JSON for each asset
    nlohmann::json asset_info;
    asset_info["name"] = it.first;
    asset_info["asset_name"] = it.second.name;
    asset_info["description"] = it.second.description;
    asset_info["uuid"] = it.second.uuid;
    
    // Add geometry and bounding box parameters
    asset_info["pivot_position"] = nlohmann::json::array({it.second.pivot_position[0], it.second.pivot_position[1], it.second.pivot_position[2]});
    asset_info["bmin"] = nlohmann::json::array({it.second.bmin[0], it.second.bmin[1], it.second.bmin[2]});
    asset_info["bmax"] = nlohmann::json::array({it.second.bmax[0], it.second.bmax[1], it.second.bmax[2]});

    if (include_preview) {
      // Add preview data if available
      if (!it.second.preview.data.empty()) {
        asset_info["preview"] = nlohmann::json::object();
        asset_info["preview"]["data"] = it.second.preview.data;
        asset_info["preview"]["mimeType"] = it.second.preview.mimeType;
        if (!it.second.preview.name.empty()) {
          asset_info["preview"]["name"] = it.second.preview.name;
        }
      }
    }

    // Return as JSON string
    nlohmann::json content;
    content["type"] = "text";
    content["text"] = asset_info.dump();

    result["content"].push_back(content);
  }

  return true;
}

bool ToUSDA(Context &ctx, const nlohmann::json &args, nlohmann::json &result,
            std::string &err) {
  DCOUT("args " << args);
  if (!args.contains("uri")) {
    DCOUT("name param not found");
    err = "`name` param not found.";
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
  std::string str = to_string(layer);  // to USDA
  content["text"] = str;

  result["content"] = nlohmann::json::array();
  result["content"].push_back(content);

  return true;
}

bool SelectAssets(Context &ctx, const nlohmann::json &args,
                  nlohmann::json &result, std::string &err) {
  //std::cout << "select_assets"  << args << "\n";
  if (!args.contains("assets")) {
    DCOUT("assets param not found");
    err = "`assets` param not found.";
    return false;
  }

  if (!args["assets"].is_array()) {
    err = "`assets` must be an array.";
    return false;
  }

  const auto &assets = args["assets"];

  ctx.selected_assets.clear();
  
  for (const auto &asset_obj : assets) {
    if (!asset_obj.is_object()) {
      err = "Each asset must be an object.";
      return false;
    }
    
    if (!asset_obj.contains("name")) {
      err = "Asset object must contain 'name' field.";
      return false;
    }
    
    std::string name = asset_obj["name"];
    
    if (!ctx.assets.count(name)) {
      err = "Asset not found" + name;
      return false;
    }
    
    // Default instance and transform parameters
    int instance_id = 0;
    std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    std::array<float, 3> rotation = {0.0f, 0.0f, 0.0f};
    
    // Default geometry and bounding box parameters
    std::array<float, 3> pivot_position = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> bmin = {-1.0f, -1.0f, -1.0f};
    std::array<float, 3> bmax = {1.0f, 1.0f, 1.0f};
    
    // Parse instance_id
    if (asset_obj.contains("instance_id") && asset_obj["instance_id"].is_number_integer()) {
      instance_id = asset_obj["instance_id"];
    }
    
    // Parse individual transform parameters for this asset
    if (asset_obj.contains("position") && asset_obj["position"].is_array() && asset_obj["position"].size() == 3) {
      position[0] = asset_obj["position"][0];
      position[1] = asset_obj["position"][1];
      position[2] = asset_obj["position"][2];
    }
    
    if (asset_obj.contains("scale") && asset_obj["scale"].is_array() && asset_obj["scale"].size() == 3) {
      scale[0] = asset_obj["scale"][0];
      scale[1] = asset_obj["scale"][1];
      scale[2] = asset_obj["scale"][2];
    }
    
    if (asset_obj.contains("rotation") && asset_obj["rotation"].is_array() && asset_obj["rotation"].size() == 3) {
      rotation[0] = asset_obj["rotation"][0];
      rotation[1] = asset_obj["rotation"][1];
      rotation[2] = asset_obj["rotation"][2];
    }
    
    // Parse geometry and bounding box parameters
    if (asset_obj.contains("pivot_position") && asset_obj["pivot_position"].is_array() && asset_obj["pivot_position"].size() == 3) {
      pivot_position[0] = asset_obj["pivot_position"][0];
      pivot_position[1] = asset_obj["pivot_position"][1];
      pivot_position[2] = asset_obj["pivot_position"][2];
    }
    
    if (asset_obj.contains("bmin") && asset_obj["bmin"].is_array() && asset_obj["bmin"].size() == 3) {
      bmin[0] = asset_obj["bmin"][0];
      bmin[1] = asset_obj["bmin"][1];
      bmin[2] = asset_obj["bmin"][2];
    }
    
    if (asset_obj.contains("bmax") && asset_obj["bmax"].is_array() && asset_obj["bmax"].size() == 3) {
      bmax[0] = asset_obj["bmax"][0];
      bmax[1] = asset_obj["bmax"][1];
      bmax[2] = asset_obj["bmax"][2];
    }
    
    // Update the asset with its individual transform parameters
    AssetSelection selection;
    selection.asset_name = name;
    selection.instance_id = instance_id;
    selection.position = position;
    selection.scale = scale;
    selection.rotation = rotation;
    //selection.pivot_position = pivot_position;
    //selection.bmin = bmin;
    //selection.bmax = bmax;
    ctx.selected_assets.push_back(selection);
    
    std::cout << "Selected asset '" << name << "' (instance_id: " << instance_id << ") with transform - Position: [" 
          << position[0] << ", " << position[1] << ", " << position[2] << "], "
          << "Scale: [" << scale[0] << ", " << scale[1] << ", " << scale[2] << "], "
          << "Rotation: [" << rotation[0] << ", " << rotation[1] << ", " << rotation[2] << "], "
          << "Pivot: [" << pivot_position[0] << ", " << pivot_position[1] << ", " << pivot_position[2] << "], "
          << "BMin: [" << bmin[0] << ", " << bmin[1] << ", " << bmin[2] << "], "
          << "BMax: [" << bmax[0] << ", " << bmax[1] << ", " << bmax[2] << "]";
  }

  result["content"] = nlohmann::json::array();

  return true;
}

bool GetSelectedAssets(Context &ctx, const nlohmann::json &args,
                       nlohmann::json &result, std::string &err) {
  (void)err;
  (void)args;
  DCOUT("args " << args);

  result["content"] = nlohmann::json::array();
  for (const auto &selection : ctx.selected_assets) {
    // Create JSON object with name and instance_id
    nlohmann::json asset_info;
    asset_info["name"] = selection.asset_name;
    asset_info["instance_id"] = selection.instance_id;
    
    nlohmann::json content;
    content["type"] = "text";
    content["text"] = asset_info.dump();
    result["content"].push_back(content);
  }

  return true;
}

}  // namespace

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
    // schena["required"] = nlohmann::json::array();

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
    // schena["required"] = nlohmann::json::array();

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
    schema["properties"]["name"] = {
        {"type", "string"}};  // TODO: accept multiple names

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_all_asset_descriptions";
    j["description"] =
        "Get description of all Assets(The response is JSON string). Optionally include preview image data by setting `include_preview` argument.";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["include_preview"] = {
        {"type", "boolean"}}; 
    // schena["required"] = nlohmann::json::array();

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_asset_description";
    j["description"] = "Get description of Asset(The response is JSON string). Optionally include preview image data by setting `include_preview` argument.";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] = {
        {"type", "string"}};  // TODO: accept multiple names
    schema["properties"]["include_preview"] = {
        {"type", "boolean"}};  // include preview image in the response?

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "load_usd_layer_from_file";
    j["description"] =
        "Load USD as Layer from a file(only works in C++ native binary)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uri"] = {{"type", "string"}};
    schema["properties"]["name"] = {{"type", "string"}};
    schema["properties"]["description"] = {{"type", "string"}};  // optional

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
    schema["properties"]["data"] = {{"type", "string"}};
    schema["properties"]["name"] = {{"type", "string"}};
    schema["properties"]["description"] = {{"type", "string"}};  // optional

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
    schema["properties"]["name"] = {{"type", "string"}};

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "read_asset";
    j["description"] = "Read asset as JSON string containing data, instance_id, transform parameters (position, scale, rotation), and metadata";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] = {{"type", "string"}};
    schema["properties"]["instance_id"] = {{"type", "integer"}, {"description", "Optional instance ID to match against the asset's instance_id"}};

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "store_asset";
    j["description"] =
        "Store asset(e.g. USD, texture) with optional preview image and geometry parameters (pivot_position, bmin, bmax). `data` is "
        "base64 encoded string.";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["data"] = {{"type", "string"}};
    schema["properties"]["name"] = {{"type", "string"}};
    schema["properties"]["description"] = {{"type", "string"}};  // optional

    // Add preview object schema
    nlohmann::json previewSchema;
    previewSchema["type"] = "object";
    previewSchema["properties"] = nlohmann::json::object();
    previewSchema["properties"]["data"] = {
        {"type", "string"}, {"description", "Base64 encoded preview image"}};
    previewSchema["properties"]["mimeType"] = {
        {"type", "string"},
        {"description", "MIME type (e.g. 'image/png', 'image/jpeg')"}};
    previewSchema["properties"]["name"] = {
        {"type", "string"},
        {"description", "Optional name for the preview image"}};
    previewSchema["required"] = nlohmann::json::array({"data", "mimeType"});

    schema["properties"]["preview"] = previewSchema;  // optional

    // Add geometry and bounding box parameters
    schema["properties"]["pivot_position"] = {{"type", "array"},
                                              {"items", {"type", "number"}},
                                              {"minItems", 3},
                                              {"maxItems", 3},
                                              {"description", "Pivot position as [x, y, z] for rotation and scaling"}};
    schema["properties"]["bmin"] = {{"type", "array"},
                                    {"items", {"type", "number"}},
                                    {"minItems", 3},
                                    {"maxItems", 3},
                                    {"description", "Bounding box minimum as [x, y, z]"}};
    schema["properties"]["bmax"] = {{"type", "array"},
                                    {"items", {"type", "number"}},
                                    {"minItems", 3},
                                    {"maxItems", 3},
                                    {"description", "Bounding box maximum as [x, y, z]"}};

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
    schema["properties"]["uuid"] = {{"type", "string"}};
    schema["properties"]["name"] = {{"type", "string"}};

    // schema["required"] = nlohmann::json::array({"uuid"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "list_sublayers";
    j["description"] = "List SubLayer USD paths in loaded USD Layer. Each item contains assetPath and layerOffsets (offset and scale)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}};
    schema["properties"]["name"] = {{"type", "string"}};

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "list_references";
    j["description"] = "List References in loaded USD Layer. Each item contains assetPath, primPath, listOp, layerOffset, and the prim that contains the reference (primName)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}};
    schema["properties"]["name"] = {{"type", "string"}};
    schema["properties"]["primName"] = {{"type", "string"}, {"description", "Optional: filter references by prim name"}};

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "list_payloads";
    j["description"] = "List Payloads in loaded USD Layer. Each item contains assetPath, primPath, listOp, layerOffset, and the prim that contains the payload (primName)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}};
    schema["properties"]["name"] = {{"type", "string"}};
    schema["properties"]["primName"] = {{"type", "string"}, {"description", "Optional: filter payloads by prim name"}};

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "add_prim";
    j["description"] = "Add PrimSpec to specified Layer as child PrimSpec at specified primPath. Input prim data can be USDA (escaped string), USDC (base64 binary), or USDJ (JSON)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path where to add the prim (e.g., /MyPrim or /Parent/Child)"}};
    schema["properties"]["data"] = {{"type", "string"}, {"description", "Prim data: USDA as escaped string, USDC as base64, USDJ as JSON string"}};
    schema["properties"]["format"] = {{"type", "string"}, {"description", "Format of input data: usda, usdc, or usdj"}};

    schema["required"] = nlohmann::json::array({"primPath", "data", "format"});

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
    schema["properties"]["name"] = {{"type", "string"}};

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "save_screenshot";
    j["description"] =
        "Save screenshot image(`data` is a base64 encoded string of image "
        "data)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["data"] = {{"type", "string"}};
    schema["properties"]["name"] = {{"type", "string"}};
    schema["properties"]["mimeType"] = {{"type", "string"}};

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
    schema["properties"]["name"] = {{"type", "string"}};

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "select_assets";
    j["description"] = "Select assets with individual transform parameters. Specify array of asset objects with name and optional transform parameters.";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    
    // Array of asset objects
    nlohmann::json assetSchema;
    assetSchema["type"] = "object";
    assetSchema["properties"] = nlohmann::json::object();
    assetSchema["properties"]["name"] = {{"type", "string"}, {"description", "Asset name"}};
    assetSchema["properties"]["instance_id"] = {{"type", "integer"}, {"description", "Instance ID for the asset"}};
    assetSchema["properties"]["position"] = {{"type", "array"},
                                             {"items", {"type", "number"}},
                                             {"minItems", 3},
                                             {"maxItems", 3},
                                             {"description", "Position as [x, y, z]"}};
    assetSchema["properties"]["scale"] = {{"type", "array"},
                                          {"items", {"type", "number"}},
                                          {"minItems", 3},
                                          {"maxItems", 3},
                                          {"description", "Scale as [x, y, z]"}};
    assetSchema["properties"]["rotation"] = {{"type", "array"},
                                             {"items", {"type", "number"}},
                                             {"minItems", 3},
                                             {"maxItems", 3},
                                             {"description", "Rotation as [x, y, z] in degrees"}};
    assetSchema["properties"]["pivot_position"] = {{"type", "array"},
                                                   {"items", {"type", "number"}},
                                                   {"minItems", 3},
                                                   {"maxItems", 3},
                                                   {"description", "Pivot position as [x, y, z] for rotation and scaling"}};
    assetSchema["properties"]["bmin"] = {{"type", "array"},
                                         {"items", {"type", "number"}},
                                         {"minItems", 3},
                                         {"maxItems", 3},
                                         {"description", "Bounding box minimum as [x, y, z]"}};
    assetSchema["properties"]["bmax"] = {{"type", "array"},
                                         {"items", {"type", "number"}},
                                         {"minItems", 3},
                                         {"maxItems", 3},
                                         {"description", "Bounding box maximum as [x, y, z]"}};
    assetSchema["required"] = nlohmann::json::array({"name"});

    schema["properties"]["assets"] = {{"type", "array"}, {"items", assetSchema}};
    schema["required"] = nlohmann::json::array({"assets"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_selected_assets";
    j["description"] = "Get selected assets as JSON strings containing name and instance_id";

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
    schema["properties"]["name"] = {{"type", "string"},
                                    {"description", "Asset name"}};

    schema["required"] = nlohmann::json::array({"name"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  std::cout << result << "\n";

  return true;
}

bool CallTool(Context &ctx, const std::string &tool_name,
              const nlohmann::json &args, nlohmann::json &result,
              std::string &err) {
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
  } else if (tool_name == "list_sublayers") {
    DCOUT("list_sublayers");
    return ListSublayers(ctx, args, result, err);
  } else if (tool_name == "list_references") {
    DCOUT("list_references");
    return ListReferences(ctx, args, result, err);
  } else if (tool_name == "list_payloads") {
    DCOUT("list_payloads");
    return ListPayloads(ctx, args, result, err);
  } else if (tool_name == "add_prim") {
    DCOUT("add_prim");
    return AddPrim(ctx, args, result, err);
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

}  // namespace mcp
}  // namespace tydra
}  // namespace tinyusdz
