#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_set>

#include "core/prim.hh"
#include "../layer.hh"
#include "../stage.hh"
#include "../tiny-hashmap.hh"

namespace tinyusdz {

namespace tydra {

// Forward declare JS engine state (defined in js-script.hh)
struct JSEngineState;

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
  // ---- Composed Stage (for scene graph queries) ----
  std::unique_ptr<Stage> stage;
  bool stage_loaded{false};

  // ---- Uncomposed Layers (for editing) ----
  // key = UUID
  tinyusdz::HashMap<std::string, USDLayer> layers;

  // ---- Viewer workflow assets ----
  // key = name
  tinyusdz::HashMap<std::string, MCPAsset> assets;

  std::vector<AssetSelection> selected_assets;

  // key = name
  tinyusdz::HashMap<std::string, Screenshot> screenshots;

  // ---- QuickJS engine (persistent per session) ----
  std::unique_ptr<JSEngineState> js_engine;

  // ---- Utility ----
  std::string session_id;
};

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz

