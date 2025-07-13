#pragma once

#include <vector>
#include <string>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json_fwd.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {
namespace tydra {
namespace mcp {

// for 'resources/list'
bool GetResourcesList(
  nlohmann::json &resources);

bool ReadResource(const std::string &uri, nlohmann::json &content);
  

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
