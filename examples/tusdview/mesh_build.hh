// SPDX-License-Identifier: Apache-2.0
// tusdview - convert a Tydra RenderScene into a backend-neutral DrawScene.
#pragma once

#include "gpu_scene.hh"
#include "load_control.hh"
#include "tydra/render-data.hh"

namespace tusdview {

// Convert `rs` (already triangulated + single-indexed by the converter) into a
// renderable DrawScene: interleaved vertices, per-material submeshes, world
// transforms (from the node hierarchy), decoded RGBA8 textures and a world-space
// AABB. Unsupported items (UDIM/undecoded textures, empty meshes) are skipped
// and recorded in out->skipped.
//
// `ctrl` (optional) makes the build cancellable and bounds it by the triangle /
// vertex-byte budget; when a budget is hit the build stops and out->truncated
// is set (prevents per-frame freeze and VRAM thrashing on huge scenes).
void BuildDrawScene(const tinyusdz::tydra::RenderScene& rs, DrawScene* out,
                    LoadControl* ctrl = nullptr);

}  // namespace tusdview
