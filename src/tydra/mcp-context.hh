#pragma once

#include <memory>
#include <string>
#include <map>

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
  std::map<std::string, USDLayer> layers;

};

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz

