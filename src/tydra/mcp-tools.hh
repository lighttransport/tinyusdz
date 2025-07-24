#pragma once

#include <vector>
#include <string>

#include "mcp-context.hh"

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
bool GetToolsList(
  Context &ctx,
  nlohmann::json &result);


// TODO: Batch call tools
bool CallTool(Context &ctx, const std::string &tool_name, const nlohmann::json &args, nlohmann::json &result, std::string &err);

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
