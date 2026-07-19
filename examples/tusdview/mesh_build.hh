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

// Derive the raster preview key light (draw->previewLightDir/Color/hasPreview
// Light) from draw->lights. Called by BuildDrawScene; also usable by the `next`
// loader, which assembles its own DrawScene.
void UpdatePreviewLight(DrawScene* draw);

// Structured tally of load-time diagnostics, derived from the converter's free
// -text warning blob plus the draw-side skipped list. Lets the app print a
// greppable, machine-parseable end-of-load summary (renderer-parity work) and
// lets the smoke harness distinguish a full material fallback (degraded) from a
// benign missing normal-map texture.
struct LoadDiagnostics {
  int degraded_material = 0;   // material rendered through a degraded surface
  int missing_texture = 0;     // a texture/image failed to load or resolve
  int unsupported_mtlx = 0;    // a MaterialX node could not be evaluated
  int skipped = 0;             // draw-side skipped items (UDIM/undecoded/empty)
  int other = 0;               // any other warning line
  std::vector<std::string> examples;  // a few representative lines (capped)
  int total() const {
    return degraded_material + missing_texture + unsupported_mtlx + skipped +
           other;
  }
  // Diagnostics that indicate a real rendering shortfall (as opposed to the
  // `other` bucket, which is dominated by tydra's informational MaterialX
  // progress messages). The app only emits a summary when this is non-zero.
  int actionable() const {
    return degraded_material + missing_texture + unsupported_mtlx + skipped;
  }
};

// Categorize the converter warning blob (newline-joined) + the draw-side skipped
// list into a LoadDiagnostics tally by matching the stable tydra message texts.
LoadDiagnostics CategorizeLoadWarnings(const std::string& warn_blob,
                                       const std::vector<std::string>& skipped);

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
                    const tinyusdz::Stage* stage = nullptr,
                    const TextureRuntimeOptions& textureOptions = {});

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
                             LoadControl* ctrl = nullptr,
                             const TextureRuntimeOptions& textureOptions = {});

// --- Texture post-passes, shared by the legacy and `--next` scene loaders ----
// Encode the already-decoded DrawScene textures to a GPU block format
// (`--texture-compress`), cap-gated by TextureRuntimeOptions::caps. Split out of
// ApplyTextureRuntimeOptions (which also does the size cap / byte budget) so the
// `--next` loader — whose own texture decoder already applies those — can call
// just this one.
void ApplyTextureCompression(const TextureRuntimeOptions& opt, DrawScene* out);

// Classify texture usage from the built materials, then build the content-aware
// CPU mip chains (and per-level compressed payloads when compression is on).
// Must run after the materials exist.
void FinalizeDrawTextures(const TextureRuntimeOptions& opt, DrawScene* out);

#if defined(TUSDVIEW_WITH_TEXTOOLS)
// Kept-compressed KTX2 passthrough from raw `.ktx2` bytes: parse it, adapt its
// blocks to what the device can sample (upload as-is / transcode uni / decode),
// carry its mip chain, and fill `tex` — no decode + re-encode. Returns false when
// the file can't be used (caller falls back to the normal decode path). Used by
// the `--next` loader, which reads the asset itself; the legacy path has an
// equivalent that starts from the tydra RenderScene's block buffer.
bool BuildKeptCompressedFromKtx2(const uint8_t* data, size_t size,
                                 const TextureRuntimeOptions& opt,
                                 DrawTextureCPU* tex);
#endif

}  // namespace tusdview
