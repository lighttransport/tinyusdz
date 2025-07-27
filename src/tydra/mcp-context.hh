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
  std::string name;
  std::string description; // optional
  Layer layer;
};

struct Screenshot
{
  std::string uuid;
  std::string mimeType;
  std::string data; // base64 encoded image data.
};

struct Context
{

  // loaded USD assets
  // key = UUID
  std::unordered_map<std::string, USDLayer> layers;

  // key = URI, value = UUID
  std::unordered_map<std::string, std::string> resources;

  // key = name
  std::unordered_map<std::string, Screenshot> screenshots;
};

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz

