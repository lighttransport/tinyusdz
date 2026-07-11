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
#include "skinning.hh"       // RtSkinnedMeshUpload

namespace tinyusdz { namespace next { class Stage; class StageSession; } }

namespace tusdview {

// Load `path` (usd/usda/usdc) through the `next` loader, convert to a
// flat-shaded DrawScene (`draw`). Returns false with `*err` set on failure (or
// when no renderable mesh was produced). Worker-thread safe (no GPU access).
// `ctrl` (optional) caps triangles (LoadControl::maxTriangles) and supports
// cancellation; the scene is marked `truncated` when a cap is hit.
// `out_session` receives the persistent composed document used by UI edits,
// payload/variant recomposition, and per-frame animation.
bool LoadUSDViaNext(const std::string& path, const LoadOptions& opts,
                    DrawScene* draw, std::string* warn, std::string* err,
                    LoadControl* ctrl = nullptr,
                    std::shared_ptr<tinyusdz::next::StageSession>* out_session = nullptr);

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

// Same, for the LEGACY loader, which has no next Stage -- only the converted
// Tydra RenderScene. `--camera` used to be silently unavailable there ("need
// --next"), which meant the two loaders could not be pointed at one camera and so
// could not be compared frame-to-frame at all. The pose comes from the camera
// node's world matrix, so it is already evaluated at the load time code (Tydra
// bakes it); `time` is not re-sampled.
bool FindLegacyCamera(const tinyusdz::tydra::RenderScene& scene,
                      const std::string& name, NextCameraPose* out);

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

// Per-frame GPU bone matrices for `--next` (the Tydra path's BuildGpuSkinningFrame
// equivalent: same bone texture, same vertex shader, but posed from the retained
// next Stage instead of RenderScene::skeletons). Re-poses every skinned source
// mesh recorded in DrawScene::nextSkels at `time` and packs its block of the bone
// texture.
//
// The next loader world-bakes vertices into material batches, so each block is
// pre-composed with the mesh's geomBindTransform AND its world transform; the
// batch itself carries an identity bind matrix and absolute joint rows. Skinned
// mesh bounds are NOT refreshed (unlike the Tydra path): the next loader frees
// the CPU geometry after upload, so the load-time bounds stand.
// Returns false when the scene has no next-path skinning.
bool BuildNextSkinningFrame(const tinyusdz::next::Stage& stage, DrawScene* draw,
                            double time, SkinningFrameCPU* frame);

// Ray tracing cannot use the raster vertex shader's deform: the BLAS is built
// from actual vertex buffers, so the geometry itself has to move. Re-pose the
// retained REST vertices of every skinned/morphed mesh at `time` (morph first,
// then linear-blend skinning -- deform.glsl's order, from the same bone rows
// BuildNextSkinningFrame packs) and hand the caller per-mesh vertex buffers to
// upload. This replaces re-running the whole converter for each new time code,
// which is what the RT path used to do.
//
// Meshes whose CPU geometry was freed after upload are skipped; the RT path
// therefore has to retain it for deformable meshes (see App::freeCpuGeometry).
// Returns false when nothing deformed.
bool BuildNextRtDeformedVertices(
    const tinyusdz::next::Stage& stage, const DrawScene& draw, double time,
    const std::unordered_map<std::string, float>* blendOverride,
    std::vector<RtSkinnedMeshUpload>* out);

}  // namespace tusdview
