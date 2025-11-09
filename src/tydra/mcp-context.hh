#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "prim-types.hh"
#include "../layer.hh"

namespace tinyusdz {

namespace tydra {
namespace mcp {

struct Image
{
  std::string name; // optional
  std::string mimeType; // 'image/jpeg' or 'image/png' for now
  std::string data; // based64 encoded image
};

struct AssetSelection
{
  std::string asset_name;

  // Instance and transform parameters
  int instance_id = 0; // Instance ID for the asset
  std::array<float, 3> position = {0.0f, 0.0f, 0.0f}; // x, y, z
  std::array<float, 3> scale = {1.0f, 1.0f, 1.0f}; // x, y, z
  std::array<float, 3> rotation = {0.0f, 0.0f, 0.0f}; // x, y, z Euler angles in degrees

};

// Generic Asset(USD, textures, etc.)
struct MCPAsset
{
  std::string name;
  std::string data; // base64 encoded asset data
  std::string description; // optional
  Image preview; // preview image of the asset(optional)
  std::string uuid;
  
  
  // Geometry and bounding box parameters
  std::array<float, 3> pivot_position = {0.0f, 0.0f, 0.0f}; // pivot point for rotation and scaling
  std::array<float, 3> bmin = {-1.0f, -1.0f, -1.0f}; // bounding box minimum
  std::array<float, 3> bmax = {1.0f, 1.0f, 1.0f}; // bounding box maximum
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

  std::vector<AssetSelection> selected_assets;

  // key = name
  std::unordered_map<std::string, Screenshot> screenshots;
};

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz

