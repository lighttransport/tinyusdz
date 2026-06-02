// SPDX-License-Identifier: Apache-2.0
// tusdview - load a USD file into a Stage and convert it to a Tydra RenderScene.
#pragma once

#include <memory>
#include <string>

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

// Load `path` (usd/usda/usdc/usdz) and convert to a RenderScene configured for
// single-index OpenGL/Vulkan rendering. Returns false with `out->err` filled on
// failure (out->stage may still be partially populated for inspection).
//
// `ctrl` (optional) enables cancellation, progress reporting and a conversion
// time budget. Safe to call on a worker thread (no GPU access).
bool LoadUSD(const std::string& path, LoadedScene* out, LoadControl* ctrl = nullptr);

}  // namespace tusdview
