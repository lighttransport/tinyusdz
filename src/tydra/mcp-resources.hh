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

// for 'tools/list'
bool GetResourcesList(
  std::vector<std::string> &toolslist);


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
