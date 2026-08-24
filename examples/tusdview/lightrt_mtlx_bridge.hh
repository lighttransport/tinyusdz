// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "gpu_scene.hh"

namespace tusdview {

constexpr int kLightRtOpenPBRVec4s =
    tinyusdz::tydra::kLightRtOpenPBRVec4s;
constexpr int kLightRtOpenPBRFloats =
    tinyusdz::tydra::kLightRtOpenPBRFloats;
// Floats 0-71 are the original 6-slot layout (slots 0-5 = base, metallic,
// roughness, normal, emissive, opacity). Floats 72-95 add UV transforms for
// slot 12 = occlusion, 13 = coat weight, 14 = coat color, 15 = coat roughness
// (slot*6 offsets, so they land at 72/78/84/90). Float 70/71 = occlusion
// tex scale/bias, 96/97/98 = occlusion / coat-weight / coat-roughness channel
// selectors, 99 = a second UV-set bitmask (1=occl, 2=coatW, 4=coatC, 8=coatR).
// Floats 100-123 carry scale/bias vec4s for coat weight, coat color, and coat
// roughness. 124-129 carry specular-color UV transform, 130 carries its UV set,
// 131 the specular-workflow flag, and 132-139 its scale/bias.
// 140-145 carry coat-normal UV transform, 146 its UV set, and 147-154 its
// scale/bias vectors.
constexpr int kRtMaterialTextureParamFloats = 155;
// Fixed-size Vulkan RT graph block. The header contains node count, the
// production OpenPBR output routes, and one reserved float; each node then
// occupies op, three input indices, three vec4 constants, resolved texture id,
// and UV scale/offset.
constexpr int kRtMaterialGraphMaxNodes = 64;
constexpr int kRtMaterialGraphOutputCount =
    MaterialXGraphRuntimeCPU::kOutputCount;
constexpr int kRtMaterialGraphHeaderFloats =
    2 + kRtMaterialGraphOutputCount;
// op + 3 input indices + 3 vec4 fallbacks + texture id + uv scale/offset.
constexpr int kRtMaterialGraphNodeFloats = 21;
constexpr int kRtMaterialGraphFloats =
    kRtMaterialGraphHeaderFloats +
    kRtMaterialGraphMaxNodes * kRtMaterialGraphNodeFloats;
constexpr int kRasterMaterialGraphImageCount = 8;
// Per-material texture-id slots in RtHostScene::matTex. 0-5 as above, then
// 6 = occlusion, 7 = coat weight, 8 = coat color, 9 = coat roughness,
// 10 = specular-workflow color.
// 11 = dedicated coat-normal map.
constexpr int kRtMaterialTexSlots = 12;
// Rows 20-24 add opacity sampling and scene-wide UDIM-atlas row selectors;
// rows 25-26 carry the independent roughness UV transform. Keep
// in lockstep with MaterialTexParam in every Vulkan mesh shader stage.
// Rows 27-28 carry the real-time PBR coat/occlusion constants.
// Rows 32-39 carry the UV transforms for the specular-color / coat-weight /
// coat-color / coat-roughness texture slots (two rows each, in that order);
// row 40 is (coatWeightChannel, coatRoughnessChannel, coatWeightUvSet,
// coatRoughnessUvSet), row 41 carries the remaining UV-set selectors, and rows
// 42-49 carry scale/bias for specular color and the three coat slots.
// Rows 50-53 carry coat-normal UV transform, scale/bias, UV set, and presence.
// Rows 67-68 carry the raster transmission approximation (weight/depth/
// dispersion and transmission color); rows 69-73 carry volume and subsurface.
constexpr int kRasterMaterialTextureParamVec4s = 74;
constexpr int kRasterMaterialTextureParamFloats =
    kRasterMaterialTextureParamVec4s * 4;

// Bake loader-adapted shader params into Tydra's backend-neutral real-time PBR
// block. Raster and RT consume the same resulting constant fallback around live
// DrawMaterialCPU semantic texture slots.
void BakeRealtimePbrMaterial(DrawMaterialCPU* mat);

// Apply a live OpenPBR constant edit without re-evaluating the retained
// MaterialX document. Callers only expose unconnected inputs; this function
// keeps the canonical block, legacy preview fields, and texture-free inspector
// parameter records synchronized.
void ApplyOpenPBRMaterialConstants(
    DrawMaterialCPU* mat, const DrawLightRtOpenPBRCPU& constants);

// Switch a dual-authored/fallback material to a session-local, connection-free
// OpenPBR preview suitable for interactive constant editing.
void MakeConstantOpenPBRMaterial(DrawMaterialCPU* mat);

// Bake texture/procedural MaterialX graph outputs into the existing semantic
// texture table when Tydra did not extract a direct slot. The retained graph
// IR is also packed separately for descriptor-indexed runtime evaluation; this
// bake remains the raster/legacy compatibility path.
void BakeMaterialXGraphTextures(DrawMaterialCPU* mat, DrawScene* scene);

// Parse the retained JSON graph into the canonical runtime IR. Returns false
// for malformed graphs; callers retain the semantic/bake fallback in that case.
bool CompileMaterialXGraphRuntime(DrawMaterialCPU* mat, std::string* err);

// Compatibility entry point retained for out-of-tree callers.
void BakeLightRtOpenPBR(DrawMaterialCPU* mat);

// Evaluate a MaterialX XML document through LightRT's MaterialX graph evaluator
// into tusdview's LightRT/OpenPBR constant block. Image nodes are resolved from
// baseDir and use the vendored texture cache; this overload is useful when a
// caller owns an asset-relative MaterialX document.
bool EvaluateMaterialXStringToLightRtOpenPBRWithBaseDir(
    const char* xml, const char* materialName, const char* baseDir,
    tydra::LightRtOpenPBRParams* out, std::string* err);

// UV-aware variant used by graph baking/runtime probes. `u` and `v` are
// normalized texture coordinates; the legacy overload samples the center.
bool EvaluateMaterialXStringToLightRtOpenPBRAtUv(
    const char* xml, const char* materialName, const char* baseDir, float u,
    float v, tydra::LightRtOpenPBRParams* out, std::string* err);

// Convenience wrapper for callers without an asset directory.
bool EvaluateMaterialXStringToLightRtOpenPBR(const char* xml,
                                             const char* materialName,
                                             tydra::LightRtOpenPBRParams* out,
                                             std::string* err);

// Pack DrawLightRtOpenPBRCPU into the vec4-friendly SSBO/kernel layout consumed
// by the Vulkan, CUDA and HIP RT preview paths. `dst` must hold
// kLightRtOpenPBRFloats floats.
void PackLightRtOpenPBR(const DrawMaterialCPU& mat, float* dst);

// Pack material texture transforms, channel selectors, and scale/bias values
// into the RT kernel layout. `dst` must hold kRtMaterialTextureParamFloats
// floats. The order must match raytracer_kernel_src.txt.
void PackRtMaterialTextureParams(const DrawMaterialCPU& mat, float* dst);

void PackMaterialXGraphRuntime(
    const DrawMaterialCPU& mat, float* dst,
    const std::vector<int>* sourceToTable = nullptr);

// Raster uses a bounded per-material image table rather than the RT scene-wide
// texture table. Image-node texture ids are rewritten to local slots [0, 7].
// UDIM nodes use a negative slot and carry their source atlas row in value.w.
void PackRasterMaterialXGraphRuntime(const DrawMaterialCPU& mat, float* dst);

// Pack the Vulkan raster/tessellation material texture SSBO layout. `dst` must
// hold kRasterMaterialTextureParamFloats floats. The order must match
// vk/shaders/mesh*.{vert,frag,tese}.
void PackRasterMaterialTextureParams(const DrawMaterialCPU& mat, float* dst);

}  // namespace tusdview
