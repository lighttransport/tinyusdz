// SPDX-License-Identifier: Apache-2.0
// tusdview - convert a Tydra RenderScene into a backend-neutral DrawScene.
#pragma once

#include "gpu_scene.hh"
#include "load_control.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-converter.hh"

namespace tinyusdz {
class Stage;
}

namespace tusdview {

// Fill DrawMeshCPU::purpose from the source Stage prims. This is intentionally
// separate from RenderScene conversion because RenderMesh does not carry USD
// purpose today.
void ApplyMeshPurposes(const tinyusdz::Stage& stage, DrawScene* draw);

// Convert `rs` (already triangulated + single-indexed by the converter) into a
// renderable DrawScene: interleaved vertices, per-material submeshes, world
// transforms (from the node hierarchy), decoded RGBA8 textures and a world-space
// AABB. Unsupported items (UDIM/undecoded textures, empty meshes) are skipped
// and recorded in out->skipped.
//
// `ctrl` (optional) makes the build cancellable and bounds it by the triangle /
// vertex-byte budget; when a budget is hit the build stops and out->truncated
// is set (prevents per-frame freeze and VRAM thrashing on huge scenes).
// `stage` (optional) supplies the source Stage so per-mesh extras not carried by
// RenderMesh can be read -- currently blendshape in-between shapes (read from the
// BlendShape prims and remapped to DrawVertex order alongside the primary target).
void BuildDrawScene(const tinyusdz::tydra::RenderScene& rs, DrawScene* out,
                    LoadControl* ctrl = nullptr,
                    const tinyusdz::Stage* stage = nullptr);

// Build DrawVolumeCPU entries from RenderScene::volumes (UsdVol / OpenVDB).
void BuildDrawVolumes(const tinyusdz::tydra::RenderScene& rs, DrawScene* out);

// Streaming variant: run `converter.ConvertToRenderSceneStreaming` and build the
// DrawScene incrementally as elements are produced (mesh geometry as each mesh
// converts, world placement when the node hierarchy is built, textures and
// materials on completion). Produces the same DrawScene as
// ConvertToRenderScene + BuildDrawScene, while also fully populating `render`.
// Returns the conversion result. `ctrl` bounds the build by the triangle /
// vertex-byte budget (draw-side truncation; conversion still completes).
bool BuildDrawSceneStreaming(tinyusdz::tydra::RenderSceneConverter& converter,
                             const tinyusdz::tydra::RenderSceneConverterEnv& env,
                             tinyusdz::tydra::RenderScene* render, DrawScene* out,
                             LoadControl* ctrl = nullptr);

}  // namespace tusdview
