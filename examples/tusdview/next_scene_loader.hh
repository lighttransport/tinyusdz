// SPDX-License-Identifier: Apache-2.0
// tusdview - load a USD file via the `next` lazy loader + tydra-next converter
// into a flat-shaded DrawScene.
//
// This is the large-scene preview path: the `next` loader composes
// references/payloads with lazy mmap arrays, and the tydra-next
// RenderSceneConverter triangulates into a RenderScene we adapt to the
// backend-neutral DrawScene the GL/Vulkan renderers already consume. Geometry
// only (default gray material + the renderer's default lighting) for now;
// instancing/materials/streaming are follow-ups.
#pragma once

#include <string>

#include "gpu_scene.hh"      // DrawScene
#include "load_control.hh"   // LoadControl
#include "scene_loader.hh"   // LoadOptions

namespace tusdview {

// Load `path` (usd/usda/usdc) through the `next` loader, convert to a
// flat-shaded DrawScene (`draw`). Returns false with `*err` set on failure (or
// when no renderable mesh was produced). Worker-thread safe (no GPU access).
// `ctrl` (optional) caps triangles (LoadControl::maxTriangles) and supports
// cancellation; the scene is marked `truncated` when a cap is hit.
bool LoadUSDViaNext(const std::string& path, const LoadOptions& opts,
                    DrawScene* draw, std::string* warn, std::string* err,
                    LoadControl* ctrl = nullptr);

}  // namespace tusdview
