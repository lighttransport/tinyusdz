// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "minijson.hh"

namespace lightusd {
namespace tydra {
namespace import_asset {
using Json = ::lightusd::minijson::Value;
struct Options {
  std::string format = "auto";
  std::string up_axis = "Z";
  bool allow_missing = false;
  bool tessellate_collision_shapes = false;
  size_t max_memory = size_t(1024) * 1024 * 1024;
  size_t max_input = size_t(512) * 1024 * 1024;
  size_t max_depth = 64;
  std::vector<std::string> asset_dirs;
  std::map<std::string, std::string> packages;
  // Resolve and read an asset, returning its resolved identifier. False may
  // carry a diagnostic; no direct filesystem fallback follows a callback.
  std::function<bool(const std::string&, const std::string&,
                     std::vector<uint8_t>*, std::string*, std::string*)>
      read;
};
struct Result {
  Json payload;
  size_t memory_estimate = 0;  // OBJ conversion budget, including the input.
  std::map<std::string, std::vector<uint8_t>> assets;
  std::string warnings;
};
struct Reader {
  explicit Reader(const Options& o) : options(o) {}
  const Options& options;
  size_t bytes = 0;
  std::string warnings, failure, root_source;
  bool allow_unanchored = true;
  bool Read(const std::string& name, const std::string& anchor,
            std::vector<uint8_t>* data, std::string* resolved,
            std::string* err);
  bool Charge(size_t amount, std::string* err);
  bool ChargeArray(size_t count, size_t stride, std::string* err);
  void Warn(const std::string& message);
};
struct Mesh {
  std::string name;
  std::string type = "Mesh";
  std::vector<float> positions, normals, uvs, colors;
  std::vector<int32_t> indices, counts, materials;
  std::vector<int32_t> source_vertices, joint_indices;
  std::vector<float> joint_weights;
  int32_t influences = 0;
};
struct OBJ {
  std::vector<Mesh> meshes;
  Json materials = Json::array();
};
template <class T>
Json Array(const std::vector<T>& values) {
  Json result = Json::array();
  result.reserve(values.size());
  for (const T& value : values) result.push_back(Json(value));
  return result;
}
bool ReadOBJ(std::string_view text, const std::string& source, Reader& reader,
             bool triangulate, OBJ* obj, std::string* err);
// Typed geometry path for large OBJ inputs; Result carries only materials/assets.
bool BuildOBJScene(std::string_view text, const std::string& source,
                   const Options& options, OBJ* obj, Result* result,
                   std::string* err);
bool BuildOBJPayload(std::string_view text, const std::string& source,
                     const Options& options, Result* result, std::string* err);
bool BuildOBJPayloadFile(const std::string& filename, const Options& options,
                         Result* result, std::string* err);
Json MeshJSON(const Mesh& mesh);
bool BuildPayload(const std::string& text, const std::string& source,
                  const Options& options, Result* result, std::string* err);
bool BuildPayloadFile(const std::string& filename, const Options& options,
                      Result* result, std::string* err);
// Used by standalone OBJ imports and robot frontends to retain material maps.
bool GatherTextures(Reader& reader, Result* result, std::string* err);
}  // namespace import_asset
}  // namespace tydra
}  // namespace lightusd
