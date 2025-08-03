#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "prim-types.hh"

namespace tinyusdz {

namespace tydra {
namespace mcp {

struct Image
{
  std::string name; // optional
  std::string mimeType; // 'image/jpeg' or 'image/png' for now
  std::string data; // based64 encoded image
};

// Generic Asset(USD, textures, etc.)
struct MCPAsset
{
  std::string name;
  std::string data; // base64 encoded asset data
  std::string description; // optional
  Image preview; // preview image of the asset(optional)
  std::string uuid;
};

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

  // key = name
  std::unordered_map<std::string, MCPAsset> assets;

  std::vector<std::string> selected_assets;

  // key = name
  std::unordered_map<std::string, Screenshot> screenshots;
};

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz

