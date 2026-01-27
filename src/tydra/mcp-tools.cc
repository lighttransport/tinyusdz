#include "mcp-tools.hh"

#include <algorithm>
#include <string>
#include <sstream>

#include "mcp-context.hh"
#include "mcp-server.hh"
#include "pprinter.hh"
#include "layer.hh"
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
bool DelPrim(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err);
bool GetPrim(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err);
bool AddProp(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err);
bool DelProp(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err);
bool GetProp(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err);
bool NewLayer(Context &ctx, const nlohmann::json &args,
              nlohmann::json &result, std::string &err);
bool GetLayerMeta(Context &ctx, const nlohmann::json &args,
                  nlohmann::json &result, std::string &err);
bool AddLayerMeta(Context &ctx, const nlohmann::json &args,
                  nlohmann::json &result, std::string &err);
bool DelLayerMeta(Context &ctx, const nlohmann::json &args,
                  nlohmann::json &result, std::string &err);
bool GetPrimMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err);
bool AddPrimMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err);
bool DelPrimMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err);
bool GetAttrMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err);
bool AddAttrMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err);
bool DelAttrMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err);

bool NewLayer(Context &ctx, const nlohmann::json &args,
              nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string layer_name = args["name"];
  std::string layer_meta_json = args["layerMeta"];
  std::string prim_specs_json = args["primSpecs"];

  if (layer_name.empty()) {
    err = "`name` argument required\n";
    return false;
  }

  // Check if layer already exists
  for (const auto &it : ctx.layers) {
    if (it.second.name == layer_name) {
      err = "Layer with name '" + layer_name + "' already exists\n";
      return false;
    }
  }

  // Create new layer
  Layer new_layer;
  std::string uuid = generateUUID();

  // Parse and apply layer metadata if provided (exception-free)
  if (!layer_meta_json.empty()) {
    // Assume layerMeta is already a valid JSON object string
    // Direct JSON parsing without exception handling
    nlohmann::json meta_obj = nlohmann::json::parse(layer_meta_json, nullptr, false);

    if (!meta_obj.is_discarded()) {
      // Set doc if provided
      if (meta_obj.contains("doc") && meta_obj["doc"].is_string()) {
        new_layer.metas().doc = tinyusdz::value::StringData(meta_obj["doc"].get<std::string>());
      }

      // Set comment if provided
      if (meta_obj.contains("comment") && meta_obj["comment"].is_string()) {
        new_layer.metas().comment = tinyusdz::value::StringData(meta_obj["comment"].get<std::string>());
      }
    }
  }

  // Parse and add primspecs if provided (exception-free)
  if (!prim_specs_json.empty()) {
    // Parse JSON without exception handling
    nlohmann::json prim_specs_obj = nlohmann::json::parse(prim_specs_json, nullptr, false);

    if (!prim_specs_obj.is_discarded()) {
      // Handle both object (single prim) and array (multiple prims)
      nlohmann::json prims_array;
      if (prim_specs_obj.is_object() && !prim_specs_obj.is_array()) {
        // Single prim object - wrap in array for uniform handling
        prims_array = nlohmann::json::array();
        prims_array.push_back(prim_specs_obj);
      } else if (prim_specs_obj.is_array()) {
        prims_array = prim_specs_obj;
      } else {
        err = "primSpecs must be a JSON object or array\n";
        return false;
      }

      // Process each prim specification
      for (const auto &prim_json : prims_array) {
        if (!prim_json.contains("name") || !prim_json["name"].is_string()) {
          err = "Each prim must have a 'name' field\n";
          return false;
        }

        std::string prim_name = prim_json["name"].get<std::string>();

        // Create PrimSpec
        PrimSpec prim_spec;
        prim_spec.set_name(prim_name);

        // Set type name if provided
        if (prim_json.contains("typeName") && prim_json["typeName"].is_string()) {
          prim_spec.typeName() = prim_json["typeName"].get<std::string>();
        }

        // Set specifier if provided (def, over, class)
        if (prim_json.contains("specifier") && prim_json["specifier"].is_string()) {
          std::string specifier_str = prim_json["specifier"].get<std::string>();
          if (specifier_str == "def") {
            prim_spec.specifier() = Specifier::Def;
          } else if (specifier_str == "over") {
            prim_spec.specifier() = Specifier::Over;
          } else if (specifier_str == "class") {
            prim_spec.specifier() = Specifier::Class;
          }
        }

        // Add prim metadata if provided
        if (prim_json.contains("metadata") && prim_json["metadata"].is_object()) {
          auto meta_obj = prim_json["metadata"];

          if (meta_obj.contains("doc") && meta_obj["doc"].is_string()) {
            prim_spec.metas().set_doc(meta_obj["doc"].get<std::string>());
          }
          if (meta_obj.contains("comment") && meta_obj["comment"].is_string()) {
            prim_spec.metas().set_comment(meta_obj["comment"].get<std::string>());
          }
        }

        // Add prim to layer (only root-level prims)
        new_layer.primspecs()[prim_name] = prim_spec;
      }
    }
  }

  // Store the layer
  USDLayer usd_layer;
  usd_layer.name = layer_name;
  usd_layer.uri = layer_name;
  usd_layer.layer = new_layer;

  ctx.layers[uuid] = usd_layer;

  // Return success response with UUID
  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully created new layer '" + layer_name + "' with UUID: " + uuid;
  result["content"].push_back(response);

  return true;
}

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
      const auto &refs_pairs = metas.references.value();
      for (const auto &refs_pair : refs_pairs) {
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
      const auto &payload_pairs = metas.payload.value();
      for (const auto &payload_pair : payload_pairs) {
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

bool DelPrim(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
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

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Cannot delete root prim";
    return false;
  }

  // Parse path to get prim name(s)
  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  // For root level prims, delete directly
  size_t slash_pos = prim_part.find('/');
  if (slash_pos == std::string::npos) {
    // Root level prim
    auto &primspecs = usd_layer.layer.primspecs();
    if (primspecs.count(prim_part)) {
      primspecs.erase(prim_part);
      result["content"] = nlohmann::json::array();
      nlohmann::json response;
      response["type"] = "text";
      response["text"] = "Successfully deleted prim at path: " + prim_path_str;
      result["content"].push_back(response);
      return true;
    } else {
      err = "Prim not found at path: " + prim_path_str;
      return false;
    }
  } else {
    // Nested prim - would need to traverse hierarchy
    // For now, only support root-level deletion
    err = "Nested prim deletion not yet supported. Only root-level prims can be deleted.";
    return false;
  }
}

bool GetPrim(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
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
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  // Parse path to get prim name(s)
  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  // For root level prims, get directly
  size_t slash_pos = prim_part.find('/');
  if (slash_pos == std::string::npos) {
    // Root level prim
    const auto &primspecs = usd_layer.layer.primspecs();
    auto it = primspecs.find(prim_part);
    if (it != primspecs.end()) {
      const PrimSpec &prim_spec = it->second;

      // Convert PrimSpec to JSON
      nlohmann::json prim_json = nlohmann::json::object();

      prim_json["name"] = prim_spec.name();
      prim_json["typeName"] = prim_spec.typeName();
      prim_json["specifier"] = (prim_spec.specifier() == Specifier::Def) ? "def" :
                               (prim_spec.specifier() == Specifier::Over) ? "over" : "class";

      // Add properties
      nlohmann::json props_json = nlohmann::json::object();
      for (const auto &prop_pair : prim_spec.props()) {
        const std::string &prop_name = prop_pair.first;
        const Property &prop = prop_pair.second;

        if (prop.is_attribute()) {
          const Attribute &attr = prop.get_attribute();
          nlohmann::json attr_json = nlohmann::json::object();
          attr_json["type"] = attr.type_name();
          attr_json["variability"] = attr.is_uniform() ? "uniform" : "varying";
          props_json[prop_name] = attr_json;
        }
      }
      if (!props_json.empty()) {
        prim_json["properties"] = props_json;
      }

      // Add metadata
      const auto &metas = prim_spec.metas();
      nlohmann::json metas_json = nlohmann::json::object();

      if (metas.has_doc()) {
        metas_json["doc"] = metas.get_doc().value;
      }
      if (metas.has_comment()) {
        metas_json["comment"] = metas.get_comment().value;
      }
      if (metas.has_hidden()) {
        metas_json["hidden"] = metas.get_hidden();
      }
      if (metas.has_active()) {
        metas_json["active"] = metas.get_active();
      }
      if (metas.has_kind()) {
        // Kind is an enum, convert to string
        Kind k = metas.get_kind_enum();
        std::string kind_str;
        switch (k) {
          case Kind::Model: kind_str = "model"; break;
          case Kind::Group: kind_str = "group"; break;
          case Kind::Assembly: kind_str = "assembly"; break;
          case Kind::Component: kind_str = "component"; break;
          case Kind::Subcomponent: kind_str = "subcomponent"; break;
          case Kind::SceneLibrary: kind_str = "sceneLibrary"; break;
          case Kind::UserDef: kind_str = "userDef"; break;
          case Kind::Invalid: kind_str = "invalid"; break;
        }
        metas_json["kind"] = kind_str;
      }

      if (!metas_json.empty()) {
        prim_json["metadata"] = metas_json;
      }

      // Add composition arcs
      if (metas.references.has_value()) {
        nlohmann::json refs_json = nlohmann::json::array();
        const auto &refs_pairs = metas.references.value();
        for (const auto &refs_pair : refs_pairs) {
          for (const auto &ref : refs_pair.second) {
            nlohmann::json ref_json = nlohmann::json::object();
            ref_json["assetPath"] = ref.asset_path.GetAssetPath();
            ref_json["primPath"] = ref.prim_path.prim_part();
            refs_json.push_back(ref_json);
          }
        }
        prim_json["references"] = refs_json;
      }

      if (metas.payload.has_value()) {
        nlohmann::json payloads_json = nlohmann::json::array();
        const auto &payload_pairs = metas.payload.value();
        for (const auto &payload_pair : payload_pairs) {
          for (const auto &payload : payload_pair.second) {
            nlohmann::json payload_json = nlohmann::json::object();
            payload_json["assetPath"] = payload.asset_path.GetAssetPath();
            payload_json["primPath"] = payload.prim_path.prim_part();
            payloads_json.push_back(payload_json);
          }
        }
        prim_json["payloads"] = payloads_json;
      }

      result["content"] = nlohmann::json::array();
      nlohmann::json content;
      content["type"] = "text";
      content["text"] = prim_json.dump(2);  // Pretty print with 2 space indent
      result["content"].push_back(content);

      return true;
    } else {
      err = "Prim not found at path: " + prim_path_str;
      return false;
    }
  } else {
    // Nested prim - would need to traverse hierarchy
    err = "Nested prim retrieval not yet supported. Only root-level prims can be retrieved.";
    return false;
  }
}

bool AddProp(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string prop_name = args["propertyName"];
  std::string prop_type = args["propertyType"];  // "attribute" or "relationship"
  std::string value = args["value"];
  std::string value_format = args["valueFormat"];  // "json" or "ascii"

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
    return false;
  }

  if (prop_name.empty()) {
    err = "`propertyName` argument required\n";
    return false;
  }

  if (prop_type.empty() || (prop_type != "attribute" && prop_type != "relationship")) {
    err = "`propertyType` must be either 'attribute' or 'relationship'\n";
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

  // Get prim name(s) from path
  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  // Only support root-level prims for now
  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested prim property modification not yet supported. Only root-level prims can be modified.";
    return false;
  }

  // Get or create prim
  auto &primspecs = usd_layer.layer.primspecs();
  if (!primspecs.count(prim_part)) {
    err = "Prim not found at path: " + prim_path_str;
    return false;
  }

  PrimSpec &prim_spec = primspecs[prim_part];

  if (prop_type == "attribute") {
    // Create attribute
    Attribute attr;
    attr.set_name(prop_name);

    // For now, store value as string and infer type
    if (value_format == "json" || value_format.empty()) {
      // Parse JSON value
      // NOTE: Project is built with -fno-exceptions, so avoid exceptions
      // Infer type from value format
      if (value == "true" || value == "false") {
        attr.set_value(value == "true");
        attr.set_type_name("bool");
      } else if (value.find('.') != std::string::npos && !value.empty()) {
        // Likely a float - parse using strtod (exception-free)
        double d = 0.0;
        // Validate numeric format without exceptions
        bool is_numeric = true;
        for (size_t i = 0; i < value.size(); ++i) {
          if (i == 0 && (value[i] == '-' || value[i] == '+')) continue;
          if (value[i] == '.') continue;
          if (value[i] == 'e' || value[i] == 'E') continue;
          if (!std::isdigit(value[i])) {
            is_numeric = false;
            break;
          }
        }
        if (is_numeric) {
          // Use strtod for safer parsing without exceptions
          d = std::strtod(value.c_str(), nullptr);
          attr.set_value(static_cast<double>(d));
          attr.set_type_name("double");
        } else {
          attr.set_value(tinyusdz::value::token(value));
          attr.set_type_name("token");
        }
      } else if (!value.empty() && std::all_of(value.begin(), value.end(), ::isdigit)) {
        // Likely an integer - use strtoll without exceptions
        int64_t i = std::strtoll(value.c_str(), nullptr, 10);
        attr.set_value(static_cast<int64_t>(i));
        attr.set_type_name("int64");
      } else {
        // Default to token
        attr.set_value(tinyusdz::value::token(value));
        attr.set_type_name("token");
      }
    } else if (value_format == "ascii") {
      // For ASCII format, treat as string
      attr.set_value(tinyusdz::value::token(value));
      attr.set_type_name("token");
    }

    // Add attribute to prim
    Property prop(attr);
    prim_spec.props()[prop_name] = prop;
  } else if (prop_type == "relationship") {
    // Create relationship
    Relationship rel;

    // Parse value as path
    Path rel_path(value, "");
    if (rel_path.is_valid()) {
      rel.set(rel_path);
    } else {
      err = "Invalid relationship target path: " + value;
      return false;
    }

    // Add relationship to prim
    Property prop(rel);
    prim_spec.props()[prop_name] = prop;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully added " + prop_type + " property '" + prop_name + "' to prim at path: " + prim_path_str;
  result["content"].push_back(response);

  return true;
}

bool DelProp(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string prop_name = args["propertyName"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
    return false;
  }

  if (prop_name.empty()) {
    err = "`propertyName` argument required\n";
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

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  // Only support root-level prims for now
  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested prim property modification not yet supported. Only root-level prims can be modified.";
    return false;
  }

  auto &primspecs = usd_layer.layer.primspecs();
  if (!primspecs.count(prim_part)) {
    err = "Prim not found at path: " + prim_path_str;
    return false;
  }

  PrimSpec &prim_spec = primspecs[prim_part];
  auto &props = prim_spec.props();

  if (!props.count(prop_name)) {
    err = "Property not found: " + prop_name;
    return false;
  }

  props.erase(prop_name);

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully deleted property '" + prop_name + "' from prim at path: " + prim_path_str;
  result["content"].push_back(response);

  return true;
}

bool GetProp(Context &ctx, const nlohmann::json &args,
             nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string prop_name = args["propertyName"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
    return false;
  }

  if (prop_name.empty()) {
    err = "`propertyName` argument required\n";
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
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  // Only support root-level prims for now
  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested prim property retrieval not yet supported. Only root-level prims can be retrieved.";
    return false;
  }

  const auto &primspecs = usd_layer.layer.primspecs();
  auto it = primspecs.find(prim_part);
  if (it == primspecs.end()) {
    err = "Prim not found at path: " + prim_path_str;
    return false;
  }

  const PrimSpec &prim_spec = it->second;
  const auto &props = prim_spec.props();

  if (!props.count(prop_name)) {
    err = "Property not found: " + prop_name;
    return false;
  }

  const Property &prop = props.at(prop_name);

  nlohmann::json prop_json = nlohmann::json::object();
  prop_json["name"] = prop_name;

  if (prop.is_attribute()) {
    prop_json["type"] = "attribute";
    const Attribute &attr = prop.get_attribute();
    prop_json["dataType"] = attr.type_name();
    prop_json["variability"] = attr.is_uniform() ? "uniform" : "varying";

    // Try to represent the value
    prop_json["value"] = ""; // Placeholder - actual value representation depends on type
  } else if (prop.is_relationship()) {
    prop_json["type"] = "relationship";
    const Relationship &rel = prop.get_relationship();
    nlohmann::json targets_json = nlohmann::json::array();
    if (rel.is_path()) {
      targets_json.push_back(rel.targetPath.full_path_name());
    } else if (rel.is_pathvector()) {
      for (const auto &target : rel.targetPathVector) {
        targets_json.push_back(target.full_path_name());
      }
    }
    prop_json["targets"] = targets_json;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json content;
  content["type"] = "text";
  content["text"] = prop_json.dump(2);
  result["content"].push_back(content);

  return true;
}

// Layer Metadata Tools

bool GetLayerMeta(Context &ctx, const nlohmann::json &args,
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
    err = "Layer not found: " + uuid;
    return false;
  }

  const USDLayer &usd_layer = ctx.layers.at(uuid);
  const auto &layer_metas = usd_layer.layer.metas();

  nlohmann::json meta_json = nlohmann::json::object();

  if (!layer_metas.doc.value.empty()) {
    meta_json["doc"] = layer_metas.doc.value;
  }
  if (!layer_metas.comment.value.empty()) {
    meta_json["comment"] = layer_metas.comment.value;
  }

  meta_json["metersPerUnit"] = layer_metas.metersPerUnit.get_value();
  meta_json["timeCodesPerSecond"] = layer_metas.timeCodesPerSecond.get_value();
  meta_json["framesPerSecond"] = layer_metas.framesPerSecond.get_value();
  meta_json["startTimeCode"] = layer_metas.startTimeCode.get_value();
  meta_json["endTimeCode"] = layer_metas.endTimeCode.get_value();

  if (!layer_metas.defaultPrim.str().empty()) {
    meta_json["defaultPrim"] = layer_metas.defaultPrim.str();
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json content;
  content["type"] = "text";
  content["text"] = meta_json.dump(2);
  result["content"].push_back(content);

  return true;
}

bool AddLayerMeta(Context &ctx, const nlohmann::json &args,
                  nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string meta_key = args["metaKey"];
  std::string meta_value = args["metaValue"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (meta_key.empty()) {
    err = "`metaKey` argument required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

  if (!ctx.layers.count(uuid)) {
    err = "Layer not found: " + uuid;
    return false;
  }

  USDLayer &usd_layer = ctx.layers.at(uuid);
  auto &layer_metas = usd_layer.layer.metas();

  // Handle different metadata keys
  if (meta_key == "doc") {
    layer_metas.doc = tinyusdz::value::StringData(meta_value);
  } else if (meta_key == "comment") {
    layer_metas.comment = tinyusdz::value::StringData(meta_value);
  } else if (meta_key == "defaultPrim") {
    layer_metas.defaultPrim = tinyusdz::value::token(meta_value);
  } else if (meta_key == "metersPerUnit") {
    double val = std::strtod(meta_value.c_str(), nullptr);
    layer_metas.metersPerUnit = val;
  } else if (meta_key == "timeCodesPerSecond") {
    double val = std::strtod(meta_value.c_str(), nullptr);
    layer_metas.timeCodesPerSecond = val;
  } else if (meta_key == "framesPerSecond") {
    double val = std::strtod(meta_value.c_str(), nullptr);
    layer_metas.framesPerSecond = val;
  } else if (meta_key == "startTimeCode") {
    double val = std::strtod(meta_value.c_str(), nullptr);
    layer_metas.startTimeCode = val;
  } else if (meta_key == "endTimeCode") {
    double val = std::strtod(meta_value.c_str(), nullptr);
    layer_metas.endTimeCode = val;
  } else {
    err = "Unknown layer metadata key: " + meta_key;
    return false;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully added/updated layer metadata: " + meta_key;
  result["content"].push_back(response);

  return true;
}

bool DelLayerMeta(Context &ctx, const nlohmann::json &args,
                  nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string meta_key = args["metaKey"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (meta_key.empty()) {
    err = "`metaKey` argument required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

  if (!ctx.layers.count(uuid)) {
    err = "Layer not found: " + uuid;
    return false;
  }

  USDLayer &usd_layer = ctx.layers.at(uuid);
  auto &layer_metas = usd_layer.layer.metas();

  // Clear metadata for given key
  if (meta_key == "doc") {
    layer_metas.doc.value = "";
  } else if (meta_key == "comment") {
    layer_metas.comment.value = "";
  } else if (meta_key == "defaultPrim") {
    layer_metas.defaultPrim = tinyusdz::value::token("");
  } else {
    err = "Cannot delete read-only or unknown layer metadata key: " + meta_key;
    return false;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully deleted layer metadata: " + meta_key;
  result["content"].push_back(response);

  return true;
}

// Prim Metadata Tools

bool GetPrimMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

  if (!ctx.layers.count(uuid)) {
    err = "Layer not found: " + uuid;
    return false;
  }

  const USDLayer &usd_layer = ctx.layers.at(uuid);
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested prim metadata retrieval not yet supported. Only root-level prims supported.";
    return false;
  }

  const auto &primspecs = usd_layer.layer.primspecs();
  auto it = primspecs.find(prim_part);
  if (it == primspecs.end()) {
    err = "Prim not found at path: " + prim_path_str;
    return false;
  }

  const PrimSpec &prim_spec = it->second;
  const auto &prim_metas = prim_spec.metas();

  nlohmann::json meta_json = nlohmann::json::object();

  if (prim_metas.has_active()) {
    meta_json["active"] = prim_metas.get_active();
  }
  if (prim_metas.has_hidden()) {
    meta_json["hidden"] = prim_metas.get_hidden();
  }
  if (prim_metas.has_kind()) {
    Kind k = prim_metas.get_kind_enum();
    if (k == Kind::Model) {
      meta_json["kind"] = "model";
    } else if (k == Kind::Group) {
      meta_json["kind"] = "group";
    } else if (k == Kind::Assembly) {
      meta_json["kind"] = "assembly";
    } else if (k == Kind::Component) {
      meta_json["kind"] = "component";
    } else if (k == Kind::Subcomponent) {
      meta_json["kind"] = "subcomponent";
    } else if (k == Kind::SceneLibrary) {
      meta_json["kind"] = "sceneLibrary";
    } else if (k == Kind::UserDef) {
      meta_json["kind"] = "userDef";
    } else if (k == Kind::Invalid) {
      meta_json["kind"] = "invalid";
    }
  }
  if (prim_metas.has_doc()) {
    meta_json["doc"] = prim_metas.get_doc().value;
  }
  if (prim_metas.has_comment()) {
    meta_json["comment"] = prim_metas.get_comment().value;
  }
  if (prim_metas.has_displayName()) {
    meta_json["displayName"] = prim_metas.get_displayName();
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json content;
  content["type"] = "text";
  content["text"] = meta_json.dump(2);
  result["content"].push_back(content);

  return true;
}

bool AddPrimMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string meta_key = args["metaKey"];
  std::string meta_value = args["metaValue"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
    return false;
  }

  if (meta_key.empty()) {
    err = "`metaKey` argument required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

  if (!ctx.layers.count(uuid)) {
    err = "Layer not found: " + uuid;
    return false;
  }

  USDLayer &usd_layer = ctx.layers.at(uuid);
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested prim metadata modification not yet supported. Only root-level prims supported.";
    return false;
  }

  auto &primspecs = usd_layer.layer.primspecs();
  auto it = primspecs.find(prim_part);
  if (it == primspecs.end()) {
    err = "Prim not found at path: " + prim_path_str;
    return false;
  }

  PrimSpec &prim_spec = it->second;
  auto &prim_metas = prim_spec.metas();

  if (meta_key == "active") {
    prim_metas.set_active(meta_value == "true" || meta_value == "1");
  } else if (meta_key == "hidden") {
    prim_metas.set_hidden(meta_value == "true" || meta_value == "1");
  } else if (meta_key == "doc") {
    prim_metas.set_doc(meta_value);
  } else if (meta_key == "comment") {
    prim_metas.set_comment(meta_value);
  } else if (meta_key == "displayName") {
    prim_metas.set_displayName(meta_value);
  } else if (meta_key == "kind") {
    if (meta_value == "model") {
      prim_metas.set_kind(Kind::Model);
    } else if (meta_value == "group") {
      prim_metas.set_kind(Kind::Group);
    } else if (meta_value == "assembly") {
      prim_metas.set_kind(Kind::Assembly);
    } else if (meta_value == "component") {
      prim_metas.set_kind(Kind::Component);
    } else if (meta_value == "subcomponent") {
      prim_metas.set_kind(Kind::Subcomponent);
    } else {
      err = "Invalid kind value: " + meta_value;
      return false;
    }
  } else {
    err = "Unknown prim metadata key: " + meta_key;
    return false;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully added/updated prim metadata: " + meta_key;
  result["content"].push_back(response);

  return true;
}

bool DelPrimMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string meta_key = args["metaKey"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
    return false;
  }

  if (meta_key.empty()) {
    err = "`metaKey` argument required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

  if (!ctx.layers.count(uuid)) {
    err = "Layer not found: " + uuid;
    return false;
  }

  USDLayer &usd_layer = ctx.layers.at(uuid);
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested prim metadata deletion not yet supported. Only root-level prims supported.";
    return false;
  }

  auto &primspecs = usd_layer.layer.primspecs();
  auto it = primspecs.find(prim_part);
  if (it == primspecs.end()) {
    err = "Prim not found at path: " + prim_path_str;
    return false;
  }

  PrimSpec &prim_spec = it->second;
  auto &prim_metas = prim_spec.metas();

  if (meta_key == "active") {
    prim_metas.remove_active();
  } else if (meta_key == "hidden") {
    prim_metas.remove_hidden();
  } else if (meta_key == "doc") {
    prim_metas.remove_doc();
  } else if (meta_key == "comment") {
    prim_metas.remove_comment();
  } else if (meta_key == "displayName") {
    prim_metas.remove_displayName();
  } else if (meta_key == "kind") {
    prim_metas.remove_kind();
  } else {
    err = "Unknown or cannot delete prim metadata key: " + meta_key;
    return false;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully deleted prim metadata: " + meta_key;
  result["content"].push_back(response);

  return true;
}

// Attribute Metadata Tools

bool GetAttrMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string attr_name = args["attrName"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty()) {
    err = "`primPath` argument required\n";
    return false;
  }

  if (attr_name.empty()) {
    err = "`attrName` argument required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

  if (!ctx.layers.count(uuid)) {
    err = "Layer not found: " + uuid;
    return false;
  }

  const USDLayer &usd_layer = ctx.layers.at(uuid);
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested attribute metadata retrieval not yet supported. Only root-level prims supported.";
    return false;
  }

  const auto &primspecs = usd_layer.layer.primspecs();
  auto it = primspecs.find(prim_part);
  if (it == primspecs.end()) {
    err = "PrimSpec not found: " + prim_part;
    return false;
  }

  const auto &prim_spec = it->second;
  const auto &props = prim_spec.props();
  if (!props.count(attr_name)) {
    err = "Attribute not found: " + attr_name;
    return false;
  }

  const auto &prop = props.at(attr_name);
  if (!prop.is_attribute()) {
    err = "Property is not an attribute: " + attr_name;
    return false;
  }

  const auto &attr = prop.get_attribute();
  const auto &attr_metas = attr.metas();

  nlohmann::json meta_json = nlohmann::json::object();

  // Get all available metadata
  if (attr_metas.has_comment()) {
    meta_json["comment"] = attr_metas.get_comment().value;
  }
  if (attr_metas.has_hidden()) {
    meta_json["hidden"] = attr_metas.get_hidden();
  }
  if (attr_metas.has_displayName()) {
    meta_json["displayName"] = attr_metas.get_displayName();
  }
  if (attr_metas.has_displayGroup()) {
    meta_json["displayGroup"] = attr_metas.get_displayGroup();
  }
  if (attr_metas.has_interpolation()) {
    // Convert interpolation enum to string
    std::string interp_str;
    switch (attr_metas.get_interpolation_enum()) {
      case Interpolation::Constant:
        interp_str = "constant";
        break;
      case Interpolation::Uniform:
        interp_str = "uniform";
        break;
      case Interpolation::Varying:
        interp_str = "varying";
        break;
      case Interpolation::Vertex:
        interp_str = "vertex";
        break;
      case Interpolation::FaceVarying:
        interp_str = "faceVarying";
        break;
      case Interpolation::Invalid:
        interp_str = "invalid";
        break;
    }
    meta_json["interpolation"] = interp_str;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json content;
  content["type"] = "text";
  content["text"] = meta_json.dump(2);
  result["content"].push_back(content);

  return true;
}

bool AddAttrMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string attr_name = args["attrName"];
  std::string meta_key = args["key"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty() || attr_name.empty() || meta_key.empty()) {
    err = "`primPath`, `attrName`, and `key` arguments required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

  if (!ctx.layers.count(uuid)) {
    err = "Layer not found: " + uuid;
    return false;
  }

  USDLayer &usd_layer = ctx.layers.at(uuid);
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested attribute modification not yet supported. Only root-level prims supported.";
    return false;
  }

  auto &primspecs = usd_layer.layer.primspecs();
  auto it = primspecs.find(prim_part);
  if (it == primspecs.end()) {
    err = "PrimSpec not found: " + prim_part;
    return false;
  }

  auto &prim_spec = it->second;
  auto &props = prim_spec.props();
  if (!props.count(attr_name)) {
    err = "Attribute not found: " + attr_name;
    return false;
  }

  auto &prop = props.at(attr_name);
  if (!prop.is_attribute()) {
    err = "Property is not an attribute: " + attr_name;
    return false;
  }

  auto &attr = prop.attribute();
  auto &attr_metas = attr.metas();

  // Update metadata by key
  if (meta_key == "comment") {
    std::string comment_str = args["value"];
    attr_metas.set_comment(comment_str);
  } else if (meta_key == "hidden") {
    bool hidden_val = false;
    if (args["value"].is_boolean()) {
      hidden_val = args["value"].get<bool>();
    } else if (args["value"].is_string()) {
      std::string val_str = args["value"].get<std::string>();
      hidden_val = (val_str == "true" || val_str == "1");
    }
    attr_metas.set_hidden(hidden_val);
  } else if (meta_key == "displayName") {
    std::string display_name = args["value"];
    attr_metas.set_displayName(display_name);
  } else if (meta_key == "displayGroup") {
    std::string display_group = args["value"];
    attr_metas.set_displayGroup(display_group);
  } else if (meta_key == "interpolation") {
    std::string interp_str = args["value"];
    Interpolation interp_val = Interpolation::Varying; // default
    if (interp_str == "constant") {
      interp_val = Interpolation::Constant;
    } else if (interp_str == "uniform") {
      interp_val = Interpolation::Uniform;
    } else if (interp_str == "varying") {
      interp_val = Interpolation::Varying;
    } else if (interp_str == "vertex") {
      interp_val = Interpolation::Vertex;
    } else if (interp_str == "faceVarying") {
      interp_val = Interpolation::FaceVarying;
    }
    attr_metas.set_interpolation_enum(interp_val);
  } else {
    err = "Unknown or read-only attribute metadata key: " + meta_key;
    return false;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully added/updated attribute metadata: " + meta_key;
  result["content"].push_back(response);

  return true;
}

bool DelAttrMeta(Context &ctx, const nlohmann::json &args,
                 nlohmann::json &result, std::string &err) {
  DCOUT("args " << args);

  std::string uuid = args["uuid"];
  std::string name = args["name"];
  std::string prim_path_str = args["primPath"];
  std::string attr_name = args["attrName"];
  std::string meta_key = args["key"];

  if (uuid.empty() && name.empty()) {
    err = "Either `name` or `uuid` arg required\n";
    return false;
  }

  if (prim_path_str.empty() || attr_name.empty() || meta_key.empty()) {
    err = "`primPath`, `attrName`, and `key` arguments required\n";
    return false;
  }

  if (uuid.empty()) {
    uuid = FindUUID(name, ctx.layers);
  }

  if (!ctx.layers.count(uuid)) {
    err = "Layer not found: " + uuid;
    return false;
  }

  USDLayer &usd_layer = ctx.layers.at(uuid);
  Path prim_path(prim_path_str, "");

  if (!prim_path.is_valid()) {
    err = "Invalid primPath: " + prim_path_str;
    return false;
  }

  std::string prim_part = prim_path.prim_part();
  if (prim_part.empty() || prim_part == "/") {
    err = "Invalid primPath";
    return false;
  }

  if (prim_part[0] == '/') {
    prim_part = prim_part.substr(1);
  }

  size_t slash_pos = prim_part.find('/');
  if (slash_pos != std::string::npos) {
    err = "Nested attribute modification not yet supported. Only root-level prims supported.";
    return false;
  }

  auto &primspecs = usd_layer.layer.primspecs();
  auto it = primspecs.find(prim_part);
  if (it == primspecs.end()) {
    err = "PrimSpec not found: " + prim_part;
    return false;
  }

  auto &prim_spec = it->second;
  auto &props = prim_spec.props();
  if (!props.count(attr_name)) {
    err = "Attribute not found: " + attr_name;
    return false;
  }

  auto &prop = props.at(attr_name);
  if (!prop.is_attribute()) {
    err = "Property is not an attribute: " + attr_name;
    return false;
  }

  auto &attr = prop.attribute();
  auto &attr_metas = attr.metas();

  // Clear metadata for given key
  if (meta_key == "comment") {
    attr_metas.remove_comment();
  } else if (meta_key == "hidden") {
    attr_metas.remove_hidden();
  } else if (meta_key == "displayName") {
    attr_metas.remove_displayName();
  } else if (meta_key == "displayGroup") {
    attr_metas.remove_displayGroup();
  } else if (meta_key == "interpolation") {
    attr_metas.remove_interpolation();
  } else {
    err = "Cannot delete unknown attribute metadata key: " + meta_key;
    return false;
  }

  result["content"] = nlohmann::json::array();
  nlohmann::json response;
  response["type"] = "text";
  response["text"] = "Successfully deleted attribute metadata: " + meta_key;
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
    j["name"] = "new_layer";
    j["description"] = "Create new empty or populated USD Layer with optional LayerMeta and PrimSpecs";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (must be unique)"}};
    schema["properties"]["layerMeta"] = {{"type", "string"}, {"description", "Optional LayerMeta as JSON string (e.g., {\"doc\": \"...\", \"comment\": \"...\"})"}};
    schema["properties"]["primSpecs"] = {{"type", "string"}, {"description", "Optional PrimSpecs as JSON array or object with fields: name (required), typeName, specifier (def/over/class), metadata"}};

    schema["required"] = nlohmann::json::array({"name"});

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
    j["name"] = "del_prim";
    j["description"] = "Delete a PrimSpec from specified Layer at specified primPath";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path of the prim to delete (e.g., /MyPrim)"}};

    schema["required"] = nlohmann::json::array({"primPath"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_prim";
    j["description"] = "Get PrimSpec information as JSON (USDJ) from specified Layer at specified primPath";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path to retrieve (e.g., /MyPrim)"}};

    schema["required"] = nlohmann::json::array({"primPath"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "add_prop";
    j["description"] = "Add property (attribute or relationship) to a PrimSpec at specified primPath";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};
    schema["properties"]["propertyName"] = {{"type", "string"}, {"description", "Name of the property to add"}};
    schema["properties"]["propertyType"] = {{"type", "string"}, {"enum", {"attribute", "relationship"}}, {"description", "Type of property"}};
    schema["properties"]["value"] = {{"type", "string"}, {"description", "Property value (JSON for attributes, path string for relationships)"}};
    schema["properties"]["valueFormat"] = {{"type", "string"}, {"enum", {"json", "ascii"}}, {"description", "Format of value for attributes"}};

    schema["required"] = nlohmann::json::array({"primPath", "propertyName", "propertyType", "value"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "del_prop";
    j["description"] = "Delete property (attribute or relationship) from a PrimSpec at specified primPath";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};
    schema["properties"]["propertyName"] = {{"type", "string"}, {"description", "Name of the property to delete"}};

    schema["required"] = nlohmann::json::array({"primPath", "propertyName"});

    j["inputSchema"] = schema;

    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "get_prop";
    j["description"] = "Get property (attribute or relationship) from a PrimSpec as JSON";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};
    schema["properties"]["propertyName"] = {{"type", "string"}, {"description", "Name of the property to get"}};

    schema["required"] = nlohmann::json::array({"primPath", "propertyName"});

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

  // Layer metadata tools
  {
    nlohmann::json j;
    j["name"] = "get_layer_meta";
    j["description"] = "Get metadata from a USD Layer (doc, comment, metersPerUnit, timeCodesPerSecond, framesPerSecond, startTimeCode, endTimeCode, defaultPrim)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};

    j["inputSchema"] = schema;
    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "add_layer_meta";
    j["description"] = "Add or update metadata field in a USD Layer";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};
    schema["properties"]["key"] = {{"type", "string"}, {"description", "Metadata key (doc, comment, metersPerUnit, timeCodesPerSecond, framesPerSecond, startTimeCode, endTimeCode, defaultPrim)"}};
    schema["properties"]["value"] = {{"description", "Metadata value (string for doc/comment/defaultPrim, number for numeric fields)"}};

    schema["required"] = nlohmann::json::array({"key", "value"});

    j["inputSchema"] = schema;
    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "del_layer_meta";
    j["description"] = "Delete or clear metadata field in a USD Layer";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};
    schema["properties"]["key"] = {{"type", "string"}, {"description", "Metadata key to delete"}};

    schema["required"] = nlohmann::json::array({"key"});

    j["inputSchema"] = schema;
    result["tools"].push_back(j);
  }

  // Prim metadata tools
  {
    nlohmann::json j;
    j["name"] = "get_prim_meta";
    j["description"] = "Get metadata from a PrimSpec (doc, comment, active, hidden, displayName, kind)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};

    schema["required"] = nlohmann::json::array({"primPath"});

    j["inputSchema"] = schema;
    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "add_prim_meta";
    j["description"] = "Add or update metadata field in a PrimSpec";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};
    schema["properties"]["key"] = {{"type", "string"}, {"description", "Metadata key (doc, comment, active, hidden, displayName, kind)"}};
    schema["properties"]["value"] = {{"description", "Metadata value (string for doc/comment/displayName/kind, boolean for active/hidden)"}};

    schema["required"] = nlohmann::json::array({"primPath", "key", "value"});

    j["inputSchema"] = schema;
    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "del_prim_meta";
    j["description"] = "Delete or clear metadata field in a PrimSpec";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};
    schema["properties"]["key"] = {{"type", "string"}, {"description", "Metadata key to delete"}};

    schema["required"] = nlohmann::json::array({"primPath", "key"});

    j["inputSchema"] = schema;
    result["tools"].push_back(j);
  }

  // Attribute metadata tools
  {
    nlohmann::json j;
    j["name"] = "get_attr_meta";
    j["description"] = "Get metadata from an Attribute (comment, hidden, displayName, displayGroup, interpolation)";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};
    schema["properties"]["attrName"] = {{"type", "string"}, {"description", "Attribute name"}};

    schema["required"] = nlohmann::json::array({"primPath", "attrName"});

    j["inputSchema"] = schema;
    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "add_attr_meta";
    j["description"] = "Add or update metadata field in an Attribute";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};
    schema["properties"]["attrName"] = {{"type", "string"}, {"description", "Attribute name"}};
    schema["properties"]["key"] = {{"type", "string"}, {"description", "Metadata key (comment, hidden, displayName, displayGroup, interpolation)"}};
    schema["properties"]["value"] = {{"description", "Metadata value (string for comment/displayName/displayGroup/interpolation, boolean for hidden)"}};

    schema["required"] = nlohmann::json::array({"primPath", "attrName", "key", "value"});

    j["inputSchema"] = schema;
    result["tools"].push_back(j);
  }

  {
    nlohmann::json j;
    j["name"] = "del_attr_meta";
    j["description"] = "Delete or clear metadata field in an Attribute";

    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"] = nlohmann::json::object();
    schema["properties"]["uuid"] = {{"type", "string"}, {"description", "Layer UUID (optional if name is provided)"}};
    schema["properties"]["name"] = {{"type", "string"}, {"description", "Layer name (optional if uuid is provided)"}};
    schema["properties"]["primPath"] = {{"type", "string"}, {"description", "USD prim path (e.g., /MyPrim)"}};
    schema["properties"]["attrName"] = {{"type", "string"}, {"description", "Attribute name"}};
    schema["properties"]["key"] = {{"type", "string"}, {"description", "Metadata key to delete"}};

    schema["required"] = nlohmann::json::array({"primPath", "attrName", "key"});

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
  } else if (tool_name == "new_layer") {
    DCOUT("new_layer");
    return NewLayer(ctx, args, result, err);
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
  } else if (tool_name == "del_prim") {
    DCOUT("del_prim");
    return DelPrim(ctx, args, result, err);
  } else if (tool_name == "get_prim") {
    DCOUT("get_prim");
    return GetPrim(ctx, args, result, err);
  } else if (tool_name == "add_prop") {
    DCOUT("add_prop");
    return AddProp(ctx, args, result, err);
  } else if (tool_name == "del_prop") {
    DCOUT("del_prop");
    return DelProp(ctx, args, result, err);
  } else if (tool_name == "get_prop") {
    DCOUT("get_prop");
    return GetProp(ctx, args, result, err);
  } else if (tool_name == "get_layer_meta") {
    DCOUT("get_layer_meta");
    return GetLayerMeta(ctx, args, result, err);
  } else if (tool_name == "add_layer_meta") {
    DCOUT("add_layer_meta");
    return AddLayerMeta(ctx, args, result, err);
  } else if (tool_name == "del_layer_meta") {
    DCOUT("del_layer_meta");
    return DelLayerMeta(ctx, args, result, err);
  } else if (tool_name == "get_prim_meta") {
    DCOUT("get_prim_meta");
    return GetPrimMeta(ctx, args, result, err);
  } else if (tool_name == "add_prim_meta") {
    DCOUT("add_prim_meta");
    return AddPrimMeta(ctx, args, result, err);
  } else if (tool_name == "del_prim_meta") {
    DCOUT("del_prim_meta");
    return DelPrimMeta(ctx, args, result, err);
  } else if (tool_name == "get_attr_meta") {
    DCOUT("get_attr_meta");
    return GetAttrMeta(ctx, args, result, err);
  } else if (tool_name == "add_attr_meta") {
    DCOUT("add_attr_meta");
    return AddAttrMeta(ctx, args, result, err);
  } else if (tool_name == "del_attr_meta") {
    DCOUT("del_attr_meta");
    return DelAttrMeta(ctx, args, result, err);
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
