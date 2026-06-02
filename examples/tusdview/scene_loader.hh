// SPDX-License-Identifier: Apache-2.0
// tusdview - load a USD file into a Stage and convert it to a Tydra RenderScene.
#pragma once

#include <memory>
#include <string>

#include "gpu_scene.hh"  // DrawScene
#include "io-util.hh"  // tinyusdz::io::MMapFileHandle
#include "load_control.hh"
#include "stage.hh"
#include "tydra/render-data.hh"

namespace tusdview {

// Holds both the parsed Stage (for the hierarchy browser / property inspector)
// and the converted RenderScene (for rendering). Both must stay alive together.
struct LoadedScene {
  tinyusdz::Stage stage;
  tinyusdz::tydra::RenderScene render;
  std::string filepath;
  std::string warn;
  std::string err;
  bool ok{false};
  // Memory-mapped file handle kept alive for the Stage's lifetime (zero-copy
  // USDC arrays reference this mapping). Unmapped when this LoadedScene dies.
  std::shared_ptr<tinyusdz::io::MMapFileHandle> mmap;
};

// Load `path` (usd/usda/usdc/usdz), convert to a RenderScene configured for
// single-index OpenGL/Vulkan rendering, and build the backend-neutral
// `DrawScene` (`draw`) in the same streaming pass (Tydra
// ConvertToRenderSceneStreaming): mesh geometry is interleaved as each mesh
// converts, world placement applied when the node hierarchy is built, and
// textures/materials decoded on completion. Returns false with `out->err`
// filled on failure (out->stage may still be partially populated for inspection).
//
// `ctrl` (optional) enables cancellation, progress reporting, a conversion time
// budget and a draw-side triangle/vertex budget. Safe to call on a worker
// thread (no GPU access).
bool LoadUSD(const std::string& path, LoadedScene* out, DrawScene* draw,
             LoadControl* ctrl = nullptr);

}  // namespace tusdview
