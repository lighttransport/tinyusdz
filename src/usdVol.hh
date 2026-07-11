// SPDX-License-Identifier: Apache-2.0
//
// Built-in OpenVDB (.vdb) import plugin for UsdVol.
//
// Import only (decode a .vdb into dense float grids). Writing .vdb is not
// supported. Uses the vendored, patched tinyvdb (src/external/tinyvdb).
//
// Supported: float / half (save-float-as-half) FloatTree grids
// (Tree4<float,5,4,3>) with None / ZIP / ACTIVE_MASK compression.
// Not supported: BLOSC compression, non-float grids, instanced grids.
//
// Typical usage (a UsdVol OpenVDBAsset field prim references a .vdb file):
//
//   def Volume "volume" {
//     rel field:density = </volume/density>
//     def OpenVDBAsset "density" {
//       asset filePath = @density.vdb@
//       token fieldName = "density"
//     }
//   }
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinyusdz {
namespace usdVol {

///
/// A single OpenVDB grid decoded into a dense float voxel array.
///
struct VDBGrid {
  std::string name;             // grid / field name (e.g. "density")
  std::string value_type;       // logical value type ("float"); data is float

  int origin[3] = {0, 0, 0};    // index-space origin (min active voxel coord)
  int dim[3] = {0, 0, 0};       // voxel dimensions (x, y, z)

  double voxel_size[3] = {1.0, 1.0, 1.0};        // world-space size of a voxel
  double world_translation[3] = {0.0, 0.0, 0.0}; // world-space origin offset

  float background = 0.0f;      // inactive (background) value

  // Dense voxels, length dim[0]*dim[1]*dim[2].
  // Layout: index = x + dim[0]*(y + dim[1]*z)  (x is contiguous).
  std::vector<float> data;
};

///
/// Decode a .vdb file into dense float grids.
///
/// `max_voxels` caps the dense voxel count of any single grid (0 = built-in
/// default) to bound memory. Returns true on success; on failure `err` holds a
/// message and `grids` may be partially filled.
///
bool ReadVDBFromFile(const std::string &filepath, std::vector<VDBGrid> *grids,
                     std::string *warn = nullptr, std::string *err = nullptr,
                     size_t max_voxels = 0);

///
/// Decode a .vdb from an in-memory buffer (e.g. via an asset resolver).
/// `uri` is used only for diagnostic messages.
///
bool ReadVDBFromMemory(const uint8_t *data, size_t len, const std::string &uri,
                       std::vector<VDBGrid> *grids, std::string *warn = nullptr,
                       std::string *err = nullptr, size_t max_voxels = 0);

}  // namespace usdVol
}  // namespace tinyusdz
