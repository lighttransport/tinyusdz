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

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gpu_scene.hh"      // DrawScene
#include "load_control.hh"   // LoadControl
#include "scene_loader.hh"   // LoadOptions

namespace tinyusdz { namespace next { class Stage; } }

namespace tusdview {

// Load `path` (usd/usda/usdc) through the `next` loader, convert to a
// flat-shaded DrawScene (`draw`). Returns false with `*err` set on failure (or
// when no renderable mesh was produced). Worker-thread safe (no GPU access).
// `ctrl` (optional) caps triangles (LoadControl::maxTriangles) and supports
// cancellation; the scene is marked `truncated` when a cap is hit.
// `out_stage` (optional) receives the composed lazy stage so the caller can keep
// it alive for per-frame animation (blendshape weights) -- the lazy mmap arrays
// stay resident but unmaterialized, so this is cheap.
bool LoadUSDViaNext(const std::string& path, const LoadOptions& opts,
                    DrawScene* draw, std::string* warn, std::string* err,
                    LoadControl* ctrl = nullptr,
                    std::shared_ptr<tinyusdz::next::Stage>* out_stage = nullptr);

// A USD camera resolved from the `next` stage, in world space: `eye` position,
// unit `forward` (the camera looks down its local -Z) and `up` (local +Y), and
// the vertical field of view in degrees (from focalLength / verticalAperture).
struct NextCameraPose {
  float eye[3]{0, 0, 0};
  float forward[3]{0, 0, -1};
  float up[3]{0, 1, 0};
  float fovYDeg{60.0f};
  float zNear{0.1f};   // from the camera's clippingRange (scene units)
  float zFar{1.0e6f};
};

// Find the Camera prim named (or path-suffixed by) `name` in `stage` and fill
// `*out` with its world-space pose at `time`. Returns false if no such camera
// exists. Used to drive the viewer's orbit camera from a scene camera (the
// auto-fit framing is useless on vast scenes like Caldera).
bool FindNextCamera(const tinyusdz::next::Stage& stage, const std::string& name,
                    double time, NextCameraPose* out);

// Per-frame GPU-morph coefficients for `--next` instanced prototypes: for each
// draw mesh that carries morph channels, resolve its blendshape weights from
// `stage` at `time` (manual `blendOverride` weights, when set, replace animated
// ones by name) and evaluate the per-channel coefficients the instanced raster
// shader sums. Emits (meshIndex, coeffs) only for morphed meshes. Mirrors
// BuildMorphChannelWeights/EvalMorphChannelCoeffs for the next stage.
void BuildNextMorphWeights(
    const tinyusdz::next::Stage& stage, const DrawScene& draw, double time,
    const std::unordered_map<std::string, float>* blendOverride,
    std::vector<std::pair<int, std::vector<float>>>* out);

}  // namespace tusdview
