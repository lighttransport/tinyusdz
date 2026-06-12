// SPDX-License-Identifier: Apache-2.0
// tusdview - load a USD file into a Stage and convert it to a Tydra RenderScene.
#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "gpu_scene.hh"  // DrawScene
#include "io-util.hh"  // tinyusdz::io::MMapFileHandle
#include "layer.hh"
#include "load_control.hh"
#include "stage.hh"
#include "tydra/render-data.hh"

namespace tusdview {

// How `payload` composition arcs are handled at load time.
enum class PayloadPolicy {
  LoadAll,    // compose all payloads eagerly
  DeferAll,   // skip all payloads; record them for on-demand loading
  Whitelist,  // load only payloads whose prim path is in `payloadWhitelist`
};

struct LoadOptions {
  // Compose USD composition arcs (subLayers/references/payload/inherits/
  // variants) on load. When false, only the root layer is loaded (legacy
  // behavior, keeps the mmap zero-copy fast path).
  bool composition{true};
  PayloadPolicy payloadPolicy{PayloadPolicy::DeferAll};
  // Also defer `references` arcs (--defer-references). Off by default: unlike
  // payload, USD semantics assume references always resolve, so most content
  // arrives through them and a deferred scene looks much emptier. Deferred
  // references share the payload whitelist/on-demand machinery (a prim path in
  // the whitelist loads both its arcs).
  bool deferReferences{false};
  // Prim paths (composed-layer full paths) whose deferred arcs to load when
  // payloadPolicy == Whitelist.
  std::set<std::string> payloadWhitelist;
};

// A payload/reference arc that was skipped during composition.
struct DeferredArc {
  std::string primPath;   // prim carrying the arc (composed-layer path)
  std::string assetPath;  // target asset (may be empty for internal arcs)
  const char* arc;        // "payload" | "reference"
};

// Composition state retained for on-demand payload loading. `rootLayer` is the
// post-sublayer snapshot of the opened file; recomposition restarts from it
// (CompositePayload strips payload metadata even for deferred arcs, so the
// composed Stage alone is not enough to load payloads later).
struct CompositionInfo {
  bool composed{false};
  std::shared_ptr<const tinyusdz::Layer> rootLayer;
  std::vector<std::string> searchPaths;
  std::vector<DeferredArc> deferred;         // still-unloaded payload/reference arcs
  std::set<std::string> loadedPayloads;      // whitelist accumulated so far
};

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
  // Null on the composition path (composition copies specs, so zero-copy
  // offsets would dangle).
  std::shared_ptr<tinyusdz::io::MMapFileHandle> mmap;
  CompositionInfo comp;
};

// Load `path` (usd/usda/usdc/usdz), convert to a RenderScene configured for
// single-index OpenGL/Vulkan rendering, and build the backend-neutral
// `DrawScene` (`draw`) in the same streaming pass (Tydra
// ConvertToRenderSceneStreaming): mesh geometry is interleaved as each mesh
// converts, world placement applied when the node hierarchy is built, and
// textures/materials decoded on completion. Returns false with `out->err`
// filled on failure (out->stage may still be partially populated for inspection).
//
// `opts` controls composition: by default the file's composition arcs are
// resolved (LIVRPS) with payloads deferred; deferred payloads are listed in
// `out->comp.deferred` and can be loaded later via RecomposeWithPayloads().
// .usdz archives always take the non-composition path for now.
//
// `ctrl` (optional) enables cancellation, progress reporting, a conversion time
// budget and a draw-side triangle/vertex budget. Safe to call on a worker
// thread (no GPU access).
//
// `rtPath` selects ray-tracer-friendly conversion: it disables the
// rasterization-only single-index dedup (build_vertex_indices=false), yielding a
// triangle-soup vertex/index layout that a BLAS consumes directly (still
// renderable by the raster path too).
bool LoadUSD(const std::string& path, const LoadOptions& opts, LoadedScene* out,
             DrawScene* draw, bool rtPath = false, LoadControl* ctrl = nullptr);

// Recompose `prev`'s retained root layer with `opts` (typically
// PayloadPolicy::Whitelist with an enlarged whitelist) and rebuild the
// Stage/RenderScene/DrawScene. `path` is the original file path (used for
// asset resolution and labeling). Worker-thread safe: `prev.rootLayer` is
// only read.
bool RecomposeWithPayloads(const std::string& path, const CompositionInfo& prev,
                           const LoadOptions& opts, LoadedScene* out,
                           DrawScene* draw, bool rtPath = false,
                           LoadControl* ctrl = nullptr);

}  // namespace tusdview
