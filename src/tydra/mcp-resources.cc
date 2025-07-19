#include "mcp-resources.hh"

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

bool ListResourcesImpl(const Context &ctx, json &result) {
  for (const auto &res_uuid : ctx.resources) {
    
    if (!result.layers.count(res_uuid)) {
      continue;
    }

    json res_j;


    res_j["uri"] = result.layers[res_uuid].uri;
    res_j["name"] = result.layers[res_uuid].uri; // FIXME
    res_j["mimeType"] = "application/octet-stream"; // FIXME

    result["resources"].push_back(res_j);
  }

}

bool GetResourcesList(const Context &ctx, nlohmann::json &result) {
  return ListResourcesImpl(ctx, result);
}

bool ReadResource(const Contect &ctx, const std::string &uuid, nlohmann::json &result) {
  // TODO
  (void)ctx;
  (void)uri;
  (void)result;

  return false;
}


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
