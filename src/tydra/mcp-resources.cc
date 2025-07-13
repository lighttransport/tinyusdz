#include "mcp-resources.hh"

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

bool GetResourcesList(nlohmann::json &result) {
  // TODO
  (void)result;

  return false;
}

bool ReadResource(const std::string &uri, nlohmann::json &result) {
  // TODO
  (void)uri;
  (void)result;

  return false;
}


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
