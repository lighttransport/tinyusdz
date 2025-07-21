#include "mcp-resources.hh"
#include "pprinter.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

using namespace nlohmann;

namespace tinyusdz {
namespace tydra {
namespace mcp {

namespace {

static bool ListResourcesImpl(const Context &ctx, json &result) {
  for (const auto &res_uuid : ctx.resources) {
    
    if (!ctx.layers.count(res_uuid)) {
      continue;
    }

    json res_j;


    res_j["uri"] = ctx.layers.at(res_uuid).uri;
    res_j["name"] = ctx.layers.at(res_uuid).uri; // FIXME
    res_j["mimeType"] = "application/octet-stream"; // FIXME
    // TODO: size, title, description

    result["resources"].push_back(res_j);
  }


  return true;
}

} // namespace

bool GetResourcesList(const Context &ctx, nlohmann::json &result) {
  return ListResourcesImpl(ctx, result);
}

bool ReadResource(const Context &ctx, const std::string &uuid, nlohmann::json &result) {
  // TODO: multiple resources
  
  (void)uuid;
  (void)result;

  if (!ctx.resources.count(uuid)) {
    return false;
  }

  if (!ctx.layers.count(uuid)) {
    // This should not happen though.
    return false;
  }

  json res;
  res["uri"] = ctx.layers.at(uuid).uri;
  res["name"] = ctx.layers.at(uuid).name;
  res["mimeType"] = "text/plain";
  // TODO: title

  const Layer &layer = ctx.layers.at(uuid).layer;

  // TODO: binary
  std::string str = to_string(layer); // to USDA
  res["text"] = str;

  result["contents"] = json::array();
  result["contents"].push_back(res);

  return true;
}


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
