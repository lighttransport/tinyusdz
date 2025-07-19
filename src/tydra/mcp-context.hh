#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "prim-types.hh"

namespace tinyusdz {

namespace tydra {
namespace mcp {

struct USDLayer
{
  std::string uri;
  Layer layer;
};

struct Context
{

  // loaded USD assets
  // key = UUID
  std::unordered_map<std::string, USDLayer> layers;

  std::unordered_set<std::string> resources;
};

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz

