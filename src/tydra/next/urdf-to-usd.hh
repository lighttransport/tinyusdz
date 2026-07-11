// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present Light Transport Entertainment Inc.
//
/// @file urdf-to-usd.hh
/// @brief Build USD Physics + MuJoCo stages from URDF-derived JSON
///        (next-core port of src/tydra/urdf-to-usd.hh).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {
class Stage;
}  // namespace next

namespace tydra {
namespace next {

/// Pre-registered mesh geometry referenced from the robot JSON via `meshRef`.
/// Same field layout as the legacy tydra::URDFMeshBuffer.
struct URDFMeshBuffer {
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> uvs;
  std::vector<int32_t> indices;
};

/// Build a USD stage (next-core) from a compact URDF/MJCF export payload.
///
/// The JSON payload is renderer-neutral (same contract as the legacy
/// converter):
/// - links[].visuals/collisions[].geometry contain positions, indices,
///   normals, and UVs (or a `meshRef` into `mesh_buffers`).
/// - links[].collisions[].shape may contain native USD collision shapes
///   (`box`, `sphere`, `cylinder`, `capsule`, or `plane`).
/// - joints[] contain URDF joint metadata, axes, limits, dynamics, and link
///   names.
///
/// The generated stage authors standard UsdPhysics, MuJoCo (mjc:*), and
/// Newton (newton:*) API schema properties, matching the prim paths, types,
/// applied API schemas, attribute names/types, and relationships produced by
/// the legacy converter.
bool ConvertURDFJsonToUSDStage(
    const std::string &robot_json,
    ::tinyusdz::next::Stage *stage,
    std::string *warn = nullptr,
    std::string *err = nullptr);

bool ConvertURDFJsonToUSDStage(
    const std::string &robot_json,
    const std::map<std::string, URDFMeshBuffer> *mesh_buffers,
    ::tinyusdz::next::Stage *stage,
    std::string *warn = nullptr,
    std::string *err = nullptr);

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
