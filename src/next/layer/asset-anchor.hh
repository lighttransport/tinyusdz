// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - asset-path anchors.
//
// A relative asset path (`asset inputs:file = @../tex/albedo.png@`) is anchored
// to the DIRECTORY OF THE LAYER THAT AUTHORED IT, not to the stage's root layer.
// Production scenes rely on this: a look layer nested several directories deep
// reaches its textures with `../..`, and resolving those against the root layer
// yields a nonexistent path (the texture is then silently dropped).
//
// Composition flattens prims from many layers into one, so the source layer is
// lost unless it is recorded. Each loaded layer's directory is interned here and
// the resulting id is stamped onto that layer's PrimSpecs
// (`PrimSpec::asset_anchor_id`), then carried through composition. This mirrors
// the eager path's per-prim `current_working_path` stamp.
//
// Ids are stable for the life of the process and shared across stages: an anchor
// is a property of the layer file itself, so a layer cached by the LayerRegistry
// and referenced from several stages always has the same anchor.
#pragma once

#include <cstdint>
#include <string>

namespace lightusd {
namespace next {

/// Intern an anchor directory and return its id. The empty string maps to 0
/// ("no anchor" -- the consumer falls back to its default base directory).
/// Thread-safe: composition loads and stamps layers from several threads.
uint32_t InternAssetAnchor(const std::string& dir);

/// The directory for `id`, or "" when `id` is 0 / unknown.
const std::string& AssetAnchorPath(uint32_t id);

}  // namespace next
}  // namespace lightusd
